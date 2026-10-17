/* 农历算法的主机端自检（**不进固件**，只用来核对 src/utils/lunar.c）。
 *
 * 用法（在 PC 上，不需要交叉编译）：
 *     gcc -I src/utils -o %TEMP%\lunar_test tools/gen/lunar_test.c src/utils/lunar.c && %TEMP%\lunar_test
 *
 * 断言的是**已知事实**：2020-2027 的春节都是正月初一，以及这些年的闰月
 * （2020 闰四月 / 2023 闰二月 / 2025 闰六月，其余 0）。与 tools/gen/lunar_check.py 对齐。
 */
#include <stdio.h>
#include <string.h>

#include "lunar.h"

static int fails;

static void expect_cny(int y, int m, int d)
{
    lunar_date_t L;

    if (!lunar_from_solar(y, m, d, &L)) {
        printf("FAIL %04d-%02d-%02d: 转换失败\n", y, m, d);
        fails++;
        return;
    }
    if (L.year != y || L.month != 1 || L.day != 1 || L.leap) {
        printf("FAIL %04d-%02d-%02d: 得到 %s%s%s（应为 正月初一）\n", y, m, d,
               L.month_name, L.day_name, L.leap ? "(闰)" : "");
        fails++;
        return;
    }
    printf("ok   %04d-%02d-%02d -> %s%s年 %s%s\n", y, m, d, L.year_name, L.animal,
           L.month_name, L.day_name);
}

static void expect_leap(int y, int want)
{
    static const int probe[12][2] = {
        {1, 15}, {2, 15}, {3, 15}, {4, 15}, {5, 15}, {6, 15},
        {7, 15}, {8, 15}, {9, 15}, {10, 15}, {11, 15}, {12, 15},
    };
    int got = -1;

    /* 逐月 15 号试；碰上闰月那一趟就会读出 leap=1（同一个月出现两次） */
    for (int i = 0; i < 12; i++) {
        lunar_date_t L;
        if (lunar_from_solar(y, probe[i][0], probe[i][1], &L) && L.leap) {
            got = L.month;
            break;
        }
    }
    if (got != want) {
        printf("FAIL %d 闰月: 得到 %d，应为 %d\n", y, got, want);
        fails++;
    } else {
        printf("ok   %d 闰月 = %d\n", y, got);
    }
}

int main(void)
{
    /* 2020-2027 春节（正月初一） */
    expect_cny(2020, 1, 25);
    expect_cny(2021, 2, 12);
    expect_cny(2022, 2, 1);
    expect_cny(2023, 1, 22);
    expect_cny(2024, 2, 10);
    expect_cny(2025, 1, 29);
    expect_cny(2026, 2, 17);
    expect_cny(2027, 2, 6);

    /* 闰月（用闰月的 15 号当探针：那天的 leap 一定是 1） */
    expect_leap(2020, 4);
    expect_leap(2023, 2);
    expect_leap(2025, 6);

    /* 看一眼今天（打印用，方便人工核对） */
    {
        lunar_date_t L;
        int y = 2026, m = 10, d = 7;
        if (lunar_from_solar(y, m, d, &L))
            printf("     %04d-%02d-%02d -> %s%s年 %s%s (闰月 %d)\n", y, m, d,
                   L.year_name, L.animal, L.month_name, L.day_name, L.leap_month);
    }

    printf("%s\n", fails ? "**有失败**" : "全部通过");
    return fails ? 1 : 0;
}
