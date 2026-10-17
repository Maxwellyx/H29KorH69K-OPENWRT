'use strict';
/*
 * modemserver 的极简替身：只为验证 tftdisplay 的 WebSocket 取数链路。
 *
 *   POST /api/login   {"username":"root","password":"hunter2"}
 *        → {"success":true,...,"data":{"token":"mock-token-123",...}}
 *   GET  /ws?token=mock-token-123   （缺/错 token → 401，与服务端行为一致）
 *
 * 连上后按 modemserver 的真实结构轮播推送：
 *   Type "IP"    = ping.InterfaceStatus（Go 字段名，无 json tag）
 *   Type "Speed" = traffic 那坨（数值是格式化字符串）
 *   Type "AT"    = map[指令]原始应答
 *   Type "Sys"   = 本工程约定的宿主信息（服务端尚未实现，屏幕按这个键名消费）
 *
 * 用法： node server.js [port]
 */

const http = require('http');
const crypto = require('crypto');

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const TOKEN = 'mock-token-123';
const USER = 'root';
const PASS = 'hunter2';
/* 屏幕端专用通道的定死钥匙（与服务端 DisplayWSKey / C 端 WS_SHARED_KEY 一致） */
const DISPLAY_KEY = 'h29k-tft-9f3c1a7b';
const PORT = parseInt(process.argv[2] || '8090', 10);
/* 第三个参数给 modemd 就推 modemd 的原生协议（{type,modem_id,at,data}），
 * 否则推旧的 modemserver 协议。屏幕端两套都认，离线验证时用这个开关切。 */
const MODEMD_MODE = (process.argv[3] || '').toLowerCase() === 'modemd';

function wsFrame(payload, opcode) {
  const p = Buffer.isBuffer(payload) ? payload : Buffer.from(payload, 'utf8');
  const op = opcode === undefined ? 0x1 : opcode;
  let head;
  if (p.length < 126) {
    head = Buffer.from([0x80 | op, p.length]);
  } else if (p.length < 65536) {
    head = Buffer.alloc(4);
    head[0] = 0x80 | op;
    head[1] = 126;
    head.writeUInt16BE(p.length, 2);
  } else {
    head = Buffer.alloc(10);
    head[0] = 0x80 | op;
    head[1] = 127;
    head.writeBigUInt64BE(BigInt(p.length), 2);
  }
  return Buffer.concat([head, p]);
}

/* ---- 推送内容（跟 modemserver 的 JSON 结构逐字段对齐） ---- */

const msgIp = () => JSON.stringify({
  type: 'IP',
  serialId: 'usb0',
  data: {
    Name: 'usb0',
    IsUp: true,
    HasIP: true,
    IPAddresses: ['10.5.21.202', '240e:478:620:163a:0:1:2:3'],
    MacAddress: 'aa:bb:cc:dd:ee:ff',
    IsConnected: true,
  },
});

const msgSpeed = () => JSON.stringify({
  type: 'Speed',
  serialId: 'usb0',
  data: {
    Device: 'usb0',
    Timestamp: new Date().toISOString().slice(0, 19).replace('T', ' '),
    TotalRecv: '1.25 GB',
    TotalSent: '345.60 MB',
    RecvRate: '1.50 MB/s',
    SentRate: '128.00 KB/s',
  },
});

const msgAt = () => JSON.stringify({
  type: 'AT',
  serialId: 'usb0',
  category: 'quectel|Quectel',
  content: 'qualcomm',
  data: {
    /* 运营商 + 制式（act 11 = NR ⇒ 5G；名字⇒中国电信） */
    'AT+COPS?': '+COPS: 0,0,"CHINA TELECOM",11\r\n\r\nOK\r\n',
    /* 3GPP 标准信号：<rxlev>,<ber>,<rscp>,<ecno>,<rsrq_idx>,<rsrp_idx>
     *   rsrq=(25-39)/2=-7dB、rsrp=51-140=-89dBm                        */
    'AT+CESQ': '+CESQ: 99,99,255,255,25,51\r\n\r\nOK\r\n',
    'AT+CSQ': '+CSQ: 22,99\r\n\r\nOK\r\n',
    'AT+CGMM': 'RM500U-CNV\r\nOK\r\n',
  },
});

/* 服务端推的基站/信号扫描（对象数组，NR5G 优先） */
const msgQscan = () => JSON.stringify({
  type: 'QSCAN',
  serialId: 'usb0',
  data: [
    {
      Protocol: 'NR5G', MCC: '460', MNC: '11', TAC: '1234567', PCI: '420',
      RSRP: -89, RSRQ: -11, SINR: 12, Status: 1,
    },
  ],
});

