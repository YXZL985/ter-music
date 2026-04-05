/**
 * @file menu_views.c
 * @brief 菜单视图模块实现
 * 
 * 实现底部菜单栏功能，支持 F1-F6 功能键切换不同界面
 * 包括：设置、历史记录、播放列表、收藏夹、信息、退出
 * 
 * @author 燕戏竹林 (yxzl666xx@outlook.com)
 * @date 2026-03-22
 */

#include "../include/defs.h"
#include "../include/lyrics.h"
#include "../include/menu_views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>

extern WINDOW *win_playlist;
extern WINDOW *win_controls;
extern WINDOW *win_lyrics;

extern void render_playlist_content(void);
extern void render_controls(void);
extern void create_layout(void);

ViewMode g_current_view = VIEW_MAIN;
int g_menu_selected_idx = 0;
PlayHistory g_play_history = {0};
Favorites g_favorites = {0};
DirHistory g_dir_history = {0};
PlaylistManager g_playlist_manager = {0};
AppConfig g_app_config = {0};

int g_content_selected_idx = 0;
FocusArea g_focus_area = FOCUS_SIDEBAR;

static char config_dir[MAX_PATH_LEN];
static char config_file[MAX_PATH_LEN];
static char history_file[MAX_PATH_LEN];
static char favorites_file[MAX_PATH_LEN];
static char dir_history_file[MAX_PATH_LEN];
static char playlists_dir[MAX_PATH_LEN];
static char temp_playlist_file[MAX_PATH_LEN];

static char g_status_message[256] = "";
static time_t g_status_message_time = 0;

static const char *settings_sidebar_items[] = {
    "颜色主题",
    "默认路径",
    "播放设置",
    "快捷键",
    "← 返回"
};
#define SETTINGS_ITEM_COUNT 5

static const char *history_sidebar_items[] = {
    "目录历史",
    "清空历史",
    "← 返回"
};
#define HISTORY_ITEM_COUNT 3

static const char *playlist_sidebar_items[] = {
    "全部歌单",
    "新建歌单",
    "← 返回"
};
#define PLAYLIST_ITEM_COUNT 3

static const char *favorites_sidebar_items[] = {
    "全部收藏",
    "← 返回"
};
#define FAVORITES_ITEM_COUNT 2

static const char *info_sidebar_items[] = {
    "关于",
    "仓库地址",
    "← 返回"
};
#define INFO_ITEM_COUNT 3

static const char *settings_sidebar_items_ascii[] = {
    "Theme",
    "Default Path",
    "Playback",
    "Hotkeys",
    "<- Back"
};

static const char *history_sidebar_items_ascii[] = {
    "Folder History",
    "Clear History",
    "<- Back"
};

static const char *playlist_sidebar_items_ascii[] = {
    "All Playlists",
    "New Playlist",
    "<- Back"
};

static const char *favorites_sidebar_items_ascii[] = {
    "All Favorites",
    "<- Back"
};

static const char *info_sidebar_items_ascii[] = {
    "About",
    "Repository",
    "<- Back"
};

static const char *color_names[] = {
    "黑色", "红色", "绿色", "黄色",
    "蓝色", "洋红", "青色", "白色"
};

static const char *color_names_ascii[] = {
    "Black", "Red", "Green", "Yellow",
    "Blue", "Magenta", "Cyan", "White"
};

static int ncurses_colors[] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
};

static const char *menu_text(const char *utf8, const char *ascii) {
    return use_english_ui() ? ascii : utf8;
}

static const char *menu_bool_text(int enabled) {
    return enabled ? menu_text("是", "On") : menu_text("否", "Off");
}

static const char *menu_color_name(int color_value) {
    const char **names = use_english_ui() ? color_names_ascii : color_names;
    if (color_value < 0 || color_value >= 8) {
        return menu_text("未知", "Unknown");
    }
    return names[color_value];
}

static const char *menu_language_name(int language) {
    return language == UI_LANG_EN ? "English" : menu_text("中文", "Chinese");
}

static void sanitize_ascii_menu_text(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }

    dest[0] = '\0';
    if (!src || src[0] == '\0') {
        return;
    }

    size_t write = 0;
    int prev_space = 1;
    int saw_non_ascii = 0;

    for (size_t read = 0; src[read] != '\0' && write + 1 < dest_size; read++) {
        unsigned char c = (unsigned char)src[read];

        if (c < 0x80) {
            if (isspace(c)) {
                if (!prev_space) {
                    dest[write++] = ' ';
                    prev_space = 1;
                }
            } else if (isprint(c)) {
                dest[write++] = (char)c;
                prev_space = 0;
            }
        } else {
            saw_non_ascii = 1;
            if (!prev_space && write + 1 < dest_size) {
                dest[write++] = ' ';
                prev_space = 1;
            }
        }
    }

    while (write > 0 && dest[write - 1] == ' ') {
        write--;
    }
    dest[write] = '\0';

    if (write == 0 && saw_non_ascii) {
        snprintf(dest, dest_size, "[status]");
    }
}

static const char **resolve_sidebar_items(const char **items) {
    if (!use_english_ui()) {
        return items;
    }
    if (items == settings_sidebar_items) {
        return settings_sidebar_items_ascii;
    }
    if (items == history_sidebar_items) {
        return history_sidebar_items_ascii;
    }
    if (items == playlist_sidebar_items) {
        return playlist_sidebar_items_ascii;
    }
    if (items == favorites_sidebar_items) {
        return favorites_sidebar_items_ascii;
    }
    if (items == info_sidebar_items) {
        return info_sidebar_items_ascii;
    }
    return items;
}

static const char *resolve_menu_title(const char *title) {
    if (!use_english_ui() || !title) {
        return title;
    }
    if (strcmp(title, "设置 [F2]") == 0) return "Settings [F2]";
    if (strcmp(title, "历史 [F3]") == 0) return "History [F3]";
    if (strcmp(title, "歌单 [F4]") == 0) return "Playlists [F4]";
    if (strcmp(title, "收藏 [F5]") == 0) return "Favorites [F5]";
    if (strcmp(title, "信息 [F6]") == 0) return "Info [F6]";
    return title;
}

static char* extract_json_string(const char *json, const char *key, char *output, size_t output_size) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    const char *pos = strstr(json, search_key);
    if (!pos) {
        output[0] = '\0';
        return output;
    }
    
    pos = strchr(pos, ':');
    if (!pos) {
        output[0] = '\0';
        return output;
    }
    
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    
    if (*pos == '"') {
        pos++;
        size_t i = 0;
        while (*pos && *pos != '"' && i < output_size - 1) {
            if (*pos == '\\' && *(pos + 1)) {
                pos++;
                switch (*pos) {
                    case 'n': output[i++] = '\n'; break;
                    case 't': output[i++] = '\t'; break;
                    case '"': output[i++] = '"'; break;
                    case '\\': output[i++] = '\\'; break;
                    default: output[i++] = *pos; break;
                }
                pos++;
            } else {
                output[i++] = *pos++;
            }
        }
        output[i] = '\0';
    } else {
        size_t i = 0;
        while (*pos && *pos != ',' && *pos != '}' && *pos != ']' && i < output_size - 1) {
            if (*pos != ' ' && *pos != '\t' && *pos != '\n' && *pos != '\r') {
                output[i++] = *pos;
            }
            pos++;
        }
        output[i] = '\0';
    }
    
    return output;
}

static long extract_json_int(const char *json, const char *key) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    const char *pos = strstr(json, search_key);
    if (!pos) return 0;
    
    pos = strchr(pos, ':');
    if (!pos) return 0;
    
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    
    return atol(pos);
}

static void escape_json_string(const char *src, char *dest, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dest_size - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j < dest_size - 2) {
                dest[j++] = '\\';
                dest[j++] = c;
            }
        } else if (c == '\n') {
            if (j < dest_size - 2) {
                dest[j++] = '\\';
                dest[j++] = 'n';
            }
        } else if (c == '\t') {
            if (j < dest_size - 2) {
                dest[j++] = '\\';
                dest[j++] = 't';
            }
        } else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

void ensure_config_dir_exists(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    uid_t uid = getuid();
    snprintf(config_dir, sizeof(config_dir), "%s/.config/ter-music", home);
    snprintf(config_file, sizeof(config_file), "%s/config.json", config_dir);
    snprintf(history_file, sizeof(history_file), "%s/history.json", config_dir);
    snprintf(favorites_file, sizeof(favorites_file), "%s/favorites.json", config_dir);
    snprintf(dir_history_file, sizeof(dir_history_file), "%s/dir_history.json", config_dir);
    snprintf(playlists_dir, sizeof(playlists_dir), "%s/playlists", config_dir);
    snprintf(temp_playlist_file, sizeof(temp_playlist_file), "/tmp/ter-music-%d-temp-playlist.json", (int)uid);

    mkdir(config_dir, 0755);
    mkdir(playlists_dir, 0755);
}

void init_default_config(void) {
    memset(&g_app_config, 0, sizeof(AppConfig));
    
    const char *xdg_music_home = getenv("XDG_MUSIC_HOME");
    if (xdg_music_home && xdg_music_home[0] != '\0') {
        strncpy(g_app_config.default_startup_path, xdg_music_home, MAX_PATH_LEN - 1);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            struct stat st;
            char candidate[MAX_PATH_LEN];
            
            snprintf(candidate, sizeof(candidate), "%s/Music", home);
            if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
                strncpy(g_app_config.default_startup_path, candidate, MAX_PATH_LEN - 1);
            } else {
                snprintf(candidate, sizeof(candidate), "%s/音乐", home);
                if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
                    strncpy(g_app_config.default_startup_path, candidate, MAX_PATH_LEN - 1);
                } else {
                    snprintf(candidate, sizeof(candidate), "%s/Música", home);
                    if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
                        strncpy(g_app_config.default_startup_path, candidate, MAX_PATH_LEN - 1);
                    } else {
                        snprintf(candidate, sizeof(candidate), "%s/Musique", home);
                        if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
                            strncpy(g_app_config.default_startup_path, candidate, MAX_PATH_LEN - 1);
                        } else {
                            snprintf(candidate, sizeof(candidate), "%s/Musik", home);
                            if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
                                strncpy(g_app_config.default_startup_path, candidate, MAX_PATH_LEN - 1);
                            } else {
                                snprintf(g_app_config.default_startup_path, MAX_PATH_LEN, "%s/Music", home);
                            }
                        }
                    }
                }
            }
        }
    }
    
    g_app_config.theme.playlist_fg = COLOR_WHITE;
    g_app_config.theme.playlist_bg = COLOR_BLACK;
    g_app_config.theme.controls_fg = COLOR_YELLOW;
    g_app_config.theme.controls_bg = COLOR_BLACK;
    g_app_config.theme.lyrics_fg = COLOR_GREEN;
    g_app_config.theme.lyrics_bg = COLOR_BLACK;
    g_app_config.theme.sidebar_fg = COLOR_CYAN;
    g_app_config.theme.sidebar_bg = COLOR_BLACK;
    g_app_config.theme.highlight_fg = COLOR_BLACK;
    g_app_config.theme.highlight_bg = COLOR_WHITE;
    g_app_config.theme.border_fg = COLOR_CYAN;
    g_app_config.theme.border_bg = COLOR_BLACK;
    
    g_app_config.auto_play_on_start = 0;
    g_app_config.remember_last_path = 1;
    g_app_config.clear_history_on_startup = 0;
    g_app_config.resume_last_playback = 0;
    g_app_config.last_played_position = 0;
    g_app_config.last_played_folder_path[0] = '\0';
    g_app_config.last_played_track_path[0] = '\0';
    g_app_config.ui_language = UI_LANG_ZH;
    g_app_config.volume_percent = 100;
    g_app_config.audio_latency_ms = 80;
}

void apply_color_theme(void) {
    if (!has_colors()) return;
    
    init_pair(COLOR_PAIR_PLAYLIST, g_app_config.theme.playlist_fg, g_app_config.theme.playlist_bg);
    init_pair(COLOR_PAIR_CONTROLS, g_app_config.theme.controls_fg, g_app_config.theme.controls_bg);
    init_pair(COLOR_PAIR_LYRICS, g_app_config.theme.lyrics_fg, g_app_config.theme.lyrics_bg);
    init_pair(COLOR_PAIR_SIDEBAR, g_app_config.theme.sidebar_fg, g_app_config.theme.sidebar_bg);
    init_pair(COLOR_PAIR_HIGHLIGHT, g_app_config.theme.highlight_fg, g_app_config.theme.highlight_bg);
    init_pair(COLOR_PAIR_BORDER, g_app_config.theme.border_fg, g_app_config.theme.border_bg);
}

