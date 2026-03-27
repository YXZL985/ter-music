/**
 * @file menu_views.h
 * @brief 菜单视图模块头文件
 * 
 * 提供底部菜单栏功能，支持 F1-F6 功能键切换不同界面
 * 包括：设置、历史记录、播放列表、收藏夹、信息、退出
 * 
 * @author 燕戏竹林 (yxzl666xx@outlook.com)
 * @date 2026-03-22
 */

#ifndef MENU_VIEWS_H
#define MENU_VIEWS_H

#include "defs.h"

void init_menu_views(void);
void render_menu_frame(const char *title);
void render_menu_sidebar(int selected_idx, const char **items, int item_count);
void handle_menu_input(int ch);
void switch_to_view(ViewMode view);
void exit_current_view(void);

void render_settings_content(void);
void render_history_content(void);
void render_favorites_content(void);
void render_info_content(void);
void render_playlist_manager_content(void);

void handle_function_keys(int fkey);

void add_history_entry(Track *track);
void load_history(void);
void save_history(void);

void load_favorites(void);
void save_favorites(void);

int add_to_favorites(Track *track);
int remove_from_favorites(int index);

void add_dir_history_entry(const char *path);
void load_dir_history(void);
void save_dir_history(void);
void clear_dir_history(void);

void load_config(void);
void save_config(void);
void init_default_config(void);

void load_all_playlists(void);
void save_all_playlists(void);
int create_user_playlist(const char *name);
int delete_user_playlist(int index);
int add_track_to_playlist(int playlist_idx, Track *track);
int remove_track_from_playlist(int playlist_idx, int track_idx);
int rename_user_playlist(int index, const char *new_name);

void ensure_config_dir_exists(void);
void init_all_persistent_data(void);

void show_status_message(const char *msg);

void render_menu_hint_bar(void);

#endif