const msgSys = () => JSON.stringify({
  type: 'Sys',
  serialId: 'ALL',
  data: {
    Cpu: 37,
    Mem: 52,
    Temp: 46,
    Clients: 7,
    Batt: 78,
    BattVoltage: 7.92,
    ChgVoltage: 12.3,
    Ssid: 'H29K-5G',
    WifiKey: '1234567890',
  },
});

/* mock 两条通道都推（CESQ 与 QSCAN 的值刻意一致，避免互相“覆盖”看出差异） */
const messages = [msgIp, msgSpeed, msgAt, msgSys, msgQscan];

/* ------------------------------------------------------------------
 * modemd（设备中心）原生协议：{type, modem_id, at, data}
 * 用来离线验证屏幕端对 modemd 的解析（真实服务端 corev2/displayws.go 推的就是这套）。
 * 字段名必须与 corev2 的 json tag 一致。
 */
const mdSignalData = { rssi: 14, ber: 99, nr_rsrp: -89, nr_rsrq: -11, nr_sinr: 12 };
const mdTrafficData = {
  supported: true, up_bps: 131072, down_bps: 1572864,
  up_text: '128.00 KB/s', down_text: '1.50 MB/s',
  sent: 211031532, recv: 349998254, interval_s: 1,
};

const mdModems = () => JSON.stringify({
  type: 'modems', at: Date.now(), data: [{
    id: 0, at_port: '/dev/ttyUSB2', vendor: 'quectel', vendor_name: '移远 Quectel',
    model_raw: 'ATI|Quectel|RM500U-CNV|OK', model: 'RM500U-CNV',
    imei: '869955063166602', firmware: 'M1250_RM500UCNVAA_BR0_R03.01.001',
    signal: mdSignalData, sms_list: null, sms_total: 3, sms_unread: 1,
    modem_status: { modem_id: 0, sim_present: true, temp_max: { name: 'modem', celsius: 46 } },
  }],
});

const mdSignal = () => JSON.stringify({ type: 'signal', modem_id: 0, at: Date.now(), data: mdSignalData });

const mdNetwork = () => JSON.stringify({
  type: 'network', modem_id: 0, at: Date.now(), data: {
    modem_id: 0, net_tech: 'NR5G-SA', tech_label: '5G NR', tech: 'NR',
    plmn: '46011', mcc: '460', mnc: '11', band_text: 'NR N78', band: 78, band_label: 'n78',
    earfcn: 627264,
    cs_reg: { stat: 0, registered: false },
    ps_reg: { stat: 0, registered: false },
    reg_5g: { stat: 1, registered: true },
    operator: '中国电信',
    serving: {
      tech: 'NR', state: 'CONNECT', mcc: '460', mnc: '11',
      rsrp: -89, rsrq: -11, rssi: -55, sinr: 12, serving: true, operator: '中国电信',
    },
    /* 载波聚合：列表含主载波 PCC，有辅载波（SCC）才算真聚合 ——
     * 与 corev2/history.go 的 caSummary / web 端 caPill 同一口径。 */
    ca: [
      { role: 'PCC', tech: 'NR', band: 78, band_text: 'NR N78', band_label: 'n78', earfcn: 627264, pcid: 521, bandwidth: 100 },
      { role: 'SCC', tech: 'NR', band: 78, band_text: 'NR N78', band_label: 'n78', earfcn: 636666, pcid: 522, bandwidth: 100 },
    ],
    ca_active: true,
    serving_count: 1,
    neighbour_count: 4,
    apn: 'ctnet',
    ipv4: '10.82.28.91',
    ipv6: '240e:0400:0680:07db:18dc:28b2:9fbd:9e37',
    traffic: mdTrafficData,
  },
});

const mdTraffic = () => JSON.stringify({ type: 'traffic', modem_id: 0, at: Date.now(), data: mdTrafficData });
const mdSmscount = () => JSON.stringify({ type: 'smscount', modem_id: 0, at: Date.now(), data: { total: 3, unread: 1 } });

/* 短信列表（type "sms"）：最新一条是**长短信 3 合 1**（服务端 GroupSMSConcat 合并后的样子：
 * UDH 三段拼成一条完整正文，part_total=3），用来验证屏幕的列表页与"逐条阅读"。
 * 第 25 次推送时插一条"刚收到的新短信" ⇒ 顺手验证新短信弹窗（屏幕按最新一条的变化判新）。 */
const SMS_LONG =
  '【中国电信】尊敬的客户，您本月已使用流量 12.34GB，剩余 7.66GB。为避免影响您正常使用，' +
  '请及时关注流量使用情况；如需办理流量包，请回复 1 或登录中国电信 App 办理。本短信由系统自动发出，请勿直接回复。';
