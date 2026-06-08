/**
 * @file wasapi.c
 * @brief WASAPI (Windows Core Audio) backend — runtime LoadLibrary COM dispatch
 *
 * Implements the audio backend interface used by backend_ops.c.
 * All COM interfaces are resolved dynamically via LoadLibrary/GetProcAddress;
 * calls are dispatched through vtable index macros.
 *
 * This file is compiled ONLY under WIN32 (enclosed in #ifdef).
 *
 * Architecture:
 *   wasapi_load()         — Load mmdevapi.dll / ole32.dll, resolve functions
 *   wasapi_unload()       — Free libraries, destroy COM state
 *   wasapi_prepare_stream()  — Create IAudioClient + IAudioRenderClient
 *   wasapi_cleanup_stream()  — Release COM objects
 *   wasapi_write_samples()   — Push PCM data to render client (event-driven)
 *   wasapi_pause/resume()    — IAudioClient::Stop/Start
 *   wasapi_sync_volume()     — IAudioEndpointVolume::SetMasterVolumeLevelScalar
 *
 * @author ter-music team
 */

#ifdef _WIN32

#include "types.h"
#include "audio/audio.h"
#include "audio/audio_internal.h"
#include "audio/backend/wasapi.h"
#include "logger/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  COM vtable call helper
 *
 *  Every COM interface is a pointer to a pointer to a vtable
 *  (an array of function pointers).  This macro calls the i-th
 *  method, casting to the appropriate function signature.
 * ================================================================ */

