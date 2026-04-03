#include "../include/defs.h"
#include "../include/progress.h"  // 新增：进度跟踪模块
#include "../include/lyrics.h"    // 新增：歌词模块
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// PulseAudio 头文件
#include <pulse/pulseaudio.h>

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

// PulseAudio 相关全局变量
static pa_mainloop *pa_ml = NULL;
static pa_context *pa_ctx = NULL;
static pa_stream *pa_s = NULL;
static pa_sample_spec pa_ss;
static int pa_connected = 0;
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

/**
 * 初始化 PulseAudio 连接
 * 连接到 PulseAudio 守护进程并验证其可用性
 */
void init_audio_device() {
    pa_ml = pa_mainloop_new();
    if (!pa_ml) {
        printf("Warning: Failed to create PulseAudio mainloop\n");
        return;
    }
    
    pa_ctx = pa_context_new(pa_mainloop_get_api(pa_ml), APP_NAME);
    if (!pa_ctx) {
        printf("Warning: Failed to create PulseAudio context\n");
        pa_mainloop_free(pa_ml);
        pa_ml = NULL;
        return;
    }
    
    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
        if (pa_context_get_state(pa_ctx) == PA_CONTEXT_FAILED ||
            pa_context_get_state(pa_ctx) == PA_CONTEXT_TERMINATED) {
            printf("Warning: Failed to connect to PulseAudio daemon\n");
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
    printf("Connected to PulseAudio daemon\n");
}

// 声明外部函数（用于UI模块）
extern void update_controls_status(const char *msg);
extern void render_playlist_content();
extern void render_controls();

/**
 * 获取循环模式的字符串表示
 */
const char *get_loop_mode_str() {
    switch(g_loop_mode) {
        case LOOP_OFF:
            return "Off";
        case LOOP_SINGLE:
            return "Single";
        case LOOP_LIST:
            return "List";
        case LOOP_RANDOM:
            return "Random";
        default:
            return "Off";
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

/**
 * 播放音频文件的线程函数
 * 负责解码音频文件并通过PulseAudio输出到音频设备
 */
void *play_audio_thread(void *arg) {
    int index = *((int *)arg);
    free(arg);
    
    pthread_mutex_lock(&g_play_mutex);
    if (index < 0 || index >= g_playlist.count || !g_play_thread_running) {
        pthread_mutex_unlock(&g_play_mutex);
        return NULL;
    }
    
    char file_path[MAX_PATH_LEN];
    strncpy(file_path, g_playlist.tracks[index].path, MAX_PATH_LEN - 1);
    file_path[MAX_PATH_LEN - 1] = '\0';
    
    pthread_mutex_unlock(&g_play_mutex);

    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) != 0) {
        update_controls_status("Failed to open audio file");
        return NULL;
    }
    
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        update_controls_status("Failed to find stream info");
        avformat_close_input(&fmt_ctx);
        return NULL;
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
    
    // 注意：不再此处调用 progress_tracker_start()
    // wall-clock 计时将在 PulseAudio 设备完全初始化后启动
    
    // 检查是否需要跳转到特定位置（在开始播放之前）
    // 如果是重启跳转，从全局变量获取目标位置
    int initial_seek_position = get_and_clear_initial_seek_position(); // 获取初始跳转位置，获取后清除
    
    // 强制更新 UI 显示，确保进度条从 0% 开始
    render_controls();
    
    // 找到音频流
    int audio_stream_index = -1;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    
    if (audio_stream_index == -1) {
        update_controls_status("No audio stream found");
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 获取解码器
    AVCodecParameters *codec_par = fmt_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
    if (!codec) {
        update_controls_status("Unsupported codec");
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 创建解码器上下文
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        update_controls_status("Failed to allocate codec context");
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0) {
        update_controls_status("Failed to copy codec parameters");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        update_controls_status("Failed to open codec");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 音频重采样
    SwrContext *swr_ctx = swr_alloc();
    if (!swr_ctx) {
        update_controls_status("Failed to allocate resampler");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
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
        update_controls_status("Failed to initialize resampler");
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 分配帧和数据包
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        update_controls_status("Failed to allocate packet or frame");
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 创建音频缓冲区
    audio_buffer = malloc(MAX_AUDIO_BUFFER_SIZE);
    if (!audio_buffer) {
        update_controls_status("Failed to allocate audio buffer");
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    buffer_size = 0;
    buffer_pos = 0;
    
    // 初始化 PulseAudio 播放流
    if (!pa_connected || !pa_ctx) {
        update_controls_status("PulseAudio not connected");
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 设置音频参数：44100 Hz, 立体声, 16-bit little-endian
    pa_ss.format = PA_SAMPLE_S16LE;
    pa_ss.rate = 44100;
    pa_ss.channels = 2;
    
    // 创建播放流
    pa_stream *new_stream = pa_stream_new(pa_ctx, "playback", &pa_ss, NULL);
    if (!new_stream) {
        update_controls_status("Failed to create PulseAudio stream");
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 设置缓冲属性 - 使用较小的缓冲区以减少延迟
    pa_buffer_attr ba;
    memset(&ba, 0, sizeof(ba));
    ba.maxlength = (uint32_t)-1;
    ba.tlength = (uint32_t)-1;
    ba.prebuf = (uint32_t)-1;
    ba.minreq = (uint32_t)-1;
    ba.fragsize = (uint32_t)-1;
    
    // 连接播放流
    if (pa_stream_connect_playback(new_stream, NULL, &ba, PA_STREAM_NOFLAGS, NULL, NULL) < 0) {
        update_controls_status("Failed to connect PulseAudio playback stream");
        pa_stream_unref(new_stream);
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 等待流就绪
    while (pa_stream_get_state(new_stream) != PA_STREAM_READY) {
        if (pa_stream_get_state(new_stream) == PA_STREAM_FAILED ||
            pa_stream_get_state(new_stream) == PA_STREAM_TERMINATED) {
            update_controls_status("PulseAudio stream failed to become ready");
            pa_stream_unref(new_stream);
            free(audio_buffer);
            audio_buffer = NULL;
            swr_free(&swr_ctx);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return NULL;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }
    
    pa_s = new_stream;
    
    // 使用固定输出采样率更新进度跟踪器
    progress_tracker_set_sample_rate(44100);
    
    // 启动 wall-clock 计时，确保与音频解码起点精确对齐
    // 此时 FFmpeg 解码器和 PulseAudio 流已完全初始化，不会有额外延迟
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
            g_seek_request = 0;
            int64_t target_ts = (int64_t)g_seek_position * AV_TIME_BASE;

            // BUGFIX 2026.03.26: 使用正确的 audio stream index
            // 指定 audio_stream_index 确保跳转到正确的流
            // 不使用 AVSEEK_FLAG_BACKWARD，直接跳转到不小于目标位置的关键帧，定位更精确
            int ret = av_seek_frame(fmt_ctx, audio_stream_index, target_ts, 0);
            if (ret < 0) {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                update_controls_status("Seek failed");
                // 失败时不重置 UI
            } else {
                // 成功：flush 解码器缓冲，避免残留旧帧
                avcodec_flush_buffers(codec_ctx);

                // 重置 swresample 上下文（清除缓冲区，但保持对象有效）
                if (swr_ctx) {
                    swr_init(swr_ctx);
                }

                // 同步 tracker 和 UI（防止脱节）
                progress_tracker_seek(g_seek_position);
                g_current_position = g_seek_position;

                // PulseAudio：清空缓冲区，丢弃旧数据避免爆音或延迟
                // 空指针检查：必须同时检查 pa_s、pa_ml 和 pa_ctx 都有效才能执行操作
                if (pa_s && pa_ml && pa_ctx) {
                    // 再次检查流状态，避免多线程竞争导致状态在检查后调用前改变
                    pa_stream_state_t state = pa_stream_get_state(pa_s);
                    if (state == PA_STREAM_READY) {
                        pa_operation *op = pa_stream_flush(pa_s, NULL, NULL);
                        if (op) {
                            // 等待 flush 操作完成，确保缓冲区真正被清空
                            // 这解决了"UI跳了但音乐还在播放旧内容"的核心问题
                            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                                pa_mainloop_iterate(pa_ml, 1, NULL);
                            }
                            pa_operation_unref(op);
                        }
                    }
                }

                char msg[64];
                snprintf(msg, sizeof(msg), "Seeked to %02d:%02d", g_seek_position / 60, g_seek_position % 60);
                update_controls_status(msg);
                
                // 如果跳转到总时长位置，立即停止播放
                if (g_seek_position >= g_total_duration) {
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
                    
                    // 写入 PulseAudio 流
                    int16_t *samples = (int16_t*)output_data;
                    size_t bytes = converted_samples * pa_ss.channels * sizeof(int16_t);
                    
                    // 等待可写空间
                    while (pa_stream_writable_size(pa_s) < (int)bytes) {
                        if (!g_play_thread_running) break;
                        pa_mainloop_iterate(pa_ml, 1, NULL);
                    }
                    
                    if (pa_stream_write(pa_s, samples, bytes, NULL, 0, PA_SEEK_RELATIVE) < 0) {
                        char err_msg[128];
                        snprintf(err_msg, sizeof(err_msg), "Failed to write audio to PulseAudio stream");
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
    
    // 清理资源
    if (pa_s) {
        pa_stream_disconnect(pa_s);
        pa_stream_unref(pa_s);
        pa_s = NULL;
    }
    
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
    }
    
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    
    // 播放完成，处理下一曲
    if (g_play_thread_running) {
        switch (g_loop_mode) {
            case LOOP_SINGLE:
                // 单曲循环，重新播放当前歌曲
                play_audio(g_current_play_index);
                break;
            case LOOP_LIST:
                // 列表循环，播放下一首
                next_track();
                break;
            case LOOP_RANDOM:
                // 随机播放，随机选择一首歌曲
                if (g_playlist.count > 0) {
                    int random_index = rand() % g_playlist.count;
                    play_audio(random_index);
                } else {
                    g_play_state = PLAY_STATE_STOPPED;
                    g_current_play_index = -1;
                }
                break;
            case LOOP_OFF:
            default:
                // 关闭循环，停止播放
                g_play_state = PLAY_STATE_STOPPED;
                g_current_play_index = -1;
                break;
        }
    }
    
    g_play_thread_running = 0;
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
    
    pthread_mutex_lock(&g_play_mutex);
    
    if (g_play_state == PLAY_STATE_PLAYING && g_current_play_index == index) {
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    int was_running = g_play_thread_running;
    
    if (was_running) {
        g_play_thread_running = 0;
        g_seek_request = 0;
        
        pthread_mutex_unlock(&g_play_mutex);
        
        pthread_join(g_play_thread, NULL);
        
        pthread_mutex_lock(&g_play_mutex);
    }
    
    g_current_play_index = index;
    g_selected_index = index;
    g_play_thread_running = 1;
    
    pthread_mutex_unlock(&g_play_mutex);
    
    int *index_ptr = malloc(sizeof(int));
    if (!index_ptr) {
        return;
    }
    *index_ptr = index;
    
    if (pthread_create(&g_play_thread, NULL, play_audio_thread, index_ptr) != 0) {
        free(index_ptr);
        pthread_mutex_lock(&g_play_mutex);
        g_play_thread_running = 0;
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Playing: %s - %s",
        g_playlist.tracks[index].title, g_playlist.tracks[index].artist);
    update_controls_status(msg);
    render_playlist_content();
    
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
    
    // PulseAudio 流操作 - 只发起异步请求，不等待完成
    // PulseAudio mainloop 处理由播放线程在 pa_mainloop_iterate 中完成
    if (pa_s && pa_ml && pa_ctx) {
        pa_stream_state_t state = pa_stream_get_state(pa_s);
        
        // 只对流处于 READY 状态执行 cork 操作
        if (state == PA_STREAM_READY) {
            if (!pa_stream_is_corked(pa_s)) {
                pa_operation *op = pa_stream_cork(pa_s, 1, NULL, NULL);
                if (op) {
                    pa_operation_unref(op);
                }
            }
        } else if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) {
            fprintf(stderr, "Pause: stream in invalid state %d\n", state);
            pa_s = NULL;
        }
    }
    
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
    
    /* 恢复 PulseAudio 流 - 只发起异步请求，不等待完成
     * PulseAudio mainloop 处理由播放线程在 pa_mainloop_iterate 中完成
     */
    if (pa_s && pa_ml && pa_ctx) {
        pa_stream_state_t state = pa_stream_get_state(pa_s);
        
        // 只对流处于 READY 状态执行 uncork 操作
        if (state == PA_STREAM_READY) {
            // 检查流是否已经被 cork（只有 corked 状态才能 uncork）
            if (pa_stream_is_corked(pa_s)) {
                pa_operation *op = pa_stream_cork(pa_s, 0, NULL, NULL);
                if (op) {
                    pa_operation_unref(op);
                }
            }
        } else if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) {
            fprintf(stderr, "Resume: stream in invalid state %d\n", state);
            pa_s = NULL;
        }
    }
    
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
    pthread_mutex_lock(&g_play_mutex);  // 新增：加锁保护
    
    // 先设置停止标志
    int was_running = g_play_thread_running;
    g_play_thread_running = 0;
    
    // 清除跳转请求，避免跳转线程继续执行
    g_seek_request = 0;
    
    // 如果 PulseAudio 流正在播放，先断开它
    if (pa_s) {
        pa_stream_disconnect(pa_s);
        pa_stream_unref(pa_s);
        pa_s = NULL;
    }
    
    pthread_mutex_unlock(&g_play_mutex);  // 新增：解锁
    
    // 重置播放状态
    g_play_state = PLAY_STATE_STOPPED;
    g_current_position = 0;
    
    // 重置进度跟踪器
    progress_tracker_on_stop();
    
    // 等待线程结束（在锁外等待，避免死锁）
    if (was_running) {
        pthread_join(g_play_thread, NULL);
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
    
    // 正在播放：停止当前播放线程，然后从目标位置重新启动
     // 这种方式虽然会有轻微延迟，但能彻底解决缓冲区残留问题，保证跳转准确性
     pthread_mutex_lock(&g_play_mutex);
     
     int current_index = g_current_play_index;
     int was_running = g_play_thread_running;
     
     if (was_running) {
         g_play_thread_running = 0;
         g_seek_request = 0;
         
         pthread_mutex_unlock(&g_play_mutex);
         
         // 等待播放线程结束，确保所有资源都被释放
         // 注意：旧线程退出时可能会修改 g_play_state
         pthread_join(g_play_thread, NULL);
         
         pthread_mutex_lock(&g_play_mutex);
     }
     
     // 重置播放状态，必须放在 pthread_join 之后！
     // 因为旧线程退出时可能会把 g_play_state 设置为 STOPPED
     g_play_state = PLAY_STATE_PLAYING;
     g_current_play_index = current_index;
     g_selected_index = current_index;
     g_play_thread_running = 1;
     g_initial_seek_position = int_position; // 存储初始跳转位置
     
     pthread_mutex_unlock(&g_play_mutex);
    
    // 重新分配索引指针并创建播放线程
    int *index_ptr = malloc(sizeof(int));
    if (!index_ptr) {
        return;
    }
    *index_ptr = current_index;
    
    if (pthread_create(&g_play_thread, NULL, play_audio_thread, index_ptr) != 0) {
        free(index_ptr);
        pthread_mutex_lock(&g_play_mutex);
        g_play_thread_running = 0;
        pthread_mutex_unlock(&g_play_mutex);
        return;
    }
    
    // 更新 UI 显示
    update_progress_bar();
    render_controls();
    render_playlist_content();
    // 刷新歌词显示，同步到跳转后的位置，保留编辑模式和光标位置
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