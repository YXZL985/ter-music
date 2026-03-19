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

// ALSA 头文件
#include <alsa/asoundlib.h>

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

// 音频输出相关全局变量
static snd_pcm_t *audio_handle = NULL;
static int16_t *audio_buffer = NULL;
static int buffer_size = 0;
static int buffer_pos = 0;
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;

// 全局变量：默认音频设备名称
static char g_default_audio_device[128] = "default";

// 最大音频缓冲区大小（约1秒的立体声音频）
#define MAX_AUDIO_BUFFER_SIZE (44100 * 2 * sizeof(int16_t))

// 定义最大路径长度和歌曲数量
#define MAX_PATH_LEN 512
#define MAX_TRACKS 1000
#define MAX_META_LEN 256

// 音频文件扩展名列表
const char *audio_extensions[] = {".mp3", ".flac", ".wav", ".ogg", ".m4a", NULL};

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

// 全局变量更新
WINDOW *win_playlist;
WINDOW *win_controls;
WINDOW *win_lyrics;
Playlist g_playlist = {0}; // 初始化全局播放列表
int g_selected_index = 0;  // 当前选中的歌曲索引

// 新增：控制区焦点状态
// 0: 列表模式 (List), 1: 控制模式 (Control)
int g_control_focus = 0; 
// 当前选中的控件索引 (0:上一曲，1:播放/暂停，2:下一曲，3:停止，4:循环)
int g_current_control_idx = 1;

// 播放相关全局变量
PlayState g_play_state = PLAY_STATE_STOPPED; // 当前播放状态
int g_current_play_index = -1; // 当前播放的歌曲索引
LoopMode g_loop_mode = LOOP_OFF; // 当前循环模式
pthread_t g_play_thread; // 播放线程
int g_play_thread_running = 0; // 播放线程运行状态
pthread_mutex_t g_play_mutex = PTHREAD_MUTEX_INITIALIZER; // 播放控制互斥锁 

// 定义控件数量
#define CONTROL_COUNT 5
const char *control_labels[] = {"<<", "Play/Pause", ">>", "Stop", "Loop"};

// 函数声明
void update_controls_status(const char *msg);
void cleanup();
void *play_audio_thread(void *arg);
void play_audio(int index);
void pause_audio();
void resume_audio();
void stop_audio();
void next_track();
void prev_track();
void toggle_loop_mode();
const char *get_loop_mode_str();
// 新增：UTF-8字符串按屏幕显示宽度安全截断
int utf8_str_truncate(char *dest, const char *src, int max_cols);


/**
 * 初始化 ncurses 环境
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
 * 创建和调整窗口布局
 * 布局策略:
 * 总高度 H, 总宽度 W
 * 左侧区域宽度：W -  lyrics_width
 * 播放列表高度：(H - 2) * 5 / 7  (减去边框占用，大致比例)
 * 控制栏高度：(H - 2) * 2 / 7
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
 * 检查文件是否为支持的音频格式
 */
int is_audio_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    for (int i = 0; audio_extensions[i] != NULL; i++) {
        if (strcasecmp(ext, audio_extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * 获取音频元数据
 * 注意：实际项目中应在此处调用 taglib 或其他库读取 ID3 标签
 * 此处若无真实标签，则默认标题为文件名，艺术家为 "Unknown Artist"
 */
void get_audio_metadata(const char *path, char *title, char *artist, char *album) {
    // 提取文件名作为默认标题
    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    
    // 去除扩展名
    char temp_title[MAX_META_LEN];
    utf8_str_truncate(temp_title, fname, MAX_META_LEN - 1);
    char *dot = strrchr(temp_title, '.');
    if (dot) *dot = '\0';

    // 初始化默认值
    utf8_str_truncate(title, temp_title, MAX_META_LEN - 1);
    utf8_str_truncate(artist, "Unknown Artist", MAX_META_LEN - 1);
    utf8_str_truncate(album, "Unknown Album", MAX_META_LEN - 1);
    
    // 尝试从文件名中提取元数据（格式：Artist - Title）
    char *dash_pos = strstr(temp_title, " - ");
    if (dash_pos) {
        *dash_pos = '\0';
        utf8_str_truncate(artist, temp_title, MAX_META_LEN - 1);
        utf8_str_truncate(title, dash_pos + 3, MAX_META_LEN - 1);
    }
    
    // TODO: 集成 taglib_c 示例 (需链接 -ltag_c)
    /*
    TagLib_File *file = taglib_file_new(path);
    if (file && taglib_file_is_valid(file)) {
        TagLib_Tag *tag = taglib_file_tag(file);
        if (tag) {
            const char *tag_title = taglib_tag_title(tag);
            const char *tag_artist = taglib_tag_artist(tag);
            const char *tag_album = taglib_tag_album(tag);
            
            if (tag_title && strlen(tag_title) > 0) {
                utf8_str_truncate(title, tag_title, MAX_META_LEN - 1);
            }
            if (tag_artist && strlen(tag_artist) > 0) {
                utf8_str_truncate(artist, tag_artist, MAX_META_LEN - 1);
            }
            if (tag_album && strlen(tag_album) > 0) {
                utf8_str_truncate(album, tag_album, MAX_META_LEN - 1);
            }
            taglib_tag_free_strings();
        }
        taglib_file_free(file);
    }
    */

}

/**
 * 加载播放列表
 */
int load_playlist(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        return -1;
    }

    g_playlist.count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL && g_playlist.count < MAX_TRACKS) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            char full_path[MAX_PATH_LEN];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            
            if (is_audio_file(entry->d_name)) {
                Track *t = &g_playlist.tracks[g_playlist.count];
                utf8_str_truncate(t->path, full_path, MAX_PATH_LEN - 1);
                
                // 读取元数据
                get_audio_metadata(full_path, t->title, t->artist, t->album);
                
                g_playlist.count++;
            }
        }
    }
    closedir(dir);
    
    utf8_str_truncate(g_playlist.folder_path, path, MAX_PATH_LEN - 1);
    g_playlist.is_loaded = (g_playlist.count > 0);
    
    return g_playlist.count;
}