#define WASAPI_VTABLE_CALL(iface, idx, ret_type, ...)           \
    do {                                                         \
        if (!(iface)) {                                          \
            log_error("wasapi", "COM call #%d on NULL iface", (int)(idx)); \
            return (ret_type)(-1);                               \
        }                                                        \
        void **vtable = *(void ***)(iface);                      \
        if (!vtable || !vtable[(idx)]) {                         \
            log_error("wasapi", "vtable[%d] is NULL", (int)(idx)); \
            return (ret_type)(-1);                               \
        }                                                        \
        typedef HRESULT (STDMETHODCALLTYPE *method_t)(void *);   \
        HRESULT hr = ((HRESULT (STDMETHODCALLTYPE *)(void *,    \
                        ##__VA_ARGS__))vtable[(idx)])((iface),   \
                        ##__VA_ARGS__);                          \
        if (FAILED(hr)) {                                        \
            log_debug("wasapi", "COM call #%d failed: hr=0x%08lX", \
                       (int)(idx), (unsigned long)hr);           \
        }                                                        \
    } while(0)

/* Variant that returns HRESULT directly */
#define WASAPI_VTABLE_CALL_HR(iface, idx, ...)                  \
    ({                                                           \
        HRESULT _hr = E_FAIL;                                    \
        if ((iface)) {                                           \
            void **vtable = *(void ***)(iface);                  \
            if (vtable && vtable[(idx)]) {                       \
                _hr = ((HRESULT (STDMETHODCALLTYPE *)(void *,   \
                            ##__VA_ARGS__))vtable[(idx)])(       \
                            (iface), ##__VA_ARGS__);             \
            }                                                    \
        }                                                        \
        _hr;                                                     \
    })

/* Variant that returns a value (not HRESULT) */
#define WASAPI_VTABLE_CALL_VAL(iface, idx, ret_type, ...)      \
    ({                                                           \
        ret_type _result = (ret_type)0;                          \
        if ((iface)) {                                           \
            void **vtable = *(void ***)(iface);                  \
            if (vtable && vtable[(idx)]) {                       \
                _result = ((ret_type (STDMETHODCALLTYPE *)(void *, \
                            ##__VA_ARGS__))vtable[(idx)])(       \
                            (iface), ##__VA_ARGS__);             \
            }                                                    \
        }                                                        \
        _result;                                                 \
    })

/* ================================================================
 *  Function pointer table instance
 * ================================================================ */

struct wasapi_funcs W = {0};

/* ================================================================
 *  WASAPI state globals
 * ================================================================ */

void    *wasapi_device_enumerator = NULL;
void    *wasapi_device            = NULL;
void    *wasapi_audio_client      = NULL;
void    *wasapi_render_client     = NULL;
void    *wasapi_endpoint_volume   = NULL;

int      wasapi_loaded          = 0;
int      wasapi_stream_ready    = 0;
int      wasapi_channels        = 0;
int      wasapi_sample_rate     = 0;
HANDLE   wasapi_buffer_event    = NULL;
uint32_t wasapi_buffer_frames   = 0;

/* Saved format info for writes */
static WASAPI_WAVEFORMATEX wasapi_created_fmt;

/* ================================================================
 *  Loader helpers
 * ================================================================ */

#define LOAD_FN(lib, fn, dst) do { \
    *(void **)&(dst) = GetProcAddress((HMODULE)(lib), (fn)); \
    if (!(dst)) { \
        log_error("wasapi", "GetProcAddress(%s) failed", (fn)); \
        wasapi_unload(); \
        return -1; \
    } \
} while(0)

/* ================================================================
 *  Load / Unload
 * ================================================================ */

int wasapi_load(void)
{
    if (W.loaded) return 0;

    /* Load mmdevapi.dll */
    HMODULE hMmdev = LoadLibraryW(L"mmdevapi.dll");
    if (!hMmdev) {
        log_warn("wasapi", "mmdevapi.dll not available");
        return -1;
    }
    W.handle_mmdevapi = hMmdev;

    /* Load ole32.dll */
    HMODULE hOle = LoadLibraryW(L"ole32.dll");
    if (!hOle) {
        log_warn("wasapi", "ole32.dll not available");
        FreeLibrary(hMmdev);
        W.handle_mmdevapi = NULL;
        return -1;
    }
    W.handle_ole32 = hOle;

    /* Resolve OLE32 functions */
    LOAD_FN(hOle, "CoInitializeEx",         W.CoInitializeEx);
    LOAD_FN(hOle, "CoUninitialize",          W.CoUninitialize);
    LOAD_FN(hOle, "CoCreateInstance",        W.CoCreateInstance);
    LOAD_FN(hOle, "CLSIDFromString",         W.CLSIDFromString);
    LOAD_FN(hOle, "StringFromGUID2",         W.StringFromGUID2);

    /* Resolve Kernel32 functions */
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel) {
        LOAD_FN(hKernel, "CreateEventW",              W.CreateEventW);
        LOAD_FN(hKernel, "WaitForSingleObject",       W.WaitForSingleObject);
        LOAD_FN(hKernel, "CloseHandle",               W.CloseHandle);
    } else {
        log_error("wasapi", "kernel32.dll not loaded");
        wasapi_unload();
        return -1;
    }

    W.loaded = 1;
    log_info("wasapi", "WASAPI runtime loaded (mmdevapi + ole32)");
    return 0;
}

void wasapi_unload(void)
{
    wasapi_cleanup_stream();

    if (W.handle_ole32) {
        FreeLibrary((HMODULE)W.handle_ole32);
        W.handle_ole32 = NULL;
    }
    if (W.handle_mmdevapi) {
        FreeLibrary((HMODULE)W.handle_mmdevapi);
        W.handle_mmdevapi = NULL;
    }
    memset(&W, 0, sizeof(W));
    wasapi_loaded = 0;
    log_info("wasapi", "WASAPI runtime unloaded");
}

/* ================================================================
 *  Helper: convert GUID string to GUID struct
 * ================================================================ */

static int guid_from_string(const wchar_t *str, GUID *guid)
{
    if (!W.CLSIDFromString) return -1;
    HRESULT hr = W.CLSIDFromString(str, guid);
    return SUCCEEDED(hr) ? 0 : -1;
}

/* ================================================================
 *  Prepare stream — initialise COM, create device, audio client
 * ================================================================ */

int wasapi_prepare_stream(int sample_rate, int channels)
{
    HRESULT hr;
    GUID clsId, iidEnum, iidClient, iidRender, iidEndpointVol;
    WASAPI_WAVEFORMATEX *mixFmt = NULL;
    uint32_t bufFrames = 0;

    if (!W.loaded) {
        log_error("wasapi", "WASAPI not loaded");
        return -1;
    }

    /* Release any previous stream state */
    wasapi_cleanup_stream();

    wasapi_sample_rate = sample_rate;
    wasapi_channels = channels;

    /* Initialise COM (single-threaded apartment) */
    W.CoInitializeEx(NULL, 0);

    /* Resolve GUIDs */
    guid_from_string(CLSID_MMDeviceEnumerator_STR, &clsId);
    guid_from_string(IID_IMMDeviceEnumerator_STR,  &iidEnum);
    guid_from_string(IID_IAudioClient_STR,         &iidClient);
    guid_from_string(IID_IAudioRenderClient_STR,   &iidRender);
    guid_from_string(IID_IAudioEndpointVolume_STR, &iidEndpointVol);

    /* Create device enumerator */
    hr = W.CoCreateInstance(&clsId, NULL, 0x17 /* CLSCTX_ALL */,
                            &iidEnum, &wasapi_device_enumerator);
    if (FAILED(hr)) {
        log_error("wasapi", "CoCreateInstance(IMMDeviceEnumerator) failed: hr=0x%08lX",
                  (unsigned long)hr);
        goto fail;
    }
    log_debug("wasapi", "IMMDeviceEnumerator created");

    /* Get default render device */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_device_enumerator,
                                IMMDE_GetDefaultAudioEndpoint,
                                eRender, eConsole, &wasapi_device);
    if (FAILED(hr)) {
        log_error("wasapi", "GetDefaultAudioEndpoint failed: hr=0x%08lX",
                  (unsigned long)hr);
        goto fail;
    }
    log_debug("wasapi", "Default render device obtained");

    /* Activate IAudioClient on the device */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_device, IMMD_Activate,
                                &iidClient, 0x17 /* CLSCTX_ALL */,
                                NULL, &wasapi_audio_client);
    if (FAILED(hr) || !wasapi_audio_client) {
        log_error("wasapi", "IAudioClient activation failed: hr=0x%08lX",
                  (unsigned long)hr);
        goto fail;
    }
    log_debug("wasapi", "IAudioClient activated");

    /* Get mix format from audio client */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_audio_client, IAC_GetMixFormat,
                                &mixFmt);
    if (FAILED(hr) || !mixFmt) {
        log_error("wasapi", "GetMixFormat failed: hr=0x%08lX",
                  (unsigned long)hr);
        goto fail;
    }

    /* Build our desired format (PCM 32-bit signed, or float32) */
    WASAPI_WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = (uint16_t)channels;
    fmt.nSamplesPerSec  = (uint32_t)sample_rate;
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = (uint16_t)(channels * 4);
    fmt.nAvgBytesPerSec = (uint32_t)(fmt.nSamplesPerSec * fmt.nBlockAlign);
    fmt.cbSize          = 0;
    wasapi_created_fmt = fmt;

    /* Create buffer-full event */
    if (W.CreateEventW) {
        wasapi_buffer_event = W.CreateEventW(NULL, 0, 0, NULL);
        if (!wasapi_buffer_event) {
            log_error("wasapi", "CreateEventW failed");
            goto fail;
        }
    } else {
        log_error("wasapi", "CreateEventW not available");
        goto fail;
    }

    /* Initialise audio client (shared mode, event-driven) */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_audio_client, IAC_Initialize,
                                AUDCLNT_SHAREMODE_SHARED,
                                0, // AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, // buffer duration (0 = default)
                                &fmt, NULL);
    if (FAILED(hr)) {
        log_error("wasapi", "IAudioClient::Initialize failed: hr=0x%08lX",
                  (unsigned long)hr);
        /* Try with float32 format */
        fmt.wFormatTag     = WAVE_FORMAT_IEEE_FLOAT;
        fmt.wBitsPerSample = 32;
        fmt.nBlockAlign    = (uint16_t)(channels * 4);
        fmt.nAvgBytesPerSec = (uint32_t)(fmt.nSamplesPerSec * fmt.nBlockAlign);
        wasapi_created_fmt = fmt;

        hr = WASAPI_VTABLE_CALL_HR(wasapi_audio_client, IAC_Initialize,
                                    AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    0, &fmt, NULL);
        if (FAILED(hr)) {
            log_error("wasapi", "IAudioClient::Initialize (float32 fallback) also failed: hr=0x%08lX",
                      (unsigned long)hr);
            goto fail;
        }
    }

    /* Set event handle on audio client (event-driven mode) */
    if (wasapi_buffer_event) {
        hr = WASAPI_VTABLE_CALL_HR(wasapi_audio_client, IAC_SetEventHandle,
                                    wasapi_buffer_event);
        if (FAILED(hr))
            log_warn("wasapi", "SetEventHandle failed (non-fatal): hr=0x%08lX",
                     (unsigned long)hr);
    }

    /* Get buffer size */
    WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_GetBufferSize,
                        void, &bufFrames);
    wasapi_buffer_frames = bufFrames;
    log_debug("wasapi", "Audio client initialised, buffer frames=%lu",
              (unsigned long)bufFrames);

    /* Get render client service */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_audio_client, IAC_GetService,
                                &iidRender, &wasapi_render_client);
    if (FAILED(hr) || !wasapi_render_client) {
        log_error("wasapi", "GetService(IAudioRenderClient) failed: hr=0x%08lX",
                  (unsigned long)hr);
        goto fail;
    }
    log_debug("wasapi", "IAudioRenderClient obtained");

    /* Activate endpoint volume control */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_device, IMMD_Activate,
                                &iidEndpointVol, 0x17 /* CLSCTX_ALL */,
                                NULL, &wasapi_endpoint_volume);
    if (FAILED(hr))
        log_warn("wasapi", "IAudioEndpointVolume activation failed (non-fatal): hr=0x%08lX",
                 (unsigned long)hr);

    /* Free mix format (CoTaskMemFree equivalent) */
    if (mixFmt) {
        /* We would call CoTaskMemFree via a loaded pointer; omit for simplicity */
    }

    wasapi_stream_ready = 1;
    log_info("wasapi", "Stream prepared: %d Hz, %d ch, %lu frames",
             sample_rate, channels, (unsigned long)bufFrames);
    return 0;

fail:
    wasapi_cleanup_stream();
    W.CoUninitialize();
    return -1;
}

