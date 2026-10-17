#include "../lvgl/lvgl.h"
#include "brightness_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>

// 背光控制节点（⚠️ 本机驱动的 max_brightness = 9，不是 255，见 get_max_brightness）
#define BACKLIGHT_DIR      "/sys/class/backlight/pinctrl:backlight"
#define BACKLIGHT_PATH     BACKLIGHT_DIR "/brightness"
#define BACKLIGHT_MAX_PATH BACKLIGHT_DIR "/max_brightness"
/* 息屏用的总开关节点：写 1 = 关屏、0 = 开屏（本机驱动实测接受，读得回）。
 * 光把 brightness 写 0 也行，但 bl_power 更彻底（有些驱动还会停掉面板）。 */
#define BACKLIGHT_POWER_PATH BACKLIGHT_DIR "/bl_power"

#define MIN_BRIGHTNESS 0      // 最小亮度值（扇区最小值）

// 设置亮度不能把背光真的关掉：黑屏之后在触摸屏上就再也点不回来了（除非重启服务）
#define MIN_VISIBLE_HW_BRIGHTNESS 1

/* 开机时硬件读不到 / 读到 0 就恢复到这一档（否则屏会留在全黑，用户再也调不回来）。
 * ⚠️ 必须取列表里存在的档位（5/25/50/75/100）—— 以前是 60：那一档不在列表里，
 * 于是 SYSTEM 亮度页一个按钮都不高亮，看起来就像"选中的亮度丢了"（用户报的）。 */
#define DEFAULT_BRIGHTNESS_PERCENT 50

/* 记住用户选的那一档：息屏时硬件亮度就是 0，进程重启后光看硬件判不出"原来是多少"，
 * 所以落一个小文件。⚠️ 放 /etc（/tmp 会被重启清掉），只在档位真变了才写。 */
#define BRIGHTNESS_STATE_PATH "/etc/tftdisplay.brightness"
int  brightness_saved_percent(void);
void brightness_save_percent(int percentage);
int  brightness_saved_or(int fallback);

// 亮度级别定义（7级映射到0-255范围）
#define MIN_BRIGHTNESS_LEVEL 0
#define MAX_BRIGHTNESS_LEVEL 6
#define BRIGHTNESS_LEVELS_COUNT 7

// 百分比范围（保持0-100，用于UI显示）
#define MIN_PERCENTAGE 0
#define MAX_PERCENTAGE 100

// 前向声明（下面几个函数互相调用）
int  get_max_brightness(void);
int  get_current_brightness(void);
void set_brightness(int value);
int  percentage_to_hw_brightness(int percentage);
int  hw_brightness_to_percentage(int hw_brightness);
void apply_brightness(int percentage);          /* 定义在下面（息屏唤醒要用它恢复档位） */

// ========================== 数值转换（百分比 <-> 节点原值） ==========================
/**
 * 亮度级别 -> 百分比（7级对应0-100%）
 */
int brightness_level_to_percentage(int level) {
    if (level < MIN_BRIGHTNESS_LEVEL) level = MIN_BRIGHTNESS_LEVEL;
    if (level > MAX_BRIGHTNESS_LEVEL) level = MAX_BRIGHTNESS_LEVEL;
    
    // 整数除法向上取整，确保级别6对应100%
    int percentage = (level * MAX_PERCENTAGE + MAX_BRIGHTNESS_LEVEL) / MAX_BRIGHTNESS_LEVEL;
    return percentage;
}

/**
 * 百分比 -> 亮度级别（100%对应级别6）
 */
int percentage_to_brightness_level(int percentage) {
    if (percentage < MIN_PERCENTAGE) percentage = MIN_PERCENTAGE;
    if (percentage > MAX_PERCENTAGE) percentage = MAX_PERCENTAGE;
    
    // 四舍五入映射，确保百分比均匀对应级别
    int level = (percentage * MAX_BRIGHTNESS_LEVEL + 50) / 100;
    return level;
}

/**
 * 百分比 -> PWM值（100%对应50000，保持PWM范围不变）
 */
