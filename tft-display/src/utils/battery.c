/*
 * 本地电池电量 + 适配器电压 —— **都不走服务端/模组**。
 *
 * 口径**照抄原版 main.c**（用户 2.18 版实现，见下），这两条通道在这族板子上是实测标定过的：
 *   电池     = /sys/bus/iio/devices/iio:device0/in_voltage3_raw
 *              V = raw / 1024 × 1.8 × 5.95     （2 节串联锂电：6.1 V = 0%，8.4 V = 100%，线性）
 *   适配器   = /sys/bus/iio/devices/iio:device0/in_voltage2_raw
 *              V = raw / 1023 × 1.8 × 11       （≥11 V 显示充电图标；4.5~10 V 报 "Power ERROR"）
 * 本模块内部统一成 `mV = raw × scale × div`，所以默认 scale = 1.8/1024×1000 = 1.7578125 mV/LSB：
 *   电池：scale 1.7578125 × div 5.95  ⇒ raw × 10.4590 mV  （= 原版公式，实测 ch3=665 → 6.96 V）
 *   适配器：scale 1.7578125 × div 11  ⇒ raw × 19.3359 mV  （实测 ch2=617 → 11.93 V，12 V 适配器 ✓）
 * ⚠️ 别用 sysfs 里的 `in_voltage_scale`（那台驱动报 0.439453 = 1.8/4096，比实际口径小 4 倍，
 *    照它算 ch2 只有 2.98 V，一眼假；原版除以 1024 才是对的）。
 *
 * 另外也支持 /sys/class/power_supply（板子带电量计驱动时优先用它，零配置即可用）。
 *
 * ⚠️ 读不到、或算出来不在合理范围（电池默认 6.0~9.0 V）⇒ 返回 -1，界面显示 "--"（不编数据）。
 *    没有电池的板子（H29K 实测）ch3 悬空约 5.3 V ⇒ 低于 6.0 V ⇒ 正好显示 "--"。
 *
 * 配置：`/etc/tftdisplay.battery`（**包外**文件，opkg 升级不覆盖；与 /etc/tftdisplay.brightness 同一套路）
 *   adc      = …/in_voltage3_raw     电池通道    div       = 5.95     电池分压比
 *   chg_adc  = …/in_voltage2_raw     适配器通道  chg_div   = 11       适配器分压比
 *   scale    = 1.7578125             mV/LSB（两条通道共用）
 *   min_mv/max_mv      = 6000/9000   电池合理范围（超出 = 没电池）
 *   empty_mv/full_mv   = 6100/8400   电量两端（线性）
 *   chg_on_mv          = 11000       适配器 ≥ 此值 = 充电中
 *   chg_err_min/max_mv = 4500/10000  适配器落在这里 = 报 "Power ERROR"
 *   ps                 = 电源名/目录（默认自动扫 power_supply；0 = 不用它）
 *   debug              = 1           每轮打一行读数
 * 环境变量：TFT_BATT_ADC / TFT_BATT_CHG_ADC / TFT_BATT_DIV / TFT_BATT_CHG_DIV / TFT_BATT_SCALE /
 *   TFT_BATT_MIN_MV / TFT_BATT_MAX_MV / TFT_BATT_EMPTY_MV / TFT_BATT_FULL_MV / TFT_BATT_CHG_ON_MV /
 *   TFT_BATT_PS / TFT_BATT_SYSFS / TFT_BATT_CONF / TFT_BATT_DEBUG  （优先级高于配置文件）
 */
#include "battery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp */
#include <stdbool.h>
#include <dirent.h>

