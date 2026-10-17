# 
> **HDMI 支持现状（H69K 已完成 / H29K 需移植）见 [`../hdmi.md`](../hdmi.md)。**dts —— H29K / H69K 的设备支持（OpenWrt 上游布局）

> 这是 **openwrt 上游树** 的版本。lede 那套（打 `target/linux/rockchip/files/`）原样
> 保留在 `../lede-only/dts-patches/`，两边机制完全不同，别混用。

支持的设备：

| 设备 | SoC | DTS | u-boot |
|---|---|---|---|
| `hinlink_opc-h29k`（OPC-H29K） | RK3528 | `rk3528-opc-h29k.dts`（我们的，新增） | `generic-rk3528` |
| `hinlink_opc-h69k`（OPC-H69K） | RK3568 | `rk3568-opc-h69k.dts`（我们的，新增） | `generic-rk3568` |

## 为什么要做两件事

上游的 rockchip target **没有 `files/` 目录**（lede 有，70 个 dts 放那儿）。上游的设备树一律
以内核补丁的形式放在 `target/linux/rockchip/patches-6.18/`，例如
`101-arm64-dts-rockchip-Add-HINLINK-H28K.patch`。

而且 `target/linux/rockchip/Makefile` 里写着 `KERNELNAME:=Image dtbs`，`make dtbs` 只编
内核 `arch/arm64/boot/dts/rockchip/Makefile` 里列出的那些 —— 光把 .dts 文件塞进去不编 DTB，
**必须同时在那个 Makefile 里加一行**。

所以两件事缺一不可：

| 目录 | 作用 | 落地方式 |
|---|---|---|
| `patches/` | 改**上游 OpenWrt 自己的文件**（`image/armv8.mk` 加两个设备定义） | `git apply`，可反向还原 |
| `kernel-patches/` | 给**内核**用的补丁（新增 dts + 改内核 dts Makefile） | 拷进 `target/linux/rockchip/patches-6.18/` 让构建系统自己打；还原 = 删掉 |

`files/*.dts` 是唯一的**真源**，`kernel-patches/*.patch` 是从它们生成出来的产物。

## 用法

```bash
~/openwrt/package/daocaoren/dts/status.sh    # 看当前状态（含 u-boot 检查）
~/openwrt/package/daocaoren/dts/apply.sh     # 应用（幂等）
~/openwrt/package/daocaoren/dts/revert.sh    # 还原，让上游源码回到干净状态
```

## 内核版本

上游 `target/linux/rockchip/Makefile` 是 `KERNEL_PATCHVER:=6.18`，具体版本在
`target/linux/generic/kernel-6.18` 里：`LINUX_VERSION-6.18 = .55` —— **6.18.55**，
和 lede 那棵树**完全同版本**，所以 lede 上生效的设备树可以原样搬过来。

⚠️ **内核版本升级时**：`patches-6.18/` 会变成 `patches-6.19/` 之类，`kernel-patches/` 里的
补丁要跟着改成新目录名（`apply.sh` 会检测并报错提醒，不会默默跳过）。

## 两个设备的素材来源

### H29K（RK3528）

`files/rk3528-opc-h29k.dts` —— 从 lede 那棵树上**生效的版本**拷来（549 行，12268 字节），
里面包含修复过的 PWM 背光：

```dts
backlight: backlight {                 /* 必须挂在 &pinctrl 下、且不能有 reg */
    compatible = "pwm-backlight";
    pwms = <&pwm6 0 25000 1>;          /* 第 4 个 cell 是极性，本机反相 ⇒ 必须写 1 */
    brightness-levels = <0 … 255>;     /* 256 级 ⇒ max_brightness = 255 */
};
```

⚠️ **极性那个 `1` 别再动**：厂商原版就是 `1`（`PWM_POLARITY_INVERTED`）。本内核的
`dt-bindings/pwm/pwm.h` 只定义了 `PWM_POLARITY_INVERTED`，没有 `PWM_POLARITY_NORMAL`，
所以只能写字面量。改错了的表现是**亮度条整个反了**（写大值反而更暗）。

### H69K（RK3568）

