#include "wsclient.h"
#include "tinyjson.h"
#include "wsdata.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MSG_MAX       65536      /* 单帧上限（AT 那一坨也就几 KB） */
#define READ_TIMEOUT  10000      /* 读超时(ms)：超了就发 ping 保活 */
#define MAX_IDLE      6          /* 连续这么多次超时（约 1 分钟）就重连 */

ws_cfg_t ws_cfg = {
    .enabled = 1,
    .host = WS_DEFAULT_HOST,
    .port = WS_DEFAULT_PORT,
    .path = WS_DEFAULT_PATH,
    .key = WS_SHARED_KEY,
    .reconnect_sec = 5,
    .timeout_sec = 5,
};

/* ========================= 端口从 modem 的配置里读（不猜） =========================
 *
 * 端口由 modem 那一侧（modemd）的配置文件决定，屏幕**每次连接前都去读一遍**，
 * 读到什么就连什么：
 *
 *   [server]
 *   listen = "0.0.0.0:8080"      ← 屏幕就是从这里取端口（modemd 也拿它 ListenAndServe）
 *
 * 文件顺序（第一个读得到的算数）：
 *   1. 环境变量 TFT_MODEM_CONFIG 指定的文件（调试用）
 *   2. /etc/modemd/config.toml        ← 设备上 modemd 用的就是它（modemd -config …）
 *   3. /etc/modemserver/config.toml   ← 将来那份若也用配置文件
 * 读不到就退回编译期的默认端口 8080，并**打印一行说明**（不静默猜）。
 * 监听地址不是可连地址时（0.0.0.0 / :: / 空）换成 127.0.0.1（屏幕与服务端同机）。
 */
#define MODEM_CFG_ENV     "TFT_MODEM_CONFIG"
#define MODEM_CFG_DEFAULT "/etc/modemd/config.toml"
#define MODEM_CFG_ALT     "/etc/modemserver/config.toml"

static char g_port_src[96];   /* 端口是从哪个文件读到的（空 = 用了默认值） */
static int  g_port_auto = 1;  /* 1 = 端口从 modem 配置取（相手改了屏幕自动跟上） */
static int  g_host_fixed;     /* 1 = 地址由命令行/配置明确指定，不从配置里覆写 */

const char *ws_port_source(void)
{
    return g_port_src[0] ? g_port_src : NULL;
}

/* 把 "0.0.0.0:8080" / "127.0.0.1:9527" / ":8080" / "8080" 拆成 host + port。
 * 返回 1 = 取到了合法端口。 */
static int parse_listen_value(const char *val, char *host, size_t hn, int *port)
{
    const char *colon = strrchr(val, ':');
    char hbuf[64] = "";
    int p;

    if (colon) {
        size_t n = (size_t)(colon - val);

        if (n >= sizeof(hbuf))
            n = sizeof(hbuf) - 1;
        memcpy(hbuf, val, n);
        hbuf[n] = '\0';
        p = atoi(colon + 1);
    } else {
        p = atoi(val);
    }

    if (p <= 0 || p > 65535)
        return 0;

    /* 监听地址不是可连地址：屏幕跟服务端同机，用回环地址 */
    if (hbuf[0] == '\0' || strcmp(hbuf, "0.0.0.0") == 0 || strcmp(hbuf, "::") == 0 ||
        strcmp(hbuf, "*") == 0)
        snprintf(host, hn, "%s", "127.0.0.1");
    else
        snprintf(host, hn, "%s", hbuf);

    *port = p;
    return 1;
}

