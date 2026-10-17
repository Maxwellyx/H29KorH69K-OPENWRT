/*
 * svg2lvgl —— 把 web 端（modemd web/index.html 里的 OP_LOGO / OP_LOGO_OTHER）
 * 那几张运营商 SVG 官方矢量图渲染成 LVGL 图片（C 数组），给屏幕端的 Mobile 页当运营商徽标用。
 *
 *   node index.js [web/index.html 路径] [输出 .c 路径] [边长像素]
 *
 * 为什么走"离线转成 C 数组"而不是运行时解码：
 * 屏幕端 lv_conf.h 里没开 PNG/SVG 解码器，C 数组（ARGB8888）零依赖、启动即用。
 *
 * 输出格式：LV_COLOR_FORMAT_ARGB8888 —— 内存里是 **B,G,R,A** 四字节（见 lv_color32_t 的
 * 字段顺序 blue/green/red/alpha），不是 RGBA，别弄反。
 */
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const HTML = process.argv[2] || 'd:\\Go\\5g-modem-manager\\web\\index.html';
const OUT = process.argv[3] || 'd:\\Go\\tftdisplay\\src\\utils\\lv_img_op_logos.c';
const SIZE = parseInt(process.argv[4] || '52', 10);

/* ---- 1. 从 index.html 里把两段 JS 对象字面量抠出来求值（不手抄超长 path 数据） ---- */
const html = fs.readFileSync(HTML, 'utf8');
const iStart = html.indexOf('const OP_LOGO = {');
const iEnd = html.indexOf('const CARRIER_BY_MNC');
if (iStart < 0 || iEnd < 0 || iEnd <= iStart) {
    throw new Error('在 index.html 里没找到 OP_LOGO / OP_LOGO_OTHER 段');
}
const snippet = html.slice(iStart, iEnd);
const { OP_LOGO, OP_LOGO_OTHER } = new Function(
    snippet + '\nreturn { OP_LOGO, OP_LOGO_OTHER };')();

/* ---- 2. 拼成完整 SVG 文档 ---- */
function svgOf(key) {
    const l = OP_LOGO[key];
    if (!l || !l.glyph) throw new Error('缺少 logo: ' + key + '（web 端改过键名？）');
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${l.viewBox}">${l.glyph}</svg>`;
}

/*
 * 未识别运营商的中性占位图：**地球**（网络/运营商的中性符号）。
 * ⚠️ 不要再用 web 端那个 `OP_LOGO_OTHER`（一张 SIM 卡轮廓）：渲染到 52px 之后
 * 用户一眼看成了**电池**，已按要求换掉。
 * 只描边不填色 ⇒ 背景透明，底下块的深灰会透出来。
 */
const OTHER_SVG =
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" ' +
    'stroke="#cfcfcf" stroke-width="1.6" stroke-linecap="round">' +
    '<circle cx="12" cy="12" r="9"/>' +
    '<ellipse cx="12" cy="12" rx="4.2" ry="9"/>' +
    '<path d="M3.2 9h17.6M3.2 15h17.6"/>' +
    '</svg>';

/* ---- 3. 渲染成 RGBA 像素 → 写成 LVGL 的 ARGB8888（内存序 B,G,R,A） ---- */
async function toC(name, svg) {
    const { data, info } = await sharp(Buffer.from(svg))
        .resize(SIZE, SIZE, { fit: 'contain', background: { r: 0, g: 0, b: 0, alpha: 0 } })
        .raw()
        .ensureAlpha()
        .toBuffer({ resolveWithObject: true });

    const w = info.width, h = info.height;
    if (info.channels !== 4) throw new Error('通道数不是 4: ' + info.channels);

    const rows = [];
    for (let y = 0; y < h; y++) {
        const px = [];
        for (let x = 0; x < w; x++) {
            const o = (y * w + x) * 4;
            const r = data[o], g = data[o + 1], b = data[o + 2], a = data[o + 3];
            px.push(`0x${b.toString(16).padStart(2, '0')},0x${g.toString(16).padStart(2, '0')},0x${r.toString(16).padStart(2, '0')},0x${a.toString(16).padStart(2, '0')}`);
        }
        rows.push('    ' + px.join(',') + ',');
    }

    return {
        w, h,
        c:
`/* ${name}（源：web/index.html 的 OP_LOGO，${w}x${h} ARGB8888） */\n` +
`static const uint8_t ${name}_map[] = {\n${rows.join('\n')}\n};\n\n` +
`const lv_image_dsc_t ${name} = {\n` +
`    .header.magic = LV_IMAGE_HEADER_MAGIC,\n` +
`    .header.cf = LV_COLOR_FORMAT_ARGB8888,\n` +
`    .header.w = ${w},\n` +
`    .header.h = ${h},\n` +
`    .header.stride = ${w * 4},\n` +
`    .data_size = sizeof(${name}_map),\n` +
`    .data = ${name}_map,\n};\n`,
    };
}

(async () => {
    const jobs = [
        ['lv_img_op_cmcc', svgOf('cmcc')],
        ['lv_img_op_unicom', svgOf('unicom')],
        ['lv_img_op_telecom', svgOf('telecom')],
        ['lv_img_op_cbn', svgOf('cbn')],
        ['lv_img_op_other', OTHER_SVG],
    ];

    const parts = [], decls = [];
    for (const [name, svg] of jobs) {
        const r = await toC(name, svg);
        parts.push(r.c);
        decls.push(`LV_IMAGE_DECLARE(${name});`);
        console.log(`  ${name.padEnd(20)} ${r.w}x${r.h}`);
    }

    const out =
`/*\n * 本文件由 tools/svg2lvgl/index.js 自动生成，**不要手改**。\n` +
` * 源：${path.basename(HTML)} 里的 OP_LOGO / OP_LOGO_OTHER（运营商官方矢量 logo）。\n` +
` * 重新生成：node tools/svg2lvgl/index.js\n */\n\n` +
`#include "lvgl.h"\n\n` +
parts.join('\n') + '\n';

    fs.writeFileSync(OUT, out);
    console.log(`\n写出 ${OUT}（${(out.length / 1024).toFixed(1)} KB）`);
})().catch((e) => { console.error('失败:', e.message); process.exit(1); });
