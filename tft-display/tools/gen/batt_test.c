/*
 * battery.c 的 PC 端自测（不需要设备、不需要 LVGL）。
 * 自己造一棵假 sysfs 树（class/power_supply + bus/iio/devices），用 TFT_BATT_SYSFS 指过去。
 * 断言口径 = 原版 main.c：电池 = ch3 ×5.95，适配器 = ch2 ×11，6.1 V→0%、8.4 V→100%。
 *
 * 用法：batt_test.exe a|b|c|d|e
 *   a  空树                  -> 没有电池：-1，不崩
 *   b  按原版真实读数        -> ch3=665 → 6.96 V → 37%；ch2=617 → 11.93 V → 充电中
 *   c  没插适配器（ch2 很低）-> 不充电、不报错
 *   d  适配器电压不对（7 V） -> 报 "Power ERROR"
 *   e  有电量计              -> 用 power_supply 的 capacity（88%），不吃 ADC
 *   f  没电池的 H29K 值      -> ch3=511 → 5.34 V ⇒ 低于 6.0 V ⇒ "--"
 * 每个 case 一个进程（battery_init() 一个进程只跑一次，和真机一样）。
 * 编译： zig cc -o batt_test.exe batt_test.c ../../src/utils/battery.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../src/utils/battery.h"

#ifdef _WIN32
static int setenv(const char *k, const char *v, int ov) { (void)ov; return _putenv_s(k, v); }
#endif

static int fails = 0;

static void expect(const char *what, int got, int want) {
    int ok = (got == want);
    if (!ok) fails++;
    printf("  %-42s got=%-6d want=%-6d %s\n", what, got, want, ok ? "ok" : "FAIL");
}

static void expect_range(const char *what, int got, int lo, int hi) {
    int ok = (got >= lo && got <= hi);
    if (!ok) fails++;
    printf("  %-42s got=%-6d want %d..%d %s\n", what, got, lo, hi, ok ? "ok" : "FAIL");
}

static void mkpath(const char *path) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(buf); *p = '/'; }
    }
    mkdir(buf);
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) { printf("cannot write %s\n", path); exit(2); }
    fputs(text, f);
    fclose(f);
}

/* <root>/bus/iio/devices/<dev>/in_voltageN_raw  （dev 用不带冒号的名字，Windows 文件名不允许冒号） */
static void make_adc(const char *root, const char *dev, int ch, const char *raw) {
    char d[400], p[460];
    snprintf(d, sizeof(d), "%s/bus/iio/devices/%s", root, dev);
    mkpath(d);
    snprintf(p, sizeof(p), "%s/in_voltage%d_raw", d, ch);
    write_file(p, raw);
    snprintf(p, sizeof(p), "%s/in_voltage_scale", d);
    write_file(p, "0.439453125\n");   /* 故意写成驱动报的那个"骗人"值：默认口径不该用它 */
}

static void make_ps(const char *root, const char *name,
                    const char *capacity, const char *status, const char *uv) {
    char d[400], p[460];
    snprintf(d, sizeof(d), "%s/class/power_supply/%s", root, name);
    mkpath(d);
    snprintf(p, sizeof(p), "%s/type", d);        write_file(p, "Battery\n");
    snprintf(p, sizeof(p), "%s/capacity", d);    write_file(p, capacity);
    snprintf(p, sizeof(p), "%s/status", d);      write_file(p, status);
    snprintf(p, sizeof(p), "%s/voltage_now", d); write_file(p, uv);
}

int main(int argc, char **argv) {
    const char *c = (argc > 1) ? argv[1] : "a";
    char root[128], conf[160];
    snprintf(root, sizeof(root), "sysfs_%s", c);
    snprintf(conf, sizeof(conf), "batt_conf_%s.txt", c);

    mkpath(root);
    setenv("TFT_BATT_SYSFS", root, 1);
    setenv("TFT_BATT_CONF", conf, 1);
    remove(conf);

    if (strcmp(c, "a") == 0) {
        printf("case a: 空树（没有电池）\n");
        expect("percent()", battery_percent(), -1);
        expect("millivolts()", battery_millivolts(), -1);
        expect("raw()", battery_raw(), -1);
        expect("charge_mv()", battery_charge_mv(), -1);
        expect("charging()", battery_charging() ? 1 : 0, 0);
        expect("charge_error()", battery_charge_error() ? 1 : 0, 0);
    }
    else if (strcmp(c, "b") == 0) {
        printf("case b: 原版实测读数 ch3=665 / ch2=617\n");
        make_adc(root, "iiodevice0", 3, "665\n");
        make_adc(root, "iiodevice0", 2, "617\n");
        expect_range("电池 mv（665→6961 mV）", battery_millivolts(), 6940, 6985);
        expect_range("电量 %（6.1~8.4 V 线性）", battery_percent(), 35, 39);
        expect_range("适配器 mv（617→11930）", battery_charge_mv(), 11900, 11960);
        expect("charging()", battery_charging() ? 1 : 0, 1);
        expect("charge_error()", battery_charge_error() ? 1 : 0, 0);
        expect("raw() 应取 ch3", battery_raw(), 665);
    }
    else if (strcmp(c, "c") == 0) {
        printf("case c: 没插适配器（ch2 ≈ 0 V）\n");
        make_adc(root, "iiodevice0", 3, "665\n");
        make_adc(root, "iiodevice0", 2, "0\n");
        expect_range("电量 %", battery_percent(), 35, 39);
        expect("charging()", battery_charging() ? 1 : 0, 0);
        expect("charge_error()", battery_charge_error() ? 1 : 0, 0);
    }
    else if (strcmp(c, "d") == 0) {
        printf("case d: 适配器只有 7 V（原版行为：报 Power ERROR）\n");
        make_adc(root, "iiodevice0", 3, "665\n");
        make_adc(root, "iiodevice0", 2, "362\n");   /* 362 × 19.3359 ≈ 7000 mV */
        expect_range("适配器 mv", battery_charge_mv(), 6950, 7050);
        expect("charging()", battery_charging() ? 1 : 0, 0);
        expect("charge_error()", battery_charge_error() ? 1 : 0, 1);
    }
    else if (strcmp(c, "e") == 0) {
        printf("case e: 有电量计（power_supply 优先）\n");
        make_ps(root, "BAT0", "88\n", "Charging\n", "7900000\n");
        make_adc(root, "iiodevice0", 3, "665\n");   /* ADC 也在，但不该被用 */
        expect("percent()", battery_percent(), 88);
        expect("millivolts()", battery_millivolts(), 7900);
        expect("charging()", battery_charging() ? 1 : 0, 1);
    }
    else if (strcmp(c, "f") == 0) {
        printf("case f: 没电池的板子（H29K ch3=511 → 5.34 V）\n");
        make_adc(root, "iiodevice0", 3, "511\n");
        make_adc(root, "iiodevice0", 2, "617\n");
        expect("percent() 应为 -1", battery_percent(), -1);
        expect("millivolts() 应为 -1", battery_millivolts(), -1);
        expect("charging()（适配器在）", battery_charging() ? 1 : 0, 1);
    }
    else {
        printf("unknown case %s\n", c);
        return 2;
    }

    printf("%s (%d failed)\n", fails ? "FAILED" : "OK", fails);
    return fails ? 1 : 0;
}
