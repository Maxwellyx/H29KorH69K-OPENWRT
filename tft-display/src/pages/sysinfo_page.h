#ifndef SYSINFO_PAGE_H
#define SYSINFO_PAGE_H

#include "../lvgl/lvgl.h"

/* SYSTEM 页（竖页 5，紧跟 INFO 后面）：
 *   [SYSTEM 概览] ←→ [WIFI] ←→ [屏幕亮度]（WIFI / 亮度就是原来的两页，收进左右划）
 * 概览页显示主机概况：CPU / 内存 / 温度 / 存储 四个大数字块 + 运行时间 / 负载。
 * 数据来自 modemd 的 `sys` 推送（1 秒一条），屏幕不自己读 /proc。 */
extern lv_obj_t *sysinfo_page;

void create_sysinfo_page(lv_obj_t *main_screen);

#endif /* SYSINFO_PAGE_H */