void load_config(void) {
    FILE *f = fopen(config_file, "r");
    if (!f) {
        init_default_config();
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        init_default_config();
        return;
    }
    
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);
    
    init_default_config();
    
    extract_json_string(json, "default_startup_path", g_app_config.default_startup_path, MAX_PATH_LEN);
    extract_json_string(json, "last_opened_path", g_app_config.last_opened_path, MAX_PATH_LEN);
    extract_json_string(json, "last_played_folder_path", g_app_config.last_played_folder_path, MAX_PATH_LEN);
    extract_json_string(json, "last_played_track_path", g_app_config.last_played_track_path, MAX_PATH_LEN);
    
    const char *theme_pos = strstr(json, "\"theme\"");
    if (theme_pos) {
        char theme_section[1024];
        const char *theme_start = strchr(theme_pos, '{');
        const char *theme_end = strchr(theme_start ? theme_start : theme_pos, '}');
        if (theme_start && theme_end && theme_end > theme_start) {
            size_t len = theme_end - theme_start + 1;
            if (len < sizeof(theme_section)) {
                strncpy(theme_section, theme_start, len);
                theme_section[len] = '\0';
                
                g_app_config.theme.playlist_fg = (int)extract_json_int(theme_section, "playlist_fg");
                g_app_config.theme.playlist_bg = (int)extract_json_int(theme_section, "playlist_bg");
                g_app_config.theme.controls_fg = (int)extract_json_int(theme_section, "controls_fg");
                g_app_config.theme.controls_bg = (int)extract_json_int(theme_section, "controls_bg");
                g_app_config.theme.lyrics_fg = (int)extract_json_int(theme_section, "lyrics_fg");
                g_app_config.theme.lyrics_bg = (int)extract_json_int(theme_section, "lyrics_bg");
                g_app_config.theme.sidebar_fg = (int)extract_json_int(theme_section, "sidebar_fg");
                g_app_config.theme.sidebar_bg = (int)extract_json_int(theme_section, "sidebar_bg");
                g_app_config.theme.highlight_fg = (int)extract_json_int(theme_section, "highlight_fg");
                g_app_config.theme.highlight_bg = (int)extract_json_int(theme_section, "highlight_bg");
                g_app_config.theme.border_fg = (int)extract_json_int(theme_section, "border_fg");
                g_app_config.theme.border_bg = (int)extract_json_int(theme_section, "border_bg");
            }
        }
    }
    
    g_app_config.auto_play_on_start = (int)extract_json_int(json, "auto_play_on_start");
    g_app_config.remember_last_path = (int)extract_json_int(json, "remember_last_path");
    g_app_config.clear_history_on_startup = (int)extract_json_int(json, "clear_history_on_startup");
    if (strstr(json, "\"resume_last_playback\"")) {
        g_app_config.resume_last_playback = (int)extract_json_int(json, "resume_last_playback");
    }
    if (strstr(json, "\"last_played_position\"")) {
        g_app_config.last_played_position = (int)extract_json_int(json, "last_played_position");
    }
    if (strstr(json, "\"ui_language\"")) {
        g_app_config.ui_language = (int)extract_json_int(json, "ui_language");
    }
    if (strstr(json, "\"volume_percent\"")) {
        g_app_config.volume_percent = (int)extract_json_int(json, "volume_percent");
    }
    if (strstr(json, "\"audio_latency_ms\"")) {
        g_app_config.audio_latency_ms = (int)extract_json_int(json, "audio_latency_ms");
    }

    g_app_config.resume_last_playback = g_app_config.resume_last_playback ? 1 : 0;
    if (g_app_config.last_played_position < 0) {
        g_app_config.last_played_position = 0;
    }
    if (g_app_config.ui_language != UI_LANG_EN) {
        g_app_config.ui_language = UI_LANG_ZH;
    }
    if (g_app_config.volume_percent < 0) {
        g_app_config.volume_percent = 0;
    }
    if (g_app_config.volume_percent > 100) {
        g_app_config.volume_percent = 100;
    }
    if (g_app_config.audio_latency_ms < 20) {
        g_app_config.audio_latency_ms = 20;
    }
    if (g_app_config.audio_latency_ms > 250) {
        g_app_config.audio_latency_ms = 250;
    }
    
    free(json);
}

void save_config(void) {
    FILE *f = fopen(config_file, "w");
    if (!f) return;
    
    char escaped_path[MAX_PATH_LEN * 2];
    
    fprintf(f, "{\n");
    
    escape_json_string(g_app_config.default_startup_path, escaped_path, sizeof(escaped_path));
    fprintf(f, "  \"default_startup_path\": \"%s\",\n", escaped_path);
    
    escape_json_string(g_app_config.last_opened_path, escaped_path, sizeof(escaped_path));
    fprintf(f, "  \"last_opened_path\": \"%s\",\n", escaped_path);

    escape_json_string(g_app_config.last_played_folder_path, escaped_path, sizeof(escaped_path));
    fprintf(f, "  \"last_played_folder_path\": \"%s\",\n", escaped_path);

    escape_json_string(g_app_config.last_played_track_path, escaped_path, sizeof(escaped_path));
    fprintf(f, "  \"last_played_track_path\": \"%s\",\n", escaped_path);
    
    fprintf(f, "  \"theme\": {\n");
    fprintf(f, "    \"playlist_fg\": %d,\n", g_app_config.theme.playlist_fg);
    fprintf(f, "    \"playlist_bg\": %d,\n", g_app_config.theme.playlist_bg);
    fprintf(f, "    \"controls_fg\": %d,\n", g_app_config.theme.controls_fg);
    fprintf(f, "    \"controls_bg\": %d,\n", g_app_config.theme.controls_bg);
    fprintf(f, "    \"lyrics_fg\": %d,\n", g_app_config.theme.lyrics_fg);
    fprintf(f, "    \"lyrics_bg\": %d,\n", g_app_config.theme.lyrics_bg);
    fprintf(f, "    \"sidebar_fg\": %d,\n", g_app_config.theme.sidebar_fg);
    fprintf(f, "    \"sidebar_bg\": %d,\n", g_app_config.theme.sidebar_bg);
    fprintf(f, "    \"highlight_fg\": %d,\n", g_app_config.theme.highlight_fg);
    fprintf(f, "    \"highlight_bg\": %d,\n", g_app_config.theme.highlight_bg);
    fprintf(f, "    \"border_fg\": %d,\n", g_app_config.theme.border_fg);
    fprintf(f, "    \"border_bg\": %d\n", g_app_config.theme.border_bg);
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"auto_play_on_start\": %d,\n", g_app_config.auto_play_on_start);
    fprintf(f, "  \"remember_last_path\": %d,\n", g_app_config.remember_last_path);
    fprintf(f, "  \"clear_history_on_startup\": %d,\n", g_app_config.clear_history_on_startup);
    fprintf(f, "  \"resume_last_playback\": %d,\n", g_app_config.resume_last_playback);
    fprintf(f, "  \"last_played_position\": %d,\n", g_app_config.last_played_position);
    fprintf(f, "  \"ui_language\": %d,\n", g_app_config.ui_language);
    fprintf(f, "  \"volume_percent\": %d,\n", g_app_config.volume_percent);
    fprintf(f, "  \"audio_latency_ms\": %d\n", g_app_config.audio_latency_ms);
    
    fprintf(f, "}\n");
    
    fclose(f);
}

void load_history(void) {
    FILE *f = fopen(history_file, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return;
    }
    
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);
    
    g_play_history.count = 0;
    
    const char *pos = strstr(json, "\"history\"");
    if (!pos) {
        free(json);
        return;
    }
    
    pos = strchr(pos, '[');
    if (!pos) {
        free(json);
        return;
    }
    
    const char *end = strchr(pos, ']');
    if (!end) {
        free(json);
        return;
    }
    
    while (pos < end && g_play_history.count < MAX_HISTORY_COUNT) {
        const char *obj_start = strchr(pos, '{');
        if (!obj_start || obj_start > end) break;
        
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > end) break;
        
        size_t obj_len = obj_end - obj_start + 1;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        
        strncpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';
        
        extract_json_string(obj, "path", g_play_history.entries[g_play_history.count].path, MAX_PATH_LEN);
        extract_json_string(obj, "title", g_play_history.entries[g_play_history.count].title, MAX_META_LEN);
        extract_json_string(obj, "artist", g_play_history.entries[g_play_history.count].artist, MAX_META_LEN);
        decode_html_entities(g_play_history.entries[g_play_history.count].title);
        decode_html_entities(g_play_history.entries[g_play_history.count].artist);
        g_play_history.entries[g_play_history.count].play_time = (time_t)extract_json_int(obj, "play_time");
        
        g_play_history.count++;
        free(obj);
        
        pos = obj_end + 1;
    }
    
    free(json);
}

void save_history(void) {
    FILE *f = fopen(history_file, "w");
    if (!f) return;
    
    fprintf(f, "{\n  \"history\": [\n");
    
    for (int i = 0; i < g_play_history.count; i++) {
        HistoryEntry *e = &g_play_history.entries[i];
        char escaped_path[MAX_PATH_LEN * 2];
        char escaped_title[MAX_META_LEN * 2];
        char escaped_artist[MAX_META_LEN * 2];
        
        escape_json_string(e->path, escaped_path, sizeof(escaped_path));
        escape_json_string(e->title, escaped_title, sizeof(escaped_title));
        escape_json_string(e->artist, escaped_artist, sizeof(escaped_artist));
        
        fprintf(f, "    {\n");
        fprintf(f, "      \"path\": \"%s\",\n", escaped_path);
        fprintf(f, "      \"title\": \"%s\",\n", escaped_title);
        fprintf(f, "      \"artist\": \"%s\",\n", escaped_artist);
        fprintf(f, "      \"play_time\": %ld\n", (long)e->play_time);
        fprintf(f, "    }%s\n", (i < g_play_history.count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n  \"count\": %d\n}\n", g_play_history.count);
    
    fclose(f);
}

void load_favorites(void) {
    FILE *f = fopen(favorites_file, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return;
    }
    
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);
    
    g_favorites.count = 0;
    
    const char *pos = strstr(json, "\"favorites\"");
    if (!pos) {
        free(json);
        return;
    }
    
    pos = strchr(pos, '[');
    if (!pos) {
        free(json);
        return;
    }
    
    const char *end = strchr(pos, ']');
    if (!end) {
        free(json);
        return;
    }
    
    while (pos < end && g_favorites.count < MAX_FAVORITES_COUNT) {
        const char *obj_start = strchr(pos, '{');
        if (!obj_start || obj_start > end) break;
        
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > end) break;
        
        size_t obj_len = obj_end - obj_start + 1;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        
        strncpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';
        
        extract_json_string(obj, "path", g_favorites.tracks[g_favorites.count].path, MAX_PATH_LEN);
        extract_json_string(obj, "title", g_favorites.tracks[g_favorites.count].title, MAX_META_LEN);
        extract_json_string(obj, "artist", g_favorites.tracks[g_favorites.count].artist, MAX_META_LEN);
        extract_json_string(obj, "album", g_favorites.tracks[g_favorites.count].album, MAX_META_LEN);
        decode_html_entities(g_favorites.tracks[g_favorites.count].title);
        decode_html_entities(g_favorites.tracks[g_favorites.count].artist);
        decode_html_entities(g_favorites.tracks[g_favorites.count].album);
        
        g_favorites.count++;
        free(obj);
        
        pos = obj_end + 1;
    }
    
    free(json);
}

void save_favorites(void) {
    FILE *f = fopen(favorites_file, "w");
    if (!f) return;
    
    fprintf(f, "{\n  \"favorites\": [\n");
    
    for (int i = 0; i < g_favorites.count; i++) {
        Track *t = &g_favorites.tracks[i];
        char escaped_path[MAX_PATH_LEN * 2];
        char escaped_title[MAX_META_LEN * 2];
        char escaped_artist[MAX_META_LEN * 2];
        char escaped_album[MAX_META_LEN * 2];
        
        escape_json_string(t->path, escaped_path, sizeof(escaped_path));
        escape_json_string(t->title, escaped_title, sizeof(escaped_title));
        escape_json_string(t->artist, escaped_artist, sizeof(escaped_artist));
        escape_json_string(t->album, escaped_album, sizeof(escaped_album));
        
        fprintf(f, "    {\n");
        fprintf(f, "      \"path\": \"%s\",\n", escaped_path);
        fprintf(f, "      \"title\": \"%s\",\n", escaped_title);
        fprintf(f, "      \"artist\": \"%s\",\n", escaped_artist);
        fprintf(f, "      \"album\": \"%s\"\n", escaped_album);
        fprintf(f, "    }%s\n", (i < g_favorites.count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n  \"count\": %d\n}\n", g_favorites.count);
    
    fclose(f);
}

void load_dir_history(void) {
    FILE *f = fopen(dir_history_file, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return;
    }
    
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);
    
    g_dir_history.count = 0;
    
    const char *pos = strstr(json, "\"directories\"");
    if (!pos) {
        free(json);
        return;
    }
    
    pos = strchr(pos, '[');
    if (!pos) {
        free(json);
        return;
    }
    
    const char *end = strchr(pos, ']');
    if (!end) {
        free(json);
        return;
    }
    
    while (pos < end && g_dir_history.count < MAX_DIR_HISTORY_COUNT) {
        const char *obj_start = strchr(pos, '{');
        if (!obj_start || obj_start > end) break;
        
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > end) break;
        
        size_t obj_len = obj_end - obj_start + 1;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        
        strncpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';
        
        extract_json_string(obj, "path", g_dir_history.entries[g_dir_history.count].path, MAX_PATH_LEN);
        g_dir_history.entries[g_dir_history.count].open_time = (time_t)extract_json_int(obj, "open_time");
        
        g_dir_history.count++;
        free(obj);
        
        pos = obj_end + 1;
    }
    
    free(json);
}