`files/rk3568-opc-h69k.dts` —— 从 lede 的
`target/linux/rockchip/files/.../rk3568-opc-h69k.dts`（2386 字节，130 行）拷来，
它 `#include "rk3568-hinlink-opc.dtsi"`，那个 dtsi **mainline 6.18.55 自带**（已核对），
并加：`pwm-fan`、`vcc5v0_ahci` 稳压器、`cpu_thermal` 四档 trip + cooling-map、
`gmac1` 的 RGMII 配置、`mdio1` 的 PHY、`sata_pwr_en` 引脚、`&pwm0`、`&sata0`。

⚠️ 我额外给这个 dts 补了两行 `#include <dt-bindings/gpio/gpio.h>` /
`<dt-bindings/pinctrl/rockchip.h>`（原本靠 dtsi 传递）。**重复 include 无害**（纯 `#define`），
但能防止上游改 dtsi 的 include 列表时把我们的 dts 带崩。

## u-boot：两个 generic 配置

`image/armv8.mk` 里的 `UBOOT_DEVICE_NAME` 决定镜像组装时从
`$(STAGING_DIR_IMAGE)/<名字>-u-boot-rockchip.bin` 取 u-boot 打进镜像
（`target/linux/rockchip/image/Makefile:43` 的 `dd ... seek=64`）。

上游原本的 rk3528/rk3568 设备里**没有 generic**，所以我们在
`../openwrt-patches/patches/002-uboot-add-generic-rk35xx.patch` 里给上游补了
`U-Boot/generic-rk3528` 和 `U-Boot/generic-rk3568` 两个设备。

为什么是 generic —— 实证依据（u-boot 版本两边都是 **2026.07**）：

| 检查项 | `generic-rk3528` | `generic-rk3568` |
|---|---|---|
| defconfig 在不在 | ✔ 存在（1128 字节） | ✔ 存在（1837 字节） |
| `CONFIG_DEFAULT_DEVICE_TREE` | `rk3528-generic` | `rk3568-generic` |
| 编译产物 | ✔ `u-boot-rockchip.bin` 9196544 字节 | ✔ 同机制 |
| `CONFIG_DEBUG_UART_BASE` | `0xFF9F0000` | `0xFE660000` |
| 对上 bootscript 的 | `serial0@ff9f0000` / ttyS0 | `0xfe660000` / ttyS2（`default.bootscript` 里就是这两个值） |
| 纯 SoC 级（`CONFIG_NO_NET=y`） | ✔ | ✔ |

而 lede 那边产出的实际文件就在
`lede/staging_dir/target-aarch64_generic_musl/image/` 里：
`generic-rk3528-idbloader.img` + `generic-rk3528-u-boot.itb`
⇒ **这就是你 H29K 现在正在跑的 u-boot**。

⚠️ **lede 和上游的 u-boot 产物形态不同**：lede 的 `pine64-img` 分别 dd
`<名>-idbloader.img`(seek 64) 和 `<名>-u-boot.itb`(seek 16384)；
上游新版合并成单个 `<名>-u-boot-rockchip.bin`(seek 64)。上游的 `Build/InstallDev`
已经把 `u-boot-rockchip.bin` 拷好了，不用我们管。

⚠️ **ATF / TPL 版本和 lede 不同**：上游 `U-Boot/rk3528/Default` 用
`rk3528_bl31_v1.20.elf` + `rk3528_ddr_1056MHz_v1.11.bin`（lede 用 `v1.17` + `v1.10`）；
rk3568 用 `rk3568_bl31_v1.45.elf` + `rk3568_ddr_1560MHz_v1.23.bin`。
都是 rkbin 官方 blob，上游自己的板子也用这些，**但万一 DDR 起不来，这是第一个要试的变量**。

## 启动脚本：上游用 `default.bootscript`，和 lede 不同

lede 的 hinlink rk3568 设备用 `boot-script vop`（`vop.bootscript`），它会
**单独 load dtb + `booti`**，bootargs 里还带 `console=tty1`。

上游的 `Device/Default` 是：

```makefile
KERNEL = kernel-bin | lzma | fit lzma $(KDIR)/image-$(DEVICE_DTS).dtb
IMAGE/sysupgrade.img.gz = boot-common | boot-script $(BOOT_SCRIPT) | pine64-img | gzip | append-metadata
```

