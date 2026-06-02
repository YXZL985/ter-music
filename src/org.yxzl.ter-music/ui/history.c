/**
 * @file history.c
 * @brief 历史视图 — 目录历史浏览和管理
 *
 * 从 menus.c 拆分而来，负责历史页面的渲染和输入处理。
 *
 * @author 燕戏竹林 (yxzl666xx@outlook.com)
 * @date 2026-06-02
 */

#include "types.h"
#include "audio/audio.h"
#include "ui/dialog.h"
#include "ui/ui.h"
#include "ui/menus.h"
#include "ui/menu_internal.h"
#include "config/config.h"
#include "playlist/playlist.h"
#include "library/library.h"
#include <stdio.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include <time.h>
#include <stddef.h>

/* ============================================================
 * History view rendering
 * ============================================================ */

void render_history_content(void)
{
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int menu_width = max_x / 4;
    int content_start_x = menu_width + 2;
    int start_y = 2;

    attron(COLOR_PAIR(COLOR_PAIR_PLAYLIST));

    mvprintw(start_y, content_start_x,
             use_english_ui() ? "Folder History (%d)" : "目录历史（%d 个目录）",
             g_dir_history.count);
    mvprintw(start_y + 1, content_start_x, "----------------------------------------");
    start_y += 3;

    if (g_dir_history.count == 0) {
        mvprintw(start_y, content_start_x, "%s",
                 menu_text("还没有目录历史。", "No folder history yet."));
        mvprintw(start_y + 1, content_start_x, "%s",
                 menu_text("打开音乐目录后会自动记录。", "Opened music folders will appear here."));
    } else {
        int visible_lines = max_y - start_y - 2;

        if (g_content_selected_idx >= g_dir_history.count) {
            g_content_selected_idx = g_dir_history.count - 1;
        }
        if (g_content_selected_idx < 0) g_content_selected_idx = 0;

        if (g_content_selected_idx < g_history_content_offset) {
            g_history_content_offset = g_content_selected_idx;
        } else if (g_content_selected_idx >= g_history_content_offset + visible_lines) {
            g_history_content_offset = g_content_selected_idx - visible_lines + 1;
        }

        for (int i = 0; i < visible_lines && (g_history_content_offset + i) < g_dir_history.count; i++) {
            int idx = g_history_content_offset + i;
            DirHistoryEntry *entry = &g_dir_history.entries[idx];

            char time_str[64];
            struct tm *tm_info = localtime(&entry->open_time);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

            char display_path[MAX_PATH_LEN];
            int path_width = max_x - menu_width - 25;
            if (path_width > MAX_PATH_LEN - 1) path_width = MAX_PATH_LEN - 1;
            utf8_str_truncate(display_path, entry->path, path_width);

            if (idx == g_content_selected_idx && g_focus_area == FOCUS_CONTENT) {
                attron(A_REVERSE);
                mvprintw(start_y + i, content_start_x, " %s [%s]", display_path, time_str);
                attroff(A_REVERSE);
            } else {
                mvprintw(start_y + i, content_start_x, " %s [%s]", display_path, time_str);
            }
        }

        int bottom_y = max_y - 3;
        mvprintw(bottom_y, content_start_x, "%s",
                 menu_text("按 ENTER 替换播放列表，按 'A' 追加到队列",
                           "Press Enter to replace, 'A' to append"));
        mvprintw(bottom_y + 1, content_start_x, "%s",
                 menu_text("按 'D' 删除当前项，按 'C' 清空全部",
                           "Press 'D' to delete, 'C' to clear all"));
    }

    attroff(COLOR_PAIR(COLOR_PAIR_PLAYLIST));
    refresh();
}

/* ============================================================
 * History view input handling
 * ============================================================ */

