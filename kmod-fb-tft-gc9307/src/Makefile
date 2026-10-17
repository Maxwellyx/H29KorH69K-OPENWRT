obj-m += gc9307.o

# fbtft 的内部头文件 fbtft.h 不在 include/ 下，要指到内核树的 staging 目录
ccflags-y += -I$(srctree)/drivers/staging/fbtft
