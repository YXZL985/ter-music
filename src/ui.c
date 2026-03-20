#include "../include/defs.h"
#include "../include/lyrics.h"    // 新增：歌词模块
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>
#include <locale.h>
#include <pthread.h>

extern WINDOW *win_playlist;
extern WINDOW *win_controls;
extern WINDOW *win_lyrics;

extern const char *control_labels[];

// 全局窗口变量
WINDOW *win_playlist;
WINDOW *win_controls;
WINDOW *win_lyrics;

// 控件标签文本
const char *control_labels[] = {"<<", "Play/Pause", ">>", "Stop", "Loop", "Progress"};

/**
 * 初始化ncurses环境
 * 设置本地化、终端模式和颜色对
 */
void init_ncurses() {
    // 设置本地化环境，支持中文等多字节字符
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "");

    initscr();             // 启动 ncurses 模式
    cbreak();              // 禁用行缓冲，立即读取输入
    noecho();              // 不回显输入字符
    keypad(stdscr, TRUE);  // 启用功能键（如方向键）
    curs_set(0);           // 隐藏光标
    clear();               // 清屏

    // 初始化颜色
    if (has_colors()) {
        start_color();
        init_pair(COLOR_PAIR_BORDER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PAIR_PLAYLIST, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_CONTROLS, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_LYRICS, COLOR_GREEN, COLOR_BLACK);
    }
}

/**
 * UTF-8字符串截断函数
 * 根据显示列数而非字节数截断字符串，正确处理多字节字符
 */
int utf8_str_truncate(char *dest, const char *src, int max_cols) {
    if (!dest || !src || max_cols <= 0) {
        if (dest) *dest = '\0';
        return 0;
    }
    int cols = 0;
    char *d = dest;
    const char *s = src;
    while (*s && cols < max_cols) {
        unsigned char c = *s;
        int char_len = 0, char_width = 1;
        if (c < 0x80) { char_len = 1; char_width = 1; }
        else if ((c & 0xE0) == 0xC0) { char_len = 2; char_width = 2; }
        else if ((c & 0xF0) == 0xE0) { char_len = 3; char_width = 2; }
        else if ((c & 0xF8) == 0xF0) { char_len = 4; char_width = 2; }
        else { char_len = 1; char_width = 1; }

        if (cols + char_width > max_cols) break;
        for (int i=0; i<char_len; i++) *d++ = *s++;
        cols += char_width;
    }
    if (*s && cols + 3 <= max_cols) strcpy(d, "...");
    else *d = '\0';
    return cols;
}

/**
 * 计算 UTF-8 字符串的显示宽度（列数）
 */
int utf8_str_width(const char *src) {
    if (!src) return 0;
    int cols = 0;
    const char *s = src;
    while (*s) {
        unsigned char c = *s;
        int char_len = 0, char_width = 1;
        if (c < 0x80) { char_len = 1; char_width = 1; }
        else if ((c & 0xE0) == 0xC0) { char_len = 2; char_width = 2; }
        else if ((c & 0xF0) == 0xE0) { char_len = 3; char_width = 2; }
        else if ((c & 0xF8) == 0xF0) { char_len = 4; char_width = 2; }
        else { char_len = 1; char_width = 1; }
        
        for (int i = 0; i < char_len; i++) s++;
        cols += char_width;
    }
    return cols;
}

/**
 * UTF-8 字符串从指定偏移开始截取
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @param start_col 起始列偏移（从 0 开始）
 * @param max_cols 最大列数
 * @return 实际占用的列数
 */
