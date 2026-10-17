/*
 * 趋势页（INFO 的左右子页）—— CPU / 内存 / 温度 各一屏，显示"浮动报表"。
 *
 * 用户要求：「左右划 添加 3 个页面 CPU 内存 温度 的数据推送过来显示浮动报表 历史最高和最低，
 * 每个页面一个」。
 *
 * 三屏结构完全一样（只是取的数字和颜色不同），所以**一份代码建三个实例**：
 *
 *   ┌─────────────────────────────────────────────┐ ← 一条大块 316x132 @(2,4)
 *   │ CPU                                    3%   │ ← 名字 + 当前值（1 秒一更新）
 *   │ ╭─────────────────────────────────────────╮ │
 *   │ │        曲线（60 点，1 点/秒 ≈ 最近 1 分钟）│ │ ← 浮动报表：旧点从左边滚出去
 *   │ ╰─────────────────────────────────────────╯ │
 *   │ 最高 37                          最低 1     │ ← 服务端累计的极值（类型页也一样）
 *   └─────────────────────────────────────────────┘
 *
 * 数据：modemd 的 `sys` 推送（1 秒一条，见 corev2/hostinfo.go）：
 *   {"Cpu":10,"Mem":20,"Temp":63,"CpuMin":1,"CpuMax":37,...}
 * 极值由**服务端**累计（自 modemd 启动起算），屏幕只负责画 ——
 * 这样屏幕重启也不会把"历史最高/最低"清零。
 *
 * ⚠️ 三屏共用**一个**定时器（不是每屏一个）：值都来自同一份快照，分开跑只是白费 CPU。
 */

#include "../lvgl/lvgl.h"
#include "syschart_page.h"
#include <stdio.h>
#include <string.h>
#include "../utils/wsdata.h"

/* 只含本页用到的汉字（最高/最低…）的 16px 子集，与 NET/INFO 页共用同一个字体文件 */
LV_FONT_DECLARE(lv_font_cjk_16);

/*
 * 曲线数据窗口：**只保留 500 条**（用户要求"数据只保留 500 条，之前的就移除，一进一出"）。
 * 1 秒一条 ⇒ 500 条 ≈ 最近 8 分钟；`lv_chart_set_next_value` 本来就是定长环形缓冲，
 * 存满之后再进一个新点就自动挤掉最旧的那个 ✓
 * （500 点 > 304px 宽，LVGL 会自己走 crowded 的竖线画法，不影响性能。）
 */
#define CH_POINTS  500     /* 数据窗口（条） */
#define CH_W       304
#define CH_H       68

typedef struct {
    const char *name;      /* 页面里显示的名字（ASCII） */
    unsigned    color;     /* 当前值 + 曲线颜色 */
    bool        percent;   /* true = 值后面带 %（CPU / 内存） */
    lv_obj_t   *page;
    lv_obj_t   *val_label;
    lv_obj_t   *max_label;
    lv_obj_t   *min_label;
    lv_obj_t   *chart;
    lv_chart_series_t *ser;
} syschart_t;

static syschart_t g_sc[3] = {
    { "CPU",  0x00BEFF, true,  NULL, NULL, NULL, NULL, NULL, NULL },
    { "MEM",  0x90EE90, true,  NULL, NULL, NULL, NULL, NULL, NULL },
    { "TEMP", 0xFF4500, false, NULL, NULL, NULL, NULL, NULL, NULL },
};

lv_obj_t *syschart_page(int idx)
{
    if (idx < 0 || idx >= SYSCHART_COUNT)
        return NULL;
    return g_sc[idx].page;
}

/* 值 → 文本（CPU/内存带 %，温度是 ℃ 只写数字） */
static void verdict(char *buf, size_t cap, const syschart_t *sc, int v)
{
    if (sc->percent)
        snprintf(buf, cap, "%d%%", v);
    else
        snprintf(buf, cap, "%d", v);
}

/* 极值 → 文本；两项都还没上报（温度那对是 0）就显示 "-" */
static void verdict_minmax(char *buf, size_t cap, const syschart_t *sc, int v)
{
    if (v <= 0 && !sc->percent)
        snprintf(buf, cap, "-");
    else
        verdict(buf, cap, sc, v);
}

