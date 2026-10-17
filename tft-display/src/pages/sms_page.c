#include "../lvgl/lvgl.h"
#include "sms_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "../utils/tool.h"
// #include "../utils/gif_player.h"

lv_obj_t *SMS_page;
lv_obj_t *sms_list;  // 短信列表容器

// 1. 先定义短信数据结构（解决unknown type name 'SmsInfo'错误）
typedef struct {
    const char *sender;    // 发送者
    const char *content;   // 短信内容
    const char *time;      // 发送时间
    bool is_read;          // 是否已读
} SmsInfo;

// 2. 再声明所有函数（此时SmsInfo已定义，无类型错误）
static void create_sms_item(lv_obj_t *parent, SmsInfo *sms);
static void sms_item_click_cb(lv_event_t *e);
void refresh_sms_list(void);
void mark_sms_as_read(uint32_t index);

// 模拟短信数据（需在SmsInfo定义后）
static SmsInfo sms_data[] = {
    {"中国移动", "您的话费余额不足10元，请及时充值", "08:32", false},
    {"支付宝", "您尾号8723的银行卡收入1500.00元", "09:45", true},
    {"微信支付", "微信支付凭证：消费28.50元，商户：星巴克", "10:18", true},
    {"快递100", "您的快递已送达小区驿站，请凭取件码1234领取", "11:27", false},
    {"招商银行", "您的信用卡账单已生成，本期应还3280.00元，还款日05月20日", "14:03", false},
    {"班主任", "明天上午9点召开家长会，请准时参加", "16:42", true},
    {"天气预报", "明天晴，22~30℃，西南风3级，适宜出行", "18:15", true},
    {"京东", "您购买的商品已发货，快递单号：JD1234567890123", "昨天", false}
};