int utf8_str_substring(char *dest, const char *src, int start_col, int max_cols) {
    if (!dest || !src || max_cols <= 0) {
        if (dest) *dest = '\0';
        return 0;
    }
    
    int cols = 0;
    char *d = dest;
    const char *s = src;
    
    // 跳过 start_col 列
    while (*s && cols < start_col) {
        unsigned char c = *s;
        int char_len = 0, char_width = 1;
        if (c < 0x80) { char_len = 1; char_width = 1; }
        else if ((c & 0xE0) == 0xC0) { char_len = 2; char_width = 2; }
        else if ((c & 0xF0) == 0xE0) { char_len = 3; char_width = 2; }
        else if ((c & 0xF8) == 0xF0) { char_len = 4; char_width = 2; }
        else { char_len = 1; char_width = 1; }
        
        if (cols + char_width > start_col) {
            // 部分进入可视区域，从这个字符开始
            break;
        }
        for (int i = 0; i < char_len; i++) s++;
        cols += char_width;
    }
    
    // 从当前位置开始复制最多 max_cols 列的字符
    int result_cols = 0;
    while (*s && result_cols < max_cols) {
        unsigned char c = *s;
        int char_len = 0, char_width = 1;
        if (c < 0x80) { char_len = 1; char_width = 1; }
        else if ((c & 0xE0) == 0xC0) { char_len = 2; char_width = 2; }
        else if ((c & 0xF0) == 0xE0) { char_len = 3; char_width = 2; }
        else if ((c & 0xF8) == 0xF0) { char_len = 4; char_width = 2; }
        else { char_len = 1; char_width = 1; }
        
        if (result_cols + char_width > max_cols) break;
        for (int i = 0; i < char_len; i++) *d++ = *s++;
        result_cols += char_width;
    }
    *d = '\0';
    
    return result_cols;
}

/**
 * 创建和调整窗口布局
 * 设置播放列表、控制栏和歌词窗口的大小和位置
 */
void create_layout() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int lyrics_width = max_x / 3; // 歌词栏占宽度的 1/3
    int main_width = max_x - lyrics_width;

    // 计算高度比例 (5:2)，预留边框空间
    int total_inner_height = max_y - 4; // 上下留边距
    int playlist_height = (total_inner_height * 5) / 7;
    int controls_height = total_inner_height - playlist_height;

    // 1. 创建播放列表窗口 (左上)
    win_playlist = newwin(playlist_height, main_width, 1, 1);
    box(win_playlist, 0, 0);
    mvwprintw(win_playlist, 0, 2, " Play List ");
    wbkgd(win_playlist, COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    wrefresh(win_playlist);

    // 2. 创建控制栏窗口 (左下)
    win_controls = newwin(controls_height, main_width, 1 + playlist_height, 1);
    box(win_controls, 0, 0);
    mvwprintw(win_controls, 0, 2, " Controls [Space:Act] [C:FocusCtrl] [L:FocusList] ");
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));
    
    wrefresh(win_controls);

    // 3. 创建歌词侧栏窗口 (右侧)
    win_lyrics = newwin(max_y - 2, lyrics_width, 1, 1 + main_width);
    box(win_lyrics, 0, 0);
    mvwprintw(win_lyrics, 0, 2, " Lyrics ");
    wbkgd(win_lyrics, COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    mvwprintw(win_lyrics, 2, 2, "No lyrics loaded.");
    wrefresh(win_lyrics);

    // --- 新增：绘制分隔线 ---

    // 绘制左侧区域与右侧歌词区之间的垂直分隔线
    // 起点：(1, 1 + main_width), 长度：max_y - 2
    mvvline(1, 1 + main_width, ACS_VLINE, max_y - 2);

    // 绘制播放列表与控制栏之间的水平分隔线
    // 起点：(1 + playlist_height, 1), 长度：main_width
    mvhline(1 + playlist_height, 1, ACS_HLINE, main_width);
    
    // 绘制交叉点字符，使分隔线连接更自然
    mvaddch(1 + playlist_height, 1 + main_width, ACS_PLUS);

    // 刷新标准屏以显示分隔线
    refresh();
}

/**
 * 绘制播放列表内容
 * 包括歌曲列表和底部状态栏
 */
