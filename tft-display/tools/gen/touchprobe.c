/*
 * touchprobe —— 触摸屏"实际怎么上报"的现场探针（aarch64 静态，直接丢到设备上跑）。
 *
 * 用途：用户 2026-10-08 报"别人装完划一下卡 3-5 秒"。根因是**松手判定**按屏型分档，
 * 而"这块屏到底报不报 BTN_TOUCH / 抬手事件发不发"必须现场看才知道。
 * 这个工具同时看两件事（互相对照，就能立刻分清屏型）：
 *   ① 事件流：按/抬/坐标 的原始 input_event（等价于 TFT_TOUCH_DBG=1，但不用改 init 脚本）；
 *   ② 内核**当前**按键位图：`EVIOCGKEY` 里 BTN_TOUCH 那一位（= 新版松手判定用的那条路）。
 *      ⇒ 如果"事件里没有 BTN_TOUCH，但位图会跟着手指变 0/1"，说明 **EVIOCGKEY 那条路可用**；
 *        如果位图一直是 0（驱动压根不维护它），那就只能靠超时兜底。
 *
 * 编译（Windows，zig 交叉编 aarch64 静态）：
 *   zig cc -target aarch64-linux-musl -static -Os -o touchprobe touchprobe.c
 * 跑（设备上，可同时开屏幕程序 —— evdev 支持多客户端，不抢设备）：
 *   ./touchprobe /dev/input/event0      # Ctrl+C 结束
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 读内核当前按键位图里的 BTN_TOUCH：0 = 没按着。返回 0 = 查到了，-1 = 查不了。 */
static int btn_touch_bit(int fd, int *down)
{
    unsigned long bits[(BTN_TOUCH / (8 * (int)sizeof(unsigned long))) + 1];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0)
        return -1;
    *down = (int)((bits[BTN_TOUCH / (8 * (int)sizeof(unsigned long))] >>
                   (BTN_TOUCH % (8 * (int)sizeof(unsigned long)))) & 1UL);
    return 0;
}

/* 能力位：某一位在不在（ioctl 拿到的位图）。
 * ⚠️ `EVIOCGBIT(type, len)` 的 len 是**位图字节数**：传 0 一个位都不会拷回来（恒为 0）。 */
static int cap_bit(int fd, int evtype, unsigned int bit)
{
    unsigned long bits[(bit / (8 * (int)sizeof(unsigned long))) + 1];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(evtype, sizeof(bits)), bits) < 0)
        return -1;
    return (int)((bits[bit / (8 * (int)sizeof(unsigned long))] >>
                  (bit % (8 * (int)sizeof(unsigned long)))) & 1UL);
}

static const char *ev_name(unsigned short type, unsigned short code)
{
    static char b[48];

    switch (type) {
    case EV_KEY:
        if (code == BTN_TOUCH) return "BTN_TOUCH";
        snprintf(b, sizeof(b), "KEY/%u", code);
        return b;
    case EV_ABS:
        switch (code) {
        case ABS_X: return "ABS_X";
        case ABS_Y: return "ABS_Y";
        case ABS_MT_SLOT: return "ABS_MT_SLOT";
        case ABS_MT_POSITION_X: return "ABS_MT_POSITION_X";
        case ABS_MT_POSITION_Y: return "ABS_MT_POSITION_Y";
        case ABS_MT_TRACKING_ID: return "ABS_MT_TRACKING_ID";
        default:
            snprintf(b, sizeof(b), "ABS/%u", code);
            return b;
        }
    case EV_SYN:
        if (code == SYN_REPORT) return "SYN_REPORT";
        if (code == SYN_DROPPED) return "SYN_DROPPED";
        snprintf(b, sizeof(b), "SYN/%u", code);
        return b;
    default:
        snprintf(b, sizeof(b), "type%u/%u", type, code);
        return b;
    }
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/dev/input/event0";
    int fd, last_bit = -1, n_events = 0;
    long long t0 = now_ms();

    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }

    printf("== touchprobe %s ==\n", path);
    printf("能力位 BTN_TOUCH(330)=%d  ABS_MT_SLOT(47)=%d  ABS_MT_TRACKING_ID(57)=%d\n",
           cap_bit(fd, EV_KEY, BTN_TOUCH),
           cap_bit(fd, EV_ABS, ABS_MT_SLOT),
           cap_bit(fd, EV_ABS, ABS_MT_TRACKING_ID));
    {
        int down = -1;
        int r = btn_touch_bit(fd, &down);

        printf("EVIOCGKEY 可用=%s  BTN_TOUCH 当前=%s\n", r == 0 ? "是" : "否(!=errno)",
               r == 0 ? (down ? "1(按着)" : "0(没按)") : "-");
        fflush(stdout);
    }
    printf("---- 现在请在屏幕上点几下 / 划几下（下面实时打印）----\n");

    for (;;) {
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        int pr = poll(&pf, 1, 100);          /* 100ms 一拍：没事件也要抽查按键位图 */
        int down = -1;

        if (pr > 0 && (pf.revents & POLLIN)) {
            struct input_event ev;

            while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_SYN && ev.code == SYN_REPORT)
                    continue;                 /* 收尾包不打印，日志干净些 */
                printf("[%6lldms] %-20s = %4d\n", now_ms() - t0, ev_name(ev.type, ev.code), ev.value);
                if (++n_events % 40 == 0)
                    fflush(stdout);
            }
            fflush(stdout);
        }

        /* 按键位图的变化单独打一行（这是"松手判定"真正会用到的那条路） */
        if (btn_touch_bit(fd, &down) == 0 && down != last_bit) {
            printf("           ^^ EVIOCGKEY BTN_TOUCH = %d\n", down);
            fflush(stdout);
            last_bit = down;
        }
    }
    return 0;
}