/* 从一份 TOML 里读 [server] 段的 listen（或 port）。读到返回 1。 */
static int parse_modem_cfg_file(const char *path, char *host, size_t hn, int *port)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int in_server = 0;

    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = line, *eq;
        char key[32], val[128];
        size_t o = 0;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        if (*p == '[') {                    /* 段头 */
            in_server = (strncmp(p, "[server]", 8) == 0);
            continue;
        }
        if (!in_server)
            continue;

        eq = strchr(p, '=');
        if (!eq)
            continue;

        {
            size_t n = (size_t)(eq - p);

            while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t'))
                n--;
            if (n == 0 || n >= sizeof(key))
                continue;
            memcpy(key, p, n);
            key[n] = '\0';
        }
        if (strcmp(key, "listen") != 0 && strcmp(key, "port") != 0)
            continue;

        /* 取 value（带引号/不带引号都认） */
        p = eq + 1;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '"' || *p == '\'') {
            char q = *p++;

            while (*p && *p != q && o + 1 < sizeof(val))
                val[o++] = *p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' &&
                   *p != '#' && o + 1 < sizeof(val))
                val[o++] = *p++;
        }
        val[o] = '\0';

        if (parse_listen_value(val, host, hn, port)) {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

/* 依次试几个已知位置，读到就用它，并把来源记下来 */
static int port_from_modem(char *host, size_t hn, int *port)
{
    const char *env = getenv(MODEM_CFG_ENV);
    const char *paths[3];
    int n = 0;

    if (env && env[0])
        paths[n++] = env;
    paths[n++] = MODEM_CFG_DEFAULT;
    paths[n++] = MODEM_CFG_ALT;

    for (int i = 0; i < n; i++) {
        if (parse_modem_cfg_file(paths[i], host, hn, port)) {
            snprintf(g_port_src, sizeof(g_port_src), "%s", paths[i]);
            return 1;
        }
    }

    g_port_src[0] = '\0';
    return 0;
}

/* 定一次地址：能读到 modem 配置就用它的，读不到用默认值 */
static void port_resolve(void)
{
    char h[64];
    int p = 0;

    if (port_from_modem(h, sizeof(h), &p)) {
        ws_cfg.port = p;
        if (!g_host_fixed)
            snprintf(ws_cfg.host, sizeof(ws_cfg.host), "%s", h);
    } else {
        ws_cfg.port = WS_DEFAULT_PORT;
        if (!g_host_fixed)
            snprintf(ws_cfg.host, sizeof(ws_cfg.host), "%s", WS_DEFAULT_HOST);
    }
}

/* ------------------------------------------------------------------ base64 */

static const char b64tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(const unsigned char *in, size_t len, char *out)
{
    size_t i = 0, o = 0;

    while (i + 2 < len) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
        out[o++] = b64tab[(v >> 18) & 63];
        out[o++] = b64tab[(v >> 12) & 63];
        out[o++] = b64tab[(v >> 6) & 63];
        out[o++] = b64tab[v & 63];
        i += 3;
    }
    if (i < len) {
        unsigned v = (unsigned)in[i] << 16;
        int rem = (int)(len - i);
        if (rem == 2)
            v |= (unsigned)in[i + 1] << 8;
        out[o++] = b64tab[(v >> 18) & 63];
        out[o++] = b64tab[(v >> 12) & 63];
        out[o++] = (rem == 2) ? b64tab[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
}

static void ws_random(void *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = 0;

    if (f) {
        got = fread(buf, 1, len, f);
        fclose(f);
    }
    if (got < len) {
        unsigned char *p = (unsigned char *)buf;
        unsigned seed = (unsigned)(time(NULL) ^ (getpid() << 8));
        for (size_t i = got; i < len; i++) {
            seed = seed * 1103515245u + 12345u;
            p[i] = (unsigned char)(seed >> 16);
        }
    }
}

/* ------------------------------------------------------------------ 时间 */

static const char *find_ci_local(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle)
        return NULL;

    for (; *hay; hay++) {
        const char *h = hay, *n = needle;

        while (*h && *n) {
            char a = *h, b = *n;

            if (a >= 'A' && a <= 'Z')
                a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z')
                b = (char)(b + 32);
            if (a != b)
                break;
            h++;
            n++;
        }
        if (!*n)
            return hay;
    }
    return NULL;
}

static void sleep_ms(int ms)
{
    while (ms > 0) {
        int chunk = ms > 200 ? 200 : ms;
        struct timespec ts;
        ts.tv_sec = chunk / 1000;
        ts.tv_nsec = (long)(chunk % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        ms -= chunk;
    }
}

/* ---------------------------------------------------------------- socket */

static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* 1 可读写 / 0 超时 / -1 出错 */
static int wait_io(int fd, int for_write, int ms)
{
    fd_set f;
    struct timeval tv;
    int r;

    FD_ZERO(&f);
    FD_SET(fd, &f);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    r = select(fd + 1, for_write ? NULL : &f, for_write ? &f : NULL, NULL, &tv);
    if (r > 0)
        return 1;
    if (r == 0)
        return 0;
    return -1;
}

static int tcp_connect(const char *host, int port, int timeout_sec)
{
    char portstr[16];
    struct addrinfo hints, *res = NULL, *rp;
    int fd = -1;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return -1;

    for (rp = res; rp; rp = rp->ai_next) {
        int rc;
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;

        set_nonblock(fd);
        rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0)
            break;

        if (errno == EINPROGRESS) {
            int r = wait_io(fd, 1, timeout_sec * 1000);
            if (r == 1) {
                int soerr = 0;
                socklen_t l = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &l) == 0 && soerr == 0)
                    break;
            }
        }
        close(fd);
        fd = -1;
    }

    if (res)
        freeaddrinfo(res);
    return fd;
}

