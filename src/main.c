#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

// 声明外部函数
extern void init_ncurses();
extern void create_layout();
extern void run_event_loop();
extern void cleanup();
extern int load_playlist(const char *path);
extern void prompt_open_folder();

int main(int argc, char *argv[]) {
    // 解析命令行参数
    char *open_path = NULL;
    int opt;
    struct option long_options[] = {
        {"open", required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "o:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                open_path = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-o|--open path]\n", argv[0]);
                return 1;
        }
    }

    // 1. 初始化
    init_ncurses();

    // 初始化FFmpeg库
    init_ffmpeg();


    // 初始化音频设备
    init_audio_device();

    // 2. 构建布局
    create_layout();
    
    // 初始化播放列表状态
    g_playlist.is_loaded = 0;
    strcpy(g_playlist.folder_path, "");
    
    // 如果指定了打开路径，加载播放列表
    if (open_path) {
        int count = load_playlist(open_path);
        if (count > 0) {
            g_selected_index = 0;
        }
    }

    // 3. 运行事件循环
    run_event_loop();

    // 4. 清理退出
    cleanup();

    printf("ter-music exited gracefully.\n");
    return 0;
}
