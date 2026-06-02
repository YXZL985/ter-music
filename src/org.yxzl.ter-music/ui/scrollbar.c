#include "../include/scrollbar.h"

int scrollbar_draw(WINDOW *win, int top, int height,
                   int total, int visible, int offset, int col) {
    if (!win || total <= visible || height <= 0) {
        return 0;
    }

    // 计算滑块大小（比例 = 可见行数 / 总行数）
    double thumb_ratio = (double)visible / total;
    int thumb_h = (int)(thumb_ratio * height);
    if (thumb_h < 1) thumb_h = 1;

    // 计算滑块位置
    int max_offset = total - visible;
    double pos = (max_offset > 0) ? (double)offset / max_offset : 0;
    int thumb_y = (int)(pos * (height - thumb_h));
    if (thumb_y + thumb_h > height)
        thumb_y = height - thumb_h;
    if (thumb_y < 0) thumb_y = 0;

    // 仅绘制滑块（█），非滑块行保持窗口背景
    for (int i = 0; i < height; i++) {
        if (i >= thumb_y && i < thumb_y + thumb_h) {
            mvwaddch(win, top + i, col, ACS_BLOCK);
        }
    }
    return 1;
}