let smsTick = 0;
const mdSms = () => {
  smsTick++;
  const list = [
    {
      index: '3', status: '0', number: '10001', timestamp: '26/10/07,15:42:11+32',
      content: SMS_LONG, part_ref: 42, part_total: 3, part_seq: 1, parts: [],
    },
    {
      index: '2', status: '1', number: '1069030012345', timestamp: '26/10/06,09:12:03+32',
      content: '您的验证码是 826431，5 分钟内有效，请勿泄露给他人。', part_total: 1,
    },
    {
      index: '1', status: '1', number: '+8613800138000', timestamp: '26/10/05,20:30:45+32',
      content: '明天上午 9 点开会，记得带上方案文档。', part_total: 1,
    },
  ];
  if (smsTick >= 6) {
    list.unshift({
      index: '9', status: '0', number: '106598001', timestamp: '26/10/07,16:20:00+32',
      content: '【气象台】暴雨橙色预警：未来 3 小时本地区降雨量将达 50 毫米以上，请注意防范。',
      part_total: 1,
    });
  }
  return JSON.stringify({ type: 'sms', modem_id: 0, at: Date.now(), data: list });
};

const mdMessages = [mdModems, mdSignal, mdNetwork, mdTraffic, mdSmscount, mdSms];

/* ------------------------------------------------------------------ */

const server = http.createServer((req, res) => {
  if (req.method === 'POST' && req.url.startsWith('/api/login')) {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      let j = {};
      try { j = JSON.parse(body); } catch (e) { /* ignore */ }
      console.log('POST /api/login', body);
      if (j.username === USER && j.password === PASS) {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          success: true, code: 200, message: '登录成功',
          data: { token: TOKEN, username: USER, role: USER },
        }));
      } else {
        res.writeHead(401, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: false, code: 401, message: '验证错误' }));
      }
    });
    return;
  }
  res.writeHead(404, { 'Content-Type': 'text/plain' });
  res.end('mock: only /api/login and /ws\n');
});

server.on('upgrade', (req, socket) => {
  const u = new URL(req.url, 'http://mock');
  const token = u.searchParams.get('token');
  const key = u.searchParams.get('key');
  console.log('UPGRADE', req.url, 'token=' + token, 'key=' + key);

  /* /display/ws —— 屏幕端：只看钥匙（不校验密码/JWT） */
  if (u.pathname === '/display/ws') {
    if (key !== DISPLAY_KEY) {
      socket.write('HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n');
      socket.destroy();
      return;
    }
  } else if (u.pathname !== '/ws' || token !== TOKEN) {
    socket.write('HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n');
    socket.destroy();
    return;
  }

  const accept = crypto.createHash('sha1')
    .update(req.headers['sec-websocket-key'] + GUID)
    .digest('base64');

  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n');

  console.log('  → 101, 开始推送');

  let i = 0;
  const timer = setInterval(() => {
    const list = MODEMD_MODE ? mdMessages : messages;
    const m = list[i++ % list.length];
    try {
      socket.write(wsFrame(m()));
    } catch (e) {
      clearInterval(timer);
    }
  }, 1000);

  /* 简易帧解析：只处理 close / ping / pong / 文本 */
  let buf = Buffer.alloc(0);
  socket.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 2) {
      const op = buf[0] & 0x0f;
      const masked = (buf[1] & 0x80) !== 0;
      let len = buf[1] & 0x7f;
      let off = 2;
      if (len === 126) {
        if (buf.length < 4) return;
        len = buf.readUInt16BE(2); off = 4;
      } else if (len === 127) {
        if (buf.length < 10) return;
        len = Number(buf.readBigUInt64BE(2)); off = 10;
      }
      const maskLen = masked ? 4 : 0;
      if (buf.length < off + maskLen + len) return;
      let payload = buf.slice(off + maskLen, off + maskLen + len);
      if (masked) {
        const key = buf.slice(off, off + 4);
        payload = Buffer.from(payload.map((b, idx) => b ^ key[idx % 4]));
      }
      buf = buf.slice(off + maskLen + len);

      if (op === 0x8) { clearInterval(timer); socket.destroy(); return; }
      if (op === 0x9) socket.write(wsFrame(payload, 0xA));
      if (op === 0x1) console.log('  ← 客户端文本: ' + payload.toString('utf8'));
    }
  });

  socket.on('close', () => { clearInterval(timer); console.log('  连接关闭'); });
  socket.on('error', () => { clearInterval(timer); });
});

server.listen(PORT, '0.0.0.0', () => {
  console.log('mock modemserver: http://0.0.0.0:' + PORT);
  console.log('  屏幕端通道 : ws://<pc-ip>:' + PORT + '/display/ws?key=' + DISPLAY_KEY);
  console.log('  网页端通道 : ws://<pc-ip>:' + PORT + '/ws?token=' + TOKEN);
});