int percentage_to_pwm_value(int percentage) {
    if (percentage < MIN_PERCENTAGE) percentage = MIN_PERCENTAGE;
    if (percentage > MAX_PERCENTAGE) percentage = MAX_PERCENTAGE;
    
    int pwm_value = (percentage * 50000) / 100;
    return pwm_value;
}

/**
 * 亮度级别 -> PWM值（级别6对应50000）
 */
int brightness_level_to_pwm_value(int level) {
    if (level < MIN_BRIGHTNESS_LEVEL) level = MIN_BRIGHTNESS_LEVEL;
    if (level > MAX_BRIGHTNESS_LEVEL) level = MAX_BRIGHTNESS_LEVEL;
    
    int pwm_value = (level * 50000) / MAX_BRIGHTNESS_LEVEL;
    return pwm_value;
}

/**
 * 百分比 -> 硬件亮度值（满量程从节点读，本机 = 9）
 */
int percentage_to_hw_brightness(int percentage) {
    int max = get_max_brightness();
    int hw_brightness;

    if (percentage < MIN_PERCENTAGE) percentage = MIN_PERCENTAGE;
    if (percentage > MAX_PERCENTAGE) percentage = MAX_PERCENTAGE;

    hw_brightness = (percentage * max + 50) / 100;   // 四舍五入
    if (percentage > 0 && hw_brightness < 1) hw_brightness = 1;  // >0% 至少亮一级
    if (hw_brightness > max) hw_brightness = max;
    return hw_brightness;
}

/**
 * 硬件亮度值 -> 百分比（0-100）
 */
int hw_brightness_to_percentage(int hw_brightness) {
    int max = get_max_brightness();

    if (hw_brightness < MIN_BRIGHTNESS) hw_brightness = MIN_BRIGHTNESS;
    if (hw_brightness > max) hw_brightness = max;
    if (max <= 0) return 0;
    return (hw_brightness * MAX_PERCENTAGE + max / 2) / max;
}

// ========================== 硬件亮度交互：适配0-255范围 ==========================
/**
 * 获取最大硬件亮度（**从节点读**，本机是 9；以前写死 255 导致越界写被驱动夹掉）
 */
int get_max_brightness(void) {
    static int cached = 0;

    if (cached <= 0) {
        int v = 0;
        FILE *fp = fopen(BACKLIGHT_MAX_PATH, "r");
        if (fp) {
            if (fscanf(fp, "%d", &v) != 1) v = 0;
            fclose(fp);
        }
        cached = (v > 0) ? v : 255;   // 读不到就退回 255
    }
    return cached;
}

/**
 * 读取当前硬件亮度（节点原值，0..max）
 */
int get_current_brightness(void) {
    int max = get_max_brightness();
    int brightness = max * DEFAULT_BRIGHTNESS_PERCENT / 100;   // 读不到就按默认档估
    FILE *fp = fopen(BACKLIGHT_PATH, "r");

    if (fp) {
        if (fscanf(fp, "%d", &brightness) != 1)
            brightness = max * DEFAULT_BRIGHTNESS_PERCENT / 100;
        fclose(fp);
    }

    if (brightness < MIN_BRIGHTNESS) brightness = MIN_BRIGHTNESS;
    if (brightness > max) brightness = max;
    return brightness;
}

/**
 * 设置硬件亮度（写入节点；越界值按节点满量程夹紧）
 */
void set_brightness(int value) {
    int max = get_max_brightness();

    if (value < MIN_BRIGHTNESS) value = MIN_BRIGHTNESS;
    if (value > max) value = max;

    FILE *fp = fopen(BACKLIGHT_PATH, "w");
    if (!fp) {
        perror("Failed to open backlight file");  // 增加错误日志，便于调试
        return;
    }
    
    fprintf(fp, "%d", value);
    fclose(fp);
}

/* ====================== 息屏 / 亮屏（用户 2026-10-07 要求） ======================
 *
 * 「不操作 3 分钟自动息屏，点屏幕自动亮屏」：无操作由 main.c 的触摸层计时，
 * 这里只管把屏真的关掉 / 恢复。
 *
 * ⚠️ 两个必须注意的点：
 *   ① **不能把亮度当成 0 就完事**：唤醒要恢复"息屏前那一档"（current_brightness_percent，
 *      亮度页那排挡位按钮点过之后就是它）；
 *   ② 关机时写 `brightness=0` + `bl_power=1`，开机时**先** `bl_power=0` **再**恢复亮度
 *      （顺序反了某些驱动会把亮度当 0 忽略）。
 */