void save_dir_history(void) {
    FILE *f = fopen(dir_history_file, "w");
    if (!f) return;
    
    fprintf(f, "{\n  \"directories\": [\n");
    
    for (int i = 0; i < g_dir_history.count; i++) {
        DirHistoryEntry *e = &g_dir_history.entries[i];
        char escaped_path[MAX_PATH_LEN * 2];
        
        escape_json_string(e->path, escaped_path, sizeof(escaped_path));
        
        fprintf(f, "    {\n");
        fprintf(f, "      \"path\": \"%s\",\n", escaped_path);
        fprintf(f, "      \"open_time\": %ld\n", (long)e->open_time);
        fprintf(f, "    }%s\n", (i < g_dir_history.count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n  \"count\": %d\n}\n", g_dir_history.count);
    
    fclose(f);
}

void add_dir_history_entry(const char *path) {
    if (!path || strlen(path) == 0) return;
    
    for (int i = 0; i < g_dir_history.count; i++) {
        if (strcmp(g_dir_history.entries[i].path, path) == 0) {
            for (int j = i; j < g_dir_history.count - 1; j++) {
                g_dir_history.entries[j] = g_dir_history.entries[j + 1];
            }
            g_dir_history.count--;
            break;
        }
    }
    
    if (g_dir_history.count >= MAX_DIR_HISTORY_COUNT) {
        memmove(&g_dir_history.entries[0], &g_dir_history.entries[1],
                sizeof(DirHistoryEntry) * (MAX_DIR_HISTORY_COUNT - 1));
        g_dir_history.count = MAX_DIR_HISTORY_COUNT - 1;
    }
    
    strncpy(g_dir_history.entries[g_dir_history.count].path, path, MAX_PATH_LEN - 1);
    g_dir_history.entries[g_dir_history.count].open_time = time(NULL);
    g_dir_history.count++;
    
    save_dir_history();
}

void clear_dir_history(void) {
    g_dir_history.count = 0;
    save_dir_history();
}

void load_all_playlists(void) {
    DIR *dir = opendir(playlists_dir);
    if (!dir) return;
    
    g_playlist_manager.count = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && g_playlist_manager.count < MAX_PLAYLISTS_COUNT) {
        if (strstr(entry->d_name, ".json") == NULL) continue;
        
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", playlists_dir, entry->d_name);
        
        FILE *f = fopen(filepath, "r");
        if (!f) continue;
        
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char *json = malloc(fsize + 1);
        if (!json) {
            fclose(f);
            continue;
        }
        
        fread(json, 1, fsize, f);
        json[fsize] = '\0';
        fclose(f);
        
        UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_manager.count];
        memset(pl, 0, sizeof(UserPlaylist));
        
        extract_json_string(json, "name", pl->name, MAX_PLAYLIST_NAME_LEN);
        decode_html_entities(pl->name);
        pl->created_time = (time_t)extract_json_int(json, "created_time");
        pl->modified_time = (time_t)extract_json_int(json, "modified_time");
        
        const char *tracks_pos = strstr(json, "\"tracks\"");
        if (tracks_pos) {
            const char *arr_start = strchr(tracks_pos, '[');
            const char *arr_end = strchr(arr_start ? arr_start : tracks_pos, ']');
            
            if (arr_start && arr_end && arr_end > arr_start) {
                const char *pos = arr_start;
                
                while (pos < arr_end && pl->track_count < MAX_TRACKS) {
                    const char *obj_start = strchr(pos, '{');
                    if (!obj_start || obj_start > arr_end) break;
                    
                    const char *obj_end = strchr(obj_start, '}');
                    if (!obj_end || obj_end > arr_end) break;
                    
                    size_t obj_len = obj_end - obj_start + 1;
                    char *obj = malloc(obj_len + 1);
                    if (!obj) break;
                    
                    strncpy(obj, obj_start, obj_len);
                    obj[obj_len] = '\0';
                    
                    extract_json_string(obj, "path", pl->tracks[pl->track_count].path, MAX_PATH_LEN);
                    extract_json_string(obj, "title", pl->tracks[pl->track_count].title, MAX_META_LEN);
                    extract_json_string(obj, "artist", pl->tracks[pl->track_count].artist, MAX_META_LEN);
                    extract_json_string(obj, "album", pl->tracks[pl->track_count].album, MAX_META_LEN);
                    decode_html_entities(pl->tracks[pl->track_count].title);
                    decode_html_entities(pl->tracks[pl->track_count].artist);
                    decode_html_entities(pl->tracks[pl->track_count].album);
                    
                    pl->track_count++;
                    free(obj);
                    
                    pos = obj_end + 1;
                }
            }
        }
        
        g_playlist_manager.count++;
        free(json);
    }
    
    closedir(dir);
}

void save_all_playlists(void) {
    for (int i = 0; i < g_playlist_manager.count; i++) {
        UserPlaylist *pl = &g_playlist_manager.playlists[i];
        
        char filename[MAX_PLAYLIST_NAME_LEN + 8];
        char safe_name[MAX_PLAYLIST_NAME_LEN];
        
        int j = 0;
        for (const char *p = pl->name; *p && j < MAX_PLAYLIST_NAME_LEN - 1; p++) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
                (*p >= '0' && *p <= '9') || *p == '_' || *p == '-') {
                safe_name[j++] = *p;
            } else if (*p == ' ') {
                safe_name[j++] = '_';
            }
        }
        safe_name[j] = '\0';
        
        if (j == 0) {
            snprintf(safe_name, sizeof(safe_name), "playlist_%d", i);
        }
        
        snprintf(filename, sizeof(filename), "%s.json", safe_name);
        
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", playlists_dir, filename);
        
        FILE *f = fopen(filepath, "w");
        if (!f) continue;
        
        char escaped_name[MAX_PLAYLIST_NAME_LEN * 2];
        escape_json_string(pl->name, escaped_name, sizeof(escaped_name));
        
        fprintf(f, "{\n");
        fprintf(f, "  \"name\": \"%s\",\n", escaped_name);
        fprintf(f, "  \"created_time\": %ld,\n", (long)pl->created_time);
        fprintf(f, "  \"modified_time\": %ld,\n", (long)pl->modified_time);
        fprintf(f, "  \"tracks\": [\n");
        
        for (int k = 0; k < pl->track_count; k++) {
            Track *t = &pl->tracks[k];
            char escaped_path[MAX_PATH_LEN * 2];
            char escaped_title[MAX_META_LEN * 2];
            char escaped_artist[MAX_META_LEN * 2];
            char escaped_album[MAX_META_LEN * 2];
            
            escape_json_string(t->path, escaped_path, sizeof(escaped_path));
            escape_json_string(t->title, escaped_title, sizeof(escaped_title));
            escape_json_string(t->artist, escaped_artist, sizeof(escaped_artist));
            escape_json_string(t->album, escaped_album, sizeof(escaped_album));
            
            fprintf(f, "    {\n");
            fprintf(f, "      \"path\": \"%s\",\n", escaped_path);
            fprintf(f, "      \"title\": \"%s\",\n", escaped_title);
            fprintf(f, "      \"artist\": \"%s\",\n", escaped_artist);
            fprintf(f, "      \"album\": \"%s\"\n", escaped_album);
            fprintf(f, "    }%s\n", (k < pl->track_count - 1) ? "," : "");
        }
        
        fprintf(f, "  ],\n");
        fprintf(f, "  \"track_count\": %d\n", pl->track_count);
        fprintf(f, "}\n");
        
        fclose(f);
    }
}

void save_temp_playlist(void) {
    if (!g_playlist.is_loaded || g_playlist.count == 0) {
        return;
    }

    FILE *f = fopen(temp_playlist_file, "w");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"folder_path\": \"%s\",\n", g_playlist.folder_path);
    fprintf(f, "  \"count\": %d,\n", g_playlist.count);
    fprintf(f, "  \"tracks\": [\n");

    for (int i = 0; i < g_playlist.count; i++) {
        Track t;
        get_track_metadata(i, &t);
        char escaped_path[MAX_PATH_LEN * 2];
        char escaped_title[MAX_META_LEN * 2];
        char escaped_artist[MAX_META_LEN * 2];
        char escaped_album[MAX_META_LEN * 2];

        escape_json_string(t.path, escaped_path, sizeof(escaped_path));
        escape_json_string(t.title, escaped_title, sizeof(escaped_title));
        escape_json_string(t.artist, escaped_artist, sizeof(escaped_artist));
        escape_json_string(t.album, escaped_album, sizeof(escaped_album));

        fprintf(f, "    {\n");
        fprintf(f, "      \"path\": \"%s\",\n", escaped_path);
        fprintf(f, "      \"title\": \"%s\",\n", escaped_title);
        fprintf(f, "      \"artist\": \"%s\",\n", escaped_artist);
        fprintf(f, "      \"album\": \"%s\"\n", escaped_album);
        fprintf(f, "    }%s\n", (i < g_playlist.count - 1) ? "," : "");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
}

void cleanup_temp_playlist(void) {
    if (temp_playlist_file[0] != '\0') {
        unlink(temp_playlist_file);
    }
}

int load_temp_playlist(void) {
    FILE *f = fopen(temp_playlist_file, "r");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        return 0;
    }

    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return 0;
    }

    fseek(f, 0, SEEK_SET);
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);

    int count = (int)extract_json_int(json, "count");
    if (count <= 0 || count > MAX_TRACKS) {
        free(json);
        return 0;
    }

    char folder_path[MAX_PATH_LEN];
    extract_json_string(json, "folder_path", folder_path, sizeof(folder_path));

    struct stat st;
    if (stat(folder_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        free(json);
        return 0;
    }

    memset(&g_playlist, 0, sizeof(g_playlist));
    clear_metadata_cache();

    const char *tracks_start = strstr(json, "\"tracks\": [");
    if (!tracks_start) {
        free(json);
        return 0;
    }

    const char *obj_end = strstr(json, "  ]\n}");
    if (!obj_end) obj_end = json + strlen(json);

    int loaded_count = 0;
    const char *current_track = tracks_start;

    while (loaded_count < count) {
        current_track = strstr(current_track, "{");
        if (!current_track || current_track > obj_end) break;

        const char *next_brace = current_track + 1;
        const char *end_track = strstr(next_brace, "}");
        if (!end_track || end_track > obj_end) break;

        size_t obj_len = end_track - current_track + 1;
        char *track_obj = malloc(obj_len + 1);
        if (!track_obj) {
            current_track = end_track + 1;
            continue;
        }

        strncpy(track_obj, current_track, obj_len);
        track_obj[obj_len] = '\0';

        char path_buf[MAX_PATH_LEN];
        extract_json_string(track_obj, "path", path_buf, sizeof(path_buf));

        free(track_obj);

        if (path_buf[0] != '\0') {
            strncpy(g_playlist.tracks[loaded_count], path_buf, MAX_PATH_LEN - 1);
            g_playlist.tracks[loaded_count][MAX_PATH_LEN - 1] = '\0';
            loaded_count++;
        }

        current_track = end_track + 1;
    }

    if (loaded_count > 0) {
        g_playlist.count = loaded_count;
        g_playlist.is_loaded = 1;
        strncpy(g_playlist.folder_path, folder_path, sizeof(g_playlist.folder_path) - 1);
        g_playlist.folder_path[sizeof(g_playlist.folder_path) - 1] = '\0';
    }

    free(json);
    return loaded_count > 0 ? loaded_count : 0;
}

void render_menu_hint_bar(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvhline(max_y - 1, 0, ' ', max_x);
    mvprintw(max_y - 1, 2, "%s",
             use_english_ui()
                 ? "F1:Home  F2:Settings  F3:History  F4:Playlists  F5:Favorites  F6:Info  F7:Lang  F8:Quit"
                 : "F1:主页  F2:设置  F3:历史  F4:歌单  F5:收藏  F6:信息  F7:中/EN  F8:退出");
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    refresh();
}