/* 原版口径（H29K / F50-PRO 这族板子实测标定） */
#define BATT_ADC_DEFAULT       "/sys/bus/iio/devices/iio:device0/in_voltage3_raw"
#define BATT_CHG_ADC_DEFAULT   "/sys/bus/iio/devices/iio:device0/in_voltage2_raw"
#define BATT_SCALE_DEFAULT     1.7578125  /* mV/LSB = 1.8 V / 1024 */
#define BATT_DIV_DEFAULT       5.95
#define BATT_CHG_DIV_DEFAULT   11.0
#define BATT_MIN_MV_DEFAULT    6000       /* 2S 锂电：低于 6.0 V 视为没有电池 */
#define BATT_MAX_MV_DEFAULT    9000
#define BATT_EMPTY_MV_DEFAULT  6100
#define BATT_FULL_MV_DEFAULT   8400
#define BATT_CHG_ON_MV_DEFAULT 11000
#define BATT_CHG_ERR_MIN_MV    4500
#define BATT_CHG_ERR_MAX_MV    10000
#define BATT_CHG_MAX_MV        20000      /* 适配器上限，超了当悬空 */

#define BATT_SYSFS_DEFAULT     "/sys"
#define BATT_CONF_DEFAULT      "/etc/tftdisplay.battery"

static char   s_conf[128]  = BATT_CONF_DEFAULT;
static char   s_sysfs[128] = BATT_SYSFS_DEFAULT;

static char   s_adc[160];                 /* 电池通道 */
static char   s_chg_adc[160];             /* 适配器通道 */
static char   s_ps[160];                  /* power_supply 目录；空 = 没有 */
static char   s_ps_name[64];

static double s_scale        = BATT_SCALE_DEFAULT;
static double s_div          = BATT_DIV_DEFAULT;
static double s_chg_scale    = BATT_SCALE_DEFAULT;
static double s_chg_div      = BATT_CHG_DIV_DEFAULT;
static int    s_min_mv       = BATT_MIN_MV_DEFAULT;
static int    s_max_mv       = BATT_MAX_MV_DEFAULT;
static int    s_empty_mv     = BATT_EMPTY_MV_DEFAULT;
static int    s_full_mv      = BATT_FULL_MV_DEFAULT;
static int    s_chg_on_mv    = BATT_CHG_ON_MV_DEFAULT;
static int    s_chg_err_min  = BATT_CHG_ERR_MIN_MV;
static int    s_chg_err_max  = BATT_CHG_ERR_MAX_MV;
static bool   s_debug        = false;
static bool   s_inited       = false;

/* ---------------- 小工具 ---------------- */

static int read_int_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return (int)v;
}

static bool read_str_file(const char *path, char *out, size_t outlen) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    bool ok = (fgets(out, (int)outlen, f) != NULL);
    fclose(f);
    if (!ok) { out[0] = '\0'; return false; }
    size_t n = strlen(out);
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                     out[n-1] == ' '  || out[n-1] == '\t'))
        out[--n] = '\0';
    return true;
}

static const char *env_str(const char *name) {
    const char *v = getenv(name);
    return (v && *v) ? v : NULL;
}

static double env_double(const char *name, double dflt) {
    const char *v = env_str(name);
    if (!v) return dflt;
    char *end = NULL;
    double d = strtod(v, &end);
    return (end && end != v) ? d : dflt;
}

static int env_int(const char *name, int dflt) {
    const char *v = env_str(name);
    if (!v) return dflt;
    char *end = NULL;
    long d = strtol(v, &end, 0);
    return (end && end != v) ? (int)d : dflt;
}

static bool contains_ci(const char *hay, const char *needle) {
    if (!hay || !*hay) return false;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++)
        if (strncasecmp(p, needle, nl) == 0) return true;
    return false;
}

/* ---------------- 标定文件 ---------------- */

static bool conf_get(const char *key, char *out, size_t outlen) {
    FILE *f = fopen(s_conf, "r");
    if (!f) return false;

    char line[192];
    size_t klen = strlen(key);
    bool hit = false;
    while (!hit && fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        char *kend = eq;
        while (kend > p && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
        if ((size_t)(kend - p) != klen) continue;
        if (strncasecmp(p, key, klen) != 0) continue;

        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *vend = v + strlen(v);
        char *hash = strchr(v, '#');
        if (hash) vend = hash;
        while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t' ||
                            vend[-1] == '\n' || vend[-1] == '\r')) vend--;
        size_t vlen = (size_t)(vend - v);
        if (vlen >= outlen) vlen = outlen - 1;
        memcpy(out, v, vlen);
        out[vlen] = '\0';
        hit = true;
    }
    fclose(f);
    return hit;
}

