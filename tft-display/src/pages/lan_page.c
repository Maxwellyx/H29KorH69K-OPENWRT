/*
 * SYSTEM → LAN 配置页：网口角色一键 LAN ⇄ WAN + 恢复原始配置
 *
 * 全部本地实现（自己解析 /etc/config/network，用 uci 写回后 reload 网络）：
 *   · 端口从 /sys/class/net/eth* 枚举（**只列真实存在的口**，本机只有 eth0），最多显示 2 行；
 *   · 上面一排 = [ETHX 块] [LAN 块] [WAN 块]（三个**大块**，顶部留 8px）——
 *     后两个就是**并排的两个按钮**：当前角色那个高亮（LAN 绿 / WAN 橙），另一个是灰的，
 *     点灰的那个就把这个口切成那个角色；
 *   · 中间一行 = 一块**小**牌子：`原始 LAN=ETH0`（只报 LAN 那一边，不显示 WAN；
 *     切换中/报错的状态文字也写在这块上；⚠️ 写 uci 时仍然用系统里的小写名 eth0）；
 *   · ⚠️ **点切换/恢复都不会当场执行**：先弹顶层二次确认弹窗（确认 / 取消，
 *     跟短信删除确认一个做法），确认了才真去改配置 —— 防误触（本机只有 1 个网口，
 *     误点成 WAN 会把有线 ssh 弄断，只能拖 WiFi/屏幕切回）。
 *   · 调试图：`TFT_LAN_CONFTEST=1 /usr/bin/tftdisplay -page 5 -col 2` ⇒ 开屏就弹确认框（拍屏用）。
 *   · 角色判定：等于 wan(或 wan6) 的 device = WAN；在 br-lan 成员里 = LAN；都不是 = 空闲（两个都不高亮）；
 *   · **只切这一个口**的角色，不做两口感互换：
 *       切 WAN：把它设成 wan/wan6 的 device，并从 br-lan 成员里去掉；
 *       切 LAN：把它加回 br-lan 成员；若 wan 还指着它，换回记录的原始 WAN 口，
 *               记录里也没有别的口就把它删掉（别让一个口既当 LAN 又当 WAN）。
 *   · "恢复原始配置"：第一次打开本页时把当时的布局记进 /etc/tftdisplay.lan，
 *     恢复时按那份记录重写 —— **只动 lan 成员 + wan/wan6 的 device，不整份覆盖 /etc/config/network**
 *     （modemd 会往里加 modem0/modem0_6/route6/NDP，整份快照回写会把它的东西冲掉）。
 *
 * ⚠️ 改完**必须**跟一条 `ubus call network reload`：实测只 `uci commit network` 新配置进不了内核。
 * ⚠️ uci + reload 都丢后台执行（reload 好几秒，同步跑会冻住 LVGL 主循环）。
 * ⚠️ 切换/恢复会换掉当前那个物理口的角色 —— 若正通过它 ssh 会断线（预期行为，按钮/网线能换回来）。
 */
#include "../lvgl/lvgl.h"
#include "lan_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>

lv_obj_t *lan_page;

#define NETWORK_CONF   "/etc/config/network"
#define LAN_STATE_CONF "/etc/tftdisplay.lan"     /* 原始布局（包外文件，升级不覆盖） */
#define MAX_PORT       4
#define SHOW_PORT      2                         /* 屏幕上显示几行端口 */
#define PORT_NAME_LEN  16

LV_FONT_DECLARE(lv_font_cjk_sms_16);
#define LAN_FONT (&lv_font_cjk_sms_16)

typedef struct {
    char brdev_uci[48];                  /* br-lan 的 uci 段名：'@device[0]' 或显式名字 */
    char lan_ports[MAX_PORT][PORT_NAME_LEN];
    int  lan_port_n;
    char wan_dev[PORT_NAME_LEN];
    char wan6_dev[PORT_NAME_LEN];
    bool has_wan6;
    bool legacy;                         /* true = 老式 network.lan.ifname */
} netinfo_t;

