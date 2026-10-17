/*
 * 屏幕端 → 服务端 的一条指令探针：连上 /display/ws?key=… ，发一条指令 JSON，再把回执打出来。
 *
 *   node send.js 192.168.1.1 8080 h29k-tft-9f3c1a7b /display/ws sms_read 1
 *
 * ⚠️ 参数故意是 action/index 而不是一整串 JSON：PowerShell 5.1 把参数交给原生程序时
 *    会把里面的双引号**吃掉**，传 `{"action":...}` 到 node 就变成 `{action:...}`（踩过）。
 *    所以 JSON 在脚本里拼。
 *
 * 用途：验证屏幕发的指令服务端到底收没收到、有什么广播回来（不依赖 npm 包）。
 * 参数：host [port] [key] [path] [action] [index]
 */
const net = require('net');
const crypto = require('crypto');

const host = process.argv[2] || '127.0.0.1';
const port = parseInt(process.argv[3] || '8080', 10);
const key = process.argv[4] || 'h29k-tft-9f3c1a7b';
const path = process.argv[5] || '/display/ws';
const action = process.argv[6] || 'snapshot';
const index = process.argv[7] || '';

const json = JSON.stringify(index ? { action, modem_id: 0, index } : { action });
const wsKey = crypto.randomBytes(16).toString('base64');

/* 客户端 → 服务端必须打掩码（RFC 6455 §5.3） */
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
    head[0] = 0x81;                                     // FIN + text
    const masked = Buffer.from(payload.map((b, i) => b ^ mask[i & 3]));
    return Buffer.concat([head, mask, masked]);
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
        if (op === 0x9) continue;                        // ping
        onFrame(op, payload.toString('utf8'));
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
    console.log(`→ ${host}:${port}${path}?key=${key}`);
});

let buf = Buffer.alloc(0);
let upgraded = false;
let sent = false;
let got = 0;

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
        sock.write(maskFrame(json));
        console.log('→ 发送', json);
        sent = true;
    }

    buf = pushFrames(buf, (op, text) => {
        if (op === 0x8) return;
        got++;
        console.log(`[${got}] ${text.slice(0, 200)}`);
        if (got >= 8) {
            sock.end();
            setTimeout(() => process.exit(0), 200);
        }
    });
});

sock.on('error', (e) => {
    console.log('连接失败:', e.message);
    process.exit(1);
});

setTimeout(() => {
    console.log(sent ? '（结束）' : '（超时，没连上）');
    process.exit(0);
}, 8000);
