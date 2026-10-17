/*
 * INFO 页 —— CPU / 内存 / 温度 三个环形仪表。
 *
 * 版式（页面 320x140；页内边距已清 0 ⇒ 下面写的坐标 = 屏幕坐标 - 30）：
 *
 *   ┌─────────────────────────────────────────┐ ← 一条大块 316x132 @(2,4)
 *   │    ╭────╮       ╭────╮       ╭────╮      │
 *   │    │ 37%│       │ 52%│       │ 57 │      │
 *   │    ╰────╯       ╰────╯       ╰────╯      │
 *   │     CPU          MEM          TEMP        │
 *   └─────────────────────────────────────────┘
 *
 * 三个仪表等宽等距：格子 100 宽，x = 4 / 108 / 212（4px 缝），弧 84x84。
 *
 * 数据来源（**屏幕不读本地 /proc**，用户要求"都走 ws"）：
 *   · CPU / 内存 = 服务端 modemd 的 `sys` 推送（corev2/hostinfo.go，5 秒一条，
 *     data = {"Cpu":37,"Mem":52}；屏幕 wsdata.c 的 apply_host_info() 在消费）
 *   · 温度       = 模组温度（modemd 的 `modems` 推送里 modem_status.temp_max.celsius）
 * 没有数据时显示 0（不显示 -1 这种看着像坏了的数）。
 */

#include "../lvgl/lvgl.h"
#include "osinfo_page.h"

#include <stdio.h>
#include <string.h>
#include "../utils/wsdata.h"
lv_obj_t *osInfo_page;

/* ---- 一条大块 + 三个仪表 ---- */
#define INFO_X0   2
#define INFO_Y0   4
#define INFO_W    316
#define INFO_H    132

#define CELL_W    100     /* 每个仪表占的格子宽（弧 84 + 左右各 8） */
#define CELL_GAP  4
#define CELL_X(i) (4 + (i) * (CELL_W + CELL_GAP))   /* 4 / 108 / 212 */
#define ARC_SIZE  84
/*
 * 仪表竖向也**居中**：块内容高度 = 弧 84 + 名字（弧下 2px 起、16px 高）= 98，
 * 块高 132 ⇒ 上边距 = (132-98)/2 = 17。
 * ⚠️ 原来写 8，三个仪表全贴在块的上半部（用户指出"靠上了 要上下居中"）。
 */
#define ARC_Y     17

// Pages osinfo
typedef struct {
    lv_obj_t *arc;
    lv_obj_t *value_label;
} gauge_t;
gauge_t mem_gauge, temp_gauge, cpu_gauge;

/*
 * 建一个环形仪表：弧 + 中间大数字 + 底部名字。
 *   idx    第几个格子（0/1/2，决定 x）
 *   name   底部名字（ASCII，montserrat）
 *   color  弧的进度色 + 数字色
 *   max    弧的量程（CPU/内存 = 100；温度也按 100 画）
 */
