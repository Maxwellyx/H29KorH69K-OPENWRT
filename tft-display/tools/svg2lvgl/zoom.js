/*
 * 屏幕截图放大镜：从 PNG 里裁一块并放大（最近邻，不糊），用来看小字/字形对不对。
 *
 *   node zoom.js <in.png> <out.png> <x> <y> <w> <h> [放大倍数=4]
 *
 * 例（看 NET 子页顶部摘要行的字形）：
 *   node zoom.js ..\..\build-aarch64\r.png ..\..\build-aarch64\r_head.png 0 30 260 28 4
 */
const path = require('path');
const sharp = require('sharp');

const [inp, outp, x, y, w, h, scale] = process.argv.slice(2);
if (!inp || !outp) {
    console.error('用法: node zoom.js <in.png> <out.png> <x> <y> <w> <h> [scale]');
    process.exit(1);
}

sharp(path.resolve(inp))
    .extract({
        left: parseInt(x || '0', 10),
        top: parseInt(y || '0', 10),
        width: parseInt(w || '320', 10),
        height: parseInt(h || '172', 10),
    })
    .resize({ width: (parseInt(w || '320', 10)) * parseInt(scale || '4', 10), kernel: 'nearest' })
    .toFile(path.resolve(outp))
    .then((info) => console.log('wrote', path.resolve(outp), info.width + 'x' + info.height))
    .catch((e) => { console.error(e.message); process.exit(1); });
