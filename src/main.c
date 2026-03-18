#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

// 定义颜色对索引
#define COLOR_PAIR_BORDER 1
#define COLOR_PAIR_PLAYLIST 2
#define COLOR_PAIR_CONTROLS 3
#define COLOR_PAIR_LYRICS 4

// 全局窗口指针
WINDOW *win_playlist;
WINDOW *win_controls;
WINDOW *win_lyrics;

/**
 * 初始化 ncurses 环境
 */
void init_ncurses() {
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
    mvwprintw(win_controls, 0, 2, " Controls [Space:Play/Pause] ");
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));
    
    // 在控制栏绘制简单的提示
    mvwprintw(win_controls, 2, 2, "Status: Stopped");
    mvwprintw(win_controls, 3, 2, "Track: --");
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
 * 主事件循环 (预留逻辑处理位置)
 */
void run_event_loop() {
    int ch;
    
    while ((ch = getch()) != 'q') {
        // 预留逻辑处理：
        // 根据 ch 的值处理播放、暂停、下一首等操作
        // 目前仅刷新界面以适应窗口大小变化
        
        // 检查窗口大小变化 (可选优化)
        // 如果检测到 KEY_RESIZE，可以在此处重新计算布局
        
        // 简单演示：在控制栏显示按下的键
        // 实际产品中应移除或改为调试模式
        /* 
        mvwprintw(win_controls, 4, 2, "Last Key: %d", ch);
        wrefresh(win_controls);
        */
    }
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

    // 3. 运行事件循环
    run_event_loop();

    // 4. 清理退出
    cleanup();

    printf("ter-music exited gracefully.\n");
    return 0;
}