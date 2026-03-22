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
// 当前选中的控件索引 (0:上一曲，1:播放/暂停，2:下一曲，3:停止，4:循环)
int g_current_control_idx = 1;

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
 * 此处若无真实标签，则默认标题为文件名，艺术家为 "Unknown Artist"
 */
void get_audio_metadata(const char *path, char *title, char *artist, char *album) {
    // 提取文件名作为默认标题
    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    
    // 去除扩展名
    char temp_title[MAX_META_LEN];
    utf8_str_truncate(temp_title, fname, MAX_META_LEN - 1);
    char *dot = strrchr(temp_title, '.');
    if (dot) *dot = '\0';

    // 初始化默认值
    utf8_str_truncate(title, temp_title, MAX_META_LEN - 1);
    utf8_str_truncate(artist, "Unknown Artist", MAX_META_LEN - 1);
    utf8_str_truncate(album, "Unknown Album", MAX_META_LEN - 1);
    
    // 尝试从文件名中提取元数据（格式：Artist - Title）
    char *dash_pos = strstr(temp_title, " - ");
    if (dash_pos) {
        *dash_pos = '\0';
        utf8_str_truncate(artist, temp_title, MAX_META_LEN - 1);
        utf8_str_truncate(title, dash_pos + 3, MAX_META_LEN - 1);
    }
    
    // TODO: 集成 taglib_c 示例 (需链接 -ltag_c)
    /*
    TagLib_File *file = taglib_file_new(path);
    if (file && taglib_file_is_valid(file)) {
        TagLib_Tag *tag = taglib_file_tag(file);
        if (tag) {
            const char *tag_title = taglib_tag_title(tag);
            const char *tag_artist = taglib_tag_artist(tag);
            const char *tag_album = taglib_tag_album(tag);
            
            if (tag_title && strlen(tag_title) > 0) {
                utf8_str_truncate(title, tag_title, MAX_META_LEN - 1);
            }
            if (tag_artist && strlen(tag_artist) > 0) {
                utf8_str_truncate(artist, tag_artist, MAX_META_LEN - 1);
            }
            if (tag_album && strlen(tag_album) > 0) {
                utf8_str_truncate(album, tag_album, MAX_META_LEN - 1);
            }
            taglib_tag_free_strings();
        }
        taglib_file_free(file);
    }
    */
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
                utf8_str_truncate(t->path, full_path, MAX_PATH_LEN - 1);
                
                // 读取元数据
                get_audio_metadata(full_path, t->title, t->artist, t->album);
                
                g_playlist.count++;
            }
        }
    }
    closedir(dir);
    
    utf8_str_truncate(g_playlist.folder_path, path, MAX_PATH_LEN - 1);
    g_playlist.is_loaded = (g_playlist.count > 0);
    
    return g_playlist.count;
}