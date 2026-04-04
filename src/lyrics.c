#include "../include/lyrics.h"
#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <ncursesw/ncurses.h>
#include <libgen.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 全局歌词变量实例
Lyrics g_lyrics = {
    .count = 0,
    .current_index = -1,
    .highlight_count = 0,
    .has_lyrics = 0,
    .cursor_index = -1,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// 外部窗口变量声明
extern WINDOW *win_lyrics;

static const char *lyric_text(const char *utf8, const char *ascii) {
    return use_english_ui() ? ascii : utf8;
}

static uint64_t lyric_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static int lyric_glyph_width(const char *glyph) {
    int width = utf8_str_width(glyph);
    return width > 0 ? width : 1;
}

static void draw_lyric_glyph(int row, int col, const char *glyph, int attrs) {
    if (!win_lyrics || !glyph || glyph[0] == '\0') {
        return;
    }

    int h, w;
    getmaxyx(win_lyrics, h, w);

    int glyph_width = lyric_glyph_width(glyph);
    if (row <= 0 || row >= h - 1 || col <= 1 || col + glyph_width > w - 1) {
        return;
    }

    if (attrs != 0) {
        wattron(win_lyrics, attrs);
    }
    mvwprintw(win_lyrics, row, col, "%s", glyph);
    if (attrs != 0) {
        wattroff(win_lyrics, attrs);
    }
}

static const char *pick_disc_glyph(double diff, double angle, int pulse_phase) {
    static const char *ascii_idle[] = {".", "o", "x", "+", "a", "m", "n", "z"};
    static const char *ascii_hot[] = {"*", "#", "@", "%", "M", "U", "S", "I", "C"};
    static const char *unicode_idle[] = {"·", "•", "◦", "○", "◇", "A", "M", "N", "Z", "♪"};
    static const char *unicode_hot[] = {"✦", "✧", "✶", "✹", "♪", "♬", "♩", "M", "U", "S", "I", "C"};

    const char **idle = use_ascii_fallback_ui() ? ascii_idle : unicode_idle;
    const char **hot = use_ascii_fallback_ui() ? ascii_hot : unicode_hot;
    int idle_count = use_ascii_fallback_ui() ? (int)(sizeof(ascii_idle) / sizeof(ascii_idle[0]))
                                             : (int)(sizeof(unicode_idle) / sizeof(unicode_idle[0]));
    int hot_count = use_ascii_fallback_ui() ? (int)(sizeof(ascii_hot) / sizeof(ascii_hot[0]))
                                            : (int)(sizeof(unicode_hot) / sizeof(unicode_hot[0]));

    int angle_bucket = (int)lround(((angle + M_PI) / (M_PI * 2.0)) * 16.0);
    if (diff < 0.18) {
        return hot[(pulse_phase + angle_bucket) % hot_count];
    }
    if (diff < 0.42) {
        return hot[(pulse_phase / 2 + angle_bucket / 2) % hot_count];
    }
    return idle[(pulse_phase / 3 + angle_bucket) % idle_count];
}

static const char *pick_bar_glyph(double angle, int step, int extent, int pulse_phase) {
    static const char *ascii_body[] = {"|", "/", "\\", ":", "!", "+", "="};
    static const char *ascii_tip[] = {"*", "#", "@", "M", "U", "S", "I", "C"};
    static const char *unicode_body[] = {"·", "•", "╎", "╏", "│", "┆", "┊", "♪"};
    static const char *unicode_tip[] = {"✦", "✧", "✶", "✹", "♪", "♬", "♩", "♫", "M", "U", "S", "I", "C"};

    const char **body = use_ascii_fallback_ui() ? ascii_body : unicode_body;
    const char **tip = use_ascii_fallback_ui() ? ascii_tip : unicode_tip;
    int body_count = use_ascii_fallback_ui() ? (int)(sizeof(ascii_body) / sizeof(ascii_body[0]))
                                             : (int)(sizeof(unicode_body) / sizeof(unicode_body[0]));
    int tip_count = use_ascii_fallback_ui() ? (int)(sizeof(ascii_tip) / sizeof(ascii_tip[0]))
                                            : (int)(sizeof(unicode_tip) / sizeof(unicode_tip[0]));

    double normalized = fmod(angle + (M_PI * 2.0), M_PI * 2.0);
    if (normalized < 0) {
        normalized += M_PI * 2.0;
    }

    int angle_bucket = (int)lround((normalized / (M_PI * 2.0)) * 12.0);
    if (step == extent - 1) {
        return tip[(pulse_phase + angle_bucket) % tip_count];
    }
    return body[(pulse_phase / 2 + angle_bucket + step) % body_count];
}

static int use_emoji_no_lyrics_title(void) {
    return !use_ascii_fallback_ui() && utf8_str_width("🎵") >= 2;
}

static void render_no_lyrics_spectrum(int h, int w) {
    if (h < 12 || w < 24) {
        mvwprintw(win_lyrics, h / 2, 2, "%s", lyric_text("没有可用歌词", "No lyrics available"));
        return;
    }

    int levels[VISUALIZER_BAND_COUNT] = {0};
    int peaks[VISUALIZER_BAND_COUNT] = {0};
    uint64_t last_update_ms = 0;
    get_visualizer_snapshot(levels, peaks, VISUALIZER_BAND_COUNT, &last_update_ms);

    uint64_t now_ms = lyric_now_ms();
    double spin = (double)(now_ms % 5000ULL) / 5000.0;
    double highlight_angle = (spin * M_PI * 2.0) - (M_PI / 2.0);
    int pulse_phase = (int)((now_ms / 180ULL) % 24ULL);

    int center_y = h / 2 - 1;
    int center_x = w / 2;
    double x_scale = 1.8;
    int ring_radius = (h - 8) / 2;
    int width_radius = (w - 10) / 4;
    if (width_radius < ring_radius) {
        ring_radius = width_radius;
    }
    if (ring_radius < 4) {
        ring_radius = 4;
    }

    int disc_outer = ring_radius - 1;
    int disc_inner = disc_outer / 2;
    int max_bar_len = ring_radius / 2;
    if (max_bar_len < 2) {
        max_bar_len = 2;
    }

    for (int row = 1; row < h - 1; row++) {
        for (int col = 2; col < w - 2; col++) {
            double dx = ((double)col - (double)center_x) / x_scale;
            double dy = (double)row - (double)center_y;
            double dist = sqrt(dx * dx + dy * dy);

            if (dist > disc_inner && dist <= disc_outer) {
                double angle = atan2(dy, dx);
                double diff = fabs(angle - highlight_angle);
                if (diff > M_PI) {
                    diff = (2.0 * M_PI) - diff;
                }
                draw_lyric_glyph(row, col, pick_disc_glyph(diff, angle, pulse_phase), diff < 0.18 ? A_BOLD : 0);
            } else if (dist <= disc_inner - 0.4) {
                mvwaddch(win_lyrics, row, col, ' ');
            }
        }
    }

    int band_count = VISUALIZER_BAND_COUNT;
    int max_band_count = (int)(disc_outer * 5.0);
    if (band_count > max_band_count) {
        band_count = max_band_count;
    }
    if (band_count < 16) {
        band_count = 16;
    }

    int is_idle = (last_update_ms == 0) || (now_ms > last_update_ms + 260ULL);
    if (g_play_state == PLAY_STATE_STOPPED) {
        is_idle = 1;
    }

    for (int i = 0; i < band_count; i++) {
        int src = (i * VISUALIZER_BAND_COUNT) / band_count;
        if (src >= VISUALIZER_BAND_COUNT) {
            src = VISUALIZER_BAND_COUNT - 1;
        }

        int level = levels[src];
        if (is_idle) {
            double wave = sin((spin * M_PI * 6.0) + (double)i * 0.42);
            level = 16 + (int)(14.0 * (wave + 1.0));
            if (g_play_state == PLAY_STATE_STOPPED) {
                level /= 2;
            }
        }

        int extent = 1 + (level * max_bar_len) / 100;
        if (extent < 1) {
            extent = 1;
        }

        double angle = (-M_PI / 2.0) + ((2.0 * M_PI * (double)i) / (double)band_count);
        const char *bar_glyph = pick_bar_glyph(angle, 0, extent, pulse_phase + i);

        for (int step = 0; step < extent; step++) {
            double radius = (double)disc_outer + 1.0 + (double)step;
            int row = (int)lround((double)center_y + sin(angle) * radius);
            int col = (int)lround((double)center_x + cos(angle) * radius * x_scale);
            if (row <= 0 || row >= h - 1 || col <= 1 || col >= w - 1) {
                continue;
            }
            bar_glyph = pick_bar_glyph(angle, step, extent, pulse_phase + i);
            draw_lyric_glyph(row, col, bar_glyph, step == extent - 1 ? A_BOLD : 0);
        }
    }

    const char *title = use_emoji_no_lyrics_title()
        ? lyric_text("🎵 暂无歌词 🎶", "🎵 No Lyrics 🎶")
        : lyric_text("暂无歌词", "No Lyrics");
    const char *subtitle = use_emoji_no_lyrics_title()
        ? lyric_text("字母 · 符号 · Emoji", "Letters · Symbols · Emoji")
        : lyric_text("字母 · 符号粒子", "Letter + Symbol Particles");
    int title_col = center_x - utf8_str_width(title) / 2;
    int subtitle_col = center_x - utf8_str_width(subtitle) / 2;
    int text_row = center_y + disc_outer + max_bar_len + 1;

    if (text_row < h - 2) {
        if (title_col < 2) {
            title_col = 2;
        }
        mvwprintw(win_lyrics, text_row, title_col, "%s", title);
    }
    if (text_row + 1 < h - 1) {
        if (subtitle_col < 2) {
            subtitle_col = 2;
        }
        mvwprintw(win_lyrics, text_row + 1, subtitle_col, "%s", subtitle);
    }
}

static int get_corner_spectrum_height(int h) {
    if (h >= 28) {
        return 5;
    }
    if (h >= 22) {
        return 4;
    }
    if (h >= 17) {
        return 3;
    }
    if (h >= 13) {
        return 2;
    }
    return 1;
}

static const char *lyric_spectrum_glyph(int units) {
    static const char *glyphs[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    static const char ascii_glyphs[] = {' ', '.', ':', '-', '=', '+', '*', '#', '#'};

    if (units < 0) {
        units = 0;
    }
    if (units > 8) {
        units = 8;
    }

    if (use_ascii_fallback_ui()) {
        static char glyph_buf[2];
        glyph_buf[0] = ascii_glyphs[units];
        glyph_buf[1] = '\0';
        return glyph_buf;
    }

    return glyphs[units];
}

static int render_corner_spectrum(int h, int w) {
    int spectrum_height = get_corner_spectrum_height(h);
    int graph_top = 1;
    int graph_bottom = graph_top + spectrum_height - 1;
    if (graph_bottom >= h - 2 || w < 16) {
        return 1;
    }

    int graph_width = w - 4;
    int levels[VISUALIZER_BAND_COUNT] = {0};
    int peaks[VISUALIZER_BAND_COUNT] = {0};
    uint64_t last_update_ms = 0;
    get_visualizer_snapshot(levels, peaks, VISUALIZER_BAND_COUNT, &last_update_ms);
    (void)peaks;

    uint64_t now_ms = lyric_now_ms();
    int inactive_decay = 0;
    int is_visualizer_active = 0;
    if (last_update_ms > 0 && now_ms > last_update_ms) {
        inactive_decay = (int)((now_ms - last_update_ms) / 90ULL);
        if ((now_ms - last_update_ms) < 250ULL &&
            (g_play_state == PLAY_STATE_PLAYING || g_play_state == PLAY_STATE_PAUSED)) {
            is_visualizer_active = 1;
        }
    }

    int column_units[graph_width];
    for (int col = 0; col < graph_width; col++) {
        double normalized = (graph_width <= 1)
            ? 0.0
            : ((double)col * (double)(VISUALIZER_BAND_COUNT - 1)) / (double)(graph_width - 1);
        int left = (int)normalized;
        int right = left + 1;
        if (right >= VISUALIZER_BAND_COUNT) {
            right = VISUALIZER_BAND_COUNT - 1;
        }

        double frac = normalized - (double)left;
        int blended_level = (int)lround(((double)levels[left] * (1.0 - frac)) + ((double)levels[right] * frac));
        int level = blended_level - inactive_decay * 7;
        if (level < 0) {
            level = 0;
        }
        if (is_visualizer_active && level > 0 && level < 3) {
            level = 3;
        }

        int units = (level * spectrum_height * 8 + 99) / 100;
        if (level > 0 && units == 0) {
            units = 1;
        }
        column_units[col] = units;
    }

    if (graph_width >= 3) {
        int smoothed_units[graph_width];
        smoothed_units[0] = (column_units[0] * 3 + column_units[1]) / 4;
        for (int col = 1; col < graph_width - 1; col++) {
            smoothed_units[col] = (column_units[col - 1] + column_units[col] * 2 + column_units[col + 1]) / 4;
        }
        smoothed_units[graph_width - 1] = (column_units[graph_width - 2] + column_units[graph_width - 1] * 3) / 4;

        for (int col = 0; col < graph_width; col++) {
            column_units[col] = smoothed_units[col];
        }
    }

    for (int row = graph_top; row <= graph_bottom; row++) {
        mvwhline(win_lyrics, row, 1, ' ', w - 2);
    }

    for (int col = 0; col < graph_width; col++) {
        for (int row = graph_bottom; row >= graph_top; row--) {
            int row_from_bottom = graph_bottom - row;
            int units = column_units[col] - row_from_bottom * 8;
            if (units < 0) {
                units = 0;
            }
            if (units > 8) {
                units = 8;
            }
            mvwaddstr(win_lyrics, row, 2 + col, lyric_spectrum_glyph(units));
        }
    }

    int separator_row = graph_bottom + 1;
    if (separator_row < h - 1) {
        mvwhline(win_lyrics, separator_row, 1, ACS_HLINE, w - 2);
        mvwaddch(win_lyrics, separator_row, 0, ACS_VLINE);
        mvwaddch(win_lyrics, separator_row, w - 1, ACS_VLINE);
        return separator_row + 1;
    }

    return graph_bottom + 1;
}

static void reset_loaded_lyrics(void) {
    pthread_mutex_lock(&g_lyrics.lock);
    g_lyrics.count = 0;
    g_lyrics.current_index = -1;
    g_lyrics.highlight_count = 0;
    g_lyrics.has_lyrics = 0;
    g_lyrics.cursor_index = -1;
    pthread_mutex_unlock(&g_lyrics.lock);
}

static int duplicate_text_bytes(const unsigned char *data, size_t len, size_t skip, char **out_text) {
    if (!out_text) {
        return -1;
    }

    if (!data || skip > len) {
        return -1;
    }

    size_t text_len = len - skip;
    char *copy = malloc(text_len + 1);
    if (!copy) {
        return -1;
    }

    if (text_len > 0) {
        memcpy(copy, data + skip, text_len);
    }
    copy[text_len] = '\0';
    *out_text = copy;
    return 0;
}

static int read_file_bytes(const char *path, unsigned char **data_out, size_t *size_out) {
    if (!path || !data_out || !size_out) {
        return -1;
    }

    *data_out = NULL;
    *size_out = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return -1;
    }

    rewind(fp);

    unsigned char *data = malloc((size_t)file_size + 1);
    if (!data) {
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);

    data[bytes_read] = '\0';
    *data_out = data;
    *size_out = bytes_read;
    return 0;
}

static int is_valid_utf8_bytes(const unsigned char *data, size_t len) {
    size_t i = 0;

    while (i < len) {
        unsigned char c = data[i];
        if (c < 0x80) {
            i++;
            continue;
        }

        if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80 || c < 0xC2) {
                return 0;
            }
            i += 2;
            continue;
        }

        if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= len ||
                (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80) {
                return 0;
            }
            if (c == 0xE0 && data[i + 1] < 0xA0) {
                return 0;
            }
            if (c == 0xED && data[i + 1] >= 0xA0) {
                return 0;
            }
            i += 3;
            continue;
        }

        if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= len ||
                (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80 ||
                (data[i + 3] & 0xC0) != 0x80) {
                return 0;
            }
            if (c == 0xF0 && data[i + 1] < 0x90) {
                return 0;
            }
            if (c > 0xF4 || (c == 0xF4 && data[i + 1] >= 0x90)) {
                return 0;
            }
            i += 4;
            continue;
        }

        return 0;
    }

    return 1;
}