**DTB 被塞进 FIT 里了**，所以 `default.bootscript` 只要 `bootm ${kernel_addr_r}` 就够，
不需要单独 load dtb。我们两个设备都不设 `BOOT_SCRIPT`，即用 `default.bootscript`。

⚠️ 唯一差别：`default.bootscript` 的 bootargs **没有 `console=tty1`**
（就是"本地显示上不给 console 输出"）。要的话得往 `BOOT_SCRIPT` 里塞一个改过的脚本，
属于额外定制，目前没做。

## ⚠️ 补丁的上下文依赖上游的 `101-` 补丁

`kernel-patches/102-*.patch` 里改动内核 dts Makefile 的那一段，上下文包含了
`101-arm64-dts-rockchip-Add-HINLINK-H28K.patch` 加进去的那一行
（`dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3528-hinlink-h28k.dtb`）。

上游那条补丁是待进主线的补丁，**将来可能被改名/重排/被主线吸收**。一旦那样，
这个补丁会打不上。`status.sh` 会报出来，到时候重新生成即可。

（`103-*.patch` 改的是 rk3568 那一段，上下文取自 **mainline 原文件第 136-141 行**，
没有被上游任何补丁碰过，所以比 `102` 稳。）

## 重做设备树

改完 `files/*.dts` 之后重新生成内核补丁（以 H69K 为例）：

```bash
cd ~/openwrt/package/daocaoren/dts
F=files/rk3568-opc-h69k.dts
{
  cat <<'HDR'
--- a/arch/arm64/boot/dts/rockchip/Makefile
+++ b/arch/arm64/boot/dts/rockchip/Makefile
@@ -136,6 +136,7 @@
 dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-fastrhino-r68s.dtb
 dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-hinlink-h66k.dtb
 dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-hinlink-h68k.dtb
+dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-opc-h69k.dtb
 dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-lubancat-2.dtb
 dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-mecsbc.dtb
 dtb-$(CONFIG_ARCH_ROCKCHIP) += rk3568-nanopi-r5c.dtb
HDR
  echo "--- /dev/null"
  echo "+++ b/arch/arm64/boot/dts/rockchip/rk3568-opc-h69k.dts"
  echo "@@ -0,0 +1,$(wc -l < $F) @@"
  sed 's/^/+/' "$F"
} > /tmp/new.patch && mv /tmp/new.patch kernel-patches/103-arm64-dts-rockchip-add-HINLINK-OPC-H69K.patch
~/openwrt/package/daocaoren/dts/status.sh
```

## 目录内容

```
dts/
├── README.md                    ← 本文件
├── apply.sh / revert.sh / status.sh
├── gen-patches.sh               ← 从 files/*.dts 重新生成 102/103 + 001 补丁（别手写 hunk）
├── files/
│   ├── rk3528-opc-h29k.dts      ← H29K 真源（lede 生效版 + 去掉 &rng）
│   └── rk3568-opc-h69k.dts      ← H69K 真源（厂商 377 行新版 + 3 处改造）
├── kernel-patches/
│   ├── 102-arm64-dts-rockchip-add-HINLINK-OPC-H29K.patch
│   └── 103-arm64-dts-rockchip-add-HINLINK-OPC-H69K.patch
├── kernel-files/
│   └── rkrng-fragment.c        ← RKRNG 驱动要插进内核的代码片段（按 SECTION 分四段）
├── patches/
│   └── 001-armv8-hinlink-devices.patch     ← 两个 Device/ 定义都在这里
├── devices.mk                   ← lede 时代的历史文件（内容已整块注释），留着备案
├── vendor-extra/                ← 厂商原始资料（留档，不参与构建）
│   ├── rk3568-opc-h69k.vendor.dts             ← 厂商 H69K dts 原版 377 行（改造前）
│   ├── rk3568-hinlink-opc.lede-develop.dtsi   ← LD 那份 dtsi（12057 字节）
│   ├── rk3528-opc-h29k.dts / rk3528-opc-ht2.dts
│   └── kernel-patches-6.6/
├── rk3528-opc-f50pro.dts        ← 其它硬件变体，上游还没做设备定义
├── rk3528-opc-h29k-V1.3.dts
└── rk3528-opc-h29kv5.dts
```