void render_playlist_content() {
    werase(win_playlist); // 清空窗口内容
    box(win_playlist, 0, 0);
    mvwprintw(win_playlist, 0, 2, " Play List ");
    wbkgd(win_playlist, COLOR_PAIR(COLOR_PAIR_PLAYLIST));

    int h, w;
    getmaxyx(win_playlist, h, w);
    int content_height = h - 2; // 可用行数
    
    // 如果未加载，显示提示信息
    if (!g_playlist.is_loaded) {
        mvwprintw(win_playlist, h/2 - 1, 2, "Playlist is empty.");
        mvwprintw(win_playlist, h/2, 2, "Press 'O' to open music folder.");
        mvwprintw(win_playlist, h/2 + 1, 2, "Current Path: %s", g_playlist.folder_path[0] ? g_playlist.folder_path : "(None)");
    } else {
        int start_idx = 0;
        int visible_lines = content_height - 5; // 预留 5 行给底部状态栏
        
        // 简单的滚动逻辑：保持选中项在可视范围内
        if (g_selected_index >= visible_lines) {
            start_idx = g_selected_index - visible_lines + 1;
        }
        
        for (int i = 0; i < visible_lines && (start_idx + i) < g_playlist.count; i++) {
            int idx = start_idx + i;
            Track *t = &g_playlist.tracks[idx];
            
            // 计算可用宽度，为不同字段分配空间
            int title_width = (w - 4) * 3 / 5;  // 标题占3/5
            int artist_width = (w - 4) * 2 / 5; // 艺术家占2/5
            
            // 截断过长的字符串
            char truncated_title[MAX_META_LEN];
            char truncated_artist[MAX_META_LEN];
            
            utf8_str_truncate(truncated_title, t->title, title_width - 1);
            utf8_str_truncate(truncated_artist, t->artist, artist_width - 1);

            if (idx == g_selected_index && g_control_focus == 0) {
                wattron(win_playlist, A_REVERSE);
                mvwprintw(win_playlist, i + 1, 1, " %-*s %-*s ", title_width, truncated_title, artist_width, truncated_artist);
                wattroff(win_playlist, A_REVERSE);
            } else {
                mvwprintw(win_playlist, i + 1, 2, "%-*s %-*s", title_width, truncated_title, artist_width, truncated_artist);
            }
        }
        
        if (g_playlist.count == 0) {
             mvwprintw(win_playlist, 1, 2, "No audio files found in this directory.");
        }

        // --- 新增：在播放列表底部绘制状态栏 ---
        int status_line = h - 6;
        mvwhline(win_playlist, status_line, 1, ACS_HLINE, w - 2);
        
        // 根据全局播放状态更新状态信息
        char status_msg[MAX_META_LEN];
        switch (g_play_state) {
            case PLAY_STATE_PLAYING:
                strcpy(status_msg, "Playing");
                break;
            case PLAY_STATE_PAUSED:
                strcpy(status_msg, "Paused");
                break;
            case PLAY_STATE_STOPPED:
            default:
                strcpy(status_msg, "Stopped");
                break;
        }
        
        if (g_playlist.count > 0) {
            Track *t;
            if (g_current_play_index >= 0) {
                // 显示当前正在播放的歌曲
                t = &g_playlist.tracks[g_current_play_index];
            } else {
                // 显示当前选中的歌曲
                t = &g_playlist.tracks[g_selected_index];
            }
            
            // 计算可用宽度，确保不超出边框
            int content_width = w - 4;  // 减去左右边框和空格
            int status_width = w - 4;
            int title_width = status_width * 2 / 5;
            int artist_width = status_width * 2 / 5;
            int album_width = status_width * 1 / 5;
            
            // 截断过长的字符串
            char truncated_title[MAX_META_LEN];
            char truncated_artist[MAX_META_LEN];
            char truncated_album[MAX_META_LEN];
            
            utf8_str_truncate(truncated_title, t->title, title_width - 1);
            utf8_str_truncate(truncated_artist, t->artist, artist_width - 1);
            utf8_str_truncate(truncated_album, t->album, album_width - 1);
            
            mvwprintw(win_playlist, status_line + 1, 2, "Status: %s | Loop: %s", status_msg, get_loop_mode_str());
            mvwprintw(win_playlist, status_line + 2, 2, "Title: %s", truncated_title); 
            mvwprintw(win_playlist, status_line + 3, 2, "Artist: %s", truncated_artist);
            mvwprintw(win_playlist, status_line + 4, 2, "Album: %s", truncated_album);
        } else {
             mvwprintw(win_playlist, status_line + 1, 2, "Status: %s | Track: --", status_msg);
             mvwprintw(win_playlist, status_line + 2, 2, "Title: --");
             mvwprintw(win_playlist, status_line + 3, 2, "Artist: --");
             mvwprintw(win_playlist, status_line + 4, 2, "Album: --");
        }
    }
    wrefresh(win_playlist);
}

