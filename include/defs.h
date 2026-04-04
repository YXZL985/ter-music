#ifndef DEFS_H
#define DEFS_H

#include <stddef.h>
#include <pthread.h>
#include "progress.h"
#include <time.h>

#define APP_NAME "ter-music"
#define APP_VERSION "v1.2.0"
#define APP_AUTHOR "燕戏竹林"
#define APP_EMAIL "yxzl666xx@outlook.com"
#define APP_REPO "https://gitee.com/yanxi-bamboo-forest/ter-music.git"

typedef enum {
    LOOP_OFF = 0,
    LOOP_SINGLE = 1,
    LOOP_LIST = 2,
    LOOP_RANDOM = 3
} LoopMode;

typedef enum {
    PLAY_STATE_STOPPED = 0,
    PLAY_STATE_PLAYING = 1,
    PLAY_STATE_PAUSED = 2
} PlayState;

typedef enum {
    VIEW_MAIN = 0,
    VIEW_SETTINGS = 1,
    VIEW_HISTORY = 2,
    VIEW_PLAYLIST = 3,
    VIEW_FAVORITES = 4,
    VIEW_INFO = 5
} ViewMode;

typedef enum {
    FOCUS_SIDEBAR = 0,
    FOCUS_CONTENT = 1
} FocusArea;

#define COLOR_PAIR_BORDER 1
#define COLOR_PAIR_PLAYLIST 2
#define COLOR_PAIR_CONTROLS 3
#define COLOR_PAIR_LYRICS 4
#define COLOR_PAIR_SIDEBAR 5
#define COLOR_PAIR_HIGHLIGHT 6

#define UI_DIRTY_PLAYLIST 0x01
#define UI_DIRTY_CONTROLS 0x02
#define UI_DIRTY_LYRICS 0x04

#define CONTROL_COUNT 7
#define MAX_AUDIO_BUFFER_SIZE (44100 * 2 * sizeof(int16_t))
#define MAX_PATH_LEN 512
#define MAX_TRACKS 1000
#define MAX_META_LEN 256

#define MAX_HISTORY_COUNT 100
#define MAX_FAVORITES_COUNT 200
#define MAX_DIR_HISTORY_COUNT 50
#define MAX_PLAYLISTS_COUNT 50
#define MAX_PLAYLIST_NAME_LEN 64

typedef struct {
    char path[MAX_PATH_LEN];
    char title[MAX_META_LEN];
    char artist[MAX_META_LEN];
    char album[MAX_META_LEN];
} Track;

typedef struct {
    char path[MAX_PATH_LEN];
    time_t open_time;
} DirHistoryEntry;

typedef struct {
    DirHistoryEntry entries[MAX_DIR_HISTORY_COUNT];
    int count;
} DirHistory;

typedef struct {
    char path[MAX_PATH_LEN];
    char title[MAX_META_LEN];
    char artist[MAX_META_LEN];
    time_t play_time;
} HistoryEntry;

typedef struct {
    HistoryEntry entries[MAX_HISTORY_COUNT];
    int count;
} PlayHistory;

typedef struct {
    Track tracks[MAX_FAVORITES_COUNT];
    int count;
} Favorites;

typedef struct {
    char name[MAX_PLAYLIST_NAME_LEN];
    Track tracks[MAX_TRACKS];
    int track_count;
    time_t created_time;
    time_t modified_time;
} UserPlaylist;

typedef struct {
    UserPlaylist playlists[MAX_PLAYLISTS_COUNT];
    int count;
} PlaylistManager;

typedef struct {
    int playlist_fg;
    int playlist_bg;
    int controls_fg;
    int controls_bg;
    int lyrics_fg;
    int lyrics_bg;
    int sidebar_fg;
    int sidebar_bg;
    int highlight_fg;
    int highlight_bg;
    int border_fg;
    int border_bg;
} ColorTheme;

typedef struct {
    char default_startup_path[MAX_PATH_LEN];
    char last_opened_path[MAX_PATH_LEN];
    ColorTheme theme;
    int auto_play_on_start;
    int remember_last_path;
    int clear_history_on_startup;
    int volume_percent;
    int audio_latency_ms;
} AppConfig;

typedef struct {
    Track tracks[MAX_TRACKS];
    int count;
    char folder_path[MAX_PATH_LEN];
    int is_loaded;
} Playlist;

extern Playlist g_playlist;
extern int g_selected_index;
extern int g_control_focus;
extern int g_current_control_idx;
extern PlayState g_play_state;
extern int g_current_play_index;
extern LoopMode g_loop_mode;
extern pthread_t g_play_thread;
extern int g_play_thread_running;
extern char g_default_audio_device[128];

extern ViewMode g_current_view;
extern int g_menu_selected_idx;
extern PlayHistory g_play_history;
extern Favorites g_favorites;
extern DirHistory g_dir_history;
extern PlaylistManager g_playlist_manager;
extern AppConfig g_app_config;

extern int g_content_selected_idx;
extern FocusArea g_focus_area;

extern int g_current_position;
extern int g_total_duration;
extern int g_initial_seek_position;
extern pthread_mutex_t g_seek_mutex;

extern int g_lyric_cursor_mode;
extern int g_lyric_cursor_index;

void init_ffmpeg();
void init_audio_device();
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
void seek_audio(double position);
int get_and_clear_initial_seek_position(void);
int get_volume_percent(void);
void set_volume_percent(int volume);
void adjust_volume(int delta);

int utf8_str_truncate(char *dest, const char *src, int max_cols);
int utf8_str_width(const char *src);
int utf8_str_substring(char *dest, const char *src, int start_col, int max_cols);
int utf8_str_pad(char *dest, size_t dest_size, const char *src, int width);
void decode_html_entities(char *str);
int use_ascii_fallback_ui(void);

void update_progress_bar();
void update_controls_status(const char *msg);
void request_ui_refresh(int dirty_mask);
void process_pending_ui_refresh(void);
void reap_finished_playback_thread(void);
void process_pending_playback_action(void);

void apply_color_theme(void);

#endif
