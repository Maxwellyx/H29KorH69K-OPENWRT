/*
 * NET 页 —— 网络参数一览（原 NETIP 页，用户要求「修改成 NET，里面内容用块显示」）。
 *
 * 版式（页面 320x140，屏幕坐标从 y=30 起；这里的坐标 = 页面内容区坐标）：
 *
 *   y   5..30   一条**贯通整宽的块**（316x26），里面左 `4G LTE B5 CA 1CC`、右 `未聚合` / `已聚合`
 *   y  36..138  8 个参数块（2 列 x 4 行，每块 155x24，直角、深底、细边框）：
 *                 网络类型 / 频点     频段 / 载波聚合     PLMN / 服务小区     MCC/MNC / 邻区
 *
 * ⚠️ 上边（摘要条）特意留了 5px：页面顶边就是标题栏（`🏠 Mobile`）的下边，
 *    不留空会跟标题栏贴在一起（用户提醒过）。
 * ⚠️ 摘要要**包在一条块里**（用户要求“加一个一整条的块包在里面”），
 *    样式与下面的参数块完全一致（style_block()）。
 * ⚠️ 这里**不画运营商名**（用户要求：子页不要“中国电信”），也不放 IP 地址。
 *
 * 数据来源：**只有 wsdata 的快照**（也就是 modemd 推过来的 network 报文），
 * 屏幕自己不查 AT、不读本地文件 —— 与 Mobile 页同一套规矩。
 * 服务端字段带 omitempty ⇒ 没上报的项这里显示 `-`（不编数据）。
 *
 * ⚠️ `lv_obj_create()` 出来的页面**默认有 20px 内边距**（LVGL 主题给的），
 *    不清掉的话所有坐标都要减 20 才对得上屏幕（Mobile 页就栽过一次）。
 *    这里显式 `pad_all = 0`，所以下面写的坐标就是屏幕坐标（y 再加 30 那个偏移）。
 */

#include "../lvgl/lvgl.h"
#include "netparam_page.h"
#include <stdio.h>
#include <string.h>
#include "../utils/wsdata.h"

/* 只含本页用到的汉字（网络类型/频段/点/载波聚合/服务小区/邻区/已未）的 16px 子集 */
LV_FONT_DECLARE(lv_font_cjk_16);

lv_obj_t *netparam_page;

/* ---- 8 个参数块 ---- */
#define NP_KEYS 8

#define BLK_X0   2      /* 左列 */
#define BLK_X1   163    /* 右列（2 + 155 + 6 的间距） */
#define BLK_W    155
#define BLK_H    24
#define BLK_Y0   36     /* 摘要条占 5..31，这里再空 5px */
#define BLK_GAP  2

#define ROW_Y(i) (BLK_Y0 + (i) * (BLK_H + BLK_GAP))   /* 36 / 62 / 88 / 114，末行底 138 */

/* ---- 顶部摘要条（贯通整宽的一条块） ---- */
#define HD_X     2
#define HD_Y     5
#define HD_W     316    /* 320 - 左右各 2 */
#define HD_H     26

static lv_obj_t *blk_self[NP_KEYS];
static lv_obj_t *blk_key[NP_KEYS];
static lv_obj_t *blk_val[NP_KEYS];

static lv_obj_t *hd_bar;    /* 摘要条本体（一整条块） */
static lv_obj_t *hd_sum;    /* 条内左侧：制式 + 频段 + 载波聚合 CC 数 */
static lv_obj_t *hd_ca;     /* 条内右侧：已聚合 / 未聚合 */

/* 没上报 → "-"（`ca_text` 这类空串不能原样贴，会看着像"坏了"） */
static const char *od(const char *s)
{
    return (s && s[0]) ? s : "-";
}

/* 数字：0/负数一律当"未上报"（服务端 0 = 没有） */
static const char *odn(char *buf, size_t cap, int v)
{
    if (v > 0)
        snprintf(buf, cap, "%d", v);
    else
        snprintf(buf, cap, "-");
    return buf;
}