/**
 * 渲染控制栏按钮
 * 显示播放控制按钮并高亮当前选中的控件
 */
void render_controls() {
    werase(win_controls);
    box(win_controls, 0, 0);
    
    const char *focus_hint = g_control_focus ? "[Ctrl Focus]" : "[List Focus]";
    mvwprintw(win_controls, 0, 2, " Controls [Space:Act] [C:FocusCtrl] [L:FocusList] %s", focus_hint);
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));

    int h, w;
    getmaxyx(win_controls, h, w);
    
    // 绘制进度条（在控件上方）
    if (g_play_state != PLAY_STATE_STOPPED && g_total_duration > 0) {
        int progress_row = h / 2 - 2; // 在控件上方两行
        
        // 计算进度百分比
        int progress_percent = (g_current_position * 100) / g_total_duration;
        if (progress_percent > 100) progress_percent = 100;
        
        // 计算进度条长度
        int progress_bar_width = w - 30; // 左右留出更多空间
        int filled_width = (progress_bar_width * progress_percent) / 100;
        
        // 格式化时间显示
        int current_min = g_current_position / 60;
        int current_sec = g_current_position % 60;
        int total_min = g_total_duration / 60;
        int total_sec = g_total_duration % 60;
        
        // 检查是否选中进度条
        int is_progress_selected = (g_current_control_idx == 5 && g_control_focus == 1);
        
        if (is_progress_selected) {
            wattron(win_controls, A_REVERSE | A_BOLD);
        }
        
        // 绘制进度条
        mvwprintw(win_controls, progress_row, 2, "%02d:%02d/%02d:%02d", 
                 current_min, current_sec, total_min, total_sec);
        
        // 绘制进度条图形
        mvwprintw(win_controls, progress_row, 12, "[");
        for (int i = 0; i < progress_bar_width; i++) {
            if (i < filled_width) {
                mvwprintw(win_controls, progress_row, 13 + i, "=");
            } else if (i == filled_width) {
                mvwprintw(win_controls, progress_row, 13 + i, ">" );
            } else {
                mvwprintw(win_controls, progress_row, 13 + i, "-");
            }
        }
        mvwprintw(win_controls, progress_row, 13 + progress_bar_width, "]");
        
        // 显示百分比
        mvwprintw(win_controls, progress_row, 13 + progress_bar_width + 2, "%d%%", progress_percent);
        
        if (is_progress_selected) {
            wattroff(win_controls, A_REVERSE | A_BOLD);
        }
    }
    
    int row = h / 2; // 垂直居中
    
    // 计算按钮总宽度以便居中
    int total_len = 0;
    for(int i=0; i<CONTROL_COUNT-1; i++) { // 不包括进度条
        total_len += strlen(control_labels[i]) + 4; // 标签 + [ ] + 空格
    }
    int start_col = (w - total_len) / 2;
    if (start_col < 1) start_col = 1;

    int current_col = start_col;
    for (int i = 0; i < CONTROL_COUNT-1; i++) { // 不包括进度条
        char display_label[32];
        if (i == 4) { // 循环按钮
            snprintf(display_label, sizeof(display_label), "%s:%s", control_labels[i], get_loop_mode_str());
        } else {
            utf8_str_truncate(display_label, control_labels[i], sizeof(display_label) - 1);
        }
        int len = strlen(display_label);
        
        if (i == g_current_control_idx && g_control_focus == 1) {
            // 高亮当前选中的控件
            wattron(win_controls, A_REVERSE | A_BOLD);
            mvwprintw(win_controls, row, current_col, " [%s] ", display_label);
            wattroff(win_controls, A_REVERSE | A_BOLD);
        } else {
            mvwprintw(win_controls, row, current_col, " [%s] ", display_label);
        }
        
        current_col += len + 4; // 移动到下一个按钮位置
    }

    wrefresh(win_controls);
}

