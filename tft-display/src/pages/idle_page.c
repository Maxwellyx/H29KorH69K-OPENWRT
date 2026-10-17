/*
 * SYSTEM → 息屏配置页（用户 2026-10-07 要求）：
 *   "在这个左右划后面再加一个 UI 叫息屏配置：1 分钟 / 3 分钟 / 5 分钟 / 不息屏，点击也要提示框确认"
 *
 * 全部本地实现：
 *   · 超时值是 main.c 里那个 `g_idle_sleep_ms`（无操作计时在触摸层，真关屏在 brightness_page.c），
 *     这里只负责**读写**它：`tft_get_idle_ms()` / `tft_set_idle_ms()`；
 *   · 顺手持久化到 /etc/tftdisplay.idle（**包外**文件，opkg 升级不覆盖；
 *     跟 /etc/tftdisplay.brightness 一个套路），重启后还是用户选的那档；
 *   · 点挡位**不当场生效**：先弹顶层二次确认框（跟 lan/wifi/brightness 页同一套做法）。
 *
 * ⚠️ 弹窗/挡位文案都是中文 ⇒ 必须用全字库 `lv_font_cjk_sms_16`（montserrat 没有 CJK 字形）。
 */
#include "../lvgl/lvgl.h"
#include "idle_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

lv_obj_t *idle_page;

/* main.c 提供：读写息屏超时（毫秒，0 = 不息屏） */
extern uint32_t tft_get_idle_ms(void);
extern void     tft_set_idle_ms(uint32_t ms);

LV_FONT_DECLARE(lv_font_cjk_sms_16);
#define IDLE_FONT (&lv_font_cjk_sms_16)

/* 四挡：1 / 3 / 5 分钟 + 永不（0） */
#define IDLE_LEVELS 4
static const uint32_t idle_ms[IDLE_LEVELS] = { 60000, 180000, 300000, 0 };
static const char    *idle_txt[IDLE_LEVELS] = { "1分钟", "3分钟", "5分钟", "不息屏" };

static lv_obj_t *level_btn[IDLE_LEVELS];
static lv_obj_t *level_lbl[IDLE_LEVELS];

/* ---- 二次确认弹窗（挂顶层，默认隐藏）---- */
static lv_obj_t *conf_pop, *conf_title, *conf_text, *conf_ok;
static int       conf_idx;               /* 待确认的挡位序号（-1 = 没待确认） */

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *mk_text(lv_obj_t *parent, const char *text, uint32_t color,
                         int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, IDLE_FONT, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    return l;
}

/* 块按钮（必须带 GESTURE_BUBBLE，否则在按钮上左右划换不了页） */
static lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t bg, lv_event_cb_t cb, void *user,
                           lv_obj_t **out_label, const char *text)
{
    lv_obj_t *b = lv_obj_create(parent);

    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

    lv_obj_t *l = mk_text(b, text, 0xFFFFFF, 0, 0, 0);

    lv_obj_center(l);
    if (out_label) *out_label = l;
    return b;
}

/* 当前值落在哪一挡（找不到就返回 -1） */
static int current_index(void)
{
    uint32_t ms = tft_get_idle_ms();

    for (int i = 0; i < IDLE_LEVELS; i++)
        if (idle_ms[i] == ms) return i;
    return -1;
}

/* 把四个挡位的高亮刷一遍（当前那挡青色，其余灰） */
static void refresh_level_ui(void)
{
    int cur = current_index();

    for (int i = 0; i < IDLE_LEVELS; i++) {
        bool on = (i == cur);

        lv_obj_set_style_bg_color(level_btn[i],
                                  lv_color_hex(on ? 0x00A0B0 : 0x2A2A2A), 0);
        lv_obj_set_style_text_color(level_lbl[i],
                                    lv_color_hex(on ? 0xFFFFFF : 0xA0A0A0), 0);
    }
}

/* ------------------------------------------------------------------ */
/* 二次确认弹窗                                                        */
/* ------------------------------------------------------------------ */

