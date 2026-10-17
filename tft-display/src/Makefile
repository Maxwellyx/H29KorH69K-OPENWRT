
CC ?= gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}
# ⚠️ 头文件搜索路径：lvgl/lvgl.mk 里 `CFLAGS += "-I$(LVGL_PATH)"`，而它把 LVGL_PATH
#    设成 `${shell pwd}/lvgl`（= src/lvgl）—— 这只够 `#include "lvgl.h"`。
#    本项目源码用的是 `#include "lvgl/lvgl.h"`，需要再加一层到 src/，所以下面显式补 -I。
#    （不要在这里改写 LVGL_PATH：lvgl.mk 靠它 find 出 lvgl 的全部源码，
#      改错会让 lvgl 自己的 .c 不进编译，变成链接期一堆 lv_* undefined reference。）
override CFLAGS += -I$(LVGL_DIR) -I$(LVGL_DIR)/$(LVGL_DIR_NAME)
BIN = tftdisplay
MAINSRC = main.c $(wildcard pages/*.c) $(wildcard utils/*.c) 
# MAINSRC += lv_font_source_han_sans_bold_20.c
# $(wildcard ui/*.c) $(wildcard utils/*.c) $(wildcard modem/*.c)

# 版本号：开机页 / -version / 启动日志显示的那个。
# 外层 package Makefile 传 TFT_VERSION="$(PKG_VERSION)-$(PKG_RELEASE)"；
# 自己手工 make 时不传就是 dev（比乱猜一个号强，见 src/version.h）。
# ⚠️ 必须 override：外层是 `CFLAGS="..."` 从**命令行**传进来的，GNU make 会把
#    普通 `CFLAGS +=` 整个忽略掉（命令行变量不给追加），加了 override 才生效。
# ⚠️ 这里的单引号是给 shell 用的，编译器最终看到 -DTFT_APP_VERSION="2.20-69"。
TFT_VERSION ?= dev
override CFLAGS += -DTFT_APP_VERSION='"$(TFT_VERSION)"'

include $(LVGL_DIR)/lvgl/lvgl.mk

OBJEXT ?= .o 

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))

MAINOBJ = $(MAINSRC:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS)


all: default

%.o: %.c
	@$(CC)  $(CFLAGS) -c $< -o $@
	@echo "CC $<"
    
default: $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)

clean: 
	rm -f $(BIN) $(AOBJS) $(COBJS) $(MAINOBJ)