static int send_all(int fd, const void *buf, size_t len, int timeout_ms)
{
    const unsigned char *p = (const unsigned char *)buf;

    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n > 0) {
            p += n;
            len -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (wait_io(fd, 1, timeout_ms) != 1)
                return -1;
            continue;
        }
        return -1;
    }
    return 0;
}

/* 带残留缓冲的连接：握手时多读到的字节会留在这里，帧解析接着用 */
typedef struct {
    int fd;
    unsigned char lb[4096];
    size_t head, tail;
} wsc_t;

/* 1 有数据 / 0 超时 / -1 断开或出错 */
static int wsc_fill(wsc_t *c, int ms)
{
    for (;;) {
        int r;
        ssize_t n;

        if (c->head < c->tail)
            return 1;

        r = wait_io(c->fd, 0, ms);
        if (r == 0)
            return 0;
        if (r < 0)
            return -1;

        n = recv(c->fd, c->lb, sizeof(c->lb), 0);
        if (n > 0) {
            c->head = 0;
            c->tail = (size_t)n;
            return 1;
        }
        if (n == 0)
            return -1;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            continue;
        return -1;
    }
}

/* 0 读到 / 1 超时 / -1 断开 */
static int wsc_read_exact(wsc_t *c, void *out, size_t n, int ms)
{
    unsigned char *p = (unsigned char *)out;

    while (n) {
        int r = wsc_fill(c, ms);
        size_t avail, take;

        if (r != 1)
            return r;

        avail = c->tail - c->head;
        take = avail < n ? avail : n;
        memcpy(p, c->lb + c->head, take);
        c->head += take;
        p += take;
        n -= take;
    }
    return 0;
}

/* 读一行（去掉 CR/LF）；0 ok / 1 超时 / -1 断开或行太长 */
static int wsc_read_line(wsc_t *c, char *out, size_t cap, int ms)
{
    size_t o = 0;

    for (;;) {
        unsigned char ch;
        int r = wsc_read_exact(c, &ch, 1, ms);

        if (r != 0)
            return r;
        if (ch == '\n') {
            if (o && out[o - 1] == '\r')
                o--;
            out[o] = '\0';
            return 0;
        }
        if (o + 1 >= cap)
            return -1;
        out[o++] = (char)ch;
    }
}

/* 一直读到断开/超时，用于读 Connection: close 的 HTTP 应答体 */
static size_t wsc_drain(wsc_t *c, char *out, size_t cap, int ms)
{
    size_t total = 0;

    if (cap == 0)
        return 0;

    for (;;) {
        if (c->head < c->tail) {
            size_t avail = c->tail - c->head;
            size_t room = cap - 1 - total;
            size_t take = avail < room ? avail : room;
            memcpy(out + total, c->lb + c->head, take);
            c->head += take;
            total += take;
            if (total + 1 >= cap)
                break;
            continue;
        }
        if (wsc_fill(c, ms) != 1)
            break;
    }
    out[total] = '\0';
    return total;
}

