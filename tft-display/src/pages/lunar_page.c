#include "../lvgl/lvgl.h"
#include "lunar_page.h"
#include "../utils/lunar.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * TIME 页的第二个横屏：**农历**（用户在 TIME 页左右划就能看到）。
 *
 * 版式（320x140，标题栏由 main.c 统一画）：
 *
 *      农历                              丙午 马年      ← 小字：左边"农历"、右边干支+生肖
 *                  八月廿七                           ← 大字（28px 子集字库）
 *              公历 2026-10-07 星期三                 ← 小字
 *                 本年闰月：无                        ← 小字（有闰月就写"闰六月"）
 *
 * 农历算法在 utils/lunar.c（表 + 算法已用 2020-2027 春节日期与 2020/2023/2025 的闰月校验过）。
 * 每秒算一次，**值没变就一个 label 都不动**（否则每秒重排一遍，看着会闪、也白费 CPU）。
 */

LV_FONT_DECLARE(lv_font_cjk_lunar_28);   /* 28px 农历子集：农历/干支/生肖/月日那几个字 */
LV_FONT_DECLARE(lv_font_cjk_sms_16);     /* 16px 全字库（短信页那套）：小字行用它 */

lv_obj_t *lunar_page;

static lv_obj_t *lp_year;    /* 右上：丙午 马年 */
static lv_obj_t *lp_big;     /* 中间大字：八月廿七 */
static lv_obj_t *lp_solar;   /* 公历 2026-10-07 星期三 */
static lv_obj_t *lp_leap;    /* 本年闰月：无 */

/* 上一轮显示过的内容（一模一样就不重设文本） */
static char lp_prev_year[32];
static char lp_prev_big[32];
static char lp_prev_solar[48];
static char lp_prev_leap[32];

static void lp_set(lv_obj_t *label, char *prev, size_t cap, const char *text)
{
    if (!label || strcmp(prev, text) == 0)
        return;
    lv_label_set_text(label, text);
    snprintf(prev, cap, "%s", text);
}

static void lunar_tick(lv_timer_t *t)
{
    time_t now;
    struct tm tm_now;
    lunar_date_t L;
    char big[32], year[32], solar[48], leap[32];
    static const char *WD[7] = { "日", "一", "二", "三", "四", "五", "六" };

    (void)t;

    time(&now);
    localtime_r(&now, &tm_now);

    if (!lunar_from_solar(tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday, &L)) {
        /* 时间没走到（或超出表范围，1900..2049）：显示 --，不编数据 */
        lp_set(lp_year, lp_prev_year, sizeof(lp_prev_year), "");
        lp_set(lp_big, lp_prev_big, sizeof(lp_prev_big), "--");
        lp_set(lp_solar, lp_prev_solar, sizeof(lp_prev_solar), "（读不到系统时间）");
        lp_set(lp_leap, lp_prev_leap, sizeof(lp_prev_leap), "");
        return;
    }

    snprintf(year, sizeof(year), "%s %s年", L.year_name, L.animal);
    snprintf(big, sizeof(big), "%s%s", L.month_name, L.day_name);
    snprintf(solar, sizeof(solar), "%d-%02d-%02d 星期%s", tm_now.tm_year + 1900,
             tm_now.tm_mon + 1, tm_now.tm_mday,
             WD[(tm_now.tm_wday < 0 || tm_now.tm_wday > 6) ? 0 : tm_now.tm_wday]);
    if (L.leap_month > 0) {
        static const char *M[12] = { "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊" };
        snprintf(leap, sizeof(leap), "本年闰月：闰%s月", M[L.leap_month - 1]);
    } else {
        snprintf(leap, sizeof(leap), "%s", "本年闰月：无");
    }

    lp_set(lp_year, lp_prev_year, sizeof(lp_prev_year), year);
    lp_set(lp_big, lp_prev_big, sizeof(lp_prev_big), big);
    lp_set(lp_solar, lp_prev_solar, sizeof(lp_prev_solar), solar);
    lp_set(lp_leap, lp_prev_leap, sizeof(lp_prev_leap), leap);
}

void create_lunar_page(lv_obj_t *parent)
{
    lunar_page = lv_obj_create(parent);
    lv_obj_set_style_bg_color(lunar_page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lunar_page, LV_OPA_COVER, 0);
    lv_obj_set_size(lunar_page, 320, 140);
    lv_obj_set_style_radius(lunar_page, 0, 0);
    lv_obj_set_style_border_width(lunar_page, 0, 0);
    /* ⚠️ 必须清掉主题默认的 20px 内边距：不清的话下面每一行都会往右下推 20px（各页都踩过） */
    lv_obj_set_style_pad_all(lunar_page, 0, 0);
    lv_obj_set_scrollbar_mode(lunar_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(lunar_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(lunar_page);

    /* 第一行：左边"农历"（灰）、右边干支+生肖（淡绿，项目里"成立/好"的那个颜色） */
    lv_obj_t *t1 = lv_label_create(lunar_page);
    lv_label_set_text(t1, "农历");
    lv_obj_set_style_text_font(t1, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(t1, lv_color_hex(0x808080), 0);
    lv_obj_set_pos(t1, 12, 6);

    lp_year = lv_label_create(lunar_page);
    lv_label_set_text(lp_year, "");
    lv_obj_set_style_text_font(lp_year, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(lp_year, lv_color_hex(0x90EE90), 0);
    lv_obj_align(lp_year, LV_ALIGN_TOP_RIGHT, -12, 6);

    /* 第二行：大字（28px 子集字库，只有农历用得到的那几十个字） */
    lp_big = lv_label_create(lunar_page);
    lv_label_set_text(lp_big, "--");
    lv_obj_set_style_text_font(lp_big, &lv_font_cjk_lunar_28, 0);
    lv_obj_set_style_text_color(lp_big, lv_color_white(), 0);
    lv_obj_align(lp_big, LV_ALIGN_TOP_MID, 0, 34);

    /* 第三行：公历日期 + 星期 */
    lp_solar = lv_label_create(lunar_page);
    lv_label_set_text(lp_solar, "");
    lv_obj_set_style_text_font(lp_solar, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(lp_solar, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(lp_solar, LV_ALIGN_TOP_MID, 0, 80);

    /* 第四行：本年闰月（没闰月就写"无"） */
    lp_leap = lv_label_create(lunar_page);
    lv_label_set_text(lp_leap, "");
    lv_obj_set_style_text_font(lp_leap, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(lp_leap, lv_color_hex(0x707070), 0);
    lv_obj_align(lp_leap, LV_ALIGN_TOP_MID, 0, 108);

    lunar_tick(NULL);                                   /* 立刻填一次，别等一秒 */
    lv_timer_create(lunar_tick, 1000, NULL);
}