static bool screen_asleep;

/* 当前亮度（存百分比）：亮度页那排挡位点过之后就是它，
 * 也是**息屏前那一档**——亮屏时要恢复成这个值。 */
int current_brightness_percent = 60;

static void write_backlight_power(int on) {
    FILE *fp = fopen(BACKLIGHT_POWER_PATH, "w");

    if (!fp)
        return;                     /* 没有这个节点就算了：亮度 0 也能把屏关黑 */
    fprintf(fp, "%d", on ? 1 : 0);
    fclose(fp);
}

bool brightness_screen_is_sleeping(void) {
    return screen_asleep;
}

/* ---- 档位的持久化（见 DEFAULT_BRIGHTNESS_PERCENT 上面的说明） ---- */
int brightness_saved_percent(void) {
    FILE *fp = fopen(BRIGHTNESS_STATE_PATH, "r");
    int v = 0;

    if (!fp)
        return 0;                       /* 没存过（或第一次开机）= 0 */
    if (fscanf(fp, "%d", &v) != 1)
        v = 0;
    fclose(fp);
    if (v < MIN_PERCENTAGE || v > MAX_PERCENTAGE)
        return 0;
    return v;
}

void brightness_save_percent(int percentage) {
    static int last = -1;
    FILE *fp;

    if (percentage == last)
        return;                         /* 没变就不写（少动 flash） */
    fp = fopen(BRIGHTNESS_STATE_PATH, "w");
    if (!fp)
        return;
    fprintf(fp, "%d", percentage);
    fclose(fp);
    last = percentage;
}

int brightness_saved_or(int fallback) {
    int v = brightness_saved_percent();

    return v > 0 ? v : fallback;
}

/* ⚠️ 日志里带 `t=%us`（LVGL 开机秒数）：现场只有 logread 可看（背光的
 * brightness/bl_power 节点在这块驱动上**写进去读不回来**，外部测不出亮没亮）⇒
 * “息屏 / 亮屏 / 保留多久”只能靠日志的时序自证。 */
void brightness_screen_sleep(void) {
    if (screen_asleep)
        return;
    screen_asleep = true;
    set_brightness(0);
    write_backlight_power(1);
    printf("[screen] 息屏（无操作）t=%us\n", (unsigned)(lv_tick_get() / 1000u));
    fflush(stdout);
}

void brightness_screen_wake(void) {
    brightness_screen_wake_ex("触摸");
}

/* 为什么单独开一个带名字的版本：息屏期间不只有触摸会把屏叫亮（新短信也会，用户 2026-10-08
 * 要求），日志里必须看得出是哪种原因亮的，不然“背光自己亮了/没亮”根本没法排查。 */
void brightness_screen_wake_ex(const char *why) {
    if (!screen_asleep)
        return;
    screen_asleep = false;
    write_backlight_power(0);
    apply_brightness(current_brightness_percent);   /* 恢复到息屏前那一档（内部会夹到至少 1 级，不会留在全黑） */
    printf("[screen] 亮屏（%s）t=%us\n", (why && why[0]) ? why : "?",
           (unsigned)(lv_tick_get() / 1000u));
    fflush(stdout);
}

// ========================== LVGL UI组件定义 ==========================
lv_obj_t *Brightness_page;
static lv_obj_t *brightness_slider;       // 旧的滑块：保留变量以兼容编译（已不再创建）
static lv_obj_t *brightness_value_label;  // 当前亮度显示（百分比）
static lv_obj_t *back_button;             // 返回按钮（原代码未实现，保留定义）

/* 挡位按钮：5 / 25 / 50 / 75 / 100（用户要求把滑块换成一排按钮 ——
 * 滑块的左右拖动会和\"左右划换页\"的手势打架）。 */
#define BRIGHTNESS_LEVELS 5
static const int  brightness_levels[BRIGHTNESS_LEVELS] = { 5, 25, 50, 75, 100 };
static lv_obj_t  *level_btn[BRIGHTNESS_LEVELS];
static lv_obj_t  *level_btn_label[BRIGHTNESS_LEVELS];