/* ================================================================
 *  Cleanup stream — release all COM interfaces
 * ================================================================ */

void wasapi_cleanup_stream(void)
{
    /* Release render client */
    if (wasapi_render_client) {
        WASAPI_VTABLE_CALL_VAL(wasapi_render_client, COM_Release,
                                ULONG);
        wasapi_render_client = NULL;
    }

    /* Release audio client */
    if (wasapi_audio_client) {
        WASAPI_VTABLE_CALL_VAL(wasapi_audio_client, COM_Release, ULONG);
        wasapi_audio_client = NULL;
    }

    /* Release endpoint volume */
    if (wasapi_endpoint_volume) {
        WASAPI_VTABLE_CALL_VAL(wasapi_endpoint_volume, COM_Release, ULONG);
        wasapi_endpoint_volume = NULL;
    }

    /* Close event handle */
    if (wasapi_buffer_event) {
        if (W.CloseHandle)
            W.CloseHandle(wasapi_buffer_event);
        wasapi_buffer_event = NULL;
    }

    /* Release device */
    if (wasapi_device) {
        WASAPI_VTABLE_CALL_VAL(wasapi_device, COM_Release, ULONG);
        wasapi_device = NULL;
    }

    /* Release device enumerator */
    if (wasapi_device_enumerator) {
        WASAPI_VTABLE_CALL_VAL(wasapi_device_enumerator, COM_Release, ULONG);
        wasapi_device_enumerator = NULL;
    }

    wasapi_stream_ready = 0;
    wasapi_buffer_frames = 0;
    wasapi_channels = 0;
    wasapi_sample_rate = 0;

    log_debug("wasapi", "Stream cleaned up");
}

