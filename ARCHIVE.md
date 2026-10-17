# 改动归档清单 —— `package/daocaoren/` 是这棵树**全部改动**的唯一出处

规则：**`package/daocaoren/` 只放"源码 + 补丁 + 归档数据"**，脚本/日志/备份一律放 `DevTools/`。
只要保留这个目录（+ `DevTools/` 里的脚本），就能在一棵**干净的上游 openwrt 树**上完整重建
H29K（RK3528）/ H69K（RK3568）两台设备。

构建总入口：**`sh DevTools/build.sh [h29k|h69k|both]`**（默认 both）→ 见 `DevTools/README.md`。

---

## 一、目录职责

| 目录 | 放什么 | 落地方式 |
|---|---|---|
| `dts/files/*.dts` | **设备树真源**（H29K / H69K 板级 dts） | 由 `gen-patches.sh` 生成内核补丁 |
| `dts/kernel-patches/*.patch` | 给**内核**用的补丁（新增 dts + 改内核 dts Makefile 等） | 拷进 `target/linux/rockchip/patches-6.18/`，由构建系统自己打 |
| `dts/kernel-patches-disabled/*.patch` | 上游自带、但这棵树**必须屏蔽**的 rockchip 内核补丁 | 从 `patches-6.18/` 里删同名文件（原文件在 git 里，`revert.sh` 能拿回） |
| `dts/patches/*.patch` | 改**上游 OpenWrt 自身文件**里与设备树/设备定义相关的部分 | `git apply`，可反向还原 |
| `openwrt-patches/patches/*.patch` | 改**上游 OpenWrt 自身文件**、与设备树无关的改动（品牌、uboot、fbdev、内核 config） | `git apply`，可反向还原 |
| `lede-only/` | **lede 树**（coolsnowwolf/lede）专用资料，**不参与** openwrt 构建 | 手工/lede 侧脚本 |
| `vendor-extra/` | 厂商素材存档（反编译 dts、旧内核 6.6 补丁等），只作对照 | 不参与构建 |
| `duplicates-archived/` | 与 `openwrt-patches/` 重复过的补丁（防再次误用） | 不参与构建 |
| 各功能包目录 | aic8800 / aic8800-loader / hinlink-settings / kmod-fb-tft-gc9307 / kmod-input-touchscreen-axs5106 / kmod-saradc-rockchip / luci-app-fan-h69k / luci-app-oled / luci-theme-daocaoren / luci-proto-modem-pro / modem-pro / modemband / tft-display | 就是普通 openwrt 包，直接构建 |
| `dts/devices.mk` | **历史存档**（老的设备定义写法，已整块注释） | 不参与构建 |

---

## 二、改动总表

### 表 1：改上游 OpenWrt 自身文件（`git apply`，可一键还原）

| # | 上游文件 | 承载补丁 | 改了什么 |
|---|---|---|---|
| 1 | `package/kernel/linux/modules/video.mk` | `openwrt-patches/patches/001-kmod-fbdev-allow-rockchip.patch` | `FBDEV_TARGETS` 加 `TARGET_rockchip`、放开 `fb-tft` 的 `GPIO_SUPPORT` 限制（H29K 屏） |
| 2 | `package/boot/uboot-rockchip/Makefile` | `openwrt-patches/patches/002-uboot-add-generic-rk35xx.patch` | 新增 `generic-rk3528` / `generic-rk3568` 两种 u-boot 变体 |
| 3 | `target/linux/rockchip/config-6.18`（**新建文件**） | `openwrt-patches/patches/003-rockchip-kernel-config.patch` | rockchip 目标的内核配置片段（`CONFIG_HW_RANDOM` 等，配合 185 号 RNG 补丁） |
| 4 | `include/version.mk` | `openwrt-patches/patches/004-branding-docaoren.patch` | 品牌：`VERSION_DIST=DaoCaoRen`、`VERSION_NUMBER=MSYF` |
| 5 | `target/linux/rockchip/image/armv8.mk` | `dts/patches/001-armv8-hinlink-devices.patch` | 两个设备定义：`hinlink_opc-h29k`、`hinlink_opc-h69k` |
| 6 | `target/linux/rockchip/armv8/base-files/etc/board.d/02_network` | `dts/patches/002-rockchip-armv8-02network-hinlink-devices.patch` | ① `hinlink,opc-h29k)`：`country CN` + 无线 `H29K` ② `hinlink,opc-h69k)`：LAN/WAN（`'eth1 eth2'`/`'eth0'`）+ 国家码 + 无线 + MAC 组 |

