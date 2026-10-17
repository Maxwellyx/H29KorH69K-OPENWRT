/*
 * SYSTEM → WIFI 页：SSID / 密码 / 二维码 / WiFi 开关
 *
 * **全部本地实现**（和老版本一样直接读 `/etc/config/wireless`，不经服务端/模组）：
 *   · 解析第一个 `config wifi-iface` + `mode 'ap'` + ssid 非空的段，取 SSID / key / encryption；
 *   · 它的 `option device`（radio0）上的 `option disabled '1'` 就是"WiFi 关着"；
 *   · 二维码内容按 WIFI: 规范生成：加密 WIFI:T:WPA;S:<ssid>;P:<key>;; / 开放 WIFI:T:nopass;S:<ssid>;;
 *     （ssid/密码里的 \ ; , : " 会转义）——手机相机直接扫就能连。
 *   · 开关按钮：写 uci 再 wifi up/down：
 *       uci set wireless.<radio>.disabled='1'; uci commit wireless; wifi down
 *     整条命令丢到后台执行（wifi up/down 要好几秒，不能卡住 LVGL 主循环），
 *     按钮按下后 6 秒内不再响应（避免连点把无线反复重启）。
 */
#include "../lvgl/lvgl.h"
#include "wifi_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

lv_obj_t *wifi_page;

#define WIRELESS_CONF "/etc/config/wireless"

/* 中文字体：必须是全字库那套（GB2312 6963 字，见 utils/lv_font_cjk_sms_16.c）。
 * ⚠️ 别用 lv_font_cjk_16 / 20 / 28 那几个子集（只有几十个字，没有"开启/关闭"就会显示成方框）。 */
LV_FONT_DECLARE(lv_font_cjk_sms_16);
#define WIFI_FONT (&lv_font_cjk_sms_16)

static lv_obj_t *wifi_qr;
static lv_obj_t *wifi_ssid_label;
static lv_obj_t *wifi_key_label;
static lv_obj_t *wifi_state_label;
static lv_obj_t *wifi_state_box;    /* 状态块（在二维码上方，宽度 = 二维码总宽 84） */
static lv_obj_t *wifi_btn;
static lv_obj_t *wifi_btn_label;

static char  g_radio[32];        /* 这个 AP 挂在哪个 wifi-device 上（开关键要用） */
static bool  g_exists;           /* 有没有解析到可用的 AP */
static uint32_t g_busy_until;    /* 切换中的冷却（LVGL tick = 开机毫秒数） */

/* ------------------------------------------------------------------ */
/* 解析 /etc/config/wireless                                           */
/* ------------------------------------------------------------------ */

/* option <name> '<v>' / "<v>" / <v>；命中返回 true */
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

/* config <type> '<name>'；命中返回 true，名字写进 name */
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

