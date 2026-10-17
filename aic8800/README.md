# aic8800 —— H29K 的 SDIO WiFi 驱动（适配 OpenWrt 原版 mac80211 7.2）

## 这个包为什么在这里

原来在 `package/kernel/aic8800/`。2026-10-13 按约定**所有自定义都归到 `package/daocaoren/`**，
所以整包（Makefile + patches）搬到了本目录。

- **包名/子包名没变**：`aic8800`、`kmod-aic8800-sdio`、`aic8800-sdio-firmware`、`aic8800-btusb`…
  ⇒ 设备定义里的 `DEVICE_PACKAGES` 一行都不用改。
- 目录深度和原来一样（`package/<组>/<包>/`），OpenWrt 扫描不受影响。
- 树里没有任何地方按路径引用过它（已 grep 全树确认）。

## 驱动来源

上游 `radxa-pkg/aic8800` @ `89f865b8`（`PKG_SOURCE_DATE:=2026-03-30`）。
**和 lede 那棵树用的是同一个提交** —— 也就是说两边的驱动源码本来就是一回事。

## 核心：OpenWrt 的 WiFi 栈是 backports **7.2**，它把一批 cfg80211 回调的
## "接口参数"从 `struct net_device *` 改成了 `struct wireless_dev *`

实测（`staging_dir/.../usr/include/mac80211/net/cfg80211.h`，backports 7.2）：

| 回调 | 7.2 的接口参数 | 说明 |
|---|---|---|
| `add_key` / `get_key` / `del_key` / `set_default_mgmt_key` | `struct wireless_dev *wdev` | 都变了 |
| `add_station` / `del_station` / `change_station` / `get_station` / `dump_station` | `struct wireless_dev *wdev` | 都变了 |
| `remain_on_channel` | `wdev` **且多一个 `const u8 *rx_addr`** | 参数个数也变了 |
| `change_beacon` / `start_ap` / `stop_ap` | **仍是** `struct net_device *dev` | **没变** |

驱动原来一律按 `net_device *` 解释第一个参数，于是 `netdev_priv(被当成 netdev 的 wdev)`
从错误偏移处取指针 ⇒ 读到垃圾地址 ⇒ DABT ⇒ panic ⇒ 看门狗复位。

**这正好解释了现场现象**：每 6 秒一次的信标更新（`change_beacon`，参数没变）一直正常，
而**一有客户端关联/一旦 hostapd 去查站点，整机立刻复位**。

### 串口实证（COM34 @1500000，2026-10-13）

```
[  120.562935] Unable to handle kernel read from unreadable memory at virtual address 0000000000002ca4
[  120.568908] Internal error: Oops: 0000000096000004 [#1]  SMP
[  120.580718] pc : rwnx_cfg80211_get_station+0xbc/0x1a0 [aic8800_fdrv]
[  120.581309] lr : nl80211_get_station+0xac/0xa9c [cfg80211]
[  120.587804] x2 : 0000000000000000 x1 : 0000000000002ca4 x0 : 0000000000002ca4
[  120.595796] Kernel panic - not syncing: Oops: Fatal exception in interrupt
[  120.597836] Rebooting in 3 seconds..
```

触发命令：`iw dev phy0-ap0 station get 02:11:22:33:44:55`
（`nl80211_get_station()` → `rdev->ops->get_station(&rdev->wiphy, **wdev**, …)`）

> 之前用 `/proc/kmsg` 抓不到这段，是因为 panic 走的是控制台路径、且复位极快；
> **只有串口能看到**。排查这类"静默复位"请直接用串口。

## 补丁清单

