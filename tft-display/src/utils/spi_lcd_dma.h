#ifndef SPI_LCD_DMA_H
#define SPI_LCD_DMA_H

#include <stdint.h>
#include <stddef.h>
#include <linux/spi/spidev.h>

// 显示方向枚举
typedef enum {
    LCD_DIR_DEFAULT,    // 默认方向
    LCD_DIR_ROTATE_90,  // 旋转90度
    LCD_DIR_ROTATE_180, // 旋转180度
    LCD_DIR_ROTATE_270  // 旋转270度
} lcd_dir_t;

// 屏幕类型枚举
typedef enum {
    LCD_TYPE_ILI9341,
    LCD_TYPE_ST7789,
    LCD_TYPE_ST7735,
    LCD_TYPE_GC9307
} lcd_type_t;

// 颜色定义 (RGB565)
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_GRAY        0x8410

// SPI LCD设备结构体
typedef struct {
    lcd_dir_t direction;
    uint16_t actual_width;
    uint16_t actual_height;
    
    // SPI配置
    const char *spi_dev_path;
    uint32_t spi_speed;
    uint8_t spi_mode;
    int spi_fd;
    
    // 屏幕参数
    lcd_type_t lcd_type;
    uint16_t width;
    uint16_t height;
    
    // GPIO配置
    int dc_pin;
    int rst_pin;
    int bl_pin;
    int dc_fd;
    int rst_fd;
    int bl_fd;
    
    // DMA传输相关
    struct spi_ioc_transfer dma_transfer;
    uint8_t *dma_buffer;
    size_t dma_buffer_size;
} spi_lcd_dev_t;

// 函数声明
int spi_lcd_init(spi_lcd_dev_t *dev);
void spi_lcd_deinit(spi_lcd_dev_t *dev);
int spi_lcd_set_brightness(spi_lcd_dev_t *dev, uint8_t brightness);

// 绘图函数
void spi_lcd_clear(spi_lcd_dev_t *dev, uint16_t color);
void spi_lcd_draw_pixel(spi_lcd_dev_t *dev, uint16_t x, uint16_t y, uint16_t color);
void spi_lcd_fill_rect(spi_lcd_dev_t *dev, uint16_t x1, uint16_t y1, 
                      uint16_t x2, uint16_t y2, uint16_t color);

// LVGL 9.3 支持函数
#include "../lvgl/lvgl.h"
void lvgl_init(spi_lcd_dev_t *dev);
void lvgl_port_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void lvgl_tick_inc(uint32_t ms);

#endif // SPI_LCD_DMA_H
