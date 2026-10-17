# luci-theme-daocaoren（OpenWrt 上游版）

Argon 派生主题，改成 daocaoren 的配色。**这套 CSS 是"手改产物"，`less/` 不参与构建**
（`less/*.less` 是上游 argon 的源码备份，里面的颜色和实际出货的 CSS 已经不一致，别照它改）。

## 文件职责

| 文件 | 作用 |
|---|---|
| `htdocs/luci-static/daocaoren/css/cascade.css` | **浅色/默认**样式（手改维护的那份） |
| `htdocs/luci-static/daocaoren/css/dark.css` | **深色**样式。由 `luasrc/view/themes/daocaoren/header.htm` **条件内联**：<br>· `mode=normal` → 包在 `@media (prefers-color-scheme: dark){…}` 里<br>· `mode=dark` → 无条件注入 |
| `htdocs/luci-static/daocaoren/css/modern.css` | 登录页专用 |
| `htdocs/luci-static/daocaoren/css/cascade copy.css` | 历史备份，**没有任何地方引用** |
| `htdocs/luci-static/daocaoren/fonts/argon.*` | 菜单图标的图标字体（伪元素 `content:"\e9xx"`） |
| `root/etc/config/daocaoren` | 主题外观 UCI（`primary` / `dark_primary` / `blur` / `mode`…），`header.htm` 读它写进 `:root` |

**深色覆盖为什么能生效**：`cascade.css` 先 `link`，`dark.css` 后内联 ⇒ 同优先级时"后出现的赢"。
若要更保险（或将来顺序变了），覆盖规则加 `body` 前缀抬高优先级。

## 菜单图标配色（2026-10-13 用户要求）

| 模式 | 颜色 |
|---|---|
| 浅色 | `#242424` |
| 深色 | `#ffffff` |

实现：两个文件**各追加一个标记清晰的块**（文件末尾）：

- `cascade.css` 末尾 —— 把 `.nav>li>a:first-child::before`、`.menu::before` 以及
  12 个 `.menu[data-title=XXX]:before` 统一改成 `#242424`
  （原来这些是 `#000000`，Status/Control 还是 `var(--primary)` 主题蓝）。
- `dark.css` 末尾 —— 同样的选择器列表加 `body` 前缀 + `!important`，改成 `#ffffff`
  （原来深色下图标是黑的，压在 `#333333` 侧栏上几乎看不见）。

**不受影响**：悬停态 `.menu:hover::before` 和展开态 `.menu.active::before` 仍是白色
（它们的背景是主题色，`!important` 的那两条规则照旧）；`Log out` 图标浅色下仍是 `#adb5bd`。

**想改回主题色**：把两个文件末尾那两块（带 `菜单图标配色（2026-10-13 用户要求）` /
`菜单图标：深色模式改白色` 注释的）删掉即可，前面的原始规则一个字没动。

### 验证方法（可复现）

拿真 CSS + 真字体 + 真侧栏 DOM 做个最小页面，用 `getComputedStyle(el,'::before').color` 读：

```html
<link rel="stylesheet" href="css/cascade.css">
<link rel="stylesheet" href="css/dark.css">   <!-- 模拟 header.htm 的注入顺序 -->
<div class="main"><div class="main-left" id="mainmenu"><ul class="nav">
  <li class="slide"><a class="menu" data-title="System">System</a></li>
  …
</ul></div></div>
```