static int conf_int(const char *key, int dflt) {
    char buf[32];
    if (!conf_get(key, buf, sizeof(buf))) return dflt;
    char *end = NULL;
    long d = strtol(buf, &end, 0);
    return (end && end != buf) ? (int)d : dflt;
}

static double conf_double(const char *key, double dflt) {
    char buf[32];
    if (!conf_get(key, buf, sizeof(buf))) return dflt;
    char *end = NULL;
    double d = strtod(buf, &end);
    return (end && end != buf) ? d : dflt;
}

static bool conf_str(const char *key, char *out, size_t outlen) {
    return conf_get(key, out, outlen);
}

/* ---------------- power_supply（有电量计驱动时优先） ---------------- */

static void ps_clear(void) { s_ps[0] = '\0'; s_ps_name[0] = '\0'; }

static bool ps_scan(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return false;

    char best[64] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char p[300];
        snprintf(p, sizeof(p), "%s/%s/capacity", dir, e->d_name);
        int c = read_int_file(p);
        if (c < 0 || c > 100) continue;

        char t[48] = "";
        snprintf(p, sizeof(p), "%s/%s/type", dir, e->d_name);
        read_str_file(p, t, sizeof(t));

        if (contains_ci(t, "Battery")) { snprintf(best, sizeof(best), "%s", e->d_name); break; }
        if (!best[0]) snprintf(best, sizeof(best), "%s", e->d_name);
    }
    closedir(d);

    if (!best[0]) return false;
    snprintf(s_ps_name, sizeof(s_ps_name), "%s", best);
    snprintf(s_ps, sizeof(s_ps), "%s/%s", dir, best);
    return true;
}

static void ps_setup(void) {
    ps_clear();

    char cfg[160];
    const char *want = env_str("TFT_BATT_PS");
    if (!want && conf_str("ps", cfg, sizeof(cfg))) want = cfg;

    if (want && strcmp(want, "0") == 0) return;          /* 明确禁用 */
    if (want && *want && strcmp(want, "auto") != 0) {
        char dir[260], cap[300];
        if (want[0] == '/') snprintf(dir, sizeof(dir), "%s", want);
        else                snprintf(dir, sizeof(dir), "%s/class/power_supply/%s", s_sysfs, want);
        snprintf(cap, sizeof(cap), "%s/capacity", dir);
        if (read_int_file(cap) >= 0) {
            snprintf(s_ps, sizeof(s_ps), "%s", dir);
            const char *base = strrchr(dir, '/');
            snprintf(s_ps_name, sizeof(s_ps_name), "%s", base ? base + 1 : dir);
        }
        return;
    }

    char base[220];
    snprintf(base, sizeof(base), "%s/class/power_supply", s_sysfs);
    ps_scan(base);
}

static int ps_capacity(void) {
    if (!s_ps[0]) return -1;
    char p[280];
    snprintf(p, sizeof(p), "%s/capacity", s_ps);
    int c = read_int_file(p);
    return (c >= 0 && c <= 100) ? c : -1;
}

static int ps_millivolts(void) {
    if (!s_ps[0]) return -1;
    char p[280];
    snprintf(p, sizeof(p), "%s/voltage_now", s_ps);   /* µV */
    int uv = read_int_file(p);
    if (uv > 0) return uv / 1000;
    snprintf(p, sizeof(p), "%s/voltage_avg", s_ps);
    uv = read_int_file(p);
    return (uv > 0) ? uv / 1000 : -1;
}

/* 有 status 用 status（更准），没有返回 -1 让上层回退到适配器电压 */
static int ps_charging(void) {
    if (!s_ps[0]) return -1;
    char p[280], st[48] = "";
    snprintf(p, sizeof(p), "%s/status", s_ps);
    if (!read_str_file(p, st, sizeof(st)) || !st[0]) return -1;
    if (contains_ci(st, "Charging") || contains_ci(st, "Full")) return 1;
    return 0;
}

