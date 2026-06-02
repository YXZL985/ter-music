/**
 * @file controls.c
 * @brief 控制栏按钮渲染及控制激活
 *
 * 从 ui.c 拆分，负责控制栏按钮的绘制、标签构建和控件激活逻辑。
 *
 * @author 燕戏竹林 (yxzl666xx@outlook.com)
 * @date 2026-06-02
 */

#include "types.h"
#include "ui/ui.h"
#include "ui/menu_internal.h"
#include "audio/audio.h"
#include "playlist/playlist.h"
#include "config/config.h"
#include <ncursesw/ncurses.h>
#include <stdio.h>
#include <string.h>

extern WINDOW *win_controls;

/* ── Control labels ── */
extern const char *control_labels[];
extern const char *control_labels_en[];

/* ============================================================
 * Control label helpers
 * ============================================================ */

static const char *get_control_label(int index)
{
    if (index < 0 || index >= CONTROL_COUNT) return "";
    return use_english_ui() ? control_labels_en[index] : control_labels[index];
}

void build_control_label(int index, char *dest, size_t dest_size)
{
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (index < 0 || index >= CONTROL_COUNT) return;

    if (index == CONTROL_IDX_LOOP) {
        snprintf(dest, dest_size, "%s:%s", get_control_label(index), get_loop_mode_str());
        return;
    }
    if (index == CONTROL_IDX_SPEED) {
        snprintf(dest, dest_size, "%s:%.2fx", get_control_label(index), (double)g_playback_speed);
        return;
    }
    if (index == CONTROL_IDX_VOLUME) {
        snprintf(dest, dest_size, "%s:%d%%", get_control_label(index), get_volume_percent());
        return;
    }
    utf8_str_truncate(dest, get_control_label(index), (int)dest_size - 1);
}

/* ============================================================
 * Status line rendering
 * ============================================================ */

static pthread_mutex_t g_controls_status_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_controls_status_message[256] = "";
static time_t g_controls_status_time = 0;

void update_controls_status(const char *msg)
{
    if (msg && msg[0]) {
        log_debug("ui", "Controls status: %s", msg);
    }
    pthread_mutex_lock(&g_controls_status_mutex);
    snprintf(g_controls_status_message, sizeof(g_controls_status_message), "%s", msg ? msg : "");
    g_controls_status_time = time(NULL);
    pthread_mutex_unlock(&g_controls_status_mutex);
    request_ui_refresh(UI_DIRTY_CONTROLS);
}

void render_controls_status_line(void)
{
    if (!win_controls) return;

    int h, w;
    getmaxyx(win_controls, h, w);
    if (h <= 2 || w <= 3) return;

    char status_msg[sizeof(g_controls_status_message)];
    time_t status_time = 0;

    pthread_mutex_lock(&g_controls_status_mutex);
    snprintf(status_msg, sizeof(status_msg), "%s", g_controls_status_message);
    status_time = g_controls_status_time;
    pthread_mutex_unlock(&g_controls_status_mutex);

    int status_row = h - 2;
    mvwhline(win_controls, status_row, 1, ' ', w - 2);

    if (status_msg[0] != '\0' && (time(NULL) - status_time) < 3) {
        char display_msg[sizeof(g_controls_status_message)];
        format_display_text(display_msg, sizeof(display_msg), status_msg, w - 4, 0);
        mvwprintw(win_controls, status_row, 2, "%s", display_msg);
    }
}

/* ============================================================
 * Control activation
 * ============================================================ */

void activate_current_control(void)
{
    switch (g_current_control_idx) {
        case CONTROL_IDX_PREV:
            prev_track();
            break;
        case CONTROL_IDX_PLAY_PAUSE: {
            PlayState current_state = g_play_state;
            int is_thread_running = g_play_thread_running;

            if (current_state == PLAY_STATE_PLAYING && is_thread_running) {
                pause_audio();
            } else if (current_state == PLAY_STATE_PAUSED && is_thread_running) {
                resume_audio();
            } else if (current_state == PLAY_STATE_STOPPED) {
                int playlist_total = playlist_count();
                if (playlist_is_loaded() && playlist_total > 0) {
                    int target_index = (g_current_play_index >= 0)
                        ? g_current_play_index
                        : g_selected_index;
                    if (g_sort_state.active && g_current_play_index < 0) {
                        target_index = g_sort_state.sorted_indices[g_selected_index];
                    }
                    if (target_index >= 0 && target_index < playlist_total) {
                        play_audio(target_index);
                    }
                }
            }
            break;
        }
        case CONTROL_IDX_NEXT:
            next_track();
            break;
        case CONTROL_IDX_STOP:
            stop_audio();
            break;
        case CONTROL_IDX_LOOP:
            toggle_loop_mode();
            break;
        case CONTROL_IDX_SPEED:
            toggle_playback_speed();
            break;
        case CONTROL_IDX_VOLUME:
            adjust_volume(10);
            break;
        case CONTROL_IDX_PROGRESS:
            break;
    }
}

/* ============================================================
 * Controls rendering
 * ============================================================ */

