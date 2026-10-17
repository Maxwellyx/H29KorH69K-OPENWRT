#ifndef SMS_PAGE_H
#define SMS_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif
extern lv_obj_t *SMS_page;

// 仅声明变量（带extern，不带初始化）
extern char MacAddress[128];
extern char IPAddresses[512];

void create_sms_page(lv_obj_t *main_screen);
#ifdef __cplusplus
}
#endif

#endif