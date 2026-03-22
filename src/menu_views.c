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
#include "../include/menu_views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include <sys/stat.h>
#include <unistd.h>

// 外部窗口声明
extern WINDOW *win_playlist;
extern WINDOW *win_controls;
extern WINDOW *win_lyrics;

// 外部函数声明（在 ui.c 中定义）
extern void render_playlist_content(void);
extern void render_controls(void);
extern void create_layout(void);

// 全局变量定义
ViewMode g_current_view = VIEW_MAIN;
int g_menu_selected_idx = 0;
PlayHistory g_play_history = {0};
Favorites g_favorites = {0};

// 菜单项定义
static const char *menu_items[] = {
    "Settings",    // F1 - 设置
    "History",     // F2 - 历史记录
    "Play List",   // F3 - 播放列表
    "Favorites",   // F4 - 收藏夹
    "Info",        // F5 - 信息
    "Exit",        // F6 - 退出程序
    "← Back"       // 返回主界面
};
#define MENU_ITEM_COUNT 7

// 持久化存储路径
static char config_dir[MAX_PATH_LEN];
static char history_file[MAX_PATH_LEN];
static char favorites_file[MAX_PATH_LEN];

/**
 * @brief 初始化配置目录和文件路径
 */
static void init_config_paths(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(config_dir, sizeof(config_dir), "%s/.config/ter-music", home);
    snprintf(history_file, sizeof(history_file), "%s/history.json", config_dir);
    snprintf(favorites_file, sizeof(favorites_file), "%s/favorites.json", config_dir);
    
    // 创建配置目录
    mkdir(config_dir, 0755);
}

/**
 * @brief 简单的 JSON 字符串提取函数（简化实现）
 */
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
    while (*pos == ' ' || *pos == '"') pos++;
    
    size_t i = 0;
    while (*pos && *pos != '"' && i < output_size - 1) {
        output[i++] = *pos++;
    }
    output[i] = '\0';
    
    return output;
}

/**
 * @brief 简单的 JSON 整数提取函数
 */
static long extract_json_int(const char *json, const char *key) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    const char *pos = strstr(json, search_key);
    if (!pos) return 0;
    
    pos = strchr(pos, ':');
    if (!pos) return 0;
    
    pos++;
    while (*pos == ' ') pos++;
    
    return atol(pos);
}

/**
 * @brief 初始化菜单视图模块
 */
void init_menu_views(void) {
    g_current_view = VIEW_MAIN;
    g_menu_selected_idx = 0;
    
    // 初始化配置路径
    init_config_paths();
    
    // 加载持久化数据
    load_history();
    load_favorites();
}

/**
 * @brief 渲染菜单视图框架
 */
void render_menu_frame(const char *title) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // 计算布局
    int menu_width = max_x / 4;        // 左侧菜单占 1/4
    int content_width = max_x - menu_width;
    
    // 清空屏幕
    clear();
    
    // 绘制外边框
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    box(stdscr, 0, 0);
    mvprintw(0, 2, " %s ", title);
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    
    // 绘制垂直分隔线
    mvvline(1, menu_width, ACS_VLINE, max_y - 2);
    
    // 绘制分隔线交点
    mvaddch(1, menu_width, ACS_TTEE);
    mvaddch(max_y - 2, menu_width, ACS_BTEE);
    
    refresh();
}

/**
 * @brief 渲染左侧菜单栏
 */
void render_menu_sidebar(int selected_idx) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int start_y = 2;
    
    for (int i = 0; i < MENU_ITEM_COUNT && (start_y + i) < max_y - 2; i++) {
        if (i == selected_idx) {
            attron(A_REVERSE);
            mvhline(start_y + i, 1, ' ', menu_width - 1);
            mvprintw(start_y + i, 2, "%s", menu_items[i]);
            attroff(A_REVERSE);
        } else {
            mvprintw(start_y + i, 2, "%s", menu_items[i]);
        }
    }
    
    refresh();
}

/**
 * @brief 切换到指定视图
 */
