#include "wsdata.h"
#include "tinyjson.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ 状态 */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static ws_snapshot_t   g;
static unsigned        g_ui_seen;
/* 信号当前用的是哪一路（0 无 / 1 服务小区 / 2 NR 上报）：
 * 两路都会来，得知道用谁，否则切回 LTE 后旧的 NR 值会一直留着。 */
static int             g_sig_rank;

#define LOCK()   pthread_mutex_lock(&g_lock)
#define UNLOCK() pthread_mutex_unlock(&g_lock)

void wsdata_init(void)
{
    LOCK();
    memset(&g, 0, sizeof(g));
    g.batt_pct = -1;
    g.temp = -1;
    g.clients = -1;             /* -1 = 服务端没报过客户端数（界面显示 --） */
    g_sig_rank = 0;
    snprintf(g.state, sizeof(g.state), "%s", "未连接");
    UNLOCK();
}

/* --------------------------------------------------------------- 小工具 */

static void set_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* 大小写不敏感的子串查找（不依赖 strcasestr，省得开 _GNU_SOURCE） */
static const char *find_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle)
        return NULL;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n &&
               ((*h == *n) ||
                ((*h >= 'A' && *h <= 'Z' ? *h + 32 : *h) ==
                 (*n >= 'A' && *n <= 'Z' ? *n + 32 : *n)))) {
            h++;
            n++;
        }
        if (!*n)
            return hay;
    }
    return NULL;
}

static bool key_is(const char *key, const char *want)
{
    return key && want && strcmp(key, want) == 0;
}

/* 多键名取值：keys 以 NULL 结尾 */
static bool get_str_any(const char *d, const char *const *keys, char *out, size_t cap)
{
    for (int i = 0; keys[i]; i++)
        if (tj_get_str(d, keys[i], out, cap))
            return true;
    return false;
}

static bool get_int_any(const char *d, const char *const *keys, int *out)
{
    for (int i = 0; keys[i]; i++)
        if (tj_get_int(d, keys[i], out))
            return true;
    return false;
}

static bool get_num_any(const char *d, const char *const *keys, double *out)
{
    for (int i = 0; keys[i]; i++)
        if (tj_get_num(d, keys[i], out))
            return true;
    return false;
}

static bool get_bool_any(const char *d, const char *const *keys, bool *out)
{
    for (int i = 0; keys[i]; i++)
        if (tj_get_bool(d, keys[i], out))
            return true;
    return false;
}

/* ------------------------------------------------------- 宿主信息(Sys) */

static const char *K_CPU[]     = { "Cpu", "cpu", "CPU", NULL };
static const char *K_MEM[]     = { "Mem", "mem", "MemPercent", NULL };
static const char *K_TEMP[]    = { "Temp", "temp", "Temperature", NULL };
static const char *K_CPUMIN[]  = { "CpuMin", "cpu_min", NULL };
static const char *K_CPUMAX[]  = { "CpuMax", "cpu_max", NULL };
static const char *K_MEMMIN[]  = { "MemMin", "mem_min", NULL };
static const char *K_MEMMAX[]  = { "MemMax", "mem_max", NULL };
static const char *K_TEMPMIN[] = { "TempMin", "temp_min", NULL };
static const char *K_TEMPMAX[] = { "TempMax", "temp_max", NULL };
static const char *K_CLIENTS[] = { "Clients", "clients", "DhcpClients", "lan_clients", NULL };
static const char *K_UPTIME[]  = { "Uptime", "uptime", NULL };
static const char *K_LOAD[]    = { "Load", "load", NULL };
static const char *K_DISK[]    = { "Disk", "disk", NULL };
static const char *K_BATT[]    = { "Batt", "batt", "Battery", NULL };
static const char *K_BATTV[]   = { "BattVoltage", "batt_voltage", "battv", "Voltage", NULL };
static const char *K_CHGV[]    = { "ChgVoltage", "chg_voltage", "chgv", NULL };
static const char *K_CHGERR[]  = { "ChgError", "chg_error", "PowerError", NULL };
static const char *K_SSID[]    = { "Ssid", "ssid", "SSID", NULL };
static const char *K_WIFIKEY[] = { "WifiKey", "wifi_key", "wifikey", "Password", "Key", NULL };

/*
 * 宿主侧的信息（CPU/内存/温度/电量/客户端数/WiFi）现有服务端都不推，
 * 这里按上面的键名消费；出现在**任何**一条推送的 data 里都会被采纳。
 * 服务端没补之前，界面显示 "---"。详见 utils/wsdata.h 的文件头。
 */
static void apply_host_info(const char *d)
{
    char s[128];
    int  iv;
    double dv;
    bool bv;

    LOCK();
    if (get_int_any(d, K_CPU, &iv))
        g.cpu = iv;
    if (get_int_any(d, K_MEM, &iv))
        g.mem = iv;
    if (get_int_any(d, K_TEMP, &iv))
        g.temp = iv;
    /* 服务端累计的极值（趋势页底部"历史最高/最低"）。⚠️ 这几个键可能是 0 的合法值，
     * 所以不能当"没上报"处理 —— CPU/内存的极值服务端每次都送，温度那对 0 就是没上报。 */
    if (get_int_any(d, K_CPUMIN, &iv))
        g.cpu_min = iv;
    if (get_int_any(d, K_CPUMAX, &iv))
        g.cpu_max = iv;
    if (get_int_any(d, K_MEMMIN, &iv))
        g.mem_min = iv;
    if (get_int_any(d, K_MEMMAX, &iv))
        g.mem_max = iv;
    if (get_int_any(d, K_TEMPMIN, &iv))
        g.temp_min = iv;
    if (get_int_any(d, K_TEMPMAX, &iv))
        g.temp_max = iv;
    if (get_int_any(d, K_CLIENTS, &iv))
        g.clients = iv;
    if (get_int_any(d, K_UPTIME, &iv))
        g.uptime = iv;
    if (get_int_any(d, K_LOAD, &iv))
        g.load = iv;
    if (get_int_any(d, K_DISK, &iv))
        g.disk = iv;
    if (get_int_any(d, K_BATT, &iv))
        g.batt_pct = iv > 100 ? 100 : (iv < 0 ? 0 : iv);
    if (get_num_any(d, K_BATTV, &dv))
        g.batt_voltage = dv;
    if (get_num_any(d, K_CHGV, &dv))
        g.chg_voltage = dv;
    if (get_bool_any(d, K_CHGERR, &bv))
        g.chg_error = bv;
    if (get_str_any(d, K_SSID, s, sizeof(s)))
        set_str(g.ssid, sizeof(g.ssid), s);
    if (get_str_any(d, K_WIFIKEY, s, sizeof(s)))
        set_str(g.wifikey, sizeof(g.wifikey), s);
    UNLOCK();
}

