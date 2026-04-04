#include "../include/defs.h"
#include "../include/menu_views.h"
#include "../include/progress.h"  // 新增：进度跟踪模块
#include "../include/lyrics.h"    // 新增：歌词模块
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

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

static int16_t *audio_buffer = NULL;
static int buffer_size = 0;
static int buffer_pos = 0;
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static int audio_backend_prepare_stream(void) {
#if defined(HAVE_PULSE)
    if (!pa_connected || !pa_ctx) {
        update_controls_status("PulseAudio 未连接");
        return -1;
    }

    pa_ss.format = PA_SAMPLE_S16LE;
    pa_ss.rate = 44100;
    pa_ss.channels = 2;

    pa_stream *new_stream = pa_stream_new(pa_ctx, "playback", &pa_ss, NULL);
    if (!new_stream) {
        update_controls_status("无法创建 PulseAudio 播放流");
        return -1;
    }

    pa_buffer_attr ba;
    memset(&ba, 0, sizeof(ba));
    ba.maxlength = (uint32_t)-1;
    ba.tlength = (uint32_t)-1;
    ba.prebuf = (uint32_t)-1;
    ba.minreq = (uint32_t)-1;
    ba.fragsize = (uint32_t)-1;

    if (pa_stream_connect_playback(new_stream, NULL, &ba, PA_STREAM_NOFLAGS, NULL, NULL) < 0) {
        update_controls_status("无法连接 PulseAudio 播放流");
        pa_stream_unref(new_stream);
        return -1;
    }

    while (pa_stream_get_state(new_stream) != PA_STREAM_READY) {
        if (pa_stream_get_state(new_stream) == PA_STREAM_FAILED ||
            pa_stream_get_state(new_stream) == PA_STREAM_TERMINATED) {
            update_controls_status("PulseAudio 播放流初始化失败");
            pa_stream_unref(new_stream);
            return -1;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }

    pa_s = new_stream;
    return 0;
#else
    if (!alsa_ready) {
        update_controls_status("ALSA 后端未就绪");
        return -1;
    }

    if (snd_pcm_open(&alsa_pcm, g_default_audio_device, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        update_controls_status("无法打开 ALSA 设备");
        alsa_pcm = NULL;
        return -1;
    }

    if (snd_pcm_set_params(alsa_pcm,
                           SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED,
                           2,
                           44100,
                           1,
                           500000) < 0) {
        update_controls_status("无法配置 ALSA 设备");
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
#if defined(HAVE_PULSE)
    size_t bytes = (size_t)frame_count * pa_ss.channels * sizeof(int16_t);

    while (pa_stream_writable_size(pa_s) < bytes) {
        if (!g_play_thread_running) {
            return 0;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }

    return pa_stream_write(pa_s, samples, bytes, NULL, 0, PA_SEEK_RELATIVE);
#else
    int written = 0;

    while (written < frame_count) {
        snd_pcm_sframes_t ret = snd_pcm_writei(alsa_pcm, samples + (written * 2), frame_count - written);
        if (ret > 0) {
            written += (int)ret;
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
        printf("警告：无法创建 PulseAudio 主循环\n");
        return;
    }
    
    pa_ctx = pa_context_new(pa_mainloop_get_api(pa_ml), APP_NAME);
    if (!pa_ctx) {
        printf("警告：无法创建 PulseAudio 上下文\n");
        pa_mainloop_free(pa_ml);
        pa_ml = NULL;
        return;
    }
    
    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
        if (pa_context_get_state(pa_ctx) == PA_CONTEXT_FAILED ||
            pa_context_get_state(pa_ctx) == PA_CONTEXT_TERMINATED) {
            printf("警告：无法连接 PulseAudio 服务\n");
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
    printf("已连接到 PulseAudio 服务\n");
#else
    alsa_ready = 1;
    printf("当前使用 ALSA 音频后端\n");
#endif
}

extern void render_playlist_content();
extern void render_controls();

/**
 * 获取循环模式的字符串表示
 */
const char *get_loop_mode_str() {
    switch(g_loop_mode) {
        case LOOP_OFF:
            return "关闭";
        case LOOP_SINGLE:
            return "单曲";
        case LOOP_LIST:
            return "列表";
        case LOOP_RANDOM:
            return "随机";
        default:
            return "关闭";
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
    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) != 0) {
        update_controls_status("无法打开音频文件");
        goto cleanup;
    }
    
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        update_controls_status("无法读取音频流信息");
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
    
    // 初始化进度跟踪器（使用默认采样率 44100）
    progress_tracker_init(44100);
    
    // 检查是否需要跳转到特定位置（在开始播放之前）
    // 如果是重启跳转，从全局变量获取目标位置
    int initial_seek_position = get_and_clear_initial_seek_position(); // 获取初始跳转位置，获取后清除
    
    // 强制更新 UI 显示，确保进度条从 0% 开始
    request_ui_refresh(UI_DIRTY_CONTROLS);
    
    // 找到音频流
    int audio_stream_index = -1;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    
    if (audio_stream_index == -1) {
        update_controls_status("未找到音频流");
        goto cleanup;
    }
    
    // 获取解码器
    AVCodecParameters *codec_par = fmt_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
    if (!codec) {
        update_controls_status("当前编解码器不受支持");
        goto cleanup;
    }
    
    // 创建解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        update_controls_status("无法分配解码器上下文");
        goto cleanup;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0) {
        update_controls_status("无法复制编解码参数");
        goto cleanup;
    }
    
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        update_controls_status("无法打开编解码器");
        goto cleanup;
    }
    
    // 音频重采样
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        update_controls_status("无法分配重采样器");
        goto cleanup;
    }
    
    // 设置重采样参数
    AVChannelLayout in_ch_layout = codec_ctx->ch_layout;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 44100, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    
    if (swr_init(swr_ctx) < 0) {
        update_controls_status("无法初始化重采样器");
        goto cleanup;
    }
    
    // 分配帧和数据包
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        update_controls_status("无法分配解码数据结构");
        goto cleanup;
    }
    
    // 创建音频缓冲区
    audio_buffer = malloc(MAX_AUDIO_BUFFER_SIZE);
    if (!audio_buffer) {
        update_controls_status("无法分配音频缓冲区");
        goto cleanup;
    }
    buffer_size = 0;
    buffer_pos = 0;
    
    if (audio_backend_prepare_stream() < 0) {
        goto cleanup;
    }
    
    // 使用固定输出采样率更新进度跟踪器
    progress_tracker_set_sample_rate(44100);
    
    progress_tracker_start();
    
    // 播放状态设置为播放中
    g_play_state = PLAY_STATE_PLAYING;
    
    // 在开始播放前执行初始跳转（如果有）
    // 注意：UI 刷新由主线程在创建线程后完成，避免多线程同时刷新 ncurses
    if (initial_seek_position > 0 && initial_seek_position < g_total_duration) {
        // 计算目标时间戳
        AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;
        int64_t target_ts = av_rescale_q(initial_seek_position, (AVRational){1, 1}, time_base);
        
        // 执行跳转
        int ret = av_seek_frame(fmt_ctx, audio_stream_index, target_ts, 0);
        if (ret >= 0) {
            // 清空解码器缓冲区
            avcodec_flush_buffers(codec_ctx);
            // 重置当前位置
            g_current_position = initial_seek_position;
            // 同步进度跟踪器
            progress_tracker_seek(initial_seek_position);
            // 注意：UI 刷新由主线程完成，避免多线程同时刷新 ncurses
        }
    }

    // 解码和播放循环
    while (g_play_thread_running) {
        // === 新增：实时处理 seek 请求（无论播放/暂停）===
        pthread_mutex_lock(&g_seek_mutex);
        if (g_seek_request && g_play_thread_running && fmt_ctx && codec_ctx) {
            int target_position = g_seek_position;
            g_seek_request = 0;
            AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;
            int64_t target_ts = av_rescale_q(target_position, (AVRational){1, 1}, time_base);

            // BUGFIX 2026.03.26: 使用正确的 audio stream index
            // 指定 audio_stream_index 确保跳转到正确的流
            // 不使用 AVSEEK_FLAG_BACKWARD，直接跳转到不小于目标位置的关键帧，定位更精确
            int ret = av_seek_frame(fmt_ctx, audio_stream_index, target_ts, 0);
            if (ret < 0) {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                update_controls_status("跳转失败");
                // 失败时不重置 UI
            } else {
                // 成功：flush 解码器缓冲，避免残留旧帧
                avcodec_flush_buffers(codec_ctx);

                // 重置 swresample 上下文（清除缓冲区，但保持对象有效）
                if (swr_ctx) {
                    swr_init(swr_ctx);
                }

                // 同步 tracker 和 UI（防止脱节）
                progress_tracker_seek(target_position);
                g_current_position = target_position;

                audio_backend_flush_stream();

                char msg[64];
                snprintf(msg, sizeof(msg), "已跳转到 %02d:%02d", target_position / 60, target_position % 60);
                update_controls_status(msg);
                
                // 如果跳转到总时长位置，立即停止播放
                if (target_position >= g_total_duration) {
                    pthread_mutex_unlock(&g_seek_mutex);
                    g_play_thread_running = 0;
                    break;
                }
                
                // 重要：如果当前 packet 已经被读取，需要释放它
                // 这样下一轮循环会直接从新位置读取新 packet
                if (packet) {
                    av_packet_unref(packet);
                }
                
                // 重要：跳过本次循环，下一轮从新位置读取新数据
                pthread_mutex_unlock(&g_seek_mutex);
                continue;
            }
        }
        pthread_mutex_unlock(&g_seek_mutex);
        
        // 检查是否需要暂停（在读取帧之前检查）
        if (g_play_state == PLAY_STATE_PAUSED) {
            while (g_play_state == PLAY_STATE_PAUSED && g_play_thread_running) {
                usleep(100000);
            }
        }
        
        if (!g_play_thread_running) {
            break;
        }
        
        if (av_read_frame(fmt_ctx, packet) < 0) {
            reached_end_of_stream = 1;
            break; // 文件读取完毕
        }
        
        if (packet->stream_index == audio_stream_index) {
            if (avcodec_send_packet(codec_ctx, packet) < 0) {
                break;
            }
            
            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                // 重采样音频帧
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                    44100, codec_ctx->sample_rate, AV_ROUND_UP);
                
                // 分配输出帧
                uint8_t *output_data;
                int ret = av_samples_alloc(&output_data, NULL, 2, dst_nb_samples,
                                          AV_SAMPLE_FMT_S16, 0);
                if (ret < 0) {
                    break; // 分配失败，跳过这一帧
                }
                
                // 执行重采样
                int converted_samples = swr_convert(swr_ctx, &output_data, dst_nb_samples,
                                                   (const uint8_t**)frame->data, frame->nb_samples);
                if (converted_samples > 0) {
                    // 累加样本数到进度跟踪器（无论 PulseAudio 写入是否成功）
                    progress_tracker_add_samples(converted_samples);
                    
                    // 更新全局位置变量（用于 UI 显示）- 每帧都更新
                    // 但如果已有跳转请求，跳过更新以避免干扰跳转
                    if (!g_seek_request) {
                        g_current_position = progress_tracker_get_position_seconds();
                    }
                    
                    int16_t *samples = (int16_t*)output_data;
                    if (audio_backend_write_samples(samples, converted_samples) < 0) {
                        char err_msg[128];
                        snprintf(err_msg, sizeof(err_msg), "写入音频设备失败");
                        update_controls_status(err_msg);
                    }
                }
                
                // 释放输出帧内存
                av_freep(&output_data);
                
                // 检查是否需要暂停（在处理每帧后检查）
                if (g_play_state == PLAY_STATE_PAUSED) {
                    while (g_play_state == PLAY_STATE_PAUSED && g_play_thread_running) {
                        usleep(100000);
                    }
                }
                
                if (!g_play_thread_running) {
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }
    
cleanup:
    audio_backend_cleanup_stream();
    
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
    }
    
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    progress_tracker_on_stop();

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
        
        pthread_join(g_play_thread, NULL);
        
        pthread_mutex_lock(&g_play_mutex);
        g_play_thread_finished = 0;
    }
    
    g_current_play_index = index;
    g_selected_index = index;
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
        g_current_play_index = -1;
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }

    pthread_mutex_lock(&g_play_mutex);
    g_play_thread_running = 1;
    g_play_thread_finished = 0;
    g_play_state = PLAY_STATE_PLAYING;
    pthread_mutex_unlock(&g_play_mutex);
    
    char msg[64];
    snprintf(msg, sizeof(msg), "正在播放：%s - %s",
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
    
    audio_backend_cleanup_stream();
    
    pthread_mutex_unlock(&g_play_mutex);  // 新增：解锁
    
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
