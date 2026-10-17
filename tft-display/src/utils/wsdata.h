#ifndef WSDATA_H
#define WSDATA_H

#include <stdbool.h>
#include <stddef.h>

/*
 * wsdata —— 界面要显示的东西**只在这里存**，而且只由 WebSocket 推送填。
 *
 * 数据来源（按实际到达的报文自动识别，两套协议都认）：
 *
 * ① **modemd（设备中心，当前的主力来源）**：{type, modem_id, at, data}
 *    · "network"  → NetworkStatus{operator, tech, mcc, mnc, ipv4, ipv6,
 *                                 serving{rsrp,rsrq,rssi,sinr,tech}, traffic{...},
 *                                 cs_reg{registered}, ps_reg{registered}}
 *    · "signal"   → SignalInfo{rssi, nr_rsrp, nr_rsrq, nr_sinr}（rssi 是 CSQ 原始值 0..31）
 *    · "traffic"  → Rate{up_bps, down_bps, sent, recv}（byte/s，无需再解析文本）
 *    · "modems"   → []Modem{model, signal{...}, sms_total, sms_unread, modem_status{...}}
 *    · "smscount" → {total, unread}
 *    对应的服务端实现：modemd 的 corev2/displayws.go
 *
 * ② **modemserver（旧协议，保留兼容）**：{type, serialId, category, content, data}
 *    · "IP" / "Speed" / "AT" / "QSCAN" / "Event*" / "Sys"
 *
 * 界面线程不要直接读内部变量：调 wsdata_snapshot() 拿一份加锁快照。
 */

/* ---- 短信（modemd 的 sms 推送）----
 * 服务端已经把**长短信（UDH 拼接）合并成一条**（corev2 的 GroupSMSConcat），
 * 这里拿到的就是"3 合 1"那条（`part_total` 会告诉界面它是由几段合起来的）。 */
#define WS_SMS_MAX  16          /* 屏幕上最多存几条（多了也划不过来） */
#define WS_SMS_TEXT 384         /* 单条正文最多存多少字节（3 合 1 大约 210 字，够） */
#define WS_SMS_PART_MAX 6       /* 一条长短信最多记几段的存储位置（服务端 parts[]） */

typedef struct {
    char index[8];              /* **存储位置**（SMS.Index）—— 标已读要用它，不是列表下标 */
    char number[24];            /* 发件号码 */
    char time[24];              /* 时间戳（服务端原文，形如 26/10/07,12:30:00+32） */
    int  unread;                /* 1 = 未读 */
    int  part_total;            /* 分条总数：>1 = 由多段合并来的长短信 */
    char text[WS_SMS_TEXT];     /* 正文（合并后的完整内容，超长已截断） */
    /* 长短信：各段各自的**存储位置**（服务端 parts[] 里的 index）。
     * ⚠️ 删的时候必须**逐段删**：只删头部那一段，剩下的段会被服务端重拼又出现，
     *    界面上看着就是"删不掉"（见 pages/smspage.c 的 send_sms_delete）。 */
    char part_idx[WS_SMS_PART_MAX][8];
    int  part_n;
} ws_sms_t;

/* ---- 组网模式偏好（modemd 的 mode 推送；Mobile 页第 3 屏的 4G / 5G / 4G+5G） ----
 * 服务端（corev2/displaymode.go）已经把"屏幕上的三个挡位"算好了：
 *   value = 语义值（LTE / NR5G / LTE:NR5G）、name = 按钮上的字（4G / 5G / 4G+5G）、
 *   writable = 本机能不能写（0 就把按钮置灰 —— 如展锐只有 仅4G / 4G+5G 两档）。
 * 屏幕只管画，不自己猜哪档叫什么名。 */
#define WS_MODE_BTN_MAX 4
typedef struct {
    char value[24];             /* 语义值（发回服务端的就是它） */
    char name[24];              /* 按钮上的字：4G / 5G / 4G+5G */
    int  writable;              /* 0 = 本机不支持这一档（按钮置灰） */
} ws_mode_btn_t;

