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
#include "../utils/tool.h"

lv_obj_t *netinfo_page;

// 界面配置（保持不变）
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 130
#define CHART_WIDTH 120
#define CHART_HEIGHT 50
#define DATA_POINTS 15
#define NETWORK_STAT_FILE "/sys/class/net/%s/statistics/%s_bytes"

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

char Device[128] = {0};
char TotalRecv[128] = {0};
char TotalSent[128] = {0};
char RecvRate[128] = {0};
char SentRate[128] = {0};
long RecvRatefloat = 0;
long SentRatefloat = 0;


static void update_network_speed(lv_timer_t *t) {
    lv_label_set_text(network_status_label, Device);
    lv_label_set_text(upload_label, SentRate);
    lv_label_set_text(download_label, RecvRate);

    lv_label_set_text(upload_label2, TotalSent);
    lv_label_set_text(download_label2, TotalRecv);

    // lv_label_set_text(upload_label2, RecvRatefloat);
    // lv_label_set_text(download_label2, SentRatefloat);

    long s1 =(RecvRatefloat / 1024 / 1024 / 1024);
    long s2 =(SentRatefloat / 1024 /1024 / 1024);
    
    // 添加数据点到图表
    lv_chart_set_next_value(download_chart, download_series, s1 > 2048 ? 2048 : s1);
    lv_chart_set_next_value(upload_chart, upload_series, s2 > 512 ? 512 : s2);

    printf("RecvRatefloat:%d","SentRatefloat:%d\n",s1,s2);
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

    // 下载容器（保持不变）
    lv_obj_t *download_container = lv_obj_create(netinfo_page);
    lv_obj_set_size(download_container, (SCREEN_WIDTH / 2)-20, SCREEN_HEIGHT - 35);
    lv_obj_set_pos(download_container, 4, 15);
    lv_obj_set_style_bg_color(download_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(download_container, 1, 0);
    lv_obj_set_style_pad_all(download_container, 1, 0);
    lv_obj_set_style_border_color(download_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(download_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(download_container, 1, 0);

    // 上传容器（保持不变）
    lv_obj_t *upload_container = lv_obj_create(netinfo_page);
    lv_obj_set_size(upload_container, (SCREEN_WIDTH / 2)-20, SCREEN_HEIGHT - 35);
    lv_obj_set_pos(upload_container, (SCREEN_WIDTH / 2) -14, 15);
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
    lv_obj_align(download_label, LV_ALIGN_TOP_LEFT, 8, 30);

    upload_label = lv_label_create(upload_container);
    lv_label_set_text(upload_label, "0 KB/s");
    lv_obj_set_style_text_color(upload_label, lv_color_make(255, 100, 0), 0);
    lv_obj_set_style_text_font(upload_label, &lv_font_montserrat_20, 0);
    lv_obj_align(upload_label, LV_ALIGN_TOP_LEFT, 8, 30);

    // 下载图表（保持不变）
    download_chart = lv_chart_create(download_container);
    lv_obj_set_size(download_chart, CHART_WIDTH - 10, CHART_HEIGHT-15);
    lv_obj_align(download_chart, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_obj_set_style_bg_color(download_chart, lv_color_make(50, 50, 50), 0);
    lv_obj_set_style_border_width(download_chart, 0, 0);
    lv_obj_set_style_line_color(download_chart, lv_color_make(100, 100, 100), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(download_chart, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_type(download_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(download_chart, DATA_POINTS);
    lv_chart_set_range(download_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 2048);
    download_series = lv_chart_add_series(download_chart, lv_color_make(0, 190, 255), LV_CHART_AXIS_PRIMARY_Y);

    // 上传图表（保持不变）
    upload_chart = lv_chart_create(upload_container);
    lv_obj_set_size(upload_chart, CHART_WIDTH - 10, CHART_HEIGHT-15);
    lv_obj_align(upload_chart, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_obj_set_style_bg_color(upload_chart, lv_color_make(50, 50, 50), 0);
    lv_obj_set_style_border_width(upload_chart, 0, 0);
    lv_obj_set_style_line_color(upload_chart, lv_color_make(100, 100, 100), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(upload_chart, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_type(upload_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(upload_chart, DATA_POINTS);
    lv_chart_set_range(upload_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 512);
    upload_series = lv_chart_add_series(upload_chart, lv_color_make(255, 100, 0), LV_CHART_AXIS_PRIMARY_Y);

    // 填充初始数据（保持不变）
    for (int i = 0; i < DATA_POINTS; i++) {
        lv_chart_set_next_value(download_chart, download_series, 0);
        lv_chart_set_next_value(upload_chart, upload_series, 0);
    }


    // 状态指示器（保持不变）
    status_indicator = lv_obj_create(netinfo_page);
    lv_obj_set_size(status_indicator, 10, 10);
    lv_obj_align(status_indicator, LV_ALIGN_TOP_LEFT, 2, -5);
    lv_obj_set_style_radius(status_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_indicator, lv_color_make(255, 0, 0), 0);

    network_status_label = lv_label_create(netinfo_page);
    lv_label_set_text(network_status_label, "NETERR");
    lv_obj_set_style_text_color(network_status_label, lv_color_make(200, 200, 200), 0);
    lv_obj_align(network_status_label, LV_ALIGN_TOP_LEFT, 20, -8);



    download_label2 = lv_label_create(netinfo_page);
    lv_label_set_text(download_label2, "0");
    lv_obj_set_style_text_color(download_label2, lv_color_make(0, 190, 255), 0);
    lv_obj_set_style_text_font(download_label2, &lv_font_montserrat_18, 0);
    lv_obj_align(download_label2, LV_ALIGN_TOP_LEFT, 100, -8);

    upload_label2 = lv_label_create(netinfo_page);
    lv_label_set_text(upload_label2, "0");
    lv_obj_set_style_text_color(upload_label2, lv_color_make(255, 100, 0), 0);
    lv_obj_set_style_text_font(upload_label2, &lv_font_montserrat_18, 0);
    lv_obj_align(upload_label2, LV_ALIGN_TOP_LEFT, 200, -8);


    // 定时器（保持不变）
    lv_timer_create(update_network_speed, 100, NULL);
}
