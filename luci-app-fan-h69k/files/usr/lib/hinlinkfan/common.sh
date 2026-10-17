#!/bin/sh
#
# H69K 风扇公共函数库
# 被 /etc/init.d/hinlinkfaninit 和 /usr/sbin/hinlinkfan-daemon 共同引用，
# 负责硬件节点探测、控制权切换（pwm1_enable）与状态文件读写。
#
# pwm1_enable 语义（内核 hwmon/pwm-fan，见 Documentation/hwmon/pwm-fan.rst）：
#   0 = 不控速（PWM 输出关闭，风扇停转）—— 同时也等于放弃内核过热保护
#   1 = 手动（写 pwm1 生效，内核热控不再写风扇）
#   2 = 自动（由设备树的 cooling-maps 驱动，写 pwm1 被忽略）★ 默认
#
# H69K 设备树里 fan 节点的 cooling-levels = <0 90 120 165 210 255>，
# 内核热控用档位 1..4，用户态接管时用 1..5/255。

HINFAN_STATE=/var/run/hinlinkfan.state
HINFAN_LOG_TAG=hinlinkfan

hinlinkfan_log() {
	logger -t "$HINFAN_LOG_TAG" "$*"
}

# 找到 pwm-fan 的 hwmon 目录（里面有 pwm1 / pwm1_enable / name）
hinlinkfan_hwmon() {
	local d

	for d in /sys/class/hwmon/hwmon*; do
		[ -d "$d" ] || continue
		[ -w "$d/pwm1" ] || continue
		case "$(cat "$d/name" 2>/dev/null)" in
		pwmfan|pwm-fan|pwm_fan)
			echo "$d"
			return 0
			;;
		esac
	done

	# 退化匹配：按平台设备名找（个别内核 hwmon name 不同）
	for d in /sys/devices/platform/*pwm-fan*/hwmon/hwmon*; do
		[ -w "$d/pwm1" ] && {
			echo "$d"
			return 0
		}
	done

	return 1
}

# 内核视角的风扇 cooling device（由设备树注册，用于页面显示档位/上限）
hinlinkfan_cdev() {
	local c

	for c in /sys/class/thermal/cooling_device*; do
		[ -r "$c/type" ] || continue
		case "$(cat "$c/type" 2>/dev/null)" in
		pwm-fan|pwmfan)
			echo "$c"
			return 0
			;;
		esac
	done

	return 1
}

# CPU 温度 zone（设备树的 4 个风扇 trip 都挂在 cpu-thermal 上）
hinlinkfan_temp_zone() {
	local z

	for z in /sys/class/thermal/thermal_zone*; do
		[ -r "$z/type" ] || continue
		case "$(cat "$z/type" 2>/dev/null)" in
		cpu-thermal)
			[ -r "$z/temp" ] && {
				echo "$z"
				return 0
			}
			;;
		esac
	done

	for z in /sys/class/thermal/thermal_zone*; do
		[ -r "$z/type" ] || continue
		case "$(cat "$z/type" 2>/dev/null)" in
		soc-thermal|cpu_thermal)
			[ -r "$z/temp" ] && {
				echo "$z"
				return 0
			}
			;;
		esac
	done

	return 1
}

# 温度（毫摄氏度）
hinlinkfan_temp_mc() {
	local z

	z=$(hinlinkfan_temp_zone) || return 1
	cat "$z/temp" 2>/dev/null
}

# 切换控制权：$1 = 0/1/2，只在值变化时写
hinlinkfan_set_pwm_enable() {
	local d cur

	d=$(hinlinkfan_hwmon) || {
		hinlinkfan_log "找不到 pwm-fan hwmon 节点"
		return 1
	}

	cur=$(cat "$d/pwm1_enable" 2>/dev/null)
	[ "$cur" = "$1" ] && return 0

	if echo "$1" >"$d/pwm1_enable" 2>/dev/null; then
		return 0
	fi

	hinlinkfan_log "写 pwm1_enable=$1 失败（当前 $cur）"
	return 1
}

# 手动写转速（仅 pwm1_enable=1 时生效）
hinlinkfan_set_pwm() {
	local d

	d=$(hinlinkfan_hwmon) || return 1
	echo "$1" >"$d/pwm1" 2>/dev/null || return 1
	return 0
}

hinlinkfan_get_pwm() {
	local d

	d=$(hinlinkfan_hwmon) || return 1
	cat "$d/pwm1" 2>/dev/null
}

# 状态文件：给 LuCI 页面显示"谁在控制、当前档位、目标转速"用
# 用法: hinlinkfan_state_write <mode> <temp> <pwm> <target> <level> <note>
hinlinkfan_state_write() {
	{
		echo "mode=$1"
		echo "temp=$2"
		echo "pwm=$3"
		echo "target=$4"
		echo "level=$5"
		echo "note=$6"
		echo "upd=$(date +%s 2>/dev/null)"
	} >"$HINFAN_STATE" 2>/dev/null
	return 0
}

hinlinkfan_state_read() {
	# $1 = 键名
	[ -r "$HINFAN_STATE" ] || return 1
	sed -n "s/^$1=//p" "$HINFAN_STATE" 2>/dev/null | head -n 1
}
