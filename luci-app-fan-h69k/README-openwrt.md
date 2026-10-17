# luci-app-hinlinkfan —— OpenWrt 移植说明

来源：`~/LS/LEDE-Develop/package/daocaoren/APP/luci-app-hinlinkfan`
（同一份也在 lede 里能用）

## 我改了什么（以及为什么）

### 1. `files/etc/init.d/hinlinkfaninit` —— 不再写死 hwmon 编号【重要】

原版：

```sh
PWM_PATH="/sys/class/hwmon/hwmon2/pwm1"
```

`hwmon2` 这个编号**不是固定的**，它由内核里 hwmon 设备的注册顺序决定：
探针顺序、模块加载顺序、板上还有几个 hwmon（rk809/tsadc/gpu…）都会改变它。
换一版内核或换一棵树，编号就可能变成 hwmon0/hwmon1/hwmon3，
那时脚本会往一个**不存在或不属于风扇**的节点写值 —— 轻则风扇不转，重则写到别的 hwmon 上。

现在改成运行时查找：

- 先按 `name == "pwmfan"` 遍历 `/sys/class/hwmon/hwmon*/`（`pwm-fan` 驱动注册的 hwmon 名字就是 `pwmfan`）
- 找不到就退回"第一个可写的 `pwm1`"
- 找不到任何节点会 `logger` 记一条，不会静默失败

温度同样是查找：优先 `cpu-thermal` / `soc-thermal`，最后才退回 `thermal_zone0`。

其余行为**逐条对应原版**：`enable` / `openfeng` 的语义、`pwm1_enable` 的 0/1 取值、
6 档温度-转速阈值、1 秒轮询、`/var/run/hinlinkfan.pid` 的启停逻辑都没动。

### 2. `files/usr/lib/lua/luci/model/cbi/hinlinkfan.lua` —— 页面不再白屏

原版：

```lua
local cur_temp = tonumber(io.open("/sys/class/thermal/thermal_zone0/temp"):read()) / 1000
local cur_pwm = tonumber(io.open("/sys/class/hwmon/hwmon2/pwm1"):read())
```

两个问题：
1. 路径写死（同上）；
2. `io.open` 失败返回 `nil`，紧接着的 `:read()` 直接抛 Lua 异常 ——
   只要编号不对，**整个页面 500**，连"禁用手动控制"都救不回来。

现在用同一套查找逻辑，加上读不到就显示 0 的兜底。

### 3. `files/etc/uci-defaults/luci-app-hinlinkfan`

- 去掉了 `chmod +x /usr/bin/fancontrol`：包里**没有**这个文件，纯属历史残留
- `uci -q batch <<-EOF` 的正文从空格缩进改成 tab（`<<-` 只剥 tab，不剥空格）
- `ucitrack` 里 `init=` 写的是 `hinlinkfan`，实际服务脚本叫 `hinlinkfaninit` —— 已修正
- 补写 `openfeng=0` 默认值

### 4. `Makefile`

- 依赖从 `+luci +kmod-hwmon-pwmfan +kmod-hwmon-gpiofan` 改成
  `+luci-base +luci-compat +luci-lua-runtime +kmod-hwmon-pwmfan +kmod-hwmon-gpiofan`
  - `+luci` 是会把整套 LuCI（含 uhttpd、一堆 app）拉进来的 meta 包，太重
  - 这是 **Lua** LuCI 应用（`/usr/lib/lua/luci/...`），openwrt 现在的 LuCI 必须有
    `luci-compat` + `luci-lua-runtime` 才能跑，原版没写
- 加了 `postinst`：装完自动 `enable` 服务

## 和内核温控的关系

H69K 的设备树里 `fan: pwm-fan` 已经被 CPU 热区当成 cooling device 用
（`&cpu_thermal` 的 `cooling-maps`）。本应用启用后会把 `pwm1_enable` 切到手动并接管 PWM，
禁用则交回内核。这两个控制器**不要同时用**，界面上也写明了。

> 顺便：H69K 厂商 dts 里 `&cpu_thermal` 的 trip 节点名重复，导致 10 档实际只剩 6 档
> （见 `dts/README.md`）。这会影响"交回内核"时的表现。