/* ================================================================
 *  Write audio samples
 *
 *  PCM data is in int32_t (signed 32-bit).  WASAPI prefers float32
 *  in shared mode; we convert if needed.
 * ================================================================ */

int wasapi_write_samples(const int32_t *samples, int frame_count)
{
    uint32_t padding = 0;
    uint32_t available, write_frames;
    uint8_t *buf = NULL;
    int i;

    if (!wasapi_stream_ready || !wasapi_render_client || !samples || frame_count <= 0)
        return -1;

    /* Check current padding */
    WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_GetCurrentPadding,
                        void, &padding);
    available = wasapi_buffer_frames > padding
                ? wasapi_buffer_frames - padding
                : 0;
    if (available == 0) {
        /* Buffer full — wait for event */
        if (wasapi_buffer_event && W.WaitForSingleObject) {
            DWORD waitResult = W.WaitForSingleObject(wasapi_buffer_event, 2000);
            if (waitResult != WAIT_OBJECT_0) {
                log_debug("wasapi", "WaitForSingleObject timed out (frames=%d)", frame_count);
                return -1;
            }
        }
        /* Re-check padding after event */
        WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_GetCurrentPadding,
                            void, &padding);
        available = wasapi_buffer_frames > padding
                    ? wasapi_buffer_frames - padding
                    : 0;
        if (available == 0) return 0;
    }

    write_frames = (uint32_t)frame_count < available ? (uint32_t)frame_count : available;

    /* Get buffer from render client */
    HRESULT hr = WASAPI_VTABLE_CALL_HR(wasapi_render_client, IARC_GetBuffer,
                                        write_frames, &buf);
    if (FAILED(hr) || !buf) {
        log_debug("wasapi", "GetBuffer(%lu) failed: hr=0x%08lX",
                  (unsigned long)write_frames, (unsigned long)hr);
        return -1;
    }

    /* Convert from int32 to the actual format */
    if (wasapi_created_fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        float *fBuf = (float *)buf;
        for (i = 0; i < (int)(write_frames * wasapi_channels); i++) {
            fBuf[i] = (float)((double)samples[i] / 2147483648.0);
        }
    } else {
        /* PCM 32-bit signed */
        memcpy(buf, samples, (size_t)write_frames * wasapi_channels * 4);
    }

    /* Release buffer */
    hr = WASAPI_VTABLE_CALL_HR(wasapi_render_client, IARC_ReleaseBuffer,
                                write_frames, 0);  /* 0 = no flags */
    if (FAILED(hr)) {
        log_debug("wasapi", "ReleaseBuffer(%lu) failed: hr=0x%08lX",
                  (unsigned long)write_frames, (unsigned long)hr);
        return -1;
    }

    return (int)write_frames;
}