`rk3528-opc-f50pro.dts` / `-V1.3.dts` / `h29kv5.dts` 这三个上游**没有**对应的
`Device/` 定义（`DEVICE_DTS` 不知道指向谁）。要用的话得照 `001-armv8-*.patch` 的样子再加设备。

---

## gen-patches.sh：改完 dts 别手写补丁

```bash
cd ~/openwrt/package/daocaoren/dts
./gen-patches.sh --check      # 生成并校验（patch -p1 dry-run + git apply --check）
```

它做四件事：

1. `files/rk3528-opc-h29k.dts` → `kernel-patches/102-*.patch`
2. `files/rk3568-opc-h69k.dts` → `kernel-patches/103-*.patch`
   （对着**上游内核 tarball** 里的 `arch/arm64/boot/dts/rockchip/Makefile` 生成，
   设备行按**字母序**插入，所以反复生成结果稳定）
3. 当前工作区的 `target/linux/rockchip/image/armv8.mk` → `patches/001-*.patch`
   （基准是 `git show HEAD:` 里的上游原版）
4. `kernel-files/rkrng-fragment.c` → `kernel-patches/104-*.patch`
   （把 RKRNG 驱动的四段代码插进上游 `drivers/char/hw_random/rockchip-rng.c`，
   插入点用锚点定位，见脚本里的 `after()` / `before()`）

内核 tarball 会依次在 `$TOPDIR/dl/` 和 `~/lede/dl/` 里找 `linux-<PATCHVER>*.tar.xz`。

> 为什么必须用脚本：手写 hunk 头（`@@ -136,6 +136,7 @@`）的行数**必错**，
> `git apply` 会报 `corrupt patch`。这个坑踩过不止一次。

---

## H69K：厂商新版 dts 的落地方式（2026-10-12）

厂商版（`vendor-extra/rk3568-opc-h69k.vendor.dts`，377 行）现在已整体落进
`files/rk3568-opc-h69k.dts`。相对厂商原版**只有 3 处改动**，原因如下：

### 改动 1：`&vcc3v3_pcie {...}` 改成在根节点里内联定义

厂商 dts 第 313 行是**覆盖式引用**：

```dts
&vcc3v3_pcie {
    compatible = "regulator-fixed";
    ...
};
```

它依赖 `rk3568-hinlink-opc.dtsi` 里已经有 `vcc3v3_pcie: vcc3v3-pcie { ... };` 这个**标签**。

- LD（LEDE-Develop）的 dtsi **有**（第 121 行）→ 所以厂商 dts 在 LD 树里能编
- **上游 6.18.55 的 dtsi 没有** → `dtc` 直接报
  `Label or path vcc3v3_pcie not found`

实测（把厂商 dts 放进上游内核树 `make rockchip/rk3568-opc-h69k.dtb`）：
**上游 dtsi 下只报这一个错**，别的一个都不缺。

改法：把整块搬到根节点里，加上标签定义：

```dts
	vcc3v3_pcie: vcc3v3-pcie {
		compatible = "regulator-fixed";
		...
	};
```

属性是**厂商那份覆盖块里的全集**（覆盖块把 dtsi 里的属性都重列了一遍，
所以搬过来不会丢东西）。这样我们的 dts 自给自足，
**上游 dtsi 一个字节都不用改**。

### 改动 2：补 `&i2c5 { status = "okay"; };`

H69K 的 OLED 挂在 i2c5 上（`luci-app-oled` 的 uci-defaults 里写的是 `/dev/i2c-5`），
但**上游 dtsi 只开了 `&i2c0` 和 `&i2c2`，没有 `&i2c5`**。

好消息：`rk3568.dtsi` 里的 i2c5 节点**自带默认 pinctrl**（`pinctrl-0` + `pinctrl-names = "default"`），
所以只要 `status = "okay"` 就够了，不用像别的板子那样自己写 `i2c5m0_xfer`。

验证（反编译 dtb）：