> 变完这两类补丁，`git status` 里应有 **12 条左右**改动，**全部**来自上表 + 表 2，没有"无主"的改动。

### 表 2：内核补丁（`dts/kernel-patches/*` → 拷进 `target/linux/rockchip/patches-6.18/`）

| 编号 | 内容 | 说明 |
|---|---|---|
| `102-arm64-dts-rockchip-add-HINLINK-OPC-H29K.patch` | 内核新增 H29K dts 并登记进 dts Makefile | 由 `dts/files/rk3528-opc-h29k.dts` **自动生成** |
| `103-arm64-dts-rockchip-add-HINLINK-OPC-H69K.patch` | 内核新增 H69K dts 并登记进 dts Makefile | 由 `dts/files/rk3568-opc-h69k.dts` **自动生成** |
| `107-mmc-core-set-initial-signal-voltage-on-power-off.patch` | mmc 断电时设置初始信号电压 | 修 H29K eMMC/SD 相关问题 |
| `185-arm64-dts-rockchip-add-rkrng-support.patch` | `rockchip-rng.c` 支持 RK3528 的 RKRNG | 需 `003` 补丁开的 `CONFIG_HW_RANDOM` |
| `800-rk3528-dtsi-use-vendor-version.patch` | RK3528 dtsi 用厂商版本 | H29K 外设齐全度 |

### 表 3：设备树真源

| 文件 | 设备 | 说明 |
|---|---|---|
| `dts/files/rk3528-opc-h29k.dts` | H29K | RK3528；屏 GC9307 + 触摸 AXS5106 |
| `dts/files/rk3568-opc-h69k.dts` | H69K | RK3568；**include 内核自带的 `rk3568-hinlink-opc.dtsi`**，本文件只写板级差异 |

---

## 三、机制与命令

### 3.1 dts 工作流（改完 dts 必须走完这三步）

```bash
# 1) 从 dts/files/*.dts 重新生成 kernel-patches/10x-*.patch（hunk 行号自动算，别手写）
bash DevTools/tools/dts/gen-patches.sh --check
# 2) 把生成的补丁装进内核补丁目录 target/linux/rockchip/patches-6.18/
bash DevTools/tools/dts/apply.sh
# 3) 改上游文件的补丁（patches/*.patch）也一起确保就位
bash DevTools/tools/dts/status.sh
```

`DevTools/build.sh` 已经把这套（gen → apply → status）**自动**跑一遍，正常不用手工执行。

> ⚠️ **生成器只更新 `package/daocaoren/dts/` 里的补丁文件**，不会自动装进内核目录，
> 所以"生成完"必须再 `apply.sh` 一次（`build.sh` 会做）。

### 3.2 还原（让树回到上游原样）

```bash
bash DevTools/tools/dts/revert.sh                    # 设备树相关：恢复被屏蔽的上游补丁 -> 删我们拷进去的 -> 反向 apply patches/
bash package/daocaoren/openwrt-patches/revert.sh     # 上游文件补丁反向 apply
```

脚本位置说明：`openwrt-patches/{apply,revert,status}.sh` 仍在**源码树内**
（`package/daocaoren/openwrt-patches/`）；**dts 的脚本**已挪进开发区
（`DevTools/tools/dts/`），因为它们只是工具、不属于构建产物。

### 3.3 `kernel-patches-disabled/` 机制

`apply.sh` 会把 `kernel-patches-disabled/` 里**与上游同名**的补丁从
`target/linux/rockchip/patches-6.18/` 删掉（先用 `git ls-files --error-unmatch` 确认是上游文件，
避免误删我们自己的）；`revert.sh` 用 `git checkout` 恢复。

---

## 四、⚠️ 已知注意点（都是实测踩出来的）