int create_user_playlist(const char *name) {
    if (!name || strlen(name) == 0) return -1;
    if (g_playlist_manager.count >= MAX_PLAYLISTS_COUNT) return -2;
    
    UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_manager.count];
    memset(pl, 0, sizeof(UserPlaylist));
    
    strncpy(pl->name, name, MAX_PLAYLIST_NAME_LEN - 1);
    pl->created_time = time(NULL);
    pl->modified_time = pl->created_time;
    
    g_playlist_manager.count++;
    save_all_playlists();
    
    return 0;
}

int delete_user_playlist(int index) {
    if (index < 0 || index >= g_playlist_manager.count) return -1;
    
    char filename[MAX_PLAYLIST_NAME_LEN + 8];
    char safe_name[MAX_PLAYLIST_NAME_LEN];
    
    int j = 0;
    for (const char *p = g_playlist_manager.playlists[index].name; *p && j < MAX_PLAYLIST_NAME_LEN - 1; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-') {
            safe_name[j++] = *p;
        } else if (*p == ' ') {
            safe_name[j++] = '_';
        }
    }
    safe_name[j] = '\0';
    
    if (j > 0) {
        snprintf(filename, sizeof(filename), "%s.json", safe_name);
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", playlists_dir, filename);
        unlink(filepath);
    }
    
    for (int i = index; i < g_playlist_manager.count - 1; i++) {
        g_playlist_manager.playlists[i] = g_playlist_manager.playlists[i + 1];
    }
    g_playlist_manager.count--;
    
    save_all_playlists();
    return 0;
}

int rename_user_playlist(int index, const char *new_name) {
    if (index < 0 || index >= g_playlist_manager.count) return -1;
    if (!new_name || strlen(new_name) == 0) return -2;

    UserPlaylist *pl = &g_playlist_manager.playlists[index];

    char old_filename[MAX_PLAYLIST_NAME_LEN + 8];
    char safe_old_name[MAX_PLAYLIST_NAME_LEN];
    int j = 0;
    for (const char *p = pl->name; *p && j < MAX_PLAYLIST_NAME_LEN - 1; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-') {
            safe_old_name[j++] = *p;
        } else if (*p == ' ') {
            safe_old_name[j++] = '_';
        }
    }
    safe_old_name[j] = '\0';

    if (j > 0) {
        snprintf(old_filename, sizeof(old_filename), "%s.json", safe_old_name);
        char old_filepath[MAX_PATH_LEN];
        snprintf(old_filepath, sizeof(old_filepath), "%s/%s", playlists_dir, old_filename);
        unlink(old_filepath);
    }

    strncpy(pl->name, new_name, MAX_PLAYLIST_NAME_LEN - 1);
    pl->name[MAX_PLAYLIST_NAME_LEN - 1] = '\0';
    pl->modified_time = time(NULL);

    save_all_playlists();
    return 0;
}

int add_track_to_playlist(int playlist_idx, Track *track) {
    if (playlist_idx < 0 || playlist_idx >= g_playlist_manager.count) return -1;
    if (!track) return -2;
    
    UserPlaylist *pl = &g_playlist_manager.playlists[playlist_idx];
    if (pl->track_count >= MAX_TRACKS) return -3;
    
    for (int i = 0; i < pl->track_count; i++) {
        if (strcmp(pl->tracks[i].path, track->path) == 0) {
            return 0;
        }
    }
    
    pl->tracks[pl->track_count] = *track;
    pl->track_count++;
    pl->modified_time = time(NULL);
    
    save_all_playlists();
    return 0;
}

int remove_track_from_playlist(int playlist_idx, int track_idx) {
    if (playlist_idx < 0 || playlist_idx >= g_playlist_manager.count) return -1;
    
    UserPlaylist *pl = &g_playlist_manager.playlists[playlist_idx];
    if (track_idx < 0 || track_idx >= pl->track_count) return -2;
    
    for (int i = track_idx; i < pl->track_count - 1; i++) {
        pl->tracks[i] = pl->tracks[i + 1];
    }
    pl->track_count--;
    pl->modified_time = time(NULL);
    
    save_all_playlists();
    return 0;
}

void init_all_persistent_data(void) {
    ensure_config_dir_exists();
    load_config();
    apply_color_theme();
    load_history();
    load_favorites();
    load_dir_history();
    load_all_playlists();
}

void show_status_message(const char *msg) {
    if (use_ascii_fallback_ui()) {
        sanitize_ascii_menu_text(g_status_message, sizeof(g_status_message), msg);
    } else {
        strncpy(g_status_message, msg, sizeof(g_status_message) - 1);
        g_status_message[sizeof(g_status_message) - 1] = '\0';
    }
    g_status_message_time = time(NULL);
}

void init_menu_views(void) {
    g_current_view = VIEW_MAIN;
    g_menu_selected_idx = 0;
    g_content_selected_idx = 0;
    g_focus_area = FOCUS_SIDEBAR;
    
    init_all_persistent_data();
}

void render_menu_frame(const char *title) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    clear();
    
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    box(stdscr, 0, 0);
    mvprintw(0, 2, " %s ", resolve_menu_title(title));
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    
    int menu_width = max_x / 4;
    mvvline(1, menu_width, ACS_VLINE, max_y - 3);
    mvaddch(1, menu_width, ACS_TTEE);
    mvaddch(max_y - 3, menu_width, ACS_BTEE);
    
    if (strlen(g_status_message) > 0 && (time(NULL) - g_status_message_time) < 3) {
        attron(COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
        mvprintw(max_y - 2, 2, "%s", g_status_message);
        attroff(COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
    }
    
    refresh();
}

void render_menu_sidebar(int selected_idx, const char **items, int item_count) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int start_y = 2;
    items = resolve_sidebar_items(items);
    
    attron(COLOR_PAIR(COLOR_PAIR_SIDEBAR));
    
    for (int y = start_y; y < max_y - 2; y++) {
        mvhline(y, 1, ' ', menu_width - 1);
    }
    
    for (int i = 0; i < item_count && (start_y + i) < max_y - 2; i++) {
        if (i == selected_idx && g_focus_area == FOCUS_SIDEBAR) {
            attron(A_REVERSE);
            mvprintw(start_y + i, 2, "%s", items[i]);
            attroff(A_REVERSE);
        } else {
            mvprintw(start_y + i, 2, "%s", items[i]);
        }
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_SIDEBAR));
    
    refresh();
}

void add_history_entry(Track *track) {
    if (!track || g_play_history.count >= MAX_HISTORY_COUNT) return;
    
    if (g_play_history.count > 0) {
        memmove(&g_play_history.entries[1], &g_play_history.entries[0], 
                sizeof(HistoryEntry) * (g_play_history.count));
    }
    
    strncpy(g_play_history.entries[0].path, track->path, MAX_PATH_LEN - 1);
    strncpy(g_play_history.entries[0].title, track->title, MAX_META_LEN - 1);
    strncpy(g_play_history.entries[0].artist, track->artist, MAX_META_LEN - 1);
    g_play_history.entries[0].play_time = time(NULL);
    
    g_play_history.count++;
    if (g_play_history.count > MAX_HISTORY_COUNT) {
        g_play_history.count = MAX_HISTORY_COUNT;
    }
    
    save_history();
}

int add_to_favorites(Track *track) {
    if (!track || g_favorites.count >= MAX_FAVORITES_COUNT) {
        return -1;
    }
    
    for (int i = 0; i < g_favorites.count; i++) {
        if (strcmp(g_favorites.tracks[i].path, track->path) == 0) {
            return 0;
        }
    }
    
    strncpy(g_favorites.tracks[g_favorites.count].path, track->path, MAX_PATH_LEN - 1);
    strncpy(g_favorites.tracks[g_favorites.count].title, track->title, MAX_META_LEN - 1);
    strncpy(g_favorites.tracks[g_favorites.count].artist, track->artist, MAX_META_LEN - 1);
    strncpy(g_favorites.tracks[g_favorites.count].album, track->album, MAX_META_LEN - 1);
    
    g_favorites.count++;
    save_favorites();
    
    return 0;
}

int remove_from_favorites(int index) {
    if (index < 0 || index >= g_favorites.count) {
        return -1;
    }
    
    if (index < g_favorites.count - 1) {
        memmove(&g_favorites.tracks[index], &g_favorites.tracks[index + 1],
                sizeof(Track) * (g_favorites.count - index - 1));
    }
    
    g_favorites.count--;
    save_favorites();
    
    return 0;
}

static int g_settings_current_option = 0;
static int g_settings_color_editing = 0;
static int g_settings_color_which = 0;

static int clamp_latency_ms(int latency_ms) {
    if (latency_ms < 20) {
        return 20;
    }
    if (latency_ms > 250) {
        return 250;
    }
    return latency_ms;
}

static const char *settings_options[] = {
    "播放列表前景色",
    "播放列表背景色",
    "控制区前景色",
    "控制区背景色",
    "歌词前景色",
    "歌词背景色",
    "侧边栏前景色",
    "侧边栏背景色",
    "高亮前景色",
    "高亮背景色",
    "边框前景色",
    "边框背景色",
    "默认启动路径",
    "启动后自动播放",
    "记住上次路径",
    "启动时清空历史",
    "界面语言",
    "默认音量",
    "输出时延"
};
static const char *settings_options_ascii[] = {
    "Playlist Foreground",
    "Playlist Background",
    "Controls Foreground",
    "Controls Background",
    "Lyrics Foreground",
    "Lyrics Background",
    "Sidebar Foreground",
    "Sidebar Background",
    "Highlight Foreground",
    "Highlight Background",
    "Border Foreground",
    "Border Background",
    "Default Startup Path",
    "Auto Play On Start",
    "Remember Last Path",
    "Clear History On Start",
    "Language",
    "Default Volume",
    "Output Latency"
};
#define SETTINGS_OPTION_COUNT 19

enum {
    SETTINGS_IDX_DEFAULT_PATH = 12,
    SETTINGS_IDX_AUTO_PLAY = 13,
    SETTINGS_IDX_REMEMBER_PATH = 14,
    SETTINGS_IDX_CLEAR_HISTORY = 15,
    SETTINGS_IDX_LANGUAGE = 16,
    SETTINGS_IDX_VOLUME = 17,
    SETTINGS_IDX_LATENCY = 18
};

void render_settings_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 2;
    
    attron(COLOR_PAIR(COLOR_PAIR_PLAYLIST));

    const char **current_settings_options = use_english_ui() ? settings_options_ascii : settings_options;
    const char *separator = use_english_ui() ? ": " : "：";
    const char *unset_label = use_english_ui() ? "(Not set)" : "(未设置)";

    mvprintw(start_y, content_start_x, "%s",
             menu_text("设置：↑/↓ 选择，←/→ 修改，ENTER 编辑",
                       "Settings: Up/Down select, Left/Right adjust, Enter edit"));
    start_y += 2;
    
    int *color_values[] = {
        &g_app_config.theme.playlist_fg,
        &g_app_config.theme.playlist_bg,
        &g_app_config.theme.controls_fg,
        &g_app_config.theme.controls_bg,
        &g_app_config.theme.lyrics_fg,
        &g_app_config.theme.lyrics_bg,
        &g_app_config.theme.sidebar_fg,
        &g_app_config.theme.sidebar_bg,
        &g_app_config.theme.highlight_fg,
        &g_app_config.theme.highlight_bg,
        &g_app_config.theme.border_fg,
        &g_app_config.theme.border_bg
    };
    
    for (int i = 0; i < SETTINGS_OPTION_COUNT && start_y + i < max_y - 2; i++) {
        char line[256];
        
        if (i < 12) {
            int color_val = *color_values[i];
            const char *color_name = menu_color_name(color_val);
            snprintf(line, sizeof(line), "%s%s%s (%d)", current_settings_options[i], separator, color_name, color_val);
        } else if (i == SETTINGS_IDX_DEFAULT_PATH) {
            snprintf(line, sizeof(line), "%s%s%s", current_settings_options[i], separator,
                    g_app_config.default_startup_path[0] ? g_app_config.default_startup_path : unset_label);
        } else if (i == SETTINGS_IDX_AUTO_PLAY) {
            snprintf(line, sizeof(line), "%s%s%s", current_settings_options[i], separator,
                    menu_bool_text(g_app_config.auto_play_on_start));
        } else if (i == SETTINGS_IDX_REMEMBER_PATH) {
            snprintf(line, sizeof(line), "%s%s%s", current_settings_options[i], separator,
                    menu_bool_text(g_app_config.remember_last_path));
        } else if (i == SETTINGS_IDX_CLEAR_HISTORY) {
            snprintf(line, sizeof(line), "%s%s%s", current_settings_options[i], separator,
                    menu_bool_text(g_app_config.clear_history_on_startup));
        } else if (i == SETTINGS_IDX_LANGUAGE) {
            snprintf(line, sizeof(line), "%s%s%s", current_settings_options[i], separator,
                    menu_language_name(g_app_config.ui_language));
        } else if (i == SETTINGS_IDX_VOLUME) {
            snprintf(line, sizeof(line), "%s%s%d%%", current_settings_options[i], separator, g_app_config.volume_percent);
        } else {
            snprintf(line, sizeof(line), "%s%s%d ms", current_settings_options[i], separator, g_app_config.audio_latency_ms);
        }
        
        move(start_y + i, content_start_x);
        if (i == g_settings_current_option && g_focus_area == FOCUS_CONTENT) {
            attron(A_REVERSE);
            printw("%s", line);
            clrtoeol();
            attroff(A_REVERSE);
        } else {
            printw("%s", line);
            clrtoeol();
        }
    }
    
    start_y += SETTINGS_OPTION_COUNT + 2;

    mvprintw(start_y, content_start_x, "%s",
             menu_text("按 ENTER 编辑默认启动路径", "Press Enter to edit the startup path"));
    mvprintw(start_y + 1, content_start_x, "%s",
             menu_text("音量即时生效，时延在下次播放生效",
                       "Volume is instant; latency applies on next playback"));
    mvprintw(start_y + 2, content_start_x, "%s",
             menu_text("按 'S' 保存设置", "Press 'S' to save settings"));
    
    attroff(COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    
    refresh();
}