```
i2c@fe5e0000 {                  <- aliases 里 i2c5 = /i2c@fe5e0000
	compatible = "rockchip,rk3568-i2c", "rockchip,rk3399-i2c";
	pinctrl-0 = <0x90>;         <- 来自 rk3568.dtsi，不用自己写
	pinctrl-names = "default";
	status = "okay";            <- 我们要的 /dev/i2c-5
};
```

`CONFIG_I2C_CHARDEV=y` 和 `CONFIG_I2C_RK3X=y` 都是**内建**的，
所以 `/dev/i2c-5` 不需要额外选 kmod。

### 改动 3：无（其余全部原样）

包括：PCIe 三个控制器的 reset/aspm/枚举超时、SDIO WiFi（`&sdmmc2`）、
`&gmac1` 的 `tx_delay/rx_delay`、`&mdio1` 的 `ethernet-phy@0`、
`&sata0` 的 `vcc5v0_ahci`、`&pwm0` + `pwm-fan`。

---

## ⚠️ 厂商 dts 里的一个真 bug：10 档温控实际只有 6 档
## ✅ 厂商 dts 的温控 bug：已修（10 档现在真的生效）

### 问题

厂商原版 `&cpu_thermal` 的 `trips` 里，**节点名重复了**（label 不同，节点名一样）：

```dts
		cpu_active: cpu_active {   temperature = <45000>; };   // 节点名 cpu_active
		cpu_active2: cpu_active {  temperature = <50000>; };   // ← 重复
		cpu_active3: cpu_active {  temperature = <55000>; };   // ← 重复
		cpu_active4: cpu_active {  temperature = <57000>; };   // ← 重复
		cpu_active5: cpu_active {  temperature = <59000>; };   // ← 重复
		cpu_active6: cpu_warm {    temperature = <60000>; };   // 节点名 cpu_warm
		cpu_warm: cpu_warm {       temperature = <65000>; };   // ← 重复
```

DTS 里同名子节点会被**静默合并**（`dtc` 连 warning 都不报）。
实测反编译厂商版编出来的 dtb，`cpu-thermal` 只剩 6 个有效档位：
`cpu_active ~ cpu_active5` 合成一个（最后一个值 **59000** 赢），
`cpu_active6` 的 60000 被 `cpu_warm` 的 65000 覆盖。
`map3`~`map7` 全指向同一个 59000 的 trip ⇒ 风扇从 PWM 90 直接跳到 140。

### 修法

只把 trip 的**节点名**改成互不相同（`label` 换成 `-` 形式），
**label 保持原样** —— `cooling-maps` 是靠 label 引用的，不受影响：

```
cpu_low: cpu-low          cpu_passive: cpu-passive    cpu_active: cpu-active
cpu_active2: cpu-active2  cpu_active3: cpu-active3    cpu_active4: cpu-active4
cpu_active5: cpu-active5  cpu_active6: cpu-active6    cpu_warm: cpu-warm
cpu_hot: cpu-hot
```

### 实测（反编译编出来的 dtb）

```
=== cpu-thermal 的 trips（共 13 个）===
  cpu_alert0  70000  passive    cpu_alert1  75000  passive    cpu_crit  95000  critical
  cpu-low     40000  active     cpu-passive 41000  active
  cpu-active  45000  active     cpu-active2 50000  active     cpu-active3 55000  active
  cpu-active4 57000  active     cpu-active5 59000  active     cpu-active6 60000  active
  cpu-warm    65000  active     cpu-hot     70000  active
  active 档 = 10 个，温度递增 ✔ 无重名 ✔

=== cooling-maps ===
  map1  cpu-low     40000mC  fan档=0 -> PWM 0      map6  cpu-active4  57000mC  fan档=5 -> PWM 130
  map2  cpu-passive 41000mC  fan档=1 -> PWM 90     map7  cpu-active5  59000mC  fan档=6 -> PWM 140
  map3  cpu-active  45000mC  fan档=2 -> PWM 100    map8  cpu-active6  60000mC  fan档=7 -> PWM 150
  map4  cpu-active2 50000mC  fan档=3 -> PWM 110    map9  cpu-warm    65000mC  fan档=8 -> PWM 160
  map5  cpu-active3 55000mC  fan档=4 -> PWM 120    map10 cpu-hot     70000mC  fan档=9 -> PWM 200
  map 引用的 trip 去重后 = 11 个（含 rk3568.dtsi 自带的 cpu_alert0）
```