/* ---------------- ADC ---------------- */

/* 从路径里取通道号：in_voltage3_raw → 3；取不到 -1 */
static int path_channel(const char *path) {
    const char *p = strstr(path, "in_voltage");
    if (!p) return -1;
    p += strlen("in_voltage");
    if (*p < '0' || *p > '9') return -1;
    return atoi(p);
}

/* 找通道：先试给的那个路径；不行就在 iio 设备里找**同一个通道号**（板子设备号可能不同）；
 * 再不行才退到"任意一个能读的通道"。 */
static bool adc_find(const char *wanted, int ch_want, char *out, size_t outlen) {
    if (wanted && *wanted && read_int_file(wanted) >= 0) {
        snprintf(out, outlen, "%s", wanted);
        return true;
    }
    char base[200];
    snprintf(base, sizeof(base), "%s/bus/iio/devices", s_sysfs);
    DIR *d = opendir(base);
    if (!d) return false;

    bool any = false;
    char anyp[280] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "iio", 3) != 0) continue;

        if (ch_want >= 0) {
            char p[280];
            snprintf(p, sizeof(p), "%s/%s/in_voltage%d_raw", base, e->d_name, ch_want);
            if (read_int_file(p) >= 0) {
                snprintf(out, outlen, "%s", p);
                closedir(d);
                return true;
            }
        }
        if (!any) {
            for (int ch = 0; ch < 8 && !any; ch++) {
                char p[280];
                snprintf(p, sizeof(p), "%s/%s/in_voltage%d_raw", base, e->d_name, ch);
                if (read_int_file(p) >= 0) { any = true; snprintf(anyp, sizeof(anyp), "%s", p); }
            }
        }
    }
    closedir(d);
    if (any) { snprintf(out, outlen, "%s", anyp); return true; }
    return false;
}

/* 同目录下的另一个通道（电池 ch3 → 适配器 ch2） */
static bool adc_sibling(const char *path, int ch, char *out, size_t outlen) {
    if (ch < 0) return false;
    char tmp[240];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = strstr(tmp, "in_voltage");
    if (!p) return false;
    const char *u = strchr(p, '_');                 /* "_raw" */
    if (!u) return false;
    int head = (int)(p - tmp);
    snprintf(tmp + head, sizeof(tmp) - (size_t)head, "in_voltage%d%s", ch, u);
    return adc_find(tmp, ch, out, outlen);
}

/* 每个通道 × 每个候选分压比的电压表（现场标定用；原版口径对不上时看这个） */
static void adc_probe_log(void) {
    char base[200];
    snprintf(base, sizeof(base), "%s/bus/iio/devices", s_sysfs);
    DIR *d = opendir(base);
    if (!d) return;

    static const double k_divs[] = { 4, 5, 5.95, 6, 8, 10, 11, 16 };
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "iio", 3) != 0) continue;
        for (int ch = 0; ch < 8; ch++) {
            char p[280];
            snprintf(p, sizeof(p), "%s/%s/in_voltage%d_raw", base, e->d_name, ch);
            int raw = read_int_file(p);
            if (raw < 0) continue;
            char text[200] = "";
            for (size_t k = 0; k < sizeof(k_divs)/sizeof(k_divs[0]); k++) {
                int mv = (int)((double)raw * s_scale * k_divs[k] + 0.5);
                char one[28];
                snprintf(one, sizeof(one), "%s%.2f:%d", k ? " " : "", k_divs[k], mv);
                if (strlen(text) + strlen(one) < sizeof(text) - 1) strcat(text, one);
            }
            printf("[batt]   %-18s raw=%-5d (div:mV) %s\n", e->d_name, raw, text);
        }
    }
    closedir(d);
}

/* ---------------- 对外接口 ---------------- */

