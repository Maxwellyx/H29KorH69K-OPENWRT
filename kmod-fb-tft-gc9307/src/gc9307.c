// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the GC9307 LCD Controller (172x320)
 * 
 * Copyright (C) 2025 Fish
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME "gc9307"

#define MADCTL_MY BIT(7)
#define MADCTL_MX BIT(6)
#define MADCTL_MV BIT(5)
#define MADCTL_BGR BIT(3)

static int init_display(struct fbtft_par *par)
{
    par->fbtftops.reset(par);

    write_reg(par, 0xfe);
    write_reg(par, 0xef);

    write_reg(par, 0x3a, 0x05); // 16bit/pixel (RGB565)

    write_reg(par, 0x86, 0x98);
    write_reg(par, 0x89, 0x03);
    write_reg(par, 0x8b, 0x80);
    write_reg(par, 0x8d, 0x33);
    write_reg(par, 0x8e, 0x0f);

    write_reg(par, 0xe8, 0x12, 0x00);

    write_reg(par, 0xc3, 0x31);
    write_reg(par, 0xc4, 0x00);
    write_reg(par, 0xc9, 0x0f);

    write_reg(par, 0xff, 0x62);

    write_reg(par, 0x99, 0x3e);
    write_reg(par, 0x9d, 0x4b);
    write_reg(par, 0x98, 0x3e);
    write_reg(par, 0x9c, 0x4b);

    write_reg(par, 0xF0, 0x8a, 0x0c, 0x09, 0x08, 0x05, 0x2f);
    write_reg(par, 0xF2, 0x8a, 0x0c, 0x09, 0x08, 0x06, 0x2f);
    write_reg(par, 0xF1, 0x49, 0x8f, 0x6f, 0x2c, 0x31, 0xff);
    write_reg(par, 0xF3, 0x46, 0x7f, 0x5f, 0x17, 0x19, 0xff);

    write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE);
    mdelay(120);
    write_reg(par, MIPI_DCS_SET_DISPLAY_ON);
    mdelay(100);

    return 0;
}

static int set_var(struct fbtft_par *par)
{
    u8 madctl = 0;

    if (par->bgr)
        madctl |= MADCTL_BGR;

    /* ⚠️ 这张表是老树（LEDE-Develop，屏幕显示正常的那版）里验证过的取值。
     * 早先我们用的是另一份驱动，270° 的 MADCTL 少了 MY 位（0x68 而不是 0xE8），
     * MY 管 Y 轴镜像 ⇒ 画面上下翻（h29k 上表现为“正反面”）。
     * 这些常量已经把 BGR 位（0x08）包含在内，所以下面直接赋值、不要再 OR。 */
    switch (par->info->var.rotate) {
    case 0:
        madctl = 0x48;  /* MV  | BGR           竖屏正向 */
        break;
    case 90:
        madctl = 0x28;  /* MX  | BGR           横屏右转 */
        break;
    case 180:
        madctl = 0x88;  /* MY  | BGR           180° */
        break;
    case 270:
        madctl = 0xE8;  /* MX | MY | MV | BGR  横屏左转（H29K 用的就是这档） */
        break;
    default:
        return -EINVAL;
    }

    write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, madctl);
    return 0;
}

static int blank(struct fbtft_par *par, bool on)
{
    if (on) {
        write_reg(par, 0x28); // display off
        mdelay(120);
        write_reg(par, 0x10); // enter sleep
    } else {
        write_reg(par, 0x11); // sleep out
        mdelay(120);
        write_reg(par, 0x29); // display on
    }
    return 0;
}

/*
 * GC9307 的 GRAM 可视区带偏移：老树里是靠内核补丁（9999-daocaoren-fbtft-
 * adjust-display-offset-for-rotation）改 fbtft-core 的 fbtft_set_addr_win() 实现的。
 * 我们不改内核 —— fbtft 的 fbtftops.set_addr_win 本来就是驱动可覆盖的钩子，
 * 所以在外置驱动里自己实现一份同样的偏移即可（rotate 0/180 加 X，90/270 加 Y）。
 */
static void gc9307_set_addr_win(struct fbtft_par *par, int xs, int ys, int xe,
                                int ye)
{
    switch (par->info->var.rotate) {
    case 0:
    case 180:
        xs += 34; xe += 34;
        break;
    case 90:
    case 270:
        ys += 34; ye += 34;
        break;
    default:
        break;
    }

    write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
              (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);
    write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
              (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);
    write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

static struct fbtft_display display = {
    .regwidth = 8,
    .width = 172,
    .height = 320,
    .gamma_num = 0,
    .gamma_len = 0,
    .gamma = NULL,
    .fbtftops = {
    	.set_addr_win = gc9307_set_addr_win,
        .init_display = init_display,
        .set_var = set_var,
        .blank = blank,
    },
};

/* SPI driver registration functions */
static int gc9307_probe_spi(struct spi_device *spi)
{
    return fbtft_probe_common(&display, spi, NULL);
}

static void gc9307_remove_spi(struct spi_device *spi)
{
    struct fb_info *info = spi_get_drvdata(spi);
    
    fbtft_remove_common(&spi->dev, info);
}

static const struct of_device_id gc9307_of_match[] = {
    { .compatible = "gc9307" },
    { .compatible = "sitronix,gc9307" },
    { },
};
MODULE_DEVICE_TABLE(of, gc9307_of_match);

static const struct spi_device_id gc9307_spi_ids[] = {
    { "gc9307", 0 },
    { "sitronix,gc9307", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, gc9307_spi_ids);

static struct spi_driver gc9307_spi_driver = {
    .driver = {
        .name   = DRVNAME,
        .owner  = THIS_MODULE,
        .of_match_table = gc9307_of_match,
    },
    .id_table = gc9307_spi_ids,
    .probe  = gc9307_probe_spi,
    .remove = gc9307_remove_spi,
};

module_spi_driver(gc9307_spi_driver);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_DESCRIPTION("FB driver for GC9307 LCD Controller (172x320)");
MODULE_AUTHOR("Fish");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");    