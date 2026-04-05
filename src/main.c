#include "../include/defs.h"
#include "../include/menu_views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <ncursesw/ncurses.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

extern void init_ncurses();
extern void create_layout();
extern void run_event_loop();
extern void cleanup();
extern int load_playlist(const char *path);
extern void prompt_open_folder();

void crash_handler(int sig) {
    endwin();
    fprintf(stderr, "致命错误：收到信号 %d\n", sig);
    exit(1);
}

static void expand_user_path(const char *input, char *output, size_t output_size) {
    if (!output || output_size == 0) {
        return;
    }

    output[0] = '\0';
    if (!input || input[0] == '\0') {
        return;
    }

    if (input[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(output, output_size, "%s%s", home, input + 1);
            return;
        }
    }

    snprintf(output, output_size, "%s", input);
}

static int has_audio_files(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return 0;
    
    const char *audio_extensions[] = {
        ".mp3", ".MP3", ".wav", ".WAV", ".flac", ".FLAC",
        ".ogg", ".OGG", ".m4a", ".M4A", ".aac", ".AAC",
        ".wma", ".WMA", ".ape", ".APE", ".opus", ".OPUS", NULL
    };
    
    struct dirent *entry;
    int found = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        const char *ext = strrchr(entry->d_name, '.');
        if (ext) {
            for (int i = 0; audio_extensions[i] != NULL; i++) {
                if (strcmp(ext, audio_extensions[i]) == 0) {
                    found = 1;
                    break;
                }
            }
        }
        if (found) break;
    }
    
    closedir(dir);
    return found;
}

static int load_startup_playlist(const char *path, char *final_path, size_t final_path_size) {
    struct stat s;
    if (!path || path[0] == '\0') {
        return 0;
    }

    if (stat(path, &s) != 0 || !S_ISDIR(s.st_mode)) {
        return 0;
    }

    if (!has_audio_files(path)) {
        return 0;
    }

    if (load_playlist(path) <= 0) {
        return 0;
    }

    g_selected_index = 0;
    snprintf(final_path, final_path_size, "%s", path);
    return 1;
}

static void clear_saved_playback_session(void) {
    g_app_config.resume_last_playback = 0;
    g_app_config.last_played_position = 0;
    g_app_config.last_played_folder_path[0] = '\0';
    g_app_config.last_played_track_path[0] = '\0';
    save_config();
}