实测：浅色 `rgb(36,36,36)`、深色 `rgb(255,255,255)`，侧栏底色 `rgb(51,51,51)`。
样例页与截图：`E:\Linux\dump\themetest\`（`light.html` / `dark.html` / `shot-*.png`）。

## 品牌 logo（`img/argon.svg`）跟随主题色（2026-10-13 用户要求）

`argon.svg` 是**黑字形**的矢量图，放在 `<img>` 里没法改颜色（CSS 的 `color` 对 `<img>` 无效）。
所以改成**蒙版 + 主题色填充**：

| 改动 | 内容 |
|---|---|
| `luasrc/view/themes/daocaoren/header.htm` | `<img src=".../img/argon.svg" class="icon" width=32 height=32 style="margin:10px 20px -6px 0">` → `<i class="brand-logo"></i>` |
| `luasrc/view/themes/daocaoren/sysauth.htm` | 同上（登录页品牌块） |
| `cascade.css` 末尾 | `.brand-logo{background-color:var(--primary); -webkit-mask:url('../img/argon.svg') …; mask:…}` |
| `dark.css` 末尾 | `body … .brand-logo{background-color:var(--dark-primary)}` |
| `modern.css` | 删掉旧的 `html[data-ds=dark] … img.icon{background: rgba(255,255,255,.9)}`（"深色下给 logo 垫浅底"）hack —— 现在 logo 自带颜色，不需要了 |

- `img/argon.svg` **必须留在包里**（蒙版要用它）。
- 主题色来自 UCI（`daocaoren.global.primary` / `dark_primary`），`header.htm` 写进 `:root`
  ⇒ 改主题色，logo 自动跟着变。

### ⚠️ 垂直居中：别再退回 `-6px` 负边距

原来 `<img>` 靠内联 `margin:10px 20px -6px 0px` 的**负下边距**去凑 `<img>` 的基线对齐。
换成 `<i>` 后这套补偿就变成"偏上"，所以改成真正的居中：

```css
.main-left .sidenav-header .brand {
	display: flex;          /* 原来是 display:block */
	align-items: center
}
.main-left .sidenav-header .brand .brand-logo {
	flex: 0 0 auto;
	width: 32px; height: 32px;
	margin: 0 20px 0 0        /* 不要再写 10px / -6px */
}
```

实测（本地复现页量 `getBoundingClientRect()`）：logo 中心 Y 与文字中心 Y **差 0.1px**。

> 登录页本来就 `.brand{display:flex;align-items:center;justify-content:center}`，不受影响。

## `header::after`（header 底部那条 2rem 色带）与背景一致（2026-10-13）

`header::after` 是 `.main-right > header` 里的一个绝对定位伪元素（`height:2rem;width:100%`），
压在 header 下方、**内容区之上**。它的颜色必须等于"它下方那块背景"，否则内容区顶部会出现一道横杠。

实测（本地复现 `getComputedStyle`）：

| 模式 | body | `.main-right`（内容区） | `header .fill`（header 本身） | 改动前 `::after` |
|---|---|---|---|---|
| 浅色 | `#f4f5f7` | **`#fff`** | `#f4f5f7` | `#f4f5f7` ← 白底灰杠 |
| 深色 | `#1e1e1e` | **`#1e1e1e`** | `var(--primary)` | `var(--primary)` ← 深底橙杠 |

深色那份本来在 dark.css 第 41 行写的是 `#1e1e1e`，但被文件后面第 157 行的
`header::after{…background-color:var(--primary)!important}` 覆盖成了主题色。

修法（在两文件末尾各追加一条，和前面的做法一致）：

```css
/* cascade.css（浅色）*/
header::after { background-color: #fff !important }

/* dark.css（深色）：body 前缀抬高优先级，钉死不被 var(--primary) 抢走 */
body header::after { background-color: #1e1e1e !important }
```

验证：两种模式下 `header::after` 的 computed color 与 `.main-right` 的 background-color **完全相等**。

> 若以后改了内容区底色（`.main-right`），记得同步这两处；或者干脆把 `background-color`
> 写成 `transparent`（那样它永远等于身下背景）。

## 顶栏固定 + 底色改成"淡一点的主题色"（2026-10-13）

### 固定顶部：用 `position:sticky`，不要 `fixed`

`.main-right` 本身是滚动容器（`height:100%; overflow-y:auto`），所以只要给 header 加
`position:sticky; top:0` 就够了 —— 不用 `position:fixed`（那样得给 `.main-right` 补
`padding-top`、还要算侧栏偏移），也不会跑出滚动容器。

```css
header { position: sticky; top: 0; z-index: 20 }
```

`z-index:20` 是刻意低于 `.main-left{position:fixed;z-index:100}` 和 `.darkMask{z-index:99}`，
这样移动端侧栏/遮罩仍然盖在顶栏之上。

### 底色淡化：`--primary` + 半透明叠加（不用 `color-mix`，老浏览器也能用）

顶栏可见的那条是 `header .fill`（不是 `header` 本身）。

| 模式 | 写法 | 实际观感（`--primary=#f59e0b` 时） |
|---|---|---|
| 浅色 | `background-color:var(--primary)` + `linear-gradient(rgba(255,255,255,.72)…)` | `rgb(252,228,187)` 淡橙 |
| 深色 | 底色仍是 dark.css 的 `var(--primary)`，再叠 `linear-gradient(rgba(0,0,0,.62)…)` | `rgb(93,60,4)` 柔和深橙 |

⚠️ **深色下"淡"是往深色方向压**，不是加白：往白里调会变成刺眼的米白条。
想调浓淡只改那两个 alpha 值即可（浅色 `.72`、深色 `.62`）。

