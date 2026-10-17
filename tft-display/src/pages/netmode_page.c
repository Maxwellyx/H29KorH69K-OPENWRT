#include "../lvgl/lvgl.h"
#include "netmode_page.h"
#include "../utils/wsdata.h"
#include "../utils/wsclient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Mobile 页的第三个横屏：**组网模式偏好**（用户在 Mobile 页左右划到底就是它）。
 *
 *      组网模式偏好                          当前 4G+5G     ← 小字
 *      ┌─────────┐ ┌─────────┐ ┌─────────┐
 *      │   4G    │ │   5G    │ │  4G+5G  │                ← 点一下直接切（当前那档高亮成青色）
 *      └─────────┘ └─────────┘ └─────────┘
 *                已切换到 4G+5G                           ← 服务端回来的反馈 / 设置中… / 失败原因
 *            点一下切换 · 切完可能短暂断网
 *
 * 数据全部来自服务端（corev2/displaymode.go 的 `mode` 推送）：
 *   · 三个按钮的**字与能不能按**都由服务端给（不同机型支持的档位不一样，例如
 *     展锐只有"仅 4G / 4G+5G"两档 —— 那一档按钮就置灰，而不是让用户点了必定失败）；
 *   · 切模式是 AT **动作**（下发 + 回读确认，可能要几秒），所以点完先显示"设置中…"，
 *     等推送回来再更新（服务端写失败也会推一份带"设置失败：…"的）。
 */

#define NETMODE_BTN_W 96
#define NETMODE_BTN_H 52
#define NETMODE_BTN_Y 34
#define NETMODE_BTN_GAP 8
#define NETMODE_BTN_X0 8

/* 16px 全字库（短信页那套）：标题/反馈/提示这些小字用它 */
LV_FONT_DECLARE(lv_font_cjk_sms_16);

lv_obj_t *netmode_page;

static lv_obj_t *nm_cur;                                  /* 右上："当前 4G+5G" */
static lv_obj_t *nm_msg;                                  /* 按钮下面那行反馈 */
static lv_obj_t *nm_btns[WS_MODE_BTN_MAX];
static lv_obj_t *nm_btn_labels[WS_MODE_BTN_MAX];
static int       nm_btn_n;                                /* 实际建了几个按钮 */

/* 上一轮画出来的状态（一样就不重设，省重排/防闪） */
static char nm_prev_val[24];
static char nm_prev_msg[96];
static int  nm_prev_cur_len = -1;

/* 点按钮（或调试钩子）⇒ 让服务端去写模组（写成功/失败都会再推一份给你） */
static void nm_send_mode(int i)
{
    ws_mode_t m;
    char cmd[96];

    if (i < 0 || i >= nm_btn_n)
        return;
    wsdata_mode(&m);
    if (i >= m.mode_btn_n) {
        printf("[mode] 不发：没有第 %d 档（服务端只给了 %d 个）\n", i + 1, m.mode_btn_n);
        fflush(stdout);
        return;
    }
    if (!m.mode_btn[i].writable) {
        printf("[mode] 不发：第 %d 档不可写 (writable=%d)\n", i + 1, m.mode_btn[i].writable);
        fflush(stdout);
        lv_label_set_text(nm_msg, "本机不支持这一档");
        return;
    }
    if (strcmp(m.mode_btn[i].value, m.mode_value) == 0) {
        printf("[mode] 不发：已经是 %s 这一档\n", m.mode_value);
        fflush(stdout);
        return;                                            /* 已经是这一档，不发指令 */
    }

    snprintf(cmd, sizeof(cmd),
             "{\"action\":\"mode_set\",\"modem_id\":0,\"value\":\"%s\"}", m.mode_btn[i].value);
    bool ok = ws_client_send_text(cmd);
    printf("[mode] 发 %s ⇒ %s\n", cmd, ok ? "已发出" : "发送失败（WS 没连上？）");
    fflush(stdout);
    if (ok) {
        lv_label_set_text(nm_msg, "设置中…（写完会回读确认）");
        snprintf(nm_prev_msg, sizeof(nm_prev_msg), "%s", "设置中…（写完会回读确认）");
    } else {
        lv_label_set_text(nm_msg, "没有连上服务端，指令没发出去");
        snprintf(nm_prev_msg, sizeof(nm_prev_msg), "%s", "没有连上服务端，指令没发出去");
    }
}

static void nm_btn_cb(lv_event_t *e)
{
    nm_send_mode((int)(intptr_t)lv_event_get_user_data(e));
}