static int looks_like_utf16_le(const unsigned char *data, size_t len) {
    if (!data || len < 4) {
        return 0;
    }

    size_t sample_len = len < 128 ? len : 128;
    int zero_odd = 0;
    int zero_even = 0;
    int pair_count = 0;

    for (size_t i = 0; i + 1 < sample_len; i += 2) {
        if (data[i] == 0x00) {
            zero_even++;
        }
        if (data[i + 1] == 0x00) {
            zero_odd++;
        }
        pair_count++;
    }

    return pair_count > 0 && zero_odd >= (pair_count / 3) && zero_odd > zero_even;
}

static int looks_like_utf16_be(const unsigned char *data, size_t len) {
    if (!data || len < 4) {
        return 0;
    }

    size_t sample_len = len < 128 ? len : 128;
    int zero_odd = 0;
    int zero_even = 0;
    int pair_count = 0;

    for (size_t i = 0; i + 1 < sample_len; i += 2) {
        if (data[i] == 0x00) {
            zero_even++;
        }
        if (data[i + 1] == 0x00) {
            zero_odd++;
        }
        pair_count++;
    }

    return pair_count > 0 && zero_even >= (pair_count / 3) && zero_even > zero_odd;
}

static int convert_text_to_utf8(const unsigned char *input,
                                size_t input_len,
                                const char *from_code,
                                char **out_text) {
    if (!input || !from_code || !out_text) {
        return -1;
    }

    iconv_t cd = iconv_open("UTF-8", from_code);
    if (cd == (iconv_t)-1) {
        return -1;
    }

    size_t out_cap = input_len * 4 + 16;
    if (out_cap < 64) {
        out_cap = 64;
    }

    char *output = malloc(out_cap);
    if (!output) {
        iconv_close(cd);
        return -1;
    }

    char *out_ptr = output;
    size_t out_left = out_cap - 1;
    char *in_ptr = (char *)input;
    size_t in_left = input_len;

    while (in_left > 0) {
        size_t ret = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
        if (ret != (size_t)-1) {
            continue;
        }

        if (errno == E2BIG) {
            size_t used = (size_t)(out_ptr - output);
            size_t new_cap = out_cap * 2;
            char *grown = realloc(output, new_cap);
            if (!grown) {
                free(output);
                iconv_close(cd);
                return -1;
            }
            output = grown;
            out_ptr = output + used;
            out_left = new_cap - used - 1;
            out_cap = new_cap;
            continue;
        }

        free(output);
        iconv_close(cd);
        return -1;
    }

    *out_ptr = '\0';
    *out_text = output;
    iconv_close(cd);
    return 0;
}