static char      g_ports[MAX_PORT][PORT_NAME_LEN];
static int       g_port_n;
static lv_obj_t *g_name_box[MAX_PORT];      /* ethX 的块 */
static lv_obj_t *g_name_label[MAX_PORT];
static lv_obj_t *g_lan_btn[MAX_PORT];       /* LAN / WAN 两个并排的块按钮 */
static lv_obj_t *g_lan_lbl[MAX_PORT];
static lv_obj_t *g_wan_btn[MAX_PORT];
static lv_obj_t *g_wan_lbl[MAX_PORT];
static lv_obj_t *g_info_lbl;                /* 中间那块小牌子（原始 LAN / 状态文字） */
static uint32_t  g_busy_until;           /* 切换冷却（LVGL tick = 开机毫秒） */

/* ---- 二次确认弹窗（挂顶层 `lv_layer_top`，默认隐藏）---- */
static lv_obj_t *conf_pop, *conf_title, *conf_text, *conf_ok;
static int       conf_idx;               /* >=0 = 要切的口；-1 = 没待确认的切换 */
static int       conf_wan;               /* 待确认的目标角色（0=LAN 1=WAN） */
static bool      conf_restore;            /* true = 待确认的是"恢复原始配置" */

/* 前向声明：按钮回调在上面先用到，弹窗函数在下面才写 */
static void conf_open_switch(int idx, int want_wan);
static void conf_open_restore(void);
static void conf_hide(void);

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static void add_unique(char dst[][PORT_NAME_LEN], int *n, const char *v)
{
    if (!v || !*v || *n >= MAX_PORT) return;
    for (int i = 0; i < *n; i++)
        if (strcmp(dst[i], v) == 0) return;
    snprintf(dst[*n], PORT_NAME_LEN, "%s", v);
    (*n)++;
}

/* 取一行里 "option <name> '…'" 的值 */
static bool opt_value(const char *line, const char *name, char *out, size_t outlen)
{
    const char *p = line;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (strncmp(p, "option", 6) != 0) return false;
    p += 6;
    while (*p == ' ' || *p == '\t') p++;

    size_t nl = strlen(name);
    if (strncmp(p, name, nl) != 0) return false;
    p += nl;
    if (*p != ' ' && *p != '\t') return false;
    while (*p == ' ' || *p == '\t') p++;

    char quote = 0;
    if (*p == '\'' || *p == '"') quote = *p++;

    size_t o = 0;
    while (*p && o + 1 < outlen) {
        if (quote) { if (*p == quote) break; }
        else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') break;
        out[o++] = *p++;
    }
    out[o] = '\0';
    return true;
}

/* 取一行里 "list <name> '…'" 的值 */
static bool list_value(const char *line, const char *name, char *out, size_t outlen)
{
    const char *p = line;

    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "list", 4) != 0) return false;
    p += 4;
    while (*p == ' ' || *p == '\t') p++;

    size_t nl = strlen(name);
    if (strncmp(p, name, nl) != 0) return false;
    p += nl;
    if (*p != ' ' && *p != '\t') return false;
    while (*p == ' ' || *p == '\t') p++;

    char quote = 0;
    if (*p == '\'' || *p == '"') quote = *p++;

    size_t o = 0;
    while (*p && o + 1 < outlen) {
        if (quote) { if (*p == quote) break; }
        else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') break;
        out[o++] = *p++;
    }
    out[o] = '\0';
    return true;
}

/* config <type> ['name']；命中返回 true，段名（可能为空串）写进 name */
static bool section_is(const char *line, const char *type, char *name, size_t namelen)
{
    const char *p = line;

    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "config", 6) != 0) return false;
    p += 6;
    while (*p == ' ' || *p == '\t') p++;

    size_t tl = strlen(type);
    if (strncmp(p, type, tl) != 0) return false;
    p += tl;
    if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0') return false;
    while (*p == ' ' || *p == '\t') p++;

    if (name && namelen) {
        char quote = 0;
        name[0] = '\0';
        if (*p == '\'' || *p == '"') quote = *p++;

        size_t o = 0;
        while (*p && o + 1 < namelen) {
            if (quote) { if (*p == quote) break; }
            else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') break;
            name[o++] = *p++;
        }
        name[o] = '\0';
    }
    return true;
}

/* 按空格/逗号切一串口名 */
static void split_add(char dst[][PORT_NAME_LEN], int *n, const char *s)
{
    while (s && *s) {
        char one[PORT_NAME_LEN];
        size_t o = 0;
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        while (*s && *s != ' ' && *s != '\t' && *s != ',' && o + 1 < sizeof(one))
            one[o++] = *s++;
        one[o] = '\0';
        if (o) add_unique(dst, n, one);
    }
}

