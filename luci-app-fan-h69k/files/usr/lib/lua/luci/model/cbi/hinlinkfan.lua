-- Copyright 2024 HinLink / daocaoren
-- H69K 风扇控制页面（经典 Lua CBI，兼容 luci-compat）
--
-- 说明：
-- 1. 页面里所有可见文案直接用中文，不走 translate()，避免中文固件里冒出英文；
-- 2. “设备树 / 内核状态”一节的数据全部读自 sysfs，也就是**设备树生效后的真实值**
--    （trip 阶梯来自 cpu-thermal 的 trip_point_*，风扇档位表来自 pwm-fan 这个
--    cooling device 的 max_state），这样页面显示的就是内核眼里的配置；
-- 3. 保存后调用 /etc/init.d/hinlinkfaninit restart；守护进程本身每 tick 重读 UCI，
--    所以即使不重启也会在数秒内生效。

-- 注意：本机 LuCI 是 ucode 版，**没有 luci.fs 模块**（require 会直接报
-- "module 'luci.fs' not found"），所以这里一律不 require luci 子模块，
-- 只用 Lua 标准库（io / os）读 sysfs，避免再踩运行库缺失的坑。

local function trim(s)
	return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- 整文件读回（不做 trim）
local function read_raw(path)
	local f = io.open(path, "r")
	if not f then return nil end
	local v = f:read("*a")
	f:close()
	return v
end

-- 单值读回（去空白，空则视为没读到）
local function read(path)
	local v = read_raw(path)
	if not v then return nil end
	v = trim(v)
	if v == "" then return nil end
	return v
end

local function read_num(path)
	local v = read(path)
	return v and tonumber(v) or nil
end

