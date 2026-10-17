/*
 * SMS 页 —— 短信列表 + 逐条阅读（竖页 2，紧跟 Mobile 后面）。
 *
 * 用户要求：「在 Mobile 页面后面插入一个 SMS 页面…显示是否有最新信息、短信数量等，
 * 然后左右划就是读取每条短信，短信是要合并的那条（一条短信 3 条来的合并一条，
 * 我要读取的就是那 3 合 1 的那条），然后有新短信在屏幕弹出提示框，点击进去读取到这条短信
 * 显示在左右划的屏幕里面」。
 *
 * 页面结构（`PAGE_COLS` 里第 2 页的列数是**动态**的，随短信条数变化）：
 *
 *   [SMS 列表] ←→ [第 1 条(最新)] ←→ [第 2 条] ←→ …（最多 WS_SMS_MAX 条；划到最后回到列表）
 *
 *  主页（短信统计）—— 左右划的第一屏：
 *   ┌─────────────────────────────────────────────┐
 *   │ 短信                                         │
 *   │ ┌────┐ ┌────┐ ┌────┐ ┌────┐                  │
 *   │ │ 4  │ │ 2  │ │ 2  │ │ 1  │                  │
 *   │ │总数│ │未读│ │已读│ │合并│                  │
 *   │ └────┘ └────┘ └────┘ └────┘                  │
 *   └─────────────────────────────────────────────┘
 *  （用户要求"简单点、块显示、不要这么多文字" ⇒ 就四个大数字 + 两个字的标签；
 *    有未读时"未读"那块数字/标签变酒绿、底色微亮；卡里没短信时右上角提一句）
 *
 *  详情页（**同一个对象**，内容随当前屏切换 —— 不用给 16 条短信各建一页）：
 *   ┌─────────────────────────────────────────────┐
 *   │ 10086          2/4 3条合并 未读 10-07 12:30 │
 *   │ 长正文（自动换行；一屏放不下就**自己往上平滑滚**）│
 *   └─────────────────────────────────────────────┘
 *  ⚠️ 条数、"未读"、时间**挤在同一行**（用户要求：条数与"未读"放时间前面）——
 *     屏幕只有 172px 高，原来单独一行太浪费，正文区因此从 84px 加到 106px。
 *  ⚠️ 正文故意**不可滚、无滚动条、也不要手指拖**：屏幕上的上下划是换页用的
 *     （拖到滚动框上就和换页打架），装不下就让它自己 100ms/2px 往上跑：
 *     到底停 2 秒回开头（跑马灯）。见 sms_scroll_tick()。
 *
 *  新短信弹窗：盖在所有页之上，点一下跳到这条短信的详情页；15 秒自己消失。
 *
 * 数据：modemd 的 `sms` 推送（正文/号码/时间/分条信息），**长短信服务端已合并成一条**。
 * 正文字体用 `lv_font_cjk_sms_16`（GB2312 全字集，6963 字；因为短信内容是任意汉字，
 * 不能像别的页面那样只挑几十个字做子集）。生成方法见 tools/gen/gb2312chars.js 的注释。
 */

#include "../lvgl/lvgl.h"
#include "smspage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../utils/wsdata.h"
#include "../utils/wsclient.h"   /* ws_client_send_text()：给服务端发"这条看过啦" */

/* 版式/流程自检：TFT_UI_DEBUG=1 时把坐标与实际取到的数据打出来（同其它页面）。
 * ⚠️ 必须 fflush：procd 把 stdout 接成管道 ⇒ 行缓冲失效，不刷就全盬在缓冲区里。 */
static bool sms_dbg;
#define SMS_DBG(...)                                                           \
    do {                                                                       \
        if (sms_dbg) {                                                         \
            printf(__VA_ARGS__);                                               \
            fflush(stdout);                                                    \
        }                                                                      \
    } while (0)

/* main.c 里的页面切换（弹窗点击要跳页） */
extern void tft_show_page(int page, int col);
extern void tft_wake_for_alert(const char *why);   /* 主界面：息屏时有事发生 ⇒ 亮屏 + 留一会儿 */

/* 短信正文/弹窗用的全字库 */
LV_FONT_DECLARE(lv_font_cjk_sms_16);

#define SMS_HDR_Y         4      /* 主页标题行 y */
#define SMS_BLK_Y         30     /* 主页四个大数字块 */
#define SMS_BLK_W         74
#define SMS_BLK_H         84
#define SMS_STAT_N        4      /* 总数 / 未读 / 已读 / 合并 */
/* 详情页版面（块内坐标）：头部一行 + 与正文框的间距 + 正文框（底部留 2px） */
#define SMS_DT_HDR_Y    2
#define SMS_DT_GAP      8
#define SMS_DT_BODY_H   98

/* ---- 删除确认弹窗（在短信详情页**长按**弹出来，用户要求） ---- */
static lv_obj_t *del_pop;
static lv_obj_t *del_text;                        /* 提示行：号码 + 时间 */
static char      del_index[8];                    /* 要删的那条的存储位置 */
static uint32_t  del_at;                          /* 弹窗时间（超时自动收） */

/* 要删的那条**各段的存储位置**（长短信要逐段删，见 send_sms_delete）。
 * 上限 8：实测最长的是 3 段，留够余量。 */
#define DEL_PARTS_MAX 8
static char      del_parts[DEL_PARTS_MAX][8];
static int       del_parts_n;

/* 弹窗自动收：跟新短信弹窗一样，避免一直挡着 */
#define DEL_AUTO_HIDE_MS 10000

#define POP_AUTO_HIDE_MS  15000  /* 弹窗自动消失 */

/* 看过就标已读（用户要求："查看完了就要把未读处理下"）——
 * 某条未读在详情页**停留够这么久**才算"看过"（快速划过去不算），
 * 然后给服务端发 {"action":"sms_read","index":...}，由它发 AT+CMGR 把存储里的
 * 状态改成 REC READ 并重读列表，于是首页未读数与"未读"标记一起消掉。 */
#define SMS_READ_DWELL_MS 1500

/* 正文自动滚动（一屏放不下时）
 *
 * ⚠️ 平滑的关键是**步长小、更新密**（原来 100ms/2px 每秒才 10 步、每步跳 2 像素，
 *    看着一跳一跳的）；但速度也不能快 —— 快了“没看完就跑了”（用户反馈）。
 *    现在：40ms/1px 太快的下一次调试 ⇒ **100ms/1px = 10px/s**，
 *    一秒大概爬半行，一行字有 1.9 秒可读；每步只动 1 像素，不会一顿一顿。
 *    LVGL 自己的刷新定时器（refr_period，默认 30ms）会把位移合并成一次重画。 */
#define SMS_SCROLL_MS       100  /* 每拍间隔（同时也是"看过就标已读"的倒计时单位） */
#define SMS_SCROLL_PX       1    /* 每拍上移几像素 ⇒ 10px/s */
#define SMS_SCROLL_START_MS 1200 /* 开头先停一下再滚 */
#define SMS_SCROLL_END_MS   3000 /* 滚到底多停一会儿（让人把最后一行看完）再回开头 */

/* ---- 主页（**只放统计**：大数字块 + 两字标签，尽量少字） ---- */
static lv_obj_t *sms_list_page;
static lv_obj_t *lst_head_cnt;                    /* 右上：只在没短信时提一句 */
static lv_obj_t *lst_num[SMS_STAT_N];             /* 大数字（montserrat_24） */
static lv_obj_t *lst_name[SMS_STAT_N];            /* 两字标签：总数/未读/已读/合并 */
static lv_obj_t *lst_stat_box[SMS_STAT_N];        /* 四个块的底框（未读那块会亮） */