static int find_track_index_by_path(const char *track_path) {
    if (!track_path || track_path[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < g_playlist.count; i++) {
        if (strcmp(g_playlist.tracks[i].path, track_path) == 0) {
            return i;
        }
    }

    return -1;
}

static int restore_saved_playback_session(void) {
    if (!g_app_config.resume_last_playback || g_app_config.last_played_track_path[0] == '\0') {
        return 0;
    }

    int track_index = find_track_index_by_path(g_app_config.last_played_track_path);
    if (track_index < 0) {
        return 0;
    }

    g_selected_index = track_index;
    g_initial_seek_position = g_app_config.last_played_position;
    play_audio(track_index);
    return 1;
}

static int find_audio_directory_recursive(const char *path, char *found_path, size_t found_path_size, int depth) {
    if (!path || !found_path || found_path_size == 0 || depth > 8) {
        return 0;
    }

    if (has_audio_files(path)) {
        snprintf(found_path, found_path_size, "%s", path);
        return 1;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char child_path[MAX_PATH_LEN];
        if (snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name) >= (int)sizeof(child_path)) {
            continue;
        }

        struct stat st;
        if (lstat(child_path, &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
            continue;
        }

        if (find_audio_directory_recursive(child_path, found_path, found_path_size, depth + 1)) {
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);
    return 0;
}

static void print_usage(const char *prog_name) {
    printf("用法：%s [选项]\n\n", prog_name);
    printf("选项：\n");
    printf("  -o, --open <path>    启动时打开指定音乐目录\n");
    printf("  -h, --help           显示帮助信息\n");
    printf("\n");
    printf("示例：\n");
    printf("  %s -o ~/Music\n", prog_name);
    printf("  %s --open /path/to/music\n", prog_name);
}

int main(int argc, char *argv[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);

    char *open_path = NULL;
    int opt;
    struct option long_options[] = {
        {"open", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "o:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                open_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "错误：ter-music 需要在终端里直接运行。\n");
        fprintf(stderr, "请不要把它通过管道重定向到其他命令。\n");
        return 1;
    }
    
    init_ncurses();
    
    init_menu_views();
    set_volume_percent(g_app_config.volume_percent);
    
    if (g_app_config.clear_history_on_startup) {
        clear_dir_history();
    }
    
    init_ffmpeg();
    
    init_audio_device();
    
    create_layout();
    
    g_playlist.is_loaded = 0;
    g_playlist.folder_path[0] = '\0';
    
    int loaded = 0;
    int used_fallback = 0;
    int attempted_resume_load = 0;
    int resumed_playback = 0;
    char final_path[MAX_PATH_LEN] = "";
    
    if (open_path && strlen(open_path) > 0) {
        char expanded_path[MAX_PATH_LEN];
        expand_user_path(open_path, expanded_path, sizeof(expanded_path));

        struct stat s;
        if (stat(expanded_path, &s) == 0 && S_ISDIR(s.st_mode)) {
            if (load_startup_playlist(expanded_path, final_path, sizeof(final_path))) {
                loaded = 1;
            } else {
                mvprintw(2, 2, "%s",
                         use_english_ui()
                             ? "Warning: the selected folder has no playable audio files"
                             : "警告：指定目录中没有可播放的音频文件");
                mvprintw(3, 2, "%s",
                         use_english_ui()
                             ? "Continuing with current directory and default paths..."
                             : "将继续尝试当前目录和默认启动路径...");
                refresh();
                used_fallback = 1;
            }
        } else {
            mvprintw(2, 2, use_english_ui() ? "Warning: invalid path: %s" : "警告：指定路径无效：%s", open_path);
            mvprintw(3, 2, "%s",
                     use_english_ui()
                         ? "Continuing with current directory and default paths..."
                         : "将继续尝试当前目录和默认启动路径...");
            refresh();
            used_fallback = 1;
        }
    }

    if (!loaded && !open_path && g_app_config.resume_last_playback &&
        g_app_config.last_played_folder_path[0] != '\0') {
        attempted_resume_load = 1;
        if (load_startup_playlist(g_app_config.last_played_folder_path, final_path, sizeof(final_path))) {
            loaded = 1;
        }
    }

    if (!loaded) {
        char current_dir[MAX_PATH_LEN];
        char auto_found_path[MAX_PATH_LEN];

        if (getcwd(current_dir, sizeof(current_dir)) &&
            find_audio_directory_recursive(current_dir, auto_found_path, sizeof(auto_found_path), 0) &&
            load_startup_playlist(auto_found_path, final_path, sizeof(final_path))) {
            loaded = 1;

            if (used_fallback) {
                mvprintw(4, 2,
                         use_english_ui() ? "Auto-detected music folder in current directory: %s"
                                          : "已从当前目录自动找到音乐目录：%s",
                         auto_found_path);
                refresh();
            }
        }
    }
    
    if (!loaded && g_app_config.default_startup_path[0] != '\0') {
        if (load_startup_playlist(g_app_config.default_startup_path, final_path, sizeof(final_path))) {
            loaded = 1;

            if (used_fallback) {
                mvprintw(4, 2,
                         use_english_ui() ? "Loaded from default path: %s" : "已从默认路径加载：%s",
                         g_app_config.default_startup_path);
                refresh();
            }
        }
    }
    
    if (!loaded && g_app_config.remember_last_path && g_app_config.last_opened_path[0] != '\0') {
        if (load_startup_playlist(g_app_config.last_opened_path, final_path, sizeof(final_path))) {
            loaded = 1;
        }
    }
    
    if (loaded && final_path[0] != '\0') {
        add_dir_history_entry(final_path);
        
        if (g_app_config.remember_last_path) {
            snprintf(g_app_config.last_opened_path, sizeof(g_app_config.last_opened_path), "%s", final_path);
            save_config();
        }

        if (g_app_config.resume_last_playback &&
            strcmp(final_path, g_app_config.last_played_folder_path) == 0) {
            resumed_playback = restore_saved_playback_session();
            if (!resumed_playback) {
                clear_saved_playback_session();
            }
        }

        if (!resumed_playback && g_app_config.auto_play_on_start && g_playlist.count > 0) {
            play_audio(0);
        }
        if (attempted_resume_load &&
            !resumed_playback &&
            strcmp(final_path, g_app_config.last_played_folder_path) != 0) {
            clear_saved_playback_session();
        }
    } else if (attempted_resume_load) {
        clear_saved_playback_session();
    }
    
    run_event_loop();
    
    cleanup();
    
    printf("%s\n", use_english_ui() ? "ter-music exited cleanly." : "ter-music 已正常退出。");
    return 0;
}