/*
 * 去掉 HTTP/1.1 的 chunked 分块编码（就地改写 buf，返回新的长度）。
 * Go 的 net/http 对小于 2KB 的应答会自动补 Content-Length，但换成
 * chunked 的写法（或前面挂代理）也不能让屏幕解析不了，所以两种都支持。
 */
static size_t http_dechunk(char *buf, size_t len)
{
    size_t in = 0, out = 0;

    while (in < len) {
        size_t sz = 0;
        int digits = 0;

        for (;;) {
            char ch = buf[in];
            int v;

            if (ch >= '0' && ch <= '9')
                v = ch - '0';
            else if (ch >= 'a' && ch <= 'f')
                v = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F')
                v = ch - 'A' + 10;
            else
                break;
            sz = sz * 16 + (size_t)v;
            digits++;
            in++;
            if (in >= len)
                break;
        }
        if (!digits)
            break;

        while (in < len && (buf[in] == '\r' || buf[in] == '\n'))
            in++;
        if (sz == 0)
            break;
        if (in + sz > len)
            sz = len - in;
        memmove(buf + out, buf + in, sz);
        out += sz;
        in += sz;
        while (in < len && (buf[in] == '\r' || buf[in] == '\n'))
            in++;
    }

    buf[out] = '\0';
    return out;
}

/* ------------------------------------------------------------ WS 帧收发 */

#define WS_OP_CONT  0x0
#define WS_OP_TEXT  0x1
#define WS_OP_BIN   0x2
#define WS_OP_CLOSE 0x8
#define WS_OP_PING  0x9
#define WS_OP_PONG  0xA

/* 客户端发出的帧必须加掩码（RFC6455 5.3） */
static int ws_send_frame(int fd, int opcode, const unsigned char *payload,
                         size_t len, int timeout_ms)
{
    unsigned char hdr[14];
    unsigned char mask[4];
    unsigned char *tmp;
    size_t hl;
    int rc;

    hdr[0] = (unsigned char)(0x80 | (opcode & 0x0F));

    if (len < 126) {
        hdr[1] = (unsigned char)(0x80 | (unsigned char)len);
        hl = 2;
    } else if (len <= 0xFFFF) {
        hdr[1] = (unsigned char)(0x80 | 126);
        hdr[2] = (unsigned char)((len >> 8) & 0xFF);
        hdr[3] = (unsigned char)(len & 0xFF);
        hl = 4;
    } else {
        hdr[1] = (unsigned char)(0x80 | 127);
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (unsigned char)((len >> (8 * (7 - i))) & 0xFF);
        hl = 10;
    }

    ws_random(mask, sizeof(mask));
    memcpy(hdr + hl, mask, sizeof(mask));
    hl += sizeof(mask);

    if (send_all(fd, hdr, hl, timeout_ms) != 0)
        return -1;
    if (len == 0)
        return 0;

    tmp = (unsigned char *)malloc(len);
    if (!tmp)
        return -1;
    for (size_t i = 0; i < len; i++)
        tmp[i] = payload[i] ^ mask[i & 3];
    rc = send_all(fd, tmp, len, timeout_ms);
    free(tmp);
    return rc;
}