static void create_gauge(lv_obj_t *parent, int idx, const char *name, gauge_t *gauge,
                         lv_color_t color, int max)
{
    int x = CELL_X(idx);

    lv_obj_t *arc = lv_arc_create(parent);

    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_arc_set_range(arc, 0, max);
    lv_obj_set_pos(arc, x + 8, ARC_Y);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);      /* 不要那个小圆点 */
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    /* 底弧（轨道）压暗一点，只让进度那一段亮 —— 原来是 0xDDDDCC，太抢眼 */
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x4A4A4A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);

    /* 中间大数字：宽度撑满弧内 + 居中，三个仪表的数字才不会左右跳 */
    lv_obj_t *value_label = lv_label_create(parent);

    lv_label_set_text(value_label, "0");
    lv_obj_set_width(value_label, ARC_SIZE);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(value_label, color, 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(value_label, x + 8, ARC_Y + 30);

    /* 底部名字 */
    lv_obj_t *name_label = lv_label_create(parent);

    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, ARC_SIZE);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(name_label, x + 8, ARC_Y + ARC_SIZE - 2);

    gauge->arc = arc;
    gauge->value_label = value_label;
}
void update_Osinfo_cb(lv_timer_t *timer){
    ws_snapshot_t s;

    wsdata_snapshot(&s);

    /* CPU / 内存：服务端推的百分比（没有就是 0） */
    lv_arc_set_value(cpu_gauge.arc, s.cpu);
    lv_label_set_text_fmt(cpu_gauge.value_label, "%d%%", s.cpu);

    lv_arc_set_value(mem_gauge.arc, s.mem);
    lv_label_set_text_fmt(mem_gauge.value_label, "%d%%", s.mem);

    /* 温度：模组温度；-1 = 还不知道，按 0 画、显示 0（别显示 -1 像坏了） */
    lv_arc_set_value(temp_gauge.arc, s.temp < 0 ? 0 : s.temp);
    lv_label_set_text_fmt(temp_gauge.value_label, "%d", s.temp < 0 ? 0 : s.temp);
}
void create_osinfo_page(lv_obj_t *main_screen){

    osInfo_page= lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(osInfo_page, lv_color_black(), 0);
    lv_obj_set_size(osInfo_page,320,140);//设置容器大小
    lv_obj_set_style_radius(osInfo_page,0,0);//设置容器圆角为直角
    // 创建一个样式描述符，用于存储样式信息
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);  // 设置边框宽度为0，从而去除边框
    // 将样式应用到标签上
    lv_obj_add_style(osInfo_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(osInfo_page, LV_SCROLLBAR_MODE_OFF);

    lv_obj_clear_flag(osInfo_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(osInfo_page, LV_OPA_COVER, 0);
    /* ⚠️ 清掉主题给的 20px 内边距：否则下面所有坐标都要减 20 才对得上屏幕 */
    lv_obj_set_style_pad_all(osInfo_page, 0, 0);



    // static lv_style_t style_indic;
    // lv_style_init(&style_indic);
    // lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
    // lv_style_set_bg_color(&style_indic, lv_palette_main(LV_PALETTE_RED));
    // lv_style_set_bg_grad_color(&style_indic, lv_palette_main(LV_PALETTE_BLUE));
    // lv_style_set_bg_grad_dir(&style_indic, LV_GRAD_DIR_VER);

    // // 创建并配置背景样式
    // static lv_style_t style_bg;
    // lv_style_init(&style_bg);
    // // 设置背景颜色（示例：深蓝色）
    // lv_style_set_bg_color(&style_bg, lv_color_hex(0x424242));
    // // 设置背景渐变（可选）
    // // lv_style_set_bg_grad_color(&style_bg, lv_color_hex(0x000000));
    // //lv_style_set_bg_grad_dir(&style_bg, LV_GRAD_DIR_VER);
    // // 设置圆角半径
    // lv_style_set_radius(&style_bg, 0);
    // // 设置边框宽度和颜色（可选）
    // lv_style_set_border_width(&style_bg, 0);
    // lv_style_set_border_color(&style_bg, lv_color_white());


    // //LV_FONT_DECLARE(lv_font_source_han_sans_bold_20);
    // lv_obj_t *title = lv_label_create(osInfo_page);
    // lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
    // //lv_label_set_recolor(label_MobileType, true);                      /*Enable re-coloring by commands in the text*/
    // lv_label_set_text(title, "OS");
    // lv_obj_set_width(title, 200);  /*Set smaller width to make the lines wrap*/
    // lv_obj_set_style_text_align(title, LV_ALIGN_TOP_LEFT, 0);
    // lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 5);
    // lv_obj_set_style_text_color(title, lv_color_white(), 0);
    // //lv_obj_set_style_text_color(label_MobileType, lv_color_hex(0xff3300), 0);
    // lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    // 
    /* 设置标签对象的位置 */
    //lv_obj_set_position(label_data, 10, 10);
    /* 设置标签对象的字体大小 */
    //lv_obj_set_style_text_font(label_MobileType, &lv_font_source_han_sans_bold_20, 0);
    // lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    // lv_obj_clear_flag(title, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_set_style_overflow(label_MobileType, LV_OVERFLOW_HIDDEN, 0); // 超出部分隐藏


    /* ---- 一整条块：把三个仪表都包在里面（用户要求"再包一层块"） ---- */
    lv_obj_t *info_box = lv_obj_create(osInfo_page);

    lv_obj_set_size(info_box, INFO_W, INFO_H);
    lv_obj_set_style_bg_color(info_box, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(info_box, 0, 0);
    lv_obj_set_style_pad_all(info_box, 0, 0);
    lv_obj_set_style_border_color(info_box, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(info_box, LV_OPA_30, 0);
    lv_obj_set_style_border_width(info_box, 1, 0);
    lv_obj_set_pos(info_box, INFO_X0, INFO_Y0);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 颜色沿用整套 UI 的口径：CPU 用速率那个蓝、内存用"成立"的淡绿、温度用主题橙 */
    create_gauge(info_box, 0, "CPU",  &cpu_gauge,  lv_color_hex(0x00BEFF), 100);
    create_gauge(info_box, 1, "MEM",  &mem_gauge,  lv_color_hex(0x90EE90), 100);
    create_gauge(info_box, 2, "TEMP", &temp_gauge, lv_color_hex(0xFF4500), 100);


    lv_timer_create(update_Osinfo_cb, 1000, NULL);


}
//end Pages osinfo