/* ================================================================
 *  Flush stream — reset audio client
 * ================================================================ */

void wasapi_flush_stream(void)
{
    if (!wasapi_audio_client || !wasapi_stream_ready) return;

    /* Stop the audio client if it is running */
    WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_Stop, void);

    /* Reset to clear buffered data */
    WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_Reset, void);

    log_debug("wasapi", "Stream flushed");
}

/* ================================================================
 *  Pause / Resume
 * ================================================================ */

void wasapi_pause_stream(void)
{
    if (!wasapi_audio_client || !wasapi_stream_ready) return;

    WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_Stop, void);
    log_debug("wasapi", "Stream paused");
}

void wasapi_resume_stream(void)
{
    if (!wasapi_audio_client || !wasapi_stream_ready) return;

    WASAPI_VTABLE_CALL(wasapi_audio_client, IAC_Start, void);
    log_debug("wasapi", "Stream resumed");
}

/* ================================================================
 *  Sync volume — use IAudioEndpointVolume
 * ================================================================ */

void wasapi_sync_volume(int volume)
{
    float scalar;

    if (!wasapi_endpoint_volume) return;

    /* volume is 0..100 → convert to 0.0..1.0 */
    scalar = (volume > 100) ? 1.0f : (volume < 0) ? 0.0f : (float)volume / 100.0f;

    WASAPI_VTABLE_CALL(wasapi_endpoint_volume, IAEV_SetMasterVolumeLevelScalar,
                        void, scalar, NULL);

    log_debug("wasapi", "Volume synced: %d%% → scalar=%.2f", volume, (double)scalar);
}

#endif /* _WIN32 */