static int g_history_content_offset = 0;

void render_history_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 2;
    
    attron(COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    
    mvprintw(start_y, content_start_x, use_english_ui() ? "Folder History (%d)" : "目录历史（%d 个目录）", g_dir_history.count);
    mvprintw(start_y + 1, content_start_x, "----------------------------------------");
    start_y += 3;
    
    if (g_dir_history.count == 0) {
        mvprintw(start_y, content_start_x, "%s", menu_text("还没有目录历史。", "No folder history yet."));
        mvprintw(start_y + 1, content_start_x, "%s", menu_text("打开音乐目录后会自动记录。", "Opened music folders will appear here."));
    } else {
        int visible_lines = max_y - start_y - 2;
        
        if (g_content_selected_idx >= g_dir_history.count) {
            g_content_selected_idx = g_dir_history.count - 1;
        }
        if (g_content_selected_idx < 0) g_content_selected_idx = 0;
        
        if (g_content_selected_idx < g_history_content_offset) {
            g_history_content_offset = g_content_selected_idx;
        } else if (g_content_selected_idx >= g_history_content_offset + visible_lines) {
            g_history_content_offset = g_content_selected_idx - visible_lines + 1;
        }
        
        for (int i = 0; i < visible_lines && (g_history_content_offset + i) < g_dir_history.count; i++) {
            int idx = g_history_content_offset + i;
            DirHistoryEntry *entry = &g_dir_history.entries[idx];
            
            char time_str[64];
            struct tm *tm_info = localtime(&entry->open_time);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
            
            char display_path[MAX_PATH_LEN];
            int path_width = max_x - menu_width - 25;
            if (path_width > MAX_PATH_LEN - 1) path_width = MAX_PATH_LEN - 1;
            utf8_str_truncate(display_path, entry->path, path_width);
            
            if (idx == g_content_selected_idx && g_focus_area == FOCUS_CONTENT) {
                attron(A_REVERSE);
                mvprintw(start_y + i, content_start_x, " %s [%s]", display_path, time_str);
                attroff(A_REVERSE);
            } else {
                mvprintw(start_y + i, content_start_x, " %s [%s]", display_path, time_str);
            }
        }
        
        int bottom_y = max_y - 3;
        mvprintw(bottom_y, content_start_x, "%s",
                 menu_text("按 ENTER 替换播放列表，按 'A' 追加到队列",
                           "Press Enter to replace, 'A' to append"));
        mvprintw(bottom_y + 1, content_start_x, "%s",
                 menu_text("按 'D' 删除当前项，按 'C' 清空全部",
                           "Press 'D' to delete, 'C' to clear all"));
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    
    refresh();
}

static int g_playlist_content_offset = 0;
static int g_playlist_view_mode = 0;
static int g_playlist_selected_playlist = -1;
static int g_playlist_track_offset = 0;

void render_playlist_manager_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 2;
    
    attron(COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    if (g_playlist_view_mode == 0) {
        mvprintw(start_y, content_start_x, use_english_ui() ? "Playlists (%d)" : "用户歌单（%d 个）", g_playlist_manager.count);
        mvprintw(start_y + 1, content_start_x, "----------------------------------------");
        start_y += 3;
        
        if (g_playlist_manager.count == 0) {
            mvprintw(start_y, content_start_x, "%s", menu_text("还没有创建歌单。", "No playlists created yet."));
            mvprintw(start_y + 1, content_start_x, "%s", menu_text("请从左侧选择“新建歌单”。", "Choose 'New Playlist' on the left."));
        } else {
            int visible_lines = max_y - start_y - 2;
            
            for (int i = 0; i < visible_lines && i < g_playlist_manager.count; i++) {
                UserPlaylist *pl = &g_playlist_manager.playlists[i];
                char display_name[MAX_PLAYLIST_NAME_LEN + 8];
                utf8_str_pad(display_name, sizeof(display_name), pl->name, 30);
                
                if (i == g_content_selected_idx && g_focus_area == FOCUS_CONTENT) {
                    attron(A_REVERSE);
                    mvprintw(start_y + i, content_start_x,
                             use_english_ui() ? " %s (%d tracks)" : " %s (%d 首)",
                             display_name, pl->track_count);
                    attroff(A_REVERSE);
                } else {
                    mvprintw(start_y + i, content_start_x,
                             use_english_ui() ? " %s (%d tracks)" : " %s (%d 首)",
                             display_name, pl->track_count);
                }
            }
            
            int bottom_y = max_y - 3;
            mvprintw(bottom_y, content_start_x, "%s",
                     menu_text("ENTER: 查看歌曲 | D: 删除歌单 | R: 重命名",
                               "Enter: View tracks | D: Delete | R: Rename"));
        }
    } else {
        if (g_playlist_selected_playlist >= 0 && g_playlist_selected_playlist < g_playlist_manager.count) {
            UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_selected_playlist];
            
            mvprintw(start_y, content_start_x,
                     use_english_ui() ? "Playlist: %s (%d tracks)" : "歌单：%s（%d 首）",
                     pl->name, pl->track_count);
            mvprintw(start_y + 1, content_start_x, "----------------------------------------");
            start_y += 3;
            
            if (pl->track_count == 0) {
                mvprintw(start_y, content_start_x, "%s", menu_text("这个歌单还是空的。", "This playlist is empty."));
                mvprintw(start_y + 1, content_start_x, "%s", menu_text("请从主界面把歌曲加入歌单。", "Add tracks from the main view."));
            } else {
                int visible_lines = max_y - start_y - 2;
                
                if (g_content_selected_idx >= pl->track_count) {
                    g_content_selected_idx = pl->track_count - 1;
                }
                if (g_content_selected_idx < 0) g_content_selected_idx = 0;
                
                if (g_content_selected_idx < g_playlist_track_offset) {
                    g_playlist_track_offset = g_content_selected_idx;
                } else if (g_content_selected_idx >= g_playlist_track_offset + visible_lines) {
                    g_playlist_track_offset = g_content_selected_idx - visible_lines + 1;
                }
                
                for (int i = 0; i < visible_lines && (g_playlist_track_offset + i) < pl->track_count; i++) {
                    int idx = g_playlist_track_offset + i;
                    Track *t = &pl->tracks[idx];
                    
                    char truncated_title[MAX_META_LEN];
                    char display_title[MAX_META_LEN + 32];
                    int title_width = max_x - menu_width - 30;
                    utf8_str_truncate(truncated_title, t->title, title_width > 0 ? title_width : 30);
                    utf8_str_pad(display_title, sizeof(display_title), truncated_title, title_width > 0 ? title_width : 30);
                    
                    if (idx == g_content_selected_idx && g_focus_area == FOCUS_CONTENT) {
                        attron(A_REVERSE);
                        mvprintw(start_y + i, content_start_x, " %s - %s", display_title, t->artist);
                        attroff(A_REVERSE);
                    } else {
                        mvprintw(start_y + i, content_start_x, " %s - %s", display_title, t->artist);
                    }
                }
                
                int bottom_y = max_y - 3;
                mvprintw(bottom_y, content_start_x, "%s",
                         menu_text("ENTER: 播放 | D: 从歌单移除 | ESC: 返回",
                                   "Enter: Play | D: Remove | Esc: Back"));
            }
        }
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    refresh();
}

static int g_favorites_content_offset = 0;

void render_favorites_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 2;
    
    attron(COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    mvprintw(start_y, content_start_x, use_english_ui() ? "Favorites (%d)" : "收藏夹（%d 首）", g_favorites.count);
    mvprintw(start_y + 1, content_start_x, "----------------------------------------");
    start_y += 3;
    
    if (g_favorites.count == 0) {
        mvprintw(start_y, content_start_x, "%s", menu_text("还没有收藏。", "No favorites yet."));
        mvprintw(start_y + 1, content_start_x, "%s", menu_text("在主界面按 'F' 可加入收藏。", "Press 'F' in the main view to favorite."));
    } else {
        int visible_lines = max_y - start_y - 2;
        
        if (g_content_selected_idx >= g_favorites.count) {
            g_content_selected_idx = g_favorites.count - 1;
        }
        if (g_content_selected_idx < 0) g_content_selected_idx = 0;
        
        if (g_content_selected_idx < g_favorites_content_offset) {
            g_favorites_content_offset = g_content_selected_idx;
        } else if (g_content_selected_idx >= g_favorites_content_offset + visible_lines) {
            g_favorites_content_offset = g_content_selected_idx - visible_lines + 1;
        }
        
        for (int i = 0; i < visible_lines && (g_favorites_content_offset + i) < g_favorites.count; i++) {
            int idx = g_favorites_content_offset + i;
            Track *t = &g_favorites.tracks[idx];
            
            char truncated_title[MAX_META_LEN];
            char truncated_artist[MAX_META_LEN];
            char display_title[MAX_META_LEN + 32];
            char display_artist[MAX_META_LEN + 32];
            int title_width = (max_x - menu_width - 10) * 3 / 5;
            int artist_width = (max_x - menu_width - 10) * 2 / 5;
            
            utf8_str_truncate(truncated_title, t->title, title_width > 0 ? title_width : 30);
            utf8_str_truncate(truncated_artist, t->artist, artist_width > 0 ? artist_width : 20);
            utf8_str_pad(display_title, sizeof(display_title), truncated_title, title_width > 0 ? title_width : 30);
            utf8_str_pad(display_artist, sizeof(display_artist), truncated_artist, artist_width > 0 ? artist_width : 20);
            
            if (idx == g_content_selected_idx && g_focus_area == FOCUS_CONTENT) {
                attron(A_REVERSE);
                mvprintw(start_y + i, content_start_x, " %s - %s", display_title, display_artist);
                attroff(A_REVERSE);
            } else {
                mvprintw(start_y + i, content_start_x, " %s - %s", display_title, display_artist);
            }
        }
        
        int bottom_y = max_y - 3;
        mvprintw(bottom_y, content_start_x, "%s",
                 menu_text("ENTER: 播放 | D: 移出收藏",
                           "Enter: Play | D: Remove"));
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    refresh();
}

void render_info_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 2;
    
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    
    mvprintw(start_y, content_start_x, "%s %s", menu_text("关于", "About"), APP_NAME);
    mvprintw(start_y + 1, content_start_x, "========================================");
    start_y += 3;
    
    mvprintw(start_y, content_start_x, use_english_ui() ? "Name: %s" : "名称：%s", APP_NAME);
    mvprintw(start_y + 1, content_start_x, use_english_ui() ? "Version: %s" : "版本：%s", APP_VERSION);
    mvprintw(start_y + 2, content_start_x, use_english_ui() ? "Authors: %s" : "巨献：%s", APP_AUTHORS);
    mvprintw(start_y + 3, content_start_x, use_english_ui() ? "Email: %s" : "邮箱：%s", APP_EMAIL);
    start_y += 5;
    
    mvprintw(start_y, content_start_x, "%s", menu_text("简介：", "Summary:"));
    mvprintw(start_y + 1, content_start_x, "%s", menu_text("  基于 ncurses 的终端音乐播放器。", "  A terminal music player built on ncurses."));
    mvprintw(start_y + 2, content_start_x, "%s", menu_text("  通过 FFmpeg 支持多种音频格式。", "  Supports multiple audio formats via FFmpeg."));
    mvprintw(start_y + 3, content_start_x, "%s", menu_text("  提供歌单、收藏和歌词显示。", "  Includes playlists, favorites, and lyrics."));
    start_y += 5;
    
    mvprintw(start_y, content_start_x, "%s", menu_text("仓库地址：", "Repository:"));
    mvprintw(start_y + 1, content_start_x, "  %s", APP_REPO);
    start_y += 3;
    
    mvprintw(start_y, content_start_x, "%s", menu_text("许可证：GPL v3", "License: GPL v3"));
    start_y += 2;
    
    mvprintw(start_y, content_start_x, "%s", menu_text("这里的信息为只读。", "This page is read-only."));
    
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    
    refresh();
}

void switch_to_view(ViewMode view) {
    g_current_view = view;
    g_menu_selected_idx = 0;
    g_content_selected_idx = 0;
    g_focus_area = FOCUS_SIDEBAR;
    g_settings_current_option = 0;
    g_history_content_offset = 0;
    g_playlist_view_mode = 0;
    g_playlist_selected_playlist = -1;
    g_playlist_track_offset = 0;
    g_favorites_content_offset = 0;
    
    switch (view) {
        case VIEW_SETTINGS:
            render_menu_frame("设置 [F2]");
            render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
            render_settings_content();
            render_menu_hint_bar();
            break;
        case VIEW_HISTORY:
            render_menu_frame("历史 [F3]");
            render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
            render_history_content();
            render_menu_hint_bar();
            break;
        case VIEW_PLAYLIST:
            render_menu_frame("歌单 [F4]");
            render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
            render_playlist_manager_content();
            render_menu_hint_bar();
            break;
        case VIEW_FAVORITES:
            render_menu_frame("收藏 [F5]");
            render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
            render_favorites_content();
            render_menu_hint_bar();
            break;
        case VIEW_INFO:
            render_menu_frame("信息 [F6]");
            render_menu_sidebar(g_menu_selected_idx, info_sidebar_items, INFO_ITEM_COUNT);
            render_info_content();
            render_menu_hint_bar();
            break;
        default:
            break;
    }
}

void exit_current_view(void) {
    g_current_view = VIEW_MAIN;
    g_menu_selected_idx = 0;
    g_content_selected_idx = 0;
    g_focus_area = FOCUS_SIDEBAR;
    
    clear();
    create_layout();
    render_playlist_content();
    render_controls();
    render_lyrics();
}

static void rerender_active_view(void) {
    if (g_current_view == VIEW_MAIN) {
        render_playlist_content();
        render_controls();
        render_lyrics();
        render_menu_hint_bar();
        return;
    }

    switch (g_current_view) {
        case VIEW_SETTINGS:
            render_menu_frame("设置 [F2]");
            render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
            render_settings_content();
            render_menu_hint_bar();
            break;
        case VIEW_HISTORY:
            render_menu_frame("历史 [F3]");
            render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
            render_history_content();
            render_menu_hint_bar();
            break;
        case VIEW_PLAYLIST:
            render_menu_frame("歌单 [F4]");
            render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
            render_playlist_manager_content();
            render_menu_hint_bar();
            break;
        case VIEW_FAVORITES:
            render_menu_frame("收藏 [F5]");
            render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
            render_favorites_content();
            render_menu_hint_bar();
            break;
        case VIEW_INFO:
            render_menu_frame("信息 [F6]");
            render_menu_sidebar(g_menu_selected_idx, info_sidebar_items, INFO_ITEM_COUNT);
            render_info_content();
            render_menu_hint_bar();
            break;
        default:
            break;
    }
}

void toggle_ui_language(void) {
    g_app_config.ui_language = (g_app_config.ui_language == UI_LANG_EN) ? UI_LANG_ZH : UI_LANG_EN;
    save_config();
    rerender_active_view();
}

void handle_function_keys(int fkey) {
    switch(fkey) {
        case KEY_F(1):
            exit_current_view();
            break;
        case KEY_F(2):
            switch_to_view(VIEW_SETTINGS);
            break;
        case KEY_F(3):
            switch_to_view(VIEW_HISTORY);
            break;
        case KEY_F(4):
            switch_to_view(VIEW_PLAYLIST);
            break;
        case KEY_F(5):
            switch_to_view(VIEW_FAVORITES);
            break;
        case KEY_F(6):
            switch_to_view(VIEW_INFO);
            break;
        case KEY_F(7):
            toggle_ui_language();
            break;
        case KEY_F(8):
            cleanup();
            printf("%s\n", menu_text("ter-music 已正常退出。", "ter-music exited cleanly."));
            exit(0);
            break;
        default:
            break;
    }
}

static void handle_settings_input(int ch) {
    int *color_values[] = {
        &g_app_config.theme.playlist_fg,
        &g_app_config.theme.playlist_bg,
        &g_app_config.theme.controls_fg,
        &g_app_config.theme.controls_bg,
        &g_app_config.theme.lyrics_fg,
        &g_app_config.theme.lyrics_bg,
        &g_app_config.theme.sidebar_fg,
        &g_app_config.theme.sidebar_bg,
        &g_app_config.theme.highlight_fg,
        &g_app_config.theme.highlight_bg,
        &g_app_config.theme.border_fg,
        &g_app_config.theme.border_bg
    };
    
    switch (ch) {
        case KEY_UP:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx--;
                if (g_menu_selected_idx < 0) g_menu_selected_idx = SETTINGS_ITEM_COUNT - 1;
                render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
            } else {
                g_settings_current_option--;
                if (g_settings_current_option < 0) g_settings_current_option = SETTINGS_OPTION_COUNT - 1;
                render_settings_content();
            }
            break;
            
        case KEY_DOWN:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx++;
                if (g_menu_selected_idx >= SETTINGS_ITEM_COUNT) g_menu_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
            } else {
                g_settings_current_option++;
                if (g_settings_current_option >= SETTINGS_OPTION_COUNT) g_settings_current_option = 0;
                render_settings_content();
            }
            break;
            
        case KEY_LEFT:
            if (g_focus_area == FOCUS_CONTENT && g_settings_current_option < 12) {
                int paired_idx;
                if (g_settings_current_option % 2 == 0) {
                    paired_idx = g_settings_current_option + 1;
                } else {
                    paired_idx = g_settings_current_option - 1;
                }
                int paired_color = *color_values[paired_idx];
                do {
                    (*color_values[g_settings_current_option])--;
                    if (*color_values[g_settings_current_option] < 0) {
                        *color_values[g_settings_current_option] = 7;
                    }
                } while (*color_values[g_settings_current_option] == paired_color);
                apply_color_theme();
                save_config();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT &&
                       g_settings_current_option >= SETTINGS_IDX_AUTO_PLAY &&
                       g_settings_current_option <= SETTINGS_IDX_CLEAR_HISTORY) {
                if (g_settings_current_option == SETTINGS_IDX_AUTO_PLAY) {
                    g_app_config.auto_play_on_start = !g_app_config.auto_play_on_start;
                } else if (g_settings_current_option == SETTINGS_IDX_REMEMBER_PATH) {
                    g_app_config.remember_last_path = !g_app_config.remember_last_path;
                } else if (g_settings_current_option == SETTINGS_IDX_CLEAR_HISTORY) {
                    g_app_config.clear_history_on_startup = !g_app_config.clear_history_on_startup;
                }
                save_config();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT && g_settings_current_option == SETTINGS_IDX_LANGUAGE) {
                g_app_config.ui_language = UI_LANG_ZH;
                save_config();
                render_menu_frame("设置 [F2]");
                render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                render_settings_content();
                render_menu_hint_bar();
            } else if (g_focus_area == FOCUS_CONTENT && g_settings_current_option == SETTINGS_IDX_VOLUME) {
                set_volume_percent(g_app_config.volume_percent - 5);
                g_app_config.volume_percent = get_volume_percent();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT && g_settings_current_option == SETTINGS_IDX_LATENCY) {
                g_app_config.audio_latency_ms = clamp_latency_ms(g_app_config.audio_latency_ms - 10);
                save_config();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT) {
                g_focus_area = FOCUS_SIDEBAR;
                render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                render_settings_content();
            }
            break;
            
        case KEY_RIGHT:
        case 9:
            if (g_focus_area == FOCUS_CONTENT && g_settings_current_option < 12) {
                int paired_idx;
                if (g_settings_current_option % 2 == 0) {
                    paired_idx = g_settings_current_option + 1;
                } else {
                    paired_idx = g_settings_current_option - 1;
                }
                int paired_color = *color_values[paired_idx];
                do {
                    (*color_values[g_settings_current_option])++;
                    if (*color_values[g_settings_current_option] > 7) {
                        *color_values[g_settings_current_option] = 0;
                    }
                } while (*color_values[g_settings_current_option] == paired_color);
                apply_color_theme();
                save_config();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT &&
                       g_settings_current_option >= SETTINGS_IDX_AUTO_PLAY &&
                       g_settings_current_option <= SETTINGS_IDX_CLEAR_HISTORY) {
                if (g_settings_current_option == SETTINGS_IDX_AUTO_PLAY) {
                    g_app_config.auto_play_on_start = !g_app_config.auto_play_on_start;
                } else if (g_settings_current_option == SETTINGS_IDX_REMEMBER_PATH) {
                    g_app_config.remember_last_path = !g_app_config.remember_last_path;
                } else if (g_settings_current_option == SETTINGS_IDX_CLEAR_HISTORY) {
                    g_app_config.clear_history_on_startup = !g_app_config.clear_history_on_startup;
                }
                save_config();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT && g_settings_current_option == SETTINGS_IDX_LANGUAGE) {
                g_app_config.ui_language = UI_LANG_EN;
                save_config();
                render_menu_frame("设置 [F2]");
                render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                render_settings_content();
                render_menu_hint_bar();
            } else if (g_focus_area == FOCUS_CONTENT && g_settings_current_option == SETTINGS_IDX_VOLUME) {
                set_volume_percent(g_app_config.volume_percent + 5);
                g_app_config.volume_percent = get_volume_percent();
                render_settings_content();
            } else if (g_focus_area == FOCUS_CONTENT && g_settings_current_option == SETTINGS_IDX_LATENCY) {
                g_app_config.audio_latency_ms = clamp_latency_ms(g_app_config.audio_latency_ms + 10);
                save_config();
                render_settings_content();
            } else if (g_focus_area == FOCUS_SIDEBAR) {
                g_focus_area = FOCUS_CONTENT;
                render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                render_settings_content();
            }
            break;
            
        case 10:
        case ' ':
            if (g_focus_area == FOCUS_SIDEBAR) {
                if (g_menu_selected_idx == SETTINGS_ITEM_COUNT - 1) {
                    exit_current_view();
                } else {
                    g_focus_area = FOCUS_CONTENT;
                    render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                    render_settings_content();
                }
            } else {
                if (g_settings_current_option == SETTINGS_IDX_DEFAULT_PATH) {
                    echo();
                    curs_set(1);
                    
                    int max_y, max_x;
                    getmaxyx(stdscr, max_y, max_x);
                    int menu_width = max_x / 4;
                    
                    const char *path_prompt = menu_text("输入路径：", "Enter path: ");
                    mvprintw(max_y - 2, menu_width + 2, "%s", path_prompt);
                    clrtoeol();
                    refresh();

                    move(max_y - 2, menu_width + 2 + utf8_str_width(path_prompt));
                    refresh();
                    
                    flushinp();
                    
                    char input_path[MAX_PATH_LEN];
                    memset(input_path, 0, sizeof(input_path));
                    int pos = 0;
                    int ch;
                    
                    // BUGFIX 2026.03.26: 改进 UTF-8 中文输入处理，每次按键后刷新
                    // BUGFIX 2026.03.29: 忽略 ERR，防止超时自动插入space字符
                    while ((ch = getch()) != '\n' && ch != KEY_ENTER && pos < MAX_PATH_LEN - 1) {
                        if (ch == ERR) {
                            continue;
                        }
                        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                            // 处理 Backspace 删除
                            if (pos > 0) {
                                int cx = getcurx(stdscr);
                                int cy = getcury(stdscr);
                                unsigned char last_c = (unsigned char)input_path[pos - 1];
                                if (last_c >= 0x80) {
                                    // 多字节 UTF-8 字符，需要回退到序列开头
                                    int bytes_to_remove = 1;
                                    if ((last_c & 0xE0) == 0xC0) bytes_to_remove = 2;
                                    else if ((last_c & 0xF0) == 0xE0) bytes_to_remove = 3;
                                    else if ((last_c & 0xF8) == 0xF0) bytes_to_remove = 4;
                                    else if ((last_c & 0xC0) == 0x80) {
                                        // continuation byte，继续向前找开头
                                        bytes_to_remove = 2;
                                        while (pos - bytes_to_remove >= 0 && 
                                               (unsigned char)input_path[pos - bytes_to_remove] >= 0x80 && 
                                               (unsigned char)input_path[pos - bytes_to_remove] < 0xC0) {
                                            bytes_to_remove++;
                                        }
                                    }
                                    if (bytes_to_remove > pos) bytes_to_remove = pos;
                                    pos -= bytes_to_remove;
                                    
                                    // 根据字符宽度调整光标位置
                                    if ((last_c & 0xF0) == 0xE0 || (last_c & 0xE0) == 0xC0) {
                                        // 中文占两列，光标左移两格
                                        move(cy, cx - 2);
                                    } else {
                                        move(cy, cx - 1);
                                    }
                                } else {
                                    // ASCII 字符
                                    pos--;
                                    move(cy, cx - 1);
                                }
                                clrtoeol();
                                // BUGFIX 2026.03.26: 每次删除后刷新，确保光标正确更新
                                refresh();
                            }
                        } else if (ch >= 0x20 && ch <= 0x7E) {
                            // ASCII 字符
                            input_path[pos++] = (char)ch;
                            addch(ch);
                            // BUGFIX 2026.03.26: 每次按键后刷新，确保显示及时
                            refresh();
                        } else if ((ch & 0xC0) == 0x80 || ch >= 0x80) {
                            // UTF-8 多字节字符
                            if (pos < MAX_PATH_LEN - 1) {
                                input_path[pos++] = (char)ch;
                                // 只有字节序列开头才需要在屏幕显示
                                if ((ch & 0xE0) == 0xC0 || (ch & 0xF0) == 0xE0) {
                                    addch(ch);
                                    refresh();
                                }
                            }
                        } else {
                            // 其他可打印字符
                            input_path[pos++] = (char)ch;
                            addch(ch);
                            refresh();
                        }
                    }
                    input_path[pos] = '\0';
                    
                    noecho();
                    curs_set(0);
                    
                    if (strlen(input_path) > 0) {
                        if (input_path[0] == '~') {
                            const char *home = getenv("HOME");
                            if (home) {
                                snprintf(g_app_config.default_startup_path, MAX_PATH_LEN, "%s%s", home, input_path + 1);
                            }
                        } else {
                            strncpy(g_app_config.default_startup_path, input_path, MAX_PATH_LEN - 1);
                        }
                        save_config();
                        show_status_message(menu_text("默认启动路径已保存", "Default startup path saved"));
                    }
                    
                    render_menu_frame("设置 [F2]");
                    render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                    render_settings_content();
                } else if (g_settings_current_option >= SETTINGS_IDX_AUTO_PLAY &&
                           g_settings_current_option <= SETTINGS_IDX_CLEAR_HISTORY) {
                    if (g_settings_current_option == SETTINGS_IDX_AUTO_PLAY) {
                        g_app_config.auto_play_on_start = !g_app_config.auto_play_on_start;
                    } else if (g_settings_current_option == SETTINGS_IDX_REMEMBER_PATH) {
                        g_app_config.remember_last_path = !g_app_config.remember_last_path;
                    } else if (g_settings_current_option == SETTINGS_IDX_CLEAR_HISTORY) {
                        g_app_config.clear_history_on_startup = !g_app_config.clear_history_on_startup;
                    }
                    save_config();
                    render_settings_content();
                } else if (g_settings_current_option == SETTINGS_IDX_LANGUAGE) {
                    g_app_config.ui_language = (g_app_config.ui_language == UI_LANG_EN) ? UI_LANG_ZH : UI_LANG_EN;
                    save_config();
                    render_menu_frame("设置 [F2]");
                    render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                    render_settings_content();
                    render_menu_hint_bar();
                } else if (g_settings_current_option == SETTINGS_IDX_VOLUME) {
                    set_volume_percent(g_app_config.volume_percent + 5);
                    g_app_config.volume_percent = get_volume_percent();
                    render_settings_content();
                } else if (g_settings_current_option == SETTINGS_IDX_LATENCY) {
                    g_app_config.audio_latency_ms = clamp_latency_ms(g_app_config.audio_latency_ms + 10);
                    save_config();
                    show_status_message(menu_text("输出时延将在下次播放生效",
                                                  "Output latency applies on next playback"));
                    render_menu_frame("设置 [F2]");
                    render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
                    render_settings_content();
                }
            }
            break;
            
        case 's':
        case 'S':
            save_config();
            show_status_message(menu_text("设置已保存", "Settings saved"));
            render_menu_frame("设置 [F2]");
            render_menu_sidebar(g_menu_selected_idx, settings_sidebar_items, SETTINGS_ITEM_COUNT);
            render_settings_content();
            break;
    }
}

