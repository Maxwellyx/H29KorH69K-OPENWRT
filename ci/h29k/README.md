# H29K RM502Q-GL 云编译

此流程为 H29K Pro 2025 无电池版、移远 RM502Q-GL 准备工程固件。
它使用原作者 `hinlink_opc-h29k` 设备配置，并固定 OpenWrt、适配源码、两个子模块和 feeds 版本。

在仓库 **Actions → Build H29K RM502Q-GL → Run workflow** 手动启动。
修改 `ci/h29k/` 或此工作流并推送到 main 时也会启动。
所有编译在 GitHub 的 Ubuntu 24.04 runner 上进行，本机不需要安装 Linux。

成功后下载 `H29K-RM502Q-GL-运行编号`，其中包含 `.img.gz`、SHA256SUMS、设备元数据、完整配置和源码版本记录。
失败时查看 `H29K-build-logs-运行编号`；失败不会上传标成可用固件的产物。

配置保留中文 LuCI、板载 AIC8800 SDIO、GC9307 屏幕、AXS5106 触摸、modem-pro 和 USB 蜂窝驱动。
保留 QMI/MBIM/ECM/NCM/RNDIS 和串口驱动，以 RM502Q-GL 为主要验证目标；其他型号仍需确认识别与拨号兼容性。
采用作者较小的 M 应用集合，避免全选大型服务。

本流程补齐了原仓库未提交的构建安装步骤。适配包从固定 Git 对象导出到独立工作副本：

- 将补丁与脚本转换为 LF；AIC 驱动解包后、打补丁前还会规范补丁涉及的源码换行，并修复 cfg80211 接口检测的 Makefile 引号解析问题。
- 补丁通过 `git apply --check` 后才应用；不忽略空白、不猜写入地址。
- 仅从工作副本的 modem-pro 中移除 Android `adb-enablemodem` 依赖，保留串口及 USB 网卡拨号逻辑。
- 每次 make 都设置 `DEVTOOLS_SUFFIX=M`，并验证关键软件包没有被 defconfig 丢弃。
- 为子模块遗漏的 `modemd` 启动脚本补齐最小 procd 服务，明确读取 `/etc/modemd/config.toml`，并在编译前检查打包输入。
- 为遗漏的现代 LuCI 页面补齐最小入口：设备中心打开原作者的内置网页；服务控制使用当前 OpenWrt 的 `rc.list/rc.init`，对应权限从旧接口等价迁移。此补齐页面不复刻原作者未提交的悬浮组件。

流程只缓存下载源码及编译器缓存，失败后也会尽量保存；源码、配置、补丁处理和镜像校验每次仍会执行。缓存不包含 `build_dir`、`staging_dir` 或已生成镜像。

构建成功及镜像结构检查不等于真机适配测试。主板具体屏幕、触摸和电源接线仍需与设备确认；此流程不自动刷写任何设备，也不提供未知来源的 loader。