/**
 * 更新进度条（增量更新版本）
 * 直接计算百分比并只重绘进度条区域
 */
void update_progress_bar() {
    // 只在播放状态下更新进度条
    if (g_play_state == PLAY_STATE_STOPPED || g_total_duration <= 0) {
        return;
    }
    
    int h, w;
    getmaxyx(win_controls, h, w);
    
    // 直接从全局变量获取最新位置（播放线程会更新它）
    int current_pos = g_current_position;
    
    // 计算进度百分比
    int progress_percent = (current_pos * 100) / g_total_duration;
    if (progress_percent > 100) progress_percent = 100;
    
    // 计算进度条长度
    int progress_bar_width = w - 30;
    int filled_width = (progress_bar_width * progress_percent) / 100;
    
    // 格式化时间显示
    int current_min = current_pos / 60;
    int current_sec = current_pos % 60;
    int total_min = g_total_duration / 60;
    int total_sec = g_total_duration % 60;
    
    // 检查是否选中进度条控件
    int is_progress_selected = (g_current_control_idx == 5 && g_control_focus == 1);
    
    int progress_row = h / 2 - 2;
    
    // 只清除进度条所在行
    wmove(win_controls, progress_row, 0);
    wclrtoeol(win_controls);
    
    if (is_progress_selected) {
        wattron(win_controls, A_REVERSE | A_BOLD);
    }
    
    // 绘制时间文本
    mvwprintw(win_controls, progress_row, 2, "%02d:%02d/%02d:%02d", 
             current_min, current_sec, total_min, total_sec);
    
    // 绘制进度条图形
    mvwprintw(win_controls, progress_row, 12, "[");
    for (int i = 0; i < progress_bar_width; i++) {
        if (i < filled_width) {
            mvwprintw(win_controls, progress_row, 13 + i, "=");
        } else if (i == filled_width) {
            mvwprintw(win_controls, progress_row, 13 + i, ">");
        } else {
            mvwprintw(win_controls, progress_row, 13 + i, "-");
        }
    }
    mvwprintw(win_controls, progress_row, 13 + progress_bar_width, "]");
    
    // 显示百分比
    mvwprintw(win_controls, progress_row, 13 + progress_bar_width + 2, "%d%%", progress_percent);
    
    if (is_progress_selected) {
        wattroff(win_controls, A_REVERSE | A_BOLD);
    }
    
    // 刷新控件窗口
    wrefresh(win_controls);
    
    // 更新歌词显示
    update_lyrics_display();
}

/**
 * 更新控制栏状态信息
 * 在控制栏底部显示临时消息
 */
void update_controls_status(const char *msg) {
    int h, w;
    getmaxyx(win_controls, h, w);
    mvwprintw(win_controls, h-1, 2, "%s", msg);
    wclrtoeol(win_controls);
    wrefresh(win_controls);
    // 移除阻塞的sleep调用，让状态信息自然显示
}

