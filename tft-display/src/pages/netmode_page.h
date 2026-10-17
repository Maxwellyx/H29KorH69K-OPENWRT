#ifndef NETMODE_PAGE_H
#define NETMODE_PAGE_H

#include "../lvgl/lvgl.h"

/* Mobile 竖页的第三个横屏：**组网模式偏好**（4G / 5G / 4G+5G）。
 *   Mobile ←→ NET 网络参数 ←→ 组网模式 */
extern lv_obj_t *netmode_page;

void create_netmode_page(lv_obj_t *parent);

/* 显示这一屏时调一次：问服务端要当前值（服务端带 TTL 缓存，不会把串口刷满）。 */
void netmode_page_refresh(void);

#endif /* NETMODE_PAGE_H */
