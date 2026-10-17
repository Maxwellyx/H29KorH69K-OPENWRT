#include "lunar.h"

#include <stdio.h>
#include <string.h>

/* 农历 1900-2049 的闰月/大小月表（每年一个整数，含义见下面三个小函数）。
 * 逐行 10 年，行尾注释就是年份，方便核对；校验见 tools/gen/lunar_check.py。 */
static const unsigned LUNAR_INFO[150] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2, /* 1900-1909 */
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977, /* 1910-1919 */
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, /* 1920-1929 */
    0x06566, 0x0d4a0, 0x0ea50, 0x16a95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950, /* 1930-1939 */
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, /* 1940-1949 */
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, /* 1950-1959 */
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, /* 1960-1969 */
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6, /* 1970-1979 */
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570, /* 1980-1989 */
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0, /* 1990-1999 */
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, /* 2000-2009 */
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, /* 2010-2019 */
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, /* 2020-2029 */
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, /* 2030-2039 */
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, /* 2040-2049 */
};

#define LUNAR_YEAR_MIN 1900
#define LUNAR_YEAR_MAX 2049

/* 天干 / 地支 / 生肖 / 数字 / 月名 —— 都用**字符串数组**：
 * 中文是多字节 UTF-8，按字节下标取字会取到半截字（踩过这种坑）。 */
static const char *GAN[10] = { "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸" };
static const char *ZHI[12] = { "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥" };
static const char *ANIMALS[12] = { "鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪" };
static const char *NSTR1[11] = { "", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" };
static const char *NSTR2[4] = { "初", "十", "廿", "卅" };
static const char *MONTHS[12] = { "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊" };

static bool table_ok(int y)
{
    return y >= LUNAR_YEAR_MIN && y <= LUNAR_YEAR_MAX;
}

/* 该年闰几月（0 = 没有闰月） */
static int lunar_leap_month(int y)
{
    return (int)(LUNAR_INFO[y - LUNAR_YEAR_MIN] & 0xf);
}

/* 该年闰月的天数（没有闰月 = 0） */
static int lunar_leap_days(int y)
{
    if (!lunar_leap_month(y))
        return 0;
    return (LUNAR_INFO[y - LUNAR_YEAR_MIN] & 0x10000) ? 30 : 29;
}

/* 该年 m 月（非闰月）的天数 */
static int lunar_month_days(int y, int m)
{
    return (LUNAR_INFO[y - LUNAR_YEAR_MIN] & (0x10000 >> m)) ? 30 : 29;
}

/* 该农历年的总天数（含闰月） */
static int lunar_year_days(int y)
{
    int sum = 348;
    unsigned i;

    for (i = 0x8000; i > 0x8; i >>= 1)
        if (LUNAR_INFO[y - LUNAR_YEAR_MIN] & i)
            sum++;
    return sum + lunar_leap_days(y);
}

/* 公历 y-m-d → 1970-01-01 起的天数（Howard Hinnant 的 days_from_civil，含负年份也正确） */
static long days_from_civil(int y, int m, int d)
{
    long yy = y - (m <= 2);
    long era = (yy >= 0 ? yy : yy - 399) / 400;
    long yoe = yy - era * 400;                                   /* [0,399] */
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   /* [0,365] */
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            /* [0,146096] */
    return era * 146097 + doe - 719468;
}

bool lunar_from_solar(int y, int m, int d, lunar_date_t *out)
{
    int  i, temp = 0;
    long offset;
    int  year, leap, is_leap = 0, month = 1, day;
    int  gz;

    if (!out || m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    /* 表覆盖 1900..2049；1900 年 1 月 31 日之前不是这个表的范围（下面 offset < 0 会挡掉） */
    if (y < LUNAR_YEAR_MIN || y > LUNAR_YEAR_MAX)
        return false;

    memset(out, 0, sizeof(*out));

    offset = days_from_civil(y, m, d) - days_from_civil(1900, 1, 31);
    if (offset < 0)
        return false;

    /* ① 逐农历年减，定位农历年 */
    for (i = 1900; i <= LUNAR_YEAR_MAX && offset > 0; i++) {
        temp = lunar_year_days(i);
        offset -= temp;
    }
    if (offset < 0) {
        offset += temp;
        i--;
    }
    year = i;
    if (!table_ok(year))
        return false;

    /* ② 逐月减（闰月插在 leap 月之后的那一趟里一起减） */
    leap = lunar_leap_month(year);
    for (i = 1; i < 13 && offset > 0; i++) {
        if (leap > 0 && i == leap + 1 && !is_leap) {
            i--;
            is_leap = 1;
            temp = lunar_leap_days(year);
        } else {
            temp = lunar_month_days(year, i);
        }
        if (is_leap && i == leap + 1)
            is_leap = 0;
        offset -= temp;
    }
    if (offset == 0 && leap > 0 && i == leap + 1) {
        if (is_leap) {
            is_leap = 0;
        } else {
            is_leap = 1;
            i--;
        }
    }
    if (offset < 0) {
        offset += temp;
        i--;
    }
    month = i;
    day   = (int)offset + 1;

    if (month < 1 || month > 12 || day < 1 || day > 30)
        return false;

    out->year       = year;
    out->month      = month;
    out->day        = day;
    out->leap       = is_leap ? true : false;
    out->leap_month = leap;

    /* 干支年 / 生肖：以 4 为基准（公元 4 年是甲子年） */
    gz = (year - 4) % 10;
    if (gz < 0)
        gz += 10;
    snprintf(out->year_name, sizeof(out->year_name), "%s%s", GAN[gz], ZHI[((year - 4) % 12 + 12) % 12]);
    snprintf(out->animal, sizeof(out->animal), "%s", ANIMALS[((year - 4) % 12 + 12) % 12]);

    /* 月名：闰月前面加"闰" */
    snprintf(out->month_name, sizeof(out->month_name), "%s%s月",
             out->leap ? "闰" : "", MONTHS[month - 1]);

    /* 日名：初一…初十、十一…十九、二十、廿一…廿九、三十 */
    if (day == 10) {
        snprintf(out->day_name, sizeof(out->day_name), "%s", "初十");
    } else if (day == 20) {
        snprintf(out->day_name, sizeof(out->day_name), "%s", "二十");
    } else if (day == 30) {
        snprintf(out->day_name, sizeof(out->day_name), "%s", "三十");
    } else {
        snprintf(out->day_name, sizeof(out->day_name), "%s%s", NSTR2[day / 10], NSTR1[day % 10]);
    }
    return true;
}