/* ---- 二次确认弹窗（用户 2026-10-07："修改就弹出提示确认"）----
 * 点挡位按钮**不直接生效**，先弹顶层确认框（跟 lan_page.c 一个做法）；
 * ⚠️ 弹窗里有中文，必须用全字库字体（本页其它文字都是 montserrat，没有 CJK）。 */
LV_FONT_DECLARE(lv_font_cjk_sms_16);
#define BRI_FONT (&lv_font_cjk_sms_16)

static lv_obj_t *conf_pop, *conf_title, *conf_text, *conf_ok;
static int       conf_pct;               /* 待确认的挡位百分比（-1 = 没待确认） */

static void conf_open(int pct);

// ========================== 亮度预设：百分比对应0-255硬件值 ==========================
#define BRIGHTNESS_LOW_PERCENT     10    // 10% -> 25（255*10%≈25）
#define BRIGHTNESS_MEDIUM_PERCENT  50    // 50% -> 127（255*50%≈127）
#define BRIGHTNESS_HIGH_PERCENT    80    // 80% -> 204（255*80%≈204）
#define BRIGHTNESS_FULL_PERCENT    100   // 100% -> 255

// （current_brightness_percent 的定义已经挪到上面「息屏 / 亮屏」那一段之前 —— 那边要用它）

// ========================== 亮度应用逻辑：UI百分比 -> 硬件0-255 ==========================
/**
 * 应用亮度值（UI百分比转换为硬件0-255值）
 */
void apply_brightness(int percentage) {
    // 1. 限制百分比在0-100%
    if (percentage < MIN_PERCENTAGE) percentage = MIN_PERCENTAGE;
    if (percentage > MAX_PERCENTAGE) percentage = MAX_PERCENTAGE;
    
    // 2. 百分比转换为硬件值（最暗也只给一级，见 MIN_VISIBLE_HW_BRIGHTNESS）
    int hw_brightness = percentage_to_hw_brightness(percentage);
    if (hw_brightness < MIN_VISIBLE_HW_BRIGHTNESS)
        hw_brightness = MIN_VISIBLE_HW_BRIGHTNESS;
    
    // 3. 写入硬件节点
    set_brightness(hw_brightness);
    printf("Setting brightness: %d%% -> Hardware value: %d/%d\n",
           percentage, hw_brightness, get_max_brightness());
    
    // 4. 更新UI显示
    char buf[16];
    sprintf(buf, "%d%%", percentage);
    lv_label_set_text(brightness_value_label, buf);
    
    // 5. 更新当前亮度记录
    current_brightness_percent = percentage;
    brightness_save_percent(percentage);      /* 落盘：息屏/重启后还能恢复这一档 */
}

// ========================== LVGL事件处理函数 ==========================
/**
 * 滑块事件：拖动时更新亮度（滑块已不再创建，保留函数以兼容旧代码引用）
 */
static void slider_event_handler(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int slider_value = lv_slider_get_value(slider);  // 滑块范围0-100（百分比）
    apply_brightness(slider_value);
}

/* 把五个挡位按钮的\"选中态\"刷一遍：等于当前亮度的那个高亮成青色，其余灰底。 */
static void refresh_level_ui(void)
{
    for (int i = 0; i < BRIGHTNESS_LEVELS; i++) {
        bool on = (brightness_levels[i] == current_brightness_percent);

        lv_obj_set_style_bg_color(level_btn[i],
                                  lv_color_hex(on ? 0x00A0B0 : 0x2A2A2A), 0);
        lv_obj_set_style_text_color(level_btn_label[i],
                                    lv_color_hex(on ? 0xFFFFFF : 0xA0A0A0), 0);
    }
}

/* 挡位按钮：点一下**先弹确认**（不当场改），确认了才 apply_brightness */
static void level_btn_event_cb(lv_event_t *e)
{
    int level = (int)(intptr_t)lv_event_get_user_data(e);

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    conf_open(level);
}

/* ========================== 二次确认弹窗 ========================== */

