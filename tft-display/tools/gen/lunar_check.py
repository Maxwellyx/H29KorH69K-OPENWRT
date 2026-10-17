# 农历表（1900-2049，取自 jjonline/calendar.js 的 lunarInfo，逐行 10 年）
LUNAR = [
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,  # 1900-1909
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,  # 1910-1919
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,  # 1920-1929
    0x06566, 0x0d4a0, 0x0ea50, 0x16a95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,  # 1930-1939
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,  # 1940-1949
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,  # 1950-1959
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,  # 1960-1969
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,  # 1970-1979
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,  # 1980-1989
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0,  # 1990-1999
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,  # 2000-2009
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,  # 2010-2019
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,  # 2020-2029
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,  # 2030-2039
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,  # 2040-2049
]


def leap_month(y):
    return LUNAR[y - 1900] & 0xf


def leap_days(y):
    return (LUNAR[y - 1900] & 0x10000) and 30 or 29 if leap_month(y) else 0


def month_days(y, m):
    return 30 if (LUNAR[y - 1900] & (0x10000 >> m)) else 29


def year_days(y):
    s = 348
    i = 0x8000
    while i > 0x8:
        s += 1 if (LUNAR[y - 1900] & i) else 0
        i >>= 1
    return s + leap_days(y)


def days_from_civil(y, m, d):
    y -= m <= 2
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def solar_to_lunar(y, m, d):
    offset = days_from_civil(y, m, d) - days_from_civil(1900, 1, 31)
    i, temp = 1900, 0
    while i < 2101 and offset > 0:
        temp = year_days(i)
        offset -= temp
        i += 1
    if offset < 0:
        offset += temp
        i -= 1
    year = i
    leap = leap_month(year)
    is_leap = False
    i = 1
    while i < 13 and offset > 0:
        if leap > 0 and i == leap + 1 and not is_leap:
            i -= 1
            is_leap = True
            temp = leap_days(year)
        else:
            temp = month_days(year, i)
        if is_leap and i == leap + 1:
            is_leap = False
        offset -= temp
        i += 1
    if offset == 0 and leap > 0 and i == leap + 1:
        if is_leap:
            is_leap = False
        else:
            is_leap = True
            i -= 1
    if offset < 0:
        offset += temp
        i -= 1
    return year, i, offset + 1, is_leap


GAN = '甲乙丙丁戊己庚辛壬癸'
ZHI = '子丑寅卯辰巳午未申酉戌亥'
ANIMAL = '鼠牛虎兔龙蛇马羊猴鸡狗猪'
NSTR1 = '日一二三四五六七八九十'
NSTR2 = '初十廿卅'


def gz_year(ly):
    return GAN[(ly - 4) % 10] + ZHI[(ly - 4) % 12]


def animal(ly):
    return ANIMAL[(ly - 4) % 12]


def month_name(lm, is_leap):
    names = '正二三四五六七八九十冬腊'
    return ('闰' if is_leap else '') + names[lm - 1] + '月'


def day_name(ld):
    if ld == 10:
        return '初十'
    if ld == 20:
        return '二十'
    if ld == 30:
        return '三十'
    return NSTR2[ld // 10] + NSTR1[ld % 10]


# ---- 校验：春节（正月初一）日期 + 闰月 ----
# 已知事实：2020-01-25/2021-02-12/2022-02-01/2023-01-22/2024-02-10/2025-01-29/2026-02-17/2027-02-06
CNY = {
    2020: (1, 25), 2021: (2, 12), 2022: (2, 1), 2023: (1, 22),
    2024: (2, 10), 2025: (1, 29), 2026: (2, 17), 2027: (2, 6),
}
ok = True
for y, (mm, dd) in CNY.items():
    ly, lm, ld, isleap = solar_to_lunar(y, mm, dd)
    good = (ly == y and lm == 1 and ld == 1 and not isleap)
    ok &= good
    print(f'CNY {y}-{mm:02d}-{dd:02d} -> 农历 {ly} {month_name(lm, isleap)}{day_name(ld)} {"OK" if good else "**BAD**"}')

# 闰月断言：2020 闰四月 / 2023 闰二月 / 2025 闰六月 / 2021,2022,2024,2026 无闰月
LEAPS = {2020: 4, 2021: 0, 2022: 0, 2023: 2, 2024: 0, 2025: 6, 2026: 0}
for y, want in LEAPS.items():
    got = leap_month(y)
    good = got == want
    ok &= good
    print(f'闰月 {y}: got={got} want={want} {"OK" if good else "**BAD**"}')

# 今天（2026-10-07）与几个已知点
for (y, m, d) in [(2026, 10, 7), (2026, 2, 17), (2025, 1, 29), (2024, 2, 10)]:
    ly, lm, ld, isleap = solar_to_lunar(y, m, d)
    print(f'{y}-{m:02d}-{d:02d} -> 农历 {gz_year(ly)}{animal(ly)}年 {month_name(lm, isleap)}{day_name(ld)}')

print('全部锚点:', 'OK' if ok else '**有错**')
