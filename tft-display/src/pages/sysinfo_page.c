/*
 * SYSTEM 页（竖页 5，紧跟 INFO 后面）—— 主机概况 + 本机设置。
 *
 *   [SYSTEM 概览] ←→ [WIFI] ←→ [屏幕亮度]      （左右划；上下划换竖页）
 *
 * 为什么把 WIFI / 亮度收进来（用户要求）：它们是"本机设置"，跟主机概况同一类；
 * 上下划的竖页少两页，翻起来更快（顶层标题栏仍然是统一的 SYSTEM）。
 *
 * 概览页（**只读**，数据全来自 modemd 的 `sys` 推送，1 秒一条）：
 *   ┌─────────────────────────────────────────────┐
 *   │ ┌────┐ ┌────┐ ┌────┐ ┌────┐                  │
 *   │ │ 3% │ │21% │ │61℃ │ │42% │                  │
 *   │ │CPU │ │内存│ │温度│ │存储│                  │
 *   │ └────┘ └────┘ └────┘ └────┘                  │
 *   │ 运行时间 2 天 03:15    负载 0.77              │
 *   └─────────────────────────────────────────────┘
 * 版式刻意和短信主页一样（四个大数字块 + 两个宽块）：一眼能读完，也不占地方。
 */

#include "../lvgl/lvgl.h"
#include "sysinfo_page.h"
#include <stdio.h>
#include <string.h>
#include "../utils/wsdata.h"

static lv_obj_t *lbl_num[4];     /* CPU / 内存 / 温度 / 存储 的大数字 */
static lv_obj_t *lbl_name[4];
static lv_obj_t *lbl_box[4];
static lv_obj_t *lbl_uptime;
static lv_obj_t *lbl_load;

lv_obj_t *sysinfo_page;          /* 页对象（main.c 通过头文件 extern 取用） */

/* 中文标签用的 16px 全字库（GB2312，见 utils/lv_font_cjk_sms_16.c）。
 * ⚠️ 不用其它页那个几十字的子集字库（lv_font_cjk_16）：它没有"内存/温度/存储/运行/负载/℃"
 *    这几个字，屏上会显示成空方框（踩过）。那个库要用得先重新生成。 */
LV_FONT_DECLARE(lv_font_cjk_sms_16);
#define SYS_FONT (&lv_font_cjk_sms_16)

#define SYS_BLK_Y   12
#define SYS_BLK_W   74
#define SYS_BLK_H   66
#define SYS_BLK_N   4
#define SYS_WIDE_Y  90
#define SYS_WIDE_H  30
#define SYS_WIDE_W  150

/* 秒 → "1 天 03:15" / "03:15"（小时补零，分钟补零） */
static void fmt_uptime(int sec, char *out, size_t cap)
{
    int d, h, m;

    if (sec <= 0) {
        snprintf(out, cap, "运行时间 -");
        return;
    }
    d = sec / 86400;
    h = (sec % 86400) / 3600;
    m = (sec % 3600) / 60;
    if (d > 0)
        snprintf(out, cap, "运行 %d 天 %02d:%02d", d, h, m);
    else
        snprintf(out, cap, "运行 %02d:%02d", h, m);
}

static void update_sysinfo_cb(lv_timer_t *t)
{
    static int shown_cpu = -1, shown_mem = -1, shown_temp = -1, shown_disk = -1;
    static int shown_up = -1, shown_load = -1;
    ws_snapshot_t s;
    char buf[48];

    (void)t;
    wsdata_snapshot(&s);

    /* 只在数字真的变了时才写标签（LVGL 每次都重画，1 秒一次足够） */
    if (s.cpu != shown_cpu || s.mem != shown_mem || s.temp != shown_temp ||
        s.disk != shown_disk) {
        shown_cpu = s.cpu;
        shown_mem = s.mem;
        shown_temp = s.temp;
        shown_disk = s.disk;
        snprintf(buf, sizeof(buf), "%d%%", s.cpu);
        lv_label_set_text(lbl_num[0], buf);
        snprintf(buf, sizeof(buf), "%d%%", s.mem);
        lv_label_set_text(lbl_num[1], buf);
        if (s.temp > 0)
            snprintf(buf, sizeof(buf), "%d", s.temp);
        else
            snprintf(buf, sizeof(buf), "-");
        lv_label_set_text(lbl_num[2], buf);
        if (s.disk > 0)
            snprintf(buf, sizeof(buf), "%d%%", s.disk);
        else
            snprintf(buf, sizeof(buf), "-");
        lv_label_set_text(lbl_num[3], buf);
    }
    if (s.uptime != shown_up) {
        shown_up = s.uptime;
        fmt_uptime(s.uptime, buf, sizeof(buf));
        lv_label_set_text(lbl_uptime, buf);
    }
    if (s.load != shown_load) {
        shown_load = s.load;
        snprintf(buf, sizeof(buf), "负载 %d.%02d", s.load / 100, s.load % 100);
        lv_label_set_text(lbl_load, buf);
    }
}

