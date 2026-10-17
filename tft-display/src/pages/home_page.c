#include "../lvgl/lvgl.h"
#include "home_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
// #include "../utils/gif_player.h"

lv_obj_t *Home_page;
lv_obj_t *date_label;
lv_obj_t *time_label;
lv_obj_t *label_weekday;

// Pages home
static void update_time_cb(lv_timer_t *t) {
    if (!time_label) return;
    time_t now;
    struct tm *timeinfo;
    char time_str[32];
    time(&now);
    timeinfo = localtime(&now);
    int weekday = timeinfo->tm_wday;

    char weekday_str[50];

    if(weekday==0){
        sprintf(weekday_str, "%s", "Sunday");
        lv_obj_set_style_text_color(label_weekday, lv_color_hex(0xccffe6), 0);
    }
    else if(weekday==1){
        sprintf(weekday_str, "%s", "Monday");
    }
    else if(weekday==2){
        sprintf(weekday_str, "%s", "Tuesday");
    }
    else if(weekday==3){
        sprintf(weekday_str, "%s", "Wednesday");
    }
    else if(weekday==4){
        sprintf(weekday_str, "%s", "Thursday");
    }
    else if(weekday==5){
        sprintf(weekday_str, "%s", "Friday");
    }
    else if(weekday==6){
        sprintf(weekday_str, "%s", "Saturday");
        lv_obj_set_style_text_color(label_weekday, lv_color_hex(0xccff99), 0);
    }

    lv_label_set_text(label_weekday, weekday_str);



    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    lv_label_set_text(time_label, time_str);

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", timeinfo);
    lv_label_set_text(date_label, date_str);
}


void create_home_page(lv_obj_t *main_screen){


    Home_page= lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(Home_page, lv_color_black(), 0);
    lv_obj_set_size(Home_page,320,140);//设置容器大小
    lv_obj_set_style_radius(Home_page,0,0);//设置容器圆角为直角
    // 创建一个样式描述符，用于存储样式信息
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);  // 设置边框宽度为0，从而去除边框
    // 将样式应用到标签上
    lv_obj_add_style(Home_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(Home_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(Home_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(Home_page, LV_OPA_COVER, 0);


    // Create time label
    time_label = lv_label_create(Home_page);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 40, 15);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xCCCCCC), 0);

    // Create date label
    date_label = lv_label_create(Home_page);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, 0);
    time_t now;
    struct tm *timeinfo;
    char date_str[32];
    time(&now);
    timeinfo = localtime(&now);
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", timeinfo);
    lv_label_set_text(date_label, date_str);
    lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 30, 80);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xCCCCCC), 0);



    label_weekday = lv_label_create(Home_page);
    lv_label_set_long_mode(label_weekday, LV_LABEL_LONG_WRAP);                  
    lv_label_set_text(label_weekday, "-----");
    lv_obj_set_width(label_weekday, 110);  
    lv_obj_set_style_text_align(label_weekday, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_weekday, LV_ALIGN_TOP_LEFT, 160, 80);
    lv_obj_set_style_text_color(label_weekday, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_weekday, &lv_font_montserrat_16, 0);









    // // 创建背景图片（可根据天气代码更换背景）
    // lv_obj_t* bg_img = lv_img_create(Home_page);
    // lv_obj_set_size(bg_img, LV_HOR_RES, LV_VER_RES);
    // lv_obj_align(bg_img, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // // 创建城市名称标签
    // lv_obj_t* city_label = lv_label_create(Home_page);
    // lv_label_set_text(city_label, "加载中...");
    // lv_obj_set_style_text_color(city_label, lv_color_white(), 0);
    // lv_obj_set_style_text_font(city_label, &lv_font_montserrat_28, 0);
    // lv_obj_align(city_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // // 创建温度显示
    // lv_obj_t* temp_label = lv_label_create(Home_page);
    // lv_label_set_text(temp_label, "0°C");
    // lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
    // lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_48, 0);
    // lv_obj_align(temp_label, LV_ALIGN_CENTER, 0, 0);
    
    // // 创建天气描述
    // lv_obj_t* desc_label = lv_label_create(Home_page);
    // lv_label_set_text(desc_label, "天气描述");
    // lv_obj_set_style_text_color(desc_label, lv_color_white(), 0);
    // lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_20, 0);
    // lv_obj_align(desc_label, LV_ALIGN_CENTER, 0, 40);
    
    // // 创建湿度显示
    // lv_obj_t* humidity_label = lv_label_create(Home_page);
    // lv_label_set_text(humidity_label, "湿度: 0%");
    // lv_obj_set_style_text_color(humidity_label, lv_color_white(), 0);
    // lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_16, 0);
    // lv_obj_align(humidity_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_timer_create(update_time_cb, 1000, NULL);
    
}
//end Pages home