static int load_lyrics_text_utf8(const char *path, char **out_text) {
    if (!path || !out_text) {
        return -1;
    }

    *out_text = NULL;

    unsigned char *raw_data = NULL;
    size_t raw_size = 0;
    if (read_file_bytes(path, &raw_data, &raw_size) != 0) {
        return -1;
    }

    if (raw_size == 0) {
        free(raw_data);
        *out_text = calloc(1, 1);
        return *out_text ? 0 : -1;
    }

    if (raw_size >= 3 &&
        raw_data[0] == 0xEF && raw_data[1] == 0xBB && raw_data[2] == 0xBF &&
        is_valid_utf8_bytes(raw_data + 3, raw_size - 3)) {
        int rc = duplicate_text_bytes(raw_data, raw_size, 3, out_text);
        free(raw_data);
        return rc;
    }

    if (raw_size >= 2 && raw_data[0] == 0xFF && raw_data[1] == 0xFE) {
        int rc = convert_text_to_utf8(raw_data + 2, raw_size - 2, "UTF-16LE", out_text);
        free(raw_data);
        return rc;
    }

    if (raw_size >= 2 && raw_data[0] == 0xFE && raw_data[1] == 0xFF) {
        int rc = convert_text_to_utf8(raw_data + 2, raw_size - 2, "UTF-16BE", out_text);
        free(raw_data);
        return rc;
    }

    if (is_valid_utf8_bytes(raw_data, raw_size)) {
        int rc = duplicate_text_bytes(raw_data, raw_size, 0, out_text);
        free(raw_data);
        return rc;
    }

    if (looks_like_utf16_le(raw_data, raw_size) &&
        convert_text_to_utf8(raw_data, raw_size, "UTF-16LE", out_text) == 0) {
        free(raw_data);
        return 0;
    }

    if (looks_like_utf16_be(raw_data, raw_size) &&
        convert_text_to_utf8(raw_data, raw_size, "UTF-16BE", out_text) == 0) {
        free(raw_data);
        return 0;
    }

    const char *fallback_encodings[] = {"GB18030", "GBK", "BIG5", NULL};
    for (int i = 0; fallback_encodings[i] != NULL; i++) {
        if (convert_text_to_utf8(raw_data, raw_size, fallback_encodings[i], out_text) == 0) {
            free(raw_data);
            return 0;
        }
    }

    int rc = duplicate_text_bytes(raw_data, raw_size, 0, out_text);
    free(raw_data);
    return rc;
}

