#ifndef SYSCHART_PAGE_H
#define SYSCHART_PAGE_H

#include "../lvgl/lvgl.h"

/* INFO 页的左右子页：CPU / 内存 / 温度 各一屏的"浮动报表"（曲线 + 历史最高/最低）。
 * 数据来自 modemd 的 sys 推送；极值由服务端累计。 */
#define SYSCHART_COUNT 3   /* 0 = CPU，1 = 内存，2 = 温度 */

/* 取第 idx 屏（给 main.c 的 page_obj_at 用）；越界回 NULL */
lv_obj_t *syschart_page(int idx);

void create_syschart_pages(lv_obj_t *main_screen);

#endif /* SYSCHART_PAGE_H */
