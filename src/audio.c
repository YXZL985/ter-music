#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// ALSA 头文件
#include <alsa/asoundlib.h>

// FFmpeg 头文件
#include <ffmpeg/libavformat/avformat.h>
#include <ffmpeg/libavcodec/avcodec.h>
#include <ffmpeg/libswresample/swresample.h>
#include <ffmpeg/libavutil/opt.h>
#include <ffmpeg/libavutil/channel_layout.h>
#include <ffmpeg/libavutil/samplefmt.h>

// 确保DT_REG和DT_UNKNOWN被定义
#ifndef DT_REG
#define DT_REG 8
#endif

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

// 音频输出相关全局变量
static snd_pcm_t *audio_handle = NULL;
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

// 进度条相关变量
int g_current_position = 0; // 当前播放位置（秒）
int g_total_duration = 0; // 当前歌曲总时长（秒） 

// 全局变量：默认音频设备名称
char g_default_audio_device[128] = "default";

/**
 * 初始化FFmpeg库
 */
void init_ffmpeg() {
    avformat_network_init();
    // 禁用FFmpeg日志输出，避免干扰UI
    av_log_set_level(AV_LOG_QUIET);
}

/**
 * 初始化音频设备
 * 尝试打开默认音频设备并验证其可用性
 */
void init_audio_device() {
    int err;
    snd_pcm_t *test_handle;
    
    // 尝试打开默认音频设备
    err = snd_pcm_open(&test_handle, g_default_audio_device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        printf("Warning: Failed to open default audio device: %s\n", snd_strerror(err));
    } else {
        printf("Selected audio device: %s\n", g_default_audio_device);
        snd_pcm_close(test_handle);
    }
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
 * 负责解码音频文件并通过ALSA输出到音频设备
 */
void *play_audio_thread(void *arg) {
    int index = *((int *)arg);
    free(arg); // 释放内存
    if (index < 0 || index >= g_playlist.count) {
        return NULL;
    }
    
    const char *file_path = g_playlist.tracks[index].path;

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
    
    // 检查是否需要跳转到特定位置（在开始播放之前）
    int initial_seek_position = g_current_position; // 使用当前全局位置作为初始跳转位置
    
    g_current_position = 0; // 重置当前播放位置
    
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
    
    // 初始化 ALSA
    int err;
    snd_pcm_hw_params_t *params;
    
    // 打开默认音频设备
    err = snd_pcm_open(&audio_handle, g_default_audio_device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Failed to open audio device: %s", snd_strerror(err));
        update_controls_status(err_msg);
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 分配硬件参数对象
    snd_pcm_hw_params_alloca(&params);
    
    // 重置参数
    err = snd_pcm_hw_params_any(audio_handle, params);
    if (err < 0) {
        update_controls_status("Failed to initialize hardware parameters");
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 设置访问模式
    err = snd_pcm_hw_params_set_access(audio_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        update_controls_status("Failed to set access mode");
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 设置采样格式
    err = snd_pcm_hw_params_set_format(audio_handle, params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        update_controls_status("Failed to set sample format");
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 设置采样率
    unsigned int rate = 44100;
    err = snd_pcm_hw_params_set_rate_near(audio_handle, params, &rate, NULL);
    if (err < 0) {
        update_controls_status("Failed to set sample rate");
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 设置通道数
    unsigned int channels = 2;
    err = snd_pcm_hw_params_set_channels(audio_handle, params, channels);
    if (err < 0) {
        update_controls_status("Failed to set channels");
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 应用参数
    err = snd_pcm_hw_params(audio_handle, params);
    if (err < 0) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Failed to apply hardware parameters: %s", snd_strerror(err));
        update_controls_status(err_msg);
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 准备音频设备
    err = snd_pcm_prepare(audio_handle);
    if (err < 0) {
        update_controls_status("Failed to prepare audio device");
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
        free(audio_buffer);
        audio_buffer = NULL;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return NULL;
    }
    
    // 播放状态设置为播放中
    g_play_state = PLAY_STATE_PLAYING;
    
    // 在开始播放前执行初始跳转（如果有）
    if (initial_seek_position > 0 && initial_seek_position < g_total_duration) {
        // 计算目标时间戳
        AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;
        int64_t target_ts = av_rescale_q(initial_seek_position, (AVRational){1, 1}, time_base);
        
        // 执行跳转
        int ret = av_seek_frame(fmt_ctx, audio_stream_index, target_ts, AVSEEK_FLAG_BACKWARD);
        if (ret >= 0) {
            // 清空解码器缓冲区
            avcodec_flush_buffers(codec_ctx);
            // 重置当前位置
            g_current_position = initial_seek_position;
            // 更新UI显示
            render_controls();
        }
    }

    // 解码和播放循环
    while (g_play_thread_running) {
        // 检查是否有跳转请求（移到循环开始处）
        if (g_seek_request) {
            // 计算目标时间戳
            AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;
            int64_t target_ts = av_rescale_q(g_seek_position, (AVRational){1, 1}, time_base);
            
            // 执行跳转
            int ret = av_seek_frame(fmt_ctx, audio_stream_index, target_ts, AVSEEK_FLAG_BACKWARD);
            if (ret >= 0) {
                // 清空解码器缓冲区
                avcodec_flush_buffers(codec_ctx);
                // 重置当前位置
                g_current_position = g_seek_position;
                // 清除跳转请求
                g_seek_request = 0;
                // 更新UI显示
                render_controls();
            } else {
                // 跳转失败，清除请求
                g_seek_request = 0;
            }
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
                    // 直接写入 ALSA 设备
                    int16_t *samples = (int16_t*)output_data;
                    int frames = converted_samples;
                    
                    int written = snd_pcm_writei(audio_handle, samples, frames);
                    if (written < 0) {
                        // 处理错误
                        if (written == -EPIPE) {
                            // 管道破裂，尝试恢复
                            snd_pcm_prepare(audio_handle);
                            written = snd_pcm_writei(audio_handle, samples, frames);
                        }
                        if (written < 0) {
                            char err_msg[128];
                            snprintf(err_msg, sizeof(err_msg), "Failed to write audio: %s", snd_strerror(written));
                            update_controls_status(err_msg);
                        }
                    }

                    // 更新当前播放位置（秒）
                    if (frame->pts != AV_NOPTS_VALUE) {
                        AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;
                        g_current_position = av_q2d(time_base) * frame->pts;
                    }
                }
                
                // 释放输出帧内存
                av_freep(&output_data);
                
                // 检查是否需要暂停
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
    if (audio_handle) {
        snd_pcm_drain(audio_handle);
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
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
    // 检查索引有效性
    if (index < 0 || index >= g_playlist.count) {
        return;
    }

    // 如果请求播放的曲目与当前播放的相同，且正在播放，则不做任何操作
    if (g_play_state == PLAY_STATE_PLAYING && g_current_play_index == index) {
        return;
    }

    // 停止当前播放
    if (g_play_thread_running) {
        g_play_thread_running = 0;
        // 关闭ALSA设备以立即停止播放
        if (audio_handle) {
            snd_pcm_drop(audio_handle);
            snd_pcm_close(audio_handle);
            audio_handle = NULL;
        }
        // 等待线程结束
        pthread_join(g_play_thread, NULL);
    }
    
    // 设置当前播放索引
    g_current_play_index = index;
    g_selected_index = index;
    
    // 启动播放线程
    g_play_thread_running = 1;
    int *index_ptr = malloc(sizeof(int));
    *index_ptr = index;
    pthread_create(&g_play_thread, NULL, play_audio_thread, index_ptr);
    
    // 更新状态
    char msg[64];
    snprintf(msg, sizeof(msg), "Playing: %s - %s", 
             g_playlist.tracks[index].title, g_playlist.tracks[index].artist);
    update_controls_status(msg);
    render_playlist_content();
}

/**
 * 暂停当前播放的音频
 * 仅在播放状态下有效
 */
void pause_audio() {
    if (g_play_state == PLAY_STATE_PLAYING) {
        g_play_state = PLAY_STATE_PAUSED;
        // 暂停ALSA设备
        if (audio_handle) {
            snd_pcm_pause(audio_handle, 1);
        }
        // 不再调用update_controls_status，避免阻塞
        render_playlist_content();
    }
}

/**
 * 恢复已暂停的音频播放
 * 仅在暂停状态下有效
 */
void resume_audio() {
    if (g_play_state == PLAY_STATE_PAUSED) {
        g_play_state = PLAY_STATE_PLAYING;
        // 恢复ALSA设备
        if (audio_handle) {
            snd_pcm_pause(audio_handle, 0);
        }
        // 不再调用update_controls_status，避免阻塞
        render_playlist_content();
    }
}

/**
 * 停止当前播放的音频
 * 停止播放线程并重置播放状态
 */
void stop_audio() {
    // 先设置停止标志
    int was_running = g_play_thread_running;
    g_play_thread_running = 0;

    // 如果ALSA设备正在播放，先关闭它
    if (audio_handle) {
        snd_pcm_drop(audio_handle);
        snd_pcm_close(audio_handle);
        audio_handle = NULL;
    }

    // 等待线程结束
    if (was_running) {
        pthread_join(g_play_thread, NULL);
    }

    // 重置播放状态
    g_play_state = PLAY_STATE_STOPPED;
    g_current_play_index = -1;
    // 不再调用update_controls_status，避免阻塞
    render_playlist_content();
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
 * 设置跳转请求标志和目标位置
 */
void seek_audio(int position) {
    if (position >= 0 && position <= g_total_duration) {
        // 如果有播放线程正在运行，则设置跳转请求供播放线程处理
        if (g_play_thread_running) {
            g_seek_request = 1;
            g_seek_position = position;
        } else {
            // 如果没有播放线程运行但存在当前播放的歌曲，则更新位置并播放
            if (g_current_play_index >= 0) {
                // 更新全局位置，这样下次播放时会从这个位置开始
                g_current_position = position;
                // 重新开始播放，从指定位置开始
                play_audio(g_current_play_index);
            } else if (g_selected_index >= 0) {
                // 如果没有当前播放的歌曲但有选中的歌曲，则更新位置并播放选中的歌曲
                g_current_position = position;
                play_audio(g_selected_index);
            }
        }
    }
}