void render_controls(void)
{
    if (!win_controls) return;

    werase(win_controls);
    box(win_controls, 0, 0);

    const char *focus_hint = g_control_focus
        ? ui_text("[控件焦点]", "[Ctrl Focus]")
        : ui_text("[列表焦点]", "[List Focus]");
    const char *lyric_hint = g_lyric_cursor_mode
        ? ui_text("[D:退出定位]", "[D:Exit Seek]")
        : ui_text("[D:歌词定位]", "[D:Lyric Seek]");
    char controls_header[160];
    snprintf(controls_header, sizeof(controls_header), "%s %s %s %s %s",
             ui_text("控制区", "Controls"),
             ui_text("[空格:执行]", "[Space:Run]"),
             ui_text("[C:控件]", "[C:Ctrl]"),
             ui_text("[L:列表]", "[L:List]"),
             focus_hint);
    mvwprintw(win_controls, 0, 2, " %s %s", controls_header, lyric_hint);
    wbkgd(win_controls, COLOR_PAIR(COLOR_PAIR_CONTROLS));

    int h, w;
    getmaxyx(win_controls, h, w);

    // Progress bar
    if (g_play_state != PLAY_STATE_STOPPED && g_total_duration > 0) {
        int progress_row = get_controls_progress_row(h);

        if (h >= 5 && w >= 20) {
            int current_pos = g_current_position;
            if (current_pos < 0) current_pos = 0;
            if (current_pos > g_total_duration) current_pos = g_total_duration;

            int progress_percent = (current_pos * 100) / g_total_duration;
            if (progress_percent > 100) progress_percent = 100;

            int current_min = current_pos / 60;
            int current_sec = current_pos % 60;
            int total_min = g_total_duration / 60;
            int total_sec = g_total_duration % 60;
            current_min %= 100;
            total_min %= 100;

            int is_progress_selected = (g_current_control_idx == CONTROL_IDX_PROGRESS && g_control_focus == 1);
            if (is_progress_selected) wattron(win_controls, A_REVERSE | A_BOLD);

            char time_str[32];
            snprintf(time_str, sizeof(time_str), "%02d:%02d / %02d:%02d",
                     current_min, current_sec, total_min, total_sec);
            mvwprintw(win_controls, progress_row, 2, "%s", time_str);

            int time_width = 13;
            int percent_width = 4;
            int padding = 4;
            int progress_bar_width = w - time_width - percent_width - padding - 4;
            if (progress_bar_width < 10) progress_bar_width = 10;

            int progress_start_col = 2 + time_width + 1;

            mvwprintw(win_controls, progress_row, progress_start_col, "[");

            int filled_width = (progress_bar_width * progress_percent) / 100;
            if (filled_width > progress_bar_width) filled_width = progress_bar_width;

            for (int i = 0; i < progress_bar_width && (progress_start_col + 1 + i) < w - 2; i++) {
                char c = '-';
                if (i < filled_width) c = '=';
                else if (i == filled_width && progress_percent < 100) c = '>';
                mvwaddch(win_controls, progress_row, progress_start_col + 1 + i, c);
            }

            mvwprintw(win_controls, progress_row, progress_start_col + 1 + progress_bar_width, "]");
            mvwprintw(win_controls, progress_row, progress_start_col + 2 + progress_bar_width, "%d%%", progress_percent);

            mvwaddch(win_controls, progress_row, 0, ACS_VLINE);
            mvwaddch(win_controls, progress_row, w - 1, ACS_VLINE);

            if (is_progress_selected) wattroff(win_controls, A_REVERSE | A_BOLD);
        }
    }

    // Control buttons
    int button_start_row = get_controls_button_row(h);
    int current_row = button_start_row;
    int available_width = w - 2;
    int current_col = 0;
    int is_first_in_row = 1;

    for (int i = 0; i < CONTROL_COUNT - 1; i++) {
        char display_label[32];
        build_control_label(i, display_label, sizeof(display_label));
        int len = utf8_str_width(display_label);
        int button_width = len + 4;

        if (is_first_in_row) {
            int row_button_count = 0;
            int row_width = 0;
            for (int j = i; j < CONTROL_COUNT - 1; j++) {
                char temp_label[32];
                build_control_label(j, temp_label, sizeof(temp_label));
                int temp_len = utf8_str_width(temp_label);
                int temp_button_width = temp_len + 4;
                if (row_width + temp_button_width > available_width && row_button_count > 0) break;
                row_width += temp_button_width;
                row_button_count++;
            }
            current_col = (available_width - row_width) / 2 + 1;
            if (current_col < 1) current_col = 1;
            is_first_in_row = 0;
        }

        if (!is_first_in_row && current_col + button_width > w - 1) {
            current_row++;
            if (current_row >= h - 2) break;

            int row_button_count = 0;
            int row_width = 0;
            for (int j = i; j < CONTROL_COUNT - 1; j++) {
                char temp_label[32];
                build_control_label(j, temp_label, sizeof(temp_label));
                int temp_len = utf8_str_width(temp_label);
                int temp_button_width = temp_len + 4;
                if (row_width + temp_button_width > available_width && row_button_count > 0) break;
                row_width += temp_button_width;
                row_button_count++;
            }
            current_col = (available_width - row_width) / 2 + 1;
            if (current_col < 1) current_col = 1;
        }

        if (i == g_current_control_idx && g_control_focus == 1) {
            wattron(win_controls, A_REVERSE | A_BOLD);
            mvwprintw(win_controls, current_row, current_col, " [%s] ", display_label);
            wattroff(win_controls, A_REVERSE | A_BOLD);
        } else {
            mvwprintw(win_controls, current_row, current_col, " [%s] ", display_label);
        }
        current_col += button_width;
    }

    render_controls_status_line();
    render_visualizer_with_album_cover();
    wrefresh(win_controls);
}
