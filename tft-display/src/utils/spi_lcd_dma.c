#include "spi_lcd_dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

// 内部函数声明
static int spi_init(spi_lcd_dev_t *dev);
static int gpio_init(spi_lcd_dev_t *dev);
static void gpio_deinit(spi_lcd_dev_t *dev);
static int dma_init(spi_lcd_dev_t *dev);
static void dma_deinit(spi_lcd_dev_t *dev);
static void lcd_reset(spi_lcd_dev_t *dev);
static void lcd_send_cmd(spi_lcd_dev_t *dev, uint8_t cmd);
static void lcd_send_data(spi_lcd_dev_t *dev, uint8_t data);
static void lcd_send_data_burst(spi_lcd_dev_t *dev, const uint8_t *data, size_t len);
static void lcd_set_window(spi_lcd_dev_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static void lcd_init_by_type(spi_lcd_dev_t *dev);

// GC9307初始化命令
static const uint8_t gc9307_init_cmds[] = {
    0x01, 0,        // 软件复位
    0x00, 100,      // 延迟100ms
    
    0x11, 0,        // 退出睡眠模式
    0x00, 120,      // 延迟120ms
    
    0xCB, 3,        // POWER_CONTROL_A
    0x39, 0x2C, 0x00,
    
    0xCF, 3,        // POWER_CONTROL_B
    0x00, 0xC1, 0x30,
    
    0xE8, 3,        // DRIVER_TIMING_CONTROL_A
    0x85, 0x00, 0x78,
    
    0xEA, 2,        // DRIVER_TIMING_CONTROL_B
    0x00, 0x00,
    
    0xED, 4,        // POWER_ON_SEQUENCE_CONTROL
    0x64, 0x03, 0x12, 0x81,
    
    0xF7, 1,        // PUMP_RATIO_CONTROL
    0x20,
    
    0xC0, 1,        // POWER_CONTROL_1
    0x23,
    
    0xC1, 1,        // POWER_CONTROL_2
    0x10,
    
    0xC5, 2,        // VCOM_CONTROL_1
    0x3E, 0x28,
    
    0xC7, 1,        // VCOM_CONTROL_2
    0x86,
    
    0x36, 1,        // MEMORY_DATA_ACCESS_CONTROL
    0x00,           // 屏幕方向配置
    
    0x3A, 1,        // COLMOD_PIXEL_FORMAT_SET
    0x05,           // 16位像素格式0x05, 
    
    0xB1, 2,        // FRAME_RATE_CONTROL_IN_NORMAL_MODE
    0x00, 0x18,
    
    0xB6, 3,        // DISPLAY_FUNCTION_CONTROL
    0x08, 0x82, 0x27,
    
    0xF2, 1,        // 3GAMMA_EN
    0x00,
    
    0x26, 1,        // GAMMA_SET
    0x01,
    
    // 正gamma校正
    0xE0, 15,       // POSITIVE_GAMMA_CORRECTION
    0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
    0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    
    // 负gamma校正
    0xE1, 15,       // NEGATIVE_GAMMA_CORRECTION
    0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
    0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,



    0x29, 0,        // 显示开启
    0x00, 120       // 延迟120ms
};

// LVGL 9.3 显示缓冲区和显示设备
static lv_display_t *disp;
static lv_color_t *buf1;
static lv_color_t *buf2;


// 初始化SPI LCD设备
int spi_lcd_init(spi_lcd_dev_t *dev) {
    if (!dev) return -1;
    
    // 初始化SPI
    if (spi_init(dev) != 0) {
        fprintf(stderr, "SPI initialization failed\n");
        return -1;
    }
    
    // 初始化GPIO
    if (gpio_init(dev) != 0) {
        fprintf(stderr, "GPIO initialization failed\n");
        close(dev->spi_fd);
        return -1;
    }
    
    // 初始化DMA
    if (dma_init(dev) != 0) {
        fprintf(stderr, "DMA initialization failed\n");
        gpio_deinit(dev);
        close(dev->spi_fd);
        return -1;
    }
    usleep(120000);   
    // 复位LCD
    lcd_reset(dev);
    usleep(120000);   
    // 根据屏幕类型执行初始化命令
    lcd_init_by_type(dev);

    return 0;
}

// 释放SPI LCD设备资源
void spi_lcd_deinit(spi_lcd_dev_t *dev) {
    if (!dev) return;
    
    // 关闭显示
    if (dev->lcd_type == LCD_TYPE_ILI9341 || 
        dev->lcd_type == LCD_TYPE_ST7789 ||
        dev->lcd_type == LCD_TYPE_GC9307) {
        lcd_send_cmd(dev, 0x28); // 关闭显示
    } else if (dev->lcd_type == LCD_TYPE_ST7735) {
        lcd_send_cmd(dev, 0x28); // 关闭显示
    }
    
    dma_deinit(dev);
    gpio_deinit(dev);
    
    if (dev->spi_fd >= 0) {
        close(dev->spi_fd);
        dev->spi_fd = -1;
    }
}

// 设置屏幕亮度
int spi_lcd_set_brightness(spi_lcd_dev_t *dev, uint8_t brightness) {
    if (!dev || dev->bl_fd < 0) return -1;
    
    // 亮度映射到0-1范围（高电平点亮背光）
    int value = (brightness > 0) ? 1 : 0;
    char buf[2];
    snprintf(buf, sizeof(buf), "%d", value);
    return write(dev->bl_fd, buf, strlen(buf)) == strlen(buf) ? 0 : -1;
}

// 清屏
void spi_lcd_clear(spi_lcd_dev_t *dev, uint16_t color) {
    if (!dev) return;
    spi_lcd_fill_rect(dev, 0, 0, dev->width - 1, dev->height - 1, color);
}

// 画点
void spi_lcd_draw_pixel(spi_lcd_dev_t *dev, uint16_t x, uint16_t y, uint16_t color) {
    if (!dev || x >= dev->width || y >= dev->height) return;
    
    lcd_set_window(dev, x, y, x, y);
    
    uint8_t data[2] = {color >> 8, color & 0xFF};
    lcd_send_data_burst(dev, data, 2);
}

// 填充矩形
void spi_lcd_fill_rect(spi_lcd_dev_t *dev, uint16_t x1, uint16_t y1, 
                      uint16_t x2, uint16_t y2, uint16_t color) {
    if (!dev || x1 > x2 || y1 > y2 || 
        x2 >= dev->width || y2 >= dev->height) return;
    
    // 设置窗口
    lcd_set_window(dev, x1, y1, x2, y2);
    
    // 计算像素数量
    uint32_t pixel_count = (x2 - x1 + 1) * (y2 - y1 + 1);
    
    // 发送颜色数据
    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;
    
    // 如果数据量较大，使用DMA加速
    if (pixel_count * 2 > 128) {  // 超过128字节使用DMA
        size_t burst_size = dev->dma_buffer_size / 2;  // 每次传输的像素数
        uint32_t remaining = pixel_count;
        
        while (remaining > 0) {
            size_t current = (remaining < burst_size) ? remaining : burst_size;
            size_t data_size = current * 2;
            
            // 填充DMA缓冲区
            for (size_t i = 0; i < current; i++) {
                dev->dma_buffer[i * 2] = color_high;
                dev->dma_buffer[i * 2 + 1] = color_low;
            }
            
            // 使用DMA发送
            lcd_send_data_burst(dev, dev->dma_buffer, data_size);
            remaining -= current;
        }
    } else {
        // 小数据量直接发送
        uint8_t *buf = malloc(pixel_count * 2);
        if (buf) {
            for (uint32_t i = 0; i < pixel_count; i++) {
                buf[i * 2] = color_high;
                buf[i * 2 + 1] = color_low;
            }
            lcd_send_data_burst(dev, buf, pixel_count * 2);
            free(buf);
        }
    }
}

// SPI初始化
static int spi_init(spi_lcd_dev_t *dev) {
    // 打开SPI设备
    dev->spi_fd = open(dev->spi_dev_path, O_RDWR);
    if (dev->spi_fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }
    
    // 设置SPI模式
    if (ioctl(dev->spi_fd, SPI_IOC_WR_MODE, &dev->spi_mode) < 0) {
        perror("Failed to set SPI mode");
        return -1;
    }
    
    // 设置SPI速度
    if (ioctl(dev->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &dev->spi_speed) < 0) {
        perror("Failed to set SPI speed");
        return -1;
    }
    
    return 0;
}

// GPIO初始化（sysfs方式）
static int gpio_init(spi_lcd_dev_t *dev) {
    char path[64];
    int fd;

    // 初始化DC引脚
    if (dev->dc_pin >= 0) {
        // 导出GPIO
        fd = open("/sys/class/gpio/export", O_WRONLY);
        if (fd < 0) {
            perror("Failed to open export for DC pin");
            return -1;
        }
        dprintf(fd, "%d", dev->dc_pin);
        close(fd);
        usleep(10000); // 等待导出完成

        // 设置为输出
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", dev->dc_pin);
        fd = open(path, O_WRONLY);
        if (fd < 0) {
            perror("Failed to set DC direction");
            return -1;
        }
        write(fd, "out", 3);
        close(fd);

        // 打开值文件
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", dev->dc_pin);
        dev->dc_fd = open(path, O_WRONLY);
        if (dev->dc_fd < 0) {
            perror("Failed to open DC value");
            return -1;
        }
    }

    // 初始化RST引脚
    if (dev->rst_pin >= 0) {
        // 导出GPIO
        fd = open("/sys/class/gpio/export", O_WRONLY);
        if (fd < 0) {
            perror("Failed to open export for RST pin");
            return -1;
        }
        dprintf(fd, "%d", dev->rst_pin);
        close(fd);
        usleep(10000); // 等待导出完成

        // 设置为输出
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", dev->rst_pin);
        fd = open(path, O_WRONLY);
        if (fd < 0) {
            perror("Failed to set RST direction");
            return -1;
        }
        write(fd, "out", 3);
        close(fd);

        // 打开值文件
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", dev->rst_pin);
        dev->rst_fd = open(path, O_WRONLY);
        if (dev->rst_fd < 0) {
            perror("Failed to open RST value");
            return -1;
        }
    }

    // 初始化背光引脚
    if (dev->bl_pin >= 0) {
        // 导出GPIO
        fd = open("/sys/class/gpio/export", O_WRONLY);
        if (fd < 0) {
            perror("Failed to open export for BL pin");
            return -1;
        }
        dprintf(fd, "%d", dev->bl_pin);
        close(fd);
        usleep(10000); // 等待导出完成

        // 设置为输出
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", dev->bl_pin);
        fd = open(path, O_WRONLY);
        if (fd < 0) {
            perror("Failed to set BL direction");
            return -1;
        }
        write(fd, "out", 3);
        close(fd);

        // 打开值文件
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", dev->bl_pin);
        dev->bl_fd = open(path, O_WRONLY);
        if (dev->bl_fd < 0) {
            perror("Failed to open BL value");
            return -1;
        }
    }

    return 0;
}

// GPIO释放（sysfs方式）
static void gpio_deinit(spi_lcd_dev_t *dev) {
    char path[64];

    if (dev->dc_fd >= 0) {
        close(dev->dc_fd);
        dev->dc_fd = -1;
        // 取消导出
        int fd = open("/sys/class/gpio/unexport", O_WRONLY);
        if (fd >= 0) {
            dprintf(fd, "%d", dev->dc_pin);
            close(fd);
        }
    }

    if (dev->rst_fd >= 0) {
        close(dev->rst_fd);
        dev->rst_fd = -1;
        int fd = open("/sys/class/gpio/unexport", O_WRONLY);
        if (fd >= 0) {
            dprintf(fd, "%d", dev->rst_pin);
            close(fd);
        }
    }

    if (dev->bl_fd >= 0) {
        close(dev->bl_fd);
        dev->bl_fd = -1;
        int fd = open("/sys/class/gpio/unexport", O_WRONLY);
        if (fd >= 0) {
            dprintf(fd, "%d", dev->bl_pin);
            close(fd);
        }
    }
}

// DMA初始化
static int dma_init(spi_lcd_dev_t *dev) {
    // 为DMA分配缓冲区 (4KB大小)
    dev->dma_buffer_size = 4096;
    dev->dma_buffer = malloc(dev->dma_buffer_size);
    if (!dev->dma_buffer) {
        perror("Failed to allocate DMA buffer");
        return -1;
    }
    
    // 初始化DMA传输结构体
    memset(&dev->dma_transfer, 0, sizeof(struct spi_ioc_transfer));
    dev->dma_transfer.speed_hz = dev->spi_speed;
    dev->dma_transfer.bits_per_word = 8;
    
    return 0;
}

// DMA释放
static void dma_deinit(spi_lcd_dev_t *dev) {
    if (dev->dma_buffer) {
        free(dev->dma_buffer);
        dev->dma_buffer = NULL;
        dev->dma_buffer_size = 0;
    }
}

// 复位LCD（sysfs方式）
static void lcd_reset(spi_lcd_dev_t *dev) {
    if (!dev || dev->rst_fd < 0) return;
    
    // 复位序列
    write(dev->rst_fd, "0", 1);  // 拉低复位
    usleep(10000);                // 延迟10ms
    write(dev->rst_fd, "1", 1);  // 释放复位
    usleep(120000);               // 延迟120ms
}

// 发送命令（sysfs方式）
static void lcd_send_cmd(spi_lcd_dev_t *dev, uint8_t cmd) {
    if (!dev || dev->dc_fd < 0 || dev->spi_fd < 0) return;
    
    // 设置为命令模式（DC=0）
    write(dev->dc_fd, "0", 1);
    
    // 发送命令
    write(dev->spi_fd, &cmd, 1);
}

// 发送单字节数据（sysfs方式）
static void lcd_send_data(spi_lcd_dev_t *dev, uint8_t data) {
    if (!dev || dev->dc_fd < 0 || dev->spi_fd < 0) return;
    
    // 设置为数据模式（DC=1）
    write(dev->dc_fd, "1", 1);
    
    // 发送数据
    write(dev->spi_fd, &data, 1);
}

// 批量发送数据（sysfs方式）
static void lcd_send_data_burst(spi_lcd_dev_t *dev, const uint8_t *data, size_t len) {
    if (!dev || !data || len == 0 || dev->dc_fd < 0 || dev->spi_fd < 0) return;
    
    // 设置为数据模式
    write(dev->dc_fd, "1", 1);
    
    // 大数据量使用DMA传输
    if (len > 128) {
        dev->dma_transfer.tx_buf = (unsigned long)data;
        dev->dma_transfer.len = len;
        
        if (ioctl(dev->spi_fd, SPI_IOC_MESSAGE(1), &dev->dma_transfer) < 0) {
            // DMA失败时回退到普通传输
            write(dev->spi_fd, data, len);
        }
    } else {
        // 小数据量直接发送
        write(dev->spi_fd, data, len);
    }
}

// 设置显示窗口
static void lcd_set_window2(spi_lcd_dev_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    if (!dev) return;
    
    switch (dev->lcd_type) {
        case LCD_TYPE_ILI9341:
        case LCD_TYPE_GC9307:  // GC9307与ILI9341窗口设置兼容
            // 设置列地址
            lcd_send_cmd(dev, 0x2A);
            lcd_send_data(dev, (x1 >> 8) & 0xFF);
            lcd_send_data(dev, x1 & 0xFF);
            lcd_send_data(dev, (x2 >> 8) & 0xFF);
            lcd_send_data(dev, x2 & 0xFF);
            
            // 设置行地址
            lcd_send_cmd(dev, 0x2B);
            lcd_send_data(dev, (y1 >> 8) & 0xFF);
            lcd_send_data(dev, y1 & 0xFF);
            lcd_send_data(dev, (y2 >> 8) & 0xFF);
            lcd_send_data(dev, y2 & 0xFF);
            
            // 准备写入
            lcd_send_cmd(dev, 0x2C);
            break;
            
        case LCD_TYPE_ST7789:
            // 设置列地址
            lcd_send_cmd(dev, 0x2A);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x1);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x2);
            
            // 设置行地址
            lcd_send_cmd(dev, 0x2B);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y1);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y2);
            
            // 准备写入
            lcd_send_cmd(dev, 0x2C);
            break;
            
        case LCD_TYPE_ST7735:
            // 设置列地址
            lcd_send_cmd(dev, 0x2A);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x1 + 0x02);  // ST7735有偏移
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x2 + 0x02);
            
            // 设置行地址
            lcd_send_cmd(dev, 0x2B);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y1 + 0x01);  // ST7735有偏移
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y2 + 0x01);
            
            // 准备写入
            lcd_send_cmd(dev, 0x2C);
            break;
    }
}

