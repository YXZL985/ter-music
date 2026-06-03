/**
 * @file play_queue.c
 * @brief 播放队列管理器 — 基于 PlayMode 的队列构建、洗牌、导航
 *
 * @author ter-music team
 * @date 2026-06-03
 */

#include "types.h"
#include "audio/play_queue.h"
#include "playlist/playlist.h"
#include "ui/ui.h"
#include "ui/menus.h"
#include "logger/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Global instance
 * ============================================================ */

PlayQueue g_play_queue = {0};

/* ============================================================
 * Lifecycle
 * ============================================================ */

void play_queue_init(PlayQueue *q)
{
    memset(q, 0, sizeof(PlayQueue));
    q->current_position = -1;
}

void play_queue_clear(PlayQueue *q)
{
    q->count = 0;
    q->current_position = -1;
}

/* ============================================================
 * Fisher-Yates shuffle
 * ============================================================ */

void play_queue_shuffle_range(PlayQueue *q, int start, int count)
{
    if (start < 0 || count <= 1 || start + count > q->count) return;

    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }

    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = q->indices[start + i];
        q->indices[start + i] = q->indices[start + j];
        q->indices[start + j] = tmp;
    }
}

static void reshuffle_keeping_current(PlayQueue *q)
{
    if (q->count <= 2) return;
    play_queue_shuffle_range(q, 1, q->count - 1);
    q->shuffle_generation++;
}

/* ============================================================
 * Folder / Album / Artist filtering helpers
 * ============================================================ */

static int filter_tracks_by_folder(const Playlist *playlist, int current_idx,
                                    int *out_indices, int max_out)
{
    char current_path[MAX_PATH_LEN];
    if (playlist_get_track_path(current_idx, current_path, sizeof(current_path)) != 0)
        return 0;

    char current_folder[MAX_PATH_LEN];
    const char *slash = strrchr(current_path, '/');
    if (!slash) return 0;
    size_t folder_len = (size_t)(slash - current_path);
    if (folder_len >= sizeof(current_folder)) folder_len = sizeof(current_folder) - 1;
    memcpy(current_folder, current_path, folder_len);
    current_folder[folder_len] = '\0';

    int count = 0;
    for (int i = 0; i < playlist->count && count < max_out; i++) {
        char path[MAX_PATH_LEN];
        if (playlist_get_track_path(i, path, sizeof(path)) != 0) continue;
        if (strncmp(path, current_folder, folder_len) == 0 &&
            path[folder_len] == '/') {
            out_indices[count++] = i;
        }
    }
    return count;
}

static int filter_tracks_by_same_value(const Playlist *playlist, int current_idx,
                                         int *out_indices, int max_out,
                                         void (*getter)(int idx, char *buf, size_t size))
{
    char current_val[MAX_META_LEN];
    getter(current_idx, current_val, sizeof(current_val));
    if (current_val[0] == '\0') return 0;

    int count = 0;
    for (int i = 0; i < playlist->count && count < max_out; i++) {
        char val[MAX_META_LEN];
        getter(i, val, sizeof(val));
        if (strcmp(val, current_val) == 0) {
            out_indices[count++] = i;
        }
    }
    return count;
}

static void get_album_for_index(int idx, char *buf, size_t size)
{
    Track t;
    if (get_track_metadata(idx, &t) == 0)
        strncpy(buf, t.album, size - 1);
    else
        buf[0] = '\0';
    buf[size - 1] = '\0';
}

static void get_artist_for_index(int idx, char *buf, size_t size)
{
    Track t;
    if (get_track_metadata(idx, &t) == 0)
        strncpy(buf, t.artist, size - 1);
    else
        buf[0] = '\0';
    buf[size - 1] = '\0';
}

static int filter_tracks_by_album(const Playlist *playlist, int current_idx,
                                    int *out_indices, int max_out)
{
    return filter_tracks_by_same_value(playlist, current_idx, out_indices, max_out,
                                        get_album_for_index);
}

static int filter_tracks_by_artist(const Playlist *playlist, int current_idx,
                                     int *out_indices, int max_out)
{
    return filter_tracks_by_same_value(playlist, current_idx, out_indices, max_out,
                                        get_artist_for_index);
}

/* ============================================================
 * Queue rebuild
 * ============================================================ */

