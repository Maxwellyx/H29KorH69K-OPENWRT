#ifndef SMSPAGE_H
#define SMSPAGE_H

#include "../lvgl/lvgl.h"

/* SMS 页（竖页 2，紧跟 Mobile 后面）：
 *   [短信列表] ←→ [第 1 条(最新)] ←→ [第 2 条] ←→ … ←→ [余额查询]
 *   （列数 = 2 + 短信条数，**动态**）
 * 数据来自 modemd 的 sms 推送；长短信（UDH 拼接）服务端已合并成一条。
 * 有新短信时弹一个提示框，点一下跳到那条短信的详情页。 */
#define SMSPAGE_PAGE_INDEX 2

lv_obj_t *smspage_list(void);      /* 列表页对象（第 0 屏） */
lv_obj_t *smspage_detail(void);    /* 详情页对象（第 1..N 屏共用同一个，内容随屏切换） */
lv_obj_t *smspage_query(void);     /* 余额查询屏对象（最后那一屏，按卡类型发查询短信） */
int       smspage_msg_count(void); /* 当前可划的短信屏数（= 条数，已夹到上限） */
void      smspage_show(int idx);   /* 详情页切到第 idx 条（0 = 最新） */
/* 弹出"删除这条短信？"确认框（主界面检测到"两指同点 / 连点两下"时调它）。
 * ⚠️ 没显示着短信（不在详情页）时什么也不做，可以随便调。 */
void      smspage_open_delete(void);

void create_smspage_pages(lv_obj_t *main_screen);

#endif /* SMSPAGE_H */
