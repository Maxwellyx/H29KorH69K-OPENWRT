#ifndef LUNAR_PAGE_H
#define LUNAR_PAGE_H

#include "../lvgl/lvgl.h"

/* TIME 竖页的第二个横屏：农历（TIME ←→ 农历，左右划切换）。
 * 页面自己每秒刷一次（值没变不重画），外面不用管。 */
extern lv_obj_t *lunar_page;

void create_lunar_page(lv_obj_t *parent);

#endif /* LUNAR_PAGE_H */
