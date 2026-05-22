#include "../include/search.h"
#include "../include/menu_views.h"
#include "../include/media_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include <ctype.h>

extern WINDOW *win_playlist;
extern WINDOW *win_controls;

extern void render_playlist_content(void);
extern void update_controls_status(const char *msg);

SearchState g_search_state = {0};

void search_clear(void) {
    memset(&g_search_state, 0, sizeof(g_search_state));
}

void perform_search(const char *query) {
    int playlist_total = playlist_count();

    memset(&g_search_state, 0, sizeof(g_search_state));
    g_search_state.selected_index = 0;

    for (int i = 0; i < playlist_total && g_search_state.result_count < MAX_SEARCH_RESULTS; i++) {
        if (track_matches_query(i, query)) {
            g_search_state.result_indices[g_search_state.result_count++] = i;
        }
    }

    if (g_search_state.result_count > 0) {
        g_search_state.active = 1;
    } else {
        g_search_state.active = 0;
    }

    log_info("search", "Search '%s': %d results out of %d tracks", query, g_search_state.result_count, playlist_total);

    render_playlist_content();

    char msg[64];
    snprintf(msg, sizeof(msg), "%s: %d %s",
             use_english_ui() ? "Search completed" : "搜索完成",
             g_search_state.result_count,
             use_english_ui() ? "results" : "个结果");
    update_controls_status(msg);
}

void search_prompt(void) {
    if (!win_controls || !playlist_is_loaded() || playlist_count() == 0) {
        if (!playlist_is_loaded() || playlist_count() == 0) {
            update_controls_status(use_english_ui() ? "Playlist is empty" : "播放列表为空，无法搜索");
        }
        return;
    }

    echo();
    curs_set(1);

    int max_y, max_x;
    getmaxyx(win_controls, max_y, max_x);

    mvwprintw(win_controls, 4, 2, "%s",
              use_english_ui() ? "Search: " : "搜索歌曲: ");
    wclrtoeol(win_controls);
    wrefresh(win_controls);

    char input[MAX_META_LEN];
    memset(input, 0, sizeof(input));
    int pos = 0;
    int ch;

    flushinp();

    while ((ch = getch()) != '\n' && ch != KEY_ENTER && ch != 27 && pos < MAX_META_LEN - 1) {
        if (ch == ERR) {
            media_session_tick();
            continue;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (pos > 0) {
                int cx = getcurx(win_controls);
                int cy = getcury(win_controls);

                unsigned char last_c = (unsigned char)input[pos - 1];
                int bytes_to_remove = 1;
                if (last_c >= 0x80) {
                    if ((last_c & 0xE0) == 0xC0) bytes_to_remove = 2;
                    else if ((last_c & 0xF0) == 0xE0) bytes_to_remove = 3;
                    else if ((last_c & 0xF8) == 0xF0) bytes_to_remove = 4;
                    else if ((last_c & 0xC0) == 0x80) {
                        bytes_to_remove = 2;
                        while (pos - bytes_to_remove >= 0 &&
                               (unsigned char)input[pos - bytes_to_remove] >= 0x80 &&
                               (unsigned char)input[pos - bytes_to_remove] < 0xC0) {
                            bytes_to_remove++;
                        }
                    }
                    if (bytes_to_remove > pos) bytes_to_remove = pos;
                    pos -= bytes_to_remove;

                    if ((last_c & 0xF0) == 0xE0 || (last_c & 0xE0) == 0xC0) {
                        move(cy, cx - 2);
                    } else {
                        move(cy, cx - 1);
                    }
                } else {
                    pos--;
                    move(cy, cx - 1);
                }
                clrtoeol();
                wrefresh(win_controls);
            }
        } else if (ch >= 0x20 && ch <= 0x7E) {
            input[pos++] = (char)ch;
            waddch(win_controls, ch);
            wrefresh(win_controls);
        } else if ((ch & 0xC0) == 0x80 || ch >= 0x80) {
            if (pos < MAX_META_LEN - 1) {
                input[pos++] = (char)ch;
                if ((ch & 0xE0) == 0xC0 || (ch & 0xF0) == 0xE0) {
                    waddch(win_controls, ch);
                    wrefresh(win_controls);
                }
            }
        } else {
            input[pos++] = (char)ch;
            waddch(win_controls, ch);
            wrefresh(win_controls);
        }
    }

    input[pos] = '\0';
    flushinp();

    noecho();
    curs_set(0);

    mvwprintw(win_controls, 4, 2, "                    ");
    wclrtoeol(win_controls);
    wrefresh(win_controls);

    if (ch == 27) {
        update_controls_status(use_english_ui() ? "Search cancelled" : "搜索已取消");
        return;
    }

    if (strlen(input) > 0) {
        perform_search(input);
    } else {
        g_search_state.active = 0;
        render_playlist_content();
        update_controls_status(use_english_ui() ? "Search cancelled" : "搜索已取消");
    }
}

int search_lines(const char **lines, int line_count, const char *query,
                 int *results, int max_results) {
    if (!lines || !query || !results || max_results <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < line_count && count < max_results; i++) {
        if (strcasestr(lines[i], query)) {
            results[count++] = i;
        }
    }

    return count;
}
