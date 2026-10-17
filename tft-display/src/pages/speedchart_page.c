/*
 * SPEED 的左右两个子页 —— 下行 / 上行 实时速率各一屏（"浮动报表"）。
 *
 * 用户要求：「SPEED 也要像 INFO 那样加 2 个页面」。
 *   [SPEED] ←→ [DOWN 下行速率] ←→ [UP 上行速率]（划到最后一屏再划回来）
 *
 * 版式与 INFO 那三张趋势页一致（一条块 316x132 @(2,4)）：
 *
 *   ┌─────────────────────────────────────────────┐
 *   │ DOWN                              1.50 MB/s │ ← 名字 + 当前速率（服务端给的文本，带单位）
 *   │ ╭─────────────────────────────────────────╮ │
 *   │ │        曲线（500 点 ≈ 最近 8 分钟）       │ │ ← 1 秒一个点，一进一出
 *   │ ╰─────────────────────────────────────────╯ │
 *   │ 最高 2.31 MB/s                最低 12.00 KB/s│ ← **本图窗口内**的极值
 *   └─────────────────────────────────────────────┘
 *
 * 数据：modemd 的 `traffic` 推送（`recv_bps`/`sent_bps` 是 byte/s，来自 wsdata 快照）。
 *
 * ⭐ 纵轴**自动换挡**：速率能跨好几个数量级（几十 KB/s 到十几 MB/s），固定量程要么糊成一条线、
 *    要么一直顶格。这里用"档位阶梯"（32/64/…/32768 KB/s）：窗口最大值超过档顶就升一档，
 *    掉到档顶 1/4 以下就降一档 —— 每秒最多调一档，曲线不会来回抖。
 *
 * ⚠️ 极值这里扫的是**图上那 500 个点**（"本图最高/最低"），与 INFO 那三页不同：
 *    那三页的极值是服务端累计的全时段（用户当时明确要"历史最高/最低"）。
 *    想统一成服务端口径就得给 modemd 的 Rate 加字段，说一声即可。
 */

#include "../lvgl/lvgl.h"
#include "speedchart_page.h"
#include <stdio.h>
#include <string.h>
#include "../utils/wsdata.h"

LV_FONT_DECLARE(lv_font_cjk_16);

#define CH_POINTS  500     /* 数据窗口（条），1 秒一条 ⇒ 约 8 分钟 */
#define CH_W       304
#define CH_H       68

/* 档位阶梯（KB/s） */
static const int RANGE_LADDER[] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};
#define LADDER_N ((int)(sizeof(RANGE_LADDER) / sizeof(RANGE_LADDER[0])))
#define LADDER_DEF 2       /* 默认 128 KB/s */

typedef struct {
    const char *name;
    unsigned    color;
    lv_obj_t   *page;
    lv_obj_t   *val_label;
    lv_obj_t   *max_label;
    lv_obj_t   *min_label;
    lv_obj_t   *chart;
    lv_chart_series_t *ser;
    int         rung;      /* 当前档位下标 */
} speedchart_t;

static speedchart_t g_sp[SPEEDCHART_COUNT] = {
    { "DOWN", 0x00BEFF, NULL, NULL, NULL, NULL, NULL, NULL, LADDER_DEF },
    { "UP",   0xFF4500, NULL, NULL, NULL, NULL, NULL, NULL, LADDER_DEF },
};

lv_obj_t *speedchart_page(int idx)
{
    if (idx < 0 || idx >= SPEEDCHART_COUNT)
        return NULL;
    return g_sp[idx].page;
}

/* byte/s → "12.34 KB/s" / "1.50 MB/s" / "512 B/s"（与 wsdata 那套口径一致） */
static void fmt_rate(char *buf, size_t cap, double bps)
{
    double kb = (bps < 0 ? 0 : bps) / 1024.0;

    if (kb >= 1024.0)
        snprintf(buf, cap, "%.2f MB/s", kb / 1024.0);
    else if (kb >= 1.0)
        snprintf(buf, cap, "%.2f KB/s", kb);
    else
        snprintf(buf, cap, "%d B/s", (int)(bps < 0 ? 0 : bps));
}