static lv_obj_t *mk_text(lv_obj_t *parent, const char *text, uint32_t color,
                         int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, BRI_FONT, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    return l;
}

/* 32px 高的块按钮（必须带 GESTURE_BUBBLE，否则划不动页） */
static lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int w, int h,
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

    lv_obj_t *l = mk_text(b, text, 0xFFFFFF, 0, 0, 0);

    lv_obj_center(l);
    return b;
}

static void conf_hide(void)
{
    conf_pct = -1;
    lv_obj_add_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

/* "亮度调到 50%？" */
static void conf_open(int pct)
{
    char t[32], b[64];

    snprintf(t, sizeof(t), "亮度调到 %d%%？", pct);
    snprintf(b, sizeof(b), "取消则保持现在的 %d%%", current_brightness_percent);

    conf_pct = pct;
    lv_label_set_text(conf_title, t);
    lv_label_set_text(conf_text, b);
    lv_obj_move_foreground(conf_pop);
    lv_obj_clear_flag(conf_pop, LV_OBJ_FLAG_HIDDEN);
}

static void conf_ok_cb(lv_event_t *e)
{
    int pct;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    pct = conf_pct;
    conf_hide();                            /* 先收框再真改 */
    if (pct >= 0) {
        apply_brightness(pct);
        refresh_level_ui();
    }
}

static void conf_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    conf_hide();
    refresh_level_ui();
}

/* 弹窗挂着时若页面被划走了（顶栏还能滑动）⇒ 收掉，别留在别的页上 */
static void conf_guard_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (conf_pop && !lv_obj_has_flag(conf_pop, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_is_visible(Brightness_page))
        conf_hide();
}

// ========================== LVGL UI创建：适配百分比显示 ==========================
/**
 * 创建亮度设置页面
 */