**注意**：这会改变风扇的实际行为（原先 59°C 一步跳到 140，现在 45/50/55/57/59°C 逐级 100→140）。
`luci-app-hinlinkfan` 启用后会接管风扇，那时用的是它自己的 6 档表。
---

## H29K：硬件 RNG（`&rng`）为什么去掉了
## ✅ H29K 硬件 RNG（RKRNG）：已移植

### 为什么之前没有

| | `rng@ffc50000` 节点 | 驱动 |
|---|---|---|
| 上游 6.18.55 | ✘ `rk3528.dtsi` 里没有 | ✘ `rockchip-rng.c` 只认 `rk3568-rng`/`rk3576-rng`/`rk3588-rng` |
| lede | ✔ 在它自己那份 `rk3528.dtsi` 里 | ✔ 换成了老版驱动，认 `rockchip,rkrng` |
| **openwrt（现在）** | ✔ **我们在 h29k dts 里自己定义** | ✔ **增量补丁给上游驱动加了 `rockchip,rkrng`** |

关键点：`RKRNG` 和上游支持的 `TRNG_*` 是**两套完全不同的寄存器**，不能靠改 compatible 蒙过去：

```
RKRNG_CTRL        0x0010      RKRNG_STATE       0x0014      RKRNG_DRNG_DATA_0   0x0070
（上游 TRNG 是     0x0004/0x0400/0x0404/0x0410）
```

### 怎么做的

**两处改动，都不碰上游的 dtsi：**

1. **dts**（`files/rk3528-opc-h29k.dts`）：用 `&{/soc}` 路径引用往里塞一个 rng 节点
   （上游的 `soc` 节点没有 label，所以只能这么引）

   ```dts
   &{/soc} {
   	rng: rng@ffc50000 {
   		compatible = "rockchip,rkrng";
   		reg = <0x0 0xffc50000 0x0 0x200>;
   		clocks = <&scmi_clk SCMI_HCLK_TRNG>;
   		clock-names = "hclk_trng";
   		interrupts = <GIC_SPI 23 IRQ_TYPE_LEVEL_HIGH>;
   		status = "okay";
   	};
   };
   ```

   - `SCMI_HCLK_TRNG` 这个宏**上游 `rk3528-cru.h` 里已经有了**（值 32），不用补头文件
   - **故意不写 `resets`**：上游的 reset 头（`dt-bindings/reset/rockchip,rk3528-cru.h`）
     里还没有 `SRST_H_TRNG_NS` 宏；而且 lede 那个驱动**根本没用 reset**
     （`grep reset` 是空的），所以不写没影响 —— 我们的 soc_data 里 `reset_optional = true`

2. **驱动**（`kernel-patches/104-*.patch`）：**纯增量**往上游
   `drivers/char/hw_random/rockchip-rng.c` 里加四段

   | 段 | 放在哪 | 内容 |
   |---|---|---|
   | MACROS | `#define TRNG_RNG_DOUT` 之后 | `RKRNG_*` 寄存器宏 + `RKRNG_HIWORD_UPDATE` |
   | FUNCS | `rk3568_soc_data` 之前 | `rkrng_rng_init` / `rkrng_rng_cleanup` / `rkrng_rng_read` |
   | SOCDATA | `rk_rng_dt_match[]` 之前 | `rkrng_soc_data`（`quality = 900`、`reset_optional = true`） |
   | MATCH | `{ /* sentinel */ }` 之前 | `{ .compatible = "rockchip,rkrng", ... }` |

   **为什么用增量而不是换掉整个驱动**：上游那份驱动同时管着 rk3568/rk3576/rk3588，
   而 lede 那份只认 `cryptov1/cryptov2/trngv1/rkrng` 四个 compatible。
   直接替换会让 rk3568（H66K/H68K/H69K！）的 RNG 全部失效 —— 所以必须是纯增量。
   实测加完之后 `rk3568_soc_data` 等三处引用完好。

   **不用改 Kconfig / Makefile / 内核 config**：`CONFIG_HW_RANDOM_ROCKCHIP` 本来就是 `=y`，
   同一个编译单元。

