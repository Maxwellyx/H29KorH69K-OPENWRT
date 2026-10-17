/*
 * 生成"短信页全字库"用的字符清单：GB2312/GBK 里能编码出来的全部汉字 + 常用标点。
 *
 * 为什么要它：短信正文是**任意汉字**，不能像别的页面那样只挑几十个字做子集；
 * 但 U+4E00–U+9FA5 全量（20902 字）在 16px bpp2 下超出 LVGL 的 1MB 位图上限
 * （LV_FONT_FMT_TXT_LARGE=0 时 bitmap_index 只有 20 位）。
 * GB2312 的 6763 个汉字覆盖日常中文（含二级字），位图 433KB ✓ 稳在限内、字也够全。
 *
 *   node tools/gen/gb2312chars.js            # 写 tools/gen/gb2312.txt（UTF-8 无 BOM）
 *
 * 再用 lv_font_conv 生成屏幕用的字体（本工程的工具链已装，直接 lv_font_conv 就行）：
 *
 *   $c = [IO.File]::ReadAllText("d:\Go\tftdisplay\tools\gen\gb2312.txt", [Text.Encoding]::UTF8)
 *   lv_font_conv --font C:\Windows\Fonts\simhei.ttf --size 16 --bpp 2 --no-compress `
 *       --format lvgl --lv-include "lvgl/lvgl.h" --lv-font-name lv_font_cjk_sms_16 `
 *       -r 0x20-0x7e --symbols "$c" -o src\utils\lv_font_cjk_sms_16.c
 *
 * ⚠️ `-r 0x20-0x7e` 不能省：数字/字母只能靠它进来（GBK 双字节循环产生不出 ASCII）。
 *    漏了它屏幕上的号码、时间、验证码全是“空心方框”（踩过）。
 * ⚠️ 不要用 `node ... > out.txt` 重定向：PowerShell 会按控制台代码页解码再重编码，
 *    得到的是乱码（踩过：13353 个怪字）。写入必须由 node 自己用 utf8 完成。
 */
const fs = require('fs');
const path = require('path');

const dec = new TextDecoder('gbk', { fatal: true });
let out = '';
for (let hi = 0xa1; hi <= 0xfe; hi++) {
    for (let lo = 0xa1; lo <= 0xfe; lo++) {
        try {
            out += dec.decode(new Uint8Array([hi, lo]));
        } catch (e) { /* 该区位码在 GBK 里没有字符 */ }
    }
}
// 去重（GBK 里 α/А 等有重复映射）+ 去掉控制字符
// ⚠️ 上面只扫了 GBK 的**双字节**区（0xa1-0xfe）⇒ 数字/字母/空格产生不出来，
//    这里把 ASCII 补上（短信里的号码、时间、验证码、12.34GB 全靠它）。
out = Array.from(new Set(Array.from(out))).join('');
out += Array.from({ length: 0x7f - 0x20 }, (_, i) => String.fromCharCode(0x20 + i)).join('');
out = Array.from(new Set(Array.from(out))).join('');

/*
 * 再过滤一遍：只要**排字用得上的**区段 ——
 * ASCII / 拉丁补充 / 常用标点 / CJK 标点 / 汉字 / 全角字符 / ℃。
 * GBK 里还有日文假名、西里尔、制表符那些，短信几乎用不到，去掉能省 ~200KB 位图。
 */
const KEEP = [
    [0x0020, 0x007e], // ASCII
    [0x00a0, 0x00ff], // 拉丁补充（é 之类）
    [0x2000, 0x206f], // 常用标点（—…‘’“”）
    [0x2103, 0x2103], // ℃
    [0x2460, 0x24ff], // ① ② ③
    [0x3000, 0x303f], // CJK 标点（、。《》・）
    [0x4e00, 0x9fff], // 汉字
    [0xff00, 0xffef], // 全角（＃％＊＋）
];
out = Array.from(out)
    .filter((ch) => {
        const c = ch.codePointAt(0);
        return KEEP.some(([a, b]) => c >= a && c <= b);
    })
    .join('');

const file = path.join(__dirname, 'gb2312.txt');
fs.writeFileSync(file, out, 'utf8');
console.log('chars=' + out.length + '  bytes=' + Buffer.byteLength(out, 'utf8') + '  -> ' + file);
