/*
 * 生成「农历页」用的 28px 中文子集字库的字符清单（农历页中间那行大字）。
 *
 * 为什么单独做一套 28px 子集：农历页要的是**大字**（"八月廿七" / "丙午 马年"），
 * 而现有的 cjk 字体只有 16px 全字库（短信页那套）—— 16px 打不大，
 * 全字库放大到 28px 又会顶爆 LVGL 的位图上限。所以只挑农历用得到的这几十个字。
 *
 *   node tools/gen/lunarfont.js          # 写 tools/gen/lunarfont.txt（UTF-8 无 BOM）
 *
 * 再用 lv_font_conv 生成字体（与 gb2312chars.js 同一套用法）：
 *
 *   $c = [IO.File]::ReadAllText("d:\Go\tftdisplay\tools\gen\lunarfont.txt", [Text.Encoding]::UTF8)
 *   lv_font_conv --font C:\Windows\Fonts\simhei.ttf --size 28 --bpp 2 --no-compress `
 *       --format lvgl --lv-include "lvgl/lvgl.h" --lv-font-name lv_font_cjk_lunar_28 `
 *       -r 0x20-0x7e --symbols "$c" -o src\utils\lv_font_cjk_lunar_28.c
 *
 * ⚠️ 一样不能漏 `-r 0x20-0x7e`：年月日里的数字与 "2026-10-07" 全靠 ASCII
 *   （漏了就是一堆空心方框，短信页踩过这个坑）。
 */
const fs = require('fs');
const path = require('path');

/* 天干 */
const GAN = '甲乙丙丁戊己庚辛壬癸';
/* 地支 */
const ZHI = '子丑寅卯辰巳午未申酉戌亥';
/* 生肖 */
const ANIMALS = '鼠牛虎兔龙蛇马羊猴鸡狗猪';
/* 月名：正月…十月 + 冬月 + 腊月 */
const MONTHS = '正二三四五六七八九十冬腊';
/* 日名用字：初一…初十、十一…十九、二十、廿一…廿九、三十 */
const DAYS = '初十廿卅一二三四五六七八九';
/* 其它出现在大字行里的字 */
const EXTRA = '年月日闰农历公历星期本无至';

const all = GAN + ZHI + ANIMALS + MONTHS + DAYS + EXTRA;
const uniq = Array.from(new Set(Array.from(all))).join('');

const file = path.join(__dirname, 'lunarfont.txt');
fs.writeFileSync(file, uniq, 'utf8');
console.log('chars=' + uniq.length + '  -> ' + file + '  (' + uniq + ')');
