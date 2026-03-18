#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>
#include <locale.h>

// 定义颜色对索引
#define COLOR_PAIR_BORDER 1
#define COLOR_PAIR_PLAYLIST 2
#define COLOR_PAIR_CONTROLS 3
#define COLOR_PAIR_LYRICS 4

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

// 定义控件数量
#define CONTROL_COUNT 5
const char *control_labels[] = {"<<", "Play/Pause", ">>", "Stop", "Loop"};

// 函数声明
void update_controls_status(const char *msg);
void cleanup();

/**
 * 初始化 ncurses 环境
 */
void init_ncurses() {
    // 设置本地化环境，支持中文等多字节字符
    setlocale(LC_ALL, "");

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
    strncpy(temp_title, fname, MAX_META_LEN - 1);
    temp_title[MAX_META_LEN - 1] = '\0';
    char *dot = strrchr(temp_title, '.');
    if (dot) *dot = '\0';

    // 初始化默认值
    strncpy(title, temp_title, MAX_META_LEN - 1);
    strncpy(artist, "Unknown Artist", MAX_META_LEN - 1);
    strncpy(album, "Unknown Album", MAX_META_LEN - 1);
    
    // 尝试从文件名中提取元数据（格式：Artist - Title）
    char *dash_pos = strstr(temp_title, " - ");
    if (dash_pos) {
        *dash_pos = '\0';
        strncpy(artist, temp_title, MAX_META_LEN - 1);
        strncpy(title, dash_pos + 3, MAX_META_LEN - 1);
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
                strncpy(title, tag_title, MAX_META_LEN - 1);
            }
            if (tag_artist && strlen(tag_artist) > 0) {
                strncpy(artist, tag_artist, MAX_META_LEN - 1);
            }
            if (tag_album && strlen(tag_album) > 0) {
                strncpy(album, tag_album, MAX_META_LEN - 1);
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
                strncpy(t->path, full_path, MAX_PATH_LEN - 1);
                
                // 读取元数据
                get_audio_metadata(full_path, t->title, t->artist, t->album);
                
                g_playlist.count++;
            }
        }
    }
    closedir(dir);
    
    strncpy(g_playlist.folder_path, path, MAX_PATH_LEN - 1);
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
            
            strncpy(truncated_title, t->title, title_width - 1);
            truncated_title[title_width - 1] = '\0';
            if (strlen(t->title) > title_width - 1) {
                truncated_title[title_width - 4] = '.';
                truncated_title[title_width - 3] = '.';
                truncated_title[title_width - 2] = '.';
            }
            
            strncpy(truncated_artist, t->artist, artist_width - 1);
            truncated_artist[artist_width - 1] = '\0';
            if (strlen(t->artist) > artist_width - 1) {
                truncated_artist[artist_width - 4] = '.';
                truncated_artist[artist_width - 3] = '.';
                truncated_artist[artist_width - 2] = '.';
            }

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
        
        char status_msg[MAX_META_LEN] = "Stopped";
        // 简单模拟状态显示，实际应由播放引擎驱动
        // 这里仅作展示，具体状态文字可根据全局播放状态变量扩展
        if (g_playlist.count > 0) {
             Track *t = &g_playlist.tracks[g_selected_index];
             // 计算可用宽度，显示更多元数据
             int status_width = w - 4;
             int title_width = status_width * 2 / 5;
             int artist_width = status_width * 2 / 5;
             int album_width = status_width * 1 / 5;
             
             // 截断过长的字符串
             char truncated_title[MAX_META_LEN];
             char truncated_artist[MAX_META_LEN];
             char truncated_album[MAX_META_LEN];
             
             strncpy(truncated_title, t->title, title_width - 1);
             truncated_title[title_width - 1] = '\0';
             if (strlen(t->title) > title_width - 1) {
                 truncated_title[title_width - 4] = '.';
                 truncated_title[title_width - 3] = '.';
                 truncated_title[title_width - 2] = '.';
             }
             
             strncpy(truncated_artist, t->artist, artist_width - 1);
             truncated_artist[artist_width - 1] = '\0';
             if (strlen(t->artist) > artist_width - 1) {
                 truncated_artist[artist_width - 4] = '.';
                 truncated_artist[artist_width - 3] = '.';
                 truncated_artist[artist_width - 2] = '.';
             }
             
             strncpy(truncated_album, t->album, album_width - 1);
             truncated_album[album_width - 1] = '\0';
             if (strlen(t->album) > album_width - 1) {
                 truncated_album[album_width - 4] = '.';
                 truncated_album[album_width - 3] = '.';
                 truncated_album[album_width - 2] = '.';
             }
             
             mvwprintw(win_playlist, status_line + 1, 2, "Status: %s", status_msg);
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
        const char *label = control_labels[i];
        int len = strlen(label);
        
        if (i == g_current_control_idx && g_control_focus == 1) {
            // 高亮当前选中的控件
            wattron(win_controls, A_REVERSE | A_BOLD);
            mvwprintw(win_controls, row, current_col, " [%s] ", label);
            wattroff(win_controls, A_REVERSE | A_BOLD);
        } else {
            mvwprintw(win_controls, row, current_col, " [%s] ", label);
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
                strncpy(expanded_path, input_path, sizeof(expanded_path) - 1);
            }
        } else {
            strncpy(expanded_path, input_path, sizeof(expanded_path) - 1);
        }
        expanded_path[MAX_PATH_LEN - 1] = '\0';
        
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
                    // 模拟执行当前选中的控件功能
                    {
                        char msg[64] = "";
                        switch(g_current_control_idx) {
                            case 0: snprintf(msg, sizeof(msg), "Action: Prev Track"); break;
                            case 1: snprintf(msg, sizeof(msg), "Action: Play/Pause"); break;
                            case 2: snprintf(msg, sizeof(msg), "Action: Next Track"); break;
                            case 3: snprintf(msg, sizeof(msg), "Action: Stop"); break;
                            case 4: snprintf(msg, sizeof(msg), "Action: Toggle Loop"); break;
                        }
                        // 临时在控制栏显示动作反馈，实际项目应更新全局播放状态
                        int h, w;
                        getmaxyx(win_controls, h, w);
                        mvwprintw(win_controls, h-1, 2, "> %s", msg); 
                        wrefresh(win_controls);
                        // 注意：此处需要重新获取 h 或者避免使用局部变量 h，简化处理直接刷新
                        // 由于上面作用域问题，这里简单重绘整个控件区恢复原状稍后优化，或者直接依赖下一次循环重绘
                        // 为保持简洁，此处假设用户能看到短暂闪烁或后续逻辑会覆盖
                        sleep(1); // 模拟延迟
                        render_controls(); 
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
                        // 在列表模式下空格也可以作为播放/暂停的快捷方式
                        // 这里复用控制逻辑的提示
                        render_playlist_content(); 
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