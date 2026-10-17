#ifndef PRIGHTNESS_PAGE_H
#define PRIGHTNESS_PAGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
extern lv_obj_t *Brightness_page;
void create_brightness_page(lv_obj_t *main_screen);

/* ---- 息屏 / 亮屏（用户 2026-10-07 要求：不操作 3 分钟息屏、点屏幕亮屏） ----
 * 无操作的计时在 main.c 的触摸层（那里才知道有没有人碰屏），这两个函数只负责
 * 真的关屏 / 开屏；息屏前那一档会记下来，亮屏时恢复。
 * ⚠️ 亮屏不只由触摸触发 —— 息屏时来新短信也要亮（用户 2026-10-08 要求），
 *    所以多一个带原因的版本，`why` 只进日志（“触摸” / “新短信”…）。 */
void brightness_screen_sleep(void);
void brightness_screen_wake(void);                 /* = _ex("触摸") */
void brightness_screen_wake_ex(const char *why);
bool brightness_screen_is_sleeping(void);
#ifdef __cplusplus
}
#endif

#endif