// 设置显示窗口
static void lcd_set_window(spi_lcd_dev_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    if (!dev) return;

    // 根据屏幕旋转方向添加偏移量（类似补丁中的34像素偏移）
    switch(dev->direction) {
        case LCD_DIR_DEFAULT:    // 0度
            x1 += 34;
            x2 += 34;
            // y方向不变
            break;
        case LCD_DIR_ROTATE_90:  // 90度
            // x方向不变
            y1 += 34;
            y2 += 34;
            break;
        case LCD_DIR_ROTATE_180: // 180度
            x1 += 34;
            x2 += 34;
            // y方向不变
            break;
        case LCD_DIR_ROTATE_270: // 270度
            // x方向不变
            y1 += 34;
            y2 += 34;
            break;
        default:
            break;
    }
    
    switch (dev->lcd_type) {
        case LCD_TYPE_ILI9341:
        case LCD_TYPE_GC9307:  // GC9307与ILI9341窗口设置兼容
            // 设置列地址
            lcd_send_cmd(dev, 0x2A);
            lcd_send_data(dev, (x1 >> 8) & 0xFF);
            lcd_send_data(dev, x1 & 0xFF);
            lcd_send_data(dev, (x2 >> 8) & 0xFF);
            lcd_send_data(dev, x2 & 0xFF);
            
            // 设置行地址
            lcd_send_cmd(dev, 0x2B);
            lcd_send_data(dev, (y1 >> 8) & 0xFF);
            lcd_send_data(dev, y1 & 0xFF);
            lcd_send_data(dev, (y2 >> 8) & 0xFF);
            lcd_send_data(dev, y2 & 0xFF);
            
            // 准备写入
            lcd_send_cmd(dev, 0x2C);
            break;
            
        case LCD_TYPE_ST7789:
            // 设置列地址
            lcd_send_cmd(dev, 0x2A);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x1);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x2);
            
            // 设置行地址
            lcd_send_cmd(dev, 0x2B);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y1);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y2);
            
            // 准备写入
            lcd_send_cmd(dev, 0x2C);
            break;
            
        case LCD_TYPE_ST7735:
            // 设置列地址
            lcd_send_cmd(dev, 0x2A);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x1 + 0x02);  // ST7735原有偏移
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, x2 + 0x02);
            
            // 设置行地址
            lcd_send_cmd(dev, 0x2B);
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y1 + 0x01);  // ST7735原有偏移
            lcd_send_data(dev, 0x00);
            lcd_send_data(dev, y2 + 0x01);
            
            // 准备写入
            lcd_send_cmd(dev, 0x2C);
            break;
    }
}