/* ------------------------------------------------------------------ */
/* 读 /etc/config/network                                              */
/* ------------------------------------------------------------------ */

static bool net_read(netinfo_t *ni)
{
    FILE *f;
    char line[256];
    char sec[32];
    bool in_dev = false, in_iface = false;
    char cur_iface[32] = "";
    int  anon_dev = -1;                  /* 匿名 device 段的序号（对应 uci @device[i]） */
    int  br_dev = -1;                    /* br-lan 所在段的序号 */
    char br_name[32] = "";               /* 该段的显式名字（有的话） */

    memset(ni, 0, sizeof(*ni));
    f = fopen(NETWORK_CONF, "r");
    if (!f) return false;
    sec[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char nm[32];

        if (section_is(line, "device", nm, sizeof(nm))) {
            anon_dev++;
            snprintf(sec, sizeof(sec), "%s", nm);
            in_dev = true;
            in_iface = false;
            continue;
        }
        if (section_is(line, "interface", nm, sizeof(nm))) {
            snprintf(sec, sizeof(sec), "%s", nm);
            snprintf(cur_iface, sizeof(cur_iface), "%s", nm);
            in_dev = false;
            in_iface = true;
            continue;
        }
        if (section_is(line, "globals", nm, sizeof(nm)) ||
            section_is(line, "route", nm, sizeof(nm)) ||
            section_is(line, "route6", nm, sizeof(nm)) ||
            section_is(line, "rule", nm, sizeof(nm)) ||
            section_is(line, "switch", nm, sizeof(nm))) {
            in_dev = in_iface = false;
            continue;
        }

        if (in_dev) {
            char v[64];

            if (opt_value(line, "name", v, sizeof(v)) && strcmp(v, "br-lan") == 0) {
                br_dev = anon_dev;
                snprintf(br_name, sizeof(br_name), "%s", sec);
            }
            /* ⚠️ 只能用 if：list_value 解析的是同一行，写成 while 会**死循环**
             * （同一行永远解析成功，指针不前进）—— 启动会卡在开机画面。 */
            if (list_value(line, "ports", v, sizeof(v)))
                add_unique(ni->lan_ports, &ni->lan_port_n, v);
            continue;
        }

        if (in_iface) {
            char v[64];

            if (strcmp(cur_iface, "lan") == 0) {
                if (opt_value(line, "ifname", v, sizeof(v))) {
                    split_add(ni->lan_ports, &ni->lan_port_n, v);
                    if (ni->lan_port_n > 0) ni->legacy = true;
                }
            } else if (strcmp(cur_iface, "wan") == 0) {
                if (opt_value(line, "device", v, sizeof(v)) ||
                    opt_value(line, "ifname", v, sizeof(v)))
                    snprintf(ni->wan_dev, sizeof(ni->wan_dev), "%s", v);
            } else if (strcmp(cur_iface, "wan6") == 0) {
                ni->has_wan6 = true;
                if (opt_value(line, "device", v, sizeof(v)) ||
                    opt_value(line, "ifname", v, sizeof(v)))
                    snprintf(ni->wan6_dev, sizeof(ni->wan6_dev), "%s", v);
            }
            continue;
        }
    }
    fclose(f);

    if (br_dev >= 0) {
        if (br_name[0]) snprintf(ni->brdev_uci, sizeof(ni->brdev_uci), "%s", br_name);
        else            snprintf(ni->brdev_uci, sizeof(ni->brdev_uci), "@device[%d]", br_dev);
    } else {
        ni->legacy = true;
        snprintf(ni->brdev_uci, sizeof(ni->brdev_uci), "lan");
    }
    if (!ni->wan_dev[0] && ni->wan6_dev[0])
        snprintf(ni->wan_dev, sizeof(ni->wan_dev), "%s", ni->wan6_dev);
    return true;
}

/* 枚举**实际存在**的物理网口 ethN（/sys/class/net）。
 * ⚠️ 不要把 /etc/config/network 里引用过的口也列出来：本机配置里 wan 指的是 eth1，
 *    而系统里根本没这个设备（只有 eth0）—— 用户要的就是"只针对 eth0 配 LAN/WAN"。 */
