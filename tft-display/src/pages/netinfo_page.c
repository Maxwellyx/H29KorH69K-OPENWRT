#include "../lvgl/lvgl.h"
#include "netinfo_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include "../utils/wsdata.h"

lv_obj_t *netinfo_page;

// 界面配置（保持不变）
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 130
#define CHART_WIDTH 120
#define CHART_HEIGHT 50
/*
 * 曲线数据窗口：**只保留 500 条**（与 INFO 的三张趋势页同一口径，用户要求"一进一出"）。
 * 更新节拍 1 秒（见文件末尾的 lv_timer_create）⇒ 500 条 ≈ 最近 8 分钟。
 */
#define DATA_POINTS 500
#define NETWORK_STAT_FILE "/sys/class/net/%s/statistics/%s_bytes"

/*
 * 版式（页面 320x140；这里把页面自身的内边距清了 0 ⇒ 下面写的坐标就是屏幕坐标减 30）：
 *
 *   列1 x=2　列2 x=161，列宽 157（左右各留 2px、中间 2px 缝）——**两列均分，不再左右留白**
 *   y   2..28   上行/下行「累计流量」两条（157x26）
 *   y  30..138  上/下行「实时速率」两块（157x108）：里面 = 标题 + 大数字 + 曲线
 *
 * 曲线：线宽 **1px**、圆点 **2x2**（用户反馈默认线太粗、点又大，15 个点挤在一起"看着粘在一起"）。
 */
#define COL_X0   2      /* 左列 */
#define COL_X1   161    /* 右列（2 + 157 + 2） */
#define COL_W    157
#define ROW1_Y   2      /* 累计流量那一条 */
#define ROW1_H   26
#define ROW2_Y   30     /* 实时速率那一块（含曲线） */
#define ROW2_H   108

// 全局变量（保持不变）
static lv_obj_t *download_label;
static lv_obj_t *download_label2;
static lv_obj_t *upload_label;
static lv_obj_t *upload_label2;
static lv_obj_t *download_chart;
static lv_obj_t *upload_chart;
static lv_chart_series_t *download_series;
static lv_chart_series_t *upload_series;
static lv_obj_t *network_status_label;
static lv_obj_t *status_indicator;

// 数据全部来自 WebSocket 快照（见 utils/wsdata.h）





static void update_network_speed(lv_timer_t *t) {
    ws_snapshot_t s;
    unsigned long long s1;  // 接收速率（MB/s）
    unsigned long long s2;  // 发送速率（MB/s）

    wsdata_snapshot(&s);

    lv_label_set_text(network_status_label, s.dev[0] ? s.dev : "NETERR");
    lv_label_set_text(upload_label, s.sent_rate[0] ? s.sent_rate : "0 KB/s");
    lv_label_set_text(download_label, s.recv_rate[0] ? s.recv_rate : "0 KB/s");

    lv_label_set_text(upload_label2, s.total_sent[0] ? s.total_sent : "0");
    lv_label_set_text(download_label2, s.total_recv[0] ? s.total_recv : "0");

    // 关键：B/s 转 MB/s（除以 1024×1024）
    s1 = (unsigned long long)(s.recv_bps / 1024.0 / 1024.0);
    s2 = (unsigned long long)(s.sent_bps / 1024.0 / 1024.0);

    // 添加数据点到图表
    lv_chart_set_next_value(download_chart, download_series, s1 > 2048 ? 2048 : s1);
    lv_chart_set_next_value(upload_chart, upload_series, s2 > 512 ? 512 : s2);
}