void play_queue_rebuild(PlayQueue *q, const Playlist *playlist,
                        PlayMode mode, int current_track_index)
{
    if (!q || !playlist) return;

    int filtered[MAX_TRACKS];
    int filtered_count = 0;

    if (play_mode_is_folder_mode(mode) && current_track_index >= 0) {
        filtered_count = filter_tracks_by_folder(playlist, current_track_index,
                                                  filtered, MAX_TRACKS);
    } else if (play_mode_is_album_mode(mode) && current_track_index >= 0) {
        filtered_count = filter_tracks_by_album(playlist, current_track_index,
                                                 filtered, MAX_TRACKS);
    } else if (play_mode_is_artist_mode(mode) && current_track_index >= 0) {
        filtered_count = filter_tracks_by_artist(playlist, current_track_index,
                                                  filtered, MAX_TRACKS);
    } else {
        /* Basic modes: copy all indices */
        filtered_count = playlist->count;
        for (int i = 0; i < filtered_count && i < MAX_TRACKS; i++)
            filtered[i] = i;
    }

    /* Copy filtered indices to queue */
    q->count = filtered_count < MAX_TRACKS ? filtered_count : MAX_TRACKS;
    memcpy(q->indices, filtered, q->count * sizeof(int));

    if (q->count == 0) {
        q->current_position = -1;
        return;
    }

    if (play_mode_is_shuffle(mode)) {
        /* Place current track at position 0, shuffle rest */
        int found = -1;
        for (int i = 0; i < q->count; i++) {
            if (q->indices[i] == current_track_index) { found = i; break; }
        }
        if (found > 0) {
            int tmp = q->indices[0];
            q->indices[0] = q->indices[found];
            q->indices[found] = tmp;
        }
        if (q->count > 1)
            play_queue_shuffle_range(q, 1, q->count - 1);
        q->current_position = (found >= 0) ? 0 : 0;
        q->shuffle_generation = 1;
    } else {
        /* Sequential modes: find current track position */
        q->current_position = -1;
        for (int i = 0; i < q->count; i++) {
            if (q->indices[i] == current_track_index) {
                q->current_position = i;
                break;
            }
        }
        if (q->current_position < 0)
            q->current_position = 0;
        q->shuffle_generation = 0;
    }
}

/* ============================================================
 * Navigation
 * ============================================================ */

int play_queue_get_current(const PlayQueue *q)
{
    if (!q || q->count == 0 || q->current_position < 0) return -1;
    if (q->current_position >= q->count) return -1;
    return q->indices[q->current_position];
}

int play_queue_peek_next(const PlayQueue *q, PlayMode mode)
{
    if (!q || q->count == 0) return -1;

    if (mode == PLAY_MODE_SINGLE_REPEAT) {
        if (q->current_position >= 0 && q->current_position < q->count)
            return q->indices[q->current_position];
        return q->indices[0];
    }

    int next_pos = q->current_position + 1;
    if (next_pos >= q->count) {
        if (play_mode_repeats(mode))
            return q->indices[0];  /* wrap around (reshuffle handled by advance) */
        return -1;  /* stop */
    }

    return q->indices[next_pos];
}

int play_queue_peek_prev(const PlayQueue *q, PlayMode mode)
{
    if (!q || q->count == 0) return -1;

    int prev_pos = q->current_position - 1;
    if (prev_pos < 0) {
        if (play_mode_repeats(mode))
            prev_pos = q->count - 1;
        else
            prev_pos = 0;
    }

    return q->indices[prev_pos];
}

void play_queue_advance(PlayQueue *q, PlayMode mode)
{
    if (!q || q->count == 0) return;

    if (mode == PLAY_MODE_SINGLE_REPEAT) {
        /* stay at same position */
        return;
    }

    if (play_mode_is_shuffle(mode) && play_mode_repeats(mode)) {
        /* SHUFFLE_REPEAT variants: reshuffle when wrapping */
        if (q->current_position + 1 >= q->count) {
            reshuffle_keeping_current(q);
            q->current_position = 1;  /* first "next" after reshuffle */
            return;
        }
    }

    q->current_position++;
    if (q->current_position >= q->count) {
        if (play_mode_repeats(mode))
            q->current_position = 0;
        else
            q->current_position = q->count - 1;
    }
}

