#include "../lvgl/lvgl.h"
#include "home_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "../utils/wsdata.h"

/* 只含“中国电信/联通/移动/广电 + 信号…”那几十个字的中文字体（tools 里用 lv_font_conv 生成） */
LV_FONT_DECLARE(lv_font_cjk_28);

/* Mobile 首行（左：模组 logo 块 / 右：运营商+制式+信号）的几何。
 * 用户 2026-10-07："第一行左右 2 个块高度调高点，不然上面留着好多空余" ⇒ 80 → 94。 */
#define MOBILE_ROW_H       94
/* ⚠️ mobilebase_page **没有** pad_all(0)（跟别的页不一样）：主题给 20px 内边距，
 * 所以 TOP_LEFT 的 y 偏移是相对"内容区上沿 = 屏幕 y50"算的 ⇒ 写 -16 = 屏幕 y34。
 * （14 是用户 2026-10-07 定的："距离上面太靠近了，向下调 2px" —— 再小就贴到顶栏了） */
#define MOBILE_ROW_TOP_OFS (-16)

/* 信号四格：最高一根的高度（后面那几根比它矮，**底对齐**才是正常的信号图标） */
#define SIG_BAR_MAX_H      (12 + (SIG_BARS - 1) * 5)
/* 整组贴在**块的右上角**（用户 2026-10-07："信号角标靠右上角"）：
 * 最高那根距块顶留 7px（跟 80 高时代一模一样的位置），其余底对齐。 */
#define SIG_BAR_TOP_PAD    7
/* 注：徽标改用真 logo 图（下面那几张）之后，20px 那套中文子集（lv_font_cjk_20）就不再被引用了，
 * 源文件留在 src/utils/ 里备用（要回到“品牌色底 + 简称”的文字徽标时可以再拿回来）。 */

/*
 * 运营商 logo：由 web 端的那几张 SVG 官方矢量图转出来的 C 数组
 * （生成器：tools/svg2lvgl/index.js，读的是 modemserver 的 web/index.html）。
 */
LV_IMAGE_DECLARE(lv_img_op_telecom);
LV_IMAGE_DECLARE(lv_img_op_unicom);
LV_IMAGE_DECLARE(lv_img_op_cmcc);
LV_IMAGE_DECLARE(lv_img_op_cbn);
LV_IMAGE_DECLARE(lv_img_op_other);
// #include "../utils/gif_player.h"


lv_obj_t *mobilebase_page;
lv_obj_t *op_logo_img;         /* 左侧方块：运营商 logo */
lv_obj_t *modemtype_label;     /* 中区大字：运营商中文名 */
lv_obj_t *mobile_rat_label;    /* 中区右端小字：5G / 4G / 3G / 2G */

/* ---- 信号格：4 根柱，高度/颜色按信号等级（不显示文字） ---- */
#define SIG_BARS 4
static lv_obj_t *sig_bars[SIG_BARS];

/* ---- 底部三个状态块（不显示文字，只靠图标 + 绿/暗两态） ----
 *  块1 链路在网络   LV_SYMBOL_WIFI
 *  块2 拿到 IP 地址  LV_SYMBOL_HOME（本机地址）
 *  块3 数据可通      LV_SYMBOL_OK
 * 要换图标就改 create 里传的符号；要加小字标题（UP/IP/NET）也很容易。 */
static lv_obj_t *blk_link, *blk_ip, *blk_data;

static const unsigned BLK_BG     = 0x2A2A2A;   /* 块背景：固定不变（用户要求“背景不要修改”） */
static const unsigned BLK_ON_FG  = 0x90EE90;   /* 成立：**淡绿**文字 */
static const unsigned BLK_OFF_FG = 0x8A8A8A;   /* 不成立：灰文字 */

/* 当前显示的信号等级：-1 = 还没设过（0..4） */
static int sig_shown = -1;
/* 当前显示的运营商名（用来判断要不要重新贴制式标签的位置） */
static char op_name_shown[32];

/*
 * 运营商 → logo。下表顺序要与 op_index_of() 的判定顺序一致：
 *   0 电信 / 1 联通 / 2 移动 / 3 广电；认不出就用 SIM 卡占位图（lv_img_op_other）。
 */
static const lv_image_dsc_t *OP_LOGOS[] = {
    &lv_img_op_telecom,
    &lv_img_op_unicom,
    &lv_img_op_cmcc,
    &lv_img_op_cbn,
};

