/*
 * 「手指松开了没有」的判定接口 —— 实现在 touchlogic.c，那里有完整的原因说明。
 * 抽出来单独放，是为了能在 PC 上用单测把所有屏的脾气都跑一遍（tools/gen/touchrelease_test.c）。
 */
#ifndef TFT_TOUCHLOGIC_H
#define TFT_TOUCHLOGIC_H

#include <stdbool.h>
#include <stdint.h>

/* 判定需要的"当前状态"（全部由 touch_read_cb 填，纯数据） */
typedef struct {
    uint32_t last_activity_ms;  /* 最后一次收到定位事件的时刻（= 手指最后一次动过的时刻） */
    bool saw_btn_touch;         /* 这块屏报过 BTN_TOUCH（含"只报按下"的情况） */
    bool btn_release_seen;      /* 见过 BTN_TOUCH=0 ⇒ 内核按键位图可信（见 touchlogic.c ②） */
    bool moved;                 /* 本次按下期间动过（> TAP_MOVE_TOL）⇒ 是滑/拖，不是按住 */
    bool ioctl_ok;              /* EVIOCGKEY 问到了（已按 btn_release_seen 过滤） */
    bool ioctl_down;            /* 位图说"现在还按着" */
} touch_release_ctx_t;

typedef enum {
    TOUCH_REL_NONE = 0,          /* 还按着，继续等 */
    TOUCH_REL_IOCTL,             /* 位图说松开了（最可信、最跟手） */
    TOUCH_REL_TIMEOUT_MOVE,      /* 超时：滑动/拖动之后没再收到事件 */
    TOUCH_REL_TIMEOUT_HOLD,      /* 超时：按住不动的上限（含"位图不可信"的屏） */
    TOUCH_REL_TIMEOUT_FAST,      /* 超时：这块屏不报 BTN_TOUCH，沿用老的 100 ms 推断 */
} touch_release_t;

touch_release_t touch_should_release(uint32_t now_ms, const touch_release_ctx_t *c);

/* 各档上限（毫秒）。⚠️ 别往小改：LVGL 长按 550 ms（短信长按删除）与 HOLD_TRIGGER_MS 900 ms
 * （"按住不动够久"）都要在这两档上限内走完。 */
#define TOUCH_GAP_FAST_MS   100u
#define TOUCH_GAP_MOVE_MS   300u
#define TOUCH_GAP_HOLD_MS  1100u

#endif /* TFT_TOUCHLOGIC_H */