/* ------------------------------------------------------------ 网口(IP) */

static void on_ip(const char *d)
{
    char buf[128];
    bool bv;
    char ips[256];

    ips[0] = '\0';
    for (int i = 0; i < 8; i++) {
        if (!tj_get_arr_str(d, "IPAddresses", i, buf, sizeof(buf)))
            break;
        if (!buf[0])
            continue;
        if (ips[0])
            strncat(ips, "\n", sizeof(ips) - strlen(ips) - 1);
        strncat(ips, buf, sizeof(ips) - strlen(ips) - 1);
    }

    LOCK();
    if (tj_get_str(d, "MacAddress", buf, sizeof(buf)))
        set_str(g.mac, sizeof(g.mac), buf);
    if (get_bool_any(d, (const char *const[]){ "IsUp", NULL }, &bv))
        g.ip_up = bv;
    if (get_bool_any(d, (const char *const[]){ "HasIP", NULL }, &bv))
        g.ip_hasip = bv;
    if (get_bool_any(d, (const char *const[]){ "IsConnected", NULL }, &bv))
        g.ip_connected = bv;
    if (ips[0])
        set_str(g.ips, sizeof(g.ips), ips);
    UNLOCK();
}

/* ---------------------------------------------------------- 流量(Speed) */

static double parse_rate(const char *s)
{
    double v;

    if (!s || !*s)
        return 0;
    v = atof(s);
    if (strstr(s, "TB"))
        v *= 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else if (strstr(s, "GB"))
        v *= 1024.0 * 1024.0 * 1024.0;
    else if (strstr(s, "MB"))
        v *= 1024.0 * 1024.0;
    else if (strstr(s, "KB"))
        v *= 1024.0;
    return v;
}

static void on_speed(const char *d)
{
    char buf[64];

    LOCK();
    if (tj_get_str(d, "Device", buf, sizeof(buf)))
        set_str(g.dev, sizeof(g.dev), buf);
    if (tj_get_str(d, "TotalRecv", buf, sizeof(buf)))
        set_str(g.total_recv, sizeof(g.total_recv), buf);
    if (tj_get_str(d, "TotalSent", buf, sizeof(buf)))
        set_str(g.total_sent, sizeof(g.total_sent), buf);
    if (tj_get_str(d, "RecvRate", buf, sizeof(buf))) {
        set_str(g.recv_rate, sizeof(g.recv_rate), buf);
        g.recv_bps = parse_rate(buf);
    }
    if (tj_get_str(d, "SentRate", buf, sizeof(buf))) {
        set_str(g.sent_rate, sizeof(g.sent_rate), buf);
        g.sent_bps = parse_rate(buf);
    }
    UNLOCK();
}

/* ------------------------------------------------------------ 蜂窝(AT) */

/* 服务端的 ratMap（monitorserver.go 的 getRAT） */
static const char *rat_by_act(int act)
{
    switch (act) {
    case 0: case 1: case 3: case 8:              return "GSM";
    case 2: case 4: case 5: case 6: case 9: case 10: return "WCDMA";
    case 7:                                      return "LTE";
    case 11: case 12:                            return "NR";
    default:                                     return "---";
    }
}

/* 服务端 getExtracted() 的运营商分类表 */
static const char *classify_operator(const char *s)
{
    if (strstr(s, "CHN-CT") || strstr(s, "CHINA TELECOM") || strstr(s, "Telecom") ||
        strstr(s, "46003") || strstr(s, "46005") || strstr(s, "46011") || strstr(s, "46012"))
        return "Telecom";
    if (strstr(s, "CHN-UNICOM") || strstr(s, "UNICOM") || strstr(s, "Unicom") ||
        strstr(s, "46001") || strstr(s, "46006") || strstr(s, "46009") || strstr(s, "46010"))
        return "Unicom";
    if (strstr(s, "CMCC") || strstr(s, "CHINA MOBILE") || strstr(s, "Mobile") ||
        strstr(s, "46000") || strstr(s, "46002") || strstr(s, "46004") ||
        strstr(s, "46007") || strstr(s, "46008") || strstr(s, "46013"))
        return "CMCC";
    if (strstr(s, "CHINA BROADCAST") || strstr(s, "CBN") || strstr(s, "CHN-CBN") ||
        strstr(s, "46015"))
        return "CBN";
    return "---";
}

/* ---- 运营商中文名 / 制式标签 / 双源合成 ---- */

/* 运营商号码（MCC+MNC）或英文名 → 中文名 */
static const char *op_name_of(const char *s)
{
    if (!s || !*s)
        return NULL;

    /* 先按 MCC+MNC（AT+COPS? 有时回号码） */
    if (strstr(s, "46000") || strstr(s, "46002") || strstr(s, "46004") ||
        strstr(s, "46007") || strstr(s, "46008"))
        return "中国移动";
    if (strstr(s, "46001") || strstr(s, "46006") || strstr(s, "46009") ||
        strstr(s, "46010"))
        return "中国联通";
    if (strstr(s, "46003") || strstr(s, "46005") || strstr(s, "46011") ||
        strstr(s, "46012"))
        return "中国电信";
    if (strstr(s, "46015") || strstr(s, "46020"))
        return "中国广电";

    /* 再按名称（CT/TELECOM 与 UNICOM 要排在 MOBILE 前面判） */
    if (find_ci(s, "CHN-CT") || find_ci(s, "CHINA TELECOM") || find_ci(s, "CHNCT") ||
        find_ci(s, "TELECOM"))
        return "中国电信";
    if (find_ci(s, "UNICOM"))
        return "中国联通";
    if (find_ci(s, "CMCC") || find_ci(s, "CHINA MOBILE"))
        return "中国移动";
    if (find_ci(s, "BROADCAST") || find_ci(s, "CBN"))
        return "中国广电";

    return NULL;
}

/* 内部口径 → 界面口径 */
static const char *rat_label_of(const char *rat)
{
    if (!rat || !*rat)
        return "---";
    if (strcmp(rat, "NR") == 0)
        return "5G";
    if (strcmp(rat, "LTE") == 0)
        return "4G";
    if (strcmp(rat, "WCDMA") == 0)
        return "3G";
    if (strcmp(rat, "GSM") == 0)
        return "2G";
    return rat;
}

/* QSCAN 的 Protocol 字段 → 内部口径 */
static const char *rat_from_proto(const char *proto)
{
    if (!proto || !*proto)
        return NULL;
    if (find_ci(proto, "NR"))
        return "NR";
    if (find_ci(proto, "LTE"))
        return "LTE";
    if (find_ci(proto, "WCDMA"))
        return "WCDMA";
    if (find_ci(proto, "GSM"))
        return "GSM";
    return NULL;
}