// 创建单个短信项（彻底适配旧版LVGL，移除所有高版本接口）
static void create_sms_item(lv_obj_t *parent, SmsInfo *sms) {
    // 短信项容器
    lv_obj_t *sms_item = lv_obj_create(parent);
    lv_obj_set_size(sms_item, lv_pct(100), 40);
    lv_obj_set_style_radius(sms_item, 8, 0);
    lv_obj_set_style_bg_color(sms_item, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_pad_left(sms_item, sms->is_read ? 12 : 24, 0);
    lv_obj_set_style_pad_right(sms_item, 12, 0);
    lv_obj_set_style_pad_top(sms_item, 8, 0);
    lv_obj_set_style_pad_bottom(sms_item, 8, 0);
    lv_obj_clear_flag(sms_item, LV_OBJ_FLAG_SCROLLABLE);
    
    // 未读短信样式
    if(!sms->is_read) {
        lv_obj_set_style_bg_color(sms_item, lv_color_hex(0x2D2D2D), 0);
        // 未读标记（左侧小圆点）
        lv_obj_t *unread_dot = lv_obj_create(sms_item);
        lv_obj_set_size(unread_dot, 8, 8);
        lv_obj_set_style_radius(unread_dot, 4, 0);
        lv_obj_set_style_bg_color(unread_dot, lv_color_hex(0x0099FF), 0);
        lv_obj_align(unread_dot, LV_ALIGN_LEFT_MID, 12, 0);
    }
    
    // 左侧容器：发送者 + 短信内容（横向排列）
    lv_obj_t *left_container = lv_obj_create(sms_item);
    lv_obj_set_size(left_container, lv_pct(75), lv_pct(100));
    lv_obj_set_layout(left_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(left_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(left_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(left_container, LV_ALIGN_LEFT_MID, 0, 0);
    
    // 发送者标签（用字体大小区分，无高版本接口）
    lv_obj_t *sender_label = lv_label_create(left_container);
    lv_label_set_text(sender_label, sms->sender);
    lv_obj_set_style_text_color(sender_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(sender_label, &lv_font_montserrat_14, 0);
    // 手动设置发送者标签右边距（替代item_space，适配所有旧版）
    lv_obj_set_style_margin_right(sender_label, 4, 0);
    
    // 分隔符
    lv_obj_t *separator = lv_label_create(left_container);
    lv_label_set_text(separator, "：");
    lv_obj_set_style_text_color(separator, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(separator, &lv_font_montserrat_12, 0);
    // 手动设置分隔符右边距
    lv_obj_set_style_margin_right(separator, 4, 0);
    
    // 短信内容标签（横向显示，超长省略）
    lv_obj_t *content_label = lv_label_create(left_container);
    lv_label_set_text(content_label, sms->content);
    lv_obj_set_style_text_color(content_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(content_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(content_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(content_label, lv_pct(100));
    
    // 时间标签（右侧垂直居中）
    lv_obj_t *time_label = lv_label_create(sms_item);
    lv_label_set_text(time_label, sms->time);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_10, 0);
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // 添加点击事件
    lv_obj_add_flag(sms_item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(sms_item, sms);
    lv_obj_add_event_cb(sms_item, sms_item_click_cb, LV_EVENT_CLICKED, NULL);
}

// 短信项点击事件回调
static void sms_item_click_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    SmsInfo *sms = lv_obj_get_user_data(obj);
    
    if(sms && !sms->is_read) {
        sms->is_read = true;
        refresh_sms_list();
    }
}

// 创建短信列表页面（移除lv_obj_set_style_item_space，用基础接口实现）
void create_sms_page(lv_obj_t *main_screen){
    SMS_page = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(SMS_page, lv_color_black(), 0);
    lv_obj_set_size(SMS_page, 320, 140);
    lv_obj_set_style_radius(SMS_page, 0, 0);
    
    // 去除边框
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);
    lv_obj_add_style(SMS_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(SMS_page, LV_SCROLLBAR_MODE_OFF);
    
    // 创建列表容器（启用垂直滚动，适配旧版）
    sms_list = lv_obj_create(SMS_page);
    lv_obj_set_size(sms_list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(sms_list, 4, 0);
    lv_obj_set_style_pad_bottom(sms_list, 4, 0);
    lv_obj_set_layout(sms_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sms_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sms_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // 关键修复：用短信项的margin_bottom替代item_space，适配所有旧版LVGL
    static lv_style_t item_style;
    lv_style_init(&item_style);
    // lv_style_set_margin_bottom(&item_style, 3, 0); // 短信项之间间距3px
    // 垂直滚动（旧版标准接口）
    // lv_obj_set_scroll_dir(sms_list, LV_SCROLL_DIR_Y);
    lv_obj_add_flag(sms_list, LV_OBJ_FLAG_SCROLLABLE);
    
    // 添加所有短信项（并应用间距样式）
    uint32_t sms_count = sizeof(sms_data) / sizeof(SmsInfo);
    for(uint32_t i = 0; i < sms_count; i++) {
        create_sms_item(sms_list, &sms_data[i]);
        // 给每个短信项加底部间距，替代item_space
        lv_obj_add_style(lv_obj_get_child(sms_list, i), &item_style, LV_PART_MAIN);
    }
}

// 刷新短信列表
void refresh_sms_list(void) {
    if(sms_list == NULL) return;
    
    // 清除现有短信项
    lv_obj_clean(sms_list);
    
    // 重新添加所有短信项（并加间距）
    static lv_style_t item_style;
    lv_style_init(&item_style);
    // lv_style_set_margin_bottom(&item_style, 3, 0);
    uint32_t sms_count = sizeof(sms_data) / sizeof(SmsInfo);
    for(uint32_t i = 0; i < sms_count; i++) {
        create_sms_item(sms_list, &sms_data[i]);
        lv_obj_add_style(lv_obj_get_child(sms_list, i), &item_style, LV_PART_MAIN);
    }
}

// 标记短信为已读
void mark_sms_as_read(uint32_t index) {
    uint32_t sms_count = sizeof(sms_data) / sizeof(SmsInfo);
    if(index < sms_count) {
        sms_data[index].is_read = true;
        refresh_sms_list();
    }
}