/* ---- 详情页 ---- */
static lv_obj_t *sms_detail_page;
static lv_obj_t *dt_box;                          /* 正文外面的框（裁切用） */
static lv_obj_t *dt_num, *dt_head, *dt_body;      /* dt_head = "1/4 未读 10-07 15:42" */
static int       dt_show = -1;                    /* 当前显示第几条（-1 = 还没设过） */
static int       dt_scroll_max, dt_scroll_y;      /* 正文可滚距离 / 当前偏移 */
static int       dt_pause;                        /* 停留倒计时（ms），>0 时不动 */
static int       dt_read_wait;                    /* >0 = "看过"计时中（ms） */
static char      dt_index[8];                     /* 当前这条的存储位置 */
static char      dt_read_sent[8];                 /* 已经发过"已读"的位置（别重复发） */
static bool      dbg_shown;                       /* 调试钩子 TFT_SMS_SHOW 只做一次 */static bool        dbg_query_armed;                 /* 调试钩子 TFT_SMS_QUERY 只做一次 */

/* ---- 余额查询屏（SMS 竖页最后一屏）----
 * 运营商表：**号码 / 指令**要按当地运营商口径改就改这里（卡类型用服务端的 cops 字段认，
 * 认不出来再用中文名里的“移动/联通/电信/广电”兜底）。 */
static const struct {
    const char *cops;      /* 服务端口径的运营商分类 */
    const char *name;      /* 中文名（兜底匹配用） */
    const char *num;       /* 查询号码 */
    const char *cmd;       /* 查询指令（短信正文） */
} sms_ops[] = {
    { "CMCC",    "中国移动", "10086", "CXHF" },
    { "Unicom",  "中国联通", "10010", "CXYE" },
    { "Telecom", "中国电信", "10001", "CXHF" },
    { "CBN",     "中国广电", "10099", "CXHF" },
};
#define SMS_OPS_N ((int)(sizeof(sms_ops) / sizeof(sms_ops[0])))

static lv_obj_t *sms_query_page;
static lv_obj_t *q_op_val;            /* 卡类型（值） */
static lv_obj_t *q_cmd_val;           /* 将发送的指令 */
static lv_obj_t *q_state;             /* 状态行（发送结果） */
static lv_obj_t *q_btn;
static lv_obj_t *q_btn_lbl;
static lv_obj_t *q_pop, *q_pop_ok;    /* 发送前的二次确认框 */
static lv_obj_t *q_pop_title, *q_pop_text;
static int       q_op_idx = -1;       /* 命中的表下标（-1 = 没认出来） */
static uint32_t  q_busy_until;        /* 发送冷却（LVGL tick = 开机毫秒） */
static unsigned  q_seen_seq;          /* 已经显示过的 sendsms 结果版本 */

/* 前向声明（smspage_tick 比定义先用到它们） */
static void q_refresh(const ws_snapshot_t *s);
static void q_hide_pop(void);
static void q_btn_cb(lv_event_t *e);static bool      dbg_del_armed;                   /* 调试钩子 TFT_SMS_DELTEST 只做一次 */
static bool      dbg_pop_armed;                   /* 调试钩子 TFT_SMS_POPTEST 只做一次 */
static uint32_t  dbg_del_at;                      /* 上面那个"自动按删除"的计时 */

/* ---- 长按（自己数时间，不用 LVGL 的 LV_EVENT_LONG_PRESSED） ----
 * 这块屏只报 ABS_MT_* 事件，LVGL 的长按要 pointer->scroll_obj == NULL
 * 且按下期间一直收到 PRESSED 才算数，实测按住不动也常常不触发 ⇒ 自己数：
 * PRESSED 记下时刻，50ms 定时器检查，够了 550ms 就弹删除确认。 */
#define DT_LONG_MS 550
static lv_timer_t *dt_lp_timer;
static uint32_t    dt_press_at;                   /* 手指按下的时刻 */
static bool        dt_pressed;                    /* 手指还按着 */
static bool        dt_scrolled;                   /* 这次按下期间滑过（滑动不算长按） */

/* ---- 新短信弹窗 ---- */
static lv_obj_t *sms_pop;
static lv_obj_t *pop_who, *pop_text;
static unsigned  pop_seq;                         /* 已经为哪个版本弹过 */
static int       pop_col;                         /* 点击后跳到第几屏 */
static uint32_t  pop_at;

/* ---------------- 小工具 ---------------- */

/* 服务端时间戳 "26/10/07,12:30:00+32" → "10-07 12:30"（认不出就原样贴） */
static void fmt_time(const char *in, char *out, size_t cap)
{
    if (!in || !in[0]) {
        snprintf(out, cap, "");
        return;
    }
    if (strlen(in) >= 14 && in[2] == '/' && in[5] == '/' && in[8] == ',')
        snprintf(out, cap, "%.2s-%.2s %.5s", in + 3, in + 6, in + 9);
    else
        snprintf(out, cap, "%.*s", (int)(cap - 1), in);
}

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (!o)
        return;
    if (hidden)
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------- 弹窗 ---------------- */

static void hide_popup(void)
{
    set_hidden(sms_pop, true);
}

/* 点击弹窗 → 跳到那条短信的详情页（主界面自己会 hide/show 页面） */
static void pop_click_cb(lv_event_t *e)
{
    (void)e;
    hide_popup();
    tft_show_page(SMSPAGE_PAGE_INDEX, pop_col);
}

/* 点"忽略" → 只是收起来，不动短信（想看随时从 SMS 页划过去） */
static void pop_ignore_cb(lv_event_t *e)
{
    (void)e;
    hide_popup();
}

/* ---------------- 删除确认 ---------------- */

static void hide_delete_popup(void)
{
    set_hidden(del_pop, true);
}

/* 真要删：告诉服务端删这条（它删完会重读并广播），然后回到统计主页。
 *
 * ⚠️⚠️ **长短信必须逐段删**（用户 2026-10-07 报"屏幕点击删除不成功，web 后台删除就行"）：
 *   一条长短信在卡里是**多段**存的、各占一个存储位置（实测江苏电信流量提醒
 *   part_total=3、段位置 5/4/…）。只发一条 index=头部那段 ⇒ 服务端删掉一段、
 *   剩下的段重拼后又出现 ⇒ 界面上看着"删不掉"。web 那边就是这么干的
 *   （index.html：`if (d.delparts) for (const i of idxs) post(...)`，
 *   注释写着"逐段删除，避免留下孤儿段"）——这里照做。 */
static void send_sms_delete(void)
{
    char idx[8];
    int  sent = 0;

    if (!del_index[0])
        return;
    snprintf(idx, sizeof(idx), "%s", del_index);
    del_index[0] = '\0';
    if (strcmp(idx, dt_read_sent) == 0)
        dt_read_sent[0] = '\0';       /* 这条已经没了，清掉防重复 */

    for (int i = 0; i < del_parts_n; i++) {
        char cmd[96];

        snprintf(cmd, sizeof(cmd),
                 "{\"action\":\"sms_delete\",\"modem_id\":0,\"index\":\"%s\"}", del_parts[i]);
        if (ws_client_send_text(cmd))
            sent++;
    }
    del_parts_n = 0;

    if (sent == 0) {
        /* 没有段信息（老服务端 / 普通短信）⇒ 至少把头部那一段删掉 */
        char cmd[96];

        snprintf(cmd, sizeof(cmd),
                 "{\"action\":\"sms_delete\",\"modem_id\":0,\"index\":\"%s\"}", idx);
        if (ws_client_send_text(cmd))
            sent = 1;
    }
    if (sent > 0) {
        SMS_DBG("[sms] 删除 idx=%s（%d 段）\n", idx, sent);
        tft_show_page(SMSPAGE_PAGE_INDEX, 0);   /* 删完回主页（条数/未读一起刷新） */
    }
}

