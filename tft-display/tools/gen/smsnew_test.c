/*
 * SMS 列表推送的回归测试：**"删掉最新那条"不能被当成"来了新短信"**（否则屏幕会弹新短信窗）。
 *
 * 用户 2026-10-07 报的现象："SMS 点击删除短信后会弹出新短信的窗口"。
 * 根因：wsdata.c 的 on_md_sms() 原先拿"最新一条（号码+时间+正文）跟上一份的最新那条比"，
 *       删掉最新那条之后新头部变成原来第二条 ⇒ 三个字段都不同 ⇒ 误判成新短信。
 * 现在改成"新头部这条在上一份列表里**找不到**才算新"。
 *
 * 用例：A(3条) → 再推 A → 删掉最新（推 A[1..2]）→ 真来一条新的（推 NEW+A[1..2]）。
 *
 * 编译（Windows 上直接跑）：
 *   zig cc -o smsnew_test.exe smsnew_test.c ..\..\src\utils\wsdata.c ..\..\src\utils\tinyjson.c
 */
#include "../../src/utils/wsdata.h"

#include <stdio.h>
#include <string.h>

static int fails;

/* ⚠️ 每条用**各自的** buffer：写成一个 static buf 被多次调用会在 printf 实参里
 * 全部指向同一块内存（只有最后那个调用写的内容生效）——第一版就踩了，
 * 结果清单里三条变成同一条，判据自然全错。 */
static const char *MSGUS(int slot, int idx, const char *num, const char *ts,
                         const char *txt, const char *status)
{
    static char buf[8][512];

    snprintf(buf[slot % 8], sizeof(buf[0]),
             "{\"index\":\"%d\",\"number\":\"%s\",\"timestamp\":\"%s\","
             "\"content\":\"%s\",\"status\":\"%s\"}", idx, num, ts, txt, status);
    return buf[slot % 8];
}

static const char *MSG(int slot, int idx, const char *num, const char *ts, const char *txt)
{
    return MSGUS(slot, idx, num, ts, txt, "1");        /* 1 = 已读（REC READ） */
}

static void feed(const char *list)
{
    char frame[2048];

    snprintf(frame, sizeof(frame), "{\"type\":\"sms\",\"modem_id\":0,\"data\":%s}", list);
    wsdata_on_message(frame);
}

static void check(const char *what, unsigned got, unsigned want)
{
    if (got != want) {
        printf("  FAIL %-34s new_seq=%u want=%u\n", what, got, want);
        fails++;
    } else {
        printf("  ok   %-34s new_seq=%u\n", what, got);
    }
}

