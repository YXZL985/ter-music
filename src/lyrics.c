#include "../include/lyrics.h"
#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include <libgen.h>
#include <time.h>

// 全局歌词变量实例
Lyrics g_lyrics = {
    .count = 0,
    .current_index = -1,
    .highlight_count = 0,
    .has_lyrics = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// 外部窗口变量声明
extern WINDOW *win_lyrics;

/**
 * 解析 LRC 时间戳字符串
 * 格式：[mm:ss.xx]
 * @param time_str 时间戳字符串（不包含方括号）
 * @return 时间戳（秒）
 */
static int parse_timestamp(const char *time_str) {
    int mm, ss, xx;
    if (sscanf(time_str, "%d:%d.%d", &mm, &ss, &xx) == 3) {
        return mm * 60 + ss;  // 忽略毫秒，只取整秒
    }
    return -1;
}

/**
 * 解析单行 LRC 内容
 * @param line LRC 文件的一行
 * @param timestamp 输出：时间戳（秒）
 * @param text 输出：歌词文本
 * @return 1 表示成功，0 表示失败
 */
static int parse_lrc_line(const char *line, int *timestamp, char *text) {
    if (!line || !timestamp || !text) {
        return 0;
    }
    
    // 跳过空行
    if (line[0] == '\0' || line[0] == '\n') {
        return 0;
    }
    
    // 查找第一个时间标签 [mm:ss.xx]
    const char *start = strchr(line, '[');
    if (!start) {
        return 0;
    }
    
    const char *end = strchr(start, ']');
    if (!end) {
        return 0;
    }
    
    // 提取时间戳字符串（不包含方括号）
    char time_str[16];
    int len = end - start - 1;
    if (len <= 0 || len >= sizeof(time_str)) {
        return 0;
    }
    strncpy(time_str, start + 1, len);
    time_str[len] = '\0';
    
    // 解析时间戳
    int ts = parse_timestamp(time_str);
    if (ts < 0) {
        return 0;
    }
    *timestamp = ts;
    
    // 提取歌词文本（跳过所有时间标签）
    const char *text_start = end + 1;
    while (*text_start == '[') {
        // 跳过连续的时间标签
        const char *next_end = strchr(text_start, ']');
        if (!next_end) {
            break;
        }
        text_start = next_end + 1;
    }
    
    // 去除前导空格
    while (*text_start == ' ' || *text_start == '\t') {
        text_start++;
    }
    
    // 复制歌词文本
    strncpy(text, text_start, MAX_LYRIC_TEXT_LEN - 1);
    text[MAX_LYRIC_TEXT_LEN - 1] = '\0';
    
    // 去除末尾换行符和空格
    len = strlen(text);
    while (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r' || text[len-1] == ' ')) {
        text[--len] = '\0';
    }
    
    // 如果歌词文本为空，使用占位符
    if (len == 0) {
        strcpy(text, "(Instrumental)");
    }
    
    return 1;
}

/**
 * 根据时间戳查找歌词索引
 * @param timestamp_seconds 时间戳（秒）
 * @return 歌词索引，-1 表示未找到
 */
static int find_lyric_index(int timestamp_seconds) {
    int i;
    int current_index = -1;
    
    // 找到最后一个 timestamp <= current_position 的行
    for (i = 0; i < g_lyrics.count; i++) {
        if (g_lyrics.lines[i].timestamp <= timestamp_seconds) {
            current_index = i;
        } else {
            break;
        }
    }
    
    return current_index;
}

/**
 * 渲染单行歌词
 * @param row 行号（窗口内坐标）
 * @param text 歌词文本
 * @param is_highlighted 是否高亮
 * @param show_marker 是否显示 ">" 标记
 */
static void render_lyric_line(int row, const char *text, int is_highlighted, int show_marker) {
    int h, w;
    getmaxyx(win_lyrics, h, w);

    // 检查窗口尺寸是否有效
    if (h <= 2 || w <= 4) {
        return;
    }

    // 检查行号是否有效
    if (row < 1 || row >= h - 1) {
        return;
    }

    // 计算最大可用宽度（减去边框和缩进）
    int max_width = w - 4;
    if (max_width <= 0) {
        return;
    }
    if (show_marker) {
        max_width -= 2;  // 为 "> " 预留空间
        if (max_width <= 0) {
            return;
        }
    }
    
    // 计算文本实际宽度
    int text_width = utf8_str_width(text);
    
    // 确定起始列偏移（用于水平滚动）
    static time_t last_scroll_time = 0;
    static int scroll_offset = 0;
    time_t now = time(NULL);
    
    // 只对高亮且超长的文本启用滚动
    int use_scrolling = is_highlighted && text_width > max_width;
    char display_text[MAX_LYRIC_TEXT_LEN];
    
    if (use_scrolling) {
        // 每 1 秒更新一次偏移量
        if (now != last_scroll_time) {
            scroll_offset++;
            // 当完全滚出后重置
            if (scroll_offset > text_width - max_width + 3) {  // +3 为了显示省略号
                scroll_offset = 0;
            }
            last_scroll_time = now;
        }
        
        // 使用偏移量截取文本
        utf8_str_substring(display_text, text, scroll_offset, max_width);
    } else {
        // 非高亮或文本不超长，使用普通截断
        utf8_str_truncate(display_text, text, max_width);
    }
    
    // 应用高亮并显示
    if (is_highlighted) {
        wattron(win_lyrics, A_REVERSE);
        if (show_marker) {
            mvwprintw(win_lyrics, row, 2, "> %s", display_text);
        } else {
            // 第二行高亮，不显示标记，缩进对齐
            mvwprintw(win_lyrics, row, 3, "%s", display_text);
        }
        wattroff(win_lyrics, A_REVERSE);
    } else {
        // 普通行，使用默认颜色对
        mvwprintw(win_lyrics, row, 3, "%s", display_text);
    }
}

void load_lyrics(const char *audio_path) {
    if (!audio_path) {
        return;
    }
    
    // 构造 LRC 文件路径
    char lrc_path[MAX_PATH_LEN];
    strncpy(lrc_path, audio_path, MAX_PATH_LEN - 1);
    lrc_path[MAX_PATH_LEN - 1] = '\0';
    
    // 替换扩展名为 .lrc
    char *ext = strrchr(lrc_path, '.');
    if (ext) {
        strcpy(ext, ".lrc");
    } else {
        strcat(lrc_path, ".lrc");
    }
    
    // 打开 LRC 文件
    FILE *fp = fopen(lrc_path, "r");
    if (!fp) {
        // 文件不存在或无法打开
        pthread_mutex_lock(&g_lyrics.lock);
        g_lyrics.has_lyrics = 0;
        g_lyrics.count = 0;
        g_lyrics.current_index = -1;
        g_lyrics.highlight_count = 0;
        pthread_mutex_unlock(&g_lyrics.lock);
        return;
    }
    
    // 临时缓冲区存储解析后的歌词
    LyricLine temp_lines[MAX_LYRIC_LINES];
    int count = 0;
    
    char line[MAX_LYRIC_TEXT_LEN + 32];  // 额外空间用于时间标签
    while (fgets(line, sizeof(line), fp) && count < MAX_LYRIC_LINES) {
        int timestamp;
        char text[MAX_LYRIC_TEXT_LEN];
        
        if (parse_lrc_line(line, &timestamp, text)) {
            temp_lines[count].timestamp = timestamp;
            strncpy(temp_lines[count].text, text, MAX_LYRIC_TEXT_LEN - 1);
            temp_lines[count].text[MAX_LYRIC_TEXT_LEN - 1] = '\0';
            count++;
        }
    }
    
    fclose(fp);
    
    // 如果没有解析到任何歌词
    if (count == 0) {
        pthread_mutex_lock(&g_lyrics.lock);
        g_lyrics.has_lyrics = 0;
        g_lyrics.count = 0;
        g_lyrics.current_index = -1;
        g_lyrics.highlight_count = 0;
        pthread_mutex_unlock(&g_lyrics.lock);
        return;
    }
    
    // 锁定并更新全局歌词数据
    pthread_mutex_lock(&g_lyrics.lock);
    g_lyrics.count = count;
    memcpy(g_lyrics.lines, temp_lines, sizeof(LyricLine) * count);
    g_lyrics.has_lyrics = 1;
    g_lyrics.current_index = -1;
    g_lyrics.highlight_count = 0;
    pthread_mutex_unlock(&g_lyrics.lock);
}

void clear_lyrics(void) {
    pthread_mutex_lock(&g_lyrics.lock);
    g_lyrics.count = 0;
    g_lyrics.current_index = -1;
    g_lyrics.highlight_count = 0;
    g_lyrics.has_lyrics = 0;
    pthread_mutex_unlock(&g_lyrics.lock);
}

void update_lyrics_display(void) {
    // 只在播放状态且主界面下更新
    if (g_play_state == PLAY_STATE_STOPPED || g_current_view != VIEW_MAIN) {
        return;
    }
    
    pthread_mutex_lock(&g_lyrics.lock);
    
    if (!g_lyrics.has_lyrics || g_lyrics.count == 0) {
        pthread_mutex_unlock(&g_lyrics.lock);
        return;
    }
    
    // 根据当前播放位置找到对应的歌词行
    int current_pos = g_current_position;
    int new_index = -1;
    int new_highlight_count = 0;
    
    // 遍历歌词数组，找到最后一个 timestamp <= current_position 的行
    for (int i = 0; i < g_lyrics.count; i++) {
        if (g_lyrics.lines[i].timestamp <= current_pos) {
            new_index = i;
            new_highlight_count = 1;
            
            // 检查下一行是否有相同时间戳，最多高亮两行
            if (i + 1 < g_lyrics.count && 
                g_lyrics.lines[i + 1].timestamp == g_lyrics.lines[i].timestamp) {
                new_highlight_count = 2;
            }
        } else {
            break;
        }
    }
    
    // 只有当索引变化时才更新
    if (new_index != g_lyrics.current_index || 
        new_highlight_count != g_lyrics.highlight_count) {
        g_lyrics.current_index = new_index;
        g_lyrics.highlight_count = new_highlight_count;
    }
    
    pthread_mutex_unlock(&g_lyrics.lock);
    
    // 在锁外调用渲染函数
    render_lyrics();
}

void render_lyrics(void) {
    if (!win_lyrics) {
        return;
    }
    
    int h, w;
    getmaxyx(win_lyrics, h, w);
    
    // 清空窗口
    werase(win_lyrics);
    
    // 重绘边框和标题
    box(win_lyrics, 0, 0);
    mvwprintw(win_lyrics, 0, 2, " Lyrics ");
    wbkgd(win_lyrics, COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    pthread_mutex_lock(&g_lyrics.lock);
    
    if (!g_lyrics.has_lyrics || g_lyrics.count == 0) {
        // 没有歌词时显示提示
        mvwprintw(win_lyrics, h / 2 - 1, 2, "No lyrics available");
        pthread_mutex_unlock(&g_lyrics.lock);
        wrefresh(win_lyrics);
        return;
    }
    
    // 如果有歌词但没有当前索引（刚开始播放）
    if (g_lyrics.current_index < 0) {
        mvwprintw(win_lyrics, h / 2 - 1, 2, "Playing...");
        pthread_mutex_unlock(&g_lyrics.lock);
        wrefresh(win_lyrics);
        return;
    }
    
    // 计算可视区域大小
    int visible_lines = h - 4;  // 减去边框和标题行
    if (visible_lines <= 0) {
        pthread_mutex_unlock(&g_lyrics.lock);
        wrefresh(win_lyrics);
        return;
    }
    
    // 垂直居中策略：使当前高亮行居中显示
    int start_idx = g_lyrics.current_index - (visible_lines / 2);
    if (start_idx < 0) start_idx = 0;
    if (start_idx + visible_lines > g_lyrics.count) {
        start_idx = g_lyrics.count - visible_lines;
    }
    if (start_idx < 0) start_idx = 0;  // 歌词行数不足时从头显示
    
    // 渲染可视区域内的歌词行
    for (int i = 0; i < visible_lines && (start_idx + i) < g_lyrics.count; i++) {
        int lyric_idx = start_idx + i;
        int row = i + 1;  // 窗口内行号（从 1 开始，避开边框）
        
        int is_highlighted = (lyric_idx >= g_lyrics.current_index && 
                              lyric_idx < g_lyrics.current_index + g_lyrics.highlight_count);
        int show_marker = (lyric_idx == g_lyrics.current_index);
        
        render_lyric_line(row, g_lyrics.lines[lyric_idx].text, 
                         is_highlighted, show_marker);
    }
    
    pthread_mutex_unlock(&g_lyrics.lock);
    wrefresh(win_lyrics);
}
