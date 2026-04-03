#include "../include/defs.h"
#include "../include/lyrics.h"    // 新增：歌词模块
#include "../include/menu_views.h" // 新增：菜单视图模块
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

// 歌词光标操作模式全局变量
int g_lyric_cursor_mode = 0;
int g_lyric_cursor_index = -1;

/**
 * 初始化ncurses环境
 * 设置本地化、终端模式和颜色对
 */
void init_ncurses() {
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    clear();

    if (has_colors()) {
        start_color();
        init_pair(COLOR_PAIR_BORDER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PAIR_PLAYLIST, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_CONTROLS, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_LYRICS, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_PAIR_SIDEBAR, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PAIR_HIGHLIGHT, COLOR_BLACK, COLOR_WHITE);
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
    // 初始化视图状态（仅在第一次调用时）
    static int initialized = 0;
    if (!initialized) {
        g_current_view = VIEW_MAIN;
        g_menu_selected_idx = 0;
        initialized = 1;
    }

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // 边界检查：确保最小尺寸，防止负数尺寸导致崩溃
    if (max_y < 8) max_y = 8;
    if (max_x < 20) max_x = 20;

    int lyrics_width = max_x / 3; // 歌词栏占宽度的 1/3
    if (lyrics_width < 10) lyrics_width = 10;
    int main_width = max_x - lyrics_width;
    if (main_width < 10) main_width = 10;

    // 计算高度比例 (5:2)，预留边框空间和底部提示条（预留1行给菜单提示条
    int total_inner_height = max_y - 5; // 上下留边距 + 底部提示条预留1行
    if (total_inner_height < 3) total_inner_height = 3;
    int playlist_height = (total_inner_height * 5) / 7;
    if (playlist_height < 1) playlist_height = 1;
    int controls_height = total_inner_height - playlist_height;
    if (controls_height < 2) controls_height = 2;

    // 1. 创建播放列表窗口 (左上)
    win_playlist = newwin(playlist_height, main_width, 1, 1);
    box(win_playlist, 0, 0);
    mvwprintw(win_playlist, 0, 2, " Play List ");
    wbkgd(win_playlist, COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    wrefresh(win_playlist);

    // 2. 创建控制栏窗口 (左下)
    win_controls = newwin(controls_height, main_width, 1 + playlist_height, 1);
    box(win_controls, 0, 0);
    const char *focus_hint = g_control_focus ? "[Ctrl Focus]" : "[List Focus]";
    const char *lyric_hint = g_lyric_cursor_mode ? "[D:ExitLyric]" : "[D:LyricEdit]";
    mvwprintw(win_controls, 0, 2, " Controls [Space:Act] [C:FocusCtrl] [L:FocusList] %s %s", focus_hint, lyric_hint);
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));
    
    wrefresh(win_controls);

    // 3. 创建歌词侧栏窗口 (右侧) - 高度减1为底部提示条预留空间
    int lyrics_height = max_y - 3;
    if (lyrics_height < 3) lyrics_height = 3;
    win_lyrics = newwin(lyrics_height, lyrics_width, 1, 1 + main_width);
    box(win_lyrics, 0, 0);
    mvwprintw(win_lyrics, 0, 2, " Lyrics ");
    wbkgd(win_lyrics, COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    mvwprintw(win_lyrics, 2, 2, "No lyrics loaded.");
    wrefresh(win_lyrics);

    // --- 新增：绘制分隔线 ---

    // 绘制左侧区域与右侧歌词区之间的垂直分隔线
    // 起点：(1, 1 + main_width), 长度：max_y - 3（给底部提示条预留空间）
    int vline_len = max_y - 3;
    if (vline_len < 1) vline_len = 1;
    mvvline(1, 1 + main_width, ACS_VLINE, vline_len);

    // 绘制播放列表与控制栏之间的水平分隔线
    // 起点：(1 + playlist_height, 1), 长度：main_width
    int hline_len = main_width;
    if (hline_len < 1) hline_len = 1;
    mvhline(1 + playlist_height, 1, ACS_HLINE, hline_len);
    
    // 绘制交叉点字符，使分隔线连接更自然
    mvaddch(1 + playlist_height, 1 + main_width, ACS_PLUS);

    // 刷新标准屏以显示分隔线
    refresh();
    
    // 渲染底部菜单栏提示条
    render_menu_hint_bar();
}

/**
 * 渲染播放列表内容
 * 包括歌曲列表和底部状态栏
 */
void render_playlist_content() {
    // 空指针检查：避免win_playlist未初始化时崩溃
    if (!win_playlist) {
        return;
    }
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
    // 检查窗口是否有效
    if (!win_controls) {
        return;
    }
    
    werase(win_controls);
    box(win_controls, 0, 0);
    
    const char *focus_hint = g_control_focus ? "[Ctrl Focus]" : "[List Focus]";
    const char *lyric_hint = g_lyric_cursor_mode ? "[D:ExitLyric]" : "[D:LyricEdit]";
    mvwprintw(win_controls, 0, 2, " Controls [Space:Act] [C:FocusCtrl] [L:FocusList] %s %s", focus_hint, lyric_hint);
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));

    int h, w;
    getmaxyx(win_controls, h, w);
    
    // 绘制进度条（在控件上方）
    if (g_play_state != PLAY_STATE_STOPPED && g_total_duration > 0) {
        int progress_row = h / 2 - 2; // 在控件上方两行
        
        // 窗口尺寸校验
        if (h >= 5 && w >= 20) {
            // 安全获取位置
            int current_pos = g_current_position;
            if (current_pos < 0) current_pos = 0;
            if (current_pos > g_total_duration) current_pos = g_total_duration;
            
            // 计算进度百分比
            int progress_percent = (current_pos * 100) / g_total_duration;
            if (progress_percent > 100) progress_percent = 100;
            
            // 格式化时间显示 - 限制时间值范围
            int current_min = current_pos / 60;
            int current_sec = current_pos % 60;
            int total_min = g_total_duration / 60;
            int total_sec = g_total_duration % 60;
            
            current_min %= 100;
            total_min %= 100;
            
            // 检查是否选中进度条
            int is_progress_selected = (g_current_control_idx == 5 && g_control_focus == 1);
            
            if (is_progress_selected) {
                wattron(win_controls, A_REVERSE | A_BOLD);
            }
            
            // 时间显示 - 固定格式确保不越界
            char time_str[32];
            snprintf(time_str, sizeof(time_str), "%02d:%02d / %02d:%02d", 
                     current_min, current_sec, total_min, total_sec);
            mvwprintw(win_controls, progress_row, 2, "%s", time_str);
            
            // 计算进度条安全宽度
            int time_width = 13;
            int percent_width = 4;
            int padding = 4;
            
            int progress_bar_width = w - time_width - percent_width - padding - 4;
            if (progress_bar_width < 10) progress_bar_width = 10;
            
            int progress_start_col = 2 + time_width + 1;
            
            // 绘制进度条边框
            mvwprintw(win_controls, progress_row, progress_start_col, "[");
            
            int filled_width = (progress_bar_width * progress_percent) / 100;
            if (filled_width > progress_bar_width) filled_width = progress_bar_width;
            
            // 使用循环绘制，避免格式化字符串溢出
            for (int i = 0; i < progress_bar_width && (progress_start_col + 1 + i) < w - 2; i++) {
                char c = '-';
                if (i < filled_width) c = '=';
                else if (i == filled_width && progress_percent < 100) c = '>';
                
                mvwaddch(win_controls, progress_row, progress_start_col + 1 + i, c);
            }
            
            mvwprintw(win_controls, progress_row, progress_start_col + 1 + progress_bar_width, "]");
            mvwprintw(win_controls, progress_row, progress_start_col + 2 + progress_bar_width, 
                      "%d%%", progress_percent);
            
            // 强制恢复边框
            mvwaddch(win_controls, progress_row, 0, ACS_VLINE);
            mvwaddch(win_controls, progress_row, w - 1, ACS_VLINE);
            
            if (is_progress_selected) {
                wattroff(win_controls, A_REVERSE | A_BOLD);
            }
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
    // 前置条件检查
    if (g_play_state == PLAY_STATE_STOPPED || g_total_duration <= 0 || !win_controls || g_current_view != VIEW_MAIN) {
        return;
    }
    
    int h, w;
    getmaxyx(win_controls, h, w);
    
    // 窗口尺寸校验
    if (h < 5 || w < 20) return;  // 最小有效尺寸
    
    // 安全获取位置
    int current_pos = g_current_position;
    if (current_pos < 0) current_pos = 0;
    if (current_pos > g_total_duration) current_pos = g_total_duration;
    
    // 计算进度百分比
    int progress_percent = (current_pos * 100) / g_total_duration;
    if (progress_percent > 100) progress_percent = 100;
    
    // 格式化时间显示 - 限制时间值范围，防止格式化溢出
    int current_min = current_pos / 60;
    int current_sec = current_pos % 60;
    int total_min = g_total_duration / 60;
    int total_sec = g_total_duration % 60;
    
    current_min %= 100;  // 限制最大显示 99:59
    total_min %= 100;
    
    // 检查是否选中进度条控件
    int is_progress_selected = (g_current_control_idx == 5 && g_control_focus == 1);
    
    int progress_row = h / 2 - 2;
    if (progress_row < 1 || progress_row >= h - 1) return;
    
    // 安全清除行 - 保留边框
    wmove(win_controls, progress_row, 1);
    for (int i = 1; i < w - 1 && i < 512; i++) {  // 限制最大清除宽度
        waddch(win_controls, ' ');
    }
    
    if (is_progress_selected) {
        wattron(win_controls, A_REVERSE | A_BOLD);
    }
    
    // 时间显示 - 固定格式确保不越界
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d / %02d:%02d", 
             current_min, current_sec, total_min, total_sec);
    mvwprintw(win_controls, progress_row, 2, "%s", time_str);
    
    // 计算进度条安全宽度
    int time_width = 13;  // "MM:SS / MM:SS" 的宽度
    int percent_width = 4;  // "100%" 的宽度
    int padding = 4;        // 左右括号和空格
    
    int progress_bar_width = w - time_width - percent_width - padding - 4;
    if (progress_bar_width < 10) progress_bar_width = 10;  // 最小进度条宽度
    
    int progress_start_col = 2 + time_width + 1;
    
    // 绘制进度条边框
    mvwprintw(win_controls, progress_row, progress_start_col, "[");
    
    int filled_width = (progress_bar_width * progress_percent) / 100;
    if (filled_width > progress_bar_width) filled_width = progress_bar_width;
    
    // 使用循环绘制，避免格式化字符串溢出
    for (int i = 0; i < progress_bar_width && (progress_start_col + 1 + i) < w - 2; i++) {
        char c = '-';
        if (i < filled_width) c = '=';
        else if (i == filled_width && progress_percent < 100) c = '>';
        
        mvwaddch(win_controls, progress_row, progress_start_col + 1 + i, c);
    }
    
    mvwprintw(win_controls, progress_row, progress_start_col + 1 + progress_bar_width, "]");
    mvwprintw(win_controls, progress_row, progress_start_col + 2 + progress_bar_width, 
              "%d%%", progress_percent);
    
    if (is_progress_selected) {
        wattroff(win_controls, A_REVERSE | A_BOLD);
    }
    
    // 恢复边框
    mvwaddch(win_controls, progress_row, 0, ACS_VLINE);
    mvwaddch(win_controls, progress_row, w - 1, ACS_VLINE);
    
    wrefresh(win_controls);
    
    // 更新歌词显示
    update_lyrics_display();
}