static void mk_page(lv_obj_t *main_screen, syschart_t *sc)
{
    sc->page = lv_obj_create(main_screen);
    lv_obj_set_size(sc->page, 320, 140);
    lv_obj_set_style_bg_color(sc->page, lv_color_black(), 0);
    lv_obj_set_style_radius(sc->page, 0, 0);
    lv_obj_set_style_border_width(sc->page, 0, 0);
    lv_obj_set_scrollbar_mode(sc->page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(sc->page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sc->page, LV_OPA_COVER, 0);
    /* ⚠️ 清掉主题给的 20px 内边距，下面坐标就是屏幕坐标 */
    lv_obj_set_style_pad_all(sc->page, 0, 0);

    lv_obj_t *box = lv_obj_create(sc->page);

    lv_obj_set_size(box, 316, 132);
    lv_obj_set_style_bg_color(box, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_border_color(box, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_30, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_pos(box, 2, 4);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* 名字（左） + 当前值（右） */
    lv_obj_t *name = lv_label_create(box);

    lv_label_set_text(name, sc->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_pos(name, 10, 6);

    sc->val_label = lv_label_create(box);
    lv_obj_set_style_text_font(sc->val_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sc->val_label, lv_color_hex(sc->color), 0);
    lv_label_set_text(sc->val_label, "-");
    lv_obj_align(sc->val_label, LV_ALIGN_TOP_RIGHT, -10, 4);

    /* 曲线：线 1px、点 2x2（与 SPEED 页同一口径，别用主题默认的粗线大点） */
    sc->chart = lv_chart_create(box);
    lv_obj_set_size(sc->chart, CH_W, CH_H);
    lv_obj_set_pos(sc->chart, 6, 34);
    lv_obj_set_style_bg_color(sc->chart, lv_color_make(50, 50, 50), 0);
    lv_obj_set_style_border_width(sc->chart, 0, 0);
    lv_obj_set_style_line_color(sc->chart, lv_color_make(100, 100, 100), LV_PART_MAIN);
    lv_obj_set_style_line_opa(sc->chart, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_line_width(sc->chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_width(sc->chart, 2, LV_PART_INDICATOR);
    lv_obj_set_style_height(sc->chart, 2, LV_PART_INDICATOR);
    lv_chart_set_type(sc->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(sc->chart, CH_POINTS);
    lv_chart_set_range(sc->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    sc->ser = lv_chart_add_series(sc->chart, lv_color_hex(sc->color), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < CH_POINTS; i++)
        lv_chart_set_next_value(sc->chart, sc->ser, 0);

    /* 底部：历史最高 / 最低（服务端累计的极值） */
    sc->max_label = lv_label_create(box);
    lv_obj_set_style_text_font(sc->max_label, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(sc->max_label, lv_color_hex(0xA0A0A0), 0);
    lv_label_set_text(sc->max_label, "最高 -");
    lv_obj_align(sc->max_label, LV_ALIGN_BOTTOM_LEFT, 10, -8);

    sc->min_label = lv_label_create(box);
    lv_obj_set_style_text_font(sc->min_label, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(sc->min_label, lv_color_hex(0xA0A0A0), 0);
    lv_label_set_text(sc->min_label, "最低 -");
    lv_obj_align(sc->min_label, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
}

/* 一个定时器刷三屏（值都来自同一份快照） */
static void update_syschart_cb(lv_timer_t *t)
{
    ws_snapshot_t s;
    char buf[32];
    int v[3], vmin[3], vmax[3];

    (void)t;
    wsdata_snapshot(&s);

    v[0] = s.cpu;
    v[1] = s.mem;
    v[2] = (s.temp < 0) ? 0 : s.temp;
    vmin[0] = s.cpu_min;  vmax[0] = s.cpu_max;
    vmin[1] = s.mem_min;  vmax[1] = s.mem_max;
    vmin[2] = s.temp_min; vmax[2] = s.temp_max;

    for (int i = 0; i < SYSCHART_COUNT; i++) {
        syschart_t *sc = &g_sc[i];

        /* 1 秒加一个点（推送就是 1 秒一条）⇒ 曲线从左边滚出去，看着像浮动的报表 */
        if (v[i] < 0)
            v[i] = 0;
        if (v[i] > 100)
            v[i] = 100;
        lv_chart_set_next_value(sc->chart, sc->ser, v[i]);

        verdict(buf, sizeof(buf), sc, v[i]);
        lv_label_set_text(sc->val_label, buf);

        verdict_minmax(buf, sizeof(buf), sc, vmax[i]);
        lv_label_set_text_fmt(sc->max_label, "最高 %s", buf);
        verdict_minmax(buf, sizeof(buf), sc, vmin[i]);
        lv_label_set_text_fmt(sc->min_label, "最低 %s", buf);
    }
}

void create_syschart_pages(lv_obj_t *main_screen)
{
    for (int i = 0; i < SYSCHART_COUNT; i++)
        mk_page(main_screen, &g_sc[i]);

    lv_timer_create(update_syschart_cb, 1000, NULL);
}
