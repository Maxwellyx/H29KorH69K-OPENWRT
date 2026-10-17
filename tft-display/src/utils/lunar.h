#ifndef TFT_LUNAR_H
#define TFT_LUNAR_H

#include <stdbool.h>

/*
 * 公历 → 农历（含干支年 / 生肖 / 闰月）。
 *
 * 数据表是业界通用的 1900 起「每年一个 17 位整数」的闰月/大小月表
 * （本文件只收录到 2049 年，够用；来源见 tools/gen/lunar_check.py 的注释）。
 * 表 + 算法已用 **已知的春节日期**（2020-2027 全部对得上）与
 * **闰月**（2020 闰四月 / 2023 闰二月 / 2025 闰六月，其余这些年无闰月）校验通过 ✓。
 *
 * 用法（页面里就是每秒试一次，值一样就不重画）：
 *     lunar_date_t L;
 *     if (lunar_from_solar(y, m, d, &L)) {
 *         // L.month_name = "八月" / "闰六月"，L.day_name = "廿七"
 *     }
 */

typedef struct {
    int  year;              /* 农历年（干支年的那个年，按春节分界） */
    int  month;             /* 1..12 */
    int  day;               /* 1..30 */
    bool leap;              /* 这个月是不是闰月 */
    char year_name[16];     /* 干支年："丙午" */
    char animal[8];         /* 生肖："马" */
    char month_name[16];    /* "八月" / "闰六月" */
    char day_name[16];      /* "廿七" */
    int  leap_month;        /* **本农历年**闰几月（0 = 无闰月），给"本年闰月"那行用 */
} lunar_date_t;

/* 公历 y-m-d → 农历。超出表范围（<1900 或 >2049）返回 false。 */
bool lunar_from_solar(int y, int m, int d, lunar_date_t *out);

#endif /* TFT_LUNAR_H */