### ⚠️ 还有一步不能漏：把 HW_RANDOM 打开（否则上面白做）

`openwrt-patches/patches/003-rockchip-enable-hw-rng.patch` 就是干这个的。
**没有它，104 补丁里的驱动代码永远编不进内核**，原因：

1. `include/target.mk:162`：

   ```make
   __config_name_list = $(1)/config-$(KERNEL_PATCHVER) $(1)/config-default
   ```

   每个 target 可以有**自己的** `config-<ver>`；拼的时候 generic 在前、target 在后，
   所以 target 的写法覆盖 generic。

2. 上游 `drivers/char/hw_random/Kconfig`：

   ```make
   config HW_RANDOM_ROCKCHIP
   	tristate "Rockchip True Random Number Generator"
   	depends on HW_RANDOM && (ARCH_ROCKCHIP || COMPILE_TEST)
   	default HW_RANDOM
   ```

   **`depends on HW_RANDOM`** ⇒ `CONFIG_HW_RANDOM` 关着的时候，
   RK35xx 的 TRNG 驱动根本不可能被选上。

3. 而 `target/linux/generic/config-6.18:2728` 写的就是
   `# CONFIG_HW_RANDOM is not set`，并且 **rockchip 这个 target 原来没有自己的
   `config-6.18`**（只有 `Makefile` / `armv8/` / `image/` / `modules.mk` / `patches-6.18/`）。

   对比一下，其它 target 都在自己的 config 里写了：
   `sunxi` / `x86` / `starfive` / `ipq40xx` / `kirkwood` / `mveil` / `omap` … 都有
   `CONFIG_HW_RANDOM=y` —— **只有 rockchip 漏了**。

所以 003 补丁新建了一个两行的 `target/linux/rockchip/config-6.18`：

```
CONFIG_HW_RANDOM=y
CONFIG_HW_RANDOM_ROCKCHIP=y
```

顺带的好处：`rk3566.dtsi` 里本来就有 `rng: rng@fe388000`（`rockchip,rk3568-rng`），
开了 HW_RANDOM 之后 H66K/H68K/H69K 也一起有 `/dev/hwrng` 了（各板 dts 里把
`status` 打开就行）。

> 这个坑值得记一笔：**判断"某个驱动会不会被编进去"，不能只看补丁打没打上，
> 还要确认 `CONFIG_*` 在目标 target 的内核 config 里到底是不是 y。**
> `grep -rn 'CONFIG_XXX' target/linux/generic/config-6.18 target/linux/rockchip/config-6.18`
> 是最快的检查方式。

### 实测

```
✔ 104 补丁 patch -p1 --dry-run 通过，git apply --check 通过（3 个 hunk）
✔ 驱动编译通过  drivers/char/hw_random/rockchip-rng.o  73472 字节，零错误零警告
✔ rk3528-opc-h29k.dtb 编译通过 38823 字节，反编译可见：

	rng@ffc50000 {
		compatible = "rockchip,rkrng";
		reg = <0x00 0xffc50000 0x00 0x200>;
		clocks = <0x06 0x20>;      <- scmi_clk + SCMI_HCLK_TRNG(=32)
		clock-names = "hclk_trng";
		interrupts = <0x00 0x17 0x04>;   <- GIC_SPI 23
		status = "okay";
	};
```

### ⚠️ 还需要真机确认

编译和 DT 都对，但**寄存器行为只能上真机看**。烧进去之后：

```sh
dmesg | grep -i rng          # 应该看到 rockchip-rng ... 注册成功
cat /sys/class/misc/hw_random/rng_current
head -c 32 /dev/hwrng | hexdump -C   # 应该有非全 0/全 FF 的数据
```

如果 probe 失败或者数据明显不对（全 0），把 `files/rk3528-opc-h29k.dts` 里那个
rng 节点的 `status` 改成 `"disabled"` 再重编一次即可（不影响其它任何东西）。

寄存器读写序列是照 lede 那份（真机跑过的）写的，`rkrng_rng_read` 用
`memcpy_fromio` 直接搬 `DRNG_DATA_0`——lede 那边是逐字 `be32_to_cpu`，
但整块数据都是熵，字节序怎么排完全等价，还顺带避免了 <4 字节时越界和对齐假设。
---

