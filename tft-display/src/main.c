#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/display/fb/lv_linux_fbdev.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/time.h>
#include "utils/GpioHelper.h"
#include "utils/tool.h"
#include "utils/wsclient.h"
#include "utils/wsdata.h"
#include "utils/battery.h"   /* 右上角电量：本地 ADC 读电池分压，不走服务端/模组 */
#include "version.h"          /* 开机页/日志/-version 用的版本号（打包时注入，见文件头） */
#include "utils/touchlogic.h"  /* 「松手了没」的判定（纯函数 + 宿主机单测，见该文件） */

#include "pages/home_page.h"
#include "pages/wifi_page.h"
#include "pages/osinfo_page.h"
#include "pages/syschart_page.h"
#include "pages/netinfo_page.h"
#include "pages/speedchart_page.h"
/* 原来的 ip_page（NETIP：IP + MAC）已被 netparam_page（NET：网络参数块）取代，
 * 源文件留在 pages/ 里备用 —— 没被引用就会被 --gc-sections 丢掉，不占空间。 */
#include "pages/netparam_page.h"
/* 新的 SMS 页（列表 + 逐条阅读 + 新短信弹窗）走 modemd 的 sms 推送。
 * ⚠️ src/pages/sms_page.c 是早期写死的假数据 demo，从没被创建过，不要混用。 */
#include "pages/smspage.h"
#include "pages/sysinfo_page.h"   /* SYSTEM 概览（WIFI / 亮度 收在它的左右划里） */
#include "pages/brightness_page.h"
#include "pages/idle_page.h"            /* SYSTEM 第五屏：息屏配置（1/3/5 分钟、不息屏） */
#include "pages/lan_page.h"         /* SYSTEM 的第三屏：LAN 配置（网口 LAN⇄WAN / 恢复原始配置） */
#include "pages/mobilebase_page.h"
#include "pages/lunar_page.h"        /* TIME 的第二个横屏：农历 */
#include "pages/netmode_page.h"       /* Mobile 的第三个横屏：组网模式偏好 4G/5G/4G+5G */

#include "logo_gif.h"
#include <linux/spi/spi.h>



// 屏幕与触摸参数
int TOUCH_RAW_WIDTH = 172;
int TOUCH_RAW_HEIGHT = 320;
#define SCREEN_WIDTH     320
#define SCREEN_HEIGHT    172

// 坐标映射模式
#define MAPPING_DIRECT     0
#define MAPPING_SWAP       1
#define MAPPING_ROT180     2
#define MAPPING_ROT90      3
#define MAPPING_ROT270     4
#define MAPPING_OEM        5

// 页面数量（竖着排的「页」）
#define MAX_PAGES    6
// 每个竖页里最多能横向排多少个「界面」（给左右划用）
/* ⚠️ MAX_COLS 必须 ≥ 最多的那个竖页的横屏数：`show_page()` 和 `-col` 都按它遍历，
 * 少了就“最后几屏永远显示不出来/也划不到”（SYSTEM 加息屏配置那屏时踩过：4 → 5）。 */
#define MAX_COLS     5

/* 每个竖页横向排了几个界面（1 = 没有左右兄弟 ⇒ 左右划不动）。
 * 实际用 page_col_count() 取（SMS 页的列数**随短信条数变化**，是动态的）：
 *   第1页（TIME）  配了两个：[TIME] ←→ [农历]；
 *   第2页（Mobile）配了三个：[Mobile] ←→ [NET 网络参数] ←→ [组网模式偏好 4G/5G/4G+5G]；
 *   第3页（SMS）  配了 1+N 个：[短信统计] ←→ [每条短信一屏]（N 随条数变）；
 *   第4页（SPEED）配了三个：[SPEED] ←→ [DOWN 下行速率] ←→ [UP 上行速率]；
 *   第5页（INFO） 配了四个：[INFO] ←→ [CPU 趋势] ←→ [内存趋势] ←→ [温度趋势]；
 *   第6页（SYSTEM）配了四个：[SYSTEM 概览] ←→ [WIFI] ←→ [LAN 配置] ←→ [屏幕亮度]；
 * 想再加一个固定横屏：把这里的数字 +1，再到 page_obj_at() 里补上对象，
 * 并在 main_screen_init() 里创建它。 */
static const int PAGE_COLS[MAX_PAGES] = { 2, 3, 1, 3, 4, 5 };
/* 每个竖页上次停在哪个横屏（上下划离开再回来时不至于又跳回第一屏） */
static int g_page_col_cur[MAX_PAGES] = { 0, 0, 0, 0, 0, 0 };

/* 某竖页横向有几屏（SMS 页动态：1 个列表屏 + 每条短信一屏 + 1 个余额查询屏） */
static int page_col_count(int page)
{
    if (page == SMSPAGE_PAGE_INDEX)
        return 2 + smspage_msg_count();
    if (page < 0 || page >= MAX_PAGES)
        return 0;
    return PAGE_COLS[page];
}


// 应用状态结构体
typedef struct {
    bool running;
    int current_page;
    int current_col;    /* 当前竖页里的第几个横屏（0 = 第一屏） */
    lv_indev_t *touch_indev;
    lv_disp_t *display;
    lv_obj_t *touch_dot;
    lv_obj_t *screen;
    int fd_touch;
    int mapping_mode;
} app_state_t;

// 全局状态
app_state_t app = {
    .running = false,
    .current_page = 0,
    .current_col = 0,
    .touch_indev = NULL,
    .display = NULL,
    .touch_dot = NULL,
    .fd_touch = -1,
    .mapping_mode = MAPPING_OEM
};

/* 启动阶段就收到 SIGTERM/SIGINT 时记下来：否则 main() 接着又把 running 置 true，
 * 进程就不再理会这个信号，procd 只能 SIGKILL（真机日志里出现过）。 */
static volatile sig_atomic_t g_signalled = 0;

// 屏幕对象
lv_obj_t *boot_screen;
lv_obj_t *main_screen;
lv_obj_t *title;
lv_obj_t *label_Charge;
lv_obj_t *label_Battery;
lv_obj_t *battery_icon;
lv_obj_t *Charge_icon;
lv_obj_t *netstart_icon;
lv_obj_t *error_container;
lv_obj_t *label_error;

// 开机画面元素
lv_obj_t *logo_img;
lv_obj_t *progress_bar;

// 主界面元素
lv_obj_t *main_title;
lv_obj_t *main_btn;

// 调试用：-page N 直接停在某一页（默认 0 = HOME），-col N 再指定横向第几屏
int g_start_page = 0;
int g_start_col = 0;

// 加载状态文本
static const char *status_texts[] = {
    "", "", "", "", ""
};



// 函数声明
void switch_to_page(int page_index);
static void show_page(int page, int col);
void gesture_handler(lv_event_t *e);
void switch_to_main_screen(void);

// 电池图标更新（电量值来自 WebSocket）
void BATTERY(int v){
    if(v<=5 && v>=0){
        lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(battery_icon, lv_color_hex(0xFF3300), 0);
    }
    else if(v >= 6 && v <= 30){
        lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_1);
        lv_obj_set_style_text_color(battery_icon, lv_color_hex(0xFFCC33), 0);
    }
    else if(v >= 31 && v <= 70){
        lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_2);
        lv_obj_set_style_text_color(battery_icon, lv_color_hex(0x99FFFF), 0);
    }
    else if(v >= 71 && v <= 90){
        lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_3);
        lv_obj_set_style_text_color(battery_icon, lv_color_hex(0x99FFFF), 0);
    }
    else{
        lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_FULL);
        lv_obj_set_style_text_color(battery_icon, lv_color_hex(0x99FFFF), 0);
    }
}