void create_brightness_page(lv_obj_t *main_screen) {
    // 1. 创建页面容器
    Brightness_page = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(Brightness_page, lv_color_black(), 0);
    lv_obj_set_size(Brightness_page, 320, 140);  // 保持原尺寸
    lv_obj_set_style_radius(Brightness_page, 0, 0);
    
    // 清除默认边框和滚动
    static lv_style_t style_page;
    lv_style_init(&style_page);
    lv_style_set_border_width(&style_page, 0);
    lv_obj_add_style(Brightness_page, &style_page, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(Brightness_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(Brightness_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(Brightness_page, LV_OPA_COVER, 0);
    /* ⚠️ 必须清掉主题默认内边距（20px）：不清的话下面那排按钮会被整体往右下推 20px，
     * 末个按钮还会被屏幕右边缘裁掉 —— "按钮不对齐、很难看"就是这个（其它页都踩过）。 */
    lv_obj_set_style_pad_all(Brightness_page, 0, 0);
    lv_obj_move_foreground(Brightness_page);

    // 2. 创建亮度值显示标签（显示百分比）
    brightness_value_label = lv_label_create(Brightness_page);
    lv_obj_align(brightness_value_label, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_text_font(brightness_value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(brightness_value_label, lv_color_hex3(0xbbb), 0);

    /* 用户 2026-10-07 要求：页面上写个标签说明这屏是什么功能。
     * 放左上角（灰色小字），跟中间的百分比、下面那排挡位都不打架。 */
    {
        lv_obj_t *t = lv_label_create(Brightness_page);

        lv_label_set_text(t, "屏幕亮度");
        lv_obj_set_style_text_font(t, BRI_FONT, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_pos(t, 10, 14);
    }

    // 3. 一排挡位按钮：5 / 25 / 50 / 75 / 100
    //    （滑块换掉的原因：滑块的左右拖动会和屏幕\"左右划换页\"的手势打架，用户要求）
    for (int i = 0; i < BRIGHTNESS_LEVELS; i++) {
        char txt[8];

        lv_obj_t *b = lv_obj_create(Brightness_page);
        lv_obj_set_size(b, 58, 46);
        /* 用户 2026-10-07：整排**往下移 16px**（原来 62，贴着上面的百分比标签太挤） */
        lv_obj_set_pos(b, 7 + i * 62, 78);          // 7,69,131,193,255 → 末个右边 313（两边各留 7px）
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        /* ⚠️ 手势泡泡：在按钮上左右划也要能换页（按钮不参与滚动，不泡泡会被吃掉） */
        lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(b, level_btn_event_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)brightness_levels[i]);

        snprintf(txt, sizeof(txt), "%d%%", brightness_levels[i]);
        level_btn_label[i] = lv_label_create(b);
        lv_label_set_text(level_btn_label[i], txt);
        lv_obj_set_style_text_font(level_btn_label[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(level_btn_label[i], lv_color_hex(0xA0A0A0), 0);
        lv_obj_center(level_btn_label[i]);
        level_btn[i] = b;
    }

    // 初始化：读硬件当前亮度同步到 UI。
    // ⚠️ 细节说明：
    //   ① 读到 0 **不能**无条件回写 0（那是黑屏，用户在黑屏上再也调不回来）；
    //   ② 但也不能"一律回默认档" —— 息屏时硬件就是 0，一重启就把用户选的档丢掉
    //      （用户报的"点屏亮屏后亮度不是原来的、SYSTEM 里没选中项"就是这个）：
    //      所以先看小文件里存的档位，再决定恢复成多少；
    //   ③ 顺便把 bl_power 复位成 0（开屏）—— 否则"息屏状态下来一次重启"会一直黑着。
    int hw_current = get_current_brightness();
    bool need_apply = false;

    write_backlight_power(0);

    if (hw_current > 0) {
        current_brightness_percent = hw_brightness_to_percentage(hw_current);
        need_apply = (percentage_to_hw_brightness(current_brightness_percent) != hw_current);
    } else {
        int saved = brightness_saved_or(DEFAULT_BRIGHTNESS_PERCENT);

        current_brightness_percent = saved;
        need_apply = true;
        printf("backlight was 0, restoring to %d%%%s\n", current_brightness_percent,
               brightness_saved_percent() > 0 ? " (saved)" : " (default)");
    }

    if (need_apply) {
        apply_brightness(current_brightness_percent);   /* 内部会记档位 + 刷标签 */
    } else {
        char buf[16];

        snprintf(buf, sizeof(buf), "%d%%", current_brightness_percent);
        lv_label_set_text(brightness_value_label, buf);
        brightness_save_percent(current_brightness_percent);
    }
    refresh_level_ui();     /* 当前亮度落在哪个挡位就高亮哪个 */

    /* ---------- 二次确认弹窗（点挡位先确认，防误触） ----------
     * 几何跟页面对象完全对齐（320x140 @0,30），不透明、无边框，默认 HIDDEN；
     * 两个按钮各半宽贴底（94+46=140）：左确认 / 右取消；点面板空白 = 取消。 */
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

    conf_title = mk_text(conf_pop, "确认？", 0xFFC24D, 14, 12, 0);
    conf_text  = mk_text(conf_pop, "", 0xD0D0D0, 14, 46, 292);
    conf_ok    = mk_button(conf_pop, 0, 94, 160, 46, 0x1D5A66, conf_ok_cb, "确认");
    mk_button(conf_pop, 160, 94, 160, 46, 0x3A3A3A, conf_cancel_cb, "取消");

    lv_timer_create(conf_guard_cb, 1000, NULL);

    /* 调试钩子：TFT_BRI_CONFTEST=1 ⇒ 开屏就弹确认框（拍屏核对排版用） */
    if (getenv("TFT_BRI_CONFTEST"))
        conf_open(brightness_levels[2]);     /* 50% */

    /* ⚠️ 这里原来有一句 lv_scr_load(Brightness_page)（早期它是独立屏幕留下的）：
     * 本页现在是 SYSTEM 的一个横屏，再 load 会把整个屏幕换掉 ⇒ 已删。 */
}

// （可选）返回按钮事件处理：若启用返回按钮，需实现此函数
// static void back_btn_event_handler(lv_event_t *e) {
//     // 此处添加返回主屏幕的逻辑，例如：
//     // 1. 销毁当前亮度页（可选，避免内存泄漏）
//     // lv_obj_del(Brightness_page);
//     // 2. 加载主屏幕UI（需调用主屏幕创建函数，如create_main_screen()）
//     // create_main_screen(lv_scr_act());
// }
