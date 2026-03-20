#ifndef DEFS_H
#define DEFS_H

#include <pthread.h>

// 循环模式定义
typedef enum {
    LOOP_OFF = 0,      // 关闭循环
    LOOP_SINGLE = 1,   // 单曲循环
    LOOP_LIST = 2,     // 列表内循环
    LOOP_RANDOM = 3    // 随机循环
} LoopMode;

// 播放状态定义
typedef enum {
    PLAY_STATE_STOPPED = 0,
    PLAY_STATE_PLAYING = 1,
    PLAY_STATE_PAUSED = 2
} PlayState;

// 定义颜色对索引
#define COLOR_PAIR_BORDER 1
#define COLOR_PAIR_PLAYLIST 2
#define COLOR_PAIR_CONTROLS 3
#define COLOR_PAIR_LYRICS 4

// 定义控件数量
#define CONTROL_COUNT 6

// 最大音频缓冲区大小（约1秒的立体声音频）
#define MAX_AUDIO_BUFFER_SIZE (44100 * 2 * sizeof(int16_t))

// 定义最大路径长度和歌曲数量
#define MAX_PATH_LEN 512
#define MAX_TRACKS 1000
#define MAX_META_LEN 256

// 音轨结构体
typedef struct {
    char path[MAX_PATH_LEN];
    char title[MAX_META_LEN];
    char artist[MAX_META_LEN];
    char album[MAX_META_LEN];
} Track;

// 播放列表结构体
typedef struct {
    Track tracks[MAX_TRACKS];
    int count;
    char folder_path[MAX_PATH_LEN];
    int is_loaded;
} Playlist;

extern Playlist g_playlist; // 全局播放列表
extern int g_selected_index;  // 当前选中的歌曲索引
extern int g_control_focus; 
extern int g_current_control_idx;
extern PlayState g_play_state; // 当前播放状态
extern int g_current_play_index; // 当前播放的歌曲索引
extern LoopMode g_loop_mode; // 当前循环模式
extern pthread_t g_play_thread; // 播放线程
extern int g_play_thread_running; // 播放线程运行状态
extern char g_default_audio_device[128]; // 默认音频设备名称

// 进度条相关全局变量
extern int g_current_position; // 当前播放位置（秒）
extern int g_total_duration; // 当前歌曲总时长（秒）

// FFmpeg 初始化函数声明
void init_ffmpeg();

// 音频设备初始化函数声明
void init_audio_device();

// 音频控制函数声明
int load_playlist(const char *folder_path);
void play_audio(int index);
void pause_audio();
void resume_audio();
void stop_audio();
void prev_track();
void next_track();
void toggle_loop_mode();
const char *get_loop_mode_str();
void cleanup();
void seek_audio(int position);

// UI 辅助函数声明
int utf8_str_truncate(char *dest, const char *src, int max_cols);

// 进度条更新函数声明
void update_progress_bar();

#endif