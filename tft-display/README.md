# luci-app-lvgl-sysinfo

**代码质量可能尚有不足，敬请谅解！如果您 fork 本项目，请保留仓库地址。**

**luci-app-lvgl-sysinfo** 是一个用于 **OpenWrt FB 设备显示** 的插件，旨在提供系统信息的直观展示，提升用户对设备状态的了解和管理。

![系统信息](./demo/1.png)

![信号信息](./demo/2.png)

## 当前状态

- ✅ **H29K 显示功能**  
  H29K 显示功能已完成并正常运行，能够准确展示系统信息。

- ❌ **LuCI 界面**  
  LuCI 界面尚未完成，正在开发中。

## TODO

- [ ] 完成 LuCI 界面开发  
  为项目添加用户友好的 LuCI 界面，提升用户体验和操作便捷性。

## 特性

- **实时系统信息显示**：监控 CPU、内存、网络等关键系统指标。
- **简洁美观的界面**：基于 LVGL 提供流畅的用户界面体验。
- **高兼容性**：适配多种 OpenWrt FB 设备，确保广泛的适用性。
- **息屏 / 亮屏**：无操作一段时自动息屏（默认 3 分钟，SYSTEM→息屏配置页可改档位），
  摸一下屏幕亮屏（这一下会被"吃掉"，不会误点到底下的按钮）。
  ⚠️ **息屏时来新短信会把屏幕叫亮**，并保持 30 秒（`TFT_ALERT_AWAKE_MS` 可改），
  同时弹出新短信提示框 —— 不然提示框画在关着背光的屏上，没人知道来过短信。

## 安装说明

### 前提条件

确保您的系统满足以下条件：

- 已安装 **OpenWrt** 操作系统
- 设备支持 **FB (Framebuffer)** 显示



  //     `LV_SYMBOL_BATTERY_FULL` 是 LVGL 内置的符号之一。LVGL 还内置了许多其他常用符号，可以直接用于 `lv_label_set_text`，如：
    // - `LV_SYMBOL_OK`              // 对号
    // - `LV_SYMBOL_CLOSE`           // 叉号
    // - `LV_SYMBOL_POWER`           // 电源
    // - `LV_SYMBOL_SETTINGS`        // 设置/齿轮
    // - `LV_SYMBOL_HOME`            // 房子
    // - `LV_SYMBOL_DOWNLOAD`        // 下载
    // - `LV_SYMBOL_WIFI`            // WiFi
    // - `LV_SYMBOL_BLUETOOTH`       // 蓝牙
    // - `LV_SYMBOL_BATTERY_FULL`    // 电池满
    // - `LV_SYMBOL_BATTERY_3`       // 电池3格
    // - `LV_SYMBOL_BATTERY_2`       // 电池2格
    // - `LV_SYMBOL_BATTERY_1`       // 电池1格
    // - `LV_SYMBOL_BATTERY_EMPTY`   // 电池空
    // - `LV_SYMBOL_CHARGE`          // 闪电/充电
    // - `LV_SYMBOL_USB`             // USB
    // - `LV_SYMBOL_VOLUME_MAX`      // 音量最大
    // - `LV_SYMBOL_VOLUME_MID`      // 音量中
    // - `LV_SYMBOL_VOLUME_MIN`      // 音量小
    // - `LV_SYMBOL_MUTE`            // 静音
    // - `LV_SYMBOL_LEFT`            // 左箭头
    // - `LV_SYMBOL_RIGHT`           // 右箭头
    // - `LV_SYMBOL_UP`              // 上箭头
    // - `LV_SYMBOL_DOWN`            // 下箭头
    // - `LV_SYMBOL_WARNING`         // 警告
    // - `LV_SYMBOL_EDIT`            // 编辑/铅笔
    // - `LV_SYMBOL_EYE_OPEN`        // 眼睛开
    // - `LV_SYMBOL_EYE_CLOSE`       // 眼睛关
    // - `LV_SYMBOL_TRASH`           // 垃圾桶
    // - `LV_SYMBOL_REFRESH`         // 刷新
    // - `LV_SYMBOL_PLUS`            // 加号
    // - `LV_SYMBOL_MINUS`           // 减号
    // - `LV_SYMBOL_IMAGE`           // 图片
    // - `LV_SYMBOL_AUDIO`           // 音频
    // - `LV_SYMBOL_VIDEO`           // 视频
    // - `LV_SYMBOL_LIST`            // 列表
    // - `LV_SYMBOL_OK`              // OK
    // - `LV_SYMBOL_SAVE`            // 保存
    // - `LV_SYMBOL_BELL`            // 铃铛
    // - `LV_SYMBOL_KEYBOARD`        // 键盘
    // - `LV_SYMBOL_GPS`             // GPS
    // - `LV_SYMBOL_CALL`            // 电话
    // - `LV_SYMBOL_CUT`             // 剪刀
    // - `LV_SYMBOL_COPY`            // 复制
    // - `LV_SYMBOL_PASTE`           // 粘贴
    // - `LV_SYMBOL_CHARGE`          // 充电

    // 你可以在 LVGL 的头文件 `lv_symbol_def.h` 或 [官方文档符号列表](https://docs.lvgl.io/latest/en/html/widgets/label.html#symbols) 查看全部符号。
    // **用法示例：**
    // ```c
    // lv_label_set_text(label, LV_SYMBOL_WIFI);
    // ```
    // 如需多个符号和文字组合：
    // ```c
    // lv_label_set_text(label, LV_SYMBOL_BATTERY_FULL " 80%");
    // 创建电池图标（用label方式放大符号）