void play_queue_rewind(PlayQueue *q, PlayMode mode)
{
    if (!q || q->count == 0) return;

    if (mode == PLAY_MODE_SINGLE_REPEAT) {
        /* stay at same position (consistent with advance) */
        return;
    }

    q->current_position--;
    if (q->current_position < 0) {
        if (play_mode_repeats(mode))
            q->current_position = q->count - 1;
        else
            q->current_position = 0;
    }
}

/* ============================================================
 * Query helpers
 * ============================================================ */

int play_queue_get_track_at(const PlayQueue *q, int position, int *track_index)
{
    if (!q || position < 0 || position >= q->count) return -1;
    if (track_index) *track_index = q->indices[position];
    return 0;
}

int play_queue_is_active(const PlayQueue *q)
{
    return q && q->count > 0 && q->current_position >= 0;
}

/* ============================================================
 * Queue editing
 * ============================================================ */

int play_queue_append(PlayQueue *q, int track_index)
{
    if (!q || q->count >= MAX_TRACKS) return -1;
    q->indices[q->count++] = track_index;
    return 0;
}

int play_queue_remove_at(PlayQueue *q, int position)
{
    if (!q || position < 0 || position >= q->count) return -1;

    int shift = q->count - position - 1;
    if (shift > 0)
        memmove(&q->indices[position], &q->indices[position + 1], shift * sizeof(int));
    q->count--;

    if (q->count == 0) {
        q->current_position = -1;
    } else if (position < q->current_position) {
        q->current_position--;
    } else if (position == q->current_position) {
        if (q->current_position >= q->count)
            q->current_position = q->count - 1;
    }
    return 0;
}

int play_queue_insert_after(PlayQueue *q, int track_index)
{
    if (!q || q->count >= MAX_TRACKS) return -1;
    if (q->current_position < 0) {
        q->current_position = 0;
        q->indices[0] = track_index;
        q->count = 1;
        return 0;
    }
    int insert_pos = q->current_position + 1;
    if (insert_pos > q->count) insert_pos = q->count;
    int shift = q->count - insert_pos;
    if (shift > 0)
        memmove(&q->indices[insert_pos + 1], &q->indices[insert_pos], shift * sizeof(int));
    q->indices[insert_pos] = track_index;
    q->count++;
    return 0;
}

int play_queue_move_up(PlayQueue *q, int position)
{
    if (!q || position <= 0 || position >= q->count) return -1;
    int tmp = q->indices[position];
    q->indices[position] = q->indices[position - 1];
    q->indices[position - 1] = tmp;
    if (q->current_position == position)
        q->current_position--;
    else if (q->current_position == position - 1)
        q->current_position++;
    return 0;
}

int play_queue_move_down(PlayQueue *q, int position)
{
    if (!q || position < 0 || position >= q->count - 1) return -1;
    int tmp = q->indices[position];
    q->indices[position] = q->indices[position + 1];
    q->indices[position + 1] = tmp;
    if (q->current_position == position)
        q->current_position++;
    else if (q->current_position == position + 1)
        q->current_position--;
    return 0;
}

/* ============================================================
 * Queue persistence
 * ============================================================ */

int play_queue_save(const PlayQueue *q)
{
    if (!q || q->count == 0) return -1;

    char path[MAX_PATH_LEN];
    ensure_config_dir_exists();
    snprintf(path, sizeof(path), "%s/queue.txt", get_config_dir());

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "%d\n%d\n", q->current_position, q->count);
    for (int i = 0; i < q->count; i++) {
        char track_path[MAX_PATH_LEN];
        if (playlist_get_track_path(q->indices[i], track_path, sizeof(track_path)) == 0)
            fprintf(f, "%s\n", track_path);
    }
    fclose(f);
    return 0;
}

int play_queue_load(PlayQueue *q)
{
    if (!q) return -1;

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/queue.txt", get_config_dir());

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int pos, cnt;
    if (fscanf(f, "%d\n%d\n", &pos, &cnt) != 2 || cnt <= 0 || cnt > MAX_TRACKS) {
        fclose(f);
        return -1;
    }

    q->count = 0;
    q->current_position = -1;
    char line[MAX_PATH_LEN];
    for (int i = 0; i < cnt && fgets(line, sizeof(line), f); i++) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        int idx = playlist_find_track_index_by_path(line);
        if (idx >= 0)
            q->indices[q->count++] = idx;
    }
    fclose(f);

    if (q->count == 0) return -1;
    if (pos >= 0 && pos < q->count)
        q->current_position = pos;
    else
        q->current_position = 0;

    /* Rebuild shuffle generation if needed */
    q->shuffle_generation = 0;
    return 0;
}