static void del_ok_cb(lv_event_t *e)
{
    (void)e;
    hide_delete_popup();
    send_sms_delete();
}

static void del_cancel_cb(lv_event_t *e)
{
    (void)e;
    hide_delete_popup();
    del_index[0] = '\0';
}

/* 长按详情页 ⇒ 弹"删除这条短信？删除 / 取消"。
 *
 * 用户要求从双击改成**长按**：双击得连着点两下、手感"反应慢"，长按更直白。
 * 触发条件：手指按住详情页 550ms 不松、期间也没有滑动。 */
static void dt_open_delete(void)
{
    char buf[128];

    if (dt_show < 0 || !dt_index[0])
        return;
    snprintf(del_index, sizeof(del_index), "%s", dt_index);

    /* 长短信：把这条的**各段存储位置**记下来（删的时候要逐段删，见 send_sms_delete） */
    del_parts_n = 0;
    {
        ws_snapshot_t s;

        wsdata_snapshot(&s);
        if (dt_show < s.sms_n) {
            const ws_sms_t *m = &s.sms[dt_show];

            for (int i = 0; i < m->part_n && del_parts_n < DEL_PARTS_MAX; i++) {
                bool dup = false;

                for (int k = 0; k < del_parts_n; k++)
                    if (strcmp(del_parts[k], m->part_idx[i]) == 0) dup = true;
                if (!dup && m->part_idx[i][0])
                    snprintf(del_parts[del_parts_n++], sizeof(del_parts[0]), "%s", m->part_idx[i]);
            }
        }
    }
    /* 头部那条 index 也带上（服务端的 parts[] 里一般就含它，这里再兜一层） */
    {
        bool dup = false;

        for (int k = 0; k < del_parts_n; k++)
            if (strcmp(del_parts[k], del_index) == 0) dup = true;
        if (!dup && del_parts_n < DEL_PARTS_MAX)
            snprintf(del_parts[del_parts_n++], sizeof(del_parts[0]), "%s", del_index);
    }

    snprintf(buf, sizeof(buf), "%s  %s", lv_label_get_text(dt_num),
             lv_label_get_text(dt_head));
    lv_label_set_text(del_text, buf);
    lv_obj_move_foreground(del_pop);
    set_hidden(del_pop, false);
    del_at = lv_tick_get();
    SMS_DBG("[sms] 长按 ⇒ 确认删除 idx=%s（%d 段）\n", del_index, del_parts_n);
}

static void dt_lp_timer_cb(lv_timer_t *t)
{
    if (!dt_pressed || dt_scrolled) {
        lv_timer_pause(t);
        return;
    }
    if (lv_tick_elaps(dt_press_at) < DT_LONG_MS)
        return;
    dt_pressed = false;               /* 只弹一次 */
    lv_timer_pause(t);
    dt_open_delete();
}

/* 按下/松开/滑动：按下开始计时，滑动或松开就取消 */
static void dt_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        dt_pressed = true;
        dt_scrolled = false;
        dt_press_at = lv_tick_get();
        if (dt_lp_timer)
            lv_timer_resume(dt_lp_timer);
        SMS_DBG("[sms] 详情按下（开始数长按）\n");
    } else if (code == LV_EVENT_SCROLL_BEGIN) {
        dt_scrolled = true;
    } else {                          /* RELEASED / PRESS_LOST / CLICKED */
        dt_pressed = false;
        if (dt_lp_timer)
            lv_timer_pause(dt_lp_timer);
        SMS_DBG("[sms] 详情松开(%d)\n", (int)code);
    }
}

/* 兼容调试钩子 TFT_SMS_DELTEST：直接弹删除确认 */
static void dt_longpress_cb(lv_event_t *e)
{
    (void)e;
    dt_open_delete();
}

/* 给主界面（main.c 的触摸层）用的入口："两指同点 / 连点两下" ⇒ 弹删除确认。
 * 用户 2026-10-07 定的口径（这块屏报不了 BTN_TOUCH，长按判不准）。 */
void smspage_open_delete(void)
{
    dt_open_delete();
}

static void show_popup(const ws_snapshot_t *s)
{
    char buf[128];

    if (s->sms_n <= 0)
        return;
    pop_col = 1;                       /* 快捷键 = 最新那一条 */
    snprintf(buf, sizeof(buf), "%s", s->sms[0].number[0] ? s->sms[0].number : "新短信");
    lv_label_set_text(pop_who, buf);
    snprintf(buf, sizeof(buf), "%.*s", 44, s->sms[0].text);
    lv_label_set_text(pop_text, buf);
    lv_obj_move_foreground(sms_pop);
    set_hidden(sms_pop, false);
    pop_at = lv_tick_get();
}

/* ---------------- 页面内容刷新 ---------------- */

static void refresh_list(const ws_snapshot_t *s)
{
    char buf[16];
    int  merged = 0;

    /* 主页**只放统计**，而且尽量少字：块状 = 大数字 + 两字标签（用户要求）。
     *   · 长短信（3 合 1 那种）条数 = part_total > 1 的；服务端已合并成一条 */
    for (int i = 0; i < s->sms_n && i < WS_SMS_MAX; i++) {
        if (s->sms[i].part_total > 1)
            merged++;
    }

    snprintf(buf, sizeof(buf), "%d", s->sms_n);
    lv_label_set_text(lst_num[0], buf);
    snprintf(buf, sizeof(buf), "%d", s->sms_unread);
    lv_label_set_text(lst_num[1], buf);
    snprintf(buf, sizeof(buf), "%d", s->sms_n > s->sms_unread ? s->sms_n - s->sms_unread : 0);
    lv_label_set_text(lst_num[2], buf);
    snprintf(buf, sizeof(buf), "%d", merged);
    lv_label_set_text(lst_num[3], buf);

    /* 有未读 ⇒ 未读那块亮起来（数字酒绿 + 淡绿底），没有就压暗 */
    lv_obj_set_style_text_color(lst_num[1],
                                lv_color_hex(s->sms_unread > 0 ? 0x90EE90 : 0x6E6E6E), 0);
    lv_obj_set_style_text_color(lst_name[1],
                                lv_color_hex(s->sms_unread > 0 ? 0x90EE90 : 0x8A8A8A), 0);
    lv_obj_set_style_bg_color(lst_stat_box[1],
                              s->sms_unread > 0 ? lv_color_make(24, 48, 32)
                                                : lv_color_make(30, 30, 30), 0);

    /* 卡里没短信时右上角提一句，别的啥都不堆 */
    lv_label_set_text(lst_head_cnt, s->sms_n > 0 ? "" : "卡里没有短信");
}