/* 调试钩子 TFT_MODE_SET=<语义值>：启动后自动按一下那一档的按钮。
 * 用途：屏幕上的点击要手指，先拿它把"屏幕 → 服务端 → 模组"这条链验证掉
 * （写法与短信页的 TFT_SMS_DELTEST 一致）。 */
static void nm_debug_hook(void)
{
    static int done;
    const char *want;
    ws_mode_t m;

    if (done)
        return;
    want = getenv("TFT_MODE_SET");
    if (!want || !*want)
        return;
    wsdata_mode(&m);
    if (!m.mode_btn_n)
        return;                                            /* 数据还没来，下一拍再试 */
    for (int i = 0; i < m.mode_btn_n; i++) {
        if (strcmp(m.mode_btn[i].value, want) == 0) {
            done = 1;
            printf("[mode] 调试钩子 TFT_MODE_SET=%s ⇒ 相当于点了第 %d 个挡位\n", want, i + 1);
            fflush(stdout);
            nm_send_mode(i);
            return;
        }
    }
    done = 1;                                              /* 认不出的取值：不再试 */
    printf("[mode] 调试钩子 TFT_MODE_SET=%s：本机没有这一档\n", want);
    fflush(stdout);
}

/* 每 300ms 按推送的内容重画（值没变就不动） */
static void nm_tick(lv_timer_t *t)
{
    ws_mode_t m;
    char buf[96];
    bool have;

    (void)t;

    have = wsdata_mode(&m);
    nm_debug_hook();

    if (!have) {
        if (nm_prev_cur_len != -2) {
            lv_label_set_text(nm_cur, "");
            for (int i = 0; i < nm_btn_n; i++) {
                lv_obj_set_style_bg_color(nm_btns[i], lv_color_hex(0x2A2A2A), 0);
                lv_obj_set_style_text_color(nm_btn_labels[i], lv_color_hex(0x808080), 0);
                lv_label_set_text(nm_btn_labels[i], "-");
            }
            lv_label_set_text(nm_msg, "读取中…");
            nm_prev_cur_len = -2;
            nm_prev_val[0] = '\0';
            snprintf(nm_prev_msg, sizeof(nm_prev_msg), "%s", "读取中…");
        }
        return;
    }

    /* 三个按钮：名字来自服务端；当前那一档高亮青色，不可写的置灰 */
    for (int i = 0; i < nm_btn_n; i++) {
        bool usable = (i < m.mode_btn_n) && m.mode_btn[i].writable;
        bool on = (i < m.mode_btn_n) && m.mode_value[0] &&
                  strcmp(m.mode_btn[i].value, m.mode_value) == 0;

        lv_label_set_text(nm_btn_labels[i],
                          (i < m.mode_btn_n) ? m.mode_btn[i].name : "-");
        lv_obj_set_style_bg_color(nm_btns[i],
                                  lv_color_hex(on ? 0x00A0B0 : (usable ? 0x2A2A2A : 0x1C1C1C)), 0);
        lv_obj_set_style_text_color(nm_btn_labels[i],
                                    lv_color_hex(on ? 0xFFFFFF : (usable ? 0xA0A0A0 : 0x606060)), 0);
    }

    /* 右上角"当前 xxx"：用服务端给的**短名**（自动 / 仅4G / 4G+5G）——
     * 全名太长（"自动（2G/3G/4G/5G）"）会和左边那行撞在一起（真机截图踩过），
     * 认不出来的取值才退回全名。 */
    if (strcmp(nm_prev_val, m.mode_value) != 0) {
        const char *short_name = NULL;

        for (int i = 0; i < m.mode_btn_n; i++)
            if (strcmp(m.mode_btn[i].value, m.mode_value) == 0)
                short_name = m.mode_btn[i].name;
        if (!short_name && m.mode_short[0])
            short_name = m.mode_short;
        if (!short_name && m.mode_name[0])
            short_name = m.mode_name;
        if (short_name)
            snprintf(buf, sizeof(buf), "当前 %s", short_name);
        else
            snprintf(buf, sizeof(buf), "%s", "");
        lv_label_set_text(nm_cur, buf);
        snprintf(nm_prev_val, sizeof(nm_prev_val), "%s", m.mode_value);
        nm_prev_cur_len = (int)strlen(buf);
        lv_obj_invalidate(nm_cur);
    }

    /* 反馈那行：服务端给的 msg（已切换 / 设置失败：…）；没有反馈时显示当前模式的全名。
     * ⚠️ **永远不要把它设成空串**：空串会让 label 缩到 0 宽，
     * 老像素就留在屏上了（实测截图：按钮都变成 4G/5G 了，这行还停在一开始的"读取中…"）。
     * 只读机型把原因说清楚。 */
    if (m.mode_readonly)
        snprintf(buf, sizeof(buf), "%s", "本机只能读组网模式，不能改");
    else if (m.mode_msg[0])
        snprintf(buf, sizeof(buf), "%s", m.mode_msg);
    else if (m.mode_name[0])
        snprintf(buf, sizeof(buf), "当前为 %s", m.mode_name);
    else
        snprintf(buf, sizeof(buf), "%s", "—");
    if (strcmp(nm_prev_msg, buf) != 0) {
        lv_label_set_text(nm_msg, buf);
        lv_obj_invalidate(nm_msg);          /* 文本变短/变长都请重画一遍（见上面的坑） */
        snprintf(nm_prev_msg, sizeof(nm_prev_msg), "%s", buf);
    }
}