void switch_to_view(ViewMode view) {
    g_current_view = view;
    g_menu_selected_idx = view;  // 同步菜单选中项
    
    // 根据视图渲染不同内容
    switch (view) {
        case VIEW_SETTINGS:
            render_menu_frame("Settings [F1]");
            render_menu_sidebar(g_menu_selected_idx);
            render_settings_content();
            break;
        case VIEW_HISTORY:
            render_menu_frame("History [F2]");
            render_menu_sidebar(g_menu_selected_idx);
            render_history_content();
            break;
        case VIEW_PLAYLIST:
            render_menu_frame("Play List [F3]");
            render_menu_sidebar(g_menu_selected_idx);
            // 复用现有播放列表渲染（需要调整位置）
            render_playlist_content();
            break;
        case VIEW_FAVORITES:
            render_menu_frame("Favorites [F4]");
            render_menu_sidebar(g_menu_selected_idx);
            render_favorites_content();
            break;
        case VIEW_INFO:
            render_menu_frame("Info [F5]");
            render_menu_sidebar(g_menu_selected_idx);
            render_info_content();
            break;
        default:
            break;
    }
}

/**
 * @brief 退出当前视图返回主界面
 */
void exit_current_view(void) {
    g_current_view = VIEW_MAIN;
    g_menu_selected_idx = 0;
    
    // 恢复主界面
    create_layout();
    render_playlist_content();
    render_controls();
}

/**
 * @brief 处理功能键（F1-F6）
 */
void handle_function_keys(int fkey) {
    switch(fkey) {
        case KEY_F(1):
            switch_to_view(VIEW_SETTINGS);
            break;
        case KEY_F(2):
            switch_to_view(VIEW_HISTORY);
            break;
        case KEY_F(3):
            switch_to_view(VIEW_PLAYLIST);
            break;
        case KEY_F(4):
            switch_to_view(VIEW_FAVORITES);
            break;
        case KEY_F(5):
            switch_to_view(VIEW_INFO);
            break;
        case KEY_F(6):
            // 退出程序
            cleanup();
            printf("ter-music exited gracefully.\n");
            exit(0);
            break;
        default:
            break;
    }
}

/**
 * @brief 渲染设置界面内容
 */
void render_settings_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 3;
    
    attron(COLOR_PAIR(COLOR_PAIR_CONTROLS));
    
    mvprintw(start_y++, content_start_x, "Audio Settings");
    mvprintw(start_y++, content_start_x, "  Device: %s", g_default_audio_device);
    start_y++;
    
    mvprintw(start_y++, content_start_x, "Playback Settings");
    mvprintw(start_y++, content_start_x, "  Loop Mode: %s", get_loop_mode_str());
    start_y++;
    
    mvprintw(start_y++, content_start_x, "UI Settings");
    mvprintw(start_y++, content_start_x, "  Color Theme: Default");
    start_y++;
    
    mvprintw(start_y++, content_start_x, "Shortcuts");
    mvprintw(start_y++, content_start_x, "  F1-F5: Open menu views");
    mvprintw(start_y++, content_start_x, "  F6: Exit program");
    mvprintw(start_y++, content_start_x, "  ESC: Back to main view");
    mvprintw(start_y++, content_start_x, "  C: Focus controls");
    mvprintw(start_y++, content_start_x, "  L: Focus playlist");
    start_y++;
    
    mvprintw(start_y++, content_start_x, "About");
    mvprintw(start_y++, content_start_x, "  Author: 燕戏竹林");
    mvprintw(start_y++, content_start_x, "  Email: yxzl666xx@outlook.com");
    
    attroff(COLOR_PAIR(COLOR_PAIR_CONTROLS));
    
    refresh();
}

/**
 * @brief 渲染历史记录界面内容
 */
