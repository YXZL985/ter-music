#include "../include/defs.h"
#include "../include/menu_views.h"
#include "../include/progress.h"  // 新增：进度跟踪模块
#include "../include/lyrics.h"    // 新增：歌词模块
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// 音频后端头文件
#if defined(HAVE_PULSE)
#include <pulse/pulseaudio.h>
#elif defined(HAVE_ALSA)
#include <alsa/asoundlib.h>
#else
#error "No audio backend configured"
#endif

// FFmpeg 头文件
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

// 确保DT_REG和DT_UNKNOWN被定义
#ifndef DT_REG
#define DT_REG 8
#endif

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

// 音频后端全局变量
#if defined(HAVE_PULSE)
static pa_mainloop *pa_ml = NULL;
static pa_context *pa_ctx = NULL;
static pa_stream *pa_s = NULL;
static pa_sample_spec pa_ss;
static int pa_connected = 0;
#elif defined(HAVE_ALSA)
static snd_pcm_t *alsa_pcm = NULL;
static int alsa_ready = 0;
#endif

static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_volume_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_visualizer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_play_control_cond = PTHREAD_COND_INITIALIZER;
static int g_volume_percent = 100;
static int g_pending_volume_sync = 0;
static int g_output_sample_rate = 44100;
static int g_output_channels = 2;
static int g_visualizer_levels[VISUALIZER_BAND_COUNT] = {0};
static int g_visualizer_peaks[VISUALIZER_BAND_COUNT] = {0};
static uint64_t g_visualizer_last_update_ms = 0;

static const char *audio_text(const char *utf8, const char *ascii) {
    return use_ascii_fallback_ui() ? ascii : utf8;
}

static uint64_t audio_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

#define PCM_QUEUE_CAPACITY 12
#define PCM_QUEUE_PREFILL_TARGET 4

typedef struct {
    int16_t *data;
    int frame_count;
    int bytes;
} PCMChunk;

typedef struct {
    PCMChunk chunks[PCM_QUEUE_CAPACITY];
    int read_index;
    int write_index;
    int count;
} PCMQueue;

static int clamp_volume_percent(int volume) {
    if (volume < 0) {
        return 0;
    }
    if (volume > 100) {
        return 100;
    }
    return volume;
}

static int get_configured_latency_ms(void) {
    int latency_ms = g_app_config.audio_latency_ms;
    if (latency_ms < 20) {
        latency_ms = 20;
    }
    if (latency_ms > 250) {
        latency_ms = 250;
    }
    return latency_ms;
}

void reset_visualizer_state(void) {
    pthread_mutex_lock(&g_visualizer_mutex);
    memset(g_visualizer_levels, 0, sizeof(g_visualizer_levels));
    memset(g_visualizer_peaks, 0, sizeof(g_visualizer_peaks));
    g_visualizer_last_update_ms = audio_now_ms();
    pthread_mutex_unlock(&g_visualizer_mutex);
}

void push_visualizer_samples(const int16_t *samples, int frame_count, int channels) {
    if (!samples || frame_count <= 0 || channels <= 0) {
        return;
    }

    int64_t sums[VISUALIZER_BAND_COUNT] = {0};
    int counts[VISUALIZER_BAND_COUNT] = {0};

    for (int frame = 0; frame < frame_count; frame++) {
        int mixed = 0;
        for (int ch = 0; ch < channels; ch++) {
            int sample = samples[frame * channels + ch];
            mixed += sample >= 0 ? sample : -sample;
        }
        mixed /= channels;

        int band = (frame * VISUALIZER_BAND_COUNT) / frame_count;
        if (band >= VISUALIZER_BAND_COUNT) {
            band = VISUALIZER_BAND_COUNT - 1;
        }
        sums[band] += mixed;
        counts[band]++;
    }

    pthread_mutex_lock(&g_visualizer_mutex);
    for (int i = 0; i < VISUALIZER_BAND_COUNT; i++) {
        int raw_level = counts[i] > 0 ? (int)(sums[i] / counts[i]) : 0;
        int scaled_level = (raw_level * 100) / 14000;
        if (scaled_level < 0) {
            scaled_level = 0;
        }
        if (scaled_level > 100) {
            scaled_level = 100;
        }

        int previous = g_visualizer_levels[i];
        if (scaled_level > previous) {
            g_visualizer_levels[i] = (previous + scaled_level * 3) / 4;
        } else {
            g_visualizer_levels[i] = (previous * 3 + scaled_level) / 4;
        }

        if (g_visualizer_levels[i] < 2) {
            g_visualizer_levels[i] = 0;
        }

        if (g_visualizer_levels[i] > g_visualizer_peaks[i]) {
            g_visualizer_peaks[i] = g_visualizer_levels[i];
        } else if (g_visualizer_peaks[i] > 0) {
            g_visualizer_peaks[i] -= 1;
        }
    }
    g_visualizer_last_update_ms = audio_now_ms();
    pthread_mutex_unlock(&g_visualizer_mutex);
}

void get_visualizer_snapshot(int *levels, int *peaks, int max_levels, uint64_t *last_update_ms) {
    if (!levels || !peaks || max_levels <= 0) {
        return;
    }

    int copy_count = max_levels < VISUALIZER_BAND_COUNT ? max_levels : VISUALIZER_BAND_COUNT;

    pthread_mutex_lock(&g_visualizer_mutex);
    for (int i = 0; i < copy_count; i++) {
        levels[i] = g_visualizer_levels[i];
        peaks[i] = g_visualizer_peaks[i];
    }
    if (last_update_ms) {
        *last_update_ms = g_visualizer_last_update_ms;
    }
    pthread_mutex_unlock(&g_visualizer_mutex);
}

static void signal_playback_thread(void) {
    pthread_cond_broadcast(&g_play_control_cond);
}