static void handle_history_input(int ch) {
    switch (ch) {
        case KEY_UP:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx--;
                if (g_menu_selected_idx < 0) g_menu_selected_idx = HISTORY_ITEM_COUNT - 1;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
            } else {
                g_content_selected_idx--;
                if (g_content_selected_idx < 0) g_content_selected_idx = g_dir_history.count - 1;
                if (g_content_selected_idx < 0) g_content_selected_idx = 0;
                render_history_content();
            }
            break;
            
        case KEY_DOWN:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx++;
                if (g_menu_selected_idx >= HISTORY_ITEM_COUNT) g_menu_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
            } else {
                g_content_selected_idx++;
                if (g_content_selected_idx >= g_dir_history.count) g_content_selected_idx = 0;
                render_history_content();
            }
            break;
            
        case KEY_RIGHT:
        case 9:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_focus_area = FOCUS_CONTENT;
                g_content_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                render_history_content();
            }
            break;
            
        case KEY_LEFT:
            if (g_focus_area == FOCUS_CONTENT) {
                g_focus_area = FOCUS_SIDEBAR;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                render_history_content();
            }
            break;
            
        case 10:
        case ' ':
            if (g_focus_area == FOCUS_SIDEBAR) {
                if (g_menu_selected_idx == 0) {
                    g_focus_area = FOCUS_CONTENT;
                    g_content_selected_idx = 0;
                    render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                    render_history_content();
                } else if (g_menu_selected_idx == 1) {
                    clear_dir_history();
                    g_content_selected_idx = 0;
                    show_status_message(menu_text("历史记录已清空", "History cleared"));
                    render_menu_frame("历史 [F3]");
                    render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                    render_history_content();
                } else if (g_menu_selected_idx == HISTORY_ITEM_COUNT - 1) {
                    exit_current_view();
                }
            } else {
                if (g_dir_history.count > 0 && g_content_selected_idx >= 0 && 
                    g_content_selected_idx < g_dir_history.count) {
                    
                    const char *path = g_dir_history.entries[g_content_selected_idx].path;
                    int count = load_playlist(path);
                    
                    if (count > 0) {
                        g_selected_index = 0;
                        add_dir_history_entry(path);
                        exit_current_view();
                        show_status_message(menu_text("目录加载成功", "Folder loaded"));
                    } else {
                        show_status_message(menu_text("目录中没有音频文件", "No audio files in this folder"));
                        render_history_content();
                    }
                }
            }
            break;

        case 'a':
        case 'A':
            if (g_focus_area == FOCUS_CONTENT &&
                g_dir_history.count > 0 &&
                g_content_selected_idx >= 0 &&
                g_content_selected_idx < g_dir_history.count) {

                const char *path = g_dir_history.entries[g_content_selected_idx].path;
                int count = append_playlist(path);

                if (count > 0) {
                    add_dir_history_entry(path);
                    exit_current_view();
                    if (g_app_config.remember_last_path) {
                        snprintf(g_app_config.last_opened_path, sizeof(g_app_config.last_opened_path), "%s", path);
                        save_config();
                    }

                    char msg[96];
                    snprintf(msg, sizeof(msg), "%s %d %s",
                             menu_text("已追加", "Appended"),
                             count,
                             menu_text("首歌曲到队列", "tracks to queue"));
                    show_status_message(msg);
                } else {
                    show_status_message(menu_text("目录中没有新的音频文件", "No new audio files to append"));
                    render_history_content();
                }
            }
            break;
            
        case 'd':
        case 'D':
            if (g_focus_area == FOCUS_CONTENT && g_dir_history.count > 0 &&
                g_content_selected_idx >= 0 && g_content_selected_idx < g_dir_history.count) {
                
                for (int i = g_content_selected_idx; i < g_dir_history.count - 1; i++) {
                    g_dir_history.entries[i] = g_dir_history.entries[i + 1];
                }
                g_dir_history.count--;
                save_dir_history();
                
                if (g_content_selected_idx >= g_dir_history.count) {
                    g_content_selected_idx = g_dir_history.count - 1;
                }
                
                show_status_message(menu_text("已删除历史项", "History entry removed"));
                render_history_content();
            }
            break;
            
        case 'c':
        case 'C':
            if (g_focus_area == FOCUS_CONTENT) {
                clear_dir_history();
                g_content_selected_idx = 0;
                show_status_message(menu_text("历史记录已清空", "History cleared"));
                render_history_content();
            }
            break;

        case 'r':
        case 'R':
            if (g_focus_area == FOCUS_CONTENT && g_playlist_view_mode == 0) {
                if (g_content_selected_idx >= 0 && g_content_selected_idx < g_playlist_manager.count) {
                    echo();
                    curs_set(1);

                    int max_y, max_x;
                    getmaxyx(stdscr, max_y, max_x);
                    int menu_width = max_x / 4;

                    const char *rename_prompt = menu_text("输入新名称：", "New name: ");
                    mvprintw(max_y - 2, menu_width + 2, "%s", rename_prompt);
                    clrtoeol();
                    refresh();

                    move(max_y - 2, menu_width + 2 + utf8_str_width(rename_prompt));
                    refresh();

                    flushinp();

                    char new_name[MAX_PLAYLIST_NAME_LEN];
                    memset(new_name, 0, sizeof(new_name));
                    int pos = 0;
                    int ch;
                    
                    while ((ch = getch()) != '\n' && ch != KEY_ENTER && pos < MAX_PLAYLIST_NAME_LEN - 1) {
                        if (ch == ERR) {
                            continue;
                        }
                        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                            if (pos > 0) {
                                int cx = getcurx(stdscr);
                                int cy = getcury(stdscr);
                                unsigned char last_c = (unsigned char)new_name[pos - 1];
                                if (last_c >= 0x80) {
                                    int bytes_to_remove = 1;
                                    if ((last_c & 0xE0) == 0xC0) bytes_to_remove = 2;
                                    else if ((last_c & 0xF0) == 0xE0) bytes_to_remove = 3;
                                    else if ((last_c & 0xF8) == 0xF0) bytes_to_remove = 4;
                                    else if ((last_c & 0xC0) == 0x80) {
                                        bytes_to_remove = 2;
                                        while (pos - bytes_to_remove >= 0 && 
                                               (unsigned char)new_name[pos - bytes_to_remove] >= 0x80 && 
                                               (unsigned char)new_name[pos - bytes_to_remove] < 0xC0) {
                                            bytes_to_remove++;
                                        }
                                    }
                                    if (bytes_to_remove > pos) bytes_to_remove = pos;
                                    pos -= bytes_to_remove;
                                    
                                    if ((last_c & 0xF0) == 0xE0 || (last_c & 0xE0) == 0xC0) {
                                        move(cy, cx - 2);
                                    } else {
                                        move(cy, cx - 1);
                                    }
                                } else {
                                    pos--;
                                    move(cy, cx - 1);
                                }
                                clrtoeol();
                                refresh();
                            }
                        } else if (ch >= 0x20 && ch <= 0x7E) {
                            new_name[pos++] = (char)ch;
                            addch(ch);
                            refresh();
                        } else if ((ch & 0xC0) == 0x80 || ch >= 0x80) {
                            if (pos < MAX_PLAYLIST_NAME_LEN - 1) {
                                new_name[pos++] = (char)ch;
                                if ((ch & 0xE0) == 0xC0 || (ch & 0xF0) == 0xE0) {
                                    addch(ch);
                                    refresh();
                                }
                            }
                        } else {
                            new_name[pos++] = (char)ch;
                            addch(ch);
                            refresh();
                        }
                    }
                    new_name[pos] = '\0';

                    noecho();
                    curs_set(0);

                    if (strlen(new_name) > 0) {
                        int result = rename_user_playlist(g_content_selected_idx, new_name);
                        if (result == 0) {
                            show_status_message(menu_text("歌单已重命名", "Playlist renamed"));
                        } else {
                            show_status_message(menu_text("歌单重命名失败", "Failed to rename playlist"));
                        }
                    }

                    render_menu_frame("歌单 [F4]");
                    render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
                    render_playlist_manager_content();
                }
            }
            break;
    }
}