/* 读一个帧：0 ok / 2 超时 / -1 断开 */
static int ws_read_frame(wsc_t *c, int *opcode, int *fin, char *out, size_t cap,
                         size_t *outlen, int timeout_ms)
{
    unsigned char h2[2], mkey[4] = { 0, 0, 0, 0 };
    unsigned long long len;
    int masked, r;

    r = wsc_read_exact(c, h2, 2, timeout_ms);
    if (r == 1)
        return 2;
    if (r != 0)
        return -1;

    *fin = (h2[0] & 0x80) ? 1 : 0;
    *opcode = h2[0] & 0x0F;
    masked = (h2[1] & 0x80) ? 1 : 0;
    len = h2[1] & 0x7F;

    if (len == 126) {
        unsigned char e[2];
        r = wsc_read_exact(c, e, 2, timeout_ms);
        if (r == 1)
            return 2;
        if (r != 0)
            return -1;
        len = ((unsigned long long)e[0] << 8) | e[1];
    } else if (len == 127) {
        unsigned char e[8];
        r = wsc_read_exact(c, e, 8, timeout_ms);
        if (r == 1)
            return 2;
        if (r != 0)
            return -1;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | e[i];
    }

    if (len + 1 > cap || len > MSG_MAX)
        return -1;                       /* 太大：甩掉连接重来 */

    if (masked) {
        r = wsc_read_exact(c, mkey, 4, timeout_ms);
        if (r == 1)
            return 2;
        if (r != 0)
            return -1;
    }

    r = wsc_read_exact(c, out, (size_t)len, timeout_ms);
    if (r == 1)
        return 2;
    if (r != 0)
        return -1;

    if (masked)
        for (unsigned long long i = 0; i < len; i++)
            out[i] = (char)((unsigned char)out[i] ^ mkey[i & 3]);

    out[len] = '\0';
    *outlen = (size_t)len;
    return 0;
}

/* 0 收到一条完整文本（含分片重组）/ 2 超时 / -1 断开 */
static int ws_read_message(wsc_t *c, char *out, size_t cap, size_t *outlen, int timeout_ms)
{
    size_t total = 0;
    char *frame = (char *)malloc(MSG_MAX + 1);

    if (!frame)
        return -1;

    for (;;) {
        int op = 0, fin = 0, r;
        size_t n = 0;

        r = ws_read_frame(c, &op, &fin, frame, MSG_MAX + 1, &n, timeout_ms);
        if (r == 2) {
            free(frame);
            return 2;
        }
        if (r != 0) {
            free(frame);
            return -1;
        }

        if (op == WS_OP_PING) {
            ws_send_frame(c->fd, WS_OP_PONG, (const unsigned char *)frame, n, 3000);
            continue;
        }
        if (op == WS_OP_PONG || op == WS_OP_BIN)
            continue;
        if (op == WS_OP_CLOSE) {
            free(frame);
            return -1;
        }

        if (op == WS_OP_TEXT || op == WS_OP_CONT) {
            if (total + n + 1 > cap) {
                free(frame);
                return -1;
            }
            memcpy(out + total, frame, n);
            total += n;
            if (fin) {
                out[total] = '\0';
                *outlen = total;
                free(frame);
                return 0;
            }
        }
    }
}

/* ---------------------------------------------------------- 握手 / 登录 */

/* 0 ok；其它 = HTTP 状态码（401 表示缺 token）；-1 连接层失败 */
static int ws_handshake(wsc_t *c, const char *token)
{
    unsigned char rnd[16];
    char key[32], req[1400], line[512], query[600];
    int status = 0, r;

    ws_random(rnd, sizeof(rnd));
    b64_encode(rnd, sizeof(rnd), key);

    /* 调试用 token 优先；平时走双方定死的钥匙 */
    if (token && *token)
        snprintf(query, sizeof(query), "?token=%s", token);
    else if (ws_cfg.key[0])
        snprintf(query, sizeof(query), "?key=%s", ws_cfg.key);
    else
        query[0] = '\0';

    snprintf(req, sizeof(req),
             "GET %s%s HTTP/1.1\r\nHost: %s:%d\r\n"
             "Upgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
             ws_cfg.path, query, ws_cfg.host, ws_cfg.port, key);

    if (send_all(c->fd, req, strlen(req), 5000) != 0)
        return -1;

    r = wsc_read_line(c, line, sizeof(line), 5000);
    if (r != 0)
        return -1;
    if (sscanf(line, "HTTP/%*d.%*d %d", &status) != 1)
        return -1;

    /* 头部剩下的行全部吃掉（顺带把可能多读的帧字节留在 c->lb 里） */
    for (;;) {
        r = wsc_read_line(c, line, sizeof(line), 5000);
        if (r != 0)
            break;
        if (!line[0])
            break;
    }

    return status == 101 ? 0 : (status ? status : -1);
}