/* 块/摘要条的通用外观：深底 + 细边框 + 直角 + 无内边距（两处共用，样式不会走偏） */
static void style_block(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(o, 0, 0);                 /* 直角（与 Mobile 页一致） */
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_color(o, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_30, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

/* 一个参数块：深底 + 细边框 + 直角，左键右值 */
static void mk_blk(int idx, const char *key, bool key_cjk, int x, int y)
{
    lv_obj_t *b = lv_obj_create(netparam_page);

    lv_obj_set_size(b, BLK_W, BLK_H);
    style_block(b);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    blk_self[idx] = b;

    blk_key[idx] = lv_label_create(b);
    lv_label_set_text(blk_key[idx], key);
    lv_obj_set_style_text_font(blk_key[idx], key_cjk ? &lv_font_cjk_16 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(blk_key[idx], lv_color_hex(0xA0A0A0), 0);
    lv_obj_align(blk_key[idx], LV_ALIGN_LEFT_MID, 5, 0);

    blk_val[idx] = lv_label_create(b);
    lv_label_set_text(blk_val[idx], "-");
    lv_obj_set_style_text_font(blk_val[idx], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(blk_val[idx], lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(blk_val[idx], LV_ALIGN_RIGHT_MID, -5, 0);
}

/* 定时器：每 300ms 把快照刷到界面上（只读缓存，不查 AT） */
static void update_netparam_cb(lv_timer_t *t)
{
    ws_snapshot_t s;
    char buf[96];
    char num[16];

    (void)t;
    wsdata_snapshot(&s);

    /* ---- 摘要行：左 `4G LTE B3 CA 1CC`，右（贴最右）`未聚合` / `已聚合` ---- */
    buf[0] = '\0';
    if (s.rat_label[0] && strcmp(s.rat_label, "---") != 0 && s.rat[0])
        snprintf(buf, sizeof(buf), "%s %s", s.rat_label, s.rat);
    else
        snprintf(buf, sizeof(buf), "%s", s.rat[0] ? s.rat : "-");
    if (s.band_label[0])
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s", s.band_label);
    if (s.ca_cc > 0)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " CA %dCC", s.ca_cc);
    lv_label_set_text(hd_sum, buf);

    /* 右边那半：用户要求贴最右、且不要那个 `·` 分隔符；服务端没报 CA 就整块不显示 */
    lv_label_set_text(hd_ca, (s.ca_cc > 0) ? (s.ca_active ? "已聚合" : "未聚合") : "-");
    lv_obj_set_style_text_color(hd_ca,
        lv_color_hex(s.ca_active ? 0x90EE90 : 0xD0D0D0), 0); /* 真聚合了才亮绿 */

    /* ---- 8 个参数块（顺序 = 用户列的那张表，按行排） ---- */
    lv_label_set_text(blk_val[0], od(s.net_tech));                        /* 网络类型 */
    lv_label_set_text(blk_val[1], od(s.band_text[0] ? s.band_text : s.band_label)); /* 频段 */
    lv_label_set_text(blk_val[2], odn(num, sizeof(num), s.earfcn));       /* 频点 */
    lv_label_set_text(blk_val[3], od(s.ca_text));                         /* 载波聚合 */
    lv_obj_set_style_text_color(blk_val[3],
        lv_color_hex(s.ca_active ? 0x90EE90 : 0xFFFFFF), 0);              /* 真聚合了才亮绿 */
    lv_label_set_text(blk_val[4], od(s.plmn));                            /* PLMN */
    lv_label_set_text(blk_val[5], od(s.mccmnc));                          /* MCC/MNC */
    lv_label_set_text(blk_val[6], odn(num, sizeof(num), s.serving_count));/* 服务小区 */
    lv_label_set_text(blk_val[7], odn(num, sizeof(num), s.neigh_count));  /* 邻区 */
}

void create_netparam_page(lv_obj_t *main_screen)
{
    netparam_page = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(netparam_page, lv_color_black(), 0);
    lv_obj_set_size(netparam_page, 320, 140);
    lv_obj_set_style_radius(netparam_page, 0, 0);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);
    lv_obj_add_style(netparam_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(netparam_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(netparam_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(netparam_page, LV_OPA_COVER, 0);
    /* ⚠️ 关键：清掉主题给的 20px 内边距，否则下面的坐标全部要减 20 才对得上屏幕 */
    lv_obj_set_style_pad_all(netparam_page, 0, 0);

    /* ---- 摘要条：一整条块，横跨整个内容宽度；里面左摘要 + 右聚合状态 ---- */
    hd_bar = lv_obj_create(netparam_page);
    lv_obj_set_size(hd_bar, HD_W, HD_H);
    style_block(hd_bar);
    lv_obj_align(hd_bar, LV_ALIGN_TOP_LEFT, HD_X, HD_Y);

    hd_sum = lv_label_create(hd_bar);
    lv_obj_set_style_text_font(hd_sum, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(hd_sum, lv_color_hex(0xD0D0D0), 0);
    lv_label_set_text(hd_sum, "-");
    lv_obj_align(hd_sum, LV_ALIGN_LEFT_MID, 12, 0);

    hd_ca = lv_label_create(hd_bar);
    lv_obj_set_style_text_font(hd_ca, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(hd_ca, lv_color_hex(0xD0D0D0), 0);
    lv_label_set_text(hd_ca, "-");
    lv_obj_align(hd_ca, LV_ALIGN_RIGHT_MID, -12, 0);

    /* ---- 8 个参数块：左列 0,2,4,6 / 右列 1,3,5,7 ---- */
    mk_blk(0, "网络类型", true,  BLK_X0, ROW_Y(0));
    mk_blk(1, "频段",     true,  BLK_X1, ROW_Y(0));
    mk_blk(2, "频点",     true,  BLK_X0, ROW_Y(1));
    mk_blk(3, "载波聚合", true,  BLK_X1, ROW_Y(1));
    mk_blk(4, "PLMN",     false, BLK_X0, ROW_Y(2));
    mk_blk(5, "MCC/MNC",  false, BLK_X1, ROW_Y(2));
    mk_blk(6, "服务小区", true,  BLK_X0, ROW_Y(3));
    mk_blk(7, "邻区",     true,  BLK_X1, ROW_Y(3));

    lv_timer_create(update_netparam_cb, 300, NULL);

    /* 版式自检：TFT_UI_DEBUG=1 启动时把各对象的**实际屏幕坐标**打出来。
     * （页/容器会被主题 padding 影响，光看代码算不准 —— 本页已经清掉 pad，
     *   预期是 page 0,30 320x140、左列块 x=2、右列块 x=163、末行 y=128。） */
    if (getenv("TFT_UI_DEBUG")) {
        lv_area_t a;

        lv_obj_update_layout(netparam_page);
        lv_obj_update_layout(blk_self[0]);

#define DBG_OBJ(tag, o)                                                    \
        do {                                                               \
            lv_obj_get_coords(o, &a);                                       \
            printf("[ui] %-6s %d,%d %dx%d\n", tag, a.x1, a.y1,              \
                   lv_obj_get_width(o), lv_obj_get_height(o));              \
        } while (0)

        DBG_OBJ("page", netparam_page);
        DBG_OBJ("hd_bar", hd_bar);
        DBG_OBJ("hd_sum", hd_sum);
        DBG_OBJ("hd_ca", hd_ca);
        DBG_OBJ("blk0", blk_self[0]);
        DBG_OBJ("blk1", blk_self[1]);
        DBG_OBJ("blk7", blk_self[NP_KEYS - 1]);
#undef DBG_OBJ
    }
}
