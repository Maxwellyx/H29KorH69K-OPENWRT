# luci-app-oled —— OpenWrt 移植说明

来源：`~/LS/LEDE-Develop/package/daocaoren/APP/luci-app-oled`

原目录是**两层嵌套**的：

```
luci-app-oled/                 <- 外层：README.md + linux/(systemd 用) + 真正的包
└── luci-app-oled/             <- 内层才是 OpenWrt 包（有 Makefile）
```

移植时**去掉了一层**，现在直接是
`package/daocaoren/luci-app-oled/{Makefile,luasrc,root,src,po,LICENSE}`。
外层的 `linux/ssd1306.cfg`、`linux/ssd1306.service` 是给普通 Linux + systemd 用的，
OpenWrt 用不上（OpenWrt 走 `root/etc/init.d/oled` + procd），所以没有拷。
外层 `README.md` 保留成 `README-upstream.md` 供参考。

## 我改了什么（以及为什么）

### 1. `src/Makefile` —— `:=` 改成 `+=`【这条不改必然链接失败】

原版：

```make
CPPFLAGS := -I SSD1306_OLED_Library -I I2C_Library
CFLAGS   := -O2
LDFLAGS  := -lpthread -lconfig
```

问题在 openwrt 的构建方式：`include/package.mk` 的 `Build/Compile/Default` 是**以命令行变量**
的形式把 `CPPFLAGS` / `CFLAGS` / `LDFLAGS` 传进去的（里面含 `-I$(STAGING_DIR)/usr/include`、
`-L$(STAGING_DIR)/usr/lib` 这些交叉编译必需路径）。
make 的**命令行变量会覆盖 Makefile 里的赋值** —— 于是 `-lconfig -lpthread` 被丢掉，
链接时必然 `undefined reference to config_*`。

改成 `+=` 之后两边都保留，谁都不吃亏。

### 2. `Makefile`

- 删掉 `LUCI_PKGARCH:=$(if $(realpath src/Makefile),,all)`：
  `feeds/luci/luci.mk` 第 12 行本来就有同样的默认值
  `LUCI_PKGARCH?=$(if $(realpath src/Makefile),,all)`，
  而且 luci.mk 里用的是 `${CURDIR}/src/Makefile`（路径基准正确），
  原版那句的 `realpath` 基准是调用方的 cwd，反而可能判断成 `all`
  —— 那就会把**交叉编译出来的二进制**打成 `PKGARCH:=all` 的平台无关包，换架构装不上。
  交给 luci.mk 的默认值即可（有 `src/Makefile` ⇒ arch 包）。
- `PKG_LINCESE_FILES` → `PKG_LICENSE_FILES`（原版拼错，少个 S，等于没设）
- `LUCI_DEPENDS` 从 `+libconfig` 改成 `+luci-base +luci-compat +libconfig`
  —— Lua 版 CBI，现代 LuCI 需要 `luci-compat`
- `PKG_LICENSE` 写成 SPDX（`GPL-3.0`）
- `include $(TOPDIR)/feeds/luci/luci.mk` 保持不动（openwrt 里这个路径是对的）

`luci.mk` 会自动做这几件事，不用手写：
`${CURDIR}/src/Makefile` 存在 ⇒ `PKG_INSTALL:=1` 且 `Build/Compile` = `make clean compile`，
装的时候按 `make install DESTDIR=...` 走，所以 `src/Makefile` 的 `install:` 目标
会把 `oled` 装到 `/usr/bin/oled`；`LUCI_DEPENDS` 里也会自动补 `+luci-lua-runtime`。

### 3. `root/etc/init.d/oled`

- `[[ ... ]]`（bash 语法）→ `[ ... ]`，不依赖 busybox 的 `BASH_COMPAT`
- 原来用 `eval "export -n mainsection=..."` 取匿名 section 名，
  busybox 的 `export` 不一定支持 `-n`；改成 `config_foreach` 把 section 名存进普通变量
- 参数拼装逻辑与原版**逐条一一对应**，没有改行为

### 4. `root/etc/uci-defaults/oled`

- `[[ ... ]]` → `[ ... ]`；`[ "$x" -eq 1 ]` 在 `x` 为空时会报
  `integer expression expected`，改成字符串比较
- heredoc 正文空格缩进 → tab（`<<-` 只剥 tab）
- **`board_name = hinlink,opc-h69k` 时写 `/dev/i2c-5`，这条保留** ——
  它对应设备树里新加的 `&i2c5 { status = "okay"; }`（见 `dts/README.md`）
- `/etc/crontabs/root` 的 sed 加了存在性判断

## 依赖链

```
luci-app-oled
├── luci-base       (Web 框架)
├── luci-compat     (Lua CBI 兼容层 —— 这个包是 Lua 写的)
├── luci-lua-runtime(luci.mk 自动加)
└── libconfig       (src/main.c 用 config_read_file 解析 /etc/oled 相关的 cfg)
```

## 和屏幕对应的硬件

| 机型 | 屏幕 | 接口 | 本包是否适用 |
|---|---|---|---|
| OPC-H69K | SSD1306 0.91" 128x32 OLED | i2c5（`/dev/i2c-5`） | ✔ |
| OPC-H29K | GC9307 TFT + AXS5106 触摸 | SPI | ✘ 用 `tft-display` 包 |

即：**`luci-app-oled` 只给 H69K**，H29K 不在 `DEVICE_PACKAGES` 里加它。