/**
 * 提示用户输入文件夹路径
 * 处理路径输入和验证
 */
void prompt_open_folder() {
    echo(); // 开启回显以输入路径
    curs_set(1); // 显示光标
    
    mvwprintw(win_controls, 4, 2, "Enter folder path: ");
    wclrtoeol(win_controls);
    wrefresh(win_controls);
    
    char input_path[MAX_PATH_LEN];
    // 注意：在 setlocale 设置正确后，wgetnstr 通常能处理多字节字符输入
    // 如果仍然乱码，可能需要改用 get_wch 逐字读取宽字符并转换
    wgetnstr(win_controls, input_path, MAX_PATH_LEN - 1);
    
    noecho();
    curs_set(0);
    
    // 清除输入提示行
    mvwprintw(win_controls, 4, 2, "                    "); 
    wrefresh(win_controls);

    if (strlen(input_path) > 0) {
        // 处理 ~ 路径
        char expanded_path[MAX_PATH_LEN];
        if (input_path[0] == '~') {
            const char *home = getenv("HOME");
            if (home) {
                snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, input_path + 1);
            } else {
                utf8_str_truncate(expanded_path, input_path, sizeof(expanded_path) - 1);
            }
        } else {
            utf8_str_truncate(expanded_path, input_path, sizeof(expanded_path) - 1);
        }
        
        // 验证路径是否存在且为目录
        struct stat s;
        if (stat(expanded_path, &s) == 0 && S_ISDIR(s.st_mode)) {
            int count = load_playlist(expanded_path);
            if (count > 0) {
                update_controls_status("Folder loaded successfully");
                g_selected_index = 0;
                render_playlist_content();
            } else {
                update_controls_status("No audio files found");
                g_playlist.is_loaded = 0; // 标记为空
                render_playlist_content();
            }
        } else {
            update_controls_status("Invalid path!");
            g_playlist.is_loaded = 0;
            render_playlist_content();
        }
    }
}

/**
 * 主事件循环
 * 处理用户输入、焦点切换和功能调用
 */