/* 两个来源合成：QSCAN（实测服务小区）> COPS（在网登记）—— 调用时需持锁 */
static void resolve_locked(void)
{
    const char *rat = g.rat_scan[0] ? g.rat_scan : g.rat_cops;

    set_str(g.rat, sizeof(g.rat), (rat && *rat) ? rat : "");
    set_str(g.rat_label, sizeof(g.rat_label), rat_label_of(rat));

    if (g.op_cops[0])
        set_str(g.op_name, sizeof(g.op_name), g.op_cops);
    else if (g.op_scan[0])
        set_str(g.op_name, sizeof(g.op_name), g.op_scan);
    else if (g.cops[0])
        set_str(g.op_name, sizeof(g.op_name), g.cops);
    else
        set_str(g.op_name, sizeof(g.op_name), "---");
}

/*
 * 从 AT+COPS? 的原始应答里抠运营商/制式，口径与服务端一致：
 *   +COPS: <mode>,<format>,"<name>",<act>
 *   → cops = 运营商分类(name)，rat = ratMap(act)，mcc = name 原文
 * 没有引号段（例如 +COPS: 0,2）就按服务端一样判为取不到。
 */
static void parse_cops(const char *resp)
{
    const char *p, *q, *e, *c;
    char name[64];
    size_t n;
    int act;

    if (!resp || !*resp)
        return;

    p = find_ci(resp, "+COPS:");
    if (!p)
        p = find_ci(resp, "COPS:");
    if (!p)
        return;

    q = strchr(p, '"');
    if (!q)
        return;
    e = strchr(q + 1, '"');
    if (!e)
        return;

    n = (size_t)(e - q - 1);
    if (n >= sizeof(name))
        n = sizeof(name) - 1;
    memcpy(name, q + 1, n);
    name[n] = '\0';

    c = e + 1;
    while (*c && *c != ',')
        c++;
    if (*c != ',')
        return;
    c++;
    while (*c == ' ')
        c++;
    if (*c < '0' || *c > '9')
        return;
    act = atoi(c);

    LOCK();
    set_str(g.cops, sizeof(g.cops), classify_operator(name));
    set_str(g.mcc, sizeof(g.mcc), name);
    set_str(g.rat_cops, sizeof(g.rat_cops), rat_by_act(act));
    {
        const char *cn = op_name_of(name);
        set_str(g.op_cops, sizeof(g.op_cops), cn ? cn : "");
    }
    resolve_locked();
    UNLOCK();
}

/* ------------------------------------------------------------------- 信号 */

/* 把一条扫描/信号结果落到模型里（rsrp 为 0 视为无效） */
static void apply_scan(const char *proto, const char *mccmnc, int rsrp, int rsrq, int sinr)
{
    const char *rat = rat_from_proto(proto);
    const char *op = mccmnc ? op_name_of(mccmnc) : NULL;

    LOCK();
    if (rat)
        set_str(g.rat_scan, sizeof(g.rat_scan), rat);
    if (op)
        set_str(g.op_scan, sizeof(g.op_scan), op);
    if (rsrp != 0) {
        if (rsrp > 0 && rsrp <= 97)     /* 有的固件回指数口径（0..97） */
            rsrp -= 140;
        g.rsrp = rsrp;
        g.rsrq = rsrq;
        g.sinr = sinr;
        g.has_signal = true;
    }
    resolve_locked();
    UNLOCK();
}

/*
 * 文本应答里的 +QSCAN: 行（服务端也是按这个格式解析的，见 monitorserver.go）：
 *   +QSCAN: "<NR5G|LTE>",<MCC>,<MNC>,<TAC>,<PCI>,<RSRP>,<RSRQ>,<SINR>,<Status>
 */