static void sanitize_ascii_lyric(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }

    dest[0] = '\0';
    if (!src || src[0] == '\0') {
        return;
    }

    size_t write = 0;
    int prev_space = 1;
    int saw_non_ascii = 0;

    for (size_t read = 0; src[read] != '\0' && write + 1 < dest_size; read++) {
        unsigned char c = (unsigned char)src[read];

        if (c < 0x80) {
            if (isspace(c)) {
                if (!prev_space) {
                    dest[write++] = ' ';
                    prev_space = 1;
                }
            } else if (isprint(c)) {
                dest[write++] = (char)c;
                prev_space = 0;
            }
        } else {
            saw_non_ascii = 1;
            if (!prev_space && write + 1 < dest_size) {
                dest[write++] = ' ';
                prev_space = 1;
            }
        }
    }

    while (write > 0 && dest[write - 1] == ' ') {
        write--;
    }
    dest[write] = '\0';

    if (write == 0 && saw_non_ascii) {
        snprintf(dest, dest_size, "[non-ASCII]");
    }
}

/**
 * 解析 LRC 时间戳字符串
 * 格式：[mm:ss.xx]
 * @param time_str 时间戳字符串（不包含方括号）
 * @return 时间戳（秒，包含毫秒）
 */
static double parse_timestamp(const char *time_str) {
    int mm, ss, xx;
    if (sscanf(time_str, "%d:%d.%d", &mm, &ss, &xx) == 3) {
        return mm * 60 + ss + xx / 100.0;  // 保留毫秒精度
    }
    return -1.0;
}

