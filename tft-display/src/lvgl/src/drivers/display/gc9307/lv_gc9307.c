/**
 * @file lv_gc9307.c
 *
 * GC9307 LCD controller driver implementation for LVGL
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_gc9307.h"

#if LV_USE_GC9307

/*********************
 *      DEFINES
 *********************/

/* GC9307 specific commands */
#define GC9307_SWRESET     0x01    /* Software reset */
#define GC9307_SLPOUT      0x11    /* Sleep out */
#define GC9307_DISPON      0x29    /* Display on */
#define GC9307_CASET       0x2A    /* Column address set */
#define GC9307_RASET       0x2B    /* Row address set */
#define GC9307_RAMWR       0x2C    /* Memory write */
#define GC9307_MADCTL      0x36    /* Memory data access control */
#define GC9307_COLMOD      0x3A    /* Interface pixel format */
#define GC9307_PWCTR1      0xC0    /* Power control 1 */
#define GC9307_PWCTR2      0xC1    /* Power control 2 */
#define GC9307_VMCTR1      0xC5    /* VCOM control 1 */
#define GC9307_VMCTR2      0xC7    /* VCOM control 2 */
#define GC9307_PVGAMCTRL   0xE0    /* Positive gamma correction */
#define GC9307_NVGAMCTRL   0xE1    /* Negative gamma correction */

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC CONSTANTS
 **********************/

/* GC9307 initialization command sequence (adjust based on actual panel specs) */
static const uint8_t init_cmd_list[] = {
    GC9307_SWRESET, 0, LV_LCD_CMD_DELAY_MS, 120,  /* Software reset + delay */
    GC9307_SLPOUT, 0, LV_LCD_CMD_DELAY_MS, 120,   /* Sleep out + delay */
    
    /* Memory data access control (adjust based on screen orientation) */
    GC9307_MADCTL, 1, 0x08,                       /* RGB order, refresh direction */
    
    /* Set pixel format to RGB565 */
    GC9307_COLMOD, 1, 0x05,
    
    /* Power control settings */
    GC9307_PWCTR1, 2, 0x0A, 0x00,
    GC9307_PWCTR2, 1, 0x0A,
    
    /* VCOM control */
    GC9307_VMCTR1, 2, 0x50, 0x5B,
    GC9307_VMCTR2, 1, 0x40,
    
    /* Gamma correction */
    GC9307_PVGAMCTRL, 15, 0x00,0x0C,0x11,0x04,0x11,0x08,0x37,0x89,0x4C,0x06,0x0C,0x0A,0x2E,0x34,0x0F,
    GC9307_NVGAMCTRL, 15, 0x00,0x0B,0x11,0x05,0x13,0x09,0x33,0x67,0x48,0x07,0x0E,0x0B,0x2E,0x33,0x0F,
    
    GC9307_DISPON, 0, LV_LCD_CMD_DELAY_MS, 100,   /* Display on + delay */
    LV_LCD_CMD_EOF                                /* End of command list */
};

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_display_t * lv_gc9307_create(uint32_t hor_res, uint32_t ver_res, lv_lcd_flag_t flags,
                                lv_gc9307_send_cmd_cb_t send_cmd_cb, lv_gc9307_send_color_cb_t send_color_cb)
{
    lv_display_t * disp = lv_lcd_generic_mipi_create(hor_res, ver_res, flags, send_cmd_cb, send_color_cb);
    lv_lcd_generic_mipi_send_cmd_list(disp, init_cmd_list);
    return disp;
}

void lv_gc9307_set_gap(lv_display_t * disp, uint16_t x, uint16_t y)
{
    lv_lcd_generic_mipi_set_gap(disp, x, y);
}

void lv_gc9307_set_invert(lv_display_t * disp, bool invert)
{
    lv_lcd_generic_mipi_set_invert(disp, invert);
}

void lv_gc9307_set_gamma_curve(lv_display_t * disp, uint8_t gamma)
{
    lv_lcd_generic_mipi_set_gamma_curve(disp, gamma);
}

void lv_gc9307_send_cmd_list(lv_display_t * disp, const uint8_t * cmd_list)
{
    lv_lcd_generic_mipi_send_cmd_list(disp, cmd_list);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /*LV_USE_GC9307*/
