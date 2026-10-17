/*
 * 模拟"8080 被别的服务占着"：对任何请求都回 404。
 * 用来验证屏幕端"候选端口自动发现"（8080 上没有 /display/ws → 自动换下一个端口）。
 *
 *   node notfound.js 8080
 */
const http = require('http');
const port = parseInt(process.argv[2] || '8080', 10);

http.createServer((req, res) => {
    console.log(new Date().toISOString(), req.method, req.url, '-> 404');
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('404 No such service here\n');
}).listen(port, '0.0.0.0', () => {
    console.log(`[notfound] 监听 0.0.0.0:${port}，所有请求回 404`);
});