static void pcm_queue_reset(PCMQueue *queue) {
    if (!queue) {
        return;
    }

    queue->read_index = 0;
    queue->write_index = 0;
    queue->count = 0;
    for (int i = 0; i < PCM_QUEUE_CAPACITY; i++) {
        queue->chunks[i].frame_count = 0;
        queue->chunks[i].bytes = 0;
    }
}

static int pcm_queue_init(PCMQueue *queue) {
    if (!queue) {
        return -1;
    }

    memset(queue, 0, sizeof(*queue));
    for (int i = 0; i < PCM_QUEUE_CAPACITY; i++) {
        queue->chunks[i].data = malloc(MAX_AUDIO_BUFFER_SIZE);
        if (!queue->chunks[i].data) {
            for (int j = 0; j < i; j++) {
                free(queue->chunks[j].data);
                queue->chunks[j].data = NULL;
            }
            return -1;
        }
    }

    pcm_queue_reset(queue);
    return 0;
}

static void pcm_queue_destroy(PCMQueue *queue) {
    if (!queue) {
        return;
    }

    for (int i = 0; i < PCM_QUEUE_CAPACITY; i++) {
        free(queue->chunks[i].data);
        queue->chunks[i].data = NULL;
    }
    pcm_queue_reset(queue);
}

static PCMChunk *pcm_queue_write_slot(PCMQueue *queue) {
    if (!queue || queue->count >= PCM_QUEUE_CAPACITY) {
        return NULL;
    }
    return &queue->chunks[queue->write_index];
}

static void pcm_queue_commit_write(PCMQueue *queue, int frame_count, int bytes) {
    if (!queue || queue->count >= PCM_QUEUE_CAPACITY) {
        return;
    }

    queue->chunks[queue->write_index].frame_count = frame_count;
    queue->chunks[queue->write_index].bytes = bytes;
    queue->write_index = (queue->write_index + 1) % PCM_QUEUE_CAPACITY;
    queue->count++;
}

static PCMChunk *pcm_queue_peek(PCMQueue *queue) {
    if (!queue || queue->count <= 0) {
        return NULL;
    }
    return &queue->chunks[queue->read_index];
}

static void pcm_queue_consume(PCMQueue *queue) {
    if (!queue || queue->count <= 0) {
        return;
    }

    queue->chunks[queue->read_index].frame_count = 0;
    queue->chunks[queue->read_index].bytes = 0;
    queue->read_index = (queue->read_index + 1) % PCM_QUEUE_CAPACITY;
    queue->count--;
}

static void apply_volume_to_samples(int16_t *samples, int sample_count) {
#if defined(HAVE_PULSE)
    (void)samples;
    (void)sample_count;
#else
    if (!samples || sample_count <= 0) {
        return;
    }

    int volume = get_volume_percent();
    if (volume >= 100) {
        return;
    }
    if (volume <= 0) {
        memset(samples, 0, (size_t)sample_count * sizeof(int16_t));
        return;
    }

    for (int i = 0; i < sample_count; i++) {
        samples[i] = (int16_t)(((int)samples[i] * volume) / 100);
    }
#endif
}

static int consume_volume_sync_request(int *volume_out, int force) {
    int should_sync = force;
    int volume = 100;

    pthread_mutex_lock(&g_volume_mutex);
    volume = g_volume_percent;
    if (g_pending_volume_sync) {
        should_sync = 1;
        g_pending_volume_sync = 0;
    }
    pthread_mutex_unlock(&g_volume_mutex);

    if (volume_out) {
        *volume_out = volume;
    }
    return should_sync;
}

static void audio_backend_sync_volume(int force) {
    int volume = 100;
    if (!consume_volume_sync_request(&volume, force)) {
        return;
    }

#if defined(HAVE_PULSE)
    if (!pa_ctx || !pa_ml || !pa_s || pa_stream_get_state(pa_s) != PA_STREAM_READY) {
        return;
    }

    uint32_t stream_index = pa_stream_get_index(pa_s);
    if (stream_index == PA_INVALID_INDEX) {
        return;
    }

    pa_cvolume cvolume;
    pa_cvolume_set(&cvolume, pa_ss.channels, pa_sw_volume_from_linear((double)volume / 100.0));

    pa_operation *op = pa_context_set_sink_input_volume(pa_ctx, stream_index, &cvolume, NULL, NULL);
    if (!op) {
        return;
    }

    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }
    pa_operation_unref(op);
#else
    (void)force;
    (void)volume;
#endif
}

// 全局变量定义
PlayState g_play_state = PLAY_STATE_STOPPED; // 当前播放状态
int g_current_play_index = -1; // 当前播放的歌曲索引
LoopMode g_loop_mode = LOOP_OFF; // 当前循环模式
pthread_t g_play_thread; // 播放线程
int g_play_thread_running = 0; // 播放线程运行状态
pthread_mutex_t g_play_mutex = PTHREAD_MUTEX_INITIALIZER; // 播放控制互斥锁
static int g_play_thread_finished = 0;
static int g_pending_playback_index = -1;

// 跳转相关变量
int g_seek_request = 0; // 跳转请求标志
int g_seek_position = 0; // 目标跳转位置（秒）
int g_initial_seek_position = 0; // 重启播放时的初始跳转位置（秒）

// 进度条相关变量（保留用于 UI 兼容，实际值从 progress_tracker 获取）
int g_current_position = 0; // 当前播放位置（秒）
int g_total_duration = 0; // 当前歌曲总时长（秒）
pthread_mutex_t g_seek_mutex = PTHREAD_MUTEX_INITIALIZER; // 跳转操作互斥锁 

// 全局变量：默认音频设备名称
char g_default_audio_device[128] = "default";

// PulseAudio 状态检查宏
#define PA_CHECK_SUCCESS(op, msg) do { \
    if (!(op)) { \
        fprintf(stderr, "PulseAudio error: %s\n", msg); \
    } \
} while(0)



