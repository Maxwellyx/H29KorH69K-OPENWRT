#ifndef SPEEDCHART_PAGE_H
#define SPEEDCHART_PAGE_H

#include "../lvgl/lvgl.h"

/* SPEED 页的左右两个子页：下行 / 上行 实时速率各一屏的"浮动报表"
 * （曲线 + 本图窗口内的最高/最低）。数据来自 modemd 的 traffic 推送。 */
#define SPEEDCHART_COUNT 2   /* 0 = 下行（接收），1 = 上行（发送） */

/* 取第 idx 屏（给 main.c 的 page_obj_at 用）；越界回 NULL */
lv_obj_t *speedchart_page(int idx);

void create_speedchart_pages(lv_obj_t *main_screen);

#endif /* SPEEDCHART_PAGE_H */