/* ============================================================
 * Mode query helpers
 * ============================================================ */

int play_mode_is_shuffle(PlayMode mode)
{
    switch (mode) {
        case PLAY_MODE_SHUFFLE_ONCE:
        case PLAY_MODE_SHUFFLE_REPEAT:
        case PLAY_MODE_FOLDER_SHUFFLE:
        case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:
        case PLAY_MODE_ALBUM_SHUFFLE:
        case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:
        case PLAY_MODE_ARTIST_SHUFFLE:
        case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:
            return 1;
        default:
            return 0;
    }
}

int play_mode_is_folder_mode(PlayMode mode)
{
    switch (mode) {
        case PLAY_MODE_FOLDER_SEQUENTIAL:
        case PLAY_MODE_FOLDER_REPEAT:
        case PLAY_MODE_FOLDER_SHUFFLE:
        case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:
            return 1;
        default:
            return 0;
    }
}

int play_mode_is_album_mode(PlayMode mode)
{
    switch (mode) {
        case PLAY_MODE_ALBUM_SEQUENTIAL:
        case PLAY_MODE_ALBUM_REPEAT:
        case PLAY_MODE_ALBUM_SHUFFLE:
        case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:
            return 1;
        default:
            return 0;
    }
}

int play_mode_is_artist_mode(PlayMode mode)
{
    switch (mode) {
        case PLAY_MODE_ARTIST_SEQUENTIAL:
        case PLAY_MODE_ARTIST_REPEAT:
        case PLAY_MODE_ARTIST_SHUFFLE:
        case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:
            return 1;
        default:
            return 0;
    }
}

int play_mode_is_advanced(PlayMode mode)
{
    return play_mode_is_album_mode(mode) || play_mode_is_artist_mode(mode);
}

int play_mode_repeats(PlayMode mode)
{
    switch (mode) {
        case PLAY_MODE_SINGLE_REPEAT:
        case PLAY_MODE_LIST_REPEAT:
        case PLAY_MODE_SHUFFLE_REPEAT:
        case PLAY_MODE_FOLDER_REPEAT:
        case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:
        case PLAY_MODE_ALBUM_REPEAT:
        case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:
        case PLAY_MODE_ARTIST_REPEAT:
        case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:
            return 1;
        default:
            return 0;
    }
}

/* ============================================================
 * Display names
 * ============================================================ */

const char *play_mode_display_name(PlayMode mode, int use_english)
{
    if (use_english) {
        switch (mode) {
            case PLAY_MODE_SEQUENTIAL:             return "Sequential";
            case PLAY_MODE_SINGLE_REPEAT:          return "Single Repeat";
            case PLAY_MODE_LIST_REPEAT:            return "List Repeat";
            case PLAY_MODE_SHUFFLE_ONCE:           return "Shuffle Once";
            case PLAY_MODE_SHUFFLE_REPEAT:         return "Shuffle Repeat";
            case PLAY_MODE_FOLDER_SEQUENTIAL:      return "Folder Sequential";
            case PLAY_MODE_FOLDER_REPEAT:          return "Folder Repeat";
            case PLAY_MODE_FOLDER_SHUFFLE:         return "Folder Shuffle";
            case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:  return "Folder Shuffle Repeat";
            case PLAY_MODE_ALBUM_SEQUENTIAL:       return "Album Sequential";
            case PLAY_MODE_ALBUM_REPEAT:           return "Album Repeat";
            case PLAY_MODE_ALBUM_SHUFFLE:          return "Album Shuffle";
            case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:   return "Album Shuffle Repeat";
            case PLAY_MODE_ARTIST_SEQUENTIAL:      return "Artist Sequential";
            case PLAY_MODE_ARTIST_REPEAT:          return "Artist Repeat";
            case PLAY_MODE_ARTIST_SHUFFLE:         return "Artist Shuffle";
            case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:  return "Artist Shuffle Repeat";
            default:                               return "Sequential";
        }
    } else {
        switch (mode) {
            case PLAY_MODE_SEQUENTIAL:             return "顺序播放";
            case PLAY_MODE_SINGLE_REPEAT:          return "单曲循环";
            case PLAY_MODE_LIST_REPEAT:            return "列表循环";
            case PLAY_MODE_SHUFFLE_ONCE:           return "随机播放";
            case PLAY_MODE_SHUFFLE_REPEAT:         return "随机循环";
            case PLAY_MODE_FOLDER_SEQUENTIAL:      return "文件夹顺序";
            case PLAY_MODE_FOLDER_REPEAT:          return "文件夹循环";
            case PLAY_MODE_FOLDER_SHUFFLE:         return "文件夹随机";
            case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:  return "文件夹随机循环";
            case PLAY_MODE_ALBUM_SEQUENTIAL:       return "专辑顺序";
            case PLAY_MODE_ALBUM_REPEAT:           return "专辑循环";
            case PLAY_MODE_ALBUM_SHUFFLE:          return "专辑随机";
            case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:   return "专辑随机循环";
            case PLAY_MODE_ARTIST_SEQUENTIAL:      return "艺术家顺序";
            case PLAY_MODE_ARTIST_REPEAT:          return "艺术家循环";
            case PLAY_MODE_ARTIST_SHUFFLE:         return "艺术家随机";
            case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:  return "艺术家随机循环";
            default:                               return "顺序播放";
        }
    }
}