/**
 * 自定义FFmpeg日志回调，完全禁止输出到终端
 * 防止日志输出破坏ncurses界面
 */
static void ffmpeg_log_callback(void *ptr, int level, const char *fmt, va_list vl) {
    // 完全丢弃所有日志，不输出到终端
    // 这防止了FFmpeg日志破坏ncurses界面
}

static int audio_backend_prepare_stream(int sample_rate, int channels) {
    g_output_sample_rate = sample_rate;
    g_output_channels = channels;

    unsigned int latency_usec = (unsigned int)get_configured_latency_ms() * 1000U;
    unsigned int minreq_usec = latency_usec / 4U;
    if (minreq_usec < 5000U) {
        minreq_usec = 5000U;
    }
    unsigned int max_latency_usec = latency_usec * 2U;

#if defined(HAVE_PULSE)
    if (!pa_connected || !pa_ctx) {
        update_controls_status(audio_text("PulseAudio 未连接", "PulseAudio disconnected"));
        return -1;
    }

    pa_ss.format = PA_SAMPLE_S16LE;
    pa_ss.rate = sample_rate;
    pa_ss.channels = (uint8_t)channels;

    pa_stream *new_stream = pa_stream_new(pa_ctx, "playback", &pa_ss, NULL);
    if (!new_stream) {
        update_controls_status(audio_text("无法创建 PulseAudio 播放流", "Cannot create PulseAudio stream"));
        return -1;
    }

    pa_buffer_attr ba;
    memset(&ba, 0, sizeof(ba));
    ba.maxlength = pa_usec_to_bytes((pa_usec_t)max_latency_usec, &pa_ss);
    ba.tlength = pa_usec_to_bytes((pa_usec_t)latency_usec, &pa_ss);
    ba.prebuf = 0;
    ba.minreq = pa_usec_to_bytes((pa_usec_t)minreq_usec, &pa_ss);
    ba.fragsize = (uint32_t)-1;

    if (pa_stream_connect_playback(new_stream, NULL, &ba, PA_STREAM_ADJUST_LATENCY, NULL, NULL) < 0) {
        update_controls_status(audio_text("无法连接 PulseAudio 播放流", "Cannot connect PulseAudio stream"));
        pa_stream_unref(new_stream);
        return -1;
    }

    while (pa_stream_get_state(new_stream) != PA_STREAM_READY) {
        if (pa_stream_get_state(new_stream) == PA_STREAM_FAILED ||
            pa_stream_get_state(new_stream) == PA_STREAM_TERMINATED) {
            update_controls_status(audio_text("PulseAudio 播放流初始化失败", "PulseAudio stream init failed"));
            pa_stream_unref(new_stream);
            return -1;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }

    pa_s = new_stream;
    return 0;
#else
    if (!alsa_ready) {
        update_controls_status(audio_text("ALSA 后端未就绪", "ALSA backend not ready"));
        return -1;
    }

    if (snd_pcm_open(&alsa_pcm, g_default_audio_device, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        update_controls_status(audio_text("无法打开 ALSA 设备", "Cannot open ALSA device"));
        alsa_pcm = NULL;
        return -1;
    }

    if (snd_pcm_set_params(alsa_pcm,
                           SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED,
                           (unsigned int)channels,
                           (unsigned int)sample_rate,
                           1,
                           latency_usec) < 0) {
        update_controls_status(audio_text("无法配置 ALSA 设备", "Cannot configure ALSA device"));
        snd_pcm_close(alsa_pcm);
        alsa_pcm = NULL;
        return -1;
    }

    return 0;
#endif
}

static void audio_backend_cleanup_stream(void) {
#if defined(HAVE_PULSE)
    if (pa_s) {
        pa_stream_disconnect(pa_s);
        pa_stream_unref(pa_s);
        pa_s = NULL;
    }
#else
    if (alsa_pcm) {
        snd_pcm_drop(alsa_pcm);
        snd_pcm_close(alsa_pcm);
        alsa_pcm = NULL;
    }
#endif
}

static void audio_backend_flush_stream(void) {
#if defined(HAVE_PULSE)
    if (pa_s && pa_ml && pa_ctx) {
        pa_stream_state_t state = pa_stream_get_state(pa_s);
        if (state == PA_STREAM_READY) {
            pa_operation *op = pa_stream_flush(pa_s, NULL, NULL);
            if (op) {
                while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                    pa_mainloop_iterate(pa_ml, 1, NULL);
                }
                pa_operation_unref(op);
            }
        }
    }
#else
    if (alsa_pcm) {
        snd_pcm_drop(alsa_pcm);
        snd_pcm_prepare(alsa_pcm);
    }
#endif
}

static int audio_backend_write_samples(const int16_t *samples, int frame_count) {
    if (!samples || frame_count <= 0) {
        return 0;
    }

#if defined(HAVE_PULSE)
    size_t bytes = (size_t)frame_count * pa_ss.channels * sizeof(int16_t);

    if (!pa_s || pa_stream_get_state(pa_s) != PA_STREAM_READY) {
        return -1;
    }

    while (pa_s && pa_stream_get_state(pa_s) == PA_STREAM_READY &&
           pa_stream_writable_size(pa_s) < bytes) {
        if (!g_play_thread_running) {
            return 0;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }

    if (!pa_s || pa_stream_get_state(pa_s) != PA_STREAM_READY) {
        return -1;
    }

    return pa_stream_write(pa_s, samples, bytes, NULL, 0, PA_SEEK_RELATIVE);
#else
    int written = 0;
    int wait_timeout_ms = get_configured_latency_ms();
    if (wait_timeout_ms < 20) {
        wait_timeout_ms = 20;
    }

    while (written < frame_count) {
        if (!g_play_thread_running) {
            return 0;
        }

        if (snd_pcm_wait(alsa_pcm, wait_timeout_ms) < 0) {
            snd_pcm_prepare(alsa_pcm);
        }
        snd_pcm_sframes_t ret = snd_pcm_writei(alsa_pcm,
                                               samples + (written * g_output_channels),
                                               frame_count - written);
        if (ret > 0) {
            written += (int)ret;
            continue;
        }
        if (ret == -EAGAIN) {
            continue;
        }
        if (ret == -EPIPE || ret == -ESTRPIPE) {
            snd_pcm_prepare(alsa_pcm);
            continue;
        }
        return -1;
    }

    return 0;
#endif
}

static void audio_backend_pause_stream(void) {
#if defined(HAVE_PULSE)
    if (pa_s && pa_ml && pa_ctx) {
        pa_stream_state_t state = pa_stream_get_state(pa_s);
        if (state == PA_STREAM_READY && !pa_stream_is_corked(pa_s)) {
            pa_operation *op = pa_stream_cork(pa_s, 1, NULL, NULL);
            if (op) {
                pa_operation_unref(op);
            }
        }
    }
#else
    if (alsa_pcm) {
        snd_pcm_pause(alsa_pcm, 1);
    }
#endif
}

static void audio_backend_resume_stream(void) {
#if defined(HAVE_PULSE)
    if (pa_s && pa_ml && pa_ctx) {
        pa_stream_state_t state = pa_stream_get_state(pa_s);
        if (state == PA_STREAM_READY && pa_stream_is_corked(pa_s)) {
            pa_operation *op = pa_stream_cork(pa_s, 0, NULL, NULL);
            if (op) {
                pa_operation_unref(op);
            }
        }
    }
#else
    if (alsa_pcm) {
        snd_pcm_pause(alsa_pcm, 0);
    }
#endif
}

/**
 * 初始化FFmpeg库
 */
void init_ffmpeg() {
    avformat_network_init();
    // 禁用FFmpeg日志输出，避免干扰UI
    av_log_set_level(AV_LOG_QUIET);
    // 设置自定义日志回调，完全禁止任何输出
    av_log_set_callback(ffmpeg_log_callback);
}

void init_audio_device() {
#if defined(HAVE_PULSE)
    pa_ml = pa_mainloop_new();
    if (!pa_ml) {
        printf("%s\n", audio_text("警告：无法创建 PulseAudio 主循环", "Warning: cannot create PulseAudio mainloop"));
        return;
    }
    
    pa_ctx = pa_context_new(pa_mainloop_get_api(pa_ml), APP_NAME);
    if (!pa_ctx) {
        printf("%s\n", audio_text("警告：无法创建 PulseAudio 上下文", "Warning: cannot create PulseAudio context"));
        pa_mainloop_free(pa_ml);
        pa_ml = NULL;
        return;
    }
    
    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
        if (pa_context_get_state(pa_ctx) == PA_CONTEXT_FAILED ||
            pa_context_get_state(pa_ctx) == PA_CONTEXT_TERMINATED) {
            printf("%s\n", audio_text("警告：无法连接 PulseAudio 服务", "Warning: cannot connect PulseAudio"));
            pa_context_unref(pa_ctx);
            pa_mainloop_free(pa_ml);
            pa_ctx = NULL;
            pa_ml = NULL;
            pa_connected = 0;
            return;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }
    
    pa_connected = 1;
    printf("%s\n", audio_text("已连接到 PulseAudio 服务", "Connected to PulseAudio"));
#else
    alsa_ready = 1;
    printf("%s\n", audio_text("当前使用 ALSA 音频后端", "Using ALSA backend"));
#endif
}

int get_volume_percent(void) {
    int volume = 100;

    pthread_mutex_lock(&g_volume_mutex);
    volume = g_volume_percent;
    pthread_mutex_unlock(&g_volume_mutex);

    return volume;
}

void set_volume_percent(int volume) {
    int clamped = clamp_volume_percent(volume);
    int changed = 0;

    pthread_mutex_lock(&g_volume_mutex);
    if (g_volume_percent != clamped) {
        g_volume_percent = clamped;
        g_pending_volume_sync = 1;
        changed = 1;
    }
    pthread_mutex_unlock(&g_volume_mutex);

    if (changed) {
        g_app_config.volume_percent = clamped;
        save_config();

        char msg[64];
        snprintf(msg, sizeof(msg),
                 use_ascii_fallback_ui() ? "Volume: %d%%" : "音量：%d%%",
                 clamped);
        update_controls_status(msg);
        request_ui_refresh(UI_DIRTY_CONTROLS);
        signal_playback_thread();
    }
}

void adjust_volume(int delta) {
    set_volume_percent(get_volume_percent() + delta);
}

extern void render_playlist_content();
extern void render_controls();

/**
 * 获取循环模式的字符串表示
 */
const char *get_loop_mode_str() {
    switch(g_loop_mode) {
        case LOOP_OFF:
            return audio_text("关闭", "Off");
        case LOOP_SINGLE:
            return audio_text("单曲", "Single");
        case LOOP_LIST:
            return audio_text("列表", "List");
        case LOOP_RANDOM:
            return audio_text("随机", "Random");
        default:
            return audio_text("关闭", "Off");
    }
}

/**
 * 切换循环模式
 * 按顺序切换：Off -> Single -> List -> Random -> Off
 */
void toggle_loop_mode() {
    g_loop_mode = (g_loop_mode + 1) % 4;
    // 不再调用update_controls_status，避免阻塞
    render_controls();
}

void reap_finished_playback_thread(void) {
    pthread_t thread_to_join;
    int should_join = 0;

    pthread_mutex_lock(&g_play_mutex);
    if (g_play_thread_finished) {
        thread_to_join = g_play_thread;
        g_play_thread_finished = 0;
        should_join = 1;
    }
    pthread_mutex_unlock(&g_play_mutex);

    if (should_join && !pthread_equal(pthread_self(), thread_to_join)) {
        pthread_join(thread_to_join, NULL);
    }
}

void process_pending_playback_action(void) {
    int pending_index = -1;

    pthread_mutex_lock(&g_play_mutex);
    if (g_pending_playback_index >= 0) {
        pending_index = g_pending_playback_index;
        g_pending_playback_index = -1;
    }
    pthread_mutex_unlock(&g_play_mutex);

    if (pending_index >= 0) {
        play_audio(pending_index);
    }
}

static int wait_while_paused(void) {
    pthread_mutex_lock(&g_play_mutex);
    while (g_play_state == PLAY_STATE_PAUSED && g_play_thread_running) {
        pthread_cond_wait(&g_play_control_cond, &g_play_mutex);
    }
    int still_running = g_play_thread_running;
    pthread_mutex_unlock(&g_play_mutex);
    return still_running;
}

static int handle_seek_request_in_decoder(AVFormatContext *fmt_ctx,
                                          AVCodecContext *codec_ctx,
                                          SwrContext *swr_ctx,
                                          AVPacket *packet,
                                          PCMQueue *queue,
                                          int audio_stream_index,
                                          int *decoder_draining,
                                          int *decoder_finished) {
    int handled = 0;

    pthread_mutex_lock(&g_seek_mutex);
    if (g_seek_request && g_play_thread_running && fmt_ctx && codec_ctx) {
        int target_position = g_seek_position;
        g_seek_request = 0;
        handled = 1;

        AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;
        int64_t target_ts = av_rescale_q(target_position, (AVRational){1, 1}, time_base);
        int ret = av_seek_frame(fmt_ctx, audio_stream_index, target_ts, 0);
        if (ret < 0) {
            update_controls_status(audio_text("跳转失败", "Seek failed"));
        } else {
            avcodec_flush_buffers(codec_ctx);
            if (swr_ctx) {
                swr_init(swr_ctx);
            }
            if (packet) {
                av_packet_unref(packet);
            }
            if (queue) {
                pcm_queue_reset(queue);
            }
            audio_backend_flush_stream();
            progress_tracker_seek(target_position);
            g_current_position = target_position;
            if (decoder_draining) {
                *decoder_draining = 0;
            }
            if (decoder_finished) {
                *decoder_finished = 0;
            }

            char msg[64];
            snprintf(msg, sizeof(msg),
                     use_ascii_fallback_ui() ? "Seek to %02d:%02d" : "已跳转到 %02d:%02d",
                     target_position / 60, target_position % 60);
            update_controls_status(msg);
        }
    }
    pthread_mutex_unlock(&g_seek_mutex);

    return handled;
}

static int decode_next_pcm_chunk(AVFormatContext *fmt_ctx,
                                 AVCodecContext *codec_ctx,
                                 SwrContext *swr_ctx,
                                 AVPacket *packet,
                                 AVFrame *frame,
                                 PCMQueue *queue,
                                 int audio_stream_index,
                                 int output_sample_rate,
                                 int output_channels,
                                 int use_resampler,
                                 int *decoder_draining,
                                 int *decoder_finished) {
    if (!fmt_ctx || !codec_ctx || !packet || !frame || !queue) {
        return -1;
    }

    while (!*decoder_finished) {
        int ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == 0) {
            PCMChunk *slot = pcm_queue_write_slot(queue);
            if (!slot) {
                av_frame_unref(frame);
                return 0;
            }

            int produced_frames = 0;
            int produced_bytes = 0;

            if (!use_resampler) {
                produced_bytes = av_samples_get_buffer_size(NULL, output_channels, frame->nb_samples,
                                                            AV_SAMPLE_FMT_S16, 1);
                if (produced_bytes > 0 && produced_bytes <= MAX_AUDIO_BUFFER_SIZE) {
                    memcpy(slot->data, frame->data[0], (size_t)produced_bytes);
                    produced_frames = frame->nb_samples;
                }
            } else {
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                    output_sample_rate, codec_ctx->sample_rate, AV_ROUND_UP);
                produced_bytes = av_samples_get_buffer_size(NULL, output_channels, dst_nb_samples,
                                                            AV_SAMPLE_FMT_S16, 1);
                if (produced_bytes > 0 && produced_bytes <= MAX_AUDIO_BUFFER_SIZE) {
                    uint8_t *output_planes[] = {(uint8_t *)slot->data};
                    produced_frames = swr_convert(swr_ctx, output_planes, dst_nb_samples,
                                                  (const uint8_t **)frame->data, frame->nb_samples);
                    if (produced_frames > 0) {
                        produced_bytes = produced_frames * output_channels * (int)sizeof(int16_t);
                    }
                }
            }

            av_frame_unref(frame);

            if (produced_frames > 0 && produced_bytes > 0) {
                pcm_queue_commit_write(queue, produced_frames, produced_bytes);
                return 1;
            }
            continue;
        }

        if (ret == AVERROR_EOF) {
            *decoder_finished = 1;
            return 0;
        }

        if (ret != AVERROR(EAGAIN)) {
            return -1;
        }

        if (!*decoder_draining) {
            while (1) {
                ret = av_read_frame(fmt_ctx, packet);
                if (ret < 0) {
                    *decoder_draining = 1;
                    ret = avcodec_send_packet(codec_ctx, NULL);
                    if (ret < 0 && ret != AVERROR_EOF) {
                        return -1;
                    }
                    break;
                }

                if (packet->stream_index != audio_stream_index) {
                    av_packet_unref(packet);
                    continue;
                }

                ret = avcodec_send_packet(codec_ctx, packet);
                av_packet_unref(packet);
                if (ret == AVERROR(EAGAIN)) {
                    break;
                }
                if (ret < 0) {
                    return -1;
                }
                break;
            }
        } else {
            *decoder_finished = 1;
            return 0;
        }
    }

    return 0;
}