/**
 * 解析单行 LRC 内容
 * @param line LRC 文件的一行
 * @param timestamp 输出：时间戳（秒，包含毫秒）
 * @param text 输出：歌词文本
 * @return 1 表示成功，0 表示失败
 */
static int parse_lrc_line(const char *line, double *timestamp, char *text) {
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
    double ts = parse_timestamp(time_str);
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
        snprintf(text, MAX_LYRIC_TEXT_LEN, "%s", lyric_text("(纯音乐)", "(Instrumental)"));
    }
    
    return 1;
}

/**
 * 根据时间戳查找歌词索引
 * @param timestamp_seconds 时间戳（秒，包含毫秒）
 * @return 歌词索引，-1 表示未找到
 */
static int find_lyric_index(double timestamp_seconds) {
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
    
    char ascii_text[MAX_LYRIC_TEXT_LEN];
    const char *display_src = text ? text : "";
    if (use_ascii_fallback_ui()) {
        sanitize_ascii_lyric(ascii_text, sizeof(ascii_text), display_src);
        display_src = ascii_text;
    }

    // 计算文本实际宽度
    int text_width = utf8_str_width(display_src);
    
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
        utf8_str_substring(display_text, display_src, scroll_offset, max_width);
    } else {
        // 非高亮或文本不超长，使用普通截断
        utf8_str_truncate(display_text, display_src, max_width);
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
    
    char *lyrics_text = NULL;
    if (load_lyrics_text_utf8(lrc_path, &lyrics_text) != 0 || !lyrics_text) {
        reset_loaded_lyrics();
        return;
    }
    
    // 临时缓冲区存储解析后的歌词
    LyricLine temp_lines[MAX_LYRIC_LINES];
    int count = 0;

    char *cursor = lyrics_text;
    while (cursor && *cursor != '\0' && count < MAX_LYRIC_LINES) {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        if (newline) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor = line + strlen(line);
        }

        if (line[0] == '\0') {
            continue;
        }

        double timestamp;
        char text[MAX_LYRIC_TEXT_LEN];

        if (parse_lrc_line(line, &timestamp, text)) {
            temp_lines[count].timestamp = timestamp;
            decode_html_entities(text);
            strncpy(temp_lines[count].text, text, MAX_LYRIC_TEXT_LEN - 1);
            temp_lines[count].text[MAX_LYRIC_TEXT_LEN - 1] = '\0';
            count++;
        }
    }

    free(lyrics_text);
    
    // 如果没有解析到任何歌词
    if (count == 0) {
        reset_loaded_lyrics();
        return;
    }
    
    // 锁定并更新全局歌词数据
    pthread_mutex_lock(&g_lyrics.lock);
    g_lyrics.count = count;
    memcpy(g_lyrics.lines, temp_lines, sizeof(LyricLine) * count);
    g_lyrics.has_lyrics = 1;
    g_lyrics.current_index = -1;
    g_lyrics.highlight_count = 0;
    g_lyrics.cursor_index = -1;
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
    double current_pos = (double)g_current_position;
    int new_index = -1;
    int new_highlight_count = 0;
    int changed = 0;
    
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
        changed = 1;
    }
    
    pthread_mutex_unlock(&g_lyrics.lock);
    
    if (changed) {
        render_lyrics();
    }
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
    if (g_lyric_cursor_mode) {
        mvwprintw(win_lyrics, 0, 2, "%s", lyric_text(" [定位] 歌词 ", " [Seek] Lyrics "));
    } else {
        mvwprintw(win_lyrics, 0, 2, "%s", lyric_text(" 歌词 ", " Lyrics "));
    }
    wbkgd(win_lyrics, COLOR_PAIR(COLOR_PAIR_LYRICS));
    
    pthread_mutex_lock(&g_lyrics.lock);
    
    if (!g_lyrics.has_lyrics || g_lyrics.count == 0) {
        pthread_mutex_unlock(&g_lyrics.lock);
        render_no_lyrics_spectrum(h, w);
        wrefresh(win_lyrics);
        return;
    }

    int content_top = render_corner_spectrum(h, w);

    // 如果有歌词但没有当前索引（刚开始播放）
    if (g_lyrics.current_index < 0) {
        int message_row = content_top + ((h - content_top - 1) / 2);
        if (message_row >= h - 1) {
            message_row = h - 2;
        }
        mvwprintw(win_lyrics, message_row, 2, "%s", lyric_text("播放中...", "Playing..."));
        pthread_mutex_unlock(&g_lyrics.lock);
        wrefresh(win_lyrics);
        return;
    }

    // 计算可视区域大小
    int visible_lines = h - content_top - 1;
    if (visible_lines <= 0) {
        pthread_mutex_unlock(&g_lyrics.lock);
        wrefresh(win_lyrics);
        return;
    }
    
    int current_center_idx;
    if (g_lyric_cursor_mode && g_lyrics.cursor_index >= 0) {
        current_center_idx = g_lyrics.cursor_index;
    } else {
        current_center_idx = g_lyrics.current_index;
    }
    
    // 垂直居中策略：使当前高亮/光标行居中显示
    int start_idx = current_center_idx - (visible_lines / 2);
    if (start_idx < 0) start_idx = 0;
    if (start_idx + visible_lines > g_lyrics.count) {
        start_idx = g_lyrics.count - visible_lines;
    }
    if (start_idx < 0) start_idx = 0;  // 歌词行数不足时从头显示
    
    // 渲染可视区域内的歌词行
    for (int i = 0; i < visible_lines && (start_idx + i) < g_lyrics.count; i++) {
        int lyric_idx = start_idx + i;
        int row = content_top + i;
        
        int is_highlighted;
        int show_marker;
        
        if (g_lyric_cursor_mode) {
            is_highlighted = (lyric_idx == g_lyrics.cursor_index);
            show_marker = (lyric_idx == g_lyrics.cursor_index);
        } else {
            is_highlighted = (lyric_idx >= g_lyrics.current_index && 
                              lyric_idx < g_lyrics.current_index + g_lyrics.highlight_count);
            show_marker = (lyric_idx == g_lyrics.current_index);
        }
        
        render_lyric_line(row, g_lyrics.lines[lyric_idx].text, 
                         is_highlighted, show_marker);
    }
    
    pthread_mutex_unlock(&g_lyrics.lock);
    wrefresh(win_lyrics);
}