static void mk_page(lv_obj_t *main_screen, speedchart_t *sc)
{
    sc->page = lv_obj_create(main_screen);
    lv_obj_set_size(sc->page, 320, 140);
    lv_obj_set_style_bg_color(sc->page, lv_color_black(), 0);
    lv_obj_set_style_radius(sc->page, 0, 0);
    lv_obj_set_style_border_width(sc->page, 0, 0);
    lv_obj_set_scrollbar_mode(sc->page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(sc->page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sc->page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(sc->page, 0, 0);   /* 清掉主题 20px 内边距 */

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

    lv_obj_t *name = lv_label_create(box);

    lv_label_set_text(name, sc->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_pos(name, 10, 6);

    sc->val_label = lv_label_create(box);
    lv_obj_set_style_text_font(sc->val_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sc->val_label, lv_color_hex(sc->color), 0);
    lv_label_set_text(sc->val_label, "0 B/s");
    lv_obj_align(sc->val_label, LV_ALIGN_TOP_RIGHT, -10, 6);

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
    lv_chart_set_range(sc->chart, LV_CHART_AXIS_PRIMARY_Y, 0, RANGE_LADDER[sc->rung]);
    sc->ser = lv_chart_add_series(sc->chart, lv_color_hex(sc->color), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < CH_POINTS; i++)
        lv_chart_set_next_value(sc->chart, sc->ser, 0);

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

/* 两个页面共用一个定时器（同一份快照，分开跑白费 CPU） */
static void update_speedchart_cb(lv_timer_t *t)
{
    ws_snapshot_t s;
    double bps[2];
    const char *txt[2];
    char b1[32], b2[32];

    (void)t;
    wsdata_snapshot(&s);

    /* 下行 = 接收，上行 = 发送 */
    bps[0] = s.recv_bps;
    txt[0] = s.recv_rate;
    bps[1] = s.sent_bps;
    txt[1] = s.sent_rate;

    for (int i = 0; i < SPEEDCHART_COUNT; i++) {
        speedchart_t *sc = &g_sp[i];
        int kb = (int)((bps[i] < 0 ? 0 : bps[i]) / 1024.0);
        int mx = 0, mn = -1;
        int *y;

        if (kb > 1000000)
            kb = 1000000;
        lv_chart_set_next_value(sc->chart, sc->ser, kb);

        lv_label_set_text(sc->val_label, (txt[i] && txt[i][0]) ? txt[i] : "0 B/s");

        /* 扫窗口算极值（⚠️ 未写过的点是 LV_CHART_POINT_NONE = INT32_MAX，得跳过） */
        y = lv_chart_get_series_y_array(sc->chart, sc->ser);
        if (y) {
            for (int k = 0; k < CH_POINTS; k++) {
                int v = y[k];

                if (v < 0 || v > 1000000)
                    continue;
                if (v > mx)
                    mx = v;
                if (mn < 0 || v < mn)
                    mn = v;
            }
        }

        /* 自动换挡：只在窗口里有真实速率时才动，避免刚启动（全是 0）就降到底档 */
        if (mx > 0) {
            if (mx > RANGE_LADDER[sc->rung] && sc->rung < LADDER_N - 1)
                sc->rung++;
            else if (sc->rung > 0 && mx < RANGE_LADDER[sc->rung] / 4)
                sc->rung--;
            lv_chart_set_range(sc->chart, LV_CHART_AXIS_PRIMARY_Y, 0, RANGE_LADDER[sc->rung]);
        }

        fmt_rate(b1, sizeof(b1), (double)mx * 1024.0);
        fmt_rate(b2, sizeof(b2), (double)(mn < 0 ? 0 : mn) * 1024.0);
        lv_label_set_text_fmt(sc->max_label, "最高 %s", b1);
        lv_label_set_text_fmt(sc->min_label, "最低 %s", b2);
    }
}

void create_speedchart_pages(lv_obj_t *main_screen)
{
    for (int i = 0; i < SPEEDCHART_COUNT; i++)
        mk_page(main_screen, &g_sp[i]);

    lv_timer_create(update_speedchart_cb, 1000, NULL);
}