/**
 * 播放音频文件的线程函数
 * 负责解码音频文件并通过音频后端输出到音频设备
 */
void *play_audio_thread(void *arg) {
    int index = *((int *)arg);
    int reached_end_of_stream = 0;
    int followup_index = -1;
    free(arg);
    
    pthread_mutex_lock(&g_play_mutex);
    if (index < 0 || index >= g_playlist.count || !g_play_thread_running) {
        g_play_thread_running = 0;
        g_play_thread_finished = 1;
        pthread_mutex_unlock(&g_play_mutex);
        return NULL;
    }
    
    char file_path[MAX_PATH_LEN];
    snprintf(file_path, sizeof(file_path), "%s", g_playlist.tracks[index].path);
    
    pthread_mutex_unlock(&g_play_mutex);

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    int initial_seek_position = 0;
    int audio_stream_index = -1;
    int input_channels = 2;
    int output_channels = 2;
    int output_sample_rate = 44100;
    int use_resampler = 1;
    int decoder_draining = 0;
    int decoder_finished = 0;
    int playback_error = 0;
    PCMQueue pcm_queue;
    int pcm_queue_initialized = 0;

    memset(&pcm_queue, 0, sizeof(pcm_queue));

    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) != 0) {
        update_controls_status(audio_text("无法打开音频文件", "Cannot open audio file"));
        goto cleanup;
    }
    
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        update_controls_status(audio_text("无法读取音频流信息", "Cannot read stream info"));
        goto cleanup;
    }

    // 获取歌曲总时长（秒）
    g_total_duration = fmt_ctx->duration / AV_TIME_BASE;
    
    // 验证时长的有效性，如果无效则尝试通过音频帧数计算
    if (g_total_duration <= 0) {
        // 尝试通过音频流的 duration 和 time_base 计算时长
        for (int i = 0; i < fmt_ctx->nb_streams; i++) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                AVRational time_base = fmt_ctx->streams[i]->time_base;
                int64_t stream_duration = fmt_ctx->streams[i]->duration;
                if (stream_duration > 0 && time_base.den > 0) {
                    g_total_duration = av_rescale_q(stream_duration, time_base, (AVRational){1, 1});
                    break;
                }
            }
        }
        // 如果还是无法获取有效时长，设置为一个默认值（300 秒）
        if (g_total_duration <= 0) {
            g_total_duration = 300; // 默认 5 分钟
        }
    }
    
    // 重置当前播放位置为 0
    g_current_position = 0;
    reset_visualizer_state();
    
    // 检查是否需要跳转到特定位置（在开始播放之前）
    // 如果是重启跳转，从全局变量获取目标位置
    initial_seek_position = get_and_clear_initial_seek_position(); // 获取初始跳转位置，获取后清除
    
    // 强制更新 UI 显示，确保进度条从 0% 开始
    request_ui_refresh(UI_DIRTY_CONTROLS);
    
    // 找到音频流
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    
    if (audio_stream_index == -1) {
        update_controls_status(audio_text("未找到音频流", "No audio stream found"));
        goto cleanup;
    }
    
    // 获取解码器
    AVCodecParameters *codec_par = fmt_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
    if (!codec) {
        update_controls_status(audio_text("当前编解码器不受支持", "Unsupported codec"));
        goto cleanup;
    }
    
    // 创建解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        update_controls_status(audio_text("无法分配解码器上下文", "Cannot allocate codec context"));
        goto cleanup;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0) {
        update_controls_status(audio_text("无法复制编解码参数", "Cannot copy codec parameters"));
        goto cleanup;
    }
    
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        update_controls_status(audio_text("无法打开编解码器", "Cannot open codec"));
        goto cleanup;
    }
    
    input_channels = codec_ctx->ch_layout.nb_channels;
    if (input_channels <= 0) {
        input_channels = 2;
    }
    output_channels = (input_channels == 1) ? 1 : 2;
    output_sample_rate = codec_ctx->sample_rate > 0 ? codec_ctx->sample_rate : 44100;
    use_resampler = !(codec_ctx->sample_fmt == AV_SAMPLE_FMT_S16 && input_channels <= 2);

    if (use_resampler) {
        swr_ctx = swr_alloc();
        if (!swr_ctx) {
            update_controls_status(audio_text("无法分配重采样器", "Cannot allocate resampler"));
            goto cleanup;
        }

        AVChannelLayout in_ch_layout = codec_ctx->ch_layout;
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, output_channels);

        av_opt_set_chlayout(swr_ctx, "in_chlayout", &in_ch_layout, 0);
        av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_ch_layout, 0);
        av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
        av_opt_set_int(swr_ctx, "out_sample_rate", output_sample_rate, 0);
        av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);
        av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

        if (swr_init(swr_ctx) < 0) {
            update_controls_status(audio_text("无法初始化重采样器", "Cannot initialize resampler"));
            goto cleanup;
        }
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        update_controls_status(audio_text("无法分配解码数据结构", "Cannot allocate decode structures"));
        goto cleanup;
    }

    if (pcm_queue_init(&pcm_queue) < 0) {
        update_controls_status(audio_text("无法分配音频缓冲区", "Cannot allocate audio buffer"));
        goto cleanup;
    }
    pcm_queue_initialized = 1;

    if (audio_backend_prepare_stream(output_sample_rate, output_channels) < 0) {
        goto cleanup;
    }

    audio_backend_sync_volume(1);

    progress_tracker_init(output_sample_rate);
    progress_tracker_set_sample_rate(output_sample_rate);
    progress_tracker_start();

    g_play_state = PLAY_STATE_PLAYING;

    if (initial_seek_position > 0 && initial_seek_position < g_total_duration) {
        pthread_mutex_lock(&g_seek_mutex);
        g_seek_position = initial_seek_position;
        g_seek_request = 1;
        pthread_mutex_unlock(&g_seek_mutex);
    }

    while (g_play_thread_running) {
        audio_backend_sync_volume(0);

        if (!wait_while_paused()) {
            break;
        }

        if (handle_seek_request_in_decoder(fmt_ctx, codec_ctx, swr_ctx, packet, &pcm_queue,
                                           audio_stream_index, &decoder_draining, &decoder_finished)) {
            if (g_current_position >= g_total_duration) {
                g_play_thread_running = 0;
                break;
            }
            continue;
        }

        while (g_play_thread_running && !decoder_finished && pcm_queue.count < PCM_QUEUE_PREFILL_TARGET) {
            int decode_result = decode_next_pcm_chunk(fmt_ctx, codec_ctx, swr_ctx, packet, frame, &pcm_queue,
                                                      audio_stream_index, output_sample_rate, output_channels,
                                                      use_resampler, &decoder_draining, &decoder_finished);
            if (decode_result < 0) {
                playback_error = 1;
                break;
            }
            if (decode_result == 0) {
                break;
            }
        }

        if (playback_error) {
            break;
        }

        PCMChunk *chunk = pcm_queue_peek(&pcm_queue);
        if (!chunk) {
            if (decoder_finished) {
                reached_end_of_stream = 1;
                break;
            }
            continue;
        }

        apply_volume_to_samples(chunk->data, chunk->frame_count * output_channels);
        push_visualizer_samples(chunk->data, chunk->frame_count, output_channels);
        if (audio_backend_write_samples(chunk->data, chunk->frame_count) < 0) {
            update_controls_status(audio_text("写入音频设备失败", "Audio device write failed"));
            playback_error = 1;
            break;
        }

        progress_tracker_add_samples(chunk->frame_count);
        g_current_position = progress_tracker_get_position_seconds();
        pcm_queue_consume(&pcm_queue);
    }
    