void battery_init(void) {
    if (s_inited) return;
    s_inited = true;

    if (env_str("TFT_BATT_CONF"))
        snprintf(s_conf, sizeof(s_conf), "%s", env_str("TFT_BATT_CONF"));
    if (env_str("TFT_BATT_SYSFS")) {
        snprintf(s_sysfs, sizeof(s_sysfs), "%s", env_str("TFT_BATT_SYSFS"));
    } else {
        char cfg[128];
        if (conf_str("sysfs", cfg, sizeof(cfg)))
            snprintf(s_sysfs, sizeof(s_sysfs), "%s", cfg);
    }

    char adc[160], chg[160];
    const char *want_adc = env_str("TFT_BATT_ADC");
    if (!want_adc && conf_str("adc", adc, sizeof(adc))) want_adc = adc;
    const char *want_chg = env_str("TFT_BATT_CHG_ADC");
    if (!want_chg && conf_str("chg_adc", chg, sizeof(chg))) want_chg = chg;

    s_scale      = conf_double("scale",        BATT_SCALE_DEFAULT);
    s_div        = conf_double("div",          BATT_DIV_DEFAULT);
    s_chg_scale  = conf_double("chg_scale",    s_scale);
    s_chg_div    = conf_double("chg_div",      BATT_CHG_DIV_DEFAULT);
    s_min_mv     = conf_int("min_mv",          BATT_MIN_MV_DEFAULT);
    s_max_mv     = conf_int("max_mv",          BATT_MAX_MV_DEFAULT);
    s_empty_mv   = conf_int("empty_mv",        BATT_EMPTY_MV_DEFAULT);
    s_full_mv    = conf_int("full_mv",         BATT_FULL_MV_DEFAULT);
    s_chg_on_mv  = conf_int("chg_on_mv",       BATT_CHG_ON_MV_DEFAULT);
    s_chg_err_min = conf_int("chg_err_min_mv", BATT_CHG_ERR_MIN_MV);
    s_chg_err_max = conf_int("chg_err_max_mv", BATT_CHG_ERR_MAX_MV);

    s_scale      = env_double("TFT_BATT_SCALE",    s_scale);
    s_div        = env_double("TFT_BATT_DIV",      s_div);
    s_chg_scale  = env_double("TFT_BATT_CHG_SCALE", s_chg_scale);
    s_chg_div    = env_double("TFT_BATT_CHG_DIV",  s_chg_div);
    s_min_mv     = env_int("TFT_BATT_MIN_MV",      s_min_mv);
    s_max_mv     = env_int("TFT_BATT_MAX_MV",      s_max_mv);
    s_empty_mv   = env_int("TFT_BATT_EMPTY_MV",    s_empty_mv);
    s_full_mv    = env_int("TFT_BATT_FULL_MV",     s_full_mv);
    s_chg_on_mv  = env_int("TFT_BATT_CHG_ON_MV",   s_chg_on_mv);
    s_chg_err_min = env_int("TFT_BATT_CHG_ERR_MIN_MV", s_chg_err_min);
    s_chg_err_max = env_int("TFT_BATT_CHG_ERR_MAX_MV", s_chg_err_max);
    s_debug      = env_int("TFT_BATT_DEBUG", 0) != 0;

    if (s_full_mv <= s_empty_mv) { s_empty_mv = BATT_EMPTY_MV_DEFAULT; s_full_mv = BATT_FULL_MV_DEFAULT; }
    if (s_max_mv <= s_min_mv)    { s_min_mv = BATT_MIN_MV_DEFAULT; s_max_mv = BATT_MAX_MV_DEFAULT; }
    if (s_div < 0.1) s_div = BATT_DIV_DEFAULT;
    if (s_chg_div < 0.1) s_chg_div = BATT_CHG_DIV_DEFAULT;

    /* 电池通道：配置优先，否则原版默认通道（找不到同号通道再退到任意可用通道） */
    int want_ch = path_channel(want_adc ? want_adc : BATT_ADC_DEFAULT);
    if (!adc_find(want_adc ? want_adc : BATT_ADC_DEFAULT, want_ch, s_adc, sizeof(s_adc)))
        s_adc[0] = '\0';

    /* 适配器通道：没配置就从电池通道推（同目录、通道号 -1：ch3 → ch2） */
    int batt_ch = s_adc[0] ? path_channel(s_adc) : -1;
    if (want_chg) {
        if (!adc_find(want_chg, path_channel(want_chg), s_chg_adc, sizeof(s_chg_adc)))
            s_chg_adc[0] = '\0';
    } else if (batt_ch > 0) {
        if (!adc_sibling(s_adc, batt_ch - 1, s_chg_adc, sizeof(s_chg_adc)))
            s_chg_adc[0] = '\0';
    }

    /* 首选 power_supply（板子带电量计驱动时） */
    ps_setup();

    printf("[batt] 口径：电池 %s raw×%.4f mV ×%.2f (合理 %d..%d mV，曲线 %d..%d mV)；"
           "适配器 %s raw×%.4f mV ×%.2f (≥%d mV 充电，%d..%d mV 报错)；conf=%s\n",
           s_adc[0] ? s_adc : "(无)", s_scale, s_div, s_min_mv, s_max_mv, s_empty_mv, s_full_mv,
           s_chg_adc[0] ? s_chg_adc : "(无)", s_chg_scale, s_chg_div,
           s_chg_on_mv, s_chg_err_min, s_chg_err_max, s_conf);
    if (s_ps[0])
        printf("[batt] power_supply=%s capacity=%d%% voltage=%dmV\n",
               s_ps_name, ps_capacity(), ps_millivolts());
    printf("[batt] 当前：raw=%d mv=%d pct=%d chg=%dmV 充电=%d 报错=%d\n",
           battery_raw(), battery_millivolts(), battery_percent(),
           battery_charge_mv(), battery_charging() ? 1 : 0, battery_charge_error() ? 1 : 0);
    if (battery_percent() < 0)
        adc_probe_log();
    fflush(stdout);
}