void handle_history_input(int ch)
{
    switch (ch) {
        case KEY_UP:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx--;
                if (g_menu_selected_idx < 0) g_menu_selected_idx = HISTORY_ITEM_COUNT - 1;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                render_history_content();
            } else {
                g_content_selected_idx--;
                if (g_content_selected_idx < 0) g_content_selected_idx = g_dir_history.count - 1;
                if (g_content_selected_idx < 0) g_content_selected_idx = 0;
                render_history_content();
            }
            break;

        case KEY_DOWN:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_menu_selected_idx++;
                if (g_menu_selected_idx >= HISTORY_ITEM_COUNT) g_menu_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                render_history_content();
            } else {
                g_content_selected_idx++;
                if (g_content_selected_idx >= g_dir_history.count) g_content_selected_idx = 0;
                render_history_content();
            }
            break;

        case KEY_RIGHT:
        case 9:
            if (g_focus_area == FOCUS_SIDEBAR) {
                g_focus_area = FOCUS_CONTENT;
                g_content_selected_idx = 0;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                render_history_content();
            }
            break;

        case KEY_LEFT:
            if (g_focus_area == FOCUS_CONTENT) {
                g_focus_area = FOCUS_SIDEBAR;
                render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                render_history_content();
            }
            break;

        case 10:
        case ' ':
            if (g_focus_area == FOCUS_SIDEBAR) {
                if (g_menu_selected_idx == 0) {
                    g_focus_area = FOCUS_CONTENT;
                    g_content_selected_idx = 0;
                    render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                    render_history_content();
                } else if (g_menu_selected_idx == 1) {
                    clear_dir_history();
                    g_content_selected_idx = 0;
                    show_status_message(menu_text("历史记录已清空", "History cleared"));
                    render_menu_frame("历史 [F3]");
                    render_menu_sidebar(g_menu_selected_idx, history_sidebar_items, HISTORY_ITEM_COUNT);
                    render_history_content();
                } else if (g_menu_selected_idx == HISTORY_ITEM_COUNT - 1) {
                    exit_current_view();
                }
            } else {
                if (g_dir_history.count > 0 && g_content_selected_idx >= 0 &&
                    g_content_selected_idx < g_dir_history.count) {

                    const char *path = g_dir_history.entries[g_content_selected_idx].path;
                    extern void stop_audio(void);
                    stop_audio();
                    int count = load_playlist(path);

                    if (count > 0) {
                        extern int g_selected_index;
                        g_selected_index = 0;
                        add_dir_history_entry(path);
                        exit_current_view();
                        show_status_message(menu_text("目录加载成功", "Folder loaded"));
                    } else {
                        show_status_message(menu_text("目录中没有音频文件", "No audio files in this folder"));
                        render_history_content();
                    }
                }
            }
            break;

        case 'a':
        case 'A':
            if (g_focus_area == FOCUS_CONTENT &&
                g_dir_history.count > 0 &&
                g_content_selected_idx >= 0 &&
                g_content_selected_idx < g_dir_history.count) {

                const char *path = g_dir_history.entries[g_content_selected_idx].path;
                int count = append_playlist(path);

                if (count > 0) {
                    add_dir_history_entry(path);
                    exit_current_view();
                    if (g_app_config.remember_last_path) {
                        snprintf(g_app_config.last_opened_path, sizeof(g_app_config.last_opened_path), "%s", path);
                        save_config();
                    }
                    char msg[96];
                    snprintf(msg, sizeof(msg), "%s %d %s",
                             menu_text("已追加", "Appended"), count,
                             menu_text("首歌曲到队列", "tracks to queue"));
                    show_status_message(msg);
                } else {
                    show_status_message(menu_text("目录中没有新的音频文件", "No new audio files to append"));
                    render_history_content();
                }
            }
            break;

        case 'd':
        case 'D':
            if (g_focus_area == FOCUS_CONTENT && g_dir_history.count > 0 &&
                g_content_selected_idx >= 0 && g_content_selected_idx < g_dir_history.count) {

                char removed_path[MAX_PATH_LEN] = "";
                strncpy(removed_path, g_dir_history.entries[g_content_selected_idx].path, MAX_PATH_LEN - 1);
                removed_path[MAX_PATH_LEN - 1] = '\0';

                for (int i = g_content_selected_idx; i < g_dir_history.count - 1; i++) {
                    g_dir_history.entries[i] = g_dir_history.entries[i + 1];
                }
                g_dir_history.count--;

                if (library_is_available())
                    library_dir_history_remove(removed_path);

                if (g_content_selected_idx >= g_dir_history.count) {
                    g_content_selected_idx = g_dir_history.count - 1;
                }

                show_status_message(menu_text("已删除历史项", "History entry removed"));
                render_history_content();
            }
            break;

        case 'c':
        case 'C':
            if (g_focus_area == FOCUS_CONTENT) {
                clear_dir_history();
                g_content_selected_idx = 0;
                show_status_message(menu_text("历史记录已清空", "History cleared"));
                render_history_content();
            }
            break;

        case 'r':
        case 'R':
            /* NOTE: 'R' for rename is handled in playlist context only.
             * This case is defined but does nothing for history. */
            break;
    }
}