⚠️ 浅色那条**必须同时写 `background-color: var(--primary)`**：dark.css 里 `.fill` 的底色
本来就是 `var(--primary)`，但浅色下 `.fill` 的底色是 `--background-color`(#f4f5f7 灰)，
只叠白的话主题色根本不参与（会得到接近纯白的一条）。

### 配套：`header::after` 置透明

那条 2rem 色带是 `header::after`，`position:absolute` 挂在 header 下沿、
**压在滚动内容之上**。顶栏固定后它会一直悬着，不透明就会把滚动内容挡掉 32px，
所以两个模式都改成 `background-color: transparent`（透明 = 显示身下背景，
"和背景色一样"这个要求依然满足）。

### 验证方法

本地最小页（`.main-right` 设成滚动容器 + 60 行内容），滚到底后读 `header.getBoundingClientRect().top`：
`position=sticky / top=0px / z-index=20`，滚动 500px 后 **top 仍为 0**；
顶栏实际颜色可用"background-color 与 gradient 的 alpha 手工合成"算出来（脚本见提交记录）。

### 顶栏内边距：`.4rem 0`（2026-10-13）

```css
header .fill { padding: .4rem 0 !important }   /* 原来 .8rem 0 */
```

顶栏高度 = `.fill` 上下 padding + 里面 `.container{height:2rem}`：
`.8rem` ⇒ ≈58px，`.4rem` ⇒ **≈45px**（本地实测 `getBoundingClientRect().height`）。

⚠️ 这里**必须带 `!important`**：`dark.css` 里还有一条同选择器的
`header .fill{padding:.8rem 0;…}`，它在 cascade.css 之后加载（同优先级后者生效），
不带 `!important` 的在深色模式下会被盖回去。

## ⚠️ 顶栏底色最终形态：**固定 `#e9e9e9`**（2026-10-13，取代上面的"淡一点主题色"）

用户要求底色"固定这个，不用被修改"，所以前面那套"`--primary` + 半透明叠加"的淡化方案已**废弃**：

```css
/* cascade.css 末尾 */
body header .fill {
	background-color: #e9e9e9 !important;
	background-image: none !important      /* 必须清掉，否则 dark.css 叠的黑还在 */
}

/* 底色是固定的浅灰 ⇒ 深色模式下原来是白字白图标，看不见，所以前景一并钉死 */
body header { color: #242424 !important }
body .main .main-right > header .dc-act { color: #4a5568 !important }
```

要点：

- **必须用 `body` 前缀**：dark.css 里有 `header .fill{…background-color:var(--primary)!important}`，
  同选择器时后加载的赢，加 `body` 抬高优先级才压得住。
- **必须写 `background-image: none !important`**：dark.css 里给 `.fill` 叠过一层半透明黑，
  只改 `background-color` 的话合成出来不是 `#e9e9e9`。
- **必须钉前景色**：`.dc-act` 的图标色由 `html[data-dc-mode="dark"] … {color:rgba(255,255,255,.92)}`
  控制（选择器优先级比我们的高），但那条**没有 `!important`**，所以我们的 `!important` 能赢。
- 验证：本地复现页两种模式下 `.fill` 的 computed `background-color` 都是 `rgb(233,233,233)`、
  `background-image` 为 `none`；`header` 文字 `rgb(36,36,36)`、`.dc-act` 图标 `rgb(74,85,104)`。

> 如果以后只想让**浅色**模式用它、深色模式仍跟主题色走，把上面这几条挪进
> `cascade.css` 里并用 `html[data-dc-mode="light"] ` 前缀限定即可。

### 更正：深色不是纯黑，而是页面背景色 `#1e1e1e`（2026-10-13）

用户补充"深色背景是 #1e1e1e"，所以深色顶栏底色由 `#000` 改为 **`#1e1e1e`**
（即 `.main-right` / `body` 在深色下的底色），这样顶栏与内容区同色、不会出现一条"纯黑带"。

```css
/* dark.css */
body header .fill { background-color: #1e1e1e !important; background-image: none !important }
```

最终两模式对照（本地复现页读 computed style，顶栏与内容区背景**完全相等**）：

| 模式 | `.fill` 顶栏 | 内容区 `.main-right` | 是否同色 | 前景 |
|---|---|---|---|---|
| 浅色 | `rgb(255,255,255)` | `rgb(255,255,255)` | ✓ | 文字 `#242424`、图标 `#4a5568` |
| 深色 | `rgb(30,30,30)` = `#1e1e1e` | `rgb(30,30,30)` | ✓ | 文字 `#fff`、图标 `rgba(255,255,255,.92)` |
