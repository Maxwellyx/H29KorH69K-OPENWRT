/*
 * 「手指松开了没有」的判定 —— 纯逻辑，不依赖 LVGL / ioctl，宿主机就能单测
 * （tools/gen/touchrelease_test.c，见那里的用例）。
 *
 * 背景（2026-10-08 用户两次报障，都是这一处的锅）：
 *   第一次："别人装后划一下要等 3-5 秒才能再划"。原因是**有些屏不给抬手信号**，
 *          而代码里给"报过 BTN_TOUCH 的屏"留了 **3000 ms** 超时兜底 ⇒ 手指抬起后
 *          `last_pressed` 还要当 3 秒 PRESSED，而 LVGL 的手势必须见到松手才算数
 *          ⇒ 3 秒 + 手势冷却 500 ms ≈ 3.5~4 秒。
 *   第二次："改完更糟，划一下直接卡住"。原因是我把那 3 秒换成了**无条件相信
 *          EVIOCGKEY 的按键位图**：位图说"还按着"就无限期保持 PRESSED。可**只报按下、
 *          不报抬起的驱动**会让这个位**永远停在 1**（内核只有在收到 BTN_TOUCH=0 时才清它）
 *          ⇒ PRESSED 永不解除 ⇒ 手势永远发不出来 ⇒ 界面看起来卡死。
 *
 * 所以现在的规矩（按可信度从高到低）：
 *   ① 驱动自己报的抬手事件（BTN_TOUCH=0 / ABS_MT_TRACKING_ID=-1 / SYN_DROPPED）
 *      —— 在 touch_read_cb 里直接处理掉，根本轮不到这里；
 *   ② EVIOCGKEY 位图 —— **只在确认过这块屏会报 BTN_TOUCH=0 之后才信**
 *      （`btn_release_seen`）。这种屏位图非常准，松手当场就能判出来，最跟手；
 *      位图说"还按着"时也只是**保持**，仍然受 ③ 的上限约束，不可能无限期卡住；
 *   ③ 超时 —— 一定会有结论的上限，按"这块屏的脾气"分三档：
 *        · 不报 BTN_TOUCH 的屏：沿用老的 100 ms（这类屏本来就很快）；
 *        · 报 BTN_TOUCH 且手指**动过**（滑动/拖动）：300 ms（手势就等这一下，必须快）；
 *        · 报 BTN_TOUCH 但手指**没动**（点按/按住）：1100 ms
 *          —— 不能更小：LVGL 长按 550 ms（短信长按删除）和 HOLD_TRIGGER_MS 900 ms
 *             （"按住不动够久"那条）都要在这段时间里走完。
 *
 * ⭐ 不变量（单测里就是验它）：**任何输入组合下，按住状态最多维持 TOUCH_GAP_HOLD_MS（1100 ms）
 *    就一定会被要求松手** ⇒ 界面不可能再"卡住"，最坏只是慢 1.1 秒。
 */
#include "touchlogic.h"

/* 这块屏不报 BTN_TOUCH：老的 100 ms 推断（这些屏抬手本来就干脆，别把它拖慢） */
static uint32_t fast_gap_ms(void) { return TOUCH_GAP_FAST_MS; }

touch_release_t touch_should_release(uint32_t now_ms, const touch_release_ctx_t *c)
{
    uint32_t gap;

    if (!c)
        return TOUCH_REL_NONE;

    /* ② 位图可信 + 位图说"现在没按着" ⇒ 立刻松手（最跟手的一条） */
    if (c->btn_release_seen && c->ioctl_ok && !c->ioctl_down)
        return TOUCH_REL_IOCTL;

    if (!c->saw_btn_touch)
        return (now_ms - c->last_activity_ms > fast_gap_ms())
                   ? TOUCH_REL_TIMEOUT_FAST : TOUCH_REL_NONE;

    /* ③ 超时上限。注意这两档都是**上限**：位图说还按着也照样到点松手（防卡死）。 */
    if (c->moved) {
        gap = TOUCH_GAP_MOVE_MS;
        return (now_ms - c->last_activity_ms > gap)
                   ? TOUCH_REL_TIMEOUT_MOVE : TOUCH_REL_NONE;
    }

    gap = TOUCH_GAP_HOLD_MS;
    return (now_ms - c->last_activity_ms > gap)
               ? TOUCH_REL_TIMEOUT_HOLD : TOUCH_REL_NONE;
}