const char *play_mode_short_name(PlayMode mode, int use_english)
{
    if (use_english) {
        switch (mode) {
            case PLAY_MODE_SEQUENTIAL:             return "Seq";
            case PLAY_MODE_SINGLE_REPEAT:          return "S-Rept";
            case PLAY_MODE_LIST_REPEAT:            return "L-Rept";
            case PLAY_MODE_SHUFFLE_ONCE:           return "Shuf";
            case PLAY_MODE_SHUFFLE_REPEAT:         return "Sh-Rept";
            case PLAY_MODE_FOLDER_SEQUENTIAL:      return "Dir-Seq";
            case PLAY_MODE_FOLDER_REPEAT:          return "Dir-Rept";
            case PLAY_MODE_FOLDER_SHUFFLE:         return "Dir-Shuf";
            case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:  return "Dir-ShRpt";
            case PLAY_MODE_ALBUM_SEQUENTIAL:       return "Alb-Seq";
            case PLAY_MODE_ALBUM_REPEAT:           return "Alb-Rept";
            case PLAY_MODE_ALBUM_SHUFFLE:          return "Alb-Shuf";
            case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:   return "Alb-ShRpt";
            case PLAY_MODE_ARTIST_SEQUENTIAL:      return "Art-Seq";
            case PLAY_MODE_ARTIST_REPEAT:          return "Art-Rept";
            case PLAY_MODE_ARTIST_SHUFFLE:         return "Art-Shuf";
            case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:  return "Art-ShRpt";
            default:                               return "Seq";
        }
    } else {
        switch (mode) {
            case PLAY_MODE_SEQUENTIAL:             return "顺序";
            case PLAY_MODE_SINGLE_REPEAT:          return "单曲";
            case PLAY_MODE_LIST_REPEAT:            return "列表";
            case PLAY_MODE_SHUFFLE_ONCE:           return "随机";
            case PLAY_MODE_SHUFFLE_REPEAT:         return "随机循";
            case PLAY_MODE_FOLDER_SEQUENTIAL:      return "文顺序";
            case PLAY_MODE_FOLDER_REPEAT:          return "文循环";
            case PLAY_MODE_FOLDER_SHUFFLE:         return "文随机";
            case PLAY_MODE_FOLDER_SHUFFLE_REPEAT:  return "文随循";
            case PLAY_MODE_ALBUM_SEQUENTIAL:       return "专顺序";
            case PLAY_MODE_ALBUM_REPEAT:           return "专循环";
            case PLAY_MODE_ALBUM_SHUFFLE:          return "专随机";
            case PLAY_MODE_ALBUM_SHUFFLE_REPEAT:   return "专随循";
            case PLAY_MODE_ARTIST_SEQUENTIAL:      return "艺顺序";
            case PLAY_MODE_ARTIST_REPEAT:          return "艺循环";
            case PLAY_MODE_ARTIST_SHUFFLE:         return "艺随机";
            case PLAY_MODE_ARTIST_SHUFFLE_REPEAT:  return "艺随循";
            default:                               return "顺序";
        }
    }
}