/* 用 config 里的用户名/密码换 JWT：POST /api/login */
static bool ws_login(char *token, size_t cap)
{
    int fd = tcp_connect(ws_cfg.host, ws_cfg.port, ws_cfg.timeout_sec);
    wsc_t c;
    char body[300], req[600], line[512];
    char *resp;
    bool ok = false;
    bool chunked = false;
    int status = 0, r;

    if (cap)
        token[0] = '\0';
    if (fd < 0)
        return false;

    memset(&c, 0, sizeof(c));
    c.fd = fd;

    snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}",
             ws_cfg.user, ws_cfg.pass);
    snprintf(req, sizeof(req),
             "POST /api/login HTTP/1.1\r\nHost: %s:%d\r\n"
             "Content-Type: application/json\r\nContent-Length: %d\r\n"
             "Connection: close\r\n\r\n%s",
             ws_cfg.host, ws_cfg.port, (int)strlen(body), body);

    if (send_all(fd, req, strlen(req), 5000) != 0) {
        close(fd);
        return false;
    }

    r = wsc_read_line(&c, line, sizeof(line), 5000);
    if (r != 0) {
        close(fd);
        return false;
    }
    sscanf(line, "HTTP/%*d.%*d %d", &status);

    for (;;) {
        r = wsc_read_line(&c, line, sizeof(line), 5000);
        if (r != 0)
            break;
        if (!line[0])
            break;
        if (find_ci_local(line, "chunked"))
            chunked = true;
    }

    resp = (char *)malloc(8192);
    if (resp) {
        size_t n = wsc_drain(&c, resp, 8192, 3000);

        if (chunked) {
            http_dechunk(resp, n);
        } else {
            /* 万一还有别的前缀（分块尾巴之类），从第一个 '{' 开始解 */
            char *brace = strchr(resp, '{');
            if (brace && brace != resp)
                memmove(resp, brace, strlen(brace) + 1);
        }

        /*
         * 服务端的应答是 {"success":true,...,"data":{"token":"...",...}}
         * ⇒ token 在 data 里面，先取 data 再取 token（顶层也兜一下）。
         */
        if (status == 200 || resp[0] == '{') {
            char *dbuf = (char *)malloc(4096);

            if (dbuf) {
                if (tj_get_raw(resp, "data", dbuf, 4096))
                    ok = tj_get_str(dbuf, "token", token, cap) && token[0];
                free(dbuf);
            }
            if (!ok)
                ok = tj_get_str(resp, "token", token, cap) && token[0];
        }

        if (!ok)
            printf("ws login failed (http %d): %.200s\n", status, resp);

        free(resp);
    }

    close(fd);
    return ok;
}

/* ------------------------------------------------------------ 配置读取 */

static int cfg_int(const char *s, int def)
{
    if (!s || !*s)
        return def;
    return atoi(s);
}

/*
 * 配置文件里只认一个开关：config ws / option enabled。
 * 地址（127.0.0.1:8080）与通讯钥匙都定在代码里 —— 端口由服务端那侧决定，
 * 屏幕端不配（需要临时改的用命令行 -ws/-token/-user/-pass，见 wsclient.h）。
 */
static void apply_ws_option(const char *key, const char *val)
{
    if (strcmp(key, "enabled") == 0) {
        ws_cfg.enabled = cfg_int(val, 1);
    } else if (strcmp(key, "port") == 0) {
        int p = cfg_int(val, 0);

        if (p > 0 && p <= 65535) {
            ws_cfg.port = p;
            g_port_auto = 0;        /* 明确指定了端口 ⇒ 不再去读 modem 的配置 */
        }
    } else if (strcmp(key, "host") == 0 && val[0]) {
        snprintf(ws_cfg.host, sizeof(ws_cfg.host), "%s", val);
        g_host_fixed = 1;
    }
}