/**
 * 更新控制栏状态信息
 * 在控制栏底部显示临时消息
 */
void update_controls_status(const char *msg) {
    // 空指针检查：避免win_controls未初始化时崩溃
    if (!win_controls) {
        return;
    }
    int h, w;
    getmaxyx(win_controls, h, w);
    mvwprintw(win_controls, h-1, 2, "%s", msg);
    wclrtoeol(win_controls);
    wrefresh(win_controls);
    // 移除阻塞的sleep调用，让状态信息自然显示
}

void prompt_open_folder() {
    // 空指针检查：避免win_controls未初始化时崩溃
    if (!win_controls) {
        return;
    }
    echo();
    curs_set(1);
    
    int max_y, max_x;
    getmaxyx(win_controls, max_y, max_x);
    
    mvwprintw(win_controls, 4, 2, "Enter folder path: ");
    wclrtoeol(win_controls);
    wrefresh(win_controls);
    
    // 获取提示文本后的起始光标位置
    int prompt_len = 18; // "Enter folder path: " 的长度
    int start_x = 2 + prompt_len;
    
    char input_path[MAX_PATH_LEN];
    memset(input_path, 0, sizeof(input_path));
    int pos = 0;
    int ch;
    
    flushinp();
    
    // BUGFIX 2026.03.26: 手动逐字符读取，正确处理 UTF-8 多字节中文输入
    // 使用 wgetnstr 无法正确处理 UTF-8 中文输入，改为手动读取
    // BUGFIX 2026.03.29: 忽略 ERR，防止超时自动插入space字符
    while ((ch = getch()) != '\n' && ch != KEY_ENTER && pos < MAX_PATH_LEN - 1) {
        if (ch == ERR) {
            continue;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            // 处理 Backspace 删除
            if (pos > 0) {
                int cx = getcurx(win_controls);
                int cy = getcury(win_controls);
                
                // BUGFIX 2026.03.26: 正确处理 UTF-8 多字节字符删除
                unsigned char last_c = (unsigned char)input_path[pos - 1];
                int bytes_to_remove = 1;
                if (last_c >= 0x80) {
                    // 多字节 UTF-8 字符，需要回退到序列开头
                    if ((last_c & 0xE0) == 0xC0) bytes_to_remove = 2;
                    else if ((last_c & 0xF0) == 0xE0) bytes_to_remove = 3;
                    else if ((last_c & 0xF8) == 0xF0) bytes_to_remove = 4;
                    else if ((last_c & 0xC0) == 0x80) {
                        //  continuation byte，继续向前找开头
                        bytes_to_remove = 2;
                        while (pos - bytes_to_remove >= 0 && 
                               (unsigned char)input_path[pos - bytes_to_remove] >= 0x80 && 
                               (unsigned char)input_path[pos - bytes_to_remove] < 0xC0) {
                            bytes_to_remove++;
                        }
                    }
                    if (bytes_to_remove > pos) bytes_to_remove = pos;
                    pos -= bytes_to_remove;
                    
                    // 中文字符占两列，光标左移两格
                    if ((last_c & 0xF0) == 0xE0 || (last_c & 0xE0) == 0xC0) {
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
                wrefresh(win_controls);
            }
        } else if (ch >= 0x20 && ch <= 0x7E) {
            // ASCII 字符
            input_path[pos++] = (char)ch;
            waddch(win_controls, ch);
            wrefresh(win_controls);
        } else if ((ch & 0xC0) == 0x80 || ch >= 0x80) {
            // UTF-8 多字节字符的后续字节
            if (pos < MAX_PATH_LEN - 1) {
                input_path[pos++] = (char)ch;
                // 只有字节序列开头才需要移动光标
                if ((ch & 0xE0) == 0xC0 || (ch & 0xF0) == 0xE0) {
                    waddch(win_controls, ch);
                    // 中文字符占两列，但终端会自动处理光标移动
                    wrefresh(win_controls);
                }
            }
        } else {
            // 其他可打印字符
            input_path[pos++] = (char)ch;
            waddch(win_controls, ch);
            wrefresh(win_controls);
        }
    }
    
    input_path[pos] = '\0';
    flushinp();
    
    noecho();
    curs_set(0);
    
    mvwprintw(win_controls, 4, 2, "                    "); 
    wclrtoeol(win_controls);
    wrefresh(win_controls);

    if (strlen(input_path) > 0) {
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
        
        struct stat s;
        if (stat(expanded_path, &s) == 0 && S_ISDIR(s.st_mode)) {
            int count = load_playlist(expanded_path);
            if (count > 0) {
                add_dir_history_entry(expanded_path);
                
                if (g_app_config.remember_last_path) {
                    strncpy(g_app_config.last_opened_path, expanded_path, MAX_PATH_LEN - 1);
                    save_config();
                }
                
                update_controls_status("Folder loaded successfully");
                g_selected_index = 0;
                render_playlist_content();
            } else {
                update_controls_status("No audio files found");
                g_playlist.is_loaded = 0;
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
            update_lyrics_display();
        }
        
        // 如果用户没有按键，继续循环以允许进度条和歌词更新
        if (ch == ERR) {
            continue;
        }
        
        // 新增：处理功能键（F1-F7）
        if (ch >= KEY_F(1) && ch <= KEY_F(7)) {
            handle_function_keys(ch);
            continue;
        }
        
        if (ch == 'q' || ch == 'Q') {
            break;
        }
        
        // 新增：如果在菜单视图模式下，优先处理菜单输入
        if (g_current_view != VIEW_MAIN) {
            handle_menu_input(ch);
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

        // 歌词光标模式切换 (D键)
        if (ch == 'D' || ch == 'd') {
            // 严格遵循需求：按下大写D切换
            if (ch == 'D' && g_current_view == VIEW_MAIN) {
                pthread_mutex_lock(&g_lyrics.lock);
                if (g_lyrics.has_lyrics && g_lyrics.count > 0 && g_lyrics.current_index >= 0) {
                    g_lyric_cursor_mode = !g_lyric_cursor_mode;
                    if (g_lyric_cursor_mode) {
                        // 激活时，初始化光标位置为当前播放歌词行
                        g_lyrics.cursor_index = g_lyrics.current_index;
                        g_lyric_cursor_index = g_lyrics.cursor_index;
                        update_controls_status("Lyric edit mode activated");
                    } else {
                        update_controls_status("Lyric edit mode deactivated");
                    }
                    pthread_mutex_unlock(&g_lyrics.lock);
                    render_controls();
                    render_lyrics();
                    continue;
                } else {
                    pthread_mutex_unlock(&g_lyrics.lock);
                    if (!g_lyrics.has_lyrics) {
                        update_controls_status("No lyrics available for editing");
                    }
                    continue;
                }
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
                    // 后退 3 秒 - 增加前置条件检查
                    if (g_play_state != PLAY_STATE_STOPPED && 
                        g_total_duration > 0 && 
                        progress_tracker_is_ready()) {
                        
                        int current_pos = progress_tracker_get_position_seconds();
                        double new_pos = (double)(current_pos - 3);
                        if (new_pos < 0) new_pos = 0;
                        
                        seek_audio(new_pos);
                    }
                    break;

                case '.':
                    // 前进 3 秒 - 增加前置条件检查
                    if (g_play_state != PLAY_STATE_STOPPED && 
                        g_total_duration > 0 && 
                        progress_tracker_is_ready()) {
                        
                        int current_pos = progress_tracker_get_position_seconds();
                        double new_pos = (double)(current_pos + 3);
                        if (new_pos > g_total_duration) new_pos = g_total_duration;
                        
                        seek_audio(new_pos);
                    }
                    break;
                case ' ':
                    // 执行当前选中的控件功能
                    // 使用局部变量保存当前状态，避免在函数调用期间状态被其他线程改变
                    switch(g_current_control_idx) {
                        case 0: // 上一曲
                            prev_track();
                            break;
                        case 1: // 播放/暂停
                            {
                                // 捕获当前状态快照，确保一致性检查
                                PlayState current_state = g_play_state;
                                int is_thread_running = g_play_thread_running;
                                
                                if (current_state == PLAY_STATE_PLAYING && is_thread_running) {
                                    pause_audio();
                                } else if (current_state == PLAY_STATE_PAUSED && is_thread_running) {
                                    resume_audio();
                                } else if (current_state == PLAY_STATE_STOPPED) {
                                    // 停止状态，播放当前选中的歌曲
                                    if (g_playlist.is_loaded && g_playlist.count > 0) {
                                        int target_index = (g_current_play_index >= 0) ? 
                                                           g_current_play_index : g_selected_index;
                                        if (target_index >= 0 && target_index < g_playlist.count) {
                                            play_audio(target_index);
                                        }
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
                    // 刷新 UI 以反映状态变化
                    render_playlist_content();
                    render_controls();
                    break;
            }
        } else {
            if (g_playlist.is_loaded) {
                switch (ch) {
                    case KEY_UP:
                        if (g_lyric_cursor_mode && g_lyrics.has_lyrics) {
                            // 歌词编辑模式下，方向键控制歌词光标
                            pthread_mutex_lock(&g_lyrics.lock);
                            if (g_lyrics.cursor_index > 0) {
                                g_lyrics.cursor_index--;
                                g_lyric_cursor_index = g_lyrics.cursor_index;
                                double target_timestamp = g_lyrics.lines[g_lyrics.cursor_index].timestamp;
                                pthread_mutex_unlock(&g_lyrics.lock);
                                render_lyrics();
                                if (g_play_state != PLAY_STATE_STOPPED && progress_tracker_is_ready()) {
                                    seek_audio(target_timestamp);
                                }
                            } else {
                                pthread_mutex_unlock(&g_lyrics.lock);
                            }
                        } else {
                            // 正常模式下，方向键控制播放列表选中项
                            if (g_selected_index > 0) {
                                g_selected_index--;
                                render_playlist_content();
                            }
                        }
                        break;
                    case KEY_DOWN:
                        if (g_lyric_cursor_mode && g_lyrics.has_lyrics) {
                            // 歌词编辑模式下，方向键控制歌词光标
                            pthread_mutex_lock(&g_lyrics.lock);
                            if (g_lyrics.cursor_index < g_lyrics.count - 1) {
                                g_lyrics.cursor_index++;
                                g_lyric_cursor_index = g_lyrics.cursor_index;
                                double target_timestamp = g_lyrics.lines[g_lyrics.cursor_index].timestamp;
                                pthread_mutex_unlock(&g_lyrics.lock);
                                render_lyrics();
                                if (g_play_state != PLAY_STATE_STOPPED && progress_tracker_is_ready()) {
                                    seek_audio(target_timestamp);
                                }
                            } else {
                                pthread_mutex_unlock(&g_lyrics.lock);
                            }
                        } else {
                            // 正常模式下，方向键控制播放列表选中项
                            if (g_selected_index < g_playlist.count - 1) {
                                g_selected_index++;
                                render_playlist_content();
                            }
                        }
                        break;
                    case ' ':
                    case 10:
                        play_audio(g_selected_index);
                        break;
                    case 'O':
                    case 'o':
                        prompt_open_folder();
                        render_playlist_content();
                        break;
                    case 'f':
                    case 'F':
                        if (g_playlist.count > 0) {
                            Track *t = &g_playlist.tracks[g_selected_index];
                            int result = add_to_favorites(t);
                            if (result == 0) {
                                update_controls_status("Added to favorites!");
                            } else {
                                update_controls_status("Already in favorites or full");
                            }
                        }
                        break;
                    case 'a':
                    case 'A':
                        if (g_playlist.count > 0 && g_playlist_manager.count > 0) {
                            Track *t = &g_playlist.tracks[g_selected_index];

                            int max_y, max_x;
                            getmaxyx(stdscr, max_y, max_x);

                            WINDOW *win_win = newwin(max_y - 4, max_x - 4, 2, 2);
                            box(win_win, 0, 0);
                            mvwprintw(win_win, 0, 2, " Select Playlist ");
                            wbkgd(win_win, COLOR_PAIR(COLOR_PAIR_PLAYLIST));

                            int start_y = 2;
                            int visible_lines = max_y - 8;
                            int selected = 0;
                            int offset = 0;

                             while (1) {
                                for (int i = 0; i < visible_lines && (offset + i) < g_playlist_manager.count; i++) {
                                    int idx = offset + i;
                                    UserPlaylist *pl = &g_playlist_manager.playlists[idx];
                                    if (idx == selected) {
                                        wattron(win_win, A_REVERSE);
                                        mvwprintw(win_win, start_y + i, 2, " %-30s (%d tracks)", pl->name, pl->track_count);
                                        wattroff(win_win, A_REVERSE);
                                    } else {
                                        mvwprintw(win_win, start_y + i, 2, " %-30s (%d tracks)", pl->name, pl->track_count);
                                    }
                                }

                                mvwprintw(win_win, max_y - 6, 2, "↑/↓: Navigate | ENTER: Select | ESC: Cancel");
                                wrefresh(win_win);

                                int c = wgetch(win_win);
                                if (c == 27) {
                                    break;
                                } else if (c == KEY_UP) {
                                    if (selected > 0) {
                                        selected--;
                                        if (selected < offset) {
                                            offset = selected;
                                        }
                                    }
                                } else if (c == KEY_DOWN) {
                                    if (selected < g_playlist_manager.count - 1) {
                                        selected++;
                                        if (selected >= offset + visible_lines) {
                                            offset = selected - visible_lines + 1;
                                        }
                                    }
                                } else if (c == 10 || c == ' ') {
                                    int result = add_track_to_playlist(selected, t);
                                      if (result == 0) {
                                          update_controls_status("Track added to playlist!");
                                      } else if (result == -3) {
                                          update_controls_status("Playlist is full");
                                      } else {
                                          update_controls_status("Track already in playlist");
                                      }
                                    break;
                                }
                             }

                             delwin(win_win);
                             create_layout();
                             render_playlist_content();
                             render_controls();
                             render_lyrics();
                        } else if (g_playlist_manager.count == 0) {
                            update_controls_status("No playlists. Create one in F4 menu.");
                        }
                        break;
                }
            }
        }
        
        if (ch == KEY_RESIZE) {
            // 终端窗口大小改变，重新创建布局
            // 先删除旧窗口再重新创建，避免内存泄漏和显示错乱
            delwin(win_playlist);
            delwin(win_controls);
            delwin(win_lyrics);
            clear();
            create_layout();
            render_playlist_content();
            render_controls();
            render_lyrics();
            continue;
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