/* 读配置：填 ssid/key（+加密标志），返回该 radio 是否 disabled；读不到文件返回 -1 */
static int wifi_read(char *ssid, size_t ssidlen, char *key, size_t keylen, bool *encrypted)
{
    FILE *f = fopen(WIRELESS_CONF, "r");
    if (!f) return -1;

    char line[256];
    char radio[32] = "";            /* 当前 section 名 */
    bool is_dev = false, is_iface = false;

    char cur_dev[32] = "", cur_mode[16] = "", cur_ssid[64] = "", cur_key[128] = "";
    char cur_enc[32] = "";
    bool cur_dev_disabled = false;

    /* 先记下每个 wifi-device 的 disabled */
    struct { char name[32]; bool disabled; } devs[4];
    int devn = 0;

    bool got = false;
    char want_radio[32] = "";
    bool want_disabled = false;

    while (fgets(line, sizeof(line), f)) {
        char nm[32];

        if (section_is(line, "wifi-device", nm, sizeof(nm))) {
            /* 上一段 iface 收尾 */
            if (is_iface && !got && strcmp(cur_mode, "ap") == 0 && cur_ssid[0]) {
                if (ssid && ssidlen) snprintf(ssid, ssidlen, "%s", cur_ssid);
                if (key && keylen)   snprintf(key, keylen, "%s", cur_key);
                snprintf(want_radio, sizeof(want_radio), "%s", cur_dev);
                if (encrypted)
                    *encrypted = (cur_enc[0] && strcmp(cur_enc, "none") != 0) || cur_key[0];
                got = true;
            }
            snprintf(radio, sizeof(radio), "%s", nm);
            is_dev = true;
            is_iface = false;
            cur_dev_disabled = false;
            continue;
        }
        if (section_is(line, "wifi-iface", nm, sizeof(nm))) {
            if (is_dev && devn < 4) {
                snprintf(devs[devn].name, sizeof(devs[devn].name), "%s", radio);
                devs[devn].disabled = cur_dev_disabled;
                devn++;
            }
            snprintf(radio, sizeof(radio), "%s", nm);
            is_iface = true;
            is_dev = false;
            cur_dev[0] = cur_mode[0] = cur_ssid[0] = cur_key[0] = cur_enc[0] = '\0';
            continue;
        }

        if (is_dev) {
            char v[16];
            if (opt_value(line, "disabled", v, sizeof(v)) && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y'))
                cur_dev_disabled = true;
        } else if (is_iface) {
            opt_value(line, "device", cur_dev, sizeof(cur_dev));
            opt_value(line, "mode", cur_mode, sizeof(cur_mode));
            opt_value(line, "ssid", cur_ssid, sizeof(cur_ssid));
            opt_value(line, "key", cur_key, sizeof(cur_key));
            opt_value(line, "encryption", cur_enc, sizeof(cur_enc));
        }
    }
    /* 文件末尾收尾 */
    if (is_iface && !got && strcmp(cur_mode, "ap") == 0 && cur_ssid[0]) {
        if (ssid && ssidlen) snprintf(ssid, ssidlen, "%s", cur_ssid);
        if (key && keylen)   snprintf(key, keylen, "%s", cur_key);
        snprintf(want_radio, sizeof(want_radio), "%s", cur_dev);
        if (encrypted)
            *encrypted = (cur_enc[0] && strcmp(cur_enc, "none") != 0) || cur_key[0];
        got = true;
    } else if (is_dev && devn < 4) {
        snprintf(devs[devn].name, sizeof(devs[devn].name), "%s", radio);
        devs[devn].disabled = cur_dev_disabled;
        devn++;
    }
    fclose(f);

    if (!got) return -1;

    snprintf(g_radio, sizeof(g_radio), "%s",
             want_radio[0] ? want_radio : (devn > 0 ? devs[0].name : "radio0"));

    for (int i = 0; i < devn; i++)
        if (strcmp(devs[i].name, g_radio) == 0) { want_disabled = devs[i].disabled; break; }
    return want_disabled ? 1 : 0;
}

/* WIFI: 二维码规范里 \ ; , : " 要转义（密码里带 ; 不转义就解析错） */
static void qr_escape(const char *in, char *out, size_t outlen)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 2 < outlen; p++) {
        if (*p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"')
            out[o++] = '\\';
        out[o++] = *p;
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* 开 / 关 WiFi                                                        */
/* ------------------------------------------------------------------ */

static void wifi_apply(bool disable)
{
    char cmd[400];
    const char *r = g_radio[0] ? g_radio : "radio0";

    /* 丢到后台：wifi up/down 要好几秒，卡在这里会把界面冻住 */
    snprintf(cmd, sizeof(cmd),
             "(/sbin/uci set wireless.%s.disabled='%d'; /sbin/uci commit wireless; "
             "/sbin/wifi %s) >/dev/null 2>&1 &",
             r, disable ? 1 : 0, disable ? "down" : "up");
    if (system(cmd) != 0) {
        /* system() 只是把命令丢出去，失败多半是 fork 不出来；记一行日志方便排查 */
        printf("wifi: 执行失败: %s\n", cmd);
        fflush(stdout);
    }
    printf("wifi: %s %s\n", disable ? "关闭" : "开启", r);
    fflush(stdout);
}

static void wifi_do(bool disable)
{
    g_busy_until = lv_tick_get() + 6000;
    lv_label_set_text(wifi_btn_label, "切换中…");
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0x3A3A3A), 0);

    wifi_apply(disable);
}