static void conf_hide(void)
{
    conf_idx = -1;
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

static void conf_open(int idx)
{
    char t[48], b[64];
    int cur = current_index();

    conf_idx = idx;
    if (idle_ms[idx] == 0)
        snprintf(t, sizeof(t), "不再自动息屏？");
    else
        snprintf(t, sizeof(t), "息屏时间设为 %s？", idle_txt[idx]);
    snprintf(b, sizeof(b), "取消则保持现在的 %s",
             (cur >= 0 && idle_ms[cur] != 0) ? idle_txt[cur] : "不息屏");

    lv_label_set_text(conf_title, t);
    lv_label_set_text(conf_text, b);
    lv_obj_set_style_bg_color(conf_ok, lv_color_hex(0x1D5A66), 0);
    lv_obj_move_foreground(conf_pop);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

static void conf_ok_cb(lv_event_t *e)
{
    int idx;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    idx = conf_idx;
    conf_hide();                            /* 先收框再真改 */
    if (idx >= 0) {
        tft_set_idle_ms(idle_ms[idx]);
        refresh_level_ui();
    }
}

static void conf_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    conf_hide();
    refresh_level_ui();
}

/* 挡位按钮：先弹确认（不当场改） */
static void level_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (idx < 0 || idx >= IDLE_LEVELS) return;
    conf_open(idx);
}

static void idle_refresh_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    /* 弹窗挂着时若页面被划走了（顶栏还能滑动）⇒ 收掉，别留在别的页上 */
    if (conf_pop && !lv_obj_has_flag(conf_pop, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_is_visible(idle_page))
        conf_hide();

    refresh_level_ui();
}

/* ------------------------------------------------------------------ */
/* 建页                                                                */
/* ------------------------------------------------------------------ */

void create_idle_page(lv_obj_t *main_screen)
{
    idle_page = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(idle_page, lv_color_black(), 0);
    lv_obj_set_size(idle_page, 320, 140);
    lv_obj_set_style_radius(idle_page, 0, 0);
    lv_obj_set_style_border_width(idle_page, 0, 0);
    lv_obj_set_scrollbar_mode(idle_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(idle_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(idle_page, LV_OPA_COVER, 0);
    /* ⚠️ 主题默认 20px 内边距会把里面的东西整体推偏（其它页都踩过） */
    lv_obj_set_style_pad_all(idle_page, 0, 0);

    /* 标题 + 说明：用户要求"UI 上写一个标签说明这个功能是什么" */
    mk_text(idle_page, "息屏配置", 0xE6E6E6, 10, 10, 0);
    mk_text(idle_page, "无操作后自动黑屏，点屏幕亮屏", 0x8A8A8A, 10, 34, 300);

    /* 四个挡位（y 与亮度页那排对齐，看着一致）：1 / 3 / 5 分钟 / 不息屏 */
    for (int i = 0; i < IDLE_LEVELS; i++) {
        level_btn[i] = mk_button(idle_page, 7 + i * 77, 78, 73, 46, 0x2A2A2A,
                                 level_btn_cb, (void *)(intptr_t)i,
                                 &level_lbl[i], idle_txt[i]);
    }

    /* ---------- 二次确认弹窗（几何跟页面对象完全对齐 320x140 @0,30） ---------- */
    conf_pop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(conf_pop, 320, 140);
    lv_obj_set_pos(conf_pop, 0, 30);
    lv_obj_set_style_bg_color(conf_pop, lv_color_make(26, 26, 26), 0);
    lv_obj_set_style_bg_opa(conf_pop, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(conf_pop, 0, 0);
    lv_obj_set_style_border_width(conf_pop, 0, 0);
    lv_obj_set_style_pad_all(conf_pop, 0, 0);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(conf_pop, conf_cancel_cb, LV_EVENT_CLICKED, NULL);  /* 点空白 = 取消 */

    conf_title = mk_text(conf_pop, "确认？", 0xFFC24D, 14, 12, 0);
    conf_text  = mk_text(conf_pop, "", 0xD0D0D0, 14, 46, 292);
    conf_ok    = mk_button(conf_pop, 0, 94, 160, 46, 0x1D5A66,
                           conf_ok_cb, NULL, NULL, "确认");
    mk_button(conf_pop, 160, 94, 160, 46, 0x3A3A3A,
              conf_cancel_cb, NULL, NULL, "取消");

    refresh_level_ui();
    lv_timer_create(idle_refresh_cb, 1000, NULL);

    /* 调试钩子：TFT_IDLE_CONFTEST=1 ⇒ 开屏就弹确认框（拍屏核对排版用） */
    if (getenv("TFT_IDLE_CONFTEST"))
        conf_open(1);                       /* 3 分钟 */
}
