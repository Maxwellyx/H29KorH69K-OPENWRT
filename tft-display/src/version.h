/*
 * 屏幕固件版本号 —— 只给「开机页 + 启动日志 + -version」用，不参与任何业务逻辑。
 *
 * 为什么要单独一个头：
 *   屏幕上那行版本号必须是**编译期常量**（开机时 WS/服务端都还没连上，没法问别人），
 *   而且必须**跟着包走** —— 用户看到屏幕上写着 v2.20-69，就说明这一台装的一定是那一包。
 *   所以版本只在打包时注入，运行时不读文件、不读 opkg 库（读库会读到「正在装的那个」）。
 *
 * 注入优先级（高的在前）：
 *   1. -DTFT_APP_VERSION="..."   —— src/Makefile 里给 OpenWrt 树内编译用（TFT_VERSION）；
 *   2. build_version.h           —— tools\build-ipk-aarch64.ps1 每次打包都重新生成，
 *                                   落在 build-aarch64\ 里，靠 -I 进来；
 *   3. 兜底 "dev"                —— 谁都没给（手工 cc/gcc 直接编）时不显示假版本号。
 *
 * ⚠️ 提交前别手改 build_version.h：它是生成物，改了下次打包就被覆盖。
 */
#ifndef TFT_VERSION_H
#define TFT_VERSION_H

#ifndef TFT_APP_VERSION
#  if defined(__has_include)
#    if __has_include("build_version.h")
#      include "build_version.h"
#    endif
#  endif
#endif

#ifndef TFT_APP_VERSION
#  define TFT_APP_VERSION "dev"
#endif

/* 构建时间：build_version.h 里给（打包那一刻）；没给就用编译器自带的 __DATE__/__TIME__ */
#ifndef TFT_BUILD_DATE
#  define TFT_BUILD_DATE __DATE__ " " __TIME__
#endif

/* 开机页/日志用的整行文本。字符串拼接是编译期的，不占运行期栈。 */
#define TFT_VERSION_LINE  "v" TFT_APP_VERSION
#define TFT_BUILDDATE_LINE "build " TFT_BUILD_DATE

#endif /* TFT_VERSION_H */
