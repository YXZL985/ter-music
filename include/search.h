#ifndef SEARCH_H
#define SEARCH_H

#include "defs.h"

// 执行搜索：遍历所有曲目，填充 g_search_state
void perform_search(const char *query);

// 交互式搜索提示（启动字符输入循环，适用于主页 playlist 搜索）
void search_prompt(void);

// 清除搜索状态
void search_clear(void);

// 在文本行数组中搜索匹配项（供帮助页面等复用）
// lines: 文本行数组, line_count: 行数, query: 查询字符串
// results: 输出匹配行索引数组, max_results: 数组容量
// 返回匹配行数
int search_lines(const char **lines, int line_count, const char *query,
                 int *results, int max_results);

#endif
