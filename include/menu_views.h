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

/**
 * @brief 初始化菜单视图模块
 * 
 * 配置 ncurses 功能键支持，初始化全局状态
 */
void init_menu_views(void);

/**
 * @brief 渲染菜单视图框架
 * 
 * 绘制左右布局的外框和标题
 * 
 * @param title 视图标题
 */
void render_menu_frame(const char *title);

/**
 * @brief 渲染左侧菜单栏
 * 
 * 显示所有菜单项并高亮选中的项目
 * 
 * @param selected_idx 当前选中的菜单项索引
 */
void render_menu_sidebar(int selected_idx);

/**
 * @brief 处理菜单模式下的输入
 * 
 * 处理方向键、ESC、Enter 等导航输入
 * 
 * @param ch 输入的字符或键值
 */
void handle_menu_input(int ch);

/**
 * @brief 切换到指定视图
 * 
 * @param view 目标视图模式
 */
void switch_to_view(ViewMode view);

/**
 * @brief 退出当前视图返回主界面
 */
void exit_current_view(void);

/**
 * @brief 渲染设置界面内容
 */
void render_settings_content(void);

/**
 * @brief 渲染历史记录界面内容
 */
void render_history_content(void);

/**
 * @brief 渲染收藏夹界面内容
 */
void render_favorites_content(void);

/**
 * @brief 渲染信息界面内容
 */
void render_info_content(void);

/**
 * @brief 处理功能键（F1-F6）
 * 
 * @param fkey 功能键键值
 */
void handle_function_keys(int fkey);

/**
 * @brief 添加历史记录
 * 
 * @param track 歌曲信息
 */
void add_history_entry(Track *track);

/**
 * @brief 加载历史记录（从持久化存储）
 */
void load_history(void);

/**
 * @brief 保存历史记录（到持久化存储）
 */
void save_history(void);

/**
 * @brief 加载收藏夹（从持久化存储）
 */
void load_favorites(void);

/**
 * @brief 保存收藏夹（到持久化存储）
 */
void save_favorites(void);

/**
 * @brief 添加到收藏夹
 * 
 * @param track 歌曲信息
 * @return int 0=成功，-1=已满
 */
int add_to_favorites(Track *track);

/**
 * @brief 从收藏夹移除
 * 
 * @param index 收藏夹索引
 * @return int 0=成功，-1=索引无效
 */
int remove_from_favorites(int index);

#endif /* MENU_VIEWS_H */
