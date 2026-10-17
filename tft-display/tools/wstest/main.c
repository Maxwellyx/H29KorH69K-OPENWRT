/*
 * wstest —— 不带 LVGL 的控制台版：跑同一套 WebSocket 客户端与数据模型，
 * 每秒把快照打出来，用来在真机上验证取数链路（登录 / 握手 / 帧 / JSON / AT 解析）。
 *
 * 交叉编译（Windows 上）：
 *   zig cc -target aarch64-linux-musl -static -Os -I src -I src/lvgl \
 *       -o build-aarch64/wstest tools/wstest/main.c \
 *       src/utils/tinyjson.c src/utils/wsclient.c src/utils/wsdata.c -lpthread
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../src/utils/wsclient.h"
#include "../../src/utils/wsdata.h"

int main(int argc, char **argv)
{
    ws_snapshot_t s;
    int i;

    ws_cfg_load(argc, argv);
    wsdata_init();

    printf("wstest: ws://%s:%d%s enabled=%d user=%s token=%s\n",
           ws_cfg.host, ws_cfg.port, ws_cfg.path, ws_cfg.enabled,
           ws_cfg.user[0] ? ws_cfg.user : "-",
           ws_cfg.token[0] ? "(set)" : "-");
    fflush(stdout);

    if (ws_client_start() != 0) {
        printf("ws_client_start failed\n");
        return 1;
    }

    for (i = 0; i < 20; i++) {
        sleep(1);
        wsdata_snapshot(&s);
        printf("--- %2ds  online=%d msgs=%u state=%s\n", i + 1, s.online, s.msgs, s.state);
        printf("    mac=%s\n", s.mac);
        printf("    ips=[%s] up=%d hasip=%d conn=%d\n", s.ips, s.ip_up, s.ip_hasip, s.ip_connected);
        printf("    dev=%s total=%s/%s rate=%s/%s (%.0f/%.0f B/s)\n",
               s.dev, s.total_recv, s.total_sent, s.recv_rate, s.sent_rate, s.recv_bps, s.sent_bps);
        printf("    cops=%s rat=%s mcc=%s model=%s\n", s.cops, s.rat, s.mcc, s.model);
        printf("    cpu=%d mem=%d temp=%d clients=%d batt=%d%% battv=%.2f chgv=%.2f err=%d\n",
               s.cpu, s.mem, s.temp, s.clients, s.batt_pct, s.batt_voltage, s.chg_voltage, s.chg_error);
        printf("    ssid=%s key=%s\n", s.ssid, s.wifikey);
        fflush(stdout);
    }

    ws_client_stop();
    return 0;
}