0. **增减 mac80211 系 kmod 后必须清 backports 构建目录。**
   `make target/linux/clean` / `make package/kernel/linux/clean` / `package/base-files/clean`
   **都不清** `build_dir/target-*/linux-rockchip_armv8/mac80211-{regular,tiny}/backports-*/`（403M）。
   陈旧的 backports 目录会沿用旧 `.config` ⇒ 打包时报 `ERROR: module '...' is missing.`
   ⇒ 保底清理（已固化进 `DevTools/tools/build-dev.sh` 的 `===CLEAN===` 段）：
   ```sh
   make package/kernel/mac80211/clean
   rm -rf build_dir/target-*/linux-rockchip_armv8/mac80211-*
   ```

1. **内核 config 有两层，子目标优先。**
   我们建的 `target/linux/rockchip/config-6.18` 是**target 级**；上游在一次重构里又加了
   **子目标级** `target/linux/rockchip/armv8/config-6.18`（还有 `armv7/`），而合并顺序是
   `generic → target → subtarget` ⇒ **子目标里同名的键会覆盖我们的**。
   当前实测重名的只有 3 个键（`CONFIG_BACKLIGHT_CLASS_DEVICE`、`CONFIG_HW_RANDOM`、
   `CONFIG_HW_RANDOM_ROCKCHIP`），两边取值一致 ⇒ **目前无影响**。
   ⚠️ 以后要往 target 级加配置时，**先查子目标里有没有同名键**：
   ```bash
   git show HEAD:target/linux/rockchip/armv8/config-6.18 | grep <KEY>
   ```
2. **补丁编号与上游会撞号**（上游也有 102/103/104/107/185 之类）。判断有没有真冲突要看
   **改的文件是否相同**，不是看编号。我们的补丁名中间带 `-arm64-dts-rockchip-` 等特征串。
3. **上游同步后 `git status` 里的 `D` 可能是"陈旧 index"造成的假删除**：
   （`git ls-tree HEAD <path>` 里已经没有该文件 ⇒ 工作区与 HEAD 其实一致，不是我们的改动，
   也**不需要**归档）。判断方法：
   ```bash
   git ls-tree HEAD target/linux/rockchip/patches-6.18/ --name-only | grep <编号>
   ```
4. **`board.d/*` 只在首次启动、`/etc/board.json` 不存在时写配置** ⇒ 刷机保留配置时，
   新的板级分支不会执行。改完要 `sysupgrade -n` 或 `rm -f /etc/board.json /etc/config/network && reboot`。
5. **不要同时对同一份文件用两套 apply 机制**（曾把 `rockchip/config-6.18` 同时交给
   `dts/apply.sh` 拷贝和 `openwrt-patches/003` 的 git 补丁，导致正反向 `git apply --check` 全失败）。
6. 往 `dts/` 或 `openwrt-patches/` 里**加补丁后，记得同时更新本文件**（这是归档的一部分）。

---

## 五、构建与验收

```bash
sh DevTools/build.sh                 # 两台都编（先 H29K 再 H69K）
sh DevTools/build.sh h69k            # 只编一台
sh DevTools/build.sh --status        # 只看当前状态
sh DevTools/tools/verify-image.sh h69k   # 单跑验收
```

产物：`bin/targets/rockchip/armv8/daocaoren-rockchip-armv8-hinlink_opc-{h29k,h69k}-squashfs-sysupgrade.img.gz`
构建日志：`DevTools/backup/build-<dev>-<MMDD_HHMM>.log`（看最后一行 `BUILD EXIT=0`）


## 三点六、可选固件补齐 + 互斥包排除（2026-10-15 深夜，H69K）

### 为什么要补：只按 Depends 收固件会漏
第一版清单是“从 `tmp/.packageinfo` 里取 `Source-Makefile` 属于
`package/kernel/mac80211/` 或 `package/kernel/mt76/` 的 kmod，再把它们
`Depends` 里的 `+xxx-firmware` 并进来”。**问题：很多固件根本没写进
驱动的 Depends** —— 最典型的就是 `ath10k-firmware-qca*`。
于是镜像里只有 `ath10k/QCA988X/hw2.0/board.bin`（板卡数据，我们自己列过），
**没有 `firmware-*.bin`** ⇒ 卡插上去根本认不到。

