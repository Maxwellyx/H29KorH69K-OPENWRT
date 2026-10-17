# 老 LEDE-Develop 树里的 4 个 daocaoren 内核补丁（存档参考）

它们原本是往内核树里打的补丁。移植到当前 lede 后，**内核源码零修改**，
效果分别落在了 daocaoren 的包里：

| 老补丁 | 现在的落点 |
| --- | --- |
| `9996-daocaoren-fb_gc9307.patch` | `kmod-fb-tft-gc9307`（外置模块，同一份驱动源码） |
| `9997-daocaoren-linux-delfbcon-cursor.patch` | `tft-display/files/etc/init.d/tftdisplay` 里的 `disable_fbcon_cursor()`（用 `cursor_blink=0` + `ESC[?25l` 代替改 `fbcon.c`） |
| `9998-daocaoren-rockchip-rk3528-iio-add-adc.patch` | 无需处理：6.18 内核的 `rockchip_saradc.c` 已内置 `rockchip,rk3528-saradc`，上游 `rk3528-opc-h29k.dts` 也已打开 `&saradc`（`vref-supply = <&vcc_1v8>`）；.ko 由 `kmod-saradc-rockchip` 负责 |
| `9999-daocaoren-fbtft-adjust-display-offset-for-rotation.patch` | `kmod-fb-tft-gc9307/src/gc9307.c` 里的 `gc9307_set_addr_win()`（覆盖 fbtft 的 `set_addr_win` 钩子，做同样的 ±34 偏移） |

⚠ 这些 `.patch` 只作存档，**不要**再打进 ledger 的 `target/linux/.../patches-*`。