-- 代替 luci.fs.glob：交给 shell 展开
local function glob(pattern)
	local out = {}
	local f = io.popen("ls -d " .. pattern .. " 2>/dev/null")
	if not f then return out end
	local line
	for line in f:lines() do
		line = trim(line)
		if line ~= "" then out[#out + 1] = line end
	end
	f:close()
	return out
end

-- 执行命令，返回是否成功（兼容 Lua 5.1 的返回 0 与 5.2+ 的返回 true）
local function run(cmd)
	local ok = os.execute(cmd)
	return ok == true or ok == 0
end

-- ============ sysfs 读取工具 ============

-- pwm-fan 的 hwmon 目录（pwm1 / pwm1_enable / name）
local function find_hwmon()
	local _, p, n
	for _, p in ipairs(glob("/sys/class/hwmon/hwmon*")) do
		n = read(p .. "/name")
		if n == "pwmfan" or n == "pwm-fan" or n == "pwm_fan" then
			return p
		end
	end
	for _, p in ipairs(glob("/sys/devices/platform/*pwm-fan*/hwmon/hwmon*")) do
		return p
	end
	return nil
end

-- CPU 温度 zone（设备树里的风扇 trip 都挂在 cpu-thermal）
local function find_temp_zone()
	local wants = { "cpu-thermal", "soc-thermal", "cpu_thermal" }
	local i, _, p
	for i = 1, #wants do
		for _, p in ipairs(glob("/sys/class/thermal/thermal_zone*")) do
			if read(p .. "/type") == wants[i] then
				return p
			end
		end
	end
	return nil
end

-- 内核视角的风扇 cooling device（设备树 cooling-levels 注册出来的）
local function find_cdev()
	local _, p, t
	for _, p in ipairs(glob("/sys/class/thermal/cooling_device*")) do
		t = read(p .. "/type")
		if t == "pwm-fan" or t == "pwmfan" then
			return p
		end
	end
	return nil
end

-- 读设备的温度阶梯（就是设备树里的 trips）
local function read_trips(zone)
	local out = {}
	local i = 0
	while true do
		local t = read_num(zone .. "/trip_point_" .. i .. "_temp")
		if not t then break end
		out[#out + 1] = {
			temp = t / 1000,
			type = read(zone .. "/trip_point_" .. i .. "_type") or "",
			hyst = (read_num(zone .. "/trip_point_" .. i .. "_hyst") or 0) / 1000,
		}
		i = i + 1
	end
	return out
end

-- 守护进程写的状态文件
local function read_state()
	local s = {}
	local f = read_raw("/var/run/hinlinkfan.state")
	if not f then return s end
	local line, k, v
	for line in f:gmatch("[^\r\n]+") do
		k, v = line:match("^([%w_]+)=(.*)$")
		if k then s[k] = v end
	end
	return s
end

-- 风扇转叶动画（随转速变化）
-- 用 SVG 内联 SMIL（<animateTransform> 的 dur 直接写在属性上），不用 CSS @keyframes：
-- 主题构建会用 csstidy 压缩 CSS，@supports/@keyframes 这类它不认识的块会被整块丢掉。
-- 返回: svg 标记, 档位文字
local function fan_svg(pwm)
	local p = tonumber(pwm or "") or 0

	if p < 0 then p = 0 end
	if p > 255 then p = 255 end

	local dur, cls, label
	if p == 0 then
		dur = nil
		cls = "hinfan-fan-stop"
		label = "停转"
	elseif p <= 80 then
		dur = string.format("%.2f", 2.20 - (p / 80) * 0.70) -- 2.20 ~ 1.50s
		cls = "hinfan-fan-slow"
		label = "低速"
	elseif p <= 160 then
		dur = string.format("%.2f", 1.50 - ((p - 80) / 80) * 0.55) -- 1.50 ~ 0.95s
		cls = "hinfan-fan-mid"
		label = "中速"
	elseif p < 255 then
		dur = string.format("%.2f", 0.95 - ((p - 160) / 95) * 0.35) -- 0.95 ~ 0.60s
		cls = "hinfan-fan-fast"
		label = "高速"
	else
		dur = "0.50"
		cls = "hinfan-fan-max"
		label = "全速"
	end

	-- 5 片叶子（同一个路径旋转 72°×n）；不要用 <use>/href，兼容性最稳
	local blades = {}
	local i
	for i = 0, 4 do
		blades[#blades + 1] = string.format(
			'<path d="M32 32C21 25 19 11 29 4C37 9 39 23 32 32Z"%s/>',
			i == 0 and "" or string.format(' transform="rotate(%d 32 32)"', i * 72))
	end

	local anim = ""
	if dur then
		anim = string.format('<animateTransform attributeName="transform" attributeType="XML"' ..
			' type="rotate" from="0 32 32" to="360 32 32" dur="%ss" repeatCount="indefinite"/>', dur)
	end

	local svg = string.format(
		'<span class="hinfan-fanwrap %s" title="%s">' ..
		'<svg viewBox="0 0 64 64" width="36" height="36" aria-hidden="true">' ..
		'<circle class="hinfan-fan-ring" cx="32" cy="32" r="29"/>' ..
		'<g class="hinfan-fan-blades">%s%s</g>' ..
		'<circle class="hinfan-fan-hub" cx="32" cy="32" r="5.5"/>' ..
		'</svg></span>',
		cls, label, table.concat(blades), anim)

	return svg, label
end

-- 守护进程是否活着：优先看状态文件新鲜度（不依赖 pgrep），其次看进程
local function daemon_running(st)
	local upd = st and tonumber(st.upd or "")
	if upd and os.time() - upd <= 15 then
		return true, "状态文件新鲜"
	end
	if run("pgrep -f hinlinkfan-daemon >/dev/null 2>&1") then
		return true, "进程在运行"
	end
	return false, "进程未运行"
end

-- ============ 状态卡片（HTML 注入到页面描述区） ============

local MODE_INFO = {
	kernel = { label = "内核温控", cls = "ok", note = "设备树 cooling-maps 自动调速" },
	manual = { label = "自定义曲线", cls = "info", note = "用户态守护进程接管 pwm1" },
	off = { label = "强制停转", cls = "bad", note = "已放弃风扇散热保护" },
	full = { label = "全速", cls = "warn", note = "风扇 100% 运转" },
}

local ENABLE_TEXT = {
	["0"] = "不控速（PWM 输出关闭）",
	["1"] = "手动（写 pwm1 生效）",
	["2"] = "自动（设备树温控接管）",
}

local function temp_class(t)
	if not t then return "" end
	if t >= 75 then return "hot" end
	if t >= 60 then return "warn" end
	if t >= 45 then return "info" end
	return "ok"
end

local function status_card()
	local hw = find_hwmon()
	local zone = find_temp_zone()
	local cdev = find_cdev()
	local st = read_state()

	local temp = zone and read_num(zone .. "/temp")
	temp = temp and temp / 1000 or nil

	local pwm = hw and read_num(hw .. "/pwm1")
	local en = hw and read(hw .. "/pwm1_enable")
	local cur = cdev and read(cdev .. "/cur_state")
	local maxs = cdev and read(cdev .. "/max_state")

	local mode = st.mode or "kernel"
	local mi = MODE_INFO[mode] or MODE_INFO.kernel

	local run, why = daemon_running(st)

	local pwm_pct = pwm and math.floor(pwm / 2.55 + 0.5) or 0

	local h = {}
	h[#h + 1] = '<div class="hinfan-card">'

	-- 模式徽章
	h[#h + 1] = string.format(
		'<div class="hinfan-hero hinfan-%s">' ..
		'<div class="hinfan-hero-main"><span class="hinfan-badge">%s</span>' ..
		'<span class="hinfan-hero-note">%s</span></div>' ..
		'<div class="hinfan-hero-sub">控制权 pwm1_enable=%s · %s</div></div>',
		mi.cls, mi.label, mi.note, en or "?", ENABLE_TEXT[en or ""] or "未知")

	-- 温度 + 转速 两块数据
	h[#h + 1] = '<div class="hinfan-grid">'

	h[#h + 1] = string.format(
		'<div class="hinfan-tile"><div class="hinfan-tile-k">CPU 温度</div>' ..
		'<div class="hinfan-tile-v hinfan-t-%s">%s<span class="hinfan-unit">°C</span></div>' ..
		'<div class="hinfan-tile-s">%s</div></div>',
		temp_class(temp),
		temp and string.format("%.1f", temp) or "--",
		zone and zone:gsub("^/sys/class/thermal/", "") or "未找到 thermal zone")

	local fan, fan_label = fan_svg(pwm)

	h[#h + 1] = string.format(
		'<div class="hinfan-tile"><div class="hinfan-tile-k">风扇转速</div>' ..
		'<div class="hinfan-tile-v">%s<span class="hinfan-unit">/255</span>%s</div>' ..
		'<div class="hinfan-bar"><span style="width:%d%%"></span></div>' ..
		'<div class="hinfan-tile-s">%s · 占空比约 %d%%</div></div>',
		pwm or "--", fan, pwm_pct, fan_label, pwm_pct)

	h[#h + 1] = string.format(
		'<div class="hinfan-tile"><div class="hinfan-tile-k">内核档位</div>' ..
		'<div class="hinfan-tile-v">%s<span class="hinfan-unit">/%s</span></div>' ..
		'<div class="hinfan-tile-s">%s</div></div>',
		cur or "--", maxs or "--",
		cdev and "档位表来自设备树 cooling-levels" or "未注册 cooling device")

	h[#h + 1] = string.format(
		'<div class="hinfan-tile"><div class="hinfan-tile-k">守护进程</div>' ..
		'<div class="hinfan-tile-v hinfan-t-%s">%s</div>' ..
		'<div class="hinfan-tile-s">%s</div></div>',
		run and "ok" or "bad", run and "运行中" or "未运行",
		string.format("%s · %s", why or "", st.upd and os.date("%H:%M:%S", tonumber(st.upd)) or "--"))

	h[#h + 1] = "</div>" -- hinfan-grid

	if st.note and st.note ~= "" then
		h[#h + 1] = string.format('<div class="hinfan-note">%s</div>', st.note)
	end

	h[#h + 1] = "</div>"

	return table.concat(h)
end

-- ============ 页面 ============

local m = Map("hinlinkfan", "调温大师", status_card())

m.description = m.description .. [[
<div class="hinfan-help">
<b>两层温控，互不打架：</b>
默认由<b>内核</b>按设备树里的 <code>cooling-maps</code> 调速（45/55/65/75°C 四档台阶，
70°C 起 CPU 还会自动降频，95°C 由 tsadc 硬件过温保护兜底）；
勾选<b>自定义曲线</b>后由用户态守护进程接管风扇，关闭时会自动把控制权交还内核。
</div>
]]

-- ---------------- 1. 控制模式 ----------------

local s = m:section(TypedSection, "global", "控制模式",
	"选谁来决定风扇转速。除『自定义曲线』外都不需要用户态进程参与。")
s.anonymous = true
s.addremove = false

local o = s:option(ListValue, "mode", "风扇控制方式")
o.widget = "radio"
o.default = "kernel"
o:value("kernel", "内核温控（推荐）",
	"完全交给设备树 cooling-maps：45°C 起转、55/65/75°C 逐档加速，内核自己带 4°C 回差")
o:value("manual", "自定义曲线",
		"由调温大师守护进程按下面的曲线调速，关闭时会自动交还内核温控")
o:value("full", "全速运转",
	"风扇 255/255 常转，适合长时间压测或大文件传输")
o:value("off", "强制停转（谨慎）",
	"直接关闭 PWM 输出。此时内核设备树的过热保护对风扇失效，只剩 CPU 降频和 95°C 硬件复位")

-- ---------------- 2. 自定义曲线 ----------------

local s2 = m:section(TypedSection, "global", "自定义曲线",
	"仅当控制方式选『自定义曲线』时生效。温度按升序，转速必须跟着单调不减；" ..
	"填错会自动排序/纠正，实在不可用则回退到设备树档位对应的默认曲线。")
s2.anonymous = true
s2.addremove = false

-- 预设：只在"选择发生变化"时才写曲线，避免每次保存都覆盖手工调整
local PRESETS = {
	quiet = {
		label = "静音优先",
		desc = "50°C 才起转，转速更保守，夜里几乎听不到",
		curve = "50:90 60:120 70:165 80:210",
		hyst = "6",
	},
	balanced = {
		label = "均衡（默认，同设备树）",
		desc = "45/55/65/75°C 四档，和内核温控的台阶一致",
		curve = "45:90 55:120 65:165 75:210",
		hyst = "4",
	},
	performance = {
		label = "散热优先",
		desc = "40°C 就起转并给到更高转速，适合密闭机箱",
		curve = "40:120 48:165 56:210 64:255",
		hyst = "3",
	},
}

local po = s2:option(ListValue, "preset", "一键预设",
	"只在选择改变时套用（会把下面的曲线整段替换掉）；选中『保持当前曲线』则什么也不动")
po.widget = "radio"
po.rmempty = false
po:value("keep", "保持当前曲线", "不套用预设，保留下面的手工曲线")
for k, v in pairs(PRESETS) do
	po:value(k, v.label, v.desc)
end
po.default = "keep"

po.write = function(self, section, value)
	local p = PRESETS[value]
	if not p then return end -- keep / 空值：不动曲线

	local c = self.map.uci
	-- 与上次套用的预设相同就不写：否则每次保存都会把手调曲线抹掉
	if c:get("hinlinkfan", section, "preset_applied") == value then
		return
	end

	c:delete("hinlinkfan", section, "curve")
	local entry
	for entry in p.curve:gmatch("%S+") do
		c:add_list("hinlinkfan", section, "curve", entry)
	end
	c:set("hinlinkfan", section, "hysteresis", p.hyst)
	c:set("hinlinkfan", section, "preset_applied", value)
end

-- 显示当前曲线来自哪个预设，便于判断还该不该再选预设
o = s2:option(DummyValue, "_applied", "当前曲线来源")
function o.cfgvalue(self, section)
	local v = self.map.uci:get("hinlinkfan", section, "preset_applied")
	if v and PRESETS[v] then
		return "已套用预设：" .. PRESETS[v].label .. "（下面曲线即该预设内容）"
	end
	return v and v ~= "" and ("已套用预设：" .. v) or "手工曲线"
end

o = s2:option(DynamicList, "curve", "温度 → 转速 阶梯",
	"格式 温度:转速，例如 45:90 表示 45°C 时 PWM 给到 90/255；最多 8 档")
o.placeholder = "45:90"
o.rmempty = false
o.optional = true
-- 不设 datatype：AbstractValue.transform 就是按 datatype 再校验一次的，
-- DynamicList 传进来的是一张表，datatype 报不过会**静默不保存**，这里用自己的 validate 管到底。
function o:validate(value)
	-- 成功必须返回原值（返回 nil 会被当成校验失败），失败返回 nil + 错误文案
	local items = type(value) == "table" and value or { value }
	local _, v, t, p
	for _, v in pairs(items) do
		v = tostring(v or "")
		if #v > 0 then
			t, p = v:match("^(%d+):(%d+)$")
			if not t then
				return nil, "每行格式应为 温度:转速（例如 45:90）"
			end
			t, p = tonumber(t), tonumber(p)
			if t < 20 or t > 110 then
				return nil, "温度请填 20 ~ 110（°C）"
			end
			if p < 0 or p > 255 then
				return nil, "转速请填 0 ~ 255"
			end
		end
	end
	return value
end

o = s2:option(Value, "tick", "采样周期（秒）",
	"默认 2 秒。改小响应更快但更耗电，改大更省电但响应迟钝")
o.datatype = "range(1,60)"
o.default = "2"

o = s2:option(Value, "hysteresis", "降档回差（°C）",
	"降温要低于档位阈值这么多才降档，避免转速在阈值上反复起停")
o.datatype = "range(0,20)"
o.default = "4"

o = s2:option(Value, "ramp", "降温步长",
	"每个采样周期最多降多少 PWM，升温是立即响应的；调小更平顺，调大更灵敏")
o.datatype = "range(1,255)"
o.default = "32"

-- ---------------- 3. 设备树 / 内核状态（只读） ----------------

local hw = find_hwmon()
local zone = find_temp_zone()
local cdev = find_cdev()
local trips = zone and read_trips(zone) or {}

local s3 = m:section(TypedSection, "global", "设备树与内核状态（只读）",
	"下面这些值直接读自 sysfs，反映的是当前生效的设备树配置，用来核对上面设置有没有落实。")
s3.anonymous = true
s3.addremove = false
s3.readonly = true

o = s3:option(DummyValue, "_drv", "风扇驱动节点")
function o.cfgvalue()
	return hw or "未找到 pwm-fan hwmon"
end

o = s3:option(DummyValue, "_cdev", "内核 cooling device")
function o.cfgvalue()
	if not cdev then
		return "未注册（检查设备树 fan 节点与 cooling-maps）"
	end
	local cur = read(cdev .. "/cur_state") or "?"
	local mx = read(cdev .. "/max_state") or "?"
	return string.format("%s（type=%s，当前档位 %s/%s）",
		cdev:gsub("^/sys/class/thermal/", ""), read(cdev .. "/type") or "?", cur, mx)
end

o = s3:option(DummyValue, "_zone", "温度传感器")
function o.cfgvalue()
	return zone or "未找到 cpu-thermal"
end

local i
for i = 1, #trips do
	local tp = trips[i]
	o = s3:option(DummyValue, "_trip" .. i,
		string.format("温度阶梯 #%d", i))
	o.cfgvalue = function()
		return string.format("%.0f°C　类型 %s　回差 %.0f°C", tp.temp, tp.type, tp.hyst)
	end
end

o = s3:option(DummyValue, "_st", "守护进程上报")
function o.cfgvalue()
	local st = read_state()
	if not st.mode then
		return "暂无状态文件（守护进程未运行）"
	end
	return string.format("模式 %s　温度 %s°C　PWM %s（目标 %s）　档位 %s",
		st.mode or "?", st.temp or "--", st.pwm or "--",
		st.target or "--", st.level or "--")
end

m.on_commit = function()
	run("/etc/init.d/hinlinkfaninit restart >/dev/null 2>&1")
	-- 给守护进程一点时间落地新配置，让同一次请求里的状态卡片就是新值
	run("sleep 1")
end

return m
