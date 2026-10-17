#ifndef TFT_BATTERY_H
#define TFT_BATTERY_H

#include <stdbool.h>

/*
 * 本地电池电量 + 适配器电压 —— **都不走服务端/模组**（用户 2026-10-07：不要从 modem 那边）。
 *
 * 口径**照抄原版 main.c**（2.18 版实现），这族板子（H29K / F50-PRO）上实测标定过：
 *   电池   = /sys/bus/iio/devices/iio:device0/in_voltage3_raw
 *            V = raw / 1024 × 1.8 × 5.95     （2 节串联：6.1 V = 0%，8.4 V = 100%，线性）
 *   适配器 = /sys/bus/iio/devices/iio:device0/in_voltage2_raw
 *            V = raw / 1023 × 1.8 × 11       （≥11 V 显示充电图标；4.5~10 V 报 "Power ERROR"）
 * 实测：ch3=665 → 6.96 V → 37%；ch2=617 → 11.93 V → 充电中。
 * ⚠️ 别用 sysfs 的 `in_voltage_scale`（本机驱动报 0.439453 = 1.8/4096，比实际口径小 4 倍）。
 *
 * 板子若带电量计驱动（/sys/class/power_supply 里有 type=Battery 的电源），那里优先。
 * 读不到 / 不在合理范围（电池默认 6.0~9.0 V）⇒ 返回 -1，界面显示 "--"，**绝不编假数据**
 * （没电池的 H29K 上 ch3 只有 5.3 V ⇒ 正好 "--"）。
 *
 * 配置：`/etc/tftdisplay.battery`（**包外**文件，opkg 升级不覆盖；与亮度文件同一套路）：
 *   adc / chg_adc      电池 / 适配器通道     div / chg_div   分压比（5.95 / 11）
 *   scale              mV/LSB（1.7578125，两条通道共用）
 *   min_mv / max_mv    电池合理范围（6000 / 9000）⇒ 超出 = 没有电池
 *   empty_mv / full_mv 电量两端（6100 / 8400）
 *   chg_on_mv          适配器 ≥ 此值算充电中（11000）
 *   chg_err_min_mv / chg_err_max_mv  适配器落在这里报错（4500 / 10000）
 *   ps                 电源名或目录（默认自动扫 power_supply；0 = 不用）
 *   debug              1 = 每轮打一行读数
 * 环境变量（优先级更高）：TFT_BATT_ADC / TFT_BATT_CHG_ADC / TFT_BATT_DIV / TFT_BATT_CHG_DIV /
 *   TFT_BATT_SCALE / TFT_BATT_CHG_SCALE / TFT_BATT_MIN_MV / TFT_BATT_MAX_MV / TFT_BATT_EMPTY_MV /
 *   TFT_BATT_FULL_MV / TFT_BATT_CHG_ON_MV / TFT_BATT_CHG_ERR_MIN_MV / TFT_BATT_CHG_ERR_MAX_MV /
 *   TFT_BATT_PS / TFT_BATT_SYSFS / TFT_BATT_CONF / TFT_BATT_DEBUG
 */

void battery_init(void);            /* 读配置 + 打自述日志（main 早期调一次） */
int  battery_percent(void);         /* 0..100；-1 = 没有电池 / 读不到 */
int  battery_millivolts(void);      /* 电池电压 mV；-1 = 未知 */
int  battery_charge_mv(void);       /* 适配器电压 mV；-1 = 读不到 */
bool battery_charging(void);        /* 充电中（电量计 status 优先，否则适配器电压 ≥ 11 V） */
bool battery_charge_error(void);    /* 适配器电压异常（4.5~10 V） */
int  battery_raw(void);             /* 电池通道原始值；-1 = 读不到（标定用） */

#endif /* TFT_BATTERY_H */