/* ---- 二次确认弹窗（用户 2026-10-07："功能 wifi 要提示框确认"）----
 * 点开关按钮**不当场执行**，先弹顶层确认框（跟 lan/brightness 页一个做法）。 */
static lv_obj_t *conf_pop, *conf_title, *conf_text, *conf_ok;
static int       conf_target;            /* -1 = 没待确认；0 = 开；1 = 关 */

static void conf_hide(void)
{
    conf_target = -1;
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

static void conf_open(bool disable)
{
    char b[96];

    conf_target = disable ? 1 : 0;
    lv_label_set_text(conf_title, disable ? "关闭 WiFi？" : "开启 WiFi？");
    if (disable)
        snprintf(b, sizeof(b), "无线热点会消失\n已连接的设备会断开");
    else
        snprintf(b, sizeof(b), "热点重新广播，需要几秒");
    lv_label_set_text(conf_text, b);
    lv_obj_set_style_bg_color(conf_ok, lv_color_hex(disable ? 0x7A1F1F : 0x1D5A66), 0);
    lv_obj_move_foreground(conf_pop);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

static void conf_ok_cb(lv_event_t *e)
{
    int t;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    t = conf_target;
    conf_hide();                            /* 先收框再真动 */
    if (t >= 0) wifi_do(t == 1);
}

static void conf_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    conf_hide();
}

static void wifi_btn_cb(lv_event_t *e)
{
    int st;
    bool now_disabled;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_tick_get() < g_busy_until) return;        /* 冷却中，别连点 */

    st = wifi_read(NULL, 0, NULL, 0, NULL);          /* 只为了拿最新状态 */
    now_disabled = (st == 1);

    /* ⚠️ 不当场切：先弹二次确认（断网会踢掉已连的设备，必须让人确认一下） */
    conf_open(!now_disabled);                        /* 现在是开着的就预备关掉 */
}

/* ------------------------------------------------------------------ */
/* 界面                                                                */
/* ------------------------------------------------------------------ */

static void wifi_refresh_ui(void)
{
    char ssid[64] = "", key[128] = "";
    bool enc = false;
    int st = wifi_read(ssid, sizeof(ssid), key, sizeof(key), &enc);
    bool disabled = (st == 1);
    g_exists = (st >= 0);

    if (st < 0) {
        lv_label_set_text(wifi_ssid_label, "读不到配置");
        lv_label_set_text(wifi_key_label, "---");
        lv_label_set_text(wifi_state_label, "未知");
        lv_obj_set_style_bg_color(wifi_state_box, lv_color_hex(0x2A2A2A), 0);
        lv_label_set_text(wifi_btn_label, "开启 WiFi");
        return;
    }

    lv_label_set_text(wifi_ssid_label, ssid);
    if (!enc) lv_label_set_text(wifi_key_label, "(无密码)");
    else      lv_label_set_text(wifi_key_label, key);

    /* 状态：做成一块牌子（颜色随状态变），不是一行小字 —— 用户要求 */
    lv_label_set_text(wifi_state_label, disabled ? "已关闭" : "已开启");
    lv_obj_set_style_bg_color(wifi_state_box,
                              lv_color_hex(disabled ? 0xA85A12 : 0x1E7A46), 0);

    /* 冷却结束才改按钮文案，免得"切换中…"被 1 秒定时器刷掉 */
    if (lv_tick_get() >= g_busy_until) {
        lv_label_set_text(wifi_btn_label, disabled ? "开启 WiFi" : "关闭 WiFi");
        lv_obj_set_style_bg_color(wifi_btn,
                                  disabled ? lv_color_hex(0x1D5A66) : lv_color_hex(0x7A1F1F), 0);
    }

    /* 二维码：内容变了才重画（qrcode_update 比较费） */
    static char qr_shown[256];
    char data[320], es[160], ek[200];

    qr_escape(ssid, es, sizeof(es));
    qr_escape(key, ek, sizeof(ek));
    if (enc) snprintf(data, sizeof(data), "WIFI:T:WPA;S:%s;P:%s;;", es, ek);
    else     snprintf(data, sizeof(data), "WIFI:T:nopass;S:%s;;", es);

    if (strcmp(qr_shown, data) != 0) {
        snprintf(qr_shown, sizeof(qr_shown), "%s", data);
        lv_qrcode_update(wifi_qr, data, strlen(data));
    }
}

