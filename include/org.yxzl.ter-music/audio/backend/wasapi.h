#ifndef DYN_WASAPI_H
#define DYN_WASAPI_H

/*
 * wasapi.h — WASAPI (Windows Core Audio) dynamic-load backend
 *
 * Follows the pattern of pulse.h / pipewire.h:
 *   - Opaque type forward declarations (COM interface pointers)
 *   - IID / CLSID string constants
 *   - WAVEFORMATEX / WAVEFORMATEXTENSIBLE definitions
 *   - struct wasapi_funcs function pointer table
 *   - wasapi_load() / wasapi_unload() API
 *
 * All COM interfaces are loaded via LoadLibrary + GetProcAddress
 * at runtime; no import libraries needed at link time.
 *
 * @author ter-music team
 */

#ifdef _WIN32

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 *  COM interface UUIDs (string form, used by CLSIDFromString)
 * ================================================================ */

#define CLSID_MMDeviceEnumerator_STR \
    L"{BCDE0395-E52F-467C-8E3D-C4579291692E}"
#define IID_IMMDeviceEnumerator_STR  \
    L"{A95664D2-9614-4F35-A746-DE8DB63617E6}"
#define IID_IAudioClient_STR         \
    L"{1CB9AD4C-DBFA-4C32-B178-C2F568A703B2}"
#define IID_IAudioRenderClient_STR   \
    L"{F294ACFC-3146-4483-A7BF-ADDCA7C260E2}"
#define IID_IAudioEndpointVolume_STR \
    L"{5CDF2C82-841E-4546-9722-0CF74078229A}"

/* ================================================================
 *  COM interface vtable offset constants
 *  (used by our inline COM thunk calls, not C++ virtual syntax)
 * ================================================================
 *
 * Each COM interface vtable is an array of function pointers.
 * We define the NUMBERS here; the actual calling code in wasapi.c
 * uses the generic COM thunk helper to invoke by index.
 */

/* IUnknown (3 methods) */
#define COM_QueryInterface   0
#define COM_AddRef           1
#define COM_Release          2

/* IMMDeviceEnumerator (IUnknown + 4) */
#define IMMDE_EnumAudioEndpoints  3
#define IMMDE_GetDefaultAudioEndpoint  4
#define IMMDE_GetDevice            5

/* IMMDevice (IUnknown + 3) */
#define IMMD_Activate  3
#define IMMD_OpenPropertyStore  4
#define IMMD_GetId     5

/* IAudioClient (IUnknown + 15) */
#define IAC_Initialize            3
#define IAC_GetBufferSize         4
#define IAC_GetStreamLatency      5
#define IAC_GetCurrentPadding     6  /* uint32_t* */
#define IAC_IsFormatSupported     7
#define IAC_GetMixFormat          8
#define IAC_GetDevicePeriod       9
#define IAC_Start                10
#define IAC_Stop                 11
#define IAC_Reset                12
#define IAC_SetEventHandle       13
#define IAC_GetService           14
#define IAC_GetSharedModeEngineLatency 15
#define IAC_GetBufferFmtCount

/* IAudioRenderClient (IUnknown + 3) */
#define IARC_GetBuffer       3
#define IARC_ReleaseBuffer   4

/* IAudioEndpointVolume (IUnknown + 9) */
#define IAEV_RegisterControlChangeNotify 3
#define IAEV_UnregisterControlChangeNotify 4
#define IAEV_GetChannelCount  5
#define IAEV_SetMasterVolumeLevel      6  /* float dB, GUID */
#define IAEV_GetMasterVolumeLevel      7  /* float* dB, GUID */
#define IAEV_GetMasterVolumeLevelScalar  8
#define IAEV_SetMasterVolumeLevelScalar  9
#define IAEV_GetMute           10
#define IAEV_SetMute           11
#define IAEV_VolumeStepUp     12
#define IAEV_VolumeStepDown   13

/* ================================================================
 *  WAVEFORMATEX subset (ABI-compatible with Windows SDK)
 * ================================================================ */

typedef struct {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
} WASAPI_WAVEFORMATEX;

#define WAVE_FORMAT_PCM      1
#define WAVE_FORMAT_IEEE_FLOAT  3
#define WAVE_FORMAT_EXTENSIBLE  0xFFFE

/* ================================================================
 *  Audio client constants
 * ================================================================ */

#define AUDCLNT_SHAREMODE_SHARED      0
#define AUDCLNT_SHAREMODE_EXCLUSIVE   1

/* Stream flags */
#define AUDCLNT_STREAMFLAGS_NONE         0x00000000
#define AUDCLNT_STREAMFLAGS_CROSSPROCESS 0x00010000
#define AUDCLNT_STREAMFLAGS_LOOPBACK     0x00020000
#define AUDCLNT_STREAMFLAGS_EVENTCALLBACK 0x00040000

/* Device role */
#define eConsole  0
#define eMultimedia 1
#define eCommunications 2

/* Data flow */
#define eRender   0
#define eCapture  1

/* ================================================================
 *  Function pointer table
 * ================================================================ */

struct wasapi_funcs {
    /* LoadLibrary + GetProcAddress handles are resolved internally */

    /* State */
    int loaded;
    void *handle_mmdevapi;   /* mmdevapi.dll */
    void *handle_ole32;      /* ole32.dll */

    /* OLE32 */
    HRESULT (STDAPICALLTYPE *CoInitializeEx)(void *, uint32_t);
    void    (STDAPICALLTYPE *CoUninitialize)(void);
    HRESULT (STDAPICALLTYPE *CoCreateInstance)(const GUID *, void *,
                                                uint32_t, const GUID *, void **);
    HRESULT (STDAPICALLTYPE *CLSIDFromString)(const wchar_t *, GUID *);
    HRESULT (STDAPICALLTYPE *StringFromGUID2)(const GUID *, wchar_t *, int);

    /* MMDevAPI */
    HRESULT (STDAPICALLTYPE *ActivateAudioInterface)(const wchar_t *, const GUID *,
                                                       void *, void **);

    /* Kernel32 */
    HANDLE  (WINAPI *CreateEventW)(void *, int, int, const wchar_t *);
    DWORD   (WINAPI *WaitForSingleObject)(HANDLE, DWORD);
    BOOL    (WINAPI *CloseHandle)(HANDLE);

    /* Win32 COM thunk helpers — see WASAPI_VTABLE_CALL macro */
};

/* ================================================================
 *  WASAPI state (exported to audio.c / backend_ops.c)
 * ================================================================ */

extern struct wasapi_funcs W;

/* COM interface pointers (opaque void* — we use vtable indices) */
extern void *wasapi_device_enumerator;
extern void *wasapi_device;
extern void *wasapi_audio_client;
extern void *wasapi_render_client;
extern void *wasapi_endpoint_volume;

extern int  wasapi_loaded;
extern int  wasapi_stream_ready;
extern int  wasapi_channels;
extern int  wasapi_sample_rate;
extern HANDLE wasapi_buffer_event;
extern uint32_t wasapi_buffer_frames;

/* ================================================================
 *  Public API
 * ================================================================ */

int  wasapi_load(void);
void wasapi_unload(void);

int  wasapi_prepare_stream(int sample_rate, int channels);
void wasapi_cleanup_stream(void);
void wasapi_flush_stream(void);
int  wasapi_write_samples(const int32_t *samples, int frame_count);
void wasapi_pause_stream(void);
void wasapi_resume_stream(void);
void wasapi_sync_volume(int volume);

#endif /* _WIN32 */
#endif /* DYN_WASAPI_H */
