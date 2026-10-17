/*
 * 屏幕端「组网模式偏好」探针：连上 /display/ws，看服务端推的 `mode` 报文（整份打印，不截断）。
 *
 *   node mode.js 192.168.1.1 8080 h29k-tft-9f3c1a7b              # 只读：发 mode_get，打印当前值+三个挡位
 *   node mode.js 192.168.1.1 8080 h29k-tft-9f3c1a7b LTE:NR5G    # 再发 mode_set 切到这一档并看回推
 *
 * 与 send.js 一样：JSON 在脚本里拼（PowerShell 交给原生程序时会吃掉双引号，踩过）。
 */
const net = require('net');
const crypto = require('crypto');

const host = process.argv[2] || '127.0.0.1';
const port = parseInt(process.argv[3] || '8080', 10);
const key = process.argv[4] || 'h29k-tft-9f3c1a7b';
const value = process.argv[5] || '';
const path = '/display/ws';

const wsKey = crypto.randomBytes(16).toString('base64');

function maskFrame(text) {
    const payload = Buffer.from(text, 'utf8');
    const mask = crypto.randomBytes(4);
    let head;

    if (payload.length < 126) {
        head = Buffer.alloc(2);
        head[1] = 0x80 | payload.length;
    } else {
        head = Buffer.alloc(4);
        head[1] = 0x80 | 126;
        head.writeUInt16BE(payload.length, 2);
    }
    head[0] = 0x81;
    return Buffer.concat([head, mask, Buffer.from(payload.map((b, i) => b ^ mask[i & 3]))]);
}

function pushFrames(buf, onFrame) {
    while (buf.length >= 2) {
        const op = buf[0] & 0x0f;
        let len = buf[1] & 0x7f;
        let off = 2;

        if (len === 126) {
            if (buf.length < 4) break;
            len = buf.readUInt16BE(2); off = 4;
        } else if (len === 127) {
            if (buf.length < 10) break;
            len = Number(buf.readBigUInt64BE(2)); off = 10;
        }
        const masked = (buf[1] & 0x80) !== 0;
        const maskLen = masked ? 4 : 0;
        if (buf.length < off + maskLen + len) break;
        let payload = buf.slice(off + maskLen, off + maskLen + len);
        if (masked) {
            const m = buf.slice(off, off + 4);
            payload = Buffer.from(payload.map((b, i) => b ^ m[i & 3]));
        }
        buf = buf.slice(off + maskLen + len);
        if (op === 0x9) continue;
        onFrame(payload.toString('utf8'));
    }
    return buf;
}

const sock = net.connect(port, host, () => {
    sock.write(
        `GET ${path}?key=${key} HTTP/1.1\r\n` +
        `Host: ${host}:${port}\r\n` +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        `Sec-WebSocket-Key: ${wsKey}\r\n` +
        'Sec-WebSocket-Version: 13\r\n\r\n');
    console.log(`→ ${host}:${port}${path}`);
});

let buf = Buffer.alloc(0);
let upgraded = false;
let got = 0;

function show(text) {
    let msg;
    try {
        msg = JSON.parse(text);
    } catch (e) {
        console.log('（非 JSON）', text.slice(0, 120));
        return;
    }
    if (msg.type !== 'mode') return;                 // 只关心组网模式这条
    got++;
    console.log(`[${got}] mode = ${text}`);
}

sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);

    if (!upgraded) {
        const end = buf.indexOf('\r\n\r\n');
        if (end < 0) return;
        const head = buf.slice(0, end).toString('latin1');
        buf = buf.slice(end + 4);
        const status = head.split('\r\n')[0];
        console.log('←', status);
        if (!/ 101 /.test(status)) {
            if (buf.length) console.log('   body:', buf.toString('utf8').trim());
            sock.end();
            return;
        }
        upgraded = true;
        sock.write(maskFrame(JSON.stringify({ action: 'mode_get', modem_id: 0 })));
        console.log('→ 发送 {"action":"mode_get","modem_id":0}');
        if (value) {
            setTimeout(() => {
                const cmd = JSON.stringify({ action: 'mode_set', modem_id: 0, value });
                sock.write(maskFrame(cmd));
                console.log('→ 发送', cmd);
            }, 1500);
        }
    }

    buf = pushFrames(buf, show);
});

sock.on('error', (e) => {
    console.log('连接失败:', e.message);
    process.exit(1);
});

setTimeout(() => {
    console.log(got ? `（收到 ${got} 条 mode 推送）` : '（没收到 mode 推送）');
    process.exit(0);
}, value ? 12000 : 5000);