static void update_wifi_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    /* 弹窗挂着时若页面被划走了（顶栏还能滑动）⇒ 收掉，别留在别的页上 */
    if (conf_pop && !lv_obj_has_flag(conf_pop, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_is_visible(wifi_page))
        conf_hide();

    wifi_refresh_ui();
}

/* 一行纯文字（弹窗里的标题/正文用） */
static lv_obj_t *wifi_mk_text(lv_obj_t *parent, const char *text, uint32_t color,
                              int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, WIFI_FONT, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    return l;
}

/* 块按钮（必须带 GESTURE_BUBBLE，否则划不动页） */
static lv_obj_t *wifi_mk_button(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t bg, lv_event_cb_t cb, const char *text)
{
    lv_obj_t *b = lv_obj_create(parent);

    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = wifi_mk_text(b, text, 0xFFFFFF, 0, 0, 0);

    lv_obj_center(l);
    return b;
}

void create_wifi_page(lv_obj_t *main_screen)
{
    wifi_page = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(wifi_page, lv_color_black(), 0);
    lv_obj_set_size(wifi_page, 320, 140);
    lv_obj_set_style_radius(wifi_page, 0, 0);
    lv_obj_set_style_border_width(wifi_page, 0, 0);
    lv_obj_set_scrollbar_mode(wifi_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(wifi_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(wifi_page, LV_OPA_COVER, 0);
    /* ⚠️ 主题默认 20px 内边距会把里面的东西整体推偏（其它页都踩过） */
    lv_obj_set_style_pad_all(wifi_page, 0, 0);

    lv_color_t bg_color = lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 5);
    lv_color_t fg_color = lv_palette_darken(LV_PALETTE_BLUE, 4);

    /* ------------------------------------------------------------------
     * 左列（x=8）：上面一块**状态牌**，下面是二维码
     *   用户要求："把状态放在二维码上面用块显示，块宽度要和二维码一样宽，二维码往下调"，
     *   随后又嫌矮："二维码 wifi 状态高度再调高点，不然有一点不协调" ⇒ 两块一起加高：
     *   二维码 size 76→**80**（+边框 4×2 = 总宽 88），状态牌也跟着 84→**88** 宽、28→**34** 高。
     *   两块左边缘都从 8 起、右边缘都到 96，下边缘都到 134（跟右列按钮齐平）。
     * ------------------------------------------------------------------ */
    wifi_state_box = lv_obj_create(wifi_page);
    lv_obj_set_size(wifi_state_box, 88, 34);
    lv_obj_set_pos(wifi_state_box, 8, 6);
    lv_obj_set_style_bg_color(wifi_state_box, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(wifi_state_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi_state_box, 0, 0);
    lv_obj_set_style_border_width(wifi_state_box, 0, 0);
    lv_obj_set_style_pad_all(wifi_state_box, 0, 0);
    lv_obj_clear_flag(wifi_state_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_state_box, LV_OBJ_FLAG_GESTURE_BUBBLE);

    wifi_state_label = lv_label_create(wifi_state_box);
    lv_label_set_text(wifi_state_label, "?");
    lv_obj_set_style_text_font(wifi_state_label, WIFI_FONT, 0);
    lv_obj_set_style_text_color(wifi_state_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(wifi_state_label);

    /* 二维码：整体下移（y 从原来的居中 ≈28 改成 46），底部与右列按钮齐平 */
    wifi_qr = lv_qrcode_create(wifi_page);
    lv_qrcode_set_size(wifi_qr, 80);
    lv_qrcode_set_dark_color(wifi_qr, fg_color);
    lv_qrcode_set_light_color(wifi_qr, bg_color);
    lv_obj_set_pos(wifi_qr, 8, 46);
    lv_obj_set_style_border_color(wifi_qr, bg_color, 0);
    lv_obj_set_style_border_width(wifi_qr, 4, 0);

    /* ------------------------------------------------------------------
     * 右列（x=100，宽 212）：两行**卡片**（SSID / 密码）+ 底部整宽开关按钮
     *   每行 = 一块底 + 左侧灰标签 + 右侧亮值（超长打省略号），三块上下间隔 4px，
     *   全部满宽对齐（以前标签/值的 x 是写死的，长短一变就参差不齐）。
     * ------------------------------------------------------------------ */
    static const char *keys[2] = { "SSID", "密码" };
    const int row_y[2] = { 6, 52 };
    lv_obj_t *val[2];

    for (int i = 0; i < 2; i++) {
        lv_obj_t *row = lv_obj_create(wifi_page);

        lv_obj_set_size(row, 212, 42);
        lv_obj_set_pos(row, 100, row_y[i]);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1E1E1E), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *k = lv_label_create(row);
        lv_label_set_text(k, keys[i]);
        lv_obj_set_style_text_font(k, WIFI_FONT, 0);
        lv_obj_set_style_text_color(k, lv_color_hex(0x8A8A8A), 0);
        lv_obj_align(k, LV_ALIGN_LEFT_MID, 12, 0);

        val[i] = lv_label_create(row);
        lv_label_set_text(val[i], "-");
        lv_obj_set_style_text_font(val[i], WIFI_FONT, 0);
        lv_obj_set_style_text_color(val[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(val[i], LV_ALIGN_LEFT_MID, 62, 0);
        /* SSID / 密码可能很长：超宽就打省略号，别把排版撑乱 */
        lv_obj_set_width(val[i], 212 - 62 - 12);
        lv_label_set_long_mode(val[i], LV_LABEL_LONG_DOT);
    }
    wifi_ssid_label = val[0];
    wifi_key_label  = val[1];

    /* 底部整宽开关按钮：与两行卡片左右对齐、上下间隔 4px */
    wifi_btn = lv_obj_create(wifi_page);
    lv_obj_set_size(wifi_btn, 212, 36);
    lv_obj_set_pos(wifi_btn, 100, 98);
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0x7A1F1F), 0);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi_btn, 0, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(wifi_btn, 0, 0);
    lv_obj_clear_flag(wifi_btn, LV_OBJ_FLAG_SCROLLABLE);
    /* ⚠️ 手势泡泡：在按钮上左右划也要能换页 */
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);

    wifi_btn_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_btn_label, "关闭 WiFi");
    lv_obj_set_style_text_font(wifi_btn_label, WIFI_FONT, 0);
    lv_obj_set_style_text_color(wifi_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(wifi_btn_label);

    /* ---------- 二次确认弹窗（开关 WiFi 先确认，防误按） ----------
     * 几何跟页面对象完全对齐（320x140 @0,30），不透明、无边框，默认 HIDDEN；
     * 两个按钮各半宽贴底（94+46=140）：左确认（颜色随动作）/ 右取消；点面板空白 = 取消。 */
    conf_pop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(conf_pop, 320, 140);
    lv_obj_set_pos(conf_pop, 0, 30);
    lv_obj_set_style_bg_color(conf_pop, lv_color_make(26, 26, 26), 0);
    lv_obj_set_style_bg_opa(conf_pop, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(conf_pop, 0, 0);
    lv_obj_set_style_border_width(conf_pop, 0, 0);
    lv_obj_set_style_pad_all(conf_pop, 0, 0);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(conf_pop, conf_cancel_cb, LV_EVENT_CLICKED, NULL);

    conf_title = wifi_mk_text(conf_pop, "确认？", 0xFFC24D, 14, 12, 0);
    conf_text  = wifi_mk_text(conf_pop, "", 0xD0D0D0, 14, 46, 292);
    conf_ok    = wifi_mk_button(conf_pop, 0, 94, 160, 46, 0x7A1F1F, conf_ok_cb, "确认");
    wifi_mk_button(conf_pop, 160, 94, 160, 46, 0x3A3A3A, conf_cancel_cb, "取消");

    wifi_refresh_ui();
    lv_timer_create(update_wifi_cb, 1000, NULL);

    /* 调试钩子：TFT_WIFI_CONFTEST=1 ⇒ 开屏就弹确认框（拍屏核对排版用） */
    if (getenv("TFT_WIFI_CONFTEST"))
        conf_open(true);
}
