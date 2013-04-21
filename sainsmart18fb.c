/*
 * FB driver for the Sainsmart 1.8" LCD display
 *
 * This display module want the color as BGR565
 * Some programs writes RGB565 regardless of what is said in info->var.[color].{offset,length}.
 * So conversion from RGB565 to BGR565 has to be done.
 *
 * Copyright (C) 2013 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "sainsmart18fb"
#define WIDTH       128
#define HEIGHT      160
#define TXBUFLEN	4*PAGE_SIZE


/* Module Parameter: debug  (also available through sysfs) */
MODULE_PARM_DEBUG;

// ftp://imall.iteadstudio.com/IM120419001_ITDB02_1.8SP/DS_ST7735.pdf
// https://github.com/johnmccombs/arduino-libraries/blob/master/ST7735/ST7735.cpp

static int sainsmart18fb_init_display(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_INIT_DISPLAY, par->info->device, "%s()\n", __func__);

	par->fbtftops.reset(par);

	/* SWRESET - Software reset */
	write_reg(par, 0x01);
	mdelay(150);

	/* SLPOUT - Sleep out & booster on */
	write_reg(par, 0x11);
	mdelay(500);

	/* FRMCTR1 - frame rate control - normal mode */
	write_reg(par, 0xB1, 0x01, 0x2C, 0x2D); /* frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D) */

	/* FRMCTR2 - frame rate control - idle mode */
	write_reg(par, 0xB2, 0x01, 0x2C, 0x2D); /* frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D) */

	/* FRMCTR1 - frame rate control - partial mode */
	write_reg(par, 0xB3, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D); /* dot inversion mode, line inversion mode */

	/* INVCTR - // display inversion control */
	write_reg(par, 0xB4, 0x07);	/* no inversion */

	/* PWCTR1 - Power Control */
	write_reg(par, 0xC0, 0xA2, 0x02, 0x84); /* -4.6V, AUTO mode */

	/* PWCTR2 - Power Control */
	write_reg(par, 0xC1, 0xC5);	/* VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD */

	/* PWCTR3 - Power Control */
	write_reg(par, 0xC2, 0x0A, 0x00); /* Opamp current small, Boost frequency */

	/* PWCTR4 - Power Control */
	write_reg(par, 0xC3, 0x8A, 0x2A); /* BCLK/2, Opamp current small & Medium low */

	/* PWCTR5 - Power Control */
	write_reg(par, 0xC4, 0x8A, 0xEE);

	/* VMCTR1 - Power Control */
	write_reg(par, 0xC5, 0x0E);

	/* INVOFF - Display inversion off */
	write_reg(par, 0x20);

	/* MADCTL - Memory data access control */
	write_reg(par, 0x36, 0xC8);	/* row address/col address, bottom to top refresh */

	/* COLMOD - Interface pixel format */
	write_reg(par, 0x3A, 0x05);

	/* GMCTRP1 - Gamma control */
	write_reg(par, 0xE0, 0x0f, 0x1a, 0x0f, 0x18, 0x2f, 0x28, 0x20, 0x22, 0x1f, 0x1b, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10);
/*	write_reg(par, 0xE0, 0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2b, 0x39, 0x00, 0x01, 0x03, 0x10); */
  
	/* GMCTRN1 - Gamma control */
	write_reg(par, 0xE1, 0x0f , 0x1b , 0x0f , 0x17 , 0x33 , 0x2c , 0x29 , 0x2e , 0x30 , 0x30 , 0x39 , 0x3f , 0x00 , 0x07 , 0x03 , 0x10); 
/*	write_reg(par, 0xE1, 0x03, 0x1d, 0x07, 0x06, 0x2e, 0x2c, 0x29, 0x2d, 0x2e, 0x2e, 0x37, 0x3f, 0x00, 0x00, 0x02, 0x10); */

	/* DISPON - Display On */
	write_reg(par, 0x29);
	mdelay(100);

	/* NORON - Partial off (Normal) */
	write_reg(par, 0x13);
	mdelay(10);

	return 0;
}

static int sainsmart18fb_write_vmem(struct fbtft_par *par)
{
	u16 *vmem16;
	u16 *txbuf16 = NULL;
    size_t remain;
	size_t to_copy;
	int i;
	int ret = 0;
	u16 val;
	unsigned red, green, blue;
	size_t offset, len;

	offset = par->dirty_lines_start * par->info->fix.line_length;
	len = (par->dirty_lines_end - par->dirty_lines_start + 1) * par->info->fix.line_length;
	remain = len;
	vmem16 = (u16 *)(par->info->screen_base + offset);

	fbtft_fbtft_dev_dbg(DEBUG_WRITE_VMEM, par, par->info->device, "%s: offset=%d, len=%d\n", __func__, offset, len);

	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, 1);

	// sanity check
	if (!par->txbuf.buf) {
		dev_err(par->info->device, "sainsmart18fb_write_vmem: txbuf.buf is needed to do conversion\n");
		return -1;
	}

	while (remain) {
		to_copy = remain > par->txbuf.len ? par->txbuf.len : remain;
		txbuf16 = (u16 *)par->txbuf.buf;
		dev_dbg(par->info->device, "    to_copy=%d, remain=%d\n", to_copy, remain - to_copy);
		for (i=0;i<to_copy;i+=2) {
			val = *vmem16++;

			// Convert to BGR565
			red   = (val >> par->info->var.red.offset)   & ((1<<par->info->var.red.length) - 1);
			green = (val >> par->info->var.green.offset) & ((1<<par->info->var.green.length) - 1);
			blue  = (val >> par->info->var.blue.offset)  & ((1<<par->info->var.blue.length) - 1);
			val  = (blue <<11) | (green <<5) | red;

#ifdef __LITTLE_ENDIAN
				*txbuf16++ = swab16(val);
#else
				*txbuf16++ = val;
#endif
		}
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}

	return ret;
}

static int sainsmart18fb_verify_gpios(struct fbtft_par *par)
{
	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par->info->device, "%s()\n", __func__);

	if (par->gpio.dc < 0) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

struct fbtft_display sainsmart18_display = {
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
};

static int __devinit sainsmart18fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	info = fbtft_framebuffer_alloc(&sainsmart18_display, &spi->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->spi = spi;
	fbtft_debug_init(par);
	par->fbtftops.init_display = sainsmart18fb_init_display;
	par->fbtftops.write_vmem = sainsmart18fb_write_vmem;
	par->fbtftops.verify_gpios = sainsmart18fb_verify_gpios;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int __devexit sainsmart18fb_remove(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	fbtft_dev_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, &spi->dev, "%s()\n", __func__);

	if (info) {
		fbtft_unregister_framebuffer(info);
		fbtft_framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver sainsmart18fb_driver = {
	.driver = {
		.name   = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe  = sainsmart18fb_probe,
	.remove = __devexit_p(sainsmart18fb_remove),
};

static int __init sainsmart18fb_init(void)
{
	fbtft_pr_debug("\n\n"DRVNAME": %s()\n", __func__);
	return spi_register_driver(&sainsmart18fb_driver);
}

static void __exit sainsmart18fb_exit(void)
{
	fbtft_pr_debug(DRVNAME": %s()\n", __func__);
	spi_unregister_driver(&sainsmart18fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(sainsmart18fb_init);
module_exit(sainsmart18fb_exit);

MODULE_DESCRIPTION("FB driver for the Sainsmart 1.8 inch LCD display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