static void refresh_detail(const ws_snapshot_t *s)
{
    char buf[80], t[24];
    const ws_sms_t *m;
    int idx = dt_show;

    if (idx < 0)
        idx = 0;
    if (idx >= s->sms_n) {
        lv_label_set_text(dt_num, "没有短信");
        lv_label_set_text(dt_head, "");
        lv_label_set_text(dt_body, "");
        return;
    }
    m = &s->sms[idx];
    lv_label_set_text(dt_num, m->number[0] ? m->number : "-");
    fmt_time(m->time, t, sizeof(t));

    /* 头部右边一行放完：「第几条/共几条  合并情况  未读  时间」——
     * 用户要求条数与"未读"都放在**时间前面**，屏幕高度本来就紧（原先是单独一行）。 */
    if (m->part_total > 1)
        snprintf(buf, sizeof(buf), "%d/%d %d条合并%s %s", idx + 1, s->sms_n, m->part_total,
                 m->unread ? " 未读" : "", t);
    else
        snprintf(buf, sizeof(buf), "%d/%d%s %s", idx + 1, s->sms_n, m->unread ? " 未读" : "", t);
    lv_label_set_text(dt_head, buf);
    lv_obj_set_style_text_color(dt_head, lv_color_hex(m->unread ? 0x90EE90 : 0xA0A0A0), 0);
    lv_obj_align(dt_head, LV_ALIGN_TOP_RIGHT, -10, 4);

    lv_label_set_text(dt_body, m->text);

    /* 未读的：开始"看过"计时（见 SMS_READ_DWELL_MS）；同一个位置只发一次已读 */
    snprintf(dt_index, sizeof(dt_index), "%s", m->index);
    dt_read_wait = 0;
    if (m->unread && dt_index[0] && strcmp(dt_index, dt_read_sent) != 0)
        dt_read_wait = SMS_READ_DWELL_MS;

    /* 换了短信：回到开头，量一下正文比可视区高多少（要不要自动滚） */
    lv_obj_set_y(dt_body, 0);
    lv_obj_update_layout(dt_box);       /* 先让它把换行后的高度算出来 */
    {
        int32_t view = lv_obj_get_content_height(dt_box);
        int32_t text = lv_obj_get_height(dt_body);

        dt_scroll_max = (text > view) ? (int)(text - view) : 0;
        dt_scroll_y = 0;
        dt_pause = dt_scroll_max > 0 ? SMS_SCROLL_START_MS : 0;
        SMS_DBG("[sms] detail idx=%d n=%d 正文%dpx 可视%dpx 可滚%dpx\n",
                idx, s->sms_n, (int)text, (int)view, dt_scroll_max);
    }
}

void smspage_show(int idx)
{
    ws_snapshot_t s;

    dt_show = idx;
    wsdata_snapshot(&s);
    SMS_DBG("[sms] show idx=%d （当前列表 %d 条）\n", idx, s.sms_n);
    refresh_detail(&s);
}

int smspage_msg_count(void)
{
    int n = wsdata_sms_count();

    return (n > WS_SMS_MAX) ? WS_SMS_MAX : (n < 0 ? 0 : n);
}

lv_obj_t *smspage_list(void)   { return sms_list_page; }
lv_obj_t *smspage_detail(void) { return sms_detail_page; }
lv_obj_t *smspage_query(void)  { return sms_query_page; }

/* 1 秒擦一遍：内容变了重画，来了新短信弹窗（15 秒后自动收） */
static void smspage_tick(lv_timer_t *t)
{
    static unsigned seen_seq, seen_new;
    static bool primed;
    ws_snapshot_t s;

    (void)t;
    wsdata_snapshot(&s);

    if (!primed || s.sms_seq != seen_seq) {
        primed = true;
        seen_seq = s.sms_seq;
        SMS_DBG("[sms] tick seq=%u new=%u n=%d dt_show=%d\n",
                s.sms_seq, s.sms_new_seq, s.sms_n, dt_show);
        refresh_list(&s);
        if (dt_show >= 0)
            refresh_detail(&s);
    }

    /* 余额查询屏（最后一屏）：卡类型 / 指令 / 发送结果都得跟着服务端变 */
    q_refresh(&s);
    if (!lv_obj_has_flag(q_pop, LV_OBJ_FLAG_HIDDEN) &&
        lv_obj_has_flag(sms_query_page, LV_OBJ_FLAG_HIDDEN))
        q_hide_pop();                                             /* 划走了就收框 */
    if (s.sms_new_seq != seen_new) {
        seen_new = s.sms_new_seq;
        show_popup(&s);
        /* 用户 2026-10-08：**息屏时来新短信要把屏叫亮** —— 不亮的话提示框画在背光关着的屏上，
         * 谁知道来过短信；叫亮后还会留一小会儿，够看清（甚至点进去看）。
         * ⚠️ 这里用的“有没有新短信”判据与弹窗完全同一套（wsdata 里的 sms_new_seq，见那里的
         *    三轮才定稿的注释）⇒ 不会出现“弹窗弹了但没亮屏”或反过来。 */
        tft_wake_for_alert("新短信");
    }

    /* 调试钩子：TFT_SMS_SHOW=2 ⇒ 短信数据一到就自动翻到第 2 条。
     * （`-page 2 -col N` 在开机时数据还没到的情况下会被 page_col_count() 夹回主页，
     *   抓详情页截图用这个更省事；只做一次，平时不设这个环境变量就是空操作。） */
    if (!dbg_shown) {
        const char *v = getenv("TFT_SMS_SHOW");

        if (v && v[0] && s.sms_n > 0) {
            int idx = atoi(v);

            dbg_shown = true;
            SMS_DBG("[sms] TFT_SMS_SHOW=%s ⇒ 翻到第 %d 条\n", v, idx);
            tft_show_page(SMSPAGE_PAGE_INDEX, idx > 0 ? idx : 0);
        }
    }

    /* 调试钩子：TFT_SMS_DELTEST=1 ⇒ 详情页一出现就假装长按（弹确认框），
     * 8 秒后自动按"删除" —— 用来抓确认框截图 + 看服务端有没有收到 sms_delete。 */
    if (getenv("TFT_SMS_DELTEST") && dt_show >= 0 && !dbg_del_armed) {
        dbg_del_armed = true;
        dt_longpress_cb(NULL);
        dbg_del_at = lv_tick_get();
    }
    if (dbg_del_at && lv_tick_elaps(dbg_del_at) > 8000) {
        dbg_del_at = 0;
        del_ok_cb(NULL);
    }

    /* 调试钩子：TFT_SMS_POPTEST=1 ⇒ 拿到短信列表就弹一次"新短信"框（抓版面截图用，
     * 写法与 TFT_SMS_DELTEST 一致）。 */
    if (getenv("TFT_SMS_POPTEST") && !dbg_pop_armed && s.sms_n > 0) {
        dbg_pop_armed = true;
        show_popup(&s);
    }

    /* 调试钩子：TFT_SMS_QUERY=1 ⇒ 跳到最后一屏（余额查询）并弹一次确认框（拍屏用）；
     * TFT_SMS_QUERY=2 ⇒ 只跳屏、不弹框（看页面本身）。
     * ⚠️ 触发条件用 `msgs > 0`（收到过任意一帧）而不是 `sms_seen`：卡里一条短信都没有时
     *    服务端**根本不会推 sms**（空列表不广播）⇒ 拿 sms_seen 当条件，这个钩子永远不会动。 */
    if (getenv("TFT_SMS_QUERY") && !dbg_query_armed && s.msgs > 0) {
        int mode = atoi(getenv("TFT_SMS_QUERY"));

        dbg_query_armed = true;
        tft_show_page(SMSPAGE_PAGE_INDEX, 1 + smspage_msg_count());
        if (mode == 1)
            q_btn_cb(NULL);
        SMS_DBG("[sms] TFT_SMS_QUERY=%d ⇒ 跳到余额查询屏\n", mode);
    }

    if (!lv_obj_has_flag(sms_pop, LV_OBJ_FLAG_HIDDEN) &&
        lv_tick_elaps(pop_at) > POP_AUTO_HIDE_MS)
        hide_popup();

    /* 删除确认框：超时自动收；详情页被划走（或删完回主页）也收，别留在屏幕上 */
    if (!lv_obj_has_flag(del_pop, LV_OBJ_FLAG_HIDDEN)) {
        if (lv_tick_elaps(del_at) > DEL_AUTO_HIDE_MS ||
            lv_obj_has_flag(sms_detail_page, LV_OBJ_FLAG_HIDDEN))
            hide_delete_popup();
    }
}