static const char *OP_KEYS[] = { "电信", "联通", "移动", "广电" };
#define OP_N ((int)(sizeof(OP_KEYS) / sizeof(OP_KEYS[0])))

/* 当前显示的 logo 下标：-2 = 还没设过，-1 = 未知运营商。
 * 缓存一下是为了不每 100ms 重设一次图片（会闪、也白耗CPU）。 */
static int op_shown = -2;

static int op_index_of(const char *op_name)
{
    if (!op_name || !*op_name)
        return -1;
    for (int i = 0; i < OP_N; i++) {
        if (strstr(op_name, OP_KEYS[i]))
            return i;
    }
    return -1;
}

/*
 * 信号等级 0..4（0 = 无信号）。
 * 优先用 RSRP（dBm）；没有 RSRP 时退回 RSSI（RSSI 一般比 RSRP 高约 20dB，阀值单独一套）。
 */
static int sig_level_of(const ws_snapshot_t *s)
{
    int dbm;

    if (!s->has_signal)
        return 0;

    if (s->rsrp != 0) {
        dbm = s->rsrp;
        if (dbm >= -85)  return 4;
        if (dbm >= -95)  return 3;
        if (dbm >= -105) return 2;
        return 1;
    }
    if (s->rssi != 0) {
        dbm = s->rssi;
        if (dbm >= -75)  return 4;
        if (dbm >= -85)  return 3;
        if (dbm >= -95)  return 2;
        return 1;
    }
    return 0;
}

/* 信号等级 → 颜色（绿 → 黄 → 橙 → 红） */
static unsigned sig_color_of(int lvl)
{
    switch (lvl) {
    case 4: return 0x3ADA7A;    /* 优 */
    case 3: return 0xE8D44D;    /* 良 */
    case 2: return 0xF0A030;    /* 中 */
    case 1: return 0xE0483A;    /* 差 */
    default: return 0x404040;   /* 无信号：全暗 */
    }
}

/* 状态块：
 * 背景**始终不变**（用户明确要求），只用**文字颜色**表示成立/不成立。 */
static void blk_set(lv_obj_t *b, bool on)
{
    lv_obj_t *sym;

    if (!b)
        return;
    sym = lv_obj_get_child(b, 0);
    if (sym)
        lv_obj_set_style_text_color(sym, lv_color_hex(on ? BLK_ON_FG : BLK_OFF_FG), 0);
}

/* 造一个状态块：直角矩形 + 中间一行**文字**（不再用图标：图标提示不清） */
static lv_obj_t *mk_block(lv_obj_t *parent, const char *text, int x)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_t *l;

    lv_obj_set_size(b, 98, 26);
    lv_obj_set_style_radius(b, 0, 0);          /* 直角，不要圆角 */
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(BLK_BG), 0);   /* 固定背景 */
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, x, 0);

    l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(BLK_OFF_FG), 0);
    lv_obj_center(l);
    return b;
}

