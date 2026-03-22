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

extern void init_ncurses();
extern void create_layout();
extern void run_event_loop();
extern void cleanup();
extern int load_playlist(const char *path);
extern void prompt_open_folder();

void crash_handler(int sig) {
    endwin();
    fprintf(stderr, "Fatal error: signal %d\n", sig);
    exit(1);
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

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  -o, --open <path>    Open a music directory at startup\n");
    printf("  -h, --help           Show this help message\n");
    printf("\n");
    printf("Examples:\n");
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
    
    init_ncurses();
    
    init_menu_views();
    
    init_ffmpeg();
    
    init_audio_device();
    
    create_layout();
    
    g_playlist.is_loaded = 0;
    strcpy(g_playlist.folder_path, "");
    
    int loaded = 0;
    int used_fallback = 0;
    char *final_path = NULL;
    
    if (open_path && strlen(open_path) > 0) {
        char expanded_path[MAX_PATH_LEN];
        
        if (open_path[0] == '~') {
            const char *home = getenv("HOME");
            if (home) {
                snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, open_path + 1);
            } else {
                strncpy(expanded_path, open_path, MAX_PATH_LEN - 1);
            }
        } else {
            strncpy(expanded_path, open_path, MAX_PATH_LEN - 1);
        }
        
        struct stat s;
        if (stat(expanded_path, &s) == 0 && S_ISDIR(s.st_mode)) {
            if (has_audio_files(expanded_path)) {
                int count = load_playlist(expanded_path);
                if (count > 0) {
                    g_selected_index = 0;
                    loaded = 1;
                    final_path = expanded_path;
                }
            } else {
                mvprintw(2, 2, "Warning: No audio files in specified directory");
                mvprintw(3, 2, "Falling back to default startup path...");
                refresh();
                used_fallback = 1;
            }
        } else {
            mvprintw(2, 2, "Warning: Invalid path specified: %s", open_path);
            mvprintw(3, 2, "Falling back to default startup path...");
            refresh();
            used_fallback = 1;
        }
    }
    
    if (!loaded && g_app_config.default_startup_path[0] != '\0') {
        struct stat s;
        if (stat(g_app_config.default_startup_path, &s) == 0 && S_ISDIR(s.st_mode)) {
            if (has_audio_files(g_app_config.default_startup_path)) {
                int count = load_playlist(g_app_config.default_startup_path);
                if (count > 0) {
                    g_selected_index = 0;
                    loaded = 1;
                    final_path = g_app_config.default_startup_path;
                    
                    if (used_fallback) {
                        mvprintw(4, 2, "Loaded from default path: %s", g_app_config.default_startup_path);
                        refresh();
                    }
                }
            }
        }
    }
    
    if (!loaded && g_app_config.remember_last_path && g_app_config.last_opened_path[0] != '\0') {
        struct stat s;
        if (stat(g_app_config.last_opened_path, &s) == 0 && S_ISDIR(s.st_mode)) {
            if (has_audio_files(g_app_config.last_opened_path)) {
                int count = load_playlist(g_app_config.last_opened_path);
                if (count > 0) {
                    g_selected_index = 0;
                    loaded = 1;
                    final_path = g_app_config.last_opened_path;
                }
            }
        }
    }
    
    if (loaded && final_path) {
        add_dir_history_entry(final_path);
        
        if (g_app_config.remember_last_path) {
            strncpy(g_app_config.last_opened_path, final_path, MAX_PATH_LEN - 1);
            save_config();
        }
        
        if (g_app_config.auto_play_on_start && g_playlist.count > 0) {
            play_audio(0);
        }
    }
    
    run_event_loop();
    
    cleanup();
    
    printf("ter-music exited gracefully.\n");
    return 0;
}