// 根据屏幕类型执行初始化
static void lcd_init_by_type(spi_lcd_dev_t *dev) {
    if (!dev) return;
    
    const uint8_t *cmd_ptr;
    size_t i = 0;
    
    switch (dev->lcd_type) {
        case LCD_TYPE_GC9307:


        
            cmd_ptr = gc9307_init_cmds;
            while (i < sizeof(gc9307_init_cmds)) {
                uint8_t cmd = cmd_ptr[i++];
                uint8_t len = cmd_ptr[i++];
                
                if (cmd == 0x00 && len > 0) {
                    // 延迟命令
                    usleep(len * 1000);
                } else {
                    // 发送命令
                    lcd_send_cmd(dev, cmd);
                    
                    // 发送命令参数
                    for (uint8_t j = 0; j < len; j++) {
                        lcd_send_data(dev, cmd_ptr[i++]);
                    }
                }
            }
            break;
            
        case LCD_TYPE_ILI9341:
            // ILI9341初始化命令集
            lcd_send_cmd(dev, 0x01); // 软件复位
            usleep(120000);
            
            lcd_send_cmd(dev, 0x11); // 退出睡眠
            usleep(120000);
            
            // 像素格式设置为RGB565
            lcd_send_cmd(dev, 0x3A);
            lcd_send_data(dev, 0x05);
            
            lcd_send_cmd(dev, 0x29); // 显示开启
            usleep(120000);
            break;
            
        case LCD_TYPE_ST7789:
            // ST7789初始化命令集
            lcd_send_cmd(dev, 0x01); // 软件复位
            usleep(120000);
            
            lcd_send_cmd(dev, 0x11); // 退出睡眠
            usleep(120000);
            
            // 像素格式设置为RGB565
            lcd_send_cmd(dev, 0x3A);
            lcd_send_data(dev, 0x05);
            
            lcd_send_cmd(dev, 0x29); // 显示开启
            usleep(120000);
            break;
            
        case LCD_TYPE_ST7735:
            // ST7735初始化命令集
            lcd_send_cmd(dev, 0x01); // 软件复位
            usleep(120000);
            
            lcd_send_cmd(dev, 0x11); // 退出睡眠
            usleep(120000);
            
            // 像素格式设置为RGB565
            lcd_send_cmd(dev, 0x3A);
            lcd_send_data(dev, 0x05);
            
            lcd_send_cmd(dev, 0x29); // 显示开启
            usleep(120000);
            break;
    }
}

