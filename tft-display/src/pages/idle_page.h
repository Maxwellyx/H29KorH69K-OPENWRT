#ifndef IDLE_PAGE_H
#define IDLE_PAGE_H

#include "../lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *idle_page;
void create_idle_page(lv_obj_t *main_screen);

#ifdef __cplusplus
}
#endif

#endif