/* 解析 /etc/config/tftdisplay 里的 UCI 段：
 *   config ws 'ws'
 *       option host '127.0.0.1'
 */
static void parse_uci_ws(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int in_ws = 0;

    if (!f)
        return;

    while (fgets(line, sizeof(line), f)) {
        char *p = line, *q;

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#' || *p == '\n' || *p == '\r')
            continue;

        if (strncmp(p, "config", 6) == 0) {
            in_ws = (strstr(p, "'ws'") || strstr(p, "\"ws\"") || strstr(p, " ws")) ? 1 : 0;
            continue;
        }
        if (!in_ws || strncmp(p, "option", 6) != 0)
            continue;

        p += 6;
        while (*p == ' ' || *p == '\t')
            p++;

        q = p;
        while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r')
            q++;
        if (!*q)
            continue;
        *q++ = '\0';

        while (*q == ' ' || *q == '\t')
            q++;

        {
            char val[256];
            size_t o = 0;

            if (*q == '\'' || *q == '"') {
                char quote = *q++;
                while (*q && *q != quote && o + 1 < sizeof(val))
                    val[o++] = *q++;
            } else {
                while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r' &&
                       o + 1 < sizeof(val))
                    val[o++] = *q++;
            }
            val[o] = '\0';
            apply_ws_option(p, val);
        }
    }

    fclose(f);
}

void ws_cfg_load(int argc, char **argv)
{
    ws_cfg.enabled = 1;
    snprintf(ws_cfg.host, sizeof(ws_cfg.host), "%s", WS_DEFAULT_HOST);
    ws_cfg.port = WS_DEFAULT_PORT;
    snprintf(ws_cfg.path, sizeof(ws_cfg.path), "%s", WS_DEFAULT_PATH);
    snprintf(ws_cfg.key, sizeof(ws_cfg.key), "%s", WS_SHARED_KEY);
    ws_cfg.token[0] = '\0';
    ws_cfg.user[0] = '\0';
    ws_cfg.pass[0] = '\0';
    ws_cfg.reconnect_sec = 5;
    ws_cfg.timeout_sec = 5;
    g_port_auto = 1;
    g_host_fixed = 0;
    g_port_src[0] = '\0';

    parse_uci_ws("/etc/config/tftdisplay");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-nows") == 0) {
            ws_cfg.enabled = 0;
        } else if (strcmp(argv[i], "-ws") == 0 && i + 1 < argc) {
            char *s = argv[++i];
            char *colon = strchr(s, ':');

            if (colon) {
                *colon = '\0';
                ws_cfg.port = atoi(colon + 1);
                g_port_auto = 0;
            }
            if (s[0]) {
                snprintf(ws_cfg.host, sizeof(ws_cfg.host), "%s", s);
                g_host_fixed = 1;
            }
        } else if (strcmp(argv[i], "-token") == 0 && i + 1 < argc) {
            snprintf(ws_cfg.token, sizeof(ws_cfg.token), "%s", argv[++i]);
        } else if (strcmp(argv[i], "-user") == 0 && i + 1 < argc) {
            snprintf(ws_cfg.user, sizeof(ws_cfg.user), "%s", argv[++i]);
        } else if (strcmp(argv[i], "-pass") == 0 && i + 1 < argc) {
            snprintf(ws_cfg.pass, sizeof(ws_cfg.pass), "%s", argv[++i]);
        }
    }

    /* 没明确指定端口就去读 modem 的配置 */
    if (g_port_auto)
        port_resolve();
}

/* ---------------------------------------------------------------- 线程 */

static pthread_t g_tid;
static volatile int g_run;
static volatile int g_fd = -1;

bool ws_client_send_text(const char *text)
{
    int fd = g_fd;

    if (fd < 0 || !text)
        return false;
    return ws_send_frame(fd, WS_OP_TEXT, (const unsigned char *)text, strlen(text), 3000) == 0;
}