// LVGL 9.3 初始化
void lvgl_init3(spi_lcd_dev_t *dev) {
    lv_init();
    
    // 修改为屏幕1/2或1/4大小（GC9307是240x320，1/2约38400像素）
    uint32_t buf_size = (dev->width * dev->height) / 2; 
    buf1 = malloc(buf_size * sizeof(lv_color_t));
    buf2 = malloc(buf_size * sizeof(lv_color_t));
    
    if(!buf1 || !buf2) {
        fprintf(stderr, "Failed to allocate LVGL buffers\n");
        return;
    }
    
    disp = lv_display_create(dev->width, dev->height);
    lv_display_set_flush_cb(disp, lvgl_port_flush);
    // 确保缓冲区大小参数正确（字节数）
    lv_display_set_buffers(disp, buf1, buf2, buf_size * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, dev);
}

// LVGL 9.3 显示刷新回调（关键修改：适配9.x API）
void lvgl_port_flush3(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    spi_lcd_dev_t *dev = lv_display_get_user_data(disp);
    if(!dev) return;

    uint16_t x1 = area->x1;
    uint16_t y1 = area->y1;
    uint16_t x2 = area->x2;
    uint16_t y2 = area->y2;

    lcd_set_window(dev, x1, y1, x2, y2);
    uint32_t pixel_count = (x2 - x1 + 1) * (y2 - y1 + 1);

    // 关键修改：交换每个像素的高低字节
    uint16_t *color_data = (uint16_t *)px_map;
    for(uint32_t i = 0; i < pixel_count; i++) {
        color_data[i] = __builtin_bswap16(color_data[i]); // 交换字节序
    }

    lcd_send_data_burst(dev, (uint8_t *)color_data, pixel_count * 2);
    lv_display_flush_ready(disp);
}



