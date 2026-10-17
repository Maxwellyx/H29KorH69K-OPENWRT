#ifndef NETPARAM_PAGE_H
#define NETPARAM_PAGE_H

#include "../lvgl/lvgl.h"

/* NET 页（网络参数：制式 / 频段 / 频点 / 载波聚合 / PLMN / MCC·MNC / 服务小区 / 邻区 + IP）
 * 数据全部来自 wsdata 的快照（modemd 的 network 推送），屏幕自己不发 AT。 */
extern lv_obj_t *netparam_page;

void create_netparam_page(lv_obj_t *main_screen);

#endif /* NETPARAM_PAGE_H */