static void handle_playlist_input(int ch) {
    switch (ch) {
        case KEY_UP:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx--;
                if (g_menu_selected_idx < 0) g_menu_selected_idx = PLAYLIST_ITEM_COUNT - 1;
                render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
            } else {
                if (g_playlist_view_mode == 0) {
                    g_content_selected_idx--;
                    if (g_content_selected_idx < 0) g_content_selected_idx = g_playlist_manager.count - 1;
                    if (g_content_selected_idx < 0) g_content_selected_idx = 0;
                } else {
                    g_content_selected_idx--;
                    if (g_content_selected_idx < 0) g_content_selected_idx = 0;
                    if (g_playlist_selected_playlist >= 0) {
                        UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_selected_playlist];
                        if (g_content_selected_idx >= pl->track_count) {
                            g_content_selected_idx = pl->track_count - 1;
                        }
                    }
                }
                render_playlist_manager_content();
            }
            break;
            
        case KEY_DOWN:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx++;
                if (g_menu_selected_idx >= PLAYLIST_ITEM_COUNT) g_menu_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
            } else {
                if (g_playlist_view_mode == 0) {
                    g_content_selected_idx++;
                    if (g_content_selected_idx >= g_playlist_manager.count) g_content_selected_idx = 0;
                } else {
                    g_content_selected_idx++;
                    if (g_playlist_selected_playlist >= 0) {
                        UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_selected_playlist];
                        if (g_content_selected_idx >= pl->track_count) {
                            g_content_selected_idx = pl->track_count - 1;
                        }
                    }
                }
                render_playlist_manager_content();
            }
            break;
            
        case KEY_RIGHT:
        case 9:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_focus_area = FOCUS_CONTENT;
                g_content_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
                render_playlist_manager_content();
            }
            break;
            
        case KEY_LEFT:
            if (g_focus_area == FOCUS_CONTENT) {
                if (g_playlist_view_mode == 1) {
                    g_playlist_view_mode = 0;
                    g_content_selected_idx = 0;
                    render_playlist_manager_content();
                } else {
                    g_focus_area = FOCUS_SIDEBAR;
                    render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
                    render_playlist_manager_content();
                }
            }
            break;
            
        case 10:
        case ' ':
            if (g_focus_area == FOCUS_SIDEBAR) {
                if (g_menu_selected_idx == 0) {
                    g_focus_area = FOCUS_CONTENT;
                    g_content_selected_idx = 0;
                    render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
                    render_playlist_manager_content();
                } else if (g_menu_selected_idx == 1) {
                    echo();
                    curs_set(1);
                    
                    int max_y, max_x;
                    getmaxyx(stdscr, max_y, max_x);
                    int menu_width = max_x / 4;
                    
                    const char *create_prompt = menu_text("输入歌单名称：", "Playlist name: ");
                    mvprintw(max_y - 2, menu_width + 2, "%s", create_prompt);
                    clrtoeol();
                    refresh();

                    move(max_y - 2, menu_width + 2 + utf8_str_width(create_prompt));
                    refresh();
                    
                    flushinp();
                    
                    char name[MAX_PLAYLIST_NAME_LEN];
                    memset(name, 0, sizeof(name));
                    int pos = 0;
                    int ch;
                    
                    while ((ch = getch()) != '\n' && ch != KEY_ENTER && pos < MAX_PLAYLIST_NAME_LEN - 1) {
                        if (ch == ERR) {
                            continue;
                        }
                        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                            if (pos > 0) {
                                int cx = getcurx(stdscr);
                                int cy = getcury(stdscr);
                                unsigned char last_c = (unsigned char)name[pos - 1];
                                if (last_c >= 0x80) {
                                    int bytes_to_remove = 1;
                                    if ((last_c & 0xE0) == 0xC0) bytes_to_remove = 2;
                                    else if ((last_c & 0xF0) == 0xE0) bytes_to_remove = 3;
                                    else if ((last_c & 0xF8) == 0xF0) bytes_to_remove = 4;
                                    else if ((last_c & 0xC0) == 0x80) {
                                        bytes_to_remove = 2;
                                        while (pos - bytes_to_remove >= 0 && 
                                               (unsigned char)name[pos - bytes_to_remove] >= 0x80 && 
                                               (unsigned char)name[pos - bytes_to_remove] < 0xC0) {
                                            bytes_to_remove++;
                                        }
                                    }
                                    if (bytes_to_remove > pos) bytes_to_remove = pos;
                                    pos -= bytes_to_remove;
                                    
                                    if ((last_c & 0xF0) == 0xE0 || (last_c & 0xE0) == 0xC0) {
                                        move(cy, cx - 2);
                                    } else {
                                        move(cy, cx - 1);
                                    }
                                } else {
                                    pos--;
                                    move(cy, cx - 1);
                                }
                                clrtoeol();
                                refresh();
                            }
                        } else if (ch >= 0x20 && ch <= 0x7E) {
                            name[pos++] = (char)ch;
                            addch(ch);
                            refresh();
                        } else if ((ch & 0xC0) == 0x80 || ch >= 0x80) {
                            if (pos < MAX_PLAYLIST_NAME_LEN - 1) {
                                name[pos++] = (char)ch;
                                if ((ch & 0xE0) == 0xC0 || (ch & 0xF0) == 0xE0) {
                                    addch(ch);
                                    refresh();
                                }
                            }
                        } else {
                            name[pos++] = (char)ch;
                            addch(ch);
                            refresh();
                        }
                    }
                    name[pos] = '\0';
                    
                    noecho();
                    curs_set(0);
                    
                    if (strlen(name) > 0) {
                        int result = create_user_playlist(name);
                        if (result == 0) {
                            show_status_message(menu_text("歌单已创建", "Playlist created"));
                        } else if (result == -2) {
                            show_status_message(menu_text("歌单数量已达上限", "Playlist limit reached"));
                        } else {
                            show_status_message(menu_text("创建歌单失败", "Failed to create playlist"));
                        }
                    }
                    
                    render_menu_frame("歌单 [F4]");
                    render_menu_sidebar(g_menu_selected_idx, playlist_sidebar_items, PLAYLIST_ITEM_COUNT);
                    render_playlist_manager_content();
                } else if (g_menu_selected_idx == PLAYLIST_ITEM_COUNT - 1) {
                    exit_current_view();
                }
            } else {
                if (g_playlist_view_mode == 0) {
                    if (g_content_selected_idx >= 0 && g_content_selected_idx < g_playlist_manager.count) {
                        g_playlist_selected_playlist = g_content_selected_idx;
                        g_playlist_view_mode = 1;
                        g_content_selected_idx = 0;
                        g_playlist_track_offset = 0;
                        render_playlist_manager_content();
                    }
                } else {
                    if (g_playlist_selected_playlist >= 0 && g_playlist_selected_playlist < g_playlist_manager.count) {
                        UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_selected_playlist];
                        if (g_content_selected_idx >= 0 && g_content_selected_idx < pl->track_count) {
                            Track *t = &pl->tracks[g_content_selected_idx];
                            
                            int found = -1;
                            for (int i = 0; i < g_playlist.count; i++) {
                                if (strcmp(g_playlist.tracks[i], t->path) == 0) {
                                    found = i;
                                    break;
                                }
                            }
                            
                            if (found >= 0) {
                                play_audio(found);
                                exit_current_view();
                            } else {
                                show_status_message(menu_text("当前播放列表中没有这首歌", "Track not found in current playlist"));
                            }
                        }
                    }
                }
            }
            break;
            
        case 'd':
        case 'D':
            if (g_focus_area == FOCUS_CONTENT) {
                if (g_playlist_view_mode == 0) {
                    if (g_content_selected_idx >= 0 && g_content_selected_idx < g_playlist_manager.count) {
                        delete_user_playlist(g_content_selected_idx);
                        if (g_content_selected_idx >= g_playlist_manager.count) {
                            g_content_selected_idx = g_playlist_manager.count - 1;
                        }
                        show_status_message(menu_text("歌单已删除", "Playlist deleted"));
                        render_playlist_manager_content();
                    }
                } else {
                    if (g_playlist_selected_playlist >= 0 && g_playlist_selected_playlist < g_playlist_manager.count) {
                        remove_track_from_playlist(g_playlist_selected_playlist, g_content_selected_idx);
                        UserPlaylist *pl = &g_playlist_manager.playlists[g_playlist_selected_playlist];
                        if (g_content_selected_idx >= pl->track_count) {
                            g_content_selected_idx = pl->track_count - 1;
                        }
                        show_status_message(menu_text("歌曲已移除", "Track removed"));
                        render_playlist_manager_content();
                    }
                }
            }
            break;
    }
}

