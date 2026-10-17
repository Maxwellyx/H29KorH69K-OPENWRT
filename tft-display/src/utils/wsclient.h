#ifndef WSCLIENT_H
#define WSCLIENT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * wsclient —— 屏幕端唯一的取数通道：连 modemserver 的 WebSocket 推送。
 *
 * 地址（主机/端口/路径）与**通讯钥匙**都写在代码里，屏幕上不需要任何配置：
 *   本机 127.0.0.1:8080 的 /display/ws?key=<WS_SHARED_KEY>
 *   —— 端口由服务端那一侧决定（服务端 ListenAndServe :8080），屏幕只管连。
 *
 * 服务端对应实现：modemserver 的 core/service/displayws.go
 *   （不要密码、不要登录、不要 JWT，只比对这把定死的钥匙）
 *
 * ⚠️ 改钥匙必须两边一起改：
 *   · 服务端  core/service/displayws.go 的 DisplayWSKey
 *   · 屏幕端  本文件的 WS_SHARED_KEY
 *
 * 链路：TCP → GET /display/ws?key=… Upgrade → 收文本帧 → 交给 wsdata 解析。
 * 断线自动重连；空闲发 ping 保活；全程跑在自己的线程里，不阻塞 LVGL。
 *
 * 兜底（只在调试时用命令行指定，配置文件里不提供）：
 *   -token <jwt> / -user <u> -pass <p>  → 改走带鉴权的 /ws
 *   -ws <host[:port]>                   → 换服务器地址
 */

/* 双方定死的通讯钥匙。必须与服务端 DisplayWSKey 一字不差。 */
#define WS_SHARED_KEY "h29k-tft-9f3c1a7b"

/* 连接参数（编译期定死，屏幕端不必配置） */
#define WS_DEFAULT_HOST "127.0.0.1"
#define WS_DEFAULT_PATH "/display/ws"

/*
 * 端口**不猜**：从 modem 那一侧的配置文件里读。
 * modemd 用它自己的 `[server] listen` 监听，屏幕就读同一个键：
 *
 *   [server]
 *   listen = "0.0.0.0:8080"     → 屏幕连 127.0.0.1:8080
 *
 * 读取顺序（第一个读得到的算数）：
 *   1. 环变量 TFT_MODEM_CONFIG 指定的文件（调试用）
 *   2. /etc/modemd/config.toml
 *   3. /etc/modemserver/config.toml
 * 读不到就用下面的默认端口（会打印一行说明，不静默猜）。
 * 每次重连前都重读：对方把端口改了，屏幕不用重启。
 */
#define WS_DEFAULT_PORT 8080

typedef struct {
    int  enabled;
    char host[64];
    int  port;
    char path[64];
    char key[128];      /* 定死的通讯钥匙（默认就是 WS_SHARED_KEY） */
    char token[1024];   /* 调试用：配了就直接走 /ws?token= */
    char user[64];      /* 调试用：没钥匙时用它 + password 调 /api/login */
    char pass[64];
    int  reconnect_sec;
    int  timeout_sec;
} ws_cfg_t;

extern ws_cfg_t ws_cfg;

/* 端口是从哪个文件读到的（NULL = 没读到，用了默认端口）—— 只给启动日志用 */
const char *ws_port_source(void);

/* 装载默认值（钥匙 + 127.0.0.1:8080/display/ws），只读配置里的开关，
 * 再用命令行参数覆盖（调试用） */
void ws_cfg_load(int argc, char **argv);

/* 起/停后台线程 */
int  ws_client_start(void);
void ws_client_stop(void);

/* 给服务端发一段文本（订阅之类）；没连上返回 false */
bool ws_client_send_text(const char *text);

#ifdef __cplusplus
}
#endif
#endif /* WSCLIENT_H */