static lv_obj_t *mk_block(lv_obj_t *parent, int x, int y, int w, int h, unsigned bg)
{
    lv_obj_t *b = lv_obj_create(parent);

    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, unsigned color,
                          int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_obj_set_height(l, 17);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_pos(l, x, y);
    return l;
}

void create_sysinfo_page(lv_obj_t *main_screen)
{
    /* ⚠️ 单位只有 % 的能放在大数字上（montserrat 有 '%'）；**℃ 放在中文标签里** ——
     * 数字用的是 montserrat_24，它没有 U+2103，写成 "65℃" 屏上是个空方框（踩过）。 */
    static const char *names[SYS_BLK_N] = { "CPU", "内存", "温度℃", "存储" };
    static const unsigned colors[SYS_BLK_N] = { 0x00BEFF, 0x90EE90, 0xFF4500, 0xC8A0FF };

    sysinfo_page = lv_obj_create(main_screen);
    lv_obj_set_size(sysinfo_page, 320, 140);
    lv_obj_set_style_bg_color(sysinfo_page, lv_color_black(), 0);
    lv_obj_set_style_radius(sysinfo_page, 0, 0);
    lv_obj_set_style_border_width(sysinfo_page, 0, 0);
    lv_obj_set_style_pad_all(sysinfo_page, 0, 0);
    lv_obj_set_style_bg_opa(sysinfo_page, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(sysinfo_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(sysinfo_page, LV_OBJ_FLAG_SCROLLABLE);

    /* 四个大数字块：和短信主页一个样式（数字 24 号 + 两字标签） */
    for (int i = 0; i < SYS_BLK_N; i++) {
        lv_obj_t *b = mk_block(sysinfo_page, 5 + i * 77, SYS_BLK_Y, SYS_BLK_W, SYS_BLK_H, 0x1E1E1E);

        lbl_box[i] = b;
        lbl_num[i] = mk_label(b, &lv_font_montserrat_24, colors[i], 0, 12, 0);
        lv_label_set_text(lbl_num[i], "-");
        lv_obj_align(lbl_num[i], LV_ALIGN_TOP_MID, 0, 12);

        lbl_name[i] = mk_label(b, SYS_FONT, 0x9A9A9A, 0, 0, 0);
        lv_label_set_text(lbl_name[i], names[i]);
        lv_obj_align(lbl_name[i], LV_ALIGN_BOTTOM_MID, 0, -6);
    }

    /* 下面两个宽块：运行时间 / 负载 */
    {
        lv_obj_t *b1 = mk_block(sysinfo_page, 5, SYS_WIDE_Y, SYS_WIDE_W, SYS_WIDE_H, 0x1E1E1E);

        lbl_uptime = mk_label(b1, SYS_FONT, 0xD0D0D0, 0, 0, 0);
        lv_label_set_text(lbl_uptime, "运行 -");
        lv_obj_align(lbl_uptime, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *b2 = mk_block(sysinfo_page, 160, SYS_WIDE_Y, SYS_WIDE_W, SYS_WIDE_H, 0x1E1E1E);

        lbl_load = mk_label(b2, SYS_FONT, 0xD0D0D0, 0, 0, 0);
        lv_label_set_text(lbl_load, "负载 -");
        lv_obj_align(lbl_load, LV_ALIGN_CENTER, 0, 0);
    }

    lv_timer_create(update_sysinfo_cb, 1000, NULL);
}