int battery_raw(void) {
    if (!s_inited) battery_init();
    return s_adc[0] ? read_int_file(s_adc) : -1;
}

int battery_millivolts(void) {
    if (!s_inited) battery_init();

    if (s_ps[0]) {
        int mv = ps_millivolts();
        if (mv > 0) return mv;
    }
    if (!s_adc[0]) return -1;

    int raw = read_int_file(s_adc);
    if (raw < 0) return -1;

    int mv = (int)((double)raw * s_scale * s_div + 0.5);
    if (mv < s_min_mv || mv > s_max_mv) {         /* 不在合理范围 ⇒ 当作没有电池 */
        if (s_debug) {
            printf("[batt] raw=%d -> %dmV 超出 %d..%d，视为无电池\n",
                   raw, mv, s_min_mv, s_max_mv);
            fflush(stdout);
        }
        return -1;
    }
    return mv;
}

int battery_percent(void) {
    if (!s_inited) battery_init();

    if (s_ps[0]) {                     /* 电量计的 capacity 是权威值 */
        int c = ps_capacity();
        if (c >= 0) return c;
    }

    int mv = battery_millivolts();
    if (mv < 0) return -1;
    if (mv >= s_full_mv)  return 100;
    if (mv <= s_empty_mv) return 0;

    /* 原版就是线性映射（empty → full） */
    return (int)((double)(mv - s_empty_mv) * 100.0 / (double)(s_full_mv - s_empty_mv) + 0.5);
}

int battery_charge_mv(void) {
    if (!s_inited) battery_init();
    if (!s_chg_adc[0]) return -1;

    int raw = read_int_file(s_chg_adc);
    if (raw < 0) return -1;

    int mv = (int)((double)raw * s_chg_scale * s_chg_div + 0.5);
    return mv;
}

bool battery_charging(void) {
    if (!s_inited) battery_init();

    if (s_ps[0]) {
        int r = ps_charging();
        if (r >= 0) return r == 1;
    }
    int mv = battery_charge_mv();
    return (mv >= s_chg_on_mv && mv <= BATT_CHG_MAX_MV);
}

bool battery_charge_error(void) {
    if (!s_inited) battery_init();
    int mv = battery_charge_mv();
    return (mv >= s_chg_err_min && mv <= s_chg_err_max);
}