void run_event_loop() {
    int ch;
    
    // 初始渲染
    render_playlist_content();
    render_controls(); // 初始绘制控件
    
    // 设置输入超时为 10ms（100 FPS 刷新率，确保进度条流畅）
    timeout(10);
    
    while ((ch = getch()) != 'q') {
        // 每帧都更新进度条（当播放状态为播放或暂停时）
        if (g_play_state == PLAY_STATE_PLAYING || g_play_state == PLAY_STATE_PAUSED) {
            update_progress_bar();
        }
        
        // 如果用户没有按键，继续循环以允许进度条更新
        if (ch == ERR) {
            continue;
        }
        
        if (!g_playlist.is_loaded && g_control_focus == 0) {
            // 未加载文件夹且焦点在列表时，主要监听 'O' 或 'o'
            if (ch == 'O' || ch == 'o') {
                prompt_open_folder();
                render_playlist_content();
                continue;
            }
        }

        // 焦点切换
        if (ch == 'C' || ch == 'c') {
            // 切换到控制区焦点 (需求要求大写 C，这里兼容小写以防误触，也可严格限制)
            // 严格遵循需求：按下大写 C
            if (ch == 'C') {
                g_control_focus = 1;
                g_current_control_idx = 1; // 默认选中播放/暂停
                render_playlist_content(); // 重绘列表以取消高亮
                render_controls();         // 重绘控件以高亮
                continue;
            }
        }
        
        if (ch == 'L' || ch == 'l') {
            // 切换到列表区焦点 (需求要求大写 L)
            if (ch == 'L') {
                g_control_focus = 0;
                render_controls();         // 重绘控件以取消高亮
                render_playlist_content(); // 重绘列表以高亮选中项
                continue;
            }
        }

        if (g_control_focus == 1) {
            // === 控制区模式 ===
            switch (ch) {
                case KEY_LEFT:
                    g_current_control_idx--;
                    if (g_current_control_idx < 0) g_current_control_idx = CONTROL_COUNT - 1;
                    render_controls();
                    break;
                case KEY_RIGHT:
                    g_current_control_idx++;
                    if (g_current_control_idx >= CONTROL_COUNT) g_current_control_idx = 0;
                    render_controls();
                    break;
                case ',':
                    // 进度减 5 秒 - 基于进度跟踪器的准确位置
                    if (g_play_state != PLAY_STATE_STOPPED && g_total_duration > 0) {
                        // 使用 progress_tracker 获取准确的当前播放位置
                        int current_pos = progress_tracker_get_position_seconds();
                        int new_pos = current_pos - 5;
                        if (new_pos < 0) new_pos = 0;
                        // 先同步进度跟踪器，再发起跳转请求
                        progress_tracker_seek(new_pos);
                        seek_audio(new_pos);
                    }
                    break;
                case '.':
                    // 进度加 5 秒 - 基于进度跟踪器的准确位置
                    if (g_play_state != PLAY_STATE_STOPPED && g_total_duration > 0) {
                        // 使用 progress_tracker 获取准确的当前播放位置
                        int current_pos = progress_tracker_get_position_seconds();
                        int new_pos = current_pos + 5;
                        if (new_pos > g_total_duration) new_pos = g_total_duration;
                        // 先同步进度跟踪器，再发起跳转请求
                        progress_tracker_seek(new_pos);
                        seek_audio(new_pos);
                    }
                    break;
                case ' ':
                    // 执行当前选中的控件功能
                    switch(g_current_control_idx) {
                        case 0: // 上一曲
                            prev_track();
                            break;
                        case 1: // 播放/暂停
                            if (g_play_state == PLAY_STATE_PLAYING) {
                                pause_audio();
                            } else if (g_play_state == PLAY_STATE_PAUSED) {
                                resume_audio();
                            } else {
                                // 停止状态，播放当前选中的歌曲
                                if (g_playlist.is_loaded && g_playlist.count > 0) {
                                    if (g_current_play_index >= 0) {
                                        play_audio(g_current_play_index);
                                    } else {
                                        play_audio(g_selected_index);
                                    }
                                }
                            }
                            break;
                        case 2: // 下一曲
                            next_track();
                            break;
                        case 3: // 停止
                            stop_audio();
                            break;
                        case 4: // 循环模式
                            toggle_loop_mode();
                            break;
                        case 5: // 进度条（占位，无操作）
                            break;
                    }
                    // 刷新UI以反映状态变化
                    render_playlist_content();
                    render_controls();
                    break;
            }
        } else {
            // === 列表区模式 ===
            if (g_playlist.is_loaded) {
                switch (ch) {
                    case KEY_UP:
                        if (g_selected_index > 0) {
                            g_selected_index--;
                            render_playlist_content();
                        }
                        break;
                    case KEY_DOWN:
                        if (g_selected_index < g_playlist.count - 1) {
                            g_selected_index++;
                            render_playlist_content();
                        }
                        break;
                    case ' ':
                    case 10: // Enter键
                        // 在列表模式下空格或Enter键播放选中的歌曲
                        play_audio(g_selected_index);
                        break;
                    case 'O':
                    case 'o':
                        // 允许重新选择文件夹
                        prompt_open_folder();
                        render_playlist_content();
                        break;
                }
            }
        }
        
        // 检查窗口大小变化 (可选优化)
        if (ch == KEY_RESIZE) {
            cleanup(); // 简单处理：退出重进或重新 init
            // 实际应调用 create_layout() 并重新渲染所有窗口
            return; 
        }
    }
}

/**
 * 清理ncurses资源
 * 释放窗口并结束ncurses模式
 */
void cleanup() {
    delwin(win_playlist);
    delwin(win_controls);
    delwin(win_lyrics);
    endwin(); // 结束 ncurses 模式
}