void netmode_page_refresh(void)
{
    /* 服务端侧带 TTL 缓存：缓存新鲜时这条只是"把现成那份再推一次"，不发 AT */
    ws_client_send_text("{\"action\":\"mode_get\",\"modem_id\":0}");
}

void create_netmode_page(lv_obj_t *parent)
{
    netmode_page = lv_obj_create(parent);
    lv_obj_set_style_bg_color(netmode_page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(netmode_page, LV_OPA_COVER, 0);
    lv_obj_set_size(netmode_page, 320, 140);
    lv_obj_set_style_radius(netmode_page, 0, 0);
    lv_obj_set_style_border_width(netmode_page, 0, 0);
    /* ⚠️ 必须清掉主题默认 20px 内边距，否则整排按钮会被推到右下、末个还被裁掉（各页都踩过） */
    lv_obj_set_style_pad_all(netmode_page, 0, 0);
    lv_obj_set_scrollbar_mode(netmode_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(netmode_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(netmode_page);

    /* 左上标题小字 */
    lv_obj_t *t = lv_label_create(netmode_page);
    lv_label_set_text(t, "组网模式偏好");
    lv_obj_set_style_text_font(t, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x808080), 0);
    lv_obj_set_pos(t, 12, 6);

    /* 右上"当前 xxx" */
    nm_cur = lv_label_create(netmode_page);
    lv_label_set_text(nm_cur, "");
    lv_obj_set_style_text_font(nm_cur, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(nm_cur, lv_color_hex(0x00BEFF), 0);
    lv_obj_align(nm_cur, LV_ALIGN_TOP_RIGHT, -12, 6);

    /* 三个挡位按钮（先建好，数据来了再填字） */
    for (int i = 0; i < 3 && i < WS_MODE_BTN_MAX; i++) {
        lv_obj_t *b = lv_obj_create(netmode_page);

        lv_obj_set_size(b, NETMODE_BTN_W, NETMODE_BTN_H);
        lv_obj_set_pos(b, NETMODE_BTN_X0 + i * (NETMODE_BTN_W + NETMODE_BTN_GAP), NETMODE_BTN_Y);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        /* 手势泡泡：在按钮上左右划也要能换页（不泡泡会被按钮吃掉） */
        lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(b, nm_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        nm_btn_labels[i] = lv_label_create(b);
        lv_label_set_text(nm_btn_labels[i], "-");
        lv_obj_set_style_text_font(nm_btn_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(nm_btn_labels[i], lv_color_hex(0xA0A0A0), 0);
        lv_obj_center(nm_btn_labels[i]);
        nm_btns[i] = b;
        nm_btn_n = i + 1;
    }

    /* 反馈行 */
    nm_msg = lv_label_create(netmode_page);
    lv_label_set_text(nm_msg, "读取中…");
    lv_obj_set_style_text_font(nm_msg, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(nm_msg, lv_color_hex(0x90EE90), 0);
    lv_obj_align(nm_msg, LV_ALIGN_TOP_MID, 0, 92);

    /* 提示行 */
    lv_obj_t *hint = lv_label_create(netmode_page);
    lv_label_set_text(hint, "点一下切换 · 切完可能短暂断网");
    lv_obj_set_style_text_font(hint, &lv_font_cjk_sms_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x707070), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 114);

    nm_tick(NULL);                                        /* 先按"没有数据"画一次 */
    lv_timer_create(nm_tick, 300, NULL);
}