/* 告诉服务端"这条我看过了" ⇒ 它发一条 AT+CMGR 把卡里那条的状态改成已读，
 * 再重读列表广播回来（首页的未读数与"未读"标记就一起消掉）。同一个位置只发一次。 */
static void send_sms_read(void)
{
    char cmd[96];

    if (!dt_index[0] || strcmp(dt_index, dt_read_sent) == 0)
        return;
    snprintf(cmd, sizeof(cmd),
             "{\"action\":\"sms_read\",\"modem_id\":0,\"index\":\"%s\"}", dt_index);
    if (ws_client_send_text(cmd)) {
        snprintf(dt_read_sent, sizeof(dt_read_sent), "%s", dt_index);
        SMS_DBG("[sms] 看过 idx=%s ⇒ 已告诉服务端标已读\n", dt_index);
    }
}

/* 正文自动往上平滑滚（装不下才滚）。
 *
 * ⚠️ 为什么不用"能滚的容器 + 手指拖"：屏幕上的上下划是**换页**用的（main.c 的手势），
 *    正文框能拖就会和换页打架；滚动条在 320x172 上也占地方。所以这里让内容自己跑：
 *    每 100ms 上移 2px（约 1.2 行/秒），到底停 2 秒再回开头，像跑马灯一样循环。 */
static void sms_scroll_tick(lv_timer_t *t)
{
    (void)t;

    /* "看过就标已读"的计时放最前面：跟正文滚不滚没关系，不能被下面的早退跳过。
     * 页面一旦被划走（详情页藏起来）就作废 —— 划过去看一眼不算看过。 */
    if (dt_read_wait > 0) {
        if (sms_detail_page && lv_obj_has_flag(sms_detail_page, LV_OBJ_FLAG_HIDDEN))
            dt_read_wait = 0;
        else if ((dt_read_wait -= SMS_SCROLL_MS) <= 0)
            send_sms_read();
    }

    if (dt_scroll_max <= 0)
        return;                                     /* 装得下就不滚 */
    if (sms_detail_page && lv_obj_has_flag(sms_detail_page, LV_OBJ_FLAG_HIDDEN))
        return;                                     /* 页面没露出来就别刷屏（省 SPI 带宽） */
    if (dt_pause > 0) {
        dt_pause -= SMS_SCROLL_MS;
        if (dt_pause <= 0) {                        /* 停留结束 */
            dt_scroll_y = 0;
            lv_obj_set_y(dt_body, 0);               /* 回到开头 */
        }
        return;
    }
    dt_scroll_y += SMS_SCROLL_PX;
    if (dt_scroll_y >= dt_scroll_max) {
        dt_scroll_y = dt_scroll_max;
        lv_obj_set_y(dt_body, -dt_scroll_y);
        dt_pause = SMS_SCROLL_END_MS;               /* 到底了：停一会儿再回开头 */
        return;
    }
    lv_obj_set_y(dt_body, -dt_scroll_y);
}

/* ---------------- 建页面 ---------------- */