static void handle_favorites_input(int ch) {
    switch (ch) {
        case KEY_UP:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx--;
                if (g_menu_selected_idx < 0) g_menu_selected_idx = FAVORITES_ITEM_COUNT - 1;
                render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
            } else {
                g_content_selected_idx--;
                if (g_content_selected_idx < 0) g_content_selected_idx = g_favorites.count - 1;
                if (g_content_selected_idx < 0) g_content_selected_idx = 0;
                render_favorites_content();
            }
            break;
            
        case KEY_DOWN:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx++;
                if (g_menu_selected_idx >= FAVORITES_ITEM_COUNT) g_menu_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
            } else {
                g_content_selected_idx++;
                if (g_content_selected_idx >= g_favorites.count) g_content_selected_idx = 0;
                render_favorites_content();
            }
            break;
            
        case KEY_RIGHT:
        case 9:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_focus_area = FOCUS_CONTENT;
                g_content_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
                render_favorites_content();
            }
            break;
            
        case KEY_LEFT:
            if (g_focus_area == FOCUS_CONTENT) {
                g_focus_area = FOCUS_SIDEBAR;
                render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
                render_favorites_content();
            }
            break;
            
        case 10:
        case ' ':
            if (g_focus_area == FOCUS_SIDEBAR) {
                if (g_menu_selected_idx == 0) {
                    g_focus_area = FOCUS_CONTENT;
                    g_content_selected_idx = 0;
                    render_menu_sidebar(g_menu_selected_idx, favorites_sidebar_items, FAVORITES_ITEM_COUNT);
                    render_favorites_content();
                } else if (g_menu_selected_idx == FAVORITES_ITEM_COUNT - 1) {
                    exit_current_view();
                }
            } else {
                if (g_favorites.count > 0 && g_content_selected_idx >= 0 && 
                    g_content_selected_idx < g_favorites.count) {
                    
                    Track *t = &g_favorites.tracks[g_content_selected_idx];
                    
                    int found = -1;
                    for (int i = 0; i < g_playlist.count; i++) {
                        if (strcmp(g_playlist.tracks[i], t->path) == 0) {
                            found = i;
                            break;
                        }
                    }
                    
                    if (found >= 0) {
                        play_audio(found);
                        exit_current_view();
                    } else {
                        show_status_message(menu_text("当前播放列表中没有这首歌", "Track not found in current playlist"));
                    }
                }
            }
            break;
            
        case 'd':
        case 'D':
            if (g_focus_area == FOCUS_CONTENT && g_favorites.count > 0 &&
                g_content_selected_idx >= 0 && g_content_selected_idx < g_favorites.count) {
                
                remove_from_favorites(g_content_selected_idx);
                if (g_content_selected_idx >= g_favorites.count) {
                    g_content_selected_idx = g_favorites.count - 1;
                }
                show_status_message(menu_text("已从收藏移除", "Removed from favorites"));
                render_favorites_content();
            }
            break;
    }
}

static void handle_info_input(int ch) {
    switch (ch) {
        case KEY_UP:
            g_menu_selected_idx--;
            if (g_menu_selected_idx < 0) g_menu_selected_idx = INFO_ITEM_COUNT - 1;
            render_menu_sidebar(g_menu_selected_idx, info_sidebar_items, INFO_ITEM_COUNT);
            break;
            
        case KEY_DOWN:
            g_menu_selected_idx++;
            if (g_menu_selected_idx >= INFO_ITEM_COUNT) g_menu_selected_idx = 0;
            render_menu_sidebar(g_menu_selected_idx, info_sidebar_items, INFO_ITEM_COUNT);
            break;
            
        case 10:
        case ' ':
            if (g_menu_selected_idx == INFO_ITEM_COUNT - 1) {
                exit_current_view();
            }
            break;
    }
}

void handle_menu_input(int ch) {
    if (ch == 27) {
        if (g_current_view == VIEW_PLAYLIST && g_playlist_view_mode == 1) {
            g_playlist_view_mode = 0;
            g_content_selected_idx = g_playlist_selected_playlist;
            render_playlist_manager_content();
            return;
        }
        exit_current_view();
        return;
    }
    
    if (ch >= KEY_F(1) && ch <= KEY_F(8)) {
        handle_function_keys(ch);
        return;
    }
    
    switch (g_current_view) {
        case VIEW_SETTINGS:
            handle_settings_input(ch);
            break;
        case VIEW_HISTORY:
            handle_history_input(ch);
            break;
        case VIEW_PLAYLIST:
            handle_playlist_input(ch);
            break;
        case VIEW_FAVORITES:
            handle_favorites_input(ch);
            break;
        case VIEW_INFO:
            handle_info_input(ch);
            break;
        default:
            break;
    }
}