cleanup:
    audio_backend_cleanup_stream();

    if (pcm_queue_initialized) {
        pcm_queue_destroy(&pcm_queue);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    progress_tracker_on_stop();
    reset_visualizer_state();

    if (!reached_end_of_stream) {
        g_current_position = 0;
    }

    pthread_mutex_lock(&g_play_mutex);
    if (g_play_thread_running && reached_end_of_stream) {
        switch (g_loop_mode) {
            case LOOP_SINGLE:
                followup_index = index;
                break;
            case LOOP_LIST:
                if (g_playlist.count > 0) {
                    followup_index = (index + 1) % g_playlist.count;
                }
                break;
            case LOOP_RANDOM:
                if (g_playlist.count > 0) {
                    followup_index = rand() % g_playlist.count;
                }
                break;
            case LOOP_OFF:
            default:
                break;
        }
    }

    g_play_thread_running = 0;
    g_play_thread_finished = 1;
    g_play_state = PLAY_STATE_STOPPED;
    g_current_play_index = -1;

    if (followup_index >= 0) {
        g_pending_playback_index = followup_index;
    }
    pthread_mutex_unlock(&g_play_mutex);

    if (followup_index < 0) {
        clear_lyrics();
        request_ui_refresh(UI_DIRTY_LYRICS);
    }
    request_ui_refresh(UI_DIRTY_PLAYLIST | UI_DIRTY_CONTROLS);
    return NULL;
}