/* 页面骨架：黑底、无边框、pad 清零（坐标 = 屏幕坐标 - 30） */
static lv_obj_t *mk_page(lv_obj_t *main_screen)
{
    lv_obj_t *p = lv_obj_create(main_screen);

    lv_obj_set_size(p, 320, 140);
    lv_obj_set_style_bg_color(p, lv_color_black(), 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    return p;
}

/* 块：与其它页同一套外观 */
static lv_obj_t *mk_box(lv_obj_t *parent)
{
    lv_obj_t *b = lv_obj_create(parent);

    lv_obj_set_size(b, 316, 132);
    lv_obj_set_style_bg_color(b, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_border_color(b, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_30, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_pos(b, 2, 4);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, unsigned color,
                          int x, int y, int w, int h)
{
    lv_obj_t *l = lv_label_create(parent);

    if (font)
        lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (w > 0) {
        lv_obj_set_width(l, w);
        /* ⚠️ 必须同时限高：LV_LABEL_LONG_DOT 是"固定尺寸+尾巴省略号"，
         * 高度 auto 时它会自动换行撑高，把下面几行撞在一起（踩过）。 */
        lv_obj_set_height(l, h > 0 ? h : 17);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_pos(l, x, y);
    return l;
}

/* ====================== 余额查询屏（SMS 竖页的最后一屏） ======================
 *
 * 用户要求（2026-10-07）："在 SMS 短信的最后一条添加一个新 UI，里面根据卡类型发送查询余额的短信"。
 *
 * 位置：竖页第 2 页（SMS）的**最后一屏** ⇒ 屏序号 = 1（列表）+ 短信条数 + 1 ⇒ `1 + msg_count`。
 * 卡类型：服务端 network 推送里的 `op_name` / `cops`（Mobile/NET 页用的同一批字段）。
 * 发送：点按钮 → 二次确认 → 发 `{"action":"sms_send","modem_id":0,"number":…,"text":…}`；
 *       服务端发完（成败都推）回 type=sendsms 的一条 ⇒ 写到下面的状态行；
 *       运营商的回复短信会走正常的 sms 推送 ⇒ 划回列表就能看到余额。
 *
 * ⚠️ 表 / 全局 / 原型都声明在文件前面的静态区（smspage_tick 里要调 q_refresh），
 *    这里只放定义。
 */

/* 按 cops 认，认不出来再拿中文名兜底（“中国移动” 里含“移动”…） */
static int q_find_op(const ws_snapshot_t *s)
{
    for (int i = 0; i < SMS_OPS_N; i++)
        if (s->cops[0] && strcasecmp(s->cops, sms_ops[i].cops) == 0)
            return i;

    for (int i = 0; i < SMS_OPS_N; i++) {
        const char *key = sms_ops[i].name + 6;   /* “中国” 后面那两个字：移动/联通/电信/广电 */

        if (s->op_name[0] && strstr(s->op_name, key))
            return i;
    }
    return -1;
}

static void q_hide_pop(void)
{
    lv_obj_add_flag(q_pop, LV_OBJ_FLAG_HIDDEN);
}

static void q_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    q_hide_pop();
}

static void q_ok_cb(lv_event_t *e)
{
    char cmd[160];

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    q_hide_pop();
    if (q_op_idx < 0) return;

    snprintf(cmd, sizeof(cmd),
             "{\"action\":\"sms_send\",\"modem_id\":0,\"number\":\"%s\",\"text\":\"%s\"}",
             sms_ops[q_op_idx].num, sms_ops[q_op_idx].cmd);
    if (ws_client_send_text(cmd)) {
        q_busy_until = lv_tick_get() + 6000;
        lv_label_set_text(q_state, "发送中…");
        lv_obj_set_style_text_color(q_state, lv_color_hex(0xD0D0D0), 0);
        SMS_DBG("[sms] 余额查询 ⇒ %s\n", cmd);
    } else {
        lv_label_set_text(q_state, "没连上服务端，发不出去");
        lv_obj_set_style_text_color(q_state, lv_color_hex(0xFF6600), 0);
    }
}

/* 点“发送查询短信” ⇒ 先弹确认框（发短信不能误触） */
static void q_btn_cb(lv_event_t *e)
{
    char t[64], b[128];

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_tick_get() < q_busy_until) return;
    if (q_op_idx < 0) return;

    snprintf(t, sizeof(t), "发送余额查询？");
    snprintf(b, sizeof(b), "将发送「%s」到 %s\n运营商的回复会进短信列表",
             sms_ops[q_op_idx].cmd, sms_ops[q_op_idx].num);
    lv_label_set_text(q_pop_title, t);
    lv_label_set_text(q_pop_text, b);
    lv_obj_move_foreground(q_pop);
    lv_obj_clear_flag(q_pop, LV_OBJ_FLAG_HIDDEN);
}

/* 1 秒刷一次（卡类型 + 指令 + 状态行 + 按钮文案） */
static void q_refresh(const ws_snapshot_t *s)
{
    char buf[128];
    static int last_op = -2;
    static bool was_vis;
    static uint32_t net_at;
    bool vis = !lv_obj_has_flag(sms_query_page, LV_OBJ_FLAG_HIDDEN);

    q_op_idx = q_find_op(s);

    /* 卡类型还是空的就问一次服务端要网络状态。
     * ⚠️ 屏幕这条通道是 **Passive**（不算"有人在看"），没人开网页时服务端不会去查网络
     *    ⇒ op_name/cops 会是空的（实机就是"未识别"）。划到这一屏就主动要一次，
     *    最多 30 秒一次（发送/重连时那个入口服务端自己还有 15s 去抖）。 */
    if (vis && !s->op_name[0] && (!was_vis || lv_tick_elaps(net_at) > 30000)) {
        if (ws_client_send_text("{\"action\":\"net_get\",\"modem_id\":0}")) {
            net_at = lv_tick_get();
            SMS_DBG("[sms] 余额查询：卡类型空着 ⇒ 要一次 net_get\n");
        }
    }
    was_vis = vis;

    if (sms_dbg && q_op_idx != last_op) {     /* TFT_UI_DEBUG=1 时打一行，认不出卡类型时好排查 */
        last_op = q_op_idx;
        printf("[sms] 余额查询：cops=[%s] op_name=[%s] ⇒ 命中表 %d\n",
               s->cops, s->op_name, q_op_idx);
        fflush(stdout);
    }

    lv_label_set_text(q_op_val, (q_op_idx >= 0) ? sms_ops[q_op_idx].name
                                                : (s->op_name[0] ? s->op_name : "未识别"));
    lv_obj_set_style_text_color(q_op_val,
                                lv_color_hex(q_op_idx >= 0 ? 0xFFFFFF : 0xFF6600), 0);

    if (q_op_idx >= 0)
        snprintf(buf, sizeof(buf), "%s 到 %s", sms_ops[q_op_idx].cmd, sms_ops[q_op_idx].num);
    else
        snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(q_cmd_val, buf);

    /* 服务端回的结果：只在收到新的那条时覆盖状态行 */
    if (s->sendsms_seq != q_seen_seq) {
        q_seen_seq = s->sendsms_seq;
        q_busy_until = 0;
        if (s->sendsms_msg[0]) {
            lv_label_set_text(q_state, s->sendsms_msg);
            lv_obj_set_style_text_color(q_state,
                                        lv_color_hex(s->sendsms_ok ? 0x88E088 : 0xFF6600), 0);
        }
    }

    if (lv_tick_get() < q_busy_until) {
        lv_label_set_text(q_btn_lbl, "发送中…");
        lv_obj_set_style_bg_color(q_btn, lv_color_hex(0x3A3A3A), 0);
    } else if (q_op_idx >= 0) {
        lv_label_set_text(q_btn_lbl, "发送查询短信");
        lv_obj_set_style_bg_color(q_btn, lv_color_hex(0x1D5A66), 0);
    } else {
        lv_label_set_text(q_btn_lbl, "未识别到卡类型");
        lv_obj_set_style_bg_color(q_btn, lv_color_hex(0x3A3A3A), 0);
    }
}

void create_smspage_pages(lv_obj_t *main_screen)
{
    sms_dbg = (getenv("TFT_UI_DEBUG") != NULL);

    /* ---------- 列表页 ---------- */
    sms_list_page = mk_page(main_screen);
    {
        lv_obj_t *box = mk_box(sms_list_page);

        /* 用户要求：不要再写"短信"两个字（顶栏的 SMS 标题已经说明是哪一页了），
         * 主页就是四个数字块，越干净越好；只在没短信时右上角提一句。 */
        lst_head_cnt = mk_label(box, &lv_font_cjk_sms_16, 0x8A8A8A, 0, SMS_HDR_Y + 1, 0, 0);
        lv_label_set_text(lst_head_cnt, "");
        lv_obj_align(lst_head_cnt, LV_ALIGN_TOP_RIGHT, -10, SMS_HDR_Y + 1);

        /* 四个**大数字块**（每块：上面一个大数字、下面两个字的标签）——
         * 比"总数 4 / 最新 10-07 16:20 / 发件人 4 个"这种长句子清爽得多。 */
        for (int i = 0; i < SMS_STAT_N; i++) {
            static const char *names[SMS_STAT_N] = { "总数", "未读", "已读", "合并" };
            lv_obj_t *b = lv_obj_create(box);

            lv_obj_set_size(b, SMS_BLK_W, SMS_BLK_H);
            lv_obj_set_pos(b, 5 + i * 77, SMS_BLK_Y);
            lv_obj_set_style_bg_color(b, lv_color_make(30, 30, 30), 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

            lst_num[i] = mk_label(b, &lv_font_montserrat_24, 0xE6E6E6, 0, 0, 0, 0);
            lv_label_set_text(lst_num[i], "0");
            lv_obj_align(lst_num[i], LV_ALIGN_CENTER, 0, -12);

            lst_name[i] = mk_label(b, &lv_font_cjk_sms_16, 0x9A9A9A, 0, 0, 0, 0);
            lv_label_set_text(lst_name[i], names[i]);
            lv_obj_align(lst_name[i], LV_ALIGN_CENTER, 0, 22);

            lst_stat_box[i] = b;
        }
    }

    /* ---------- 详情页 ---------- */
    sms_detail_page = mk_page(main_screen);
    {
        lv_obj_t *box = mk_box(sms_detail_page);

        dt_num = mk_label(box, &lv_font_cjk_sms_16, 0xE6E6E6, 10, SMS_DT_HDR_Y, 150, 17);
        lv_label_set_text(dt_num, "-");

        /* 头部右侧：「第几条/共几条 未读 时间」都在这一行（省出高度给正文） */
        dt_head = mk_label(box, &lv_font_cjk_sms_16, 0xA0A0A0, 0, SMS_DT_HDR_Y, 0, 0);
        lv_label_set_text(dt_head, "");
        lv_obj_align(dt_head, LV_ALIGN_TOP_RIGHT, -10, SMS_DT_HDR_Y);

        /* 正文框：只是个"窗口"—— 不可滚、无滚动条、不吃手指（上下划留给换页），
         * 装不下的正文由 sms_scroll_tick() 自己往上滚（label 的 y 变负，被这里的
         * 尺寸裁切）。⚠️ 不能加 LV_OBJ_FLAG_OVERFLOW_VISIBLE，否则就不裁切了。
         *
         * 与头部留 SMS_DT_GAP 的间距、底部也留一点：挤在一起（尤其正文紧贴头部）
         * 看着很乱（用户反馈过）；内边距从 4 加到 5，文字不再贴着框线。 */
        dt_box = lv_obj_create(box);
        lv_obj_set_size(dt_box, 304, SMS_DT_BODY_H);
        lv_obj_set_pos(dt_box, 6, SMS_DT_HDR_Y + 17 + SMS_DT_GAP);
        lv_obj_set_style_bg_color(dt_box, lv_color_make(30, 30, 30), 0);
        lv_obj_set_style_radius(dt_box, 0, 0);
        lv_obj_set_style_border_width(dt_box, 0, 0);
        lv_obj_set_style_pad_all(dt_box, 5, 0);
        lv_obj_set_scrollbar_mode(dt_box, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(dt_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dt_box, LV_OBJ_FLAG_GESTURE_BUBBLE);

        /* 正文**不限高**（w=0 走 auto 分支）：让它按内容撑开，底部超出部分被框裁掉，
         * 再由定时器一点点上移露出后面的内容。 */
        dt_body = mk_label(dt_box, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 0, 0, 0);
        lv_obj_set_width(dt_body, 304 - 10);   /* = 框宽 - 左右内边距(5*2) */
        lv_label_set_long_mode(dt_body, LV_LABEL_LONG_WRAP);
        lv_label_set_text(dt_body, "");

        /* 长按 ⇒ 确认删除。三个对象都挂上：按在正文上命中的是 label、
         * 按在框上命中的是框、按在边上命中的是页。
         * 计时自己来（dt_press_cb + dt_lp_timer），不用 LVGL 的 LONG_PRESSED。 */
        lv_obj_add_event_cb(sms_detail_page, dt_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(sms_detail_page, dt_press_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(sms_detail_page, dt_press_cb, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_event_cb(dt_box, dt_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(dt_box, dt_press_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(dt_box, dt_press_cb, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_event_cb(dt_body, dt_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(dt_body, dt_press_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(dt_body, dt_press_cb, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_event_cb(dt_body, dt_press_cb, LV_EVENT_SCROLL_BEGIN, NULL);
        if (!dt_lp_timer) {
            dt_lp_timer = lv_timer_create(dt_lp_timer_cb, 50, NULL);
            lv_timer_pause(dt_lp_timer);
        }
    }

    /* ---------- 新短信弹窗（挂在**顶层**，盖在所有页之上） ----------
     * ⚠️ 必须用 lv_layer_top()：挂在 main_screen 上会被**后建的页面**盖住
     *    （create_smspage_pages 之后还会建 SPEED/INFO/Mobile 三页），
     *    切到那几页时弹窗就"看不见"了（踩过）。
     * ⭐ 排版与下面的删除确认框**完全一致**（用户 2026-10-07 要求）：
     *    320x140 放在 (0,30)、不留边也不留边框、标题 0xFF4500 在 (14,10)、
     *    正文左 14 / 宽 292、底部两个 160x46 的半宽按钮。 */
    sms_pop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(sms_pop, 320, 140);
    lv_obj_set_pos(sms_pop, 0, 30);
    lv_obj_set_style_bg_color(sms_pop, lv_color_make(26, 26, 26), 0);
    lv_obj_set_style_bg_opa(sms_pop, LV_OPA_COVER, 0);   /* 不透明，否则底下的页会透出来 */
    lv_obj_set_style_radius(sms_pop, 0, 0);
    lv_obj_set_style_border_width(sms_pop, 0, 0);
    lv_obj_set_style_pad_all(sms_pop, 0, 0);
    lv_obj_clear_flag(sms_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sms_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(sms_pop, pop_click_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t *t = mk_label(sms_pop, &lv_font_cjk_sms_16, 0xFF4500, 14, 10, 0, 0);

        lv_label_set_text(t, "新短信");

        /* 发件号码：与标题同一行、靠右（删不掉，删掉的只是"点击查看"那句提示） */
        pop_who = mk_label(sms_pop, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 10, 0, 0);
        lv_label_set_text(pop_who, "");
        lv_obj_align(pop_who, LV_ALIGN_TOP_RIGHT, -14, 10);

        /* 正文：与删除框那行说明**同一个起点**（x=14 / y=40 / 宽 292），
         * 高度给 46（两行 16px）—— 比删除框那行高是必须的，这里放的是短信正文。 */
        pop_text = mk_label(sms_pop, &lv_font_cjk_sms_16, 0xD0D0D0, 14, 40, 292, 46);
        lv_label_set_text(pop_text, "");

        /* 两个按钮：查看（青，主操作）/ 忽略（灰），各 160x46，**贴着左右下三边** */
        struct {
            const char *text;
            int         x;
            unsigned    bg;
            lv_event_cb_t cb;
        } btn[2] = {
            { "查看", 0,   0x1D5A66, pop_click_cb },
            { "忽略", 160, 0x3A3A3A, pop_ignore_cb },
        };

        for (int i = 0; i < 2; i++) {
            lv_obj_t *b = lv_obj_create(sms_pop);

            lv_obj_set_size(b, 160, 46);
            lv_obj_set_pos(b, btn[i].x, 94);        /* 94 + 46 = 140 ⇒ 贴底 */
            lv_obj_set_style_bg_color(b, lv_color_hex(btn[i].bg), 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(b, btn[i].cb, LV_EVENT_CLICKED, NULL);

            lv_obj_t *l = mk_label(b, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 0, 0, 0);

            lv_label_set_text(l, btn[i].text);
            lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        }
    }

    /* ---------- 删除确认弹窗（详情页长按弹出，同样挂**顶层**） ----------
     * 用户要求：**左右下都不留边、顶部对齐到标题栏**（铺满页面区那一整块）：
     * 面板 = 320x140 放在 (0,30)（与页面对象的几何完全一致），两个按钮各占半宽贴底。 */
    del_pop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(del_pop, 320, 140);
    lv_obj_set_pos(del_pop, 0, 30);
    lv_obj_set_style_bg_color(del_pop, lv_color_make(26, 26, 26), 0);
    lv_obj_set_style_bg_opa(del_pop, LV_OPA_COVER, 0);   /* 必须不透明，否则底下的页会透出来 */
    lv_obj_set_style_radius(del_pop, 0, 0);
    lv_obj_set_style_border_width(del_pop, 0, 0);   /* 不留边 ⇒ 也不要边框 */
    lv_obj_set_style_pad_all(del_pop, 0, 0);
    lv_obj_clear_flag(del_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(del_pop, LV_OBJ_FLAG_HIDDEN);
    {
        lv_obj_t *t = mk_label(del_pop, &lv_font_cjk_sms_16, 0xFF4500, 14, 10, 0, 0);

        lv_label_set_text(t, "删除这条短信？");

        del_text = mk_label(del_pop, &lv_font_cjk_sms_16, 0xD0D0D0, 14, 40, 292, 17);
        lv_label_set_text(del_text, "");

        /* 两个按钮：删除（红）/ 取消（灰），各 160x46，**贴着左右下三边** */
        struct {
            const char *text;
            int         x;
            unsigned    bg;
            lv_event_cb_t cb;
        } btn[2] = {
            { "删除", 0, 0x7A1F1F, del_ok_cb },
            { "取消", 160, 0x3A3A3A, del_cancel_cb },
        };

        for (int i = 0; i < 2; i++) {
            lv_obj_t *b = lv_obj_create(del_pop);

            lv_obj_set_size(b, 160, 46);
            lv_obj_set_pos(b, btn[i].x, 94);        /* 94 + 46 = 140 ⇒ 贴底 */
            lv_obj_set_style_bg_color(b, lv_color_hex(btn[i].bg), 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(b, btn[i].cb, LV_EVENT_CLICKED, NULL);

            lv_obj_t *l = mk_label(b, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 0, 0, 0);

            lv_label_set_text(l, btn[i].text);
            lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        }
    }

    if (sms_dbg) {
        lv_area_t a;

        lv_obj_update_layout(sms_list_page);
        lv_obj_update_layout(sms_detail_page);
#define DBG_OBJ(tag, o)                                                        \
        do {                                                                   \
            lv_obj_get_coords(o, &a);                                           \
            printf("[ui] %-8s %d,%d %dx%d\n", tag, a.x1, a.y1,                  \
                   lv_obj_get_width(o), lv_obj_get_height(o));                  \
        } while (0)

        DBG_OBJ("list-page", sms_list_page);
        DBG_OBJ("detail-pg", sms_detail_page);
        DBG_OBJ("dt_box", dt_box);
        DBG_OBJ("dt_num", dt_num);
        DBG_OBJ("dt_head", dt_head);
        DBG_OBJ("dt_body", dt_body);
        DBG_OBJ("stat0", lst_stat_box[0]);
        DBG_OBJ("num0", lst_num[0]);
        DBG_OBJ("name0", lst_name[0]);
        DBG_OBJ("stat3", lst_stat_box[3]);
        DBG_OBJ("popup", sms_pop);
        DBG_OBJ("del-pop", del_pop);
#undef DBG_OBJ
    }

    lv_timer_create(smspage_tick, 1000, NULL);
    lv_timer_create(sms_scroll_tick, SMS_SCROLL_MS, NULL);   /* 正文自动上滚 */

    /* ---------- 余额查询屏（SMS 竖页的最后一屏，用户 2026-10-07 要求） ----------
     * 版面：标题 / 卡类型 / 指令 / 状态行 / 整宽按钮，纵坐标与其它页同一套路。 */
    sms_query_page = mk_page(main_screen);
    {
        lv_obj_t *l;

        l = mk_label(sms_query_page, &lv_font_cjk_sms_16, 0xE6E6E6, 10, 6, 0, 0);
        lv_label_set_text(l, "余额查询");

        l = mk_label(sms_query_page, &lv_font_cjk_sms_16, 0x8A8A8A, 10, 34, 0, 0);
        lv_label_set_text(l, "卡类型");
        q_op_val = mk_label(sms_query_page, &lv_font_cjk_sms_16, 0xFFFFFF, 78, 34, 0, 0);
        lv_label_set_text(q_op_val, "--");

        l = mk_label(sms_query_page, &lv_font_cjk_sms_16, 0x8A8A8A, 10, 58, 0, 0);
        lv_label_set_text(l, "指令");
        q_cmd_val = mk_label(sms_query_page, &lv_font_cjk_sms_16, 0xFFFFFF, 78, 58, 0, 0);
        lv_label_set_text(q_cmd_val, "--");

        q_state = mk_label(sms_query_page, &lv_font_cjk_sms_16, 0x8A8A8A, 10, 82, 300, 18);
        lv_label_set_text(q_state, "点下面按钮发查询短信");

        q_btn = lv_obj_create(sms_query_page);
        lv_obj_set_size(q_btn, 304, 30);
        lv_obj_set_pos(q_btn, 8, 104);
        lv_obj_set_style_bg_color(q_btn, lv_color_hex(0x1D5A66), 0);
        lv_obj_set_style_bg_opa(q_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(q_btn, 0, 0);
        lv_obj_set_style_border_width(q_btn, 0, 0);
        lv_obj_set_style_pad_all(q_btn, 0, 0);
        lv_obj_clear_flag(q_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(q_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(q_btn, q_btn_cb, LV_EVENT_CLICKED, NULL);
        q_btn_lbl = mk_label(q_btn, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 0, 0, 0);
        lv_label_set_text(q_btn_lbl, "发送查询短信");
        lv_obj_align(q_btn_lbl, LV_ALIGN_CENTER, 0, 0);

        /* 发送前的二次确认框（挂顶层，几何 = 页面区 320x140 @(0,30)，默认隐藏） */
        q_pop = lv_obj_create(lv_layer_top());
        lv_obj_set_size(q_pop, 320, 140);
        lv_obj_set_pos(q_pop, 0, 30);
        lv_obj_set_style_bg_color(q_pop, lv_color_make(26, 26, 26), 0);
        lv_obj_set_style_bg_opa(q_pop, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(q_pop, 0, 0);
        lv_obj_set_style_border_width(q_pop, 0, 0);
        lv_obj_set_style_pad_all(q_pop, 0, 0);
        lv_obj_clear_flag(q_pop, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(q_pop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(q_pop, q_cancel_cb, LV_EVENT_CLICKED, NULL);   /* 点空白 = 取消 */

        q_pop_title = mk_label(q_pop, &lv_font_cjk_sms_16, 0xFFC24D, 14, 12, 0, 0);
        lv_label_set_text(q_pop_title, "发送余额查询？");
        q_pop_text = mk_label(q_pop, &lv_font_cjk_sms_16, 0xD0D0D0, 14, 46, 292, 40);
        lv_label_set_text(q_pop_text, "");

        q_pop_ok = lv_obj_create(q_pop);
        lv_obj_set_size(q_pop_ok, 160, 46);
        lv_obj_set_pos(q_pop_ok, 0, 94);
        lv_obj_set_style_bg_color(q_pop_ok, lv_color_hex(0x1D5A66), 0);
        lv_obj_set_style_bg_opa(q_pop_ok, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(q_pop_ok, 0, 0);
        lv_obj_set_style_border_width(q_pop_ok, 0, 0);
        lv_obj_set_style_pad_all(q_pop_ok, 0, 0);
        lv_obj_clear_flag(q_pop_ok, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(q_pop_ok, q_ok_cb, LV_EVENT_CLICKED, NULL);
        l = mk_label(q_pop_ok, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 0, 0, 0);
        lv_label_set_text(l, "确认");
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);

        {
            lv_obj_t *b = lv_obj_create(q_pop);

            lv_obj_set_size(b, 160, 46);
            lv_obj_set_pos(b, 160, 94);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x3A3A3A), 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(b, q_cancel_cb, LV_EVENT_CLICKED, NULL);
            l = mk_label(b, &lv_font_cjk_sms_16, 0xFFFFFF, 0, 0, 0, 0);
            lv_label_set_text(l, "取消");
            lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        }
    }

    /* 初始三页都先藏着（show_page() 会亮出该显示的那一个）。
     * ⚠️ 详情页尤其要藏：它列数随短信条数变，卡里没短信时 show_page() 的遍历
     *    看不到它，不先藏就会盖在别的页面上面（踩过：开机后主页被它盖成"空的"）。
     * 余额查询屏同理（它是竖页最后一屏，位置随条数变）。 */
    lv_obj_add_flag(sms_list_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sms_detail_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sms_query_page, LV_OBJ_FLAG_HIDDEN);
}