static void scan_text_qscan(const char *text)
{
    const char *p = text;

    if (!text || !*text)
        return;

    while ((p = find_ci(p, "+QSCAN:")) != NULL) {
        const char *q = strchr(p, '"');
        char proto[16] = "";
        int v[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        int n = 0;

        if (q) {
            const char *qe = strchr(q + 1, '"');
            if (qe) {
                size_t l = (size_t)(qe - q - 1);
                if (l >= sizeof(proto))
                    l = sizeof(proto) - 1;
                memcpy(proto, q + 1, l);
                proto[l] = '\0';

                q = qe + 1;
                while (n < 8) {
                    while (*q == ',' || *q == ' ')
                        q++;
                    if (!*q || *q == '\r' || *q == '\n')
                        break;
                    v[n++] = atoi(q);
                    while (*q && *q != ',')
                        q++;
                }
            }
        }

        if (proto[0] && n >= 6) {
            char code[16];
            snprintf(code, sizeof(code), "%d%02d", v[0], v[1]);
            apply_scan(proto, code, v[4], v[5], (n > 6) ? v[6] : 0);
        }
        p += 7;
    }
}

/*
 * 标准/常见信号应答（谁先出现用谁；都只是"有没有读数"）：
 *   +CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq_idx>,<rsrp_idx>   ← 3GPP TS 27.007 8.69
 *           rsrp = idx-140 (dBm)、rsrq = (idx-39)/2 (dB)
 *   +QCSQ: "NR5G",<rsrp>,<rsrq>,<sinr> / "LTE",<rsrp>,<rsrq>,<rssi>,<sinr>
 *   +CSQ:  <rssi_idx>,<ber>                                    ← RSSI = -113+2*idx
 */
static void scan_text_signal(const char *text)
{
    const char *p;
    int a, b, c, d, e, f;

    if (!text || !*text)
        return;

    p = find_ci(text, "+CESQ:");
    if (p && sscanf(p, "%*[^:]: %d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &e, &f) == 6) {
        int rsrq = (e >= 1 && e <= 34) ? (e - 39) / 2 : 0;
        int rsrp = (f >= 0 && f <= 97) ? f - 140 : 0;
        if (rsrp != 0 || rsrq != 0) {
            LOCK();
            if (rsrp != 0) {
                g.rsrp = rsrp;
                g.has_signal = true;
            }
            if (rsrq != 0)
                g.rsrq = rsrq;
            UNLOCK();
        }
    }

    p = find_ci(text, "+QCSQ:");
    if (p) {
        const char *q = strchr(p, '"');
        const char *qe = q ? strchr(q + 1, '"') : NULL;

        if (q && qe) {
            char kind[16] = "";
            size_t l = (size_t)(qe - q - 1);

            if (l >= sizeof(kind))
                l = sizeof(kind) - 1;
            memcpy(kind, q + 1, l);
            kind[l] = '\0';

            if (find_ci(kind, "NR") || find_ci(kind, "LTE")) {
                int v[5] = { 0, 0, 0, 0, 0 };
                int n = 0;
                const char *r = qe + 1;

                while (n < 5) {
                    while (*r == ',' || *r == ' ')
                        r++;
                    if (!*r || *r == '\r' || *r == '\n')
                        break;
                    v[n++] = atoi(r);
                    while (*r && *r != ',')
                        r++;
                }
                if (n >= 3) {
                    LOCK();
                    g.rsrp = v[0];
                    g.rsrq = v[1];
                    g.sinr = find_ci(kind, "NR") ? v[2] : ((n >= 4) ? v[3] : 0);
                    g.has_signal = true;
                    UNLOCK();
                }
            }
        }
    }

    p = find_ci(text, "+CSQ:");
    if (p && sscanf(p, "%*[^:]: %d,%d", &a, &b) == 2) {
        if (a >= 0 && a <= 31) {
            LOCK();
            g.rssi = -113 + 2 * a;
            g.has_signal = true;
            UNLOCK();
        }
    }
}

/* 服务端推的 QSCAN（对象数组；优先 NR5G，其次第一个） */
static void apply_scan_obj(const char *obj)
{
    char proto[16] = "", mcc[8] = "", mnc[8] = "", code[16];
    int rsrp = 0, rsrq = 0, sinr = 0;

    tj_get_str(obj, "Protocol", proto, sizeof(proto));
    tj_get_str(obj, "MCC", mcc, sizeof(mcc));
    tj_get_str(obj, "MNC", mnc, sizeof(mnc));
    tj_get_int(obj, "RSRP", &rsrp);
    tj_get_int(obj, "RSRQ", &rsrq);
    tj_get_int(obj, "SINR", &sinr);

    code[0] = '\0';
    if (mcc[0] && mnc[0])
        snprintf(code, sizeof(code), "%s%s", mcc, mnc);

    apply_scan(proto, code[0] ? code : NULL, rsrp, rsrq, sinr);
}

static void on_qscan(const char *d)
{
    char obj[512], proto[16];
    int chosen = -1;

    if (!d || !*d)
        return;

    if (*d == '{') {          /* 单个对象也吃 */
        apply_scan_obj(d);
        return;
    }

    for (int i = 0; i < 16; i++) {
        if (!tj_get_arr_obj(d, NULL, i, obj, sizeof(obj)))
            break;
        if (!tj_get_str(obj, "Protocol", proto, sizeof(proto)))
            continue;
        if (find_ci(proto, "NR")) {     /* 优先 5G */
            chosen = i;
            break;
        }
        if (chosen < 0)
            chosen = i;
    }

    if (chosen < 0)
        return;
    if (tj_get_arr_obj(d, NULL, chosen, obj, sizeof(obj)))
        apply_scan_obj(obj);
}

/* ------------------------------------------------------------------ AT */

struct at_walk {
    const char *data;
};

static void at_each_cb(const char *key, const char *val, const char *val_end, void *user)
{
    struct at_walk *w = (struct at_walk *)user;
    char buf[512];

    (void)val;
    (void)val_end;

    {
        /* 任何一条应答都拿来找信号/基站（找不到就什么都不做） */
        if (tj_get_str(w->data, key, buf, sizeof(buf))) {
            scan_text_qscan(buf);
            scan_text_signal(buf);
        }
    }

    if (find_ci(key, "cops")) {
        if (tj_get_str(w->data, key, buf, sizeof(buf)))
            parse_cops(buf);
        return;
    }

    /* 型号：+CGMM / +CGMR / ^CGMM 之类 */
    if (find_ci(key, "cgmm") || find_ci(key, "cgmr")) {
        char *nl;
        if (tj_get_str(w->data, key, buf, sizeof(buf))) {
            nl = strpbrk(buf, "\r\n");
            if (nl)
                *nl = '\0';
            if (buf[0]) {
                LOCK();
                if (!g.model[0])
                    set_str(g.model, sizeof(g.model), buf);
                UNLOCK();
            }
        }
    }
}

static void on_at(const char *d)
{
    struct at_walk w;

    if (!d || *d != '{')
        return;
    w.data = d;
    tj_each(d, at_each_cb, &w);
}

/* ------------------------------------------------- 模组事件(EventAdded) */

static void on_event(const char *d)
{
    char buf[128];

    LOCK();
    if (tj_get_str(d, "Model", buf, sizeof(buf)) && buf[0])
        set_str(g.model, sizeof(g.model), buf);
    else if (tj_get_str(d, "Product", buf, sizeof(buf)) && buf[0])
        set_str(g.model, sizeof(g.model), buf);
    if (tj_get_str(d, "Device", buf, sizeof(buf)) && buf[0] && !g.dev[0])
        set_str(g.dev, sizeof(g.dev), buf);
    UNLOCK();
}

/* ------------------------------------------------------------ 分发入口 */

/* =============== modem（设备中心 modemd）原生推送 ===============
 *
 * 报文形状 {type, modem_id, at, data}（字段名见 corev2 的 json tag）：
 *
 *   network  → NetworkStatus{operator, tech, tech_label, mcc, mnc, plmn, ipv4, ipv6,
 *                            serving{tech,rsrp,rsrq,rssi,sinr,mcc,mnc,operator},
 *                            traffic{up_bps,down_bps,sent,recv},
 *                            cs_reg{registered}, ps_reg{registered}}
 *   signal   → SignalInfo{rssi, ber, nr_rsrp, nr_rsrq, nr_sinr}
 *              ⚠ rssi 是 +CSQ 的**原始值**（0..31，99 = 无法测得），不是 dBm
 *   traffic  → Rate{up_bps, down_bps, sent, recv}（已经是 byte/s）
 *   modems   → []Modem{model, signal{...}, sms_total, sms_unread,
 *                      modem_status{temp_max{name, celsius}}}
 *   smscount → {total, unread}
 */

#define SIG_RANK_SERVING 1
#define SIG_RANK_NR      2

/* 服务端给的是名字（可能是中文，也可能是"CHINA TELECOM"这种） */
static bool has_nonascii(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++)
        if (*p >= 0x80)
            return true;
    return false;
}

/* byte/s 或累计字节 → "12.34 KB"（速率调用方再补 "/s"） */
static void fmt_bytes(double v, char *out, size_t cap)
{
    static const char *unit[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;

    if (v < 0)
        v = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        i++;
    }
    if (i == 0)
        snprintf(out, cap, "%.0f %s", v, unit[i]);
    else
        snprintf(out, cap, "%.2f %s", v, unit[i]);
}

/* modemd 的 CellTech（"NR" / "LTE" / "WCDMA" / "GSM"）→ 内部口径 */
static const char *rat_from_tech(const char *tech)
{
    if (!tech || !*tech)
        return NULL;
    if (strstr(tech, "NR"))
        return "NR";
    if (find_ci(tech, "LTE"))
        return "LTE";
    if (find_ci(tech, "WCDMA") || find_ci(tech, "UMTS"))
        return "WCDMA";
    if (find_ci(tech, "GSM"))
        return "GSM";
    return NULL;
}

/* NR 那一路信号（优先级高于服务小区）。需持锁。 */
static void apply_nr_signal_locked(int rsrp, int rsrq, int sinr, int csq)
{
    if (rsrp < 0) {                     /* 只有真实的负值才算"有信号" */
        g.rsrp = rsrp;
        if (rsrq < 0)
            g.rsrq = rsrq;
        g.sinr = sinr;
        g.has_signal = true;
        g_sig_rank = SIG_RANK_NR;
    } else if (g_sig_rank == SIG_RANK_NR) {
        g_sig_rank = 0;                 /* NR 不再上报：让服务小区那一路接管 */
        g.has_signal = false;
    }
    if (csq > 0 && csq <= 31)
        g.rssi = -113 + 2 * csq;        /* +CSQ 原始值 → dBm */
}

/* Rate 对象 → 速率与累计量（byte/s，不必再解析文本）。需持锁。 */
static void apply_rate_locked(const char *d)
{
    double up = 0, down = 0, sent = 0, recv = 0;
    char t[40];
    bool sup = true;

    if (tj_get_bool(d, "supported", &sup) && !sup)
        return;                         /* 模组不上报流量统计 */

    tj_get_num(d, "up_bps", &up);
    tj_get_num(d, "down_bps", &down);
    tj_get_num(d, "sent", &sent);
    tj_get_num(d, "recv", &recv);

    g.sent_bps = up;
    g.recv_bps = down;
    fmt_bytes(up, t, sizeof(t));
    snprintf(g.sent_rate, sizeof(g.sent_rate), "%s/s", t);
    fmt_bytes(down, t, sizeof(t));
    snprintf(g.recv_rate, sizeof(g.recv_rate), "%s/s", t);
    if (sent > 0) {
        fmt_bytes(sent, t, sizeof(t));
        set_str(g.total_sent, sizeof(g.total_sent), t);
    }
    if (recv > 0) {
        fmt_bytes(recv, t, sizeof(t));
        set_str(g.total_recv, sizeof(g.total_recv), t);
    }
}

/* signal 推送：SignalInfo{rssi, nr_rsrp, nr_rsrq, nr_sinr} */
static void on_md_signal(const char *d)
{
    int rsrp = 0, rsrq = 0, sinr = 0, csq = 0;

    tj_get_int(d, "nr_rsrp", &rsrp);
    tj_get_int(d, "nr_rsrq", &rsrq);
    tj_get_int(d, "nr_sinr", &sinr);
    tj_get_int(d, "rssi", &csq);

    LOCK();
    apply_nr_signal_locked(rsrp, rsrq, sinr, csq);
    UNLOCK();
}

/* traffic 推送：Rate{...} */
static void on_md_traffic(const char *d)
{
    LOCK();
    apply_rate_locked(d);
    UNLOCK();
}

/* smscount 推送：{total, unread}（只有数字，没有正文） */
static void on_md_smscount(const char *d)
{
    int total = 0, unread = 0;

    tj_get_int(d, "total", &total);
    tj_get_int(d, "unread", &unread);

    LOCK();
    g.sms_total = total;
    g.sms_unread = unread;
    UNLOCK();
}

/* sms 推送：短信列表（JSON 数组，按时间倒序）。
 *
 * ⭐ 长短信（UDH 拼接）服务端已经合并成**一条**（corev2 的 GroupSMSConcat），
 * 这里拿到的就是"3 合 1"那条，`part_total` 告诉界面它由几段合起来的。
 *
 * 顺带判定"有没有新短信"：拿最新一条（号码+时间+正文）跟上一份比，不一样就算新的。
 * ⚠️ 第一次收到只建基线、不报新（否则屏幕一上线就把旧短信当新短信弹窗）。 */
static void on_md_sms(const char *d)
{
    static ws_sms_t tmp[WS_SMS_MAX];      /* static：一条就 400+ 字节，别放线程栈 */
    char obj[4096];
    char st[32];
    int  n = 0, unread = 0;
    int  iv = 0;
    bool fresh = false;

    if (!d || d[0] != '[')
        return;

    for (int i = 0; i < WS_SMS_MAX; i++) {
        ws_sms_t *m;

        if (!tj_get_arr_obj(d, NULL, i, obj, sizeof(obj)) || obj[0] != '{')
            break;
        m = &tmp[n];
        memset(m, 0, sizeof(*m));
        tj_get_str(obj, "index", m->index, sizeof(m->index));
        tj_get_str(obj, "number", m->number, sizeof(m->number));
        tj_get_str(obj, "timestamp", m->time, sizeof(m->time));
        tj_get_str(obj, "content", m->text, sizeof(m->text));
        /* 未读判据同服务端：文本模式回 "REC UNREAD"，PDU 模式回数字 0 */
        st[0] = '\0';
        if (tj_get_str(obj, "status", st, sizeof(st)) && st[0]) {
            if (strcmp(st, "0") == 0 || find_ci(st, "unread") != NULL)
                m->unread = 1;
        }
        if (tj_get_int(obj, "part_total", &iv))
            m->part_total = iv;
        /* 长短信各段的**存储位置**：删的时候要逐段删（只删一段会留下"孤儿段"，
         * 服务端重拼后又出现 ⇒ 看着像"删不掉"）。 */
        m->part_n = 0;
        for (int k = 0; k < WS_SMS_PART_MAX; k++) {
            char po[320];

            if (!tj_get_arr_obj(obj, "parts", k, po, sizeof(po)) || po[0] != '{')
                break;
            if (tj_get_str(po, "index", m->part_idx[m->part_n], sizeof(m->part_idx[0])))
                m->part_n++;
        }
        unread += m->unread ? 1 : 0;
        n++;
    }

    LOCK();
    if (g.sms_seen && n > 0) {
        const ws_sms_t *a = &tmp[0];
        bool newer_time = false;

        /* ⚠️ "有没有新短信"的判据（2026-10-07 三轮才定稿，**别再改回按内容比对**）：
         *   ✗ "新头部跟上一份的头部逐字段比（号码+时间+正文）"——删掉最新那条之后新头部变成
         *     原来第二条，三个字段全不同 ⇒ 误判成新短信、平白弹窗（用户报的第 1 次）。
         *   ✗ "新头部在上一份列表里找不找得到"——**长短信**（多段拼接，实测江苏电信流量提醒
         *     是 part_total=3、各段各占一个 index）删掉其中一段后服务端重拼：头部还是同一条
         *     （号码/时间都没变）**但正文变了** ⇒ 又误判（用户报的第 2 次，真机把 `sms` 帧
         *     抓下来看到 parts/index 才定位到）。
         *   ✓ 只看两个**单调量**，都不受"正文变了"影响：
         *       ① 时间戳变新（列表按时间倒序 ⇒ 真来新短信，头部时间戳晚于上一份头部的）；
         *       ② 未读数变多（新到的那条一定是未读；删短信/标已读都只会让它变小）。
         *     时间戳是服务端给的固定格式（`26/10/07,20:00:00+32`），逐字节比大小即可。 */
        if (g.sms_n <= 0) {
            newer_time = true;                  /* 上一份是空的 ⇒ 这条算新的 */
        } else if (a->time[0] && g.sms[0].time[0]) {
            newer_time = (strcmp(a->time, g.sms[0].time) > 0);
        }
        fresh = newer_time || (unread > g.sms_unread);
    }
    if (!g.sms_seen || fresh || n != g.sms_n) {
        memcpy(g.sms, tmp, sizeof(tmp[0]) * (size_t)n);
        g.sms_n = n;
        g.sms_seq++;
        if (fresh) {
            g.sms_new_seq++;
            g.sms[0].unread = 1;      /* 刚到的这条一定是未读 */
            unread = unread > 0 ? unread : 1;
        }
        g.sms_seen = true;
    }
    g.sms_total = n;
    g.sms_unread = unread;
    UNLOCK();
}

/* network 推送：制式 / 运营商 / IP / 注册状态 / 服务小区信号 / 流量 */
static void on_md_network(const char *d)
{
    char name[64] = "", tech[32] = "", mcc[16] = "", mnc[16] = "";
    char ip4[64] = "", ip6[128] = "";
    char cell_state[24] = "";
    char sub[2048];
    char ips[256];
    /* ---- NET 页（网络参数）那一组 ---- */
    char nt[32] = "", bt[24] = "", bl[16] = "", plmn[16] = "", mm[20] = "";
    char ca[40] = "", cbuf[512] = "", lb[24] = "", role[8] = "";
    int  earfcn = 0, band = 0, cc = 0, serving_n = 0, neigh_n = 0;
    bool ca_active = false, ca_bv = false;
    int rsrp = 0, rsrq = 0, sinr = 0, rssi = 0;
    bool has_serving = false, bv = false, has_cs = false, has_ps = false;
    bool cs = false, ps = false;
    bool reg5g = false, reg_eps = false, has_reg5g = false, has_eps = false;
    bool cell_connect, reg_any, has_ip;
    /* serving 对象在不在（服务小区数在服务端就是 "没有扫描缓存时实时补一次"） */
    bool serving_obj = false;

    tj_get_str(d, "operator", name, sizeof(name));
    tj_get_str(d, "tech", tech, sizeof(tech));
    tj_get_str(d, "mcc", mcc, sizeof(mcc));
    tj_get_str(d, "mnc", mnc, sizeof(mnc));
    tj_get_str(d, "ipv4", ip4, sizeof(ip4));
    tj_get_str(d, "ipv6", ip6, sizeof(ip6));

    if (tj_get_raw(d, "serving", sub, sizeof(sub)) && sub[0] == '{') {
        serving_obj = true;
        tj_get_int(sub, "rsrp", &rsrp);
        tj_get_int(sub, "rsrq", &rsrq);
        tj_get_int(sub, "sinr", &sinr);
        tj_get_int(sub, "rssi", &rssi);
        tj_get_str(sub, "state", cell_state, sizeof(cell_state));   /* CONNECT / CAMPED / SEARCH … */
        if (!tech[0])
            tj_get_str(sub, "tech", tech, sizeof(tech));
        if (!mcc[0])
            tj_get_str(sub, "mcc", mcc, sizeof(mcc));
        if (!mnc[0])
            tj_get_str(sub, "mnc", mnc, sizeof(mnc));
        if (!name[0])
            tj_get_str(sub, "operator", name, sizeof(name));
        has_serving = (rsrp < 0);
    }

    /* ---- NET 页（网络参数）：与 web 端「网络类型 / 接入信息」同一批字段 ---- */
    tj_get_str(d, "net_tech", nt, sizeof(nt));
    tj_get_str(d, "band_text", bt, sizeof(bt));
    tj_get_str(d, "band_label", bl, sizeof(bl));
    tj_get_str(d, "plmn", plmn, sizeof(plmn));
    tj_get_int(d, "earfcn", &earfcn);
    tj_get_int(d, "band", &band);
    tj_get_int(d, "serving_count", &serving_n);
    tj_get_int(d, "neighbour_count", &neigh_n);
    if (!bt[0] && band > 0 && tech[0]) {
        /* 模组不给 band_text 时按 "<制式> BAND <号>" 兜一个，别让「频段」空着 */
        snprintf(bt, sizeof(bt), "%s BAND %d", tech, band);
    }
    /* 载波聚合：列表里**含主载波**（PCC），有非 PCC 的才算真聚合（与 web 端 caPill 一致） */
    if (tj_get_bool(d, "ca_active", &ca_bv))
        ca_active = ca_bv;
    for (int i = 0; i < 8; i++) {
        if (!tj_get_arr_obj(d, "ca", i, cbuf, sizeof(cbuf)) || cbuf[0] != '{')
            break;
        cc++;
        role[0] = '\0';
        tj_get_str(cbuf, "role", role, sizeof(role));
        if (role[0] && !find_ci(role, "PCC"))
            ca_active = true;               /* 有辅载波 ⇒ 真的在聚合 */
        lb[0] = '\0';
        if (!tj_get_str(cbuf, "band_label", lb, sizeof(lb)) || !lb[0]) {
            if (!tj_get_str(cbuf, "band_text", lb, sizeof(lb)))
                lb[0] = '\0';
        }
        if (lb[0]) {
            if (ca[0])
                strncat(ca, "+", sizeof(ca) - strlen(ca) - 1);
            strncat(ca, lb, sizeof(ca) - strlen(ca) - 1);
        }
    }
    if (tj_get_raw(d, "cs_reg", sub, sizeof(sub)) && sub[0] == '{')
        has_cs = tj_get_bool(sub, "registered", &cs);
    if (tj_get_raw(d, "ps_reg", sub, sizeof(sub)) && sub[0] == '{')
        has_ps = tj_get_bool(sub, "registered", &ps);
    /* 5G SA 下 +CGREG 常常回 0,0（看着像"未注册"），真正的注册在 +C5GREG（reg_5g）里 —— 
     * 服务端 only 在"CS/PS 都看着没注册"时才补查它，所以这里必须一并认。 */
    if (tj_get_raw(d, "reg_5g", sub, sizeof(sub)) && sub[0] == '{')
        has_reg5g = tj_get_bool(sub, "registered", &reg5g);
    if (tj_get_raw(d, "reg_eps", sub, sizeof(sub)) && sub[0] == '{')
        has_eps = tj_get_bool(sub, "registered", &reg_eps);

    /* 流量也常常挂在这条里（NetworkStatus.Traffic）；取出来交给同一个函数处理 */
    {
        char rate[512];

        if (tj_get_raw(d, "traffic", rate, sizeof(rate)) && rate[0] == '{') {
            LOCK();
            apply_rate_locked(rate);
            UNLOCK();
        }
    }

    LOCK();
    if (has_serving && g_sig_rank < SIG_RANK_NR) {
        g.rsrp = rsrp;
        g.rsrq = rsrq;
        g.sinr = sinr;
        if (rssi < 0)
            g.rssi = rssi;
        g.has_signal = true;
        g_sig_rank = SIG_RANK_SERVING;
    }
    if (tech[0]) {
        const char *r = rat_from_tech(tech);

        if (r)
            set_str(g.rat_scan, sizeof(g.rat_scan), r);
    }
    if (name[0]) {
        /* modemd 给的已经是中文名时就照用，否则按名字/号码归类 */
        const char *cn = has_nonascii(name) ? name : op_name_of(name);

        if (cn)
            set_str(g.op_scan, sizeof(g.op_scan), cn);
        else
            set_str(g.op_scan, sizeof(g.op_scan), name);
        set_str(g.cops, sizeof(g.cops), classify_operator(name));
    }
    if (mcc[0] && mnc[0]) {
        snprintf(g.mcc, sizeof(g.mcc), "%s%s", mcc, mnc);
        snprintf(g.mccmnc, sizeof(g.mccmnc), "%s/%s", mcc, mnc);
    }

    /* ---- NET 页那一组：每轮都覆盖（服务端 omitempty 就是"这次没上报"，
     * 留着上一轮的值会显示成过期数据，不如显示 "-"） ---- */
    set_str(g.net_tech, sizeof(g.net_tech), nt);
    set_str(g.band_text, sizeof(g.band_text), bt);
    set_str(g.band_label, sizeof(g.band_label), bl);
    g.earfcn = earfcn;
    set_str(g.ca_text, sizeof(g.ca_text), ca);
    g.ca_cc = cc;
    g.ca_active = ca_active;
    set_str(g.plmn, sizeof(g.plmn), plmn);
    g.neigh_count = neigh_n;
    /* 服务小区数：扫描缓存里数出来的优先；没有缓存但实时补到了服务小区 ⇒ 1
     * （与 modemd 的 net.go 一致：st.ServingCount==0 && st.Serving != nil → 1） */
    g.serving_count = (serving_n > 0) ? serving_n : (serving_obj ? 1 : 0);

    ips[0] = '\0';
    if (ip4[0])
        strncat(ips, ip4, sizeof(ips) - 1);
    if (ip6[0]) {
        if (ips[0])
            strncat(ips, "\n", sizeof(ips) - strlen(ips) - 1);
        strncat(ips, ip6, sizeof(ips) - strlen(ips) - 1);
    }
    if (ips[0])
        set_str(g.ips, sizeof(g.ips), ips);

    /*
     * 底部那三个状态块（在网 / 有地址 / 数据可通）的判据。
     * ⚠️ **不能只看 CREG/CGREG**：实测这台 RM500U（LTE B5）明明已拨号、有 IPv4/IPv6、
     * 流量在走，却回 `+CREG: 0,0` / `+CGREG: 0,0` / `+C5GREG: 0,0`（全报未注册）——
     * 只看那三个标志会把“好好的”判成断网。所以并看服务小区状态。
     */
    cell_connect = (cell_state[0] != '\0' && find_ci(cell_state, "connect") != NULL);
    reg_any      = (cs || ps || reg5g || reg_eps);
    has_ip       = (ip4[0] != '\0' || ip6[0] != '\0');

    g.ip_up        = (reg_any || cell_connect || has_ip);   /* 链路在用 */
    g.ip_hasip     = has_ip;                                /* 拿到地址 */
    g.ip_connected = (has_ip && (cell_connect || reg_any)); /* 数据可通（有地址且有网） */

    resolve_locked();
    UNLOCK();
}

/* 组网模式偏好推送（type = "mode"）：{value, name, msg, read_only, screen:[{value,name,writable}]}
 *
 * 屏幕的 Mobile 页第 3 屏就是拿它画的（4G / 5G / 4G+5G 三个挡位按钮）。
 * 用户点一下 → 屏幕发 {"action":"mode_set","value":"LTE"} → 服务端写模组并回推一份新的。 */
static void on_md_mode(const char *d)
{
    static int mode_dbg_n;
    char buf[128];
    bool bv = false;
    int  n  = 0;

    LOCK();
    if (tj_get_str(d, "value", buf, sizeof(buf)))
        set_str(g.mode.mode_value, sizeof(g.mode.mode_value), buf);
    if (tj_get_str(d, "name", buf, sizeof(buf)))
        set_str(g.mode.mode_name, sizeof(g.mode.mode_name), buf);
    if (tj_get_str(d, "short", buf, sizeof(buf)))
        set_str(g.mode.mode_short, sizeof(g.mode.mode_short), buf);
    if (tj_get_str(d, "msg", buf, sizeof(buf)))
        set_str(g.mode.mode_msg, sizeof(g.mode.mode_msg), buf);
    /* ⚠️ read_only / writable 是 JSON 的 **true/false**，必须用 tj_get_bool ——
     * tj_get_int 只认数字，拿它读布尔会永远得到 0（踩过：三个挡位全被判成"不可写"，
     * 点上去只回一句"本机不支持这一档"）。 */
    g.mode.mode_readonly = (tj_get_bool(d, "read_only", &bv) && bv) ? 1 : 0;

    /* screen 数组 = 屏幕上从左到右那几个按钮（服务端已经算好能不能按） */
    for (int i = 0; i < WS_MODE_BTN_MAX; i++) {
        char obj[384];
        char v[24], nm[24];

        if (!tj_get_arr_obj(d, "screen", i, obj, sizeof(obj)))
            break;
        v[0] = nm[0] = '\0';
        tj_get_str(obj, "value", v, sizeof(v));
        tj_get_str(obj, "name", nm, sizeof(nm));
        if (!v[0])
            break;
        set_str(g.mode.mode_btn[i].value, sizeof(g.mode.mode_btn[i].value), v);
        set_str(g.mode.mode_btn[i].name, sizeof(g.mode.mode_btn[i].name), nm[0] ? nm : v);
        g.mode.mode_btn[i].writable = (tj_get_bool(obj, "writable", &bv) && bv) ? 1 : 0;
        if (getenv("TFT_UI_DEBUG") && mode_dbg_n < 4) {
            mode_dbg_n++;
            printf("[mode-dbg] item%d = %s  ⇒ writable=%d\n", i, obj, g.mode.mode_btn[i].writable);
            fflush(stdout);
        }
        n++;
    }
    g.mode.mode_btn_n = n;
    g.mode.mode_seq++;
    UNLOCK();
}

/* modems 推送：模组列表（取第一个：屏幕就一台模组） */
static void on_md_modems(const char *d)
{
    char *obj = (char *)malloc(16384);
    char buf[128];
    char sub[2048];
    int iv = 0;

    if (!obj)
        return;
    if (!tj_get_arr_obj(d, NULL, 0, obj, 16384)) {
        free(obj);
        return;
    }

    LOCK();
    if (tj_get_str(obj, "model", buf, sizeof(buf)) && buf[0])
        set_str(g.model, sizeof(g.model), buf);
    if (tj_get_int(obj, "sms_total", &iv))
        g.sms_total = iv;
    if (tj_get_int(obj, "sms_unread", &iv))
        g.sms_unread = iv;

    if (tj_get_raw(obj, "signal", sub, sizeof(sub)) && sub[0] == '{') {
        int rsrp = 0, rsrq = 0, sinr = 0, csq = 0;

        tj_get_int(sub, "nr_rsrp", &rsrp);
        tj_get_int(sub, "nr_rsrq", &rsrq);
        tj_get_int(sub, "nr_sinr", &sinr);
        tj_get_int(sub, "rssi", &csq);
        apply_nr_signal_locked(rsrp, rsrq, sinr, csq);
    }
    /* 模组温度（+QTEMP 之类）：拿去填 INFO 页的 TEMP */
    if (tj_get_raw(obj, "modem_status", sub, sizeof(sub)) && sub[0] == '{') {
        char t2[512];

        if (tj_get_raw(sub, "temp_max", t2, sizeof(t2)) && t2[0] == '{' &&
            tj_get_int(t2, "celsius", &iv) && iv > 0)
            g.temp = iv;
    }
    UNLOCK();

    free(obj);
}

/* 余额查询短信的发送结果（type = "sendsms"）：{number, text, ok, msg}
 *
 * 屏幕的 SMS 页最后一屏（余额查询）发完 {"action":"sms_send",…} 后，
 * 服务端发完（成败都推）回这条；那屏就把 msg 写到状态行上。 */
static void on_md_sendsms(const char *d)
{
    char buf[160];
    bool ok = false;

    LOCK();
    if (tj_get_str(d, "msg", buf, sizeof(buf)))
        set_str(g.sendsms_msg, sizeof(g.sendsms_msg), buf);
    if (tj_get_str(d, "number", buf, sizeof(buf)))
        set_str(g.sendsms_num, sizeof(g.sendsms_num), buf);
    g.sendsms_ok = (tj_get_bool(d, "ok", &ok) && ok) ? true : false;
    g.sendsms_seq++;
    UNLOCK();
}

void wsdata_on_message(const char *json)
{
    char type[32];
    char *data;

    if (!json || *json != '{')
        return;

    type[0] = '\0';
    tj_get_str(json, "type", type, sizeof(type));

    /* data 可能很大（AT 那一坨），放堆上，别吃线程栈 */
    data = (char *)malloc(65536);
    if (!data)
        return;
    if (!tj_get_raw(json, "data", data, 65536))
        data[0] = '\0';

    /* 宿主信息可以搭在任何一条推送里（服务端补 Sys 推送前也能先用） */
    if (data[0] == '{')
        apply_host_info(data);

    if (key_is(type, "IP"))
        on_ip(data);
    else if (key_is(type, "Speed"))
        on_speed(data);
    else if (key_is(type, "AT"))
        on_at(data);
    else if (key_is(type, "QSCAN"))
        on_qscan(data);
    else if (find_ci(type, "Event"))
        on_event(data);
    /* ---- modem（modemd 设备中心）原生推送 ---- */
    else if (key_is(type, "network"))
        on_md_network(data);
    else if (key_is(type, "signal"))
        on_md_signal(data);
    else if (key_is(type, "traffic"))
        on_md_traffic(data);
    else if (key_is(type, "modems"))        on_md_modems(data);
    else if (key_is(type, "smscount"))
        on_md_smscount(data);
    else if (key_is(type, "sms"))
        on_md_sms(data);          /* 短信列表（长短信已合并成一条） */
    else if (key_is(type, "mode"))
        on_md_mode(data);         /* 组网模式偏好（Mobile 页第 3 屏的 4G / 5G / 4G+5G） */
    else if (key_is(type, "sendsms"))
        on_md_sendsms(data);      /* 余额查询短信的发送结果（SMS 页最后一屏） */

    LOCK();
    g.msgs++;
    UNLOCK();

    free(data);
}

void wsdata_set_online(bool on)
{
    LOCK();
    g.online = on;
    if (!on)
        snprintf(g.state, sizeof(g.state), "%s", "未连接");
    UNLOCK();
}

void wsdata_set_state(const char *text)
{
    /* 要足够大：比它长的文案会被截断，截断后逐次比较就不相等 ⇒ 每次重连都刷一行日志 */
    static char last_logged[192];
    bool changed = false;
    bool log_it = false;

    LOCK();
    if (text && strcmp(g.state, text) != 0) {
        set_str(g.state, sizeof(g.state), text);
        changed = true;
    }
    UNLOCK();

    /*
     * 只在"结果态"变化时打一行：procd 抓 stdout ⇒ logread 里能看到
     * “为什么没有数据”。中间态（“正在连接 ...”）与重复的同一个错误不打，
     * 否则 5 秒一次的重连会把 logread 刷满。
     */
    if (changed && strncmp(text, "正在", 6) != 0 && strcmp(last_logged, text) != 0) {
        snprintf(last_logged, sizeof(last_logged), "%s", text);
        log_it = true;
    }

    if (log_it)
        printf("tftdisplay ws: %s\n", text);
}

bool wsdata_online(void)
{
    bool v;
    LOCK();
    v = g.online;
    UNLOCK();
    return v;
}

/* 短信条数（轻量：不动那 7 KB 的快照，给 page_col_count() 这类频繁调用的地方用） */
int wsdata_sms_count(void)
{
    int n;
    LOCK();
    n = g.sms_n;
    UNLOCK();
    return n < 0 ? 0 : n;
}

/* 组网模式偏好（轻量：不动那 8KB 的大快照；Mobile 页几百毫秒就刷一次） */
bool wsdata_mode(ws_mode_t *out)
{
    if (!out)
        return false;
    LOCK();
    *out = g.mode;
    UNLOCK();
    return out->mode_btn_n > 0 || out->mode_value[0] != '\0';
}

void wsdata_snapshot(ws_snapshot_t *out)
{
    if (!out)
        return;
    LOCK();
    *out = g;
    out->fresh = (g.msgs != g_ui_seen);
    g_ui_seen = g.msgs;
    UNLOCK();
}