int main(void)
{
    ws_snapshot_t s;
    char list[1600];

    wsdata_init();

    /* 第 1 条（10086，最新），第 2 条（10000），第 3 条（朋友） */
    snprintf(list, sizeof(list), "[%s,%s,%s]",
             MSG(0, 1, "10086", "26/10/07,20:00:00+32", "话费余额 12.34 元"),
             MSG(1, 2, "10000", "26/10/07,19:00:00+32", "欢迎使用"),
             MSG(2, 3, "13800000000", "26/10/07,18:00:00+32", "在吗"));

    printf("1) 首次收到列表（只建基线，不该弹窗）\n");
    feed(list);
    wsdata_snapshot(&s);
    check("first list", s.sms_new_seq, 0);

    printf("2) 同一份列表再推一遍（没有任何新东西）\n");
    feed(list);
    wsdata_snapshot(&s);
    check("same list again", s.sms_new_seq, 0);

    printf("3) 删掉最新那条（新头部 = 原第二条）← 用户报的 bug\n");
    snprintf(list, sizeof(list), "[%s,%s]",
             MSG(0, 2, "10000", "26/10/07,19:00:00+32", "欢迎使用"),
             MSG(1, 3, "13800000000", "26/10/07,18:00:00+32", "在吗"));
    feed(list);
    wsdata_snapshot(&s);
    check("delete newest (no popup!)", s.sms_new_seq, 0);

    printf("4) 真的来了一条新短信\n");
    snprintf(list, sizeof(list), "[%s,%s,%s]",
             MSG(0, 9, "10001", "26/10/07,21:00:00+32", "您的余额为 8.88 元"),
             MSG(1, 2, "10000", "26/10/07,19:00:00+32", "欢迎使用"),
             MSG(2, 3, "13800000000", "26/10/07,18:00:00+32", "在吗"));
    feed(list);
    wsdata_snapshot(&s);
    check("real new sms (must popup)", s.sms_new_seq, 1);

    printf("5) 新短信之后又删掉它\n");
    snprintf(list, sizeof(list), "[%s,%s]",
             MSG(0, 2, "10000", "26/10/07,19:00:00+32", "欢迎使用"),
             MSG(1, 3, "13800000000", "26/10/07,18:00:00+32", "在吗"));
    feed(list);
    wsdata_snapshot(&s);
    check("delete it again", s.sms_new_seq, 1);

    /* ⚠️ 用户第二次报的场景：**长短信**（多段拼接）删掉其中一段之后服务端重拼，
     * 头部还是同一条（号码/时间都没变）但**正文变了** ⇒ 按"内容比对"会误判成新短信。 */
    printf("6) 长短信删掉一段后重拼（头部时间不变、正文变了）\n");
    snprintf(list, sizeof(list), "[%s,%s]",
             MSG(0, 2, "10000", "26/10/07,19:00:00+32", "欢迎使用"),
             MSG(1, 3, "13800000000", "26/10/07,18:00:00+32", "在吗"));
    feed(list);
    wsdata_snapshot(&s);
    check("baseline again", s.sms_new_seq, 1);

    snprintf(list, sizeof(list), "[%s,%s]",
             MSG(0, 2, "10000", "26/10/07,19:00:00+32", "欢迎使用（后半段）"),
             MSG(1, 3, "13800000000", "26/10/07,18:00:00+32", "在吗"));
    feed(list);
    wsdata_snapshot(&s);
    check("long sms re-merged (no popup)", s.sms_new_seq, 1);

    printf("7) 全部删空\n");
    feed("[]");
    wsdata_snapshot(&s);
    check("empty list", s.sms_new_seq, 1);

    printf("8) 清空后来一条新的（应该弹）\n");
    snprintf(list, sizeof(list), "[%s]",
             MSG(0, 5, "10086", "26/10/07,22:00:00+32", "话费余额 5.00 元"));
    feed(list);
    wsdata_snapshot(&s);
    check("first sms after empty", s.sms_new_seq, 2);

    /* ⚠️ 真机上这条 10001 的流量提醒是 part_total=3 的长短信，删一段后重拼：
     * 头部 index/时间都没变、正文变了、未读数也没变 ⇒ 绝不能弹。 */
    printf("9) 长短信重拼（同时间戳、正文变了、未读数不变）\n");
    snprintf(list, sizeof(list), "[%s,%s]",
             MSG(0, 5, "10001", "26/03/08,13:45:18+32", "已使用61440MB。详情如下：【本地流量-60GB】"),
             MSG(1, 3, "10001", "26/03/07,14:10:24+32", "使用49280.6M"));
    feed(list);
    wsdata_snapshot(&s);
    check("long-sms baseline", s.sms_new_seq, 2);

    snprintf(list, sizeof(list), "[%s,%s]",
             MSG(0, 4, "10001", "26/03/08,13:45:18+32", "http://a.189.cn/nfau2J 回复“流量”查询详情。"),
             MSG(1, 3, "10001", "26/03/07,14:10:24+32", "使用49280.6M"));
    feed(list);
    wsdata_snapshot(&s);
    check("re-merged (no popup!)", s.sms_new_seq, 2);

    printf("10) 新短信但时间戳跟原来一样（靠未读数变多认出来，应弹）\n");
    snprintf(list, sizeof(list), "[%s,%s]",
             MSGUS(0, 9, "10001", "26/03/08,13:45:18+32", "您的余额为 8.88 元", "REC UNREAD"),
             MSG(1, 3, "10001", "26/03/07,14:10:24+32", "使用49280.6M"));
    feed(list);
    wsdata_snapshot(&s);
    check("unread grew (must popup)", s.sms_new_seq, 3);

    /* 长短信各段的存储位置要解析出来（删的时候必须逐段删，否则留下孤儿段、看着像删不掉） */
    printf("11) 长短信 parts[] 解析（用真机抓下来的那帧的 shape）\n");
    snprintf(list, sizeof(list),
             "[{\"index\":\"5\",\"status\":\"REC READ\",\"number\":\"10001\","
             "\"timestamp\":\"26/10/07,23:00:00+32\",\"content\":\"已使用61440MB\","
             "\"part_ref\":156,\"part_total\":3,\"part_seq\":2,"
             "\"parts\":[{\"index\":\"5\",\"seq\":2},{\"index\":\"4\",\"seq\":3}]}]");
    feed(list);
    wsdata_snapshot(&s);
    if (s.sms_n == 1 && s.sms[0].part_n == 2 &&
        strcmp(s.sms[0].part_idx[0], "5") == 0 && strcmp(s.sms[0].part_idx[1], "4") == 0) {
        printf("  ok   parts parsed                    n=%d parts=%s,%s\n",
               s.sms[0].part_n, s.sms[0].part_idx[0], s.sms[0].part_idx[1]);
    } else {
        printf("  FAIL parts parsed  n=%d part_n=%d [%s][%s]\n", s.sms_n, s.sms[0].part_n,
               s.sms[0].part_idx[0], s.sms[0].part_idx[1]);
        fails++;
    }

    printf(fails ? "\n== FAILED (%d) ==\n" : "\n== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
