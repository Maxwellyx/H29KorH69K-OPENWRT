#
# oled 显示程序（交叉编译版）
#
# 【OpenWrt 移植说明】
#   原版用的都是 `:=`（CPPFLAGS/CFLAGS/LDFLAGS），而 openwrt 的
#   Build/Compile/Default 会以【命令行变量】的形式传入
#   CPPFLAGS/CFLAGS/LDFLAGS（里面有 -I staging 头文件路径、-L staging 库路径）。
#   命令行变量会覆盖 Makefile 里的赋值 —— 于是 `-lconfig -lpthread` 会被丢掉，
#   链接必然报 undefined reference。这里全改成 `+=`，两边都保留。
#
ifndef CC
CC = gcc
endif
ifndef LD
LD = ld
endif

SOURCES = SSD1306_OLED_Library/SSD1306_OLED.c Example_Code/main.c Example_Code/example_app.c I2C_Library/I2C.c
OBJS := $(SOURCES:.c=.o)

CPPFLAGS += -I SSD1306_OLED_Library -I I2C_Library
CFLAGS += -O2
LDFLAGS += -lpthread -lconfig

oled: $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

ssd1306: $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS) --static
	strip $@

clean:
	rm -rf oled ssd1306 $(OBJS)

compile: oled

install: compile
	mkdir -p $(DESTDIR)/usr/bin
	cp oled $(DESTDIR)/usr/bin/oled