void render_history_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 3;
    
    attron(COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    
    mvprintw(start_y++, content_start_x, "Recently Played (%d songs)", g_play_history.count);
    mvprintw(start_y++, content_start_x, "----------------------------------------");
    
    if (g_play_history.count == 0) {
        mvprintw(start_y++, content_start_x, "No history yet. Play some songs!");
    } else {
        int display_count = 0;
        int start_idx = 0;
        
        // 简单的滚动逻辑
        if (g_menu_selected_idx >= g_play_history.count) {
            start_idx = g_menu_selected_idx - (max_y - start_y - 2);
            if (start_idx < 0) start_idx = 0;
        }
        
        for (int i = start_idx; i < g_play_history.count && display_count < (max_y - start_y - 2); i++) {
            HistoryEntry *entry = &g_play_history.entries[i];
            
            // 格式化时间
            char time_str[64];
            struct tm *tm_info = localtime(&entry->play_time);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
            
            if (i == g_menu_selected_idx) {
                attron(A_REVERSE);
                mvprintw(start_y + display_count, content_start_x, " %-30s - %-20s [%s]", 
                        entry->title, entry->artist, time_str);
                attroff(A_REVERSE);
            } else {
                mvprintw(start_y + display_count, content_start_x, " %-30s - %-20s [%s]", 
                        entry->title, entry->artist, time_str);
            }
            display_count++;
        }
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    
    refresh();
}

/**
 * @brief 渲染收藏夹界面内容
 */
void render_favorites_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 3;
    
    attron(COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    mvprintw(start_y++, content_start_x, "Favorites (%d songs)", g_favorites.count);
    mvprintw(start_y++, content_start_x, "----------------------------------------");
    
    if (g_favorites.count == 0) {
        mvprintw(start_y++, content_start_x, "No favorites yet. Add some songs!");
    } else {
        int display_count = 0;
        int start_idx = 0;
        
        // 简单的滚动逻辑
        if (g_menu_selected_idx >= g_favorites.count) {
            start_idx = g_menu_selected_idx - (max_y - start_y - 2);
            if (start_idx < 0) start_idx = 0;
        }
        
        for (int i = start_idx; i < g_favorites.count && display_count < (max_y - start_y - 2); i++) {
            Track *track = &g_favorites.tracks[i];
            
            if (i == g_menu_selected_idx) {
                attron(A_REVERSE);
                mvprintw(start_y + display_count, content_start_x, " %-30s - %-20s", 
                        track->title, track->artist);
                attroff(A_REVERSE);
            } else {
                mvprintw(start_y + display_count, content_start_x, " %-30s - %-20s", 
                        track->title, track->artist);
            }
            display_count++;
        }
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    refresh();
}

/**
 * @brief 渲染信息界面内容
 */
void render_info_content(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 3;
    
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    
    mvprintw(start_y++, content_start_x, "Current Track Information");
    mvprintw(start_y++, content_start_x, "----------------------------------------");
    
    if (g_playlist.is_loaded && g_playlist.count > 0) {
        Track *t;
        if (g_current_play_index >= 0) {
            t = &g_playlist.tracks[g_current_play_index];
        } else {
            t = &g_playlist.tracks[g_selected_index];
        }
        
        mvprintw(start_y++, content_start_x, "Title:  %s", t->title);
        mvprintw(start_y++, content_start_x, "Artist: %s", t->artist);
        mvprintw(start_y++, content_start_x, "Album:  %s", t->album);
        mvprintw(start_y++, content_start_x, "Path:   %s", t->path);
        start_y++;
        
        // 播放状态
        const char *status_str;
        switch (g_play_state) {
            case PLAY_STATE_PLAYING: status_str = "Playing"; break;
            case PLAY_STATE_PAUSED:  status_str = "Paused";  break;
            case PLAY_STATE_STOPPED: status_str = "Stopped"; break;
            default: status_str = "Unknown"; break;
        }
        
        mvprintw(start_y++, content_start_x, "Status: %s", status_str);
        
        // 进度信息
        if (g_total_duration > 0) {
            int current_min = g_current_position / 60;
            int current_sec = g_current_position % 60;
            int total_min = g_total_duration / 60;
            int total_sec = g_total_duration % 60;
            
            mvprintw(start_y++, content_start_x, "Progress: %02d:%02d / %02d:%02d", 
                    current_min, current_sec, total_min, total_sec);
        }
        
        start_y++;
        mvprintw(start_y++, content_start_x, "Loop Mode: %s", get_loop_mode_str());
    } else {
        mvprintw(start_y++, content_start_x, "No track loaded.");
        mvprintw(start_y++, content_start_x, "Press 'O' to open a music folder.");
    }
    
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    
    refresh();
}

/**
 * @brief 处理菜单模式下的输入
 */
void handle_menu_input(int ch) {
    // ESC 键或 q 键退出当前视图
    if (ch == 27 || ch == 'q') {
        exit_current_view();
        return;
    }
    
    // F1-F6 功能键
    if (ch >= KEY_F(1) && ch <= KEY_F(6)) {
        handle_function_keys(ch);
        return;
    }
    
    // 方向键导航
    switch (ch) {
        case KEY_UP:
            g_menu_selected_idx--;
            if (g_menu_selected_idx < 0) {
                g_menu_selected_idx = MENU_ITEM_COUNT - 1;
            }
            render_menu_sidebar(g_menu_selected_idx);
            break;
            
        case KEY_DOWN:
            g_menu_selected_idx++;
            if (g_menu_selected_idx >= MENU_ITEM_COUNT) {
                g_menu_selected_idx = 0;
            }
            render_menu_sidebar(g_menu_selected_idx);
            break;
            
        case KEY_RIGHT:
            // 切换到内容区域焦点（未来扩展）
            break;
            
        case KEY_LEFT:
            // 切换回菜单区域焦点（未来扩展）
            break;
            
        case 10:  // Enter 键
        case ' ': // 空格键
            // 执行选中的菜单项
            if (g_menu_selected_idx == MENU_ITEM_COUNT - 1) {
                // "← Back" 选项
                exit_current_view();
            } else if (g_menu_selected_idx == 5) {
                // "Exit" 选项
                cleanup();
                printf("ter-music exited gracefully.\n");
                exit(0);
            } else {
                // 其他菜单项，切换到对应视图
                switch_to_view((ViewMode)g_menu_selected_idx);
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief 添加历史记录
 */
void add_history_entry(Track *track) {
    if (!track || g_play_history.count >= MAX_HISTORY_COUNT) return;
    
    // 移动现有记录
    if (g_play_history.count > 0) {
        memmove(&g_play_history.entries[1], &g_play_history.entries[0], 
                sizeof(HistoryEntry) * (g_play_history.count));
    }
    
    // 添加新记录到开头
    strncpy(g_play_history.entries[0].path, track->path, MAX_PATH_LEN - 1);
    strncpy(g_play_history.entries[0].title, track->title, MAX_META_LEN - 1);
    strncpy(g_play_history.entries[0].artist, track->artist, MAX_META_LEN - 1);
    g_play_history.entries[0].play_time = time(NULL);
    
    g_play_history.count++;
    if (g_play_history.count > MAX_HISTORY_COUNT) {
        g_play_history.count = MAX_HISTORY_COUNT;
    }
    
    // 保存历史记录
    save_history();
}

/**
 * @brief 加载历史记录（从持久化存储）
 */
void load_history(void) {
    FILE *f = fopen(history_file, "r");
    if (!f) return;
    
    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // 分配缓冲区
    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return;
    }
    
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);
    
    // 解析 JSON（简化实现）
    g_play_history.count = 0;
    
    // 查找 history 数组
    const char *pos = strstr(json, "\"history\"");
    if (!pos) {
        free(json);
        return;
    }
    
    // 简单解析每个历史记录条目
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
    
    // 遍历每个对象
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
        
        // 提取字段
        extract_json_string(obj, "path", g_play_history.entries[g_play_history.count].path, MAX_PATH_LEN);
        extract_json_string(obj, "title", g_play_history.entries[g_play_history.count].title, MAX_META_LEN);
        extract_json_string(obj, "artist", g_play_history.entries[g_play_history.count].artist, MAX_META_LEN);
        g_play_history.entries[g_play_history.count].play_time = (time_t)extract_json_int(obj, "play_time");
        
        g_play_history.count++;
        free(obj);
        
        pos = obj_end + 1;
    }
    
    free(json);
}

/**
 * @brief 保存历史记录（到持久化存储）
 */
void save_history(void) {
    FILE *f = fopen(history_file, "w");
    if (!f) return;
    
    fprintf(f, "{\n  \"history\": [\n");
    
    for (int i = 0; i < g_play_history.count; i++) {
        HistoryEntry *e = &g_play_history.entries[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"path\": \"%s\",\n", e->path);
        fprintf(f, "      \"title\": \"%s\",\n", e->title);
        fprintf(f, "      \"artist\": \"%s\",\n", e->artist);
        fprintf(f, "      \"play_time\": %ld\n", (long)e->play_time);
        fprintf(f, "    }%s\n", (i < g_play_history.count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n  \"count\": %d\n}\n", g_play_history.count);
    
    fclose(f);
}

/**
 * @brief 加载收藏夹（从持久化存储）
 */
void load_favorites(void) {
    FILE *f = fopen(favorites_file, "r");
    if (!f) return;
    
    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // 分配缓冲区
    char *json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return;
    }
    
    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);
    
    // 解析 JSON（简化实现）
    g_favorites.count = 0;
    
    // 查找 favorites 数组
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
    
    // 遍历每个对象
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
        
        // 提取字段
        extract_json_string(obj, "path", g_favorites.tracks[g_favorites.count].path, MAX_PATH_LEN);
        extract_json_string(obj, "title", g_favorites.tracks[g_favorites.count].title, MAX_META_LEN);
        extract_json_string(obj, "artist", g_favorites.tracks[g_favorites.count].artist, MAX_META_LEN);
        
        g_favorites.count++;
        free(obj);
        
        pos = obj_end + 1;
    }
    
    free(json);
}

/**
 * @brief 保存收藏夹（到持久化存储）
 */
void save_favorites(void) {
    FILE *f = fopen(favorites_file, "w");
    if (!f) return;
    
    fprintf(f, "{\n  \"favorites\": [\n");
    
    for (int i = 0; i < g_favorites.count; i++) {
        Track *t = &g_favorites.tracks[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"path\": \"%s\",\n", t->path);
        fprintf(f, "      \"title\": \"%s\",\n", t->title);
        fprintf(f, "      \"artist\": \"%s\",\n", t->artist);
        fprintf(f, "    }%s\n", (i < g_favorites.count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n  \"count\": %d\n}\n", g_favorites.count);
    
    fclose(f);
}

/**
 * @brief 添加到收藏夹
 */
int add_to_favorites(Track *track) {
    if (!track || g_favorites.count >= MAX_FAVORITES_COUNT) {
        return -1;
    }
    
    // 检查是否已存在
    for (int i = 0; i < g_favorites.count; i++) {
        if (strcmp(g_favorites.tracks[i].path, track->path) == 0) {
            return 0;  // 已存在
        }
    }
    
    // 添加到收藏夹
    strncpy(g_favorites.tracks[g_favorites.count].path, track->path, MAX_PATH_LEN - 1);
    strncpy(g_favorites.tracks[g_favorites.count].title, track->title, MAX_META_LEN - 1);
    strncpy(g_favorites.tracks[g_favorites.count].artist, track->artist, MAX_META_LEN - 1);
    strncpy(g_favorites.tracks[g_favorites.count].album, track->album, MAX_META_LEN - 1);
    
    g_favorites.count++;
    
    // 保存到文件
    save_favorites();
    
    return 0;
}

/**
 * @brief 从收藏夹移除
 */
int remove_from_favorites(int index) {
    if (index < 0 || index >= g_favorites.count) {
        return -1;
    }
    
    // 移动后续记录
    if (index < g_favorites.count - 1) {
        memmove(&g_favorites.tracks[index], &g_favorites.tracks[index + 1],
                sizeof(Track) * (g_favorites.count - index - 1));
    }
    
    g_favorites.count--;
    
    // 保存到文件
    save_favorites();
    
    return 0;
}