### 补了什么（共 57 个，清单现 233 个包）
- **`ath10k-firmware-qca{4019,6174,9377,9887,9888,988x,9984,99x0}`**（非 ct，配 `kmod-ath10k`）
- `ath11k-firmware-ipq5018` / `-qca2066`、`brcmfmac-nvram-43430/4356-sdio`、`cypress-firmware-*`（13 个）
- `mwlwifi-firmware-88w8864/8897/8964/8997`、`iwlwifi-firmware-iwl*` + `iwl3945/4965`（旧款 Intel）
- `rtl8188eu/8188fu/8192eu/8723au/8723bu-firmware`、`kmod-cypress-firmware`

### ⚠️ 三类“看着缺、其实不能加”的包
1. **平台限制**：`kmod-mwlwifi` 要 `@TARGET_mvebu`、`kmod-rtl8812au-ct` 要 `@!LINUX_6_18`
   ⇒ 在 RK3568 + 6.18 上根本选不了（对应固件会被 defconfig 自动剔掉）。
2. **互斥变体**：`kmod-ath10k-ct`、`ath11k-firmware-ipq5018-qcn6122`、
   `cypress-firmware-4339-sdio`（与 `brcmfmac-firmware-4339-sdio` 抢同名文件）。
3. **kmod 自带固件**：`kmod-brcmsmac` 本身就安装 `bcm43xx-0.fw` / `bcm43xx_hdr-0.fw`
   ⇒ 再加 `brcmsmac-firmware` 就会“试图覆盖别人的文件”而安装失败。

> **排查手法（可复用，比一个个试快得多）**：直接扫各包的 `ipkg-*/下 lib/firmware`
> 目录，统计“同一文件被哪几个包提供”即可 —— 脚本：
> `DevTools/tools/one-off/detect-conflicts3.py`（输出冲突组 + 建议剔除项）。
> ⚠️ 它必须限深度（`glob` 不能递归扫 `build_dir`，23G 会超时）。

### ⚠️ aic8800 的 USB 变体在本树编过不了
一开始想“M.2 万一换成 aic8800 卡”把 `kmod-aic8800-usb` 也加上，
结果 `package/daocaoren/aic8800` 的 USB 变体编译报错（`rwnx_msg_rx.o` / `rwnx_tdls.o`）
⇒ `ERROR: package/daocaoren/aic8800 failed to build.` ⇒ `BUILD EXIT=2`。
**只保留板载的 SDIO 变体**（`kmod-aic8800-sdio` + `aic8800-sdio-firmware`，一直好用），
USB 方案等以后有人真正需要时再单独修那个驱动。

---

## 三点七、H29K 加 USB 无线网卡（dongle）支持（2026-10-15 晚）

H29K 板载是 **aic8800 SDIO**，本轮另加 **USB 接口**无线网卡支持。

- 清单：`dts/h29k-wifi-usb-packages.txt` —— **46 个包**（mac80211/mt76 系 USB kmod + 固件）
- 落地：`armv8.mk` 的 `Device/hinlink_opc-h29k`（标记 `H29K-USB-WIFI-BEGIN/END`，注释独立成行）
  + 基线 `DevTools/backup/base-h29k.config`
- 覆盖的 USB 无线芯片：`mt7601u`、`mt76x0u/mt76x2u`、`mt7663u`、`mt7921u/mt7925u`、
  `rtw88-usb`（8822bu/8822cu/8821cu/8812au/8814au/8723du 等）、`rtl8192cu`、`rtl8xxxu`、
  `rt2800-usb`、`rt73-usb`、`ath9k-htc`（AR9271）、`ath6kl-usb`、`carl9170`、`ar5523`、
  `rsi91x-usb`、`mwifiex-usb`、`brcmfmac`
- 剔除的 6 个：`kmod-aic8800-usb` + `aic8800-usb-firmware`（本树 USB 变体编译不过，见三点六）、
  `rtl8761a/b/bu-firmware` + `ar3k-firmware`（**蓝牙**固件，与 WiFi 无关）
- 同名文件冲突预扫：**无**
- 验收脚本：`DevTools/tools/one-off/verify-h29k-usb.sh`
- 以后要再加网卡：包名加进那个清单 → 改基线 + `armv8.mk` 块 → `gen-patches.sh` → 编译
