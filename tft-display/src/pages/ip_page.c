/*
 * ⚠️ 这一页（标题 NETIP：本机 IP + MAC）**当前不再被创建** ——
 * 用户要求「NETIP 那边修改成 NET，里面内容用块显示」，已由 `netparam_page.c`
 * （网络参数：制式/频段/频点/载波聚合/PLMN/MCC·MNC/服务小区/邻区 + 一行 IP）取代。
 * 源码保留备用：要恢复就在 main.c 里改回 `#include "pages/ip_page.h"` +
 * `create_ip_page(main_screen)` + switch_to_page 的 case 3。
 */

#include "../lvgl/lvgl.h"
#include "ip_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "../utils/wsdata.h"
// #include "../utils/gif_player.h"


lv_obj_t *IP_page;
lv_obj_t *MAC_cmn;
lv_obj_t *IPS_cmn;

// 全局变量（保留你的定义，调整为合理大小）


static void update_info_cb(lv_timer_t *t) {
    ws_snapshot_t s;

    wsdata_snapshot(&s);

    /* 多个 IP 已按行分好（wsdata 里就是 '\n' 分隔），直接贴 */
    lv_label_set_text(IPS_cmn, s.ips[0] ? s.ips : "------");
    lv_label_set_text(MAC_cmn, s.mac[0] ? s.mac : "--:--:--:--:--:--");
}

void create_ip_page(lv_obj_t *main_screen){
    IP_page= lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(IP_page, lv_color_black(), 0);
    lv_obj_set_size(IP_page,320,140);//设置容器大小
    lv_obj_set_style_radius(IP_page,0,0);//设置容器圆角为直角

    // 创建一个样式描述符，用于存储样式信息
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);  // 设置边框宽度为0，从而去除边框
    // 将样式应用到标签上
    lv_obj_add_style(IP_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(IP_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(IP_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(IP_page, LV_OPA_COVER, 0);




    lv_obj_t *main_container = lv_obj_create(IP_page);
    lv_obj_set_size(main_container, 280, 110);
    lv_obj_set_style_bg_color(main_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 0, 0);
    lv_obj_set_style_border_color(main_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(main_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(main_container, 1, 0);
    lv_obj_align(main_container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

    // 容器
    lv_obj_t *mac_container = lv_obj_create(main_container);
    lv_obj_set_size(mac_container, 280, 30);
    lv_obj_set_style_bg_color(mac_container, lv_color_make(40, 89, 40), 0);
    lv_obj_set_style_radius(mac_container, 0, 0);
    lv_obj_set_style_pad_all(mac_container, 0, 0);
    lv_obj_set_style_border_color(mac_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(mac_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(mac_container, 1, 0);
    lv_obj_align(mac_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(mac_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *MAC_label = lv_label_create(mac_container);
    lv_label_set_text(MAC_label, "MAC");
    lv_obj_set_style_text_color(MAC_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(MAC_label, &lv_font_montserrat_14, 0);
    lv_obj_align(MAC_label, LV_ALIGN_LEFT_MID, 5, 0);

    // 创建标签（保持原有代码）
    IPS_cmn = lv_label_create(main_container);
    lv_label_set_text(IPS_cmn, "------");
    lv_obj_set_style_text_color(IPS_cmn, lv_color_white(), 0);
    lv_obj_set_style_text_font(IPS_cmn, &lv_font_montserrat_12, 0);
    lv_obj_align(IPS_cmn, LV_ALIGN_TOP_LEFT, 10, 10);

    // ------------------- LVGL 9.1 适配：超出隐藏核心设置 -------------------
    // 1. 启用父容器裁剪（关键！子对象超出父容器部分会被隐藏）
    // lv_obj_set_clip_self(main_container, true); // 父容器自身启用裁剪
    // lv_obj_set_clip_children(main_container, true); // 父容器裁剪子对象（核心）

    // 2. 限制标签尺寸（与父容器一致，避免标签无限制扩展）
    // lv_coord_t parent_w = lv_obj_get_width(main_container);
    // lv_coord_t parent_h = lv_obj_get_height(main_container);
    // lv_obj_set_size(IPS_cmn, parent_w, parent_h); // 标签尺寸 = 父容器尺寸

// // 3. 标签文本对齐（可选，避免文本因偏移显示异常）
// lv_label_set_align(IPS_cmn, LV_LABEL_ALIGN_LEFT);

    MAC_cmn = lv_label_create(mac_container);
    lv_label_set_text(MAC_cmn, "--:--:--:--:--:--");
    lv_obj_set_style_text_color(MAC_cmn, lv_color_white(), 0);
    lv_obj_set_style_text_font(MAC_cmn, &lv_font_montserrat_14, 0);
    lv_obj_align(MAC_cmn, LV_ALIGN_RIGHT_MID, -5, 0);


    // lv_label_set_long_mode(IP_label, LV_LABEL_LONG_SCROLL_CIRCULAR); // 循环滚动（推荐）
    // lv_obj_set_width(IP_label, 320); 
    // lv_obj_set_style_scroll_speed(IP_label, 25, 0); // 滚动速度（数值越大越快，默认约 20）
    // lv_obj_set_style_scroll_delay(IP_label, 1000, 0); // 初始延迟 1 秒后开始滚动（默认 0）

    lv_timer_create(update_info_cb, 100, NULL);
}


//end Pages home