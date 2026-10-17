/* 组网模式推送的解析自检（**不进固件**，只在 PC 上跑）。
 *
 *   zig cc -I src/utils -o %TEMP%\modeparse.exe tools/gen/modeparse_test.c src/utils/wsdata.c src/utils/tinyjson.c
 *
 * 为什么要它：屏幕端曾经用 tj_get_int 去读 JSON 的 writable（**布尔** true/false，
 * tj_get_int 只认数字）⇒ 三个挡位全被判成"不可写"，点上去只回一句"本机不支持这一档"。
 * 这个测试就是把服务端真实发的那份 JSON 喂给 wsdata，断言挡位解析正确。
 */
#include <stdio.h>
#include <string.h>

#include "wsdata.h"

static int fails;

/* PC 上没有 pthread（Windows 目标的 zig cc 不带给 pthread.h）：
 * 用 -include tools/gen/pthread_shim.h 把它垫掉（见同目录的说明）。 */

/* 服务端（corev2/displaymode.go）真实发的样子（从真机 /display/ws 抓的） */
static const char *FRAME =
    "{\"type\":\"mode\",\"modem_id\":0,\"at\":1791364806181,\"data\":{"
    "\"value\":\"AUTO\",\"name\":\"自动（2G/3G/4G/5G）\",\"short\":\"自动\","
    "\"choices\":[{\"value\":\"AUTO\",\"name\":\"自动\",\"writable\":true}],"
    "\"screen\":["
    "{\"value\":\"LTE\",\"name\":\"4G\",\"writable\":true,\"source\":\"屏幕挡位\"},"
    "{\"value\":\"NR5G\",\"name\":\"5G\",\"writable\":true,\"source\":\"屏幕挡位\"},"
    "{\"value\":\"LTE:NR5G\",\"name\":\"4G+5G\",\"writable\":true,\"source\":\"屏幕挡位\"}"
    "]}}";

int main(void)
{
    ws_mode_t m;

    wsdata_init();
    wsdata_on_message(FRAME);

    if (!wsdata_mode(&m)) {
        printf("FAIL: wsdata_mode() 说没有数据\n");
        return 1;
    }

    if (m.mode_btn_n != 3) {
        printf("FAIL: 挡位个数 = %d（应为 3）\n", m.mode_btn_n);
        fails++;
    }
    if (strcmp(m.mode_value, "AUTO") != 0) {
        printf("FAIL: 当前值 = %s（应为 AUTO）\n", m.mode_value);
        fails++;
    }
    if (strcmp(m.mode_short, "自动") != 0) {
        printf("FAIL: 短名 = %s（应为 自动）\n", m.mode_short);
        fails++;
    }
    for (int i = 0; i < m.mode_btn_n; i++) {
        printf("挡位 %d: value=%-8s name=%-6s writable=%d\n", i + 1,
               m.mode_btn[i].value, m.mode_btn[i].name, m.mode_btn[i].writable);
        if (!m.mode_btn[i].writable) {
            printf("FAIL: 第 %d 档应该是可写的\n", i + 1);
            fails++;
        }
    }

    /* 再来一份带 read_only + msg 的（只读机型：三个挡位都必须判成不可写） */
    wsdata_init();
    wsdata_on_message(
        "{\"type\":\"mode\",\"data\":{\"value\":\"LTE 32\",\"read_only\":true,\"msg\":\"本机只能读\","
        "\"screen\":[{\"value\":\"LTE\",\"name\":\"4G\",\"writable\":false},"
        "{\"value\":\"NR5G\",\"name\":\"5G\",\"writable\":false},"
        "{\"value\":\"LTE:NR5G\",\"name\":\"4G+5G\",\"writable\":false}]}}");
    if (!wsdata_mode(&m)) {
        printf("FAIL: 第二份也没有数据\n");
        fails++;
    } else {
        if (!m.mode_readonly) {
            printf("FAIL: read_only 没解析出来\n");
            fails++;
        }
        for (int i = 0; i < m.mode_btn_n; i++)
            if (m.mode_btn[i].writable) {
                printf("FAIL: 只读机型的第 %d 档不该可写\n", i + 1);
                fails++;
            }
        printf("只读机型: readonly=%d msg=%s（挡位全不可写 ✓）\n", m.mode_readonly, m.mode_msg);
    }

    printf("%s\n", fails ? "**有失败**" : "全部通过");
    return fails ? 1 : 0;
}