/* 组网模式偏好一整块。
 * 界面侧单独有这个结构 + wsdata_mode() 取数：它每隔几百毫秒就要刷一次，
 * 而 ws_snapshot_t 有 8KB（带短信正文）—— 为这一屏拷一份大快照不划算
 *（而且那是在 LVGL 任务栈上的栈拷贝）。 */
typedef struct {
    char mode_value[24];        /* 当前值（LTE / NR5G / LTE:NR5G） */
    char mode_name[64];         /* 当前值的中文全名（服务端给，长） */
    char mode_short[24];        /* 当前值的中文**短名**（自动 / 仅4G / 4G+5G）—— 屏上那行用它 */
    char mode_msg[96];          /* 最近一次操作的反馈："已切换到 4G" / "设置失败：…" */
    int  mode_readonly;         /* 1 = 本机只能读（三个按钮全置灰） */
    int  mode_btn_n;            /* 按钮个数（服务端挑的，通常 3） */
    ws_mode_btn_t mode_btn[WS_MODE_BTN_MAX];
    unsigned mode_seq;          /* 内容版本号：每收到一份不一样的组网模式就 +1（界面据此重画） */
} ws_mode_t;

typedef struct {
    bool online;                /* WebSocket 已连上且通过握手 */
    char state[64];             /* 给人看的一句话：未连接 / 已连接 / 登录失败… */
    unsigned msgs;              /* 收到的帧数（调试用） */
    /* ---- 网口（Type IP） ---- */
    char mac[32];
    bool ip_up, ip_hasip, ip_connected;
    char ips[256];              /* 多个 IP 已用 '\n' 分隔，可直接贴到 label */

    /* ---- 流量（Type Speed） ---- */
    char dev[64];
    char total_recv[32], total_sent[32];
    char recv_rate[32], sent_rate[32];
    double recv_bps, sent_bps;  /* 从 "12.34 KB/s" 反解出来的字节/秒 */

    /* ---- 蜂窝（Type AT / QSCAN / EventAdded） ---- */
    char cops[64];              /* 运营商分类：Telecom/Unicom/CMCC/CBN/--- */
    char op_name[32];           /* 运营商中文名：中国电信/中国联通/中国移动/中国广电 */
    char rat[16];               /* 制式（内部口径）：GSM/WCDMA/LTE/NR */
    char rat_label[8];          /* 制式（界面用）：5G/4G/3G/2G */
    char mcc[32];               /* 服务端口径：+COPS 应答里引号内那段 */
    char model[64];             /* 模块型号（AT+CGMM/CGMR） */

    /* ---- 网络参数（NET 页；全部来自 modemd 的 network 推送，屏幕不发 AT） ----
     * 与 web 端「网络类型 / 接入信息」那张卡片同一批字段（net.go 的 NetworkStatus）。
     * 服务端字段是 omitempty ⇒ 某项没上报时这里就是空串/0，界面显示 "-"（不编数据）。 */
    char net_tech[24];          /* net_tech   ：模组原始制式 "FDD LTE" / "TDD NR5G" */
    char band_text[24];         /* band_text  ："LTE BAND 3" / "NR5G BAND 78" */
    char band_label[16];        /* band_label ："B3" / "n78" */
    int  earfcn;                /* 频点（LTE EARFCN / NR ARFCN） */
    char ca_text[32];           /* 载波聚合各分量载波："B3" / "B3+n78" */
    int  ca_cc;                 /* 分量载波个数（0 = 模组不上报 CA） */
    bool ca_active;             /* 真的聚合了（列表里有非 PCC 的辅载波） */
    char plmn[16];              /* 46011 */
    char mccmnc[16];            /* 460/11（分开的两段，界面按 "MCC/MNC" 显示） */
    int  serving_count;         /* 服务小区数（0 = 没有扫描缓存且实时也查不到） */
    int  neigh_count;           /* 邻区数（来自扫描缓存，没扫过就是 0） */

    /* ---- 信号（Type QSCAN 优先，其次 AT 里的 +CESQ/+QCSQ/+CSQ） ---- */
    bool has_signal;
    int  rsrp, rsrq, sinr;      /* dBm / dB / dB */
    int  rssi;                  /* 只有 +CSQ 时才用得上 */

    /* 内部：两路来源分开存，快照前按优先级合成 */
    char rat_cops[16];
    char rat_scan[16];
    char op_cops[32];
    char op_scan[32];

    /* ---- 宿主信息（现由 modemd 的 `sys` 推送提供：Cpu/Mem/Temp，1 秒一条） ---- */
    int   cpu, mem, temp;
    /* ---- 主机其它信息（同一个 sys 推送，SYSTEM 页显示） ----
     * uptime = 已运行秒数（0 = 没上报）；load = 1 分钟负载×100（0.77 → 77）；disk = 根分区占用 %。 */
    int   uptime, load, disk;
    /* 服务端累计的极值（自 modemd 启动起算）：CPU/内存恒有；温度 0 = 没上报过。
     * 屏幕的趋势页就显示这两个数（"历史最高/最低"），不自己攒长历史。 */
    int   cpu_min, cpu_max, mem_min, mem_max, temp_min, temp_max;
    int   clients;              /* 局域网客户端数，-1 = 服务端没报过（显示 --） */
    int   batt_pct;             /* 电量百分比，-1 = 未知 */
    double batt_voltage, chg_voltage;
    bool  chg_error;

    /* ---- 短信条数（modemd 的 smscount / modems[].sms_*；不含任何正文） ---- */
    int   sms_total, sms_unread;

    /* ---- 短信列表（modemd 的 sms 推送；按时间倒序，[0] = 最新） ---- */
    int      sms_n;             /* 有效条数（0 = 还没读到过短信） */
    ws_sms_t sms[WS_SMS_MAX];
    /* 内容版本号：每收到一条"不一样的"列表就 +1（界面据此重画/弹窗）；
     * `sms_new_seq` 是其中"有新短信"的那次（首次收到只建基线，不算新）。 */
    unsigned sms_seq;
    unsigned sms_new_seq;
    bool     sms_seen;          /* 内部：是否已经收到过第一份列表 */

    /* ---- WiFi（Type Sys） ---- */
    char ssid[64], wifikey[64];

    bool fresh;                 /* 本次快照前是否有新数据（方便界面只在变化时重画） */

    /* ---- 组网模式偏好（modemd 的 mode 推送） ---- */
    ws_mode_t mode;

    /* ---- 余额查询短信的发送结果（modemd 的 sendsms 推送；SMS 页最后一屏用）----
     * 屏幕发 {"action":"sms_send","number":"10086","text":"CXHF"}，
     * 服务端发完（成败都推）回 {"number","text","ok","msg"}。 */
    char     sendsms_msg[128];  /* 给人看的一句话：已发送，等运营商回复 / 发送失败：… */
    bool     sendsms_ok;
    char     sendsms_num[24];   /* 目标号码（回显） */
    unsigned sendsms_seq;       /* 每收到一条结果 +1（界面据此刷新那行字） */
} ws_snapshot_t;

/* ---- WS 线程调用 ---- */
void wsdata_on_message(const char *json);          /* 一整帧文本 */
void wsdata_set_online(bool on);
void wsdata_set_state(const char *text);
bool wsdata_online(void);

/* ---- 界面线程调用 ---- */
void wsdata_snapshot(ws_snapshot_t *out);
int  wsdata_sms_count(void);       /* 只要短信条数时用这个（比整份快照便宜） */
/* 只要组网模式偏好那一块（Mobile 页第 3 屏几百毫秒一次地刷）。
 * 返回 false = 还没有数据（屏幕显示"读取中"），此时 out 已被清空。 */
bool wsdata_mode(ws_mode_t *out);

/* 初始化（把默认值/占位符写好），main() 早期调一次 */
void wsdata_init(void);

#endif /* WSDATA_H */