// spi_lcd_dma.c 中修改LVGL初始化和刷新部分
void lvgl_init(spi_lcd_dev_t *dev) {
    lv_init();
    
    // 调整缓冲区大小为屏幕的1/10，减少内存压力
    uint32_t buf_size = (dev->width * dev->height) / 10; 
    buf1 = malloc(buf_size * sizeof(lv_color_t));
    buf2 = malloc(buf_size * sizeof(lv_color_t));
    
    if(!buf1 || !buf2) {
        fprintf(stderr, "Failed to allocate LVGL buffers: %s\n", strerror(errno));
        return;
    }
    
    disp = lv_display_create(dev->width, dev->height);
    lv_display_set_flush_cb(disp, lvgl_port_flush);
    // 明确指定缓冲区大小（像素数），而不是字节数
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, dev);
    
    // 增加调试信息
    printf("LVGL initialized with buffers: %dx%d, buffer size: %d pixels\n",
           dev->width, dev->height, buf_size);
}

// 优化LVGL刷新回调函数
void lvgl_port_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    spi_lcd_dev_t *dev = lv_display_get_user_data(disp);
    if(!dev) return;

    uint16_t x1 = area->x1;
    uint16_t y1 = area->y1;
    uint16_t x2 = area->x2;
    uint16_t y2 = area->y2;

    // 增加调试信息
    printf("Flushing area: x1=%d, y1=%d, x2=%d, y2=%d\n", x1, y1, x2, y2);

    // 检查坐标是否在有效范围内
    if(x1 >= dev->width || y1 >= dev->height || x2 >= dev->width || y2 >= dev->height) {
        printf("Invalid flush area, skipping\n");
        lv_display_flush_ready(disp);
        return;
    }

    lcd_set_window(dev, x1, y1, x2, y2);
    uint32_t pixel_count = (x2 - x1 + 1) * (y2 - y1 + 1);

    // 根据屏幕类型调整颜色字节序（GC9307不需要交换）
    uint16_t *color_data = (uint16_t *)px_map;
    if(dev->lcd_type != LCD_TYPE_GC9307) {  // GC9307特殊处理
        for(uint32_t i = 0; i < pixel_count; i++) {
            color_data[i] = __builtin_bswap16(color_data[i]);
        }
    }

    // 确保DC引脚设置正确
    write(dev->dc_fd, "1", 1);
    
    // 分块发送数据，避免DMA缓冲区溢出
    size_t total_sent = 0;
    size_t bytes_per_pixel = 2;
    size_t max_chunk = dev->dma_buffer_size;
    
    while(total_sent < pixel_count * bytes_per_pixel) {
        size_t chunk_size = (pixel_count * bytes_per_pixel - total_sent) > max_chunk ? 
                           max_chunk : (pixel_count * bytes_per_pixel - total_sent);
        
        if(ioctl(dev->spi_fd, SPI_IOC_MESSAGE(1), &dev->dma_transfer) < 0) {
            // DMA失败时使用普通发送并输出错误
            perror("DMA transfer failed, falling back to normal transfer");
            write(dev->spi_fd, (uint8_t*)color_data + total_sent, chunk_size);
        }
        
        total_sent += chunk_size;
    }

    lv_display_flush_ready(disp);
}




// LVGL 系统时钟更新（9.x 兼容）
void lvgl_tick_inc(uint32_t ms) {
    lv_tick_inc(ms);
}