// 创建触摸调试点
void create_touch_dot(lv_obj_t *parent) {
    app.touch_dot = lv_obj_create(parent);
    lv_obj_set_size(app.touch_dot, 20, 20);
    lv_obj_set_style_radius(app.touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(app.touch_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(app.touch_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_width(app.touch_dot, 2, 0);
    lv_obj_set_style_border_color(app.touch_dot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(app.touch_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(app.touch_dot);
}

/* (page, col) → 界面对象；col 超出该页配置的横屏数就返回 NULL */
static lv_obj_t *page_obj_at(int page, int col)
{
    if (page < 0 || page >= MAX_PAGES || col < 0 || col >= page_col_count(page))
        return NULL;
    if (page == 1 && col == 1)
        return netparam_page;          /* Mobile 右边那一屏：NET 网络参数 */
    if (page == 1 && col == 2)
        return netmode_page;           /* Mobile 右边第二屏：组网模式偏好（4G / 5G / 4G+5G） */
    if (page == 0 && col == 1)
        return lunar_page;             /* TIME 右边那一屏：农历 */
    if (page == SMSPAGE_PAGE_INDEX) {
        if (col == 0)
            return smspage_list();                          /* SMS：第 1 屏 = 列表 */
        if (col > smspage_msg_count())
            return smspage_query();                         /* SMS：最后一屏 = 余额查询 */
        return smspage_detail();                            /* SMS：第 1..N 屏 = 详情（同一个对象） */
    }
    if (page == 3 && col > 0)
        return speedchart_page(col - 1); /* SPEED 右边两屏：下行 / 上行 速率 */
    if (page == 4 && col > 0)
        return syschart_page(col - 1); /* INFO 右边三屏：CPU / 内存 / 温度 趋势 */
    if (page == 5 && col > 0) {
        if (col == 1) return wifi_page;          /* SYSTEM 第二屏：WIFI（SSID/密码/二维码/开关） */
        if (col == 2) return lan_page;           /* SYSTEM 第三屏：LAN 配置（网口角色 / 恢复原始配置） */
        if (col == 3) return Brightness_page;    /* SYSTEM 第四屏：屏幕亮度 */
        return idle_page;                        /* SYSTEM 第五屏：息屏配置（1/3/5 分钟、不息屏） */
    }

    switch (page) {
        case 0:  return Home_page;
        case 1:  return mobilebase_page;
        case 2:  return smspage_list();
        case 3:  return netinfo_page;
        case 4:  return osInfo_page;
        case 5:  return sysinfo_page;
        default: return NULL;
    }
}

/* 顶栏布局参数
 * 右边三个图标块从屏幕右边缘往里排：netstart(-126) / Charge(-94) / Battery(0)，
 * 宽度分别是 30/30/92，块与块之间 2px。标题块从 x=0 一直铺到最左边那个图块前面，只留 1px。 */
#define TITLE_BAR_H        30
#define TITLE_PAD_HOR      10
#define ICON_BOX_W         30
#define NET_BOX_RIGHT_OFS  (-126)                       /* netstart_container 的 TOP_RIGHT 偏移 */
#define ICONS_LEFT_X       (SCREEN_WIDTH + NET_BOX_RIGHT_OFS - ICON_BOX_W)   /* 最左图块左边界 = 164 */
#define TITLE_GAP_RIGHT    2                            /* 标题块与图块之间留 2px（与图块之间的 2px 一致） */

/* (page) → 标题栏文案
 *
 * ⚠️ **同一竖页的所有横屏共用一个标题**（用户要求：左右划时名称与图标保持不变，
 * 这样一眼看出待的就是这一页的子页面，例如 Mobile 右边的网络参数页）。
 * 将来真要给某个横屏单独起名，就在这里改成按 (page, col) 分就可以了。 */
static const char *page_title_at(int page)
{
    switch (page) {
        case 0:  return LV_SYMBOL_HOME " TIME";
        case 1:  return LV_SYMBOL_HOME " Mobile";
        case 2:  return LV_SYMBOL_ENVELOPE " SMS";
        case 3:  return LV_SYMBOL_USB " SPEED";   /* D/Speed、U/Speed 两张曲线 */
        case 4:  return LV_SYMBOL_REFRESH " INFO";
        case 5:  return LV_SYMBOL_SETTINGS " SYSTEM";  /* 概览 + WIFI + 亮度 */
        default: return "";
    }
}

/* 显示 (page, col) 这一屏：其它页、以及同组里别的横屏全部隐藏。
 * 横屏切换**不做动画**（滑动时就当作换了一屏，跟上下划一个观感）。 */
static void show_page(int page, int col)
{
    if (page < 0 || page >= MAX_PAGES)
        return;
    if (col < 0 || col >= page_col_count(page))
        col = 0;

    for (int p = 0; p < MAX_PAGES; p++) {
        for (int c = 0; c < MAX_COLS; c++) {
            lv_obj_t *o = page_obj_at(p, c);

            if (o && o != page_obj_at(page, col))
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ⚠️ SMS 详情页是「**一个对象多屏**」（1 个主页 + 每条短信一屏），列数随短信条数变化。
     * 卡里一条短信都没有时 page_col_count() 只到 0，上面那圈遍历就**看不到它** ⇒
     * 它会一直盖在主页（甚至其它竖页）上面。所以打开主页时要单独把它藏起来。
     * 余额查询屏同理：它站在"最后一屏"，条数一变位置就变（上面那圈遍历也看不到）。 */
    if (page == SMSPAGE_PAGE_INDEX) {
        if (col == 0)
            lv_obj_add_flag(smspage_detail(), LV_OBJ_FLAG_HIDDEN);
        if (col != smspage_msg_count() + 1)
            lv_obj_add_flag(smspage_query(), LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *o = page_obj_at(page, col);

    if (o)
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);

    /* SMS 的详情页是**同一个对象**，切屏时要把内容换成对应那条短信
     * （余额查询屏是独立对象、不接 -1 的序号，所以要先比一下条数） */
    if (page == SMSPAGE_PAGE_INDEX && col > 0 && col <= smspage_msg_count())
        smspage_show(col - 1);

    /* 组网模式页：切到它就问服务端要一次当前值（服务端带 TTL 缓存，不会把串口刷满） */
    if (page == 1 && col == 2)
        netmode_page_refresh();

    lv_label_set_text(title, page_title_at(page));

    app.current_page = page;
    app.current_col  = col;
    g_page_col_cur[page] = col;    /* 记住这一竖页停在哪个横屏 */
}

/* 给别的模块（短信弹窗）用的页面跳转 */
void tft_show_page(int page, int col)
{
    show_page(page, col);
}

// 页面切换（上下划用这个：回到该竖页上次停的那个横屏）
void switch_to_page(int page_index) {
    if (page_index < 0 || page_index >= MAX_PAGES)
        return;

    show_page(page_index, g_page_col_cur[page_index]);
}

/* ====================== 息屏 / 亮屏（用户 2026-10-07 要求） ======================
 * 「不操作 3 分钟自动息屏，点击屏幕自动亮屏」。
 *
 * 无操作只能在这里算：只有触摸层知道"有没有人碰屏"（这块屏摸一下会发事件，
 * 摸完就没了 —— 没有持续的按住/抬起事件，所以"最后一次事件"就是判据）。
 * 真的关屏/开屏在 brightness_page.c（背光 0 + bl_power 1）。
 *
 * ⚠️ 唤醒的那一下要**吃掉**（g_wake_swallow）：不然"点亮屏幕"的同一根手指会顺手把
 * 手指落点上的按钮/页面给点了（甚至弹出短信删除框 —— 那就很吓人）。
 */
#define IDLE_SLEEP_MS_DEFAULT 180000   /* 3 分钟 */
#define IDLE_STATE_CONF "/etc/tftdisplay.idle"   /* 用户选的那一挡（**包外**文件，升级不覆盖） */
static uint32_t g_last_touch_at;       /* 最后一次触摸事件的时刻 */
static uint32_t g_idle_sleep_ms = 0;   /* 0 = 永不息屏（也在 TFT_IDLE_OFF_MS=0 时） */
static bool     g_touch_down;          /* 原始"手指按着"（按住不动时也不该息屏） */
static bool     g_wake_swallow;        /* 唤醒那一下吃掉中（等手指抬起才解除） */

/* ---- 非触摸原因亮屏（用户 2026-10-08：“有短信在息屏情况下要亮屏幕”） ----
 * 算法：告知主界面的息屏计时“现在算作刚有人用过”，并且在这段时间内**不许息屏** ——
 * 不这样做的话，息屏时来短信把背光点亮后，下一拍 idle_check_cb 看到“已经 3 分钟没碰屏了”
 * 会马上把屏又关掉（弹窗只闪一下）。
 * 30 秒的来历：新短信弹窗自己 15 秒收（POP_AUTO_HIDE_MS），留双倍时间够看清+点进去。 */
#define ALERT_AWAKE_MS_DEFAULT 30000u
#define ALERT_AWAKE_ENV  "TFT_ALERT_AWAKE_MS"   /* 调试/截图用；0 = 亮一下不额外留 */
static uint32_t g_alert_awake_ms = ALERT_AWAKE_MS_DEFAULT;
static uint32_t g_alert_woke_at;       /* 上次被“有事发生”叫亮的时刻（0 = 不在保留窗口内） */

/* ---- 给 SYSTEM→息屏配置页（pages/idle_page.c）用的读写接口 ---- */

static void idle_save_ms(uint32_t ms)
{
    FILE *f = fopen(IDLE_STATE_CONF, "w");

    if (!f) return;
    fprintf(f, "%u\n", (unsigned)ms);
    fclose(f);
}

uint32_t tft_get_idle_ms(void)
{
    return g_idle_sleep_ms;
}

/* 改息屏超时：⚠️ 必须把基准时刻重置成"现在"，否则改成小值时
 * （比如从 3 分钟改成 1 分钟）可能下一拍就直接黑屏。 */
void tft_set_idle_ms(uint32_t ms)
{
    g_idle_sleep_ms = ms;
    g_last_touch_at = lv_tick_get();
    idle_save_ms(ms);
    printf("tftdisplay: 息屏时间已设成 %u 毫秒%s\n", (unsigned)ms,
           ms ? "" : "（不息屏）");
    fflush(stdout);
}

/* 给别的模块用（现在只有短信页）："出事了 ⇒ 把屏叫亮"，并在之后留一小段时间不许息屏。
 *
 * 用户 2026-10-08："如果有新短信在息屏情况下要亮屏幕"。
 *
 * ⚠️ 息屏期间 LVGL 照常在画（息屏 = 背光 0 + bl_power 1，屏里的画面一直是新的）
 *    ⇒ 这里只做"开背光 + 重新计时"，不需要额外重绘，也不用管唤醒吞一下那套（那是手指的事）。
 * ⚠️ 屏幕本来就亮着时不重复开背光（brightness_screen_wake_ex 自己会判），只把计时往后顶。 */
void tft_wake_for_alert(const char *why)
{
    static bool cfg_done;
    uint32_t now = lv_tick_get();

    if (!cfg_done) {
        const char *v = getenv(ALERT_AWAKE_ENV);

        cfg_done = true;
        if (v)
            g_alert_awake_ms = (uint32_t)atoi(v);
        printf("tftdisplay: 有事亮屏后至少留 %u 毫秒（%s 可改）\n",
               (unsigned)g_alert_awake_ms, ALERT_AWAKE_ENV);
    }

    if (brightness_screen_is_sleeping()) {
        brightness_screen_wake_ex(why ? why : "有事发生");
        printf("[screen] %s ⇒ 亮屏，并保持 %u 秒（%s 可改）\n",
               why ? why : "有事发生", (unsigned)(g_alert_awake_ms / 1000u), ALERT_AWAKE_ENV);
        fflush(stdout);
    } else {
        /* 屏本来就亮着：只把计时往后顶（用户正常用时来短信不会闪一下背光） */
        printf("[screen] %s ⇒ 屏本就亮着，只把息屏计时往后顶 %u 秒\n",
               why ? why : "有事发生", (unsigned)(g_alert_awake_ms / 1000u));
        fflush(stdout);
    }

    g_last_touch_at = now;      /* 从这一刻重新算"无操作" */
    g_alert_woke_at = now;      /* 并进入保留窗口（见 idle_check_cb） */
}

static void idle_check_cb(lv_timer_t *t)
{
    /* 自检钩子 TFT_SCREEN_TEST=1：10 秒息屏 → 20 秒亮屏 → 循环。
     * 用途：没有手指也能验证"关屏"与"开屏"两个方向都通（看背光节点的值）。 */
    static bool hook_init, hooked, alert_hooked;
    static uint32_t hook_at;
    static bool alert_woke;

    (void)t;

    if (!hook_init) {
        hook_init = true;
        hooked = (getenv("TFT_SCREEN_TEST") != NULL);
        alert_hooked = (getenv("TFT_ALERT_TEST") != NULL);
        hook_at = lv_tick_get();
    }
    if (hooked) {
        uint32_t el = lv_tick_elaps(hook_at);

        if (el >= 20000) {
            hook_at = lv_tick_get();
        } else if (el >= 10000) {
            brightness_screen_sleep();
        } else {
            brightness_screen_wake();
        }
        return;
    }

    /* 自检钩子 TFT_ALERT_TEST=1：5 秒先息屏 → 10 秒用"有事发生"那条路把屏叫亮
     * （息屏时来新短信走的就是同一个 tft_wake_for_alert）⇒ 验证"亮了之后真的留得住"，
     * 不会下一拍又被关掉。配合 TFT_IDLE_OFF_MS=45000 看：
     * 10s 亮 → 保留窗口到 40s → 45s 无操作才真的再息屏。全程没有手指。 */
    if (alert_hooked) {
        uint32_t el = lv_tick_elaps(hook_at);

        if (el >= 5000 && el < 6000 && !brightness_screen_is_sleeping())
            brightness_screen_sleep();
        if (el >= 10000 && !alert_woke) {
            alert_woke = true;
            tft_wake_for_alert("自检");
        }
        /* 之后不再插手：让正常的息屏计时自己去验"保留窗口有没有生效" */
    }

    if (g_idle_sleep_ms == 0 || brightness_screen_is_sleeping()) {
        g_last_touch_at = lv_tick_get();   /* 息屏期间不累计，免得刚亮屏又马上再息 */
        return;
    }
    /* 刚被"有事发生"叫亮 ⇒ 先把这段保留时间给完（不给的话下一拍就把屏又关了，弹窗只闪一下） */
    if (g_alert_woke_at) {
        if (lv_tick_elaps(g_alert_woke_at) < g_alert_awake_ms) {
            g_last_touch_at = lv_tick_get();
            return;
        }
        g_alert_woke_at = 0;               /* 留够了 ⇒ 从此刻开始正常数无操作 */
        g_last_touch_at = lv_tick_get();
        printf("[screen] 有事亮屏保留 %u 秒结束（t=%us）⇒ 恢复正常息屏计时\n",
               (unsigned)(g_alert_awake_ms / 1000u), (unsigned)(g_last_touch_at / 1000u));
        fflush(stdout);
        return;
    }
    /* ⚠️ 第一拍先把基准定在"现在"：本机 LVGL 的 tick 是**开机以来的毫秒数**（不是从 0 开始），
     * 用 0 当初值的话 `lv_tick_elaps(0)` 一上来就是个很大的数 ⇒ **开机 1 秒就把屏关了**（踩过）。 */
    if (g_last_touch_at == 0) {
        g_last_touch_at = lv_tick_get();
        return;
    }
    if (g_touch_down)
        return;                            /* 手指还在屏上（按住不动）—— 别在手指底下关屏 */
    if (lv_tick_elaps(g_last_touch_at) >= g_idle_sleep_ms)
        brightness_screen_sleep();
}

// 触摸读取回调
/* ====================== "两指同点 / 连点两下" ⇒ 短信删除确认 ======================
 *
 * 背景（用户 2026-10-07 要求）：这块 AXS5106 屏**不报 BTN_TOUCH**（见下面 touch_read_cb 里
 * 的判定），手指按住不动时一条事件都不发 ⇒ 上层只能靠"100ms 没消息就当松手"推断，
 * **长按根本无从判定**（按住 0.1 秒就已经被判成一次短按）。用户提的办法：**两根手指同时点一下**。
 *
 * 实现放在**读事件这一层**（比 LVGL 的长按靠谱得多）：每出现一次"按下"边沿就记下时刻，
 * 两次相距 ≤ DOUBLE_PRESS_MS 且当前停在短信详情页 ⇒ 当成"两指同点 / 连点两下"，弹删除确认。
 *
 * 为什么两指也能被当成"两次按下"：上面那个 100ms 推断会把"手指还在屏上但不发事件"翻成松手，
 * 所以第二根手指落下时（哪怕只晚几十毫秒）就是一次**新的按下边沿** ✓。
 * 而真正按住不放时两次边沿至少隔几秒 ⇒ 不会误触。 */
#define DOUBLE_PRESS_MS 600      /* 两次按下相距多近算"两指/连点"（放宽点：两根手指往往是先后落的） */
#define HOLD_TRIGGER_MS 900      /* 按住不动多久算"长按" */
#define HOLD_MOVE_TOL   8        /* 允许多大位移（超过就当滑动，不算按住） */
/* ⚠️ 2026-10-07 真机日志（`TFT_TOUCH_DBG=1`）确认这块 AXS5106 的实际上报：
 *   · **从不报 `BTN_TOUCH`**（每行都是 `saw_btn_touch=0`）；
 *   · **从不报 `ABS_MT_SLOT` / `ABS_MT_TRACKING_ID`**（虽然能力位里声明了）——
 *     两指同时按也只当**一个**接触点（报的是合并后的坐标）；
 *   · 抬手、按住不动都**不发任何事件** ⇒ 只能靠"100ms 没消息 = 松手"推断，
 *     所以**长按在这块屏上原理上判不出来**（用户 2026-10-07 拍板改用"两根手指同时点"）。
 * 能用得上的信号只有：**两次"原地轻点"（双击 / 两指先后点）**。
 * ⚠️ 2026-10-07 用户反馈"左右滑动会误触弹出删除框"：我先前加的"按下期间坐标跳变"那条判据
 *    把**快滑（flick）**也算进去了（快滑就是"一个大位移 + 很快抬手"）⇒ **已删除**；
 *    并且现在要求两次都是**原地轻点**（位移 ≤ TAP_MOVE_TOL），一旦滑动就立即作废。
 *
 * ⚠️⚠️ 2026-10-08 用户报"**我这边不卡，别人装完划一下要卡 3-5 秒才能再划**" —— 根因是**松手判定**
 *    按屏型分档踩到了坑：原来"报过 BTN_TOUCH 的屏"只认 `BTN_TOUCH=0` 那条事件，
 *    兜底放宽到 **3000 ms**；而不少屏抬手只发 `ABS_MT_TRACKING_ID=-1`（不发 BTN_TOUCH=0）、
 *    或驱动丢了那批事件 ⇒ `last_pressed` 硬撑 3 秒，LVGL 的手势又有 `wait_until_release`
 *    （手势发过一次必须见到松手才允许下一次）⇒ 3 秒 + 手势 500 ms 冷却 = 用户看到的 3~5 秒。
 *    现在改成三层（见下面 `touch_read_cb`）：
 *      ① 无事件时用 `EVIOCGKEY` **主动查** BTN_TOUCH 位 ⇒ 抬手立刻算松手（不依赖事件到没到）；
 *      ② `SYN_DROPPED`（驱动丢事件）直接当松手；
 *      ③ 兜底从 3000 ms 降到 **600 ms**（只在键状态也查不到时生效）。
 *    自己这块屏（从不报 BTN_TOUCH）走的还是原来的 100 ms 推断，行为一个字都没变。 */
#define TAP_MOVE_TOL    12       /* "原地轻点"允许的最大位移；超过就算滑动（不是轻点） */
/* 多点：这块屏的驱动是 **type B**（能力位 `ABS=…` 里有 `ABS_MT_SLOT`(47) 与
 * `ABS_MT_TRACKING_ID`(57)，实测 `cat /proc/bus/input/devices`）⇒ 真能报多指，
 * 所以"两指同时点"可以直接数手指，不用猜。 */
#define MT_SLOTS 10
static uint32_t g_press_at;      /* 本次按下的时刻（记档/调试用） */
static uint32_t g_hold_at;       /* 本次按下开始计时 */
static int      g_hold_x, g_hold_y;
static bool     g_hold_armed;    /* 正在判定"按住不动" */
static int      g_mt_slot;       /* 当前 SLOT */
static bool     g_mt_used[MT_SLOTS];   /* 该 slot 上有没有手指（由 TRACKING_ID 维护） */
static int      g_mt_n;          /* 屏上现在几根手指 */
static bool     g_mt_fired;      /* 本次多指还没触发过 */
static bool     g_mt_second;     /* 本轮读到过"第二根手指落下"（读完这批再触发） */
static bool     g_mt_lift;       /* 本轮读到过"某根手指抬起" */
static uint32_t g_mt_last_fire;  /* 上次因"两指"弹框的时刻（冷却用：
                                  * 万一固件不发 TRACKING_ID，g_mt_n 永远为 0 ⇒
                                  * 单靠 g_mt_fired 会每批都重新武装、连着弹好几次） */
static uint32_t g_press_samples; /* 本次按下到现在收到多少个定位批次（判拖动） */
static int      g_press_px, g_press_py;   /* 上一个定位点（判在原地还是滑动） */
static bool     g_press_moved;   /* 本次按下期间动过（超过 TAP_MOVE_TOL）⇒ 不是"轻点" */
static uint32_t g_tap_at;        /* 上一次"轻点"完成的时刻 */
static bool     g_tap_seen;      /* 记过一次轻点，还没配对 */

/* 手上这三种情形都算"想删这条短信"：① 两根手指同时点；② 两次按下挨得近；③ 按住不动够久。
 * 都只有在**短信详情页**才真的弹框（别的页什么都不做）。
 * ⚠️ 触发要放在**读完这批事件之后**（不要在 indev 回谓里直接改 LVGL 对象）。 */
static void tft_open_delete_now(const char *why)
{
    if (app.current_page != SMSPAGE_PAGE_INDEX || app.current_col <= 0)
        return;
    printf("[touch] %s ⇒ 打开删除确认\n", why);
    fflush(stdout);
    smspage_open_delete();
}

static void tft_note_press(uint32_t now, int x, int y)
{
    g_press_at = now;
    g_hold_armed = true;                 /* 新的一次按下：重新开始数"按住不动" */
    g_hold_at = now;
    g_hold_x = x;
    g_hold_y = y;
    g_press_samples = 0;                 /* "原地 / 滑动"判定也跟着重新开始 */
    g_press_px = x;
    g_press_py = y;
    g_press_moved = false;
}

/* 主动读"按键位图"里的 BTN_TOUCH（0 = 现在没有手指按着）。
 *
 * ⚠️ 只在**确认过这块屏会报 BTN_TOUCH=0 之后**才信它的结论（`btn_release_seen`）！
 * 因为这玩意只有驱动真的发过 BTN_TOUCH=0 时才会被内核清零；**只报按下、不报抬起**的驱动
 * 会让这一位**永远停在 1** —— 无条件相信它 = 永远以为手指没抬 ⇒ 界面直接卡死（踩过，见
 * src/utils/touchlogic.c 文件头）。所以这里只当"提前知道抬了"的加速器，不再是唯一依据。
 *
 * 返回 0 = 查到了（*down 有效），-1 = 这个 fd 查不了（调用方退回超时兜底）。 */
static int touch_btn_touch_down(int fd, bool *down)
{
    unsigned long bits[(BTN_TOUCH / (8 * (int)sizeof(unsigned long))) + 1];

    if (fd < 0 || !down)
        return -1;
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0)
        return -1;
    *down = (bits[BTN_TOUCH / (8 * (int)sizeof(unsigned long))] >>
             (BTN_TOUCH % (8 * (int)sizeof(unsigned long)))) & 1UL;
    return 0;
}

void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    static bool dp_prev_pressed = false;    /* 上一拍的按下状态（判"按下"边沿用） */
    static lv_point_t last_point = {0};
    static bool last_pressed = false;
    static bool got_x = false;
    static bool got_y = false;
    /* saw_btn_touch：面板报过 BTN_TOUCH ⇒ 按/松手就尽量以它为准（"有没有真的按着"用
     * `touch_btn_touch_down()` 主动问内核，不再干等 `BTN_TOUCH=0` 那条事件）。
     * ⚠️⚠️ 这是"长按"能不能用的关键：AXS5106 这类电容屏**手指按住不动时不发事件**，
     * 原来纯靠"100ms 没事件就当松手"⇒ 按住 0.1 秒就被当成一次短按，
     * LVGL 的 LV_EVENT_LONG_PRESSED（短信页长按删除）永远不触发（踩过）。 */
    static bool saw_btn_touch = false;
    /* btn_release_seen：见过**抬手**（BTN_TOUCH=0）⇒ 内核那个按键位图可信，
     * 可以用它抢在事件前面判松手（touchlogic.c ②）。一辈子都没见过 0 的屏就别信。 */
    static bool btn_release_seen = false;
    static int  stuck_warned = 0;    /* "位图一直是 1" 这条告警只打前几次 */
    /* TFT_TOUCH_DBG=1：把触摸事件的"有哪些类型/码"与"按下↔松开"判定打到系统日志。
     * 排长按这类问题必须看它 —— 不同触摸屏上报什么差别很大（踩过）。
     * 只打前 60 条 + 状态变化，不刷屏。 */
    static bool touch_dbg = false;
    static bool touch_dbg_init = false;
    static int  touch_dbg_n = 0;
    static int  touch_dbg_state_n = 0;    /* "按下/松开"那条只记前 40 条（见下） */
    static uint32_t last_activity_time = 0;
    struct input_event ev;
    int bytes_read;
    static int dbg_counter = 0;
    uint32_t curr_time = lv_tick_get();
    bool touch_active = false;
    int  got_events = 0;          /* 这一次回调里收到几条实事（判"有人在碰屏"） */
    bool syn_dropped = false;     /* 驱动告知"丢了一批事件"（状态不可信，当松手处理） */
    
    while ((bytes_read = read(app.fd_touch, &ev, sizeof(ev))) > 0) {
        if (dbg_counter++ % 100 == 0) {}

        got_events++;
        g_last_touch_at = curr_time;    /* 有事件就是"人在操作"（息屏计时归零） */

        if (!touch_dbg_init) {
            touch_dbg_init = true;
            touch_dbg = (getenv("TFT_TOUCH_DBG") != NULL);
            if (touch_dbg)
                printf("[touch] 开始记录事件（type/code/value）\n");
        }
        if (touch_dbg && touch_dbg_n < 200 &&
            (ev.type != EV_SYN || ev.code != SYN_REPORT)) {
            touch_dbg_n++;
            printf("[touch] t=%d code=%d val=%d%s\n", ev.type, ev.code, ev.value,
                   ev.type == EV_KEY && ev.code == BTN_TOUCH ? "  <= BTN_TOUCH(按下/松开)" : "");
            fflush(stdout);
        }
        
        if (ev.type == EV_ABS) {
            touch_active = true;
            last_activity_time = curr_time;

            /* 多点（type B）：SLOT 决定"接下来这组坐标属于哪根手指"，
             * TRACKING_ID 是"这根手指起/落"（-1 = 落）。数出一共几根手指。 */
            if (ev.code == ABS_MT_SLOT) {
                g_mt_slot = (ev.value > 0 && ev.value < MT_SLOTS) ? ev.value : 0;
                /* 备选判据（有些固件不发 TRACKING_ID）：按下期间切到 slot ≥1 ⇒ 第二根手指 */
                if (ev.value >= 1 && last_pressed)
                    g_mt_second = true;
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                bool on = (ev.value >= 0);
                if (g_mt_slot >= 0 && g_mt_slot < MT_SLOTS) {
                    if (on && !g_mt_used[g_mt_slot]) {
                        g_mt_used[g_mt_slot] = true;
                        g_mt_n++;
                        if (g_mt_n >= 2)
                            g_mt_second = true;      /* 第二根手指落下了 */
                    } else if (!on) {
                        /* -1 = 这根手指抬起了。⚠️ **不管之前记没记过它的按下**都得认：
                         * 有的驱动只在抬手发 TRACKING_ID、按下只给 SLOT + 坐标（不发正数 ID），
                         * 原来的 `&& g_mt_used[slot]` 会把这类抬手整个**丢掉** ⇒ 靠 3 秒超时兜底
                         * （用户报的"划一下卡 3-5 秒"，见 touchlogic.c）。 */
                        g_mt_used[g_mt_slot] = false;
                        if (g_mt_n > 0)
                            g_mt_n--;
                        g_mt_lift = true;
                    }
                }
            }

            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) {
                last_point.x = ev.value;
                got_x = true;
            }
            if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) {
                last_point.y = ev.value;
                got_y = true;
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            last_pressed = (ev.value == 1);
            saw_btn_touch = true;          /* 这块屏会报 BTN_TOUCH ⇒ 松手判定按它的脾气来 */
            if (ev.value == 0)
                btn_release_seen = true;   /* 抬手也报 ⇒ 内核按键位图可信（见 touchlogic.c） */
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (got_x || got_y) {
                last_pressed = true;
                last_activity_time = curr_time;
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
            /* 客户端读得太慢 / 缓冲溢出时驱动会发这条：此后"按着还是松了"不可信，
             * 本轮直接当松手（不然会卡在 PRESSED 上直到超时，换页就卡住了）。 */
            syn_dropped = true;
        }
    }
    
    if (syn_dropped) {
        if (last_pressed && touch_dbg) {
            printf("[touch] SYN_DROPPED ⇒ 判松手（事件被丢）\n");
            fflush(stdout);
        }
        last_pressed = false;
        got_x = false;
        got_y = false;
    } else if (g_mt_lift && g_mt_n == 0) {
        /* 最后一个手指抬起了（type B 驱动会明确告知）⇒ 立刻松手，不用等超时（点按更跟手）。 */
        last_pressed = false;
        got_x = false;
        got_y = false;
        g_mt_lift = false;
    } else if (touch_active) {
        last_pressed = true;
    } else if (last_pressed) {
        /* 没有新事件 ⇒ 松手了没？判定全在 utils/touchlogic.c（纯函数、宿主机有单测），
         * 这里只负责把"当前状态"喂进去，按结论动状态机。 */
        touch_release_ctx_t ctx;
        bool down = false;

        memset(&ctx, 0, sizeof(ctx));
        ctx.last_activity_ms = last_activity_time;
        ctx.saw_btn_touch    = saw_btn_touch;
        ctx.btn_release_seen = btn_release_seen;
        ctx.moved            = g_press_moved;
        ctx.ioctl_ok         = (btn_release_seen &&
                                touch_btn_touch_down(app.fd_touch, &down) == 0);
        ctx.ioctl_down       = down;

        {
            touch_release_t rel = touch_should_release(curr_time, &ctx);

            if (rel != TOUCH_REL_NONE) {
                if (touch_dbg) {
                    static const char *why[] = {
                        "?", "EVIOCGKEY：位图说抬了", "超时：滑动结束（300ms）",
                        "超时：按住上限（1100ms）", "超时：老路径（100ms）"
                    };

                    printf("[touch] 松手（%s，已按 %ums）\n", why[rel],
                           (unsigned)(curr_time - g_press_at));
                    fflush(stdout);
                }
                /* ⚠️ 位图说"还按着"，却靠超时上限松的手 ⇒ 这块屏的 BTN_TOUCH 只报按下不报抬起，
                 * 位图永远停在 1（"改完划一下卡住"就是它）。这条**不受 TFT_TOUCH_DBG 控制**，
                 * 只打前几次 —— 现场出问题时 logread 里必须看得到这句。 */
                if (rel == TOUCH_REL_TIMEOUT_HOLD && ctx.ioctl_down && stuck_warned < 5) {
                    stuck_warned++;
                    printf("[touch] 注意：BTN_TOUCH 位图一直是 1（这块屏只报按下、不报抬起）"
                           " ⇒ 松手靠 %ums 上限兜底\n", (unsigned)TOUCH_GAP_HOLD_MS);
                    fflush(stdout);
                }
                last_pressed = false;
                got_x = false;
                got_y = false;
            }
        }
    }

    if (bytes_read < 0 && errno != EAGAIN) {}

    int raw_x = last_point.x;
    int raw_y = last_point.y;
    
    switch(app.mapping_mode) {
        case MAPPING_DIRECT:
            data->point.x = raw_x;
            data->point.y = raw_y;
            break;
        case MAPPING_SWAP:
            data->point.x = raw_y;
            data->point.y = raw_x;
            break;
        case MAPPING_ROT180:
            data->point.x = SCREEN_WIDTH - raw_x;
            data->point.y = SCREEN_HEIGHT - raw_y;
            break;
        case MAPPING_ROT90:
            data->point.x = raw_y;
            data->point.y = SCREEN_WIDTH - raw_x;
            break;
        case MAPPING_ROT270:
            data->point.x = SCREEN_HEIGHT - raw_y;
            data->point.y = raw_x;
            break;
        case MAPPING_OEM:
            data->point.x = SCREEN_WIDTH - raw_y;
            data->point.y = SCREEN_HEIGHT - raw_x;
            break;
        default:
            data->point.x = raw_x;
            data->point.y = raw_y;
    }
    
    if (data->point.x < 0) data->point.x = 0;
    if (data->point.y < 0) data->point.y = 0;
    if (data->point.x >= SCREEN_WIDTH) data->point.x = SCREEN_WIDTH - 1;
    if (data->point.y >= SCREEN_HEIGHT) data->point.y = SCREEN_HEIGHT - 1;
    
    data->state = last_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    g_touch_down = last_pressed;          /* 给息屏计时用（按住不动时不关屏） */

    /* 息屏时被摸到 ⇒ 先亮屏，并把这一下吃掉（不点任何东西、不弹任何框）。
     * 吃到手指抬起为止（这块屏抬起不会发事件 ⇒ 就靠上面那个 100ms 推断出来的松手边沿）。 */
    if (got_events && brightness_screen_is_sleeping()) {
        brightness_screen_wake();
        g_wake_swallow = true;
    }
    if (g_wake_swallow) {
        if (!last_pressed)
            g_wake_swallow = false;       /* 抬起了 ⇒ 下一次触摸照常处理 */
        last_pressed = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* ⚠️ 下面这些手势/弹框判定必须在 **不是唤醒那一下** 时才跑 */
    if (g_wake_swallow) {
        g_hold_armed = false;      /* 别把"点亮屏幕"当成按住/两次按下 */
        dp_prev_pressed = false;
    } else {

    /* 两根手指同时点：第二根落下（或按下期间切到 slot≥1）即触发；
     * 直到手指全部抬起才重新武装，避免一次长按连着弹好几遍。 */
    if (g_mt_n == 0)
        g_mt_fired = false;
    if (g_mt_second && !g_mt_fired &&
        (g_mt_last_fire == 0 || (curr_time - g_mt_last_fire) > 800u)) {
        g_mt_fired = true;
        g_mt_last_fire = curr_time;
        tft_open_delete_now("两根手指同时点");
    }
    g_mt_second = false;

    /* 按下边沿 ⇒ 交给"两指同点 / 连点两下"的判定（松手不做事，判定在后面自己配对） */
    if (last_pressed && !dp_prev_pressed)
        tft_note_press(curr_time, data->point.x, data->point.y);

    /* 松手边沿 ⇒ 这一下算不算"原地轻点"？两次轻点挨得近就是"双击/两指先后点" ⇒ 弹删除确认。
     * ⚠️ 滑动（g_press_moved）一律作废：否则左右划换页会误触（用户反馈过）。 */
    if (!last_pressed && dp_prev_pressed) {
        if (g_press_moved) {
            g_tap_seen = false;
        } else if (g_tap_seen && (curr_time - g_tap_at) <= DOUBLE_PRESS_MS) {
            g_tap_seen = false;                    /* 配上了 ⇒ 不要连着触发 */
            tft_open_delete_now("两次轻点（双击 / 两指先后点）");
        } else {
            g_tap_seen = true;
            g_tap_at = curr_time;
        }
    }
    dp_prev_pressed = last_pressed;

    /* 按下期间的定位批次与位移统计（判"原地轻点"还是"滑动"） */
    if (last_pressed) {
        int jx, jy;

        g_press_samples++;
        jx = data->point.x - g_press_px;
        jy = data->point.y - g_press_py;
        if (jx < 0) jx = -jx;
        if (jy < 0) jy = -jy;
        if (jx > TAP_MOVE_TOL || jy > TAP_MOVE_TOL)
            g_press_moved = true;                  /* 动过 ⇒ 不可能是"轻点" */
        g_press_px = data->point.x;
        g_press_py = data->point.y;
    } else {
        g_press_samples = 0;
    }

    /* 另一条：按住不动够久也算（有些面板按住会持续上报事件，那时这条会命中；
     * 完全不报事件的机器上它永远不会触发，全靠上面那个"两次按下"的判定）。
     * 手指一动就取消 ⇒ 左右划换页不会被误当成按住。 */
    if (!last_pressed) {
        g_hold_armed = false;
    } else if (g_hold_armed) {
        int dx = data->point.x - g_hold_x;
        int dy = data->point.y - g_hold_y;

        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > HOLD_MOVE_TOL || dy > HOLD_MOVE_TOL) {
            g_hold_armed = false;                    /* 动了 ⇒ 是滑动 */
        } else if (curr_time - g_hold_at >= HOLD_TRIGGER_MS) {
            g_hold_armed = false;
            tft_open_delete_now("按住不动够久");
        }
    }

    }   /* end: if (!g_wake_swallow) 手势判定区 */

    static bool prev_state = false;
    if (prev_state != last_pressed) {
        prev_state = last_pressed;
        /* ⚠️ 只记前 40 条：这个调试标志可能一挂好几天，每次都打会在 logread 里刷屏
         *（"⇒ 打开删除确认"那几条**不限量** —— 它很少、且是排查时的关键证据）。 */
        if (touch_dbg && touch_dbg_state_n < 40) {
            touch_dbg_state_n++;
            printf("[touch] %s  (%d,%d) saw_btn_touch=%d\n",
                   last_pressed ? "按下" : "松开", data->point.x, data->point.y,
                   (int)saw_btn_touch);
            fflush(stdout);
        }
    }

    if (last_pressed && dbg_counter % 20 == 0) {}

    if (app.touch_dot && last_pressed) {
        lv_coord_t dot_width = lv_obj_get_width(app.touch_dot);
        lv_coord_t dot_height = lv_obj_get_height(app.touch_dot);
        
        lv_coord_t x = data->point.x - dot_width/2;
        lv_coord_t y = data->point.y - dot_height/2;
        
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + dot_width > SCREEN_WIDTH) x = SCREEN_WIDTH - dot_width;
        if (y + dot_height > SCREEN_HEIGHT) y = SCREEN_HEIGHT - dot_height;
        
        lv_obj_set_pos(app.touch_dot, x, y);
        lv_obj_clear_flag(app.touch_dot, LV_OBJ_FLAG_HIDDEN);
    } else if (app.touch_dot) {
        lv_obj_add_flag(app.touch_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

// 触摸初始化
bool init_touch(void) {
    const char *touch_paths[] = {
        "/dev/input/event0",
        "/dev/input/event1",
        "/dev/input/event2",
        "/dev/input/event3",
        "/dev/input/touchscreen0",
        "/dev/input/touch0",
        "/dev/touchscreen0",
        NULL
    };
    
    int i = 0;
    app.fd_touch = -1;
    
    while (touch_paths[i] != NULL) {
        app.fd_touch = open(touch_paths[i], O_RDONLY | O_NONBLOCK);
        if (app.fd_touch >= 0) {
            break;
        }
        i++;
    }
    
    if (app.fd_touch < 0) {
        return false;
    }
    
    struct input_absinfo abs_x_info;
    struct input_absinfo abs_y_info;
    
    if (ioctl(app.fd_touch, EVIOCGABS(ABS_X), &abs_x_info) < 0 && 
        ioctl(app.fd_touch, EVIOCGABS(ABS_MT_POSITION_X), &abs_x_info) < 0) {
        close(app.fd_touch);
        return false;
    }
    
    if (ioctl(app.fd_touch, EVIOCGABS(ABS_Y), &abs_y_info) < 0 &&
        ioctl(app.fd_touch, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y_info) < 0) {
        close(app.fd_touch);
        return false;
    }
    
    TOUCH_RAW_WIDTH = abs_x_info.maximum - abs_x_info.minimum + 1;
    TOUCH_RAW_HEIGHT = abs_y_info.maximum - abs_y_info.minimum + 1;

    app.touch_indev = lv_indev_create();
    lv_indev_set_type(app.touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(app.touch_indev, touch_read_cb);
    /* 长按判定时长：短信页的"长按删除"就靠它（原来是双击，用户嫌反应慢改成按住 0.55 秒）。
     * ⚠️ LVGL 里这个值同时是"短按连击（双击）"的间隔上限，本工程不再用双击，无影响。
     * ⚠️ 本工程也没有用 LONG_PRESSED_REPEAT（长按连发），所以不用管重复触发。 */
    lv_indev_set_long_press_time(app.touch_indev, 550);

    return true;
}

// 手势处理
void gesture_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_GESTURE) return;
    
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    
    static uint32_t last_gesture_time = 0;
    uint32_t curr_time = lv_tick_get();
    /* 换页冷却 500 ms（曾经为了治"划一下卡 3-5 秒"降到 220 ms，可那是**松手判定**的锅，
     * 不是这里的锅 —— 现在松手快了，冷却就得挡住"手指抖/一次滑动被判两次"，回 500 ms）。 */
    if (curr_time - last_gesture_time < 500) {
        return;
    }
    last_gesture_time = curr_time;

    if (getenv("TFT_TOUCH_DBG")) {
        printf("[touch] 手势 dir=%d（页 %d 列 %d）\n", (int)dir, app.current_page, app.current_col);
        fflush(stdout);
    }
    
    /* 上下划 = 换竖页；左右划 = 换当前竖页里的横屏
     * （只有本页横屏数 > 1 的页才动，也就是「配置了左右界面」的那种） */
    if (dir == LV_DIR_TOP) {
        switch_to_page((app.current_page + 1) % MAX_PAGES);
    }
    else if (dir == LV_DIR_BOTTOM) {
        switch_to_page((app.current_page + MAX_PAGES - 1) % MAX_PAGES);
    }
    else if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        /* 往左划 ⇒ 看右边那一屏；往右划 ⇒ 看左边那一屏。
         * **循环**（与上下划一样"无限"）：最后一屏再往左划就回到第一屏，反之亦然；
         * 循环范围只在**本页内**，不会因为左右划跨到别的竖页去（换竖页仍然只靠上下划）。
         * 横屏数取 page_col_count()：SMS 页的屏数 = 1 + 短信条数，是**动态**的。 */
        int n = page_col_count(app.current_page);

        if (n > 1) {
            int c = app.current_col + (dir == LV_DIR_LEFT ? 1 : -1);

            c = (c % n + n) % n;   /* 负数也归一到 0..n-1 */
            show_page(app.current_page, c);
        }
    }
}

// 信号处理
void signal_handler(int signum) {
    g_signalled = 1;
    app.running = false;
}

// 进度条动画回调
static void progress_anim_cb(void *var, int32_t v) {
    lv_bar_set_value(progress_bar, v, LV_ANIM_OFF);
    
    int status_idx = v / 20;
    if (status_idx >= sizeof(status_texts) / sizeof(status_texts[0])) {
        status_idx = sizeof(status_texts) / sizeof(status_texts[0]) - 1;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %d%%", status_texts[status_idx], v);
}

// 加载完成回调
// LVGL 的 lv_anim_ready_cb_t / lv_anim_completed_cb_t 是单参数：
//   typedef void (*lv_anim_completed_cb_t)(lv_anim_t *);
// 原来写成 (void *, int32_t) 属于类型不匹配（真实 UB）。GCC 13 只报警告，
// GCC 14 把 -Wincompatible-pointer-types 变成错误，所以这里必须改对。
static void load_complete_cb(lv_anim_t *a) {
	(void)a;
    lv_task_handler();
    lv_tick_inc(500);
    lv_task_handler();
    
    switch_to_main_screen();
}

// 电池更新回调
// 右上角电量 + 充电图标 + 适配器报错：**全部本地读 ADC**（口径见 utils/battery.c，与原版一致）
static void update_Battery_cb(lv_timer_t *timer){
    static int last_pct = -2, last_chg = -1, last_err = -1;

    int pct = battery_percent();                        /* 电池：in_voltage3_raw ×5.95 */
    int chg = battery_charging() ? 1 : 0;               /* 适配器：in_voltage2_raw ×11 ≥ 11V */
    int err = battery_charge_error() ? 1 : 0;           /* 适配器 4.5~10V = 不正常 */

    /* 电量：没电池（或读不到）时显示 "--"，并清掉图标 —— 画个空电池会被当成 "0% / 坏了" */
    if (pct != last_pct || chg != last_chg) {
        last_pct = pct;
        last_chg = chg;
        if (pct < 0) {
            lv_label_set_text(battery_icon, "");
            lv_label_set_text(label_Battery, "--");
            lv_label_set_text(Charge_icon, "");
        } else {
            BATTERY(pct);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(label_Battery, buf);
            lv_label_set_text(Charge_icon, chg ? LV_SYMBOL_CHARGE : "");
        }
    }

    /* 适配器电压异常（原版行为：4.5~10 V 弹 "Power [xxV] ERROR"） */
    if (err != last_err) {
        last_err = err;
        if (err) {
            char buf[28];
            snprintf(buf, sizeof(buf), "Power [%.0fV] \n  ERROR", battery_charge_mv() / 1000.0);
            lv_label_set_text(label_error, buf);
            lv_obj_clear_flag(error_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(label_error, "");
            lv_obj_add_flag(error_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// 局域网客户端数（来自 WebSocket 推送）
static void update_DHCP_cb(lv_timer_t *timer){
    ws_snapshot_t s;
    char label_text[12];

    wsdata_snapshot(&s);

    /* 服务端没报过客户端数（modemd 不提供）就别显示 0 —— 那看起来像“真的没有客户端” */
    if (s.clients < 0) {
        lv_label_set_text(label_Charge, "--");
        return;
    }

    snprintf(label_text, sizeof(label_text), "%d", s.clients);
    lv_label_set_text(label_Charge, label_text);
}

// 4G/5G 指示灯：跟着 WebSocket 报上来的制式走
static void update_led_cb(lv_timer_t *timer){
    static char last_rat[16] = "";
    ws_snapshot_t s;

    wsdata_snapshot(&s);

    if (strcmp(last_rat, s.rat) == 0) {
        return;
    }
    snprintf(last_rat, sizeof(last_rat), "%s", s.rat);

    if (strcmp(s.rat, "LTE") == 0 || strcmp(s.rat, "4G") == 0) {
        Open4Gled();
    } else if (strcmp(s.rat, "NR") == 0 || strcmp(s.rat, "5G") == 0) {
        Open5Gled();
    } else if (s.rat[0]) {
        CloseAllLed();
    }
}

// 开机画面初始化
void boot_screen_init(void) {
    // 创建开机屏幕
    boot_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_screen, lv_color_hex(0x1E1E1E), 0);
    
    // 创建logo文本
    lv_obj_t *logo_label = lv_label_create(boot_screen);
    lv_obj_set_style_text_font(logo_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(logo_label, "H29K");
    lv_obj_align(logo_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(logo_label, lv_color_hex(0xFFFFFF), 0);

    /* 版本号（用户要求："在启动页面添加版本号信息，不然不知道屏幕的版本"）。
     * 字符串是**编译期**塞进二进制的 ⇒ 屏幕上写的就是这一包，不用再去设备上
     * `opkg list-installed` 或者看后台。⚠️ 只有 ASCII：这个页没引 CJK 字库。 */
    lv_obj_t *ver_label = lv_label_create(boot_screen);
    lv_obj_set_style_text_font(ver_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ver_label, TFT_VERSION_LINE);
    lv_obj_set_style_text_color(ver_label, lv_color_hex(0xE8E8E8), 0);
    /* 用 align_to 串在 logo 下面：以后 logo 换字号也不会和版本号压在一起 */
    lv_obj_align_to(ver_label, logo_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_obj_t *build_label = lv_label_create(boot_screen);
    /* 10px 在 320×172 的屏上太小（实测截图），12px 才看得清 —— 这行的用处就是报身份 */
    lv_obj_set_style_text_font(build_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(build_label, TFT_BUILDDATE_LINE);   /* 打包那一刻，用来区分同号的测试包 */
    lv_obj_set_style_text_color(build_label, lv_color_hex(0x8A8A8A), 0);
    lv_obj_align_to(build_label, ver_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // 创建进度条
    progress_bar = lv_bar_create(boot_screen);
    lv_obj_set_size(progress_bar, 240, 8);
    lv_obj_align(progress_bar, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xFF3333), LV_PART_INDICATOR);
    
    // 创建状态标签
    // status_label = lv_label_create(boot_screen);
    // lv_label_set_text(status_label, "0%");
    // lv_obj_align_to(status_label, progress_bar, LV_ALIGN_OUT_TOP_MID, -99999, 0);
    // lv_obj_set_style_text_color(status_label, lv_color_hex(0xCCCCCC), 0);
    // lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    
    // 显示开机画面
    lv_scr_load_anim(boot_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    
    // 进度条动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, progress_bar);
    lv_anim_set_exec_cb(&a, progress_anim_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 6000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, load_complete_cb);
    lv_anim_start(&a);
}

// 主界面初始化
void main_screen_init(void) {    // 创建主屏幕
    main_screen = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(main_screen, 0, 0);
    lv_obj_set_style_bg_color(main_screen, lv_color_black(), 0);
    lv_obj_set_style_pad_top(main_screen, 30, 0);

    // 背景样式（标题块与右边那几个块共用）
    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0x424242));
    lv_style_set_radius(&style_bg, 0);
    lv_style_set_border_width(&style_bg, 0);
    lv_style_set_border_color(&style_bg, lv_color_white());

    // 标题块（用户要求：标题里的 logo 符号也一起用块包起来，和右边三个块同款）
    lv_obj_t *title_container = lv_obj_create(main_screen);
    lv_obj_align(title_container, LV_ALIGN_TOP_LEFT, 0, -30);   /* 左边贴屏幕边缘，不留边 */
    /* 宽度：从 0 一直铺到右边图标块前面，只留 1px（用户要求） */
    lv_obj_set_size(title_container, ICONS_LEFT_X - TITLE_GAP_RIGHT, TITLE_BAR_H);
    lv_obj_clear_flag(title_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(title_container, &style_bg, 0);
    lv_obj_set_style_bg_color(title_container, lv_color_hex(0x424242), 0);
    /* 主题默认给的 ~20px 内边距会把这颗块撑得很宽 ⇒ 显式设定 */
    lv_obj_set_style_pad_all(title_container, 0, 0);

    // 创建标题标签（靠左，位置对所有页都一样）
    /* 用户 2026-10-07："头部标题文字和图标小一点，有点大了" ⇒ 24 → **20**（标题栏高 30，24 太满） */
    title = lv_label_create(title_container);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, TITLE_PAD_HOR, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xCCCCCC), 0);

    // 网络状态容器
    lv_obj_t *netstart_container = lv_obj_create(main_screen);
    lv_obj_align(netstart_container, LV_ALIGN_TOP_RIGHT, NET_BOX_RIGHT_OFS, -30);
    lv_obj_set_size(netstart_container, ICON_BOX_W, ICON_BOX_W);
    lv_obj_set_style_bg_color(netstart_container, lv_color_hex(0x424242), 0);
    lv_obj_clear_flag(netstart_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(netstart_container, &style_bg, 0);

    netstart_icon = lv_label_create(netstart_container);
    lv_obj_set_style_text_font(netstart_icon, &lv_font_montserrat_16, 0);   /* 18 → 16（图标调小） */
    lv_obj_align(netstart_icon, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(netstart_icon, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_color(netstart_icon, lv_color_make(0xFF, 0xFF, 0xFF), 0);

    // 充电状态容器
    lv_obj_t *Charge_container = lv_obj_create(main_screen);
    lv_obj_align(Charge_container, LV_ALIGN_TOP_RIGHT, -94, -30);
    lv_obj_set_size(Charge_container, 30,30);
    lv_obj_set_style_bg_color(Charge_container, lv_color_hex(0x424242), 0);
    lv_obj_clear_flag(Charge_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(Charge_container, &style_bg, 0);

    label_Charge = lv_label_create(Charge_container);
    lv_obj_center(label_Charge);
    lv_label_set_text(label_Charge, "--");
    lv_obj_set_style_text_color(label_Charge, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(label_Charge, &lv_font_montserrat_14, 0);   /* 16 → 14（跟着图标调小） */

    // 电池容器（宽一点：图标 + 百分比 + 闪电各占一块，互不遮）
    lv_obj_t *Battery_container = lv_obj_create(main_screen);
    lv_obj_align(Battery_container, LV_ALIGN_TOP_RIGHT, 0, -30);
    lv_obj_set_size(Battery_container, 92,30);
    lv_obj_clear_flag(Battery_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(Battery_container, &style_bg, 0);
    lv_obj_set_style_bg_color(Battery_container, lv_color_hex(0x424242), 0);
    /* ⚠️ 主题会给容器 ~20px 内边距，子对象就整体往右偏 20（实测闪电因此落不到最右边）⇒ 清零 */
    lv_obj_set_style_pad_all(Battery_container, 0, 0);

    // 电池图标（定尺寸 + 居中：定尺后才能算出准确位置）
    /* ⚠️ 盒子尺寸（34×26/@2）别动：闪电贴右那几个位置是按它算出来的；只把**字体**调小 */
    battery_icon = lv_label_create(Battery_container);
    lv_obj_set_style_text_font(battery_icon, &lv_font_montserrat_20, 0);   /* 24 → 20（图标调小） */
    lv_obj_set_size(battery_icon, 34, 26);
    lv_obj_set_style_text_align(battery_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(battery_icon, LV_ALIGN_LEFT_MID, 2, 0);
    /* 没数据时不画电池：画成红色空电池会被当成“0% / 坏了”（数据来了再上色） */
    lv_label_set_text(battery_icon, "");

    // 电量百分比（定尺寸 + 居中，占中间一块）
    label_Battery = lv_label_create(Battery_container);
    lv_obj_set_style_text_font(label_Battery, &lv_font_montserrat_12, 0);
    lv_obj_set_size(label_Battery, 36, 16);
    lv_obj_set_style_text_align(label_Battery, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_Battery, LV_ALIGN_LEFT_MID, 36, 0);
    lv_label_set_text(label_Battery, "--");
    lv_obj_set_style_text_color(label_Battery, lv_color_hex(0xEEEEEE), 0);

    // 充电图标：**贴屏幕最右边**（用户要求：不然会挡住百分比文字）
    Charge_icon = lv_label_create(Battery_container);
    lv_obj_set_style_text_font(Charge_icon, &lv_font_montserrat_14, 0);    /* 16 → 14（图标调小） */
    lv_obj_set_size(Charge_icon, 18, 20);
    lv_obj_set_style_text_align(Charge_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(Charge_icon, LV_ALIGN_RIGHT_MID, -2, 1);
    lv_label_set_text(Charge_icon, "");
    lv_obj_set_style_text_color(Charge_icon, lv_color_hex(0xFF6600), 0);

    // 初始化定时器（所有数值都来自 WebSocket 推送）
    lv_timer_create(update_Battery_cb, 2000, NULL);
    lv_timer_create(update_DHCP_cb, 3000, NULL);
    lv_timer_create(update_led_cb, 2000, NULL);
    /* 息屏检查：每秒看一眼"多久没碰屏幕了"（用户要求：不操作 3 分钟息屏、点屏亮屏） */
    lv_timer_create(idle_check_cb, 1000, NULL);

    // 创建页面
    create_home_page(main_screen);
    create_osinfo_page(main_screen);
    create_netinfo_page(main_screen);
    create_brightness_page(main_screen);
    create_wifi_page(main_screen);
    create_lan_page(main_screen);         /* SYSTEM 第三屏：LAN 配置（WIFI 之后、亮度之前） */
    create_idle_page(main_screen);        /* SYSTEM 第五屏：息屏配置（亮度之后） */
    /* create_ip_page() 不再调（NETIP → NET 网络参数页）；旧实现留在 pages/ip_page.c 备用 */
    create_netparam_page(main_screen);
    create_smspage_pages(main_screen);    /* SMS：短信列表 + 每条短信一屏 + 新短信弹窗 */
    create_speedchart_pages(main_screen); /* SPEED 的左右两屏：下行 / 上行 速率 */
    create_syschart_pages(main_screen);   /* INFO 的左右三屏：CPU / 内存 / 温度 趋势 */
    create_sysinfo_page(main_screen);     /* SYSTEM：概览（右划 = WIFI / LAN 配置 / 屏幕亮度） */
    create_mobilebase_page(main_screen);
    create_lunar_page(main_screen);       /* TIME 的第二个横屏：农历 */
    create_netmode_page(main_screen);     /* Mobile 的第三个横屏：组网模式偏好 */

    app.current_page = g_start_page;
    app.current_col  = g_start_col;
    show_page(g_start_page, g_start_col);

    // 错误提示容器
    error_container = lv_obj_create(main_screen);
    lv_obj_align(error_container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(error_container, 320,142);
    lv_obj_set_style_bg_color(error_container, lv_color_hex(0xFF3300), 0);
    lv_obj_clear_flag(error_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(error_container, &style_bg, 0);
    
    label_error = lv_label_create(error_container);
    lv_obj_center(label_error);
    lv_label_set_text(label_error, "ERROR");
    lv_obj_set_style_text_color(label_error, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(label_error, &lv_font_montserrat_24, 0);
    lv_obj_add_flag(error_container, LV_OBJ_FLAG_HIDDEN);

    // 启用手势
    lv_obj_add_event_cb(main_screen, gesture_handler, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);  
    lv_scr_load(main_screen);
    lv_obj_invalidate(main_screen);
}

// 切换到主界面
void switch_to_main_screen(void) {
    lv_obj_clean(boot_screen);

    if (main_screen == NULL) {
        main_screen_init();
    }
    
    lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
}

// 重置到开机画面
void reset_to_boot_screen(void) {
    if (boot_screen == NULL || !lv_obj_is_valid(boot_screen)) {
        boot_screen_init();
    } else {
        lv_obj_clean(boot_screen);
        boot_screen_init();
    }
}



/* -------------------------------------------------------------------------
 * 数据来源已全部改成 WebSocket（utils/wsclient.c + utils/wsdata.c）：
 * 原来的 Unix 域套接字文本协议（/tmp/modemserver_unix_server.sock）整套删除。
 * ------------------------------------------------------------------------- */

// 主函数
/* ⚠️ TZ 为空 = UTC（musl 语义），而 procd 起进程时常常给空 TZ ⇒ 屏幕上时钟会比设备本地时间
 * 差 8 小时（实测：屏幕 10:29 / 设备 18:29 CST）。这里按 OpenWrt 的约定把 /etc/TZ 读回来。 */
static void fix_timezone(void) {
    const char *tz = getenv("TZ");
    if (tz && *tz) {
        printf("tftdisplay: 时区 TZ=%s（环境里已有）\n", tz);
        return;
    }

    char buf[64] = "";
    FILE *f = fopen("/etc/TZ", "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) {
            size_t n = strlen(buf);
            while (n && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                         buf[n-1] == ' '  || buf[n-1] == '\t'))
                buf[--n] = '\0';
        }
        fclose(f);
    }

    if (buf[0]) {
        setenv("TZ", buf, 1);
        tzset();
        printf("tftdisplay: 环境里 TZ 为空，已按 /etc/TZ 设为 %s\n", buf);
    } else {
        printf("tftdisplay: 环境里 TZ 为空且 /etc/TZ 读不到 ⇒ 时间会按 UTC 显示\n");
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* procd 抓的是管道，默认块缓冲 ⇒ logread 里看不到实时状态，这里改成行缓冲 */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* -version / --version / -v：只报版本就退出。
     * 现场排查用：SSH 上去 `tftdisplay -version` 就知道这台屏幕装的是哪一版，
     * 不用等 6 秒开机页、也不用翻 opkg 库（opkg 只反映包，不反映正在跑的那个二进制）。 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-version") == 0 || strcmp(argv[i], "--version") == 0 ||
            strcmp(argv[i], "-v") == 0) {
            printf("tftdisplay %s (%s, LVGL %d.%d.%d)\n",
                   TFT_APP_VERSION, TFT_BUILD_DATE,
                   LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
            return 0;
        }
    }

    printf("tftdisplay: 版本 %s（%s, LVGL %d.%d.%d）\n",
           TFT_APP_VERSION, TFT_BUILD_DATE,
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    fix_timezone();

    // 数据来源：WebSocket。先把地址/令牌准备好（配置文件 + 命令行覆盖）
    ws_cfg_load(argc, argv);
    wsdata_init();

    /* 电池：**本地** ADC（不经服务端/模组）。参数走 TFT_BATT_* 环境变量，
     * 没电池的板子读出来不在合理范围 ⇒ 界面显示 "--"，不编数据。 */
    battery_init();

    /* 息屏时间：默认 3 分钟；**先看用户在小文件里选过的那一挡**（SYSTEM→息屏配置页），
     * 再用 TFT_IDLE_OFF_MS 覆盖（毫秒，0 = 永不熄屏）—— 环境变量留给调试/截图用。 */
    g_idle_sleep_ms = IDLE_SLEEP_MS_DEFAULT;
    {
        FILE *f = fopen(IDLE_STATE_CONF, "r");
        unsigned v = 0;

        if (f) {
            if (fscanf(f, "%u", &v) == 1)
                g_idle_sleep_ms = v;
            fclose(f);
        }
    }
    {
        const char *idle = getenv("TFT_IDLE_OFF_MS");

        if (idle)
            g_idle_sleep_ms = (uint32_t)atoi(idle);
        if (getenv("TFT_NO_SLEEP"))
            g_idle_sleep_ms = 0;
        printf("tftdisplay: 息屏 = %u 毫秒无操作（默认 180000 = 3 分钟；SYSTEM→息屏配置页 或 TFT_IDLE_OFF_MS 可改）\n",
               (unsigned)g_idle_sleep_ms);
    }

    // 调试用：-page N 直接停在某一页（不触摸也能看那一页的数据），-col N 指定横向第几屏
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-page") == 0 && i + 1 < argc) {
            g_start_page = atoi(argv[++i]);
            if (g_start_page < 0 || g_start_page >= MAX_PAGES) {
                g_start_page = 0;
            }
        } else if (strcmp(argv[i], "-col") == 0 && i + 1 < argc) {
            g_start_col = atoi(argv[++i]);
            if (g_start_col < 0 || g_start_col >= MAX_COLS) {
                g_start_col = 0;
            }
        }
    }

    int pin = 1; 
    // 导出 GPIO
    export_gpio(pin);
    // 设置为输出
    set_gpio_direction(pin, "out");
    CloseAllLed();

    lv_init();
    app.display = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(app.display, "/dev/fb0");
    
    if (!init_touch()) {}

    boot_screen_init();

    // 起 WebSocket 线程（登录、握手、收帧、断线重连都在它自己里面）
    {
        const char *src = ws_port_source();

        printf("tftdisplay: ws://%s:%d%s (enabled=%d, user=%s)\n",
               ws_cfg.host, ws_cfg.port, ws_cfg.path, ws_cfg.enabled,
               ws_cfg.user[0] ? ws_cfg.user : "-");
        if (src)
            printf("tftdisplay: 端口 %d 取自 modem 配置 %s 的 [server] listen\n",
                   ws_cfg.port, src);
        else
            printf("tftdisplay: 没读到 modem 配置的 [server] listen，用默认端口 %d\n",
                   ws_cfg.port);
    }
    if (ws_client_start() != 0) {
        fprintf(stderr, "ws_client_start failed\n");
        return 1;
    }

    // 主循环
    app.running = true;
    if (g_signalled) {
        app.running = false;    /* 启动阶段已经被要求退出了 */
    }
    while (app.running) {
        lv_timer_handler();
        usleep(100);
    }

    ws_client_stop();

    // 清理
    if (app.fd_touch >= 0) {
        close(app.fd_touch);
    }

    return 0;
}