| 补丁 | 作用 |
|---|---|
| `010-fix-fall-through.patch` | 修 switch 贯穿告警 |
| `020-wireless-6.16.patch` | 适配 6.16+ 内核无线接口 |
| `030-update-firmware-path.patch` | 固件路径改到 `/lib/firmware/aic8800/...` |
| `040-rename-module.patch` | 模块名规范 |
| `050-log-level.patch` | 日志级别可调（`aicwf_dbg_level`） |
| `060-fix-read-cpuid.patch` | 读 CPU ID 的越界 |
| `070-fix-tdls-action-struct.patch` | TDLS action 结构体名（**7.2 头文件需要，不能去掉**） |
| **`080-fix-cfg80211-wdev-ops.patch`** | **本次核心修复**：把上面表格里 9 个回调 + 2 个通知接口（`cfg80211_new_sta` / `cfg80211_del_sta`）按 7.2 的 `wireless_dev` 语义改造 |

`080` 的做法（不破坏任何编译条件，只在驱动源码里加一层兼容宏）：

```c
#ifdef RWNX_CFG80211_OPS_USE_WDEV          /* 由包 Makefile 自动探测后传入 */
#define RWNX_CFG80211_IFACE_DECL     struct wireless_dev *wdev
#define RWNX_CFG80211_IFACE_ARG(dev) ((struct wireless_dev *)((dev) ? (dev)->ieee80211_ptr : NULL))
#define RWNX_IFACE_DEV(wdev)         ((wdev) ? (wdev)->netdev : NULL)
#define RWNX_CFG80211_NOTIFY_IFACE(vif)  (&(vif)->wdev)
#else
#define RWNX_CFG80211_IFACE_DECL     struct net_device *wdev
#define RWNX_CFG80211_IFACE_ARG(dev) (dev)
#define RWNX_IFACE_DEV(wdev)         (wdev)
#define RWNX_CFG80211_NOTIFY_IFACE(vif)  ((vif)->ndev)
#endif
```

- 每个回调统一写成 `RWNX_CFG80211_IFACE_DECL`（形参名 `wdev`），函数体第一件事
  `dev = RWNX_IFACE_DEV(wdev)`，并判空返回（NAN 之类没有 netdev 的接口会传 NULL）。
- 驱动内部自己调用这些回调时（手上是 net_device），用 `RWNX_CFG80211_IFACE_ARG(dev)` 转回去。
- `remain_on_channel` 的 `rx_addr` 用 `#ifdef` 包住，只在 7.2 下出现。

**开关是自动探测的**（`Makefile` 读真正被编译的那份 `cfg80211.h`）：

```make
AIC_MAC80211_CFG80211_H := $(STAGING_DIR)/usr/include/mac80211/net/cfg80211.h
ifneq ($(shell grep -c 'del_station)(struct wiphy \*wiphy, struct wireless_dev' $(AIC_MAC80211_CFG80211_H) 2>/dev/null),0)
  AIC_CFG80211_COMPAT := -DRWNX_CFG80211_OPS_USE_WDEV=1
endif
```

⇒ 换回 6.18.39 或任何旧 backports，开关自动不发，源码仍是老的 `net_device` 语义，**两边都能编**。

## 验证（2026-10-13，真机 H29K）

| 项目 | 修复前 | 修复后 |
|---|---|---|
| `iw dev phy0-ap0 station get <mac>` | **oops → panic → 复位** | 正常返回 `-ENOENT` |
| `iw dev phy0-ap0 station del <mac>` | 崩 | 串口打印 `rwnx_cfg80211_del_station_compat: 02:11:22:33:44:55` 后正常返回 |
| 客户端关联（手机） | 整机复位 | 稳定连上，`rx bitrate 600.4 MBit/s 80MHz HE-MCS 11 HE-NSS 1` |
| `dmesg` oops 计数 | ≥1 | 0 |
| 编译告警 `incompatible pointer type` | 11 处 | **0 处** |

## 注意

- **不要替换 OpenWrt 的 mac80211/backports 版本**（曾试过降到 6.18.39，已按要求回滚）。
  正解是**驱动侧适配**，即本目录的 `080` 补丁。
- 该芯片是 **1×1 单空间流**：2.4G 最高 HE40 MCS11≈286Mbps，5G 最高 **HE80 MCS11=600.4Mbps**，
  不支持 160MHz；`600` 就是硬件天花板。