## 实测：两个 dtb 都能编出来

在 `/tmp/owdts2`（= 上游 linux-6.18.55 tarball + openwrt 的 rk3528 系列补丁
`038-*`/`039-3`/`070-26`/`072-*`/`073-1`/`161-*` + 我们的 102/103）里：

```
✔ rk3528-opc-h29k.dtb   38823 字节（含新增的 rng 节点）
✔ rk3568-opc-h69k.dtb   66165 字节（含修好的 10 档温控）
```

编 h29k 时 `Backlight` 抽查（反编译 dtb）：

```
backlight {
	pinctrl-names = "backlight";
	compatible = "pwm-backlight";
	pwms = <0x15 0x00 0x61a8 0x01>;   <- 0x61a8 = 25000ns，最后一位 1 = PWM_POLARITY_INVERTED
	brightness-levels = <0x00 ... 0xff>;  <- 256 级
	default-brightness-level = <0xc8>;    <- 200
};
```

（`0x01` 那个极性标志**不能改** —— 本机背光是反相的，改成 0 会让亮度条整个反过来。）

---

## 补充（2026-10-13）：`patches/` 瘦身 + `kernel-patches-disabled/` 有了真机制

### 1. `patches/` 现在只剩设备定义

`uboot-rockchip/Makefile` 和 `kernel/linux/modules/video.mk` 这两个补丁**和 `openwrt-patches/` 里的重复**了
（同一改动两份拷贝，只是 patch 头不同），已移出并归档到 `duplicates-archived/`。
**归口原则**：

| 改动对象 | 归口 |
|---|---|
| `target/linux/rockchip/image/armv8.mk`（设备定义，和板子强相关） | **这里** `dts/patches/` |
| `package/boot/uboot-rockchip/Makefile`、`package/kernel/linux/modules/video.mk`、rockchip 内核 config | `../openwrt-patches/patches/` |

### 2. `kernel-patches-disabled/` 从"只是备案"升级为"真机制"

`target/linux/rockchip/patches-6.18/` 里那些**上游自带、但这棵树必须屏蔽掉**的补丁
（RK3528 的 PCIe/OTP/USB/TSADC，和我们的设备树冲突），现在由脚本自动处理：

- `apply.sh` 第 3 步：**删掉**同名文件（`kernel-patches-disabled/` 里的名字）
  - 先 `git ls-files --error-unmatch` 确认它确实是**上游**文件才删 —— 防止误删我们自己的补丁
  - 非上游的（如 `11x-arm64-rockchip-*-hdmi-*`，是我们自己的 HDMI 补丁）只备案，不动树
- `revert.sh` 第 1 步：**`git checkout` 恢复**被屏蔽的上游补丁（顺序必须在删我们自己的补丁之前）

### 3. 往返自检（可随时复核）

```bash
# 上游树是否干净 = 只剩我们希望的那几处改动
~/openwrt/package/daocaoren/openwrt-patches/revert.sh
~/openwrt/package/daocaoren/dts/revert.sh
git -C ~/openwrt status --porcelain | grep -v '^?? package/daocaoren/'   # 应为空

# 复原
~/openwrt/package/daocaoren/openwrt-patches/apply.sh
~/openwrt/package/daocaoren/dts/apply.sh
```

2026-10-13 实测：**revert 后 0 条残留改动，apply 后 13 条改动与之前逐字节一致，两边都 0 失败**。

树内那 13 处改动，逐条都能在 `package/daocaoren/` 里找到出处：

| 树内改动 | 出处 |
|---|---|
| `M package/boot/uboot-rockchip/Makefile` | `openwrt-patches/patches/002` |
| `M package/kernel/linux/modules/video.mk` | `openwrt-patches/patches/001` |
| `M target/linux/rockchip/image/armv8.mk` | `dts/patches/001` |
| `?? target/linux/rockchip/config-6.18` | `openwrt-patches/patches/003` |
| `A 102/103/104/107/800-*.patch` | `dts/kernel-patches/`（拷入） |
| `D 070-26 / 072-3 / 073-1 / 161-03` | `dts/kernel-patches-disabled/`（屏蔽） |