/**
 * 播放指定索引的音频文件
 * 如果已有播放线程在运行，则先停止当前播放
 */
void play_audio(int index) {
    if (index < 0 || index >= g_playlist.count) {
        return;
    }

    reap_finished_playback_thread();
    
    pthread_mutex_lock(&g_play_mutex);
    
    if (g_play_state == PLAY_STATE_PLAYING && g_current_play_index == index) {
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    int was_running = g_play_thread_running;
    g_pending_playback_index = -1;
    
    if (was_running) {
        g_play_thread_running = 0;
        g_seek_request = 0;
        
        pthread_mutex_unlock(&g_play_mutex);
        signal_playback_thread();
        
        pthread_join(g_play_thread, NULL);
        
        pthread_mutex_lock(&g_play_mutex);
        g_play_thread_finished = 0;
    }
    
    g_current_play_index = index;
    g_selected_index = index;
    g_play_thread_running = 1;
    g_play_thread_finished = 0;
    g_play_state = PLAY_STATE_STOPPED;
    
    pthread_mutex_unlock(&g_play_mutex);
    
    int *index_ptr = malloc(sizeof(int));
    if (!index_ptr) {
        pthread_mutex_lock(&g_play_mutex);
        g_current_play_index = -1;
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    *index_ptr = index;
    
    if (pthread_create(&g_play_thread, NULL, play_audio_thread, index_ptr) != 0) {
        free(index_ptr);
        pthread_mutex_lock(&g_play_mutex);
        g_play_thread_running = 0;
        g_play_state = PLAY_STATE_STOPPED;
        g_current_play_index = -1;
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }

    pthread_mutex_lock(&g_play_mutex);
    g_play_state = PLAY_STATE_PLAYING;
    pthread_mutex_unlock(&g_play_mutex);
    signal_playback_thread();
    
    char msg[64];
    snprintf(msg, sizeof(msg), "%s%s - %s",
        audio_text("正在播放：", "Playing: "),
        g_playlist.tracks[index].title, g_playlist.tracks[index].artist);
    update_controls_status(msg);
    add_history_entry(&g_playlist.tracks[index]);
    render_playlist_content();
    request_ui_refresh(UI_DIRTY_CONTROLS);
    
    load_lyrics(g_playlist.tracks[index].path);
    render_lyrics();
}

/**
 * 暂停当前播放的音频
 * 仅在播放状态下有效
 */
void pause_audio() {
    pthread_mutex_lock(&g_play_mutex);
    
    // 二次验证：确保仍在播放状态且播放线程运行中
    if (g_play_state != PLAY_STATE_PLAYING || !g_play_thread_running) {
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    // 验证当前播放索引有效
    if (g_current_play_index < 0 || g_current_play_index >= g_playlist.count) {
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    // 设置暂停状态
    g_play_state = PLAY_STATE_PAUSED;
    
    audio_backend_pause_stream();
    
    pthread_mutex_unlock(&g_play_mutex);
    signal_playback_thread();
    
    // 通知进度跟踪器（在锁外调用，避免死锁）
    progress_tracker_on_pause();
    render_playlist_content();
}

/**
 * 恢复已暂停的音频播放
 * 仅在暂停状态下有效
 */
void resume_audio() {
    pthread_mutex_lock(&g_play_mutex);
    
    // 二次验证：确保仍在暂停状态且播放线程运行中
    if (g_play_state != PLAY_STATE_PAUSED || !g_play_thread_running) {
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    // 验证当前播放索引有效
    if (g_current_play_index < 0 || g_current_play_index >= g_playlist.count) {
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    // 设置播放状态
    g_play_state = PLAY_STATE_PLAYING;
    
    audio_backend_resume_stream();
    
    pthread_mutex_unlock(&g_play_mutex);
    signal_playback_thread();
    
    /* 通知进度跟踪器（在锁外调用，避免死锁） */
    progress_tracker_on_resume();
    render_playlist_content();
    update_progress_bar();   // 确保进度条立刻同步
    update_lyrics_display(); // 确保歌词从新位置高亮
}

/**
 * 停止当前播放的音频
 * 停止播放线程并重置播放状态
 */
void stop_audio() {
    reap_finished_playback_thread();

    pthread_mutex_lock(&g_play_mutex);  // 新增：加锁保护
    
    // 先设置停止标志
    int was_running = g_play_thread_running;
    g_play_thread_running = 0;
    g_pending_playback_index = -1;
    
    // 清除跳转请求，避免跳转线程继续执行
    g_seek_request = 0;

    pthread_mutex_unlock(&g_play_mutex);  // 新增：解锁
    signal_playback_thread();
    
    // 重置播放状态
    g_play_state = PLAY_STATE_STOPPED;
    g_current_position = 0;
    
    // 重置进度跟踪器
    progress_tracker_on_stop();
    
    // 等待线程结束（在锁外等待，避免死锁）
    if (was_running) {
        pthread_join(g_play_thread, NULL);
        pthread_mutex_lock(&g_play_mutex);
        g_play_thread_finished = 0;
        pthread_mutex_unlock(&g_play_mutex);
    }
    
    g_current_play_index = -1;
    
    // 清空歌词
    clear_lyrics();
    reset_visualizer_state();
    
    render_playlist_content();
    render_controls();  // 新增：更新控制栏
    render_lyrics();    // 更新歌词显示
}

/**
 * 播放下一曲
 * 根据当前循环模式决定下一曲的选择逻辑
 */
void next_track() {
    if (g_playlist.count == 0) {
        return;
    }
    
    int next_index;
    if (g_loop_mode == LOOP_RANDOM) {
        // 随机播放
        next_index = rand() % g_playlist.count;
    } else {
        // 顺序播放，使用当前播放索引或选中索引
        if (g_current_play_index >= 0) {
            next_index = g_current_play_index + 1;
        } else {
            next_index = g_selected_index + 1;
        }

        // 循环到列表开头
        if (next_index >= g_playlist.count) {
            next_index = 0;
        }
    }
    
    play_audio(next_index);
}

/**
 * 播放上一曲
 * 根据当前循环模式决定上一曲的选择逻辑
 */
void prev_track() {
    if (g_playlist.count == 0) {
        return;
    }
    
    int prev_index;
    if (g_loop_mode == LOOP_RANDOM) {
        // 随机播放
        prev_index = rand() % g_playlist.count;
    } else {
        // 顺序播放，使用当前播放索引或选中索引
        if (g_current_play_index >= 0) {
            prev_index = g_current_play_index - 1;
        } else {
            prev_index = g_selected_index - 1;
        }

        // 循环到列表末尾
        if (prev_index < 0) {
            prev_index = g_playlist.count - 1;
        }
    }
    
    play_audio(prev_index);
}

/**
 * 跳转到指定位置
 * 通过停止当前播放线程再重新启动，确保从目标位置准确开始播放
 */
void seek_audio(double position) {
    reap_finished_playback_thread();

    // 参数校验
    if (position < 0) position = 0;
    if (g_total_duration > 0 && position > g_total_duration) position = g_total_duration;
    
    int int_position = (int)position;
    
    // 如果未在播放，仅更新 UI 状态
    if (g_play_state == PLAY_STATE_STOPPED || !g_play_thread_running) {
        pthread_mutex_lock(&g_play_mutex);
        g_current_position = int_position;
        pthread_mutex_unlock(&g_play_mutex);
        
        // 同步进度跟踪器（如果已初始化）
        if (progress_tracker_is_ready()) {
            progress_tracker_seek(int_position);
        }
        
        update_progress_bar();
        render_controls();
        return;
    }

    pthread_mutex_lock(&g_seek_mutex);
    g_seek_position = int_position;
    g_current_position = int_position;
    g_seek_request = 1;
    pthread_mutex_unlock(&g_seek_mutex);
    signal_playback_thread();

    update_progress_bar();
    render_controls();
    render_playlist_content();
    update_lyrics_display();
}

/**
 * 获取初始跳转位置（用于重启播放时）
 * 调用后会清除存储的位置，避免重复使用
 */
int get_and_clear_initial_seek_position(void) {
    int pos = g_initial_seek_position;
    g_initial_seek_position = 0;
    return pos;
}