static void ports_scan(const netinfo_t *ni)
{
    DIR *d = opendir("/sys/class/net");
    struct dirent *e;
    (void)ni;

    g_port_n = 0;
    if (!d) return;

    while ((e = readdir(d)) && g_port_n < MAX_PORT) {
        const char *n = e->d_name;
        if (strncmp(n, "eth", 3) != 0) continue;
        if (n[3] < '0' || n[3] > '9') continue;
        add_unique(g_ports, &g_port_n, n);
    }
    closedir(d);
}

/* ------------------------------------------------------------------ */
/* 原始布局：记 / 恢复                                                 */
/* ------------------------------------------------------------------ */

/* 文件用和 /etc/config 一样的 "option x 'y'" 写法，读回来还是走 opt_value */
static void save_original_if_needed(const netinfo_t *ni)
{
    FILE *f = fopen(LAN_STATE_CONF, "r");
    char joined[128] = "";
    size_t off = 0;

    if (f) { fclose(f); return; }        /* 已存在就别覆盖（那才是"原始"） */

    for (int i = 0; i < ni->lan_port_n; i++) {
        int wr = snprintf(joined + off, sizeof(joined) - off, "%s%s",
                          i ? "," : "", ni->lan_ports[i]);
        if (wr < 0 || (size_t)wr >= sizeof(joined) - off) break;
        off += (size_t)wr;
    }

    f = fopen(LAN_STATE_CONF, "w");
    if (!f) return;
    fprintf(f, "# tftdisplay 原始网口布局（第一次打开 LAN 配置页时记下，供\"恢复原始配置\"用）\n");
    fprintf(f, "option lan_ports '%s'\n", joined);
    fprintf(f, "option wan_device '%s'\n", ni->wan_dev);
    fprintf(f, "option wan6_device '%s'\n", ni->has_wan6 ? ni->wan6_dev : "");
    fclose(f);
}

static bool load_original(char ports[][PORT_NAME_LEN], int *pn, char *wan, size_t wanlen)
{
    FILE *f = fopen(LAN_STATE_CONF, "r");
    char line[256];

    *pn = 0;
    wan[0] = '\0';
    if (!f) return false;

    while (fgets(line, sizeof(line), f)) {
        char v[128];
        if (opt_value(line, "lan_ports", v, sizeof(v)))
            split_add(ports, pn, v);
        if (opt_value(line, "wan_device", v, sizeof(v)))
            snprintf(wan, wanlen, "%s", v);
    }
    fclose(f);
    return (*pn > 0 || wan[0]);
}

/* ------------------------------------------------------------------ */
/* 写回：把目标布局落到 uci 并 reload                                  */
/* ------------------------------------------------------------------ */

static void apply_layout(char ports[][PORT_NAME_LEN], int pn, const char *wan, bool set_wan6)
{
    netinfo_t ni;
    char cmd[1024];
    size_t off = 0;
    const char *sec;

    net_read(&ni);
    sec = ni.brdev_uci[0] ? ni.brdev_uci : "lan";

#define APPEND(...)                                                             \
    do {                                                                        \
        if (off < sizeof(cmd) - 1) {                                            \
            int _w = snprintf(cmd + off, sizeof(cmd) - off, __VA_ARGS__);       \
            if (_w > 0) off += (size_t)_w;                                      \
            if (off > sizeof(cmd) - 1) off = sizeof(cmd) - 1;                   \
        }                                                                       \
    } while (0)

    APPEND("(");
    /* 1) 先清掉 LAN 成员（两种写法都清一遍；老式那条在 -q 下是空操作） */
    if (ni.legacy)
        APPEND("/sbin/uci -q delete network.%s.ifname; ", sec);
    APPEND("/sbin/uci -q delete network.%s.ports; ", sec);

    /* 2) 按目标加回去 */
    for (int i = 0; i < pn; i++) {
        if (ni.legacy)
            APPEND("/sbin/uci -q add_list network.%s.ifname='%s'; ", sec, ports[i]);
        APPEND("/sbin/uci -q add_list network.%s.ports='%s'; ", sec, ports[i]);
    }

    /* 3) WAN / WAN6 指向目标口：
     *    "-" = 把 device 删掉（唯一的物理口要留给 LAN，WAN 就没口了）
     *    空串 = 不动 */
    if (wan && strcmp(wan, "-") == 0) {
        APPEND("/sbin/uci -q delete network.wan.device; ");
        if (set_wan6)
            APPEND("/sbin/uci -q delete network.wan6.device; ");
    } else if (wan && *wan) {
        APPEND("/sbin/uci -q set network.wan.device='%s'; ", wan);
        if (set_wan6)
            APPEND("/sbin/uci -q set network.wan6.device='%s'; ", wan);
    }

    /* 4) 提交 + reload（⚠️ 只 commit 不 reload，新配置进不了内核） */
    APPEND("/sbin/uci commit network; ubus call network reload) >/dev/null 2>&1 &");
#undef APPEND

    printf("lan: 应用布局 lan=[%s,%s] wan=%s cmd=%s\n",
           pn > 0 ? ports[0] : "-", pn > 1 ? ports[1] : "-",
           (wan && *wan) ? wan : "-", cmd);
    fflush(stdout);
    system(cmd);
}