// 创建页面函数（保持不变）
void create_netinfo_page(lv_obj_t *main_screen){

    netinfo_page= lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(netinfo_page, lv_color_black(), 0);
    lv_obj_set_size(netinfo_page,SCREEN_WIDTH,140);
    lv_obj_set_style_radius(netinfo_page,0,0);
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);
    lv_obj_add_style(netinfo_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(netinfo_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(netinfo_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(netinfo_page, LV_OPA_COVER, 0);
    /* ⚠️ 清掉主题给的 20px 内边距：否则下面所有坐标都要减 20 才对得上屏幕 */
    lv_obj_set_style_pad_all(netinfo_page, 0, 0);

    // 下载容器（左列：实时下行速率 + 曲线）
    lv_obj_t *download_container = lv_obj_create(netinfo_page);
    lv_obj_set_size(download_container, COL_W, ROW2_H);
    lv_obj_set_pos(download_container, COL_X0, ROW2_Y);
    lv_obj_set_style_bg_color(download_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(download_container, 1, 0);
    lv_obj_set_style_pad_all(download_container, 1, 0);
    lv_obj_set_style_border_color(download_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(download_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(download_container, 1, 0);

    // 上传容器（右列：实时上行速率 + 曲线）
    lv_obj_t *upload_container = lv_obj_create(netinfo_page);
    lv_obj_set_size(upload_container, COL_W, ROW2_H);
    lv_obj_set_pos(upload_container, COL_X1, ROW2_Y);
    lv_obj_set_style_bg_color(upload_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(upload_container, 1, 0);
    lv_obj_set_style_pad_all(upload_container, 1, 0);
    lv_obj_set_style_border_color(upload_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(upload_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(upload_container, 1, 0);

    // 标题和标签（保持不变）
    lv_obj_t *download_title = lv_label_create(download_container);
    lv_label_set_text(download_title, "D/Speed");
    lv_obj_set_style_text_color(download_title, lv_color_make(200, 200, 200), 0);
    lv_obj_align(download_title, LV_ALIGN_TOP_LEFT, 5, 5);

    lv_obj_t *upload_title = lv_label_create(upload_container);
    lv_label_set_text(upload_title, "U/Speed");
    lv_obj_set_style_text_color(upload_title, lv_color_make(200, 200, 200), 0);
    lv_obj_align(upload_title, LV_ALIGN_TOP_LEFT, 5, 5);

    download_label = lv_label_create(download_container);
    lv_label_set_text(download_label, "0 KB/s");
    lv_obj_set_style_text_color(download_label, lv_color_make(0, 190, 255), 0);
    lv_obj_set_style_text_font(download_label, &lv_font_montserrat_20, 0);
    lv_obj_align(download_label, LV_ALIGN_TOP_LEFT, 7, 25);

    upload_label = lv_label_create(upload_container);
    lv_label_set_text(upload_label, "0 KB/s");
    lv_obj_set_style_text_color(upload_label, lv_color_make(255, 100, 0), 0);
    lv_obj_set_style_text_font(upload_label, &lv_font_montserrat_20, 0);
    lv_obj_align(upload_label, LV_ALIGN_TOP_LEFT, 7, 25);

    // 下行曲线：铺满列宽，线细一点
    download_chart = lv_chart_create(download_container);
    lv_obj_set_size(download_chart, COL_W - 12, 52);
    lv_obj_align(download_chart, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(download_chart, lv_color_make(50, 50, 50), 0);
    lv_obj_set_style_border_width(download_chart, 0, 0);
    lv_obj_set_style_line_color(download_chart, lv_color_make(100, 100, 100), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(download_chart, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_type(download_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(download_chart, DATA_POINTS);
    lv_chart_set_range(download_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 2048);
    /* ⭐ 线条/圆点变细：线 1px、点 2x2（不设的话主题默认又粗又大） */
    lv_obj_set_style_line_width(download_chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_width(download_chart, 2, LV_PART_INDICATOR);
    lv_obj_set_style_height(download_chart, 2, LV_PART_INDICATOR);
    download_series = lv_chart_add_series(download_chart, lv_color_make(0, 190, 255), LV_CHART_AXIS_PRIMARY_Y);

    // 上行曲线：同上
    upload_chart = lv_chart_create(upload_container);
    lv_obj_set_size(upload_chart, COL_W - 12, 52);
    lv_obj_align(upload_chart, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(upload_chart, lv_color_make(50, 50, 50), 0);
    lv_obj_set_style_border_width(upload_chart, 0, 0);
    lv_obj_set_style_line_color(upload_chart, lv_color_make(100, 100, 100), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(upload_chart, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_type(upload_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(upload_chart, DATA_POINTS);
    lv_chart_set_range(upload_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 512);
    lv_obj_set_style_line_width(upload_chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_width(upload_chart, 2, LV_PART_INDICATOR);
    lv_obj_set_style_height(upload_chart, 2, LV_PART_INDICATOR);
    upload_series = lv_chart_add_series(upload_chart, lv_color_make(255, 100, 0), LV_CHART_AXIS_PRIMARY_Y);

    // 填充初始数据（保持不变）
    for (int i = 0; i < DATA_POINTS; i++) {
        lv_chart_set_next_value(download_chart, download_series, 0);
        lv_chart_set_next_value(upload_chart, upload_series, 0);
    }


    // 状态指示器（保持不变）
    status_indicator = lv_obj_create(netinfo_page);
    lv_obj_set_size(status_indicator, 10, 10);
    //lv_obj_align(status_indicator, LV_ALIGN_TOP_LEFT, 2, -5);
    lv_obj_align(status_indicator, LV_ALIGN_TOP_LEFT, 2, -500);
    lv_obj_set_style_radius(status_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_indicator, lv_color_make(255, 0, 0), 0);

    network_status_label = lv_label_create(netinfo_page);
    lv_label_set_text(network_status_label, "NETERR");
    lv_obj_set_style_text_color(network_status_label, lv_color_make(200, 200, 200), 0);
    //lv_obj_align(network_status_label, LV_ALIGN_TOP_LEFT, 20, -8);
    lv_obj_align(network_status_label, LV_ALIGN_TOP_LEFT, 20, -800);




    lv_obj_t *sumdownload_container = lv_obj_create(netinfo_page);
    lv_obj_set_size(sumdownload_container, COL_W, ROW1_H);
    lv_obj_set_pos(sumdownload_container, COL_X0, ROW1_Y);
    lv_obj_set_style_bg_color(sumdownload_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(sumdownload_container, 1, 0);
    lv_obj_set_style_pad_all(sumdownload_container, 1, 0);
    lv_obj_set_style_border_color(sumdownload_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(sumdownload_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(sumdownload_container, 1, 0);

    

    download_label2 = lv_label_create(sumdownload_container);
    lv_label_set_text(download_label2, "0");
    lv_obj_set_style_text_color(download_label2, lv_color_make(0, 190, 255), 0);
    lv_obj_set_style_text_font(download_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(download_label2, LV_ALIGN_LEFT_MID, 5, 0);



    // 上传容器（保持不变）
    lv_obj_t *sumupload_container = lv_obj_create(netinfo_page);
    lv_obj_set_size(sumupload_container, COL_W, ROW1_H);
    lv_obj_set_pos(sumupload_container, COL_X1, ROW1_Y);
    lv_obj_set_style_bg_color(sumupload_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(sumupload_container, 1, 0);
    lv_obj_set_style_pad_all(sumupload_container, 1, 0);
    lv_obj_set_style_border_color(sumupload_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(sumupload_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(sumupload_container, 1, 0);

    upload_label2 = lv_label_create(sumupload_container);
    lv_label_set_text(upload_label2, "0");
    lv_obj_set_style_text_color(upload_label2, lv_color_make(255, 100, 0), 0);
    lv_obj_set_style_text_font(upload_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(upload_label2, LV_ALIGN_LEFT_MID, 5, 0);


    // 定时器（保持不变）
    lv_timer_create(update_network_speed, 1000, NULL);
}