/**
 * 绘制播放列表内容 (包含底部的状态提示)
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
        int visible_lines = content_height - 3; // 预留 3 行给底部状态栏
        
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
        int status_line = h - 4;
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
            
            // 计算可用宽度，显示更多元数据
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
            mvwprintw(win_playlist, status_line + 2, 2, "Title: %-*s Artist: %-*s Album: %-*s", 
                      title_width, truncated_title, artist_width, truncated_artist, album_width, truncated_album);
        } else {
             mvwprintw(win_playlist, status_line + 1, 2, "Status: %s | Track: --", status_msg);
             mvwprintw(win_playlist, status_line + 2, 2, "Title: -- Artist: -- Album: --");
        }
    }
    wrefresh(win_playlist);
}

/**
 * 渲染控制栏按钮
 */
void render_controls() {
    werase(win_controls);
    box(win_controls, 0, 0);
    
    const char *focus_hint = g_control_focus ? "[Ctrl Focus]" : "[List Focus]";
    mvwprintw(win_controls, 0, 2, " Controls [Space:Act] [C:FocusCtrl] [L:FocusList] %s", focus_hint);
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));

    int h, w;
    getmaxyx(win_controls, h, w);
    int row = h / 2; // 垂直居中
    
    // 计算按钮总宽度以便居中
    int total_len = 0;
    for(int i=0; i<CONTROL_COUNT; i++) {
        total_len += strlen(control_labels[i]) + 4; // 标签 + [ ] + 空格
    }
    int start_col = (w - total_len) / 2;
    if (start_col < 1) start_col = 1;

    int current_col = start_col;
    for (int i = 0; i < CONTROL_COUNT; i++) {
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
 * 处理用户输入路径
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
 * 主事件循环 (预留逻辑处理位置)
 */
void run_event_loop() {
    int ch;
    
    // 初始渲染
    render_playlist_content();
    render_controls(); // 初始绘制控件
    
    while ((ch = getch()) != 'q') {
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
                    }
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
 * 更新控制栏状态信息
 */
void update_controls_status(const char *msg) {
    int h, w;
    getmaxyx(win_controls, h, w);
    mvwprintw(win_controls, h-1, 2, "%s", msg);
    wclrtoeol(win_controls);
    wrefresh(win_controls);
    // 短暂显示后清除
    sleep(1);
    mvwprintw(win_controls, h-1, 2, "                    ");
    wclrtoeol(win_controls);
    wrefresh(win_controls);
}

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
 */
void toggle_loop_mode() {
    g_loop_mode = (g_loop_mode + 1) % 4;
    char msg[64];
    snprintf(msg, sizeof(msg), "Loop mode: %s", get_loop_mode_str());
    update_controls_status(msg);
    render_controls();
}

/**
 * 播放音频文件的线程函数
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
    
    // 解码和播放循环
    while (g_play_thread_running && av_read_frame(fmt_ctx, packet) >= 0) {
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
 * 播放音频
 */
void play_audio(int index) {
    // 停止当前播放
    if (g_play_thread_running) {
        stop_audio();
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
 * 暂停音频
 */
void pause_audio() {
    if (g_play_state == PLAY_STATE_PLAYING) {
        g_play_state = PLAY_STATE_PAUSED;
        update_controls_status("Paused");
        render_playlist_content();
    }
}

/**
 * 恢复音频
 */
void resume_audio() {
    if (g_play_state == PLAY_STATE_PAUSED) {
        g_play_state = PLAY_STATE_PLAYING;
        update_controls_status("Resumed");
        render_playlist_content();
    }
}

/**
 * 停止音频
 */
void stop_audio() {
    if (g_play_thread_running) {
        g_play_thread_running = 0;
        pthread_join(g_play_thread, NULL);
    }
    g_play_state = PLAY_STATE_STOPPED;
    g_current_play_index = -1;
    update_controls_status("Stopped");
    render_playlist_content();
}

/**
 * 播放下一曲
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
        // 顺序播放
        next_index = g_current_play_index + 1;
        if (next_index >= g_playlist.count) {
            next_index = 0;
        }
    }
    
    play_audio(next_index);
}

/**
 * 播放上一曲
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
        // 顺序播放
        prev_index = g_current_play_index - 1;
        if (prev_index < 0) {
            prev_index = g_playlist.count - 1;
        }
    }
    
    play_audio(prev_index);
}

/**
 * 清理资源
 */
void cleanup() {
    delwin(win_playlist);
    delwin(win_controls);
    delwin(win_lyrics);
    endwin(); // 结束 ncurses 模式
}

int main(int argc, char *argv[]) {
    // 1. 初始化
    init_ncurses();

    // 初始化FFmpeg库
    avformat_network_init();

    // 初始化 ALSA 并检测音频设备
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

    // 2. 构建布局
    create_layout();
    
    // 初始化播放列表状态
    g_playlist.is_loaded = 0;
    strcpy(g_playlist.folder_path, "");

    // 3. 运行事件循环
    run_event_loop();

    // 4. 清理退出
    cleanup();

    printf("ter-music exited gracefully.\n");
    return 0;
}