static void update_info_cb(lv_timer_t *t)
{
    ws_snapshot_t s;

    wsdata_snapshot(&s);

    /* 底部：三个状态块（不再写 Up:0, IP:0, Connected:0 那种文字） */
    blk_set(blk_link, s.ip_up);
    blk_set(blk_ip, s.ip_hasip);
    blk_set(blk_data, s.ip_connected);

    /* 左侧方块：运营商 logo（换运营商时才重设，未知就用占位图） */
    {
        int idx = op_index_of(s.op_name);

        if (idx != op_shown) {
            op_shown = idx;
            lv_image_set_src(op_logo_img, idx >= 0 ? OP_LOGOS[idx] : &lv_img_op_other);
        }
    }

    /* 中区大字：运营商（中文）。用“名字内容”做缓存：
     * 变了才重设文字，并重新把制式标签贴在名字右边（4G/5G 要紧跟在名字后面）。 */
    {
        const char *nm = s.op_name[0] ? s.op_name : "------";

        if (strcmp(nm, op_name_shown) != 0) {
            snprintf(op_name_shown, sizeof(op_name_shown), "%s", nm);
            lv_label_set_text(modemtype_label, nm);
            lv_obj_align_to(mobile_rat_label, modemtype_label,
                            LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        }
    }

    /* 制式（5G / 4G / 3G / 2G）：紧跟在名字后面 */
    if (s.rat_label[0] && strcmp(s.rat_label, "---") != 0)
        lv_label_set_text(mobile_rat_label, s.rat_label);
    else
        lv_label_set_text(mobile_rat_label, "--");

    /* 信号：四格柱状图标（颜色随等级）；只有等级变了才重画 */
    {
        int lvl = sig_level_of(&s);

        if (lvl != sig_shown) {
            sig_shown = lvl;
            for (int i = 0; i < SIG_BARS; i++) {
                bool on = (i < lvl);

                /* 柱子高度仍按位置递增（没信号时也看得出是“格子”），颜色分挡暗 */
                lv_obj_set_style_bg_color(sig_bars[i],
                    lv_color_hex(on ? sig_color_of(lvl) : 0x555555), 0);
            }
        }
    }
}

void create_mobilebase_page(lv_obj_t *main_screen){
    mobilebase_page= lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(mobilebase_page, lv_color_black(), 0);
    lv_obj_set_size(mobilebase_page,320,140);//设置容器大小
    lv_obj_set_style_radius(mobilebase_page,0,0);//设置容器圆角为直角


    // 创建一个样式描述符，用于存储样式信息
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_border_width(&style, 0);  // 设置边框宽度为0，从而去除边框
    // 将样式应用到标签上
    lv_obj_add_style(mobilebase_page, &style, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(mobilebase_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(mobilebase_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(mobilebase_page, LV_OPA_COVER, 0);


    lv_obj_t *modem_container = lv_obj_create(mobilebase_page);
    /* 用户 2026-10-07："Mobile 第一行的左右 2 个块高度调高点，不然上面留好多空余"
     * ⇒ 80 → **94**，并且顶到页面上沿（页面区 y 30..170，实测 TOP_LEFT 偏移是相对
     *   那个 20px 主题内边距算的：-18 ⇒ 屏幕 y=32，刚好贴在顶栏下面）。
     * ⚠️ 页面没有 pad_all(0)（与别的页不同，这里是故意的）：偏移都按内容区（+20）算。 */
    lv_obj_set_size(modem_container, 80, MOBILE_ROW_H);
    lv_obj_set_style_bg_color(modem_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(modem_container, 0, 0);
    lv_obj_set_style_pad_all(modem_container, 0, 0);
    lv_obj_set_style_border_color(modem_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(modem_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(modem_container, 1, 0);
    /* 左边缘贴屏幕左边（内容区 x=20 ⇒ -20）、顶部留 2px */
    lv_obj_align(modem_container, LV_ALIGN_TOP_LEFT, -20, MOBILE_ROW_TOP_OFS);
    lv_obj_clear_flag(modem_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *time_container = lv_obj_create(mobilebase_page);
    lv_obj_set_size(time_container, 235, MOBILE_ROW_H);
    lv_obj_set_style_bg_color(time_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(time_container, 0, 0);
    lv_obj_set_style_pad_all(time_container, 0, 0);
    lv_obj_set_style_border_color(time_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(time_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(time_container, 1, 0);
    lv_obj_align(time_container, LV_ALIGN_TOP_LEFT, 65, MOBILE_ROW_TOP_OFS);
    lv_obj_clear_flag(time_container, LV_OBJ_FLAG_SCROLLABLE);

    // 注：原来这里还有一条绿色底带（dt_container），里面放的是“RSRP -xxx dBm”文字。
    // 信号改成图形后挪到了上面那一行右端，这条带子连着文字一起去掉了。
    // （要恢复的话：建一个 235x30 的容器，align BOTTOM_MID，再把信号格放回它里面。）





    lv_obj_t *ip_container = lv_obj_create(mobilebase_page);
    lv_obj_set_size(ip_container, 320, 35);
    lv_obj_set_style_bg_color(ip_container, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(ip_container, 0, 0);
    lv_obj_set_style_pad_all(ip_container, 0, 0);
    lv_obj_set_style_border_color(ip_container, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_border_opa(ip_container, LV_OPA_30, 0);
    lv_obj_set_style_border_width(ip_container, 1, 0);
    lv_obj_align(ip_container, LV_ALIGN_BOTTOM_MID, 0, 15);
    lv_obj_clear_flag(ip_container, LV_OBJ_FLAG_SCROLLABLE);


    op_logo_img = lv_image_create(modem_container);
    lv_image_set_src(op_logo_img, &lv_img_op_other);
    /*
     * 图标在块里上下左右居中。
     * ⚠️ 实测（TFT_UI_DEBUG=1 自检）：`mobilebase_page` 有 **20px 默认内边距**，
     * 所以块虽然写的是 `TOP_LEFT, -20, -5`，实际落在屏幕坐标 (0,45) 80x80 ——
     * **完全可见**。别再按“左侧露在屏外”去加正偏移，那会让图标偏右（踩过一次）。
     */
    lv_obj_align(op_logo_img, LV_ALIGN_CENTER, 0, 0);

    /* 底部：三个状态块（链路 / 本机地址 / 数据可通）—— 文字 + 橙/暗两态，直角 */
    blk_link = mk_block(ip_container, "UP",   7);
    blk_ip   = mk_block(ip_container, "IP", 111);
    blk_data = mk_block(ip_container, "DATA", 215);

    /* 中区大字：运营商（中文）—— 字号 28 是为了给右侧的“制式 + 信号格”留出位置 */
    modemtype_label = lv_label_create(time_container);
    lv_obj_set_style_text_font(modemtype_label, &lv_font_cjk_28, 0);
    lv_label_set_text(modemtype_label, "------");
    lv_obj_align(modemtype_label, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_text_color(modemtype_label, lv_color_hex(0xE6E6E6), 0);

    /* 制式（5G / 4G / 3G / 2G）：紧跟在运营商名字后面（位置在 update 里重新贴） */
    mobile_rat_label = lv_label_create(time_container);
    lv_obj_set_style_text_font(mobile_rat_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(mobile_rat_label, "--");
    lv_obj_align_to(mobile_rat_label, modemtype_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_text_color(mobile_rat_label, lv_color_hex(0xFF4500), 0);

    /* 信号：4 格柱状图标，放在这一行最右端（高度递增、底对齐） */
    for (int i = 0; i < SIG_BARS; i++) {
        sig_bars[i] = lv_obj_create(time_container);
        lv_obj_set_size(sig_bars[i], 10, 12 + i * 5);      /* 12 / 17 / 22 / 27 */
        lv_obj_set_style_radius(sig_bars[i], 0, 0);   /* 跟块一样走直角 */
        lv_obj_set_style_border_width(sig_bars[i], 0, 0);
        lv_obj_set_style_pad_all(sig_bars[i], 0, 0);
        lv_obj_set_style_bg_opa(sig_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(sig_bars[i], lv_color_hex(0x555555), 0);
        lv_obj_clear_flag(sig_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        /* 整组：**右上角**，底对齐（短的在左、高的在右，信号图标的正常样子）。
         * ⚠️ 别用 RIGHT_MID 让每根各自居中：那样四根的中心线对齐、最高的上下都撑出来
         *    ⇒ 图标看着是**反的**（用户 2026-10-07 一眼看出来的）。
         * 底边位置 = 块高 - 顶边距 - 最高一根的高度。 */
        lv_obj_align(sig_bars[i], LV_ALIGN_BOTTOM_RIGHT,
                     -(12 + (SIG_BARS - 1 - i) * 14),
                     -(MOBILE_ROW_H - SIG_BAR_TOP_PAD - SIG_BAR_MAX_H));
    }
    lv_timer_create(update_info_cb, 100, NULL);

    /* 版式自检：TFT_UI_DEBUG=1 启动时把各对象的**实际屏幕坐标**打出来。
     * （页/容器可能被主题 padding 或滚动偏移影响，光看代码算不准。） */
    if (getenv("TFT_UI_DEBUG")) {
        lv_area_t a;

        lv_obj_update_layout(mobilebase_page);
        lv_obj_update_layout(modem_container);

#define DBG_OBJ(tag, o)                                                    \
        do {                                                               \
            lv_obj_get_coords(o, &a);                                       \
            printf("[ui] %-6s %d,%d %dx%d\n", tag, a.x1, a.y1,              \
                   lv_obj_get_width(o), lv_obj_get_height(o));              \
        } while (0)

        DBG_OBJ("page", mobilebase_page);
        DBG_OBJ("modem", modem_container);
        DBG_OBJ("logo", op_logo_img);
        DBG_OBJ("time", time_container);
        DBG_OBJ("name", modemtype_label);
        DBG_OBJ("rat", mobile_rat_label);
        DBG_OBJ("bar0", sig_bars[0]);
        DBG_OBJ("bar3", sig_bars[SIG_BARS - 1]);
        DBG_OBJ("ip", ip_container);
        DBG_OBJ("blk0", blk_link);
        DBG_OBJ("blk2", blk_data);
#undef DBG_OBJ
    }
}


//end Pages home