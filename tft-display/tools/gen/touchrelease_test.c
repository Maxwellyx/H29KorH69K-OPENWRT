/*
 * 松手判定（src/utils/touchlogic.c）的宿主机单测 —— 直接在 PC 上跑，不需要屏幕。
 *
 * 编译（和其它 hosttest 一样，用 zig 自带的 cc，fake pthread.h 在 tools/gen/hosttest/）：
 *   cd d:\Go\tftdisplay\tools\gen
 *   & <zig> cc -I hosttest -I ..\..\src\utils -o %TEMP%\tr.exe touchrelease_test.c ..\..\src\utils\touchlogic.c
 *   %TEMP%\tr.exe
 *
 * 为什么值得写：这块逻辑被用户报障两次（3-5 秒卡顿 / 直接卡死），而它依赖的
 * "屏上报什么事件"在 PC 上没法复现 —— 只能把各种屏的脾气写成用例，保证不变量成立：
 *   ⭐ 任何组合下，按住状态最多 TOUCH_GAP_HOLD_MS（1100 ms）就一定会被要求松手。
 */
#include <stdio.h>
#include "touchlogic.h"

static int fails, checks;

static const char *rel_name(touch_release_t r)
{
    switch (r) {
    case TOUCH_REL_NONE:         return "hold";
    case TOUCH_REL_IOCTL:        return "release/bitmap";
    case TOUCH_REL_TIMEOUT_MOVE: return "release/timeout-move";
    case TOUCH_REL_TIMEOUT_HOLD: return "release/timeout-hold";
    case TOUCH_REL_TIMEOUT_FAST: return "release/timeout-fast";
    }
    return "?";
}

static void expect(touch_release_t got, touch_release_t want, const char *what)
{
    checks++;
    if (got != want) {
        fails++;
        printf("  [FAIL] %-44s got %s, want %s\n", what, rel_name(got), rel_name(want));
    } else {
        printf("  [ ok ] %-44s %s\n", what, rel_name(got));
    }
}

/* 一块"典型"屏的状态：MK(最后活动时刻, now 占位, 报 BTN_TOUCH?, 见过抬手?, 动过?,
 *                        ioctl 可用?, 位图说按着?)
 * 用**复合字面量**（C99 里是左值）⇒ 可以 &MK(...) 直接传地址；没写的字段自动为 0。 */
#define MK(la, nw, sb, rs, mv, io, id) \
    ((touch_release_ctx_t){ .last_activity_ms = (la), .saw_btn_touch = (sb), \
                            .btn_release_seen = (rs), .moved = (mv), \
                            .ioctl_ok = (io), .ioctl_down = (id) })

int main(void)
{
    printf("== touchlogic (松手判定) unit test ==\n");

    /* ① 屏不报 BTN_TOUCH（这些屏本来靠 100 ms 推断就很快） */
    printf("\n[1] panel without BTN_TOUCH: keep the old 100 ms path\n");
    expect(touch_should_release(1000, &MK(950, 1000, false, false, false, false, false)),
           TOUCH_REL_NONE, "idle 50ms  -> still pressed");
    expect(touch_should_release(1000, &MK(880, 1000, false, false, false, false, false)),
           TOUCH_REL_TIMEOUT_FAST, "idle 120ms -> released");

    /* ② 位图可信的屏（既报按下也报抬起）：抬手当场判出来，最跟手 */
    printf("\n[2] trustworthy key bitmap + bitmap says up -> instant release\n");
    expect(touch_should_release(1000, &MK(995, 1000, true, true, true, true, false)),
           TOUCH_REL_IOCTL, "bitmap=0 right after a drag");

    /* ③ 位图可信、但位图说还按着 ⇒ 保持（长按 550 ms / 按住 900 ms 靠它） */
    printf("\n[3] trustworthy bitmap + bitmap says down -> hold, capped at 1100 ms\n");
    expect(touch_should_release(1000, &MK(400, 1000, true, true, false, true, true)),
           TOUCH_REL_NONE, "hold 600ms (> long-press 550ms)");
    expect(touch_should_release(1200, &MK(0, 1200, true, true, false, true, true)),
           TOUCH_REL_TIMEOUT_HOLD, "hold 1200ms -> over cap");

    /* ④ ⚠️ 只报按下、不报抬起（位图**永远**是 1）—— 上一版就是在这里把 UI 卡死的 */
    printf("\n[4] press-only panel (bitmap stuck at 1) -> must release by timeout\n");
    {
        uint32_t i;
        bool ever_released = false;

        for (i = 1; i <= 20000; i++) {
            if (touch_should_release(i, &MK(0, i, true, false, false, false, false))
                != TOUCH_REL_NONE) {
                ever_released = true;
                break;
            }
        }
        expect(ever_released ? TOUCH_REL_TIMEOUT_HOLD : TOUCH_REL_NONE,
               TOUCH_REL_TIMEOUT_HOLD, "released within 20s (invariant)");
        expect(touch_should_release(TOUCH_GAP_HOLD_MS,
                                    &MK(0, TOUCH_GAP_HOLD_MS, true, false, false, false, false)),
               TOUCH_REL_NONE, "exactly 1100ms -> still hold");
        expect(touch_should_release(TOUCH_GAP_HOLD_MS + 1,
                                    &MK(0, TOUCH_GAP_HOLD_MS + 1, true, false, false, false, false)),
               TOUCH_REL_TIMEOUT_HOLD, "1101ms -> released");
    }

    /* ⑤ 滑动（moved）必须快：手势就等这一下 */
    printf("\n[5] finger that moved: release after 300 ms\n");
    expect(touch_should_release(1000, &MK(750, 1000, true, false, true, false, false)),
           TOUCH_REL_NONE, "250ms after last move");
    expect(touch_should_release(1000, &MK(650, 1000, true, false, true, false, false)),
           TOUCH_REL_TIMEOUT_MOVE, "350ms after last move");

    /* ⑥ 卡死回归测试：位图说按着也必须有上限 */
    printf("\n[6] regression: nothing can stay pressed forever\n");
    expect(touch_should_release(5000, &MK(0, 5000, true, true, true, true, true)),
           TOUCH_REL_TIMEOUT_MOVE, "bitmap down + moved -> 300ms cap");
    expect(touch_should_release(5000, &MK(0, 5000, true, true, false, true, true)),
           TOUCH_REL_TIMEOUT_HOLD, "bitmap down + still   -> 1100ms cap");

    printf("\n== %d checks, %d failed ==\n", checks, fails);
    return fails ? 1 : 0;
}