/* ------------------------------------------------------------------ */
/* 界面                                                                */
/* ------------------------------------------------------------------ */

static const char *port_role(const netinfo_t *ni, const char *p)
{
    if (ni->wan_dev[0] && strcmp(ni->wan_dev, p) == 0) return "WAN";
    if (ni->wan6_dev[0] && strcmp(ni->wan6_dev, p) == 0) return "WAN";
    for (int i = 0; i < ni->lan_port_n; i++)
        if (strcmp(ni->lan_ports[i], p) == 0) return "LAN";
    return "空闲";
}

/* 屏幕上把网口名写成大写（ETH0）；⚠️ 只用于显示，写 uci 时还是系统里的小写名 */
static void port_disp(const char *src, char *dst, size_t dstlen)
{
    size_t i = 0;
    for (; src && src[i] && i + 1 < dstlen; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* 中间那块小牌子（报错/切换中就把文案写在这里） */
static void set_info(const char *text)
{
    lv_label_set_text(g_info_lbl, text);
}

/* 当前角色那个块高亮，另一个留灰色 = 可点的"切换" */
static void style_role_btn(lv_obj_t *b, lv_obj_t *l, bool active, bool is_wan)
{
    lv_obj_set_style_bg_color(b, active
        ? (is_wan ? lv_color_hex(0xA85A12) : lv_color_hex(0x1E7A46))
        : lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_text_color(l, active ? lv_color_hex(0xFFFFFF)
                                          : lv_color_hex(0x9A9A9A), 0);
}

static void refresh_ui(void)
{
    netinfo_t ni;
    char lanstr[48];
    size_t off = 0;

    net_read(&ni);
    save_original_if_needed(&ni);
    ports_scan(&ni);

    for (int i = 0; i < SHOW_PORT; i++) {
        if (i >= g_port_n) {
            lv_obj_add_flag(g_name_box[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_lan_btn[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_wan_btn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(g_name_box[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_lan_btn[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_wan_btn[i], LV_OBJ_FLAG_HIDDEN);

        char disp[PORT_NAME_LEN];
        port_disp(g_ports[i], disp, sizeof(disp));
        lv_label_set_text(g_name_label[i], disp);

        if (lv_tick_get() < g_busy_until) continue;   /* 切换中先别翻牌子 */

        const char *role = port_role(&ni, g_ports[i]);
        style_role_btn(g_lan_btn[i], g_lan_lbl[i], strcmp(role, "LAN") == 0, false);
        style_role_btn(g_wan_btn[i], g_wan_lbl[i], strcmp(role, "WAN") == 0, true);
    }

    lanstr[0] = '-'; lanstr[1] = '\0';
    for (int i = 0; i < ni.lan_port_n && off < sizeof(lanstr) - 1; i++) {
        char up[PORT_NAME_LEN];
        int w;
        port_disp(ni.lan_ports[i], up, sizeof(up));
        w = snprintf(lanstr + off, sizeof(lanstr) - off, "%s%s",
                     i ? "," : "", up);
        if (w < 0 || (size_t)w >= sizeof(lanstr) - off) break;
        off += (size_t)w;
    }

    if (lv_tick_get() >= g_busy_until) {          /* 切换中就让状态文案留在小牌上 */
        char l[48];
        snprintf(l, sizeof(l), "原始 LAN=%s", lanstr);
        set_info(l);
    }
}

static void switch_cb(lv_event_t *e)
{
    netinfo_t ni;
    int  v, idx, want_wan;
    const char *role;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_tick_get() < g_busy_until) return;

    /* user_data = 口序号 * 2 + 目标角色（0=LAN 1=WAN） */
    v = (int)(intptr_t)lv_event_get_user_data(e);
    idx = v >> 1;
    want_wan = v & 1;
    if (idx < 0 || idx >= g_port_n) return;

    net_read(&ni);
    role = port_role(&ni, g_ports[idx]);

    if (want_wan && strcmp(role, "WAN") == 0) {   /* 已经是了，不白切一下把网抖断 */
        set_info("该口已经是 WAN");
        return;
    }
    if (!want_wan && strcmp(role, "LAN") == 0) {
        set_info("该口已经是 LAN");
        return;
    }

    /* ⚠️ 不当场切：先弹二次确认（用户要求"需要弹出提示确认"，防误触） */
    conf_open_switch(idx, want_wan);
}

/* 真正干活（确认之后才走到这里）：⚠️ 只切这一个口的角色，**不做两口感互换** */
static void switch_apply(int idx, int want_wan)
{
    netinfo_t ni;
    char lan[MAX_PORT][PORT_NAME_LEN];
    int  lann = 0;
    char wan[PORT_NAME_LEN] = "";        /* 空串 = 不动 wan/wan6 的 device */

    net_read(&ni);

    if (idx < 0 || idx >= g_port_n) return;

    if (want_wan) {
        snprintf(wan, sizeof(wan), "%s", g_ports[idx]);
        for (int i = 0; i < ni.lan_port_n; i++)
            if (strcmp(ni.lan_ports[i], g_ports[idx]) != 0)
                add_unique(lan, &lann, ni.lan_ports[i]);
    } else {
        /* 变回 LAN：加进桥成员；若 wan 还指着它，就把 wan 换回"原始那个 WAN 口" */
        for (int i = 0; i < ni.lan_port_n; i++)
            add_unique(lan, &lann, ni.lan_ports[i]);
        add_unique(lan, &lann, g_ports[idx]);

        if (strcmp(ni.wan_dev, g_ports[idx]) == 0) {
            char op[MAX_PORT][PORT_NAME_LEN], orig_wan[PORT_NAME_LEN] = "";
            int  opn = 0;
            if (load_original(op, &opn, orig_wan, sizeof(orig_wan)) && orig_wan[0] &&
                strcmp(orig_wan, g_ports[idx]) != 0)
                snprintf(wan, sizeof(wan), "%s", orig_wan);
            else
                snprintf(wan, sizeof(wan), "-");   /* 没有别的口 ⇒ 把 wan 的 device 删掉 */
        }
    }

    g_busy_until = lv_tick_get() + 12000;      /* reload 慢，冷却给足 */
    set_info(want_wan ? "正在切换为 WAN…" : "正在切换为 LAN…");

    apply_layout(lan, lann, wan, ni.has_wan6);
}

/* ------------------------------------------------------------------ */
/* 二次确认弹窗（用户："切换 LAN WAN 恢复原始 需要弹出提示确认"）      */
/* ------------------------------------------------------------------ */

static void restore_apply(void);

static void conf_hide(void)
{
    conf_idx = -1;
    conf_restore = false;
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

static void conf_show(void)
{
    lv_obj_move_foreground(conf_pop);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

/* "ETH0 切成 WAN/LAN 口？" */
static void conf_open_switch(int idx, int want_wan)
{
    char t[40], b[96], up[PORT_NAME_LEN];

    port_disp(g_ports[idx], up, sizeof(up));
    snprintf(t, sizeof(t), "%s 切成 %s 口？", up, want_wan ? "WAN" : "LAN");
    if (want_wan)
        snprintf(b, sizeof(b), "有线连接会断开\n可用 WiFi 或屏幕切回");
    else
        snprintf(b, sizeof(b), "加回 LAN 桥，需要几秒");

    conf_idx = idx;
    conf_wan = want_wan;
    conf_restore = false;
    lv_label_set_text(conf_title, t);
    lv_label_set_text(conf_text, b);
    lv_obj_set_style_bg_color(conf_ok, lv_color_hex(want_wan ? 0xA85A12 : 0x1E7A46), 0);
    conf_show();
}

static void conf_open_restore(void)
{
    char ports[MAX_PORT][PORT_NAME_LEN], wan[PORT_NAME_LEN] = "", up[PORT_NAME_LEN], b[96];
    int  pn = 0;

    if (!load_original(ports, &pn, wan, sizeof(wan))) {
        set_info("没有原始配置记录");
        return;
    }
    port_disp(pn > 0 ? ports[0] : "-", up, sizeof(up));
    snprintf(b, sizeof(b), "按首次记录的布局写回\nLAN=%s", up);

    conf_idx = -1;
    conf_restore = true;
    lv_label_set_text(conf_title, "恢复原始配置？");
    lv_label_set_text(conf_text, b);
    lv_obj_set_style_bg_color(conf_ok, lv_color_hex(0x1D5A66), 0);
    conf_show();
}

static void conf_ok_cb(lv_event_t *e)
{
    int  idx, want_wan;
    bool restore;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    idx = conf_idx;
    want_wan = conf_wan;
    restore = conf_restore;
    conf_hide();                            /* 先收框再干活 */

    if (restore)       restore_apply();
    else if (idx >= 0) switch_apply(idx, want_wan);
}

static void conf_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    conf_hide();
    refresh_ui();                           /* 把"原始 LAN=…"写回小牌子 */
}

static void restore_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_tick_get() < g_busy_until) return;

    /* ⚠️ 也不当场恢复：先弹二次确认 */
    conf_open_restore();
}

/* 真正恢复（确认之后才走到这里） */
static void restore_apply(void)
{
    char ports[MAX_PORT][PORT_NAME_LEN];
    int  pn = 0;
    char wan[PORT_NAME_LEN] = "";
    netinfo_t ni;

    if (!load_original(ports, &pn, wan, sizeof(wan))) {
        set_info("没有原始配置记录");
        return;
    }
    net_read(&ni);

    g_busy_until = lv_tick_get() + 12000;
    set_info("正在恢复原始配置…");
    apply_layout(ports, pn, wan, ni.has_wan6);
}

static void refresh_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    /* 弹窗挂着时若页面被划走了（顶栏还能滑动）⇒ 把弹窗收掉，别留在别的页上 */
    if (conf_pop && !lv_obj_has_flag(conf_pop, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_is_visible(lan_page))
        conf_hide();

    refresh_ui();
}

/* 一块不动的东西（背景色+居中文字），当"牌子"用 */
static lv_obj_t *make_block(lv_obj_t *parent, int x, int y, int w, int h,
                            lv_color_t bg, lv_obj_t **out_label, const char *text)
{
    lv_obj_t *b = lv_obj_create(parent);

    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, LAN_FONT, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xE6E6E6), 0);
    lv_obj_center(l);

    if (out_label) *out_label = l;
    return b;
}

/* 一行纯文字（弹窗里的标题/正文用） */
static lv_obj_t *mk_text(lv_obj_t *parent, const char *text, uint32_t color,
                         int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, LAN_FONT, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    return l;
}

/* 做成一个块按钮（和其它页一致；必须带 GESTURE_BUBBLE，否则划不动页） */
static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h,
                             lv_color_t bg, lv_event_cb_t cb, void *user,
                             lv_obj_t **out_label, const char *text)
{
    lv_obj_t *b = make_block(parent, x, y, w, h, bg, out_label, text);

    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

void create_lan_page(lv_obj_t *main_screen)
{
    netinfo_t ni0;

    net_read(&ni0);        /* ⚠️ 先知道有哪些口，再建标签（否则名字是空的） */
    ports_scan(&ni0);

    lan_page = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(lan_page, lv_color_black(), 0);
    lv_obj_set_size(lan_page, 320, 140);
    lv_obj_set_style_radius(lan_page, 0, 0);
    lv_obj_set_style_border_width(lan_page, 0, 0);
    lv_obj_set_scrollbar_mode(lan_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(lan_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(lan_page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lan_page, 0, 0);      /* ⚠️ 主题默认 20px 内边距会把内容推偏 */

    /* 上面一排 = [ETHX 块] [LAN 块] [WAN 块]（**大块**，顶上留边距；口多时每块矮一点腾地方）
     * 后两个就是并排的两个按钮：当前角色那个高亮、另一个灰色可点（点了就把这个口切成那个角色）。 */
    int rows   = (g_port_n > 0 && g_port_n < SHOW_PORT) ? g_port_n : SHOW_PORT;
    int bh     = (rows >= 2) ? 32 : 42;          /* 大块高度 */
    int pitch  = bh + 6;
    int top_y  = 8;                              /* ⚠️ 别顶到 0，太挤 */

    for (int i = 0; i < SHOW_PORT; i++) {
        int y = top_y + i * pitch;

        g_name_box[i] = make_block(lan_page, 8, y, 88, bh, lv_color_hex(0x2E2E2E),
                                   &g_name_label[i],
                                   g_ports[i][0] ? g_ports[i] : "ETH0");
        g_lan_btn[i] = make_button(lan_page, 102, y, 102, bh, lv_color_hex(0x2A2A2A),
                                   switch_cb, (void *)(intptr_t)(i * 2), &g_lan_lbl[i], "LAN");
        g_wan_btn[i] = make_button(lan_page, 210, y, 102, bh, lv_color_hex(0x2A2A2A),
                                   switch_cb, (void *)(intptr_t)(i * 2 + 1), &g_wan_lbl[i], "WAN");
    }

    /* 中间一块**小**牌子：只报原始 LAN（用户要求不显示 原始 WAN） */
    int info_y = top_y + rows * pitch;
    int info_h = (rows >= 2) ? 22 : 26;
    make_block(lan_page, 8, info_y, 304, info_h, lv_color_hex(0x242424),
               &g_info_lbl, "原始 LAN=-");
    lv_obj_set_style_text_color(g_info_lbl, lv_color_hex(0xA0A0A0), 0);

    /* 恢复按钮：整宽一条，放在最下面 */
    int btn_y = info_y + info_h + 6;
    int btn_h = 140 - btn_y - 8;
    if (btn_h < 24) btn_h = 24;
    if (btn_y + btn_h > 136) btn_y = 136 - btn_h;

    make_button(lan_page, 8, btn_y, 304, btn_h, lv_color_hex(0x3A3A3A),
                restore_cb, NULL, NULL, "恢复原始配置");

    /* ---------- 二次确认弹窗（切换/恢复都要先确认，防误触） ----------
     * 跟短信删除确认一个做法：挂 **顶层** `lv_layer_top()`，几何 = 页面区
     * 320x140 @(0,30)（跟页面对象完全对齐），不透明、无边框，默认 HIDDEN。
     * 两个按钮各半宽贴底（94+46=140），左确认（颜色随目标角色变）/ 右取消。 */
    conf_pop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(conf_pop, 320, 140);
    lv_obj_set_pos(conf_pop, 0, 30);
    lv_obj_set_style_bg_color(conf_pop, lv_color_make(26, 26, 26), 0);
    lv_obj_set_style_bg_opa(conf_pop, LV_OPA_COVER, 0);   /* 不透明，否则底下的页会透出来 */
    lv_obj_set_style_radius(conf_pop, 0, 0);
    lv_obj_set_style_border_width(conf_pop, 0, 0);
    lv_obj_set_style_pad_all(conf_pop, 0, 0);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(conf_pop, conf_cancel_cb, LV_EVENT_CLICKED, NULL);  /* 点空白 = 取消 */

    conf_title = mk_text(conf_pop, "确认？", 0xFFC24D, 14, 12, 0);
    conf_text  = mk_text(conf_pop, "", 0xD0D0D0, 14, 46, 292);
    conf_ok    = make_button(conf_pop, 0, 94, 160, 46, lv_color_hex(0x1E7A46),
                             conf_ok_cb, NULL, NULL, "确认");
    make_button(conf_pop, 160, 94, 160, 46, lv_color_hex(0x3A3A3A),
                conf_cancel_cb, NULL, NULL, "取消");

    refresh_ui();
    lv_timer_create(refresh_cb, 1000, NULL);

    /* 调试钩子：TFT_LAN_CONFTEST=1 ⇒ 开屏就弹确认框（拍屏核对排版用） */
    if (getenv("TFT_LAN_CONFTEST") && g_port_n > 0)
        conf_open_switch(0, 1);
}
