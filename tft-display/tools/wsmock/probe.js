/*
 * 原始 WebSocket 探针：连上 /display/ws?key=…，打印前几条推送原文。
 * 不依赖任何 npm 包（node 内置 net + crypto 自己实现握手与帧解析）。
 *
 *   node probe.js 192.168.1.1 8080 h29k-tft-9f3c1a7b /display/ws 5
 *
 * 参数：host [port] [key] [path] [条数]
 */
const net = require('net');
const crypto = require('crypto');

const host = process.argv[2] || '127.0.0.1';
const port = parseInt(process.argv[3] || '8080', 10);
const key = process.argv[4] || 'h29k-tft-9f3c1a7b';
const path = process.argv[5] || '/display/ws';
const want = parseInt(process.argv[6] || '5', 10);

const wsKey = crypto.randomBytes(16).toString('base64');
const expect = crypto.createHash('sha1')
    .update(wsKey + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64');

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
        const m = /sec-websocket-accept:\s*(\S+)/i.exec(head);
        if (m) console.log('   accept', m[1] === expect ? 'OK' : `不一致（期望 ${expect}）`);
        if (!/ 101 /.test(status)) {
            // 非 101：把剩下的正文也打出来（401/404/426 都能看出原因）
            if (buf.length) console.log('   body:', buf.toString('utf8').trim());
            sock.end();
            return;
        }
        upgraded = true;
    }

    while (upgraded && got < want) {
        if (buf.length < 2) return;
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
        if (masked) off += 4;
        if (buf.length < off + len) return;

        const payload = buf.slice(off, off + len);
        buf = buf.slice(off + len);
        got++;
        console.log(`[${got}] op=${op} len=${len}`);
        console.log('    ' + payload.toString('utf8'));
    }

    if (got >= want) sock.end();
});

sock.on('error', (e) => console.log('连接失败:', e.message));
sock.on('close', () => console.log('关闭'));
setTimeout(() => { console.log('（超时，主动退出）'); sock.destroy(); }, 15000);
