#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>
#include <locale.h>
#include <pthread.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>

const char *audio_extensions[] = {
    ".mp3", ".MP3", ".flac", ".FLAC", ".wav", ".WAV", 
    ".ogg", ".OGG", ".m4a", ".M4A", ".aac", ".AAC",
    ".wma", ".WMA", ".ape", ".APE", ".opus", ".OPUS",
    NULL
};

// 全局变量定义
Playlist g_playlist = {0};
int g_selected_index = 0;  // 当前选中的歌曲索引

// 控制区焦点状态
// 0: 列表模式 (List), 1: 控制模式 (Control)
int g_control_focus = 0; 
// 当前选中的控件索引 (0:上一曲，1:播放/暂停，2:下一曲，3:停止，4:循环，5:音量，6:进度条)
int g_current_control_idx = 1;

static void copy_metadata_field(char *dest, size_t dest_size, const char *value) {
    if (!dest || dest_size == 0) {
        return;
    }

    if (!value || value[0] == '\0') {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%s", value);
    decode_html_entities(dest);
}

static const char *find_metadata_tag(AVDictionary *primary,
                                     AVDictionary *secondary,
                                     const char *const *keys,
                                     int key_count) {
    for (int i = 0; i < key_count; i++) {
        if (primary) {
            AVDictionaryEntry *entry = av_dict_get(primary, keys[i], NULL, 0);
            if (entry && entry->value && entry->value[0] != '\0') {
                return entry->value;
            }
        }

        if (secondary) {
            AVDictionaryEntry *entry = av_dict_get(secondary, keys[i], NULL, 0);
            if (entry && entry->value && entry->value[0] != '\0') {
                return entry->value;
            }
        }
    }

    return NULL;
}

static void fill_metadata_from_filename(const char *path, char *title, char *artist, char *album) {
    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;

    char temp_title[MAX_META_LEN];
    utf8_str_truncate(temp_title, fname, MAX_META_LEN - 1);
    char *dot = strrchr(temp_title, '.');
    if (dot) {
        *dot = '\0';
    }

    utf8_str_truncate(title, temp_title, MAX_META_LEN - 1);
    utf8_str_truncate(artist, "未知艺术家", MAX_META_LEN - 1);
    utf8_str_truncate(album, "未知专辑", MAX_META_LEN - 1);

    char *dash_pos = strstr(temp_title, " - ");
    if (dash_pos) {
        *dash_pos = '\0';
        utf8_str_truncate(artist, temp_title, MAX_META_LEN - 1);
        utf8_str_truncate(title, dash_pos + 3, MAX_META_LEN - 1);
    }

    decode_html_entities(title);
    decode_html_entities(artist);
    decode_html_entities(album);
}

void decode_html_entities(char *str) {
    if (!str) {
        return;
    }

    char *read = str;
    char *write = str;

    while (*read) {
        if (*read == '&') {
            if (strncmp(read, "&amp;", 5) == 0) {
                *write++ = '&';
                read += 5;
                continue;
            }
            if (strncmp(read, "&lt;", 4) == 0) {
                *write++ = '<';
                read += 4;
                continue;
            }
            if (strncmp(read, "&gt;", 4) == 0) {
                *write++ = '>';
                read += 4;
                continue;
            }
            if (strncmp(read, "&quot;", 6) == 0) {
                *write++ = '"';
                read += 6;
                continue;
            }
            if (strncmp(read, "&apos;", 6) == 0) {
                *write++ = '\'';
                read += 6;
                continue;
            }
            if (strncmp(read, "&#39;", 5) == 0) {
                *write++ = '\'';
                read += 5;
                continue;
            }
            if (strncmp(read, "&nbsp;", 6) == 0) {
                *write++ = ' ';
                read += 6;
                continue;
            }
        }

        *write++ = *read++;
    }

    *write = '\0';
}

/**
 * 检查文件是否为支持的音频格式
 * 通过文件扩展名判断
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
 * 从文件路径提取基本元数据信息
 * 注意：实际项目中应在此处调用 taglib 或其他库读取 ID3 标签
 * 此处若无真实标签，则默认标题为文件名，艺术家为“未知艺术家”
 */
void get_audio_metadata(const char *path, char *title, char *artist, char *album) {
    static const char *const title_keys[] = {"title", "TITLE"};
    static const char *const artist_keys[] = {"artist", "ARTIST", "album_artist", "ALBUM_ARTIST"};
    static const char *const album_keys[] = {"album", "ALBUM"};

    fill_metadata_from_filename(path, title, artist, album);

    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, path, NULL, NULL) != 0) {
        return;
    }

    AVDictionary *stream_meta = NULL;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_meta = fmt_ctx->streams[i]->metadata;
            break;
        }
    }

    const char *tag_title = find_metadata_tag(stream_meta, fmt_ctx->metadata,
                                              title_keys, (int)(sizeof(title_keys) / sizeof(title_keys[0])));
    const char *tag_artist = find_metadata_tag(stream_meta, fmt_ctx->metadata,
                                               artist_keys, (int)(sizeof(artist_keys) / sizeof(artist_keys[0])));
    const char *tag_album = find_metadata_tag(stream_meta, fmt_ctx->metadata,
                                              album_keys, (int)(sizeof(album_keys) / sizeof(album_keys[0])));

    if (tag_title) {
        copy_metadata_field(title, MAX_META_LEN, tag_title);
    }
    if (tag_artist) {
        copy_metadata_field(artist, MAX_META_LEN, tag_artist);
    }
    if (tag_album) {
        copy_metadata_field(album, MAX_META_LEN, tag_album);
    }

    avformat_close_input(&fmt_ctx);
}

/**
 * 加载指定路径下的音频文件到播放列表
 * 扫描目录，过滤支持的音频文件，并提取元数据
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
                snprintf(t->path, sizeof(t->path), "%s", full_path);
                
                // 读取元数据
                get_audio_metadata(full_path, t->title, t->artist, t->album);
                
                g_playlist.count++;
            }
        }
    }
    closedir(dir);
    
    snprintf(g_playlist.folder_path, sizeof(g_playlist.folder_path), "%s", path);
    g_playlist.is_loaded = (g_playlist.count > 0);
    
    return g_playlist.count;
}