static void *ws_thread(void *arg)
{
    char *msg;
    char token[1024];

    (void)arg;

    msg = (char *)malloc(MSG_MAX + 1);
    if (!msg)
        return NULL;

    while (g_run) {
        wsc_t c;
        int fd, hs, idle = 0;
        int wait = ws_cfg.reconnect_sec > 0 ? ws_cfg.reconnect_sec : 5;

        if (!ws_cfg.enabled) {
            wsdata_set_state("WS 已关闭 (config ws enabled=0)");
            sleep_ms(3000);
            continue;
        }

        /* 每次重连都重读一遍 modem 的配置：对方把端口改了，屏幕不重启也能跟上 */
        if (g_port_auto) {
            char h[64];
            int p = 0;

            if (port_from_modem(h, sizeof(h), &p) &&
                (p != ws_cfg.port || (!g_host_fixed && strcmp(h, ws_cfg.host) != 0))) {
                char why[96];

                ws_cfg.port = p;
                if (!g_host_fixed)
                    snprintf(ws_cfg.host, sizeof(ws_cfg.host), "%s", h);
                snprintf(why, sizeof(why), "modem 配置改了，改用 %s:%d", ws_cfg.host, ws_cfg.port);
                wsdata_set_state(why);
            }
        }

        token[0] = '\0';
        if (ws_cfg.token[0]) {
            snprintf(token, sizeof(token), "%s", ws_cfg.token);
        } else if (ws_cfg.user[0]) {
            wsdata_set_state("正在登录 /api/login ...");
            if (!ws_login(token, sizeof(token))) {
                wsdata_set_state("登录失败（用户名/密码不对？）");
                sleep_ms(wait * 1000);
                continue;
            }
        }

        wsdata_set_state("正在连接 ...");
        fd = tcp_connect(ws_cfg.host, ws_cfg.port, ws_cfg.timeout_sec);
        if (fd < 0) {
            char why[96];

            snprintf(why, sizeof(why), "连不上 modem %s:%d", ws_cfg.host, ws_cfg.port);
            wsdata_set_state(why);
            sleep_ms(wait * 1000);
            continue;
        }

        memset(&c, 0, sizeof(c));
        c.fd = fd;

        hs = ws_handshake(&c, token);
        if (hs != 0) {
            char why[128];

            close(fd);

            if (hs == 401)
                snprintf(why, sizeof(why), "服务端拒绝：钥匙不对（HTTP 401）");
            else if (hs == 404)
                snprintf(why, sizeof(why), "%s:%d 上没有 %s（HTTP 404）",
                         ws_cfg.host, ws_cfg.port, ws_cfg.path);
            else if (hs > 0)
                snprintf(why, sizeof(why), "握手失败（HTTP %d）", hs);
            else
                snprintf(why, sizeof(why), "WebSocket 握手失败");

            wsdata_set_state(why);
            sleep_ms(wait * 1000);
            continue;
        }

        g_fd = fd;
        wsdata_set_online(true);
        wsdata_set_state("已连接");

        for (;;) {
            size_t n = 0;
            int r = ws_read_message(&c, msg, MSG_MAX, &n, READ_TIMEOUT);

            if (!g_run)
                break;
            if (r == 2) {
                if (ws_send_frame(fd, WS_OP_PING, (const unsigned char *)"", 0, 3000) != 0)
                    break;
                if (++idle > MAX_IDLE)
                    break;
                continue;
            }
            if (r != 0)
                break;

            idle = 0;
            if (n)
                wsdata_on_message(msg);
        }

        g_fd = -1;
        wsdata_set_online(false);
        close(fd);

        if (g_run) {
            wsdata_set_state("连接断开，准备重连");
            sleep_ms(wait * 1000);
        }
    }

    free(msg);
    return NULL;
}

int ws_client_start(void)
{
    if (g_run)
        return 0;

    g_run = 1;
    if (pthread_create(&g_tid, NULL, ws_thread, NULL) != 0) {
        g_run = 0;
        return -1;
    }
    pthread_detach(g_tid);
    return 0;
}

void ws_client_stop(void)
{
    g_run = 0;
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}
