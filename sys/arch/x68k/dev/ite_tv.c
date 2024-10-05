/*	$NetBSD: ite_tv.c,v 1.21 2024/10/05 03:56:54 isaki Exp $	*/

/*
 * Copyright (c) 1997 Masaru Oki.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Masaru Oki.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ite_tv.c,v 1.21 2024/10/05 03:56:54 isaki Exp $");

#include "opt_ite.h"

#include <sys/param.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/grfioctl.h>

#include <arch/x68k/x68k/iodevice.h>
#include <arch/x68k/dev/itevar.h>
#include <arch/x68k/dev/grfvar.h>
#include <arch/x68k/dev/mfp.h>

/*
 * ITE device dependent routine for X680x0 Text-Video framebuffer.
 * Use X680x0 ROM fixed width font (8x16)
 */

#define CRTC    (IODEVbase->io_crtc)

/*
 * font constant
 */
#define FONTWIDTH   8
#define FONTHEIGHT  16
#define UNDERLINE   14

/*
 * framebuffer constant
 */
#define PLANEWIDTH  1024
#define PLANEHEIGHT 1024
#define PLANELINES  (PLANEHEIGHT / FONTHEIGHT)
#define ROWBYTES    (PLANEWIDTH  / FONTWIDTH)
#define PLANESIZE   (PLANEHEIGHT * ROWBYTES)

static u_int  tv_top;
static uint8_t *tv_row[PLANELINES];
#if defined(ITE_SIXEL)
static uint8_t *tv_end;
#endif
static uint8_t *tv_font[256];
static volatile uint8_t *tv_kfont[0x7f];

uint8_t kern_font[256 * FONTHEIGHT];

#define PHYSLINE(y)  ((tv_top + (y)) % PLANELINES)
#define ROWOFFSET(y) ((y) * FONTHEIGHT * ROWBYTES)
#define CHADDR(y, x) (tv_row[PHYSLINE(y)] + (x))

#define SETGLYPH(to,from)	\
	memcpy(&kern_font[(from) * 16],&kern_font[(to) * 16], 16)
#define KFONTBASE(left)   ((left) * 32 * 0x5e - 0x21 * 32)

/* prototype */
static void tv_putc(struct ite_softc *, int, int, int, int);
static void tv_cursor(struct ite_softc *, int);
static void tv_clear(struct ite_softc *, int, int, int, int);
static void tv_scroll(struct ite_softc *, int, int, int, int);
#if defined(ITE_SIXEL)
static void tv_sixel(struct ite_softc *, int, int);
#endif

static inline uint32_t expbits(uint32_t);
static inline void txrascpy(uint8_t, uint8_t, int16_t, uint16_t);

static inline void
txrascpy(uint8_t src, uint8_t dst, int16_t size, uint16_t mode)
{
	/*int s;*/
	uint16_t saved_r21 = CRTC.r21;
	int8_t d;

	d = ((mode & 0x8000) != 0) ? -1 : 1;
	src *= FONTHEIGHT / 4;
	dst *= FONTHEIGHT / 4;
	size *= 4;
	if (d < 0) {
		src += (FONTHEIGHT / 4) - 1;
		dst += (FONTHEIGHT / 4) - 1;
	}

	/* specify same time write mode & page */
	CRTC.r21 = (mode & 0x0f) | 0x0100;
	/*mfp.ddr = 0;*/			/* port is input */

	/*s = splhigh();*/
	while (--size >= 0) {
		/* wait for hsync */
		mfp_wait_for_hsync();
		CRTC.r22 = (src << 8) | dst;	/* specify raster number */
		/* start raster copy */
		CRTC.crtctrl = 0x0008;

		src += d;
		dst += d;
	}
	/*splx(s);*/

	/* wait for hsync */
	mfp_wait_for_hsync();

	/* stop raster copy */
	CRTC.crtctrl = 0x0000;

	CRTC.r21 = saved_r21;
}

/*
 * Change glyphs from SRAM switch.
 */
void
ite_set_glyph(void)
{
	uint8_t glyph = IODEVbase->io_sram[0x59];

	if ((glyph & 4) != 0)
		SETGLYPH(0x82, '|');
	if ((glyph & 2) != 0)
		SETGLYPH(0x81, '~');
	if ((glyph & 1) != 0)
		SETGLYPH(0x80, '\\');
}

/*
 * Initialize
 */
void
tv_init(struct ite_softc *ip)
{
	short i;

	/*
	 * initialize private variables
	 */
	tv_top = 0;
	for (i = 0; i < PLANELINES; i++)
		tv_row[i] =
		    (void *)__UNVOLATILE(&IODEVbase->tvram[ROWOFFSET(i)]);
#if defined(ITE_SIXEL)
	tv_end = (void *)__UNVOLATILE(&IODEVbase->tvram[ROWOFFSET(i)]);
#endif
	/* shadow ANK font */
	memcpy(kern_font, (void *)&IODEVbase->cgrom0_8x16, 256 * FONTHEIGHT);
	ite_set_glyph();
	/* set font address cache */
	for (i = 0; i < 256; i++)
		tv_font[i] = &kern_font[i * FONTHEIGHT];
	for (i = 0x21; i < 0x30; i++)
		tv_kfont[i] = &IODEVbase->cgrom0_16x16[KFONTBASE(i-0x21)];
	for (; i < 0x50; i++)
		tv_kfont[i] = &IODEVbase->cgrom1_16x16[KFONTBASE(i-0x30)];
	for (; i < 0x7f; i++)
		tv_kfont[i] = &IODEVbase->cgrom2_16x16[KFONTBASE(i-0x50)];

	/*
	 * initialize part of ip
	 */
	ip->cols = ip->grf->g_display.gd_dwidth  / FONTWIDTH;
	ip->rows = ip->grf->g_display.gd_dheight / FONTHEIGHT;
	/* set draw routine dynamically */
	ip->isw->ite_putc   = tv_putc;
	ip->isw->ite_cursor = tv_cursor;
	ip->isw->ite_clear  = tv_clear;
	ip->isw->ite_scroll = tv_scroll;
#if defined(ITE_SIXEL)
	ip->isw->ite_sixel  = tv_sixel;
#endif

	/*
	 * Initialize colormap
	 */
#define RED   (0x1f << 6)
#define BLUE  (0x1f << 1)
#define GREEN (0x1f << 11)
	IODEVbase->tpalet[0] = 0;			/* black */
	IODEVbase->tpalet[1] = 1 | RED;			/* red */
	IODEVbase->tpalet[2] = 1 | GREEN;		/* green */
	IODEVbase->tpalet[3] = 1 | RED | GREEN;		/* yellow */
	IODEVbase->tpalet[4] = 1 | BLUE;		/* blue */
	IODEVbase->tpalet[5] = 1 | BLUE | RED;		/* magenta */
	IODEVbase->tpalet[6] = 1 | BLUE | GREEN;	/* cyan */
	IODEVbase->tpalet[7] = 1 | BLUE | RED | GREEN;	/* white */
}

/*
 * Deinitialize
 */
void
tv_deinit(struct ite_softc *ip)
{

	ip->flags &= ~ITE_INITED; /* XXX? */
}

static inline uint8_t *tv_getfont(int, int);
typedef void tv_putcfunc(struct ite_softc *, int, char *);
static tv_putcfunc tv_putc_nm;
static tv_putcfunc tv_putc_in;
static tv_putcfunc tv_putc_ul;
static tv_putcfunc tv_putc_ul_in;
static tv_putcfunc tv_putc_bd;
static tv_putcfunc tv_putc_bd_in;
static tv_putcfunc tv_putc_bd_ul;
static tv_putcfunc tv_putc_bd_ul_in;

static tv_putcfunc *putc_func[ATTR_ALL + 1] = {
	[ATTR_NOR]					= tv_putc_nm,
	[ATTR_INV]					= tv_putc_in,
	[ATTR_UL]					= tv_putc_ul,
	[ATTR_INV | ATTR_UL]				= tv_putc_ul_in,
	[ATTR_BOLD]					= tv_putc_bd,
	[ATTR_BOLD | ATTR_INV]				= tv_putc_bd_in,
	[ATTR_BOLD | ATTR_UL]				= tv_putc_bd_ul,
	[ATTR_BOLD | ATTR_UL | ATTR_INV]		= tv_putc_bd_ul_in,
	/* no support for blink */
	[ATTR_BLINK]					= tv_putc_nm,
	[ATTR_BLINK | ATTR_INV]				= tv_putc_in,
	[ATTR_BLINK | ATTR_UL]				= tv_putc_ul,
	[ATTR_BLINK | ATTR_UL | ATTR_INV]		= tv_putc_ul_in,
	[ATTR_BLINK | ATTR_BOLD]			= tv_putc_bd,
	[ATTR_BLINK | ATTR_BOLD | ATTR_INV]		= tv_putc_bd_in,
	[ATTR_BLINK | ATTR_BOLD | ATTR_UL]		= tv_putc_bd_ul,
	[ATTR_BLINK | ATTR_BOLD | ATTR_UL | ATTR_INV]	= tv_putc_bd_ul_in,
};

/*
 * simple put character function
 */
static void
tv_putc(struct ite_softc *ip, int ch, int y, int x, int mode)
{
	uint8_t *p = CHADDR(y, x);
	short fh;

	/* multi page write mode */
	CRTC.r21 = 0x0100 | ip->fgcolor << 4;

	/* draw plane */
	putc_func[mode](ip, ch, p);

	/* erase plane */
	CRTC.r21 ^= 0x00f0;
	if (ip->save_char) {
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*(uint16_t *)p = 0;
	} else {
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*p = 0;
	}

	/* crtc mode reset */
	CRTC.r21 = 0;
}

static inline uint8_t *
tv_getfont(int cset, int ch)
{

	if (cset == CSET_JISKANA) {
		ch |= 0x80;
	} else if (cset == CSET_DECGRAPH) {
		if (ch < 0x80) {
			ch = ite_decgraph2ascii[ch];
		}
	}

	return tv_font[ch];
}

static void
tv_putc_nm(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*(uint16_t *)p = *kf++;
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
		*p = *f++;
}

static void
tv_putc_in(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*(uint16_t *)p = ~*kf++;
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
		*p = ~*f++;
}

static void
tv_putc_bd(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	u_int data;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
			data = *kf++;
			*(uint16_t *)p = data | (data >> 1);
		}
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
		data = *f++;
		*p = data | (data >> 1);
	}
}

static inline uint32_t
expbits(uint32_t data)
{
	int i;
	u_int nd = 0;

	if ((data & 1) != 0)
		nd |= 0x02;
	for (i = 1; i < 32; i++) {
		if ((data & (1 << i)) != 0)
			nd |= 0x5 << (i - 1);
	}
	nd &= ~data;
	return ~nd;
}

static void
tv_putc_ul(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES)
			*(uint16_t *)p = *kf++;
		*(uint16_t *)p = expbits(*kf++);
		p += ROWBYTES;
		for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*(uint16_t *)p = *kf++;
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES)
		*p = *f++;
	*p = expbits(*f++);
	p += ROWBYTES;
	for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES)
		*p = *f++;
}

static void
tv_putc_bd_in(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	u_int data;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
			data = *kf++;
			*(uint16_t *)p = ~(data | (data >> 1));
		}
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
		data = *f++;
		*p = ~(data | (data >> 1));
	}
}

static void
tv_putc_ul_in(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES)
			*(uint16_t *)p = ~*kf++;
		*(uint16_t *)p = ~expbits(*kf++);
		p += ROWBYTES;
		for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*(uint16_t *)p = ~*kf++;
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES)
		*p = ~*f++;
	*p = ~expbits(*f++);
	p += ROWBYTES;
	for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES)
		*p = ~*f++;
}

static void
tv_putc_bd_ul(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	u_int data;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES) {
			data = *kf++;
			*(uint16_t *)p = data | (data >> 1);
		}
		data = *kf++;
		*(uint16_t *)p = expbits(data | (data >> 1));
		p += ROWBYTES;
		for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
			data = *kf++;
			*(uint16_t *)p = data | (data >> 1);
		}
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES) {
		data = *f++;
		*p = data | (data >> 1);
	}
	data = *f++;
	*p = expbits(data | (data >> 1));
	p += ROWBYTES;
	for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
		data = *f++;
		*p = data | (data >> 1);
	}
}

static void
tv_putc_bd_ul_in(struct ite_softc *ip, int ch, char *p)
{
	short fh, hi, lo;
	u_int data;
	volatile uint16_t *kf;
	uint8_t *f;

	hi = ip->save_char & 0x7f;
	lo = ch & 0x7f;

	if (hi >= 0x21 && hi <= 0x7e && lo >= 0x21 && lo <= 0x7e) {
		/* multibyte character */
		kf = (volatile uint16_t *)tv_kfont[hi];
		kf += lo * FONTHEIGHT;
		/* draw plane */
		for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES) {
			data = *kf++;
			*(uint16_t *)p = ~(data | (data >> 1));
		}
		data = *kf++;
		*(uint16_t *)p = ~expbits(data | (data >> 1));
		p += ROWBYTES;
		for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
			data = *kf++;
			*(uint16_t *)p = ~(data | (data >> 1));
		}
		return;
	}

	/* singlebyte character */
	f = tv_getfont(*ip->GL, ch);

	/* draw plane */
	for (fh = 0; fh < UNDERLINE; fh++, p += ROWBYTES) {
		data = *f++;
		*p = ~(data | (data >> 1));
	}
	data = *f++;
	*p = ~expbits(data | (data >> 1));
	p += ROWBYTES;
	for (fh++; fh < FONTHEIGHT; fh++, p += ROWBYTES) {
		data = *f++;
		data |= data >> 1;
		*p = ~(data | (data >> 1));
	}
}

/*
 * draw/erase/move cursor
 */
static void
tv_cursor(struct ite_softc *ip, int flag)
{
	uint8_t *p;
	short fh;

	/* erase */
	switch (flag) {
	/*case DRAW_CURSOR:*/
	/*case ERASE_CURSOR:*/
	/*case MOVE_CURSOR:*/
	case START_CURSOROPT:
		/*
		 * old: ip->cursorx, ip->cursory
		 * new: ip->curx, ip->cury
		 */
		p = CHADDR(ip->cursory, ip->cursorx);
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*p = ~*p;
		break;
	}

	/* draw */
	switch (flag) {
	/*case MOVE_CURSOR:*/
	case END_CURSOROPT:
		/*
		 * Use exclusive-or.
		 */
		p = CHADDR(ip->cury, ip->curx);
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			*p = ~*p;

		ip->cursorx = ip->curx;
		ip->cursory = ip->cury;
		break;
	}
}

/*
 * clear rectangle
 */
static void
tv_clear(struct ite_softc *ip, int y, int x, int height, int width)
{
	uint8_t *p;
	short fh;

	/* XXX: reset scroll register on clearing whole screen */
	if (y == 0 && x == 0 && height == ip->rows && width == ip->cols) {
		CRTC.r10 = 0;
		CRTC.r11 = tv_top * FONTHEIGHT;
	}

	CRTC.r21 = 0x01f0;
	while (height--) {
		p = CHADDR(y++, x);
		for (fh = 0; fh < FONTHEIGHT; fh++, p += ROWBYTES)
			memset(p, 0, width);
	}
	/* crtc mode reset */
	CRTC.r21 = 0;
}

/*
 * scroll lines/columns
 */
static void
tv_scroll(struct ite_softc *ip, int srcy, int srcx, int count, int dir)
{
	int dst, siz, pl;

	switch (dir) {
	case SCROLL_UP:
		/*
		 * src: srcy
		 * dst: (srcy - count)
		 * siz: (ip->bottom_margin - sy + 1)
		 */
		dst = srcy - count;
		siz = ip->bottom_margin - srcy + 1;
		if (dst == 0 && ip->bottom_margin == ip->rows - 1) {
			/* special case, hardware scroll */
			tv_top = (tv_top + count) % PLANELINES;
			CRTC.r11 = tv_top * FONTHEIGHT;
		} else {
			srcy = PHYSLINE(srcy);
			dst = PHYSLINE(dst);
			txrascpy(srcy, dst, siz, 0x0f);
		}
		break;

	case SCROLL_DOWN:
		/*
		 * src: srcy
		 * dst: (srcy + count)
		 * siz: (ip->bottom_margin - dy + 1)
		 */
		dst = srcy + count;
		siz = ip->bottom_margin - dst + 1;
		if (srcy == 0 && ip->bottom_margin == ip->rows - 1) {
			/* special case, hardware scroll */
			tv_top = (tv_top + PLANELINES - count) % PLANELINES;
			CRTC.r11 = tv_top * FONTHEIGHT;
		} else {
			srcy = PHYSLINE(srcy) + siz - 1;
			dst = PHYSLINE(dst) + siz - 1;
			txrascpy(srcy, dst, siz, 0x0f | 0x8000);
		}
		break;

	case SCROLL_LEFT:
		for (pl = 0; pl < PLANESIZE * 4; pl += PLANESIZE) {
			short fh;
			uint8_t *src = CHADDR(srcy, srcx) + pl;
			uint8_t *dest = CHADDR(srcy, srcx - count) + pl;

			siz = ip->cols - srcx;
			for (fh = 0; fh < FONTHEIGHT; fh++) {
				memcpy(dest, src, siz);
				src += ROWBYTES;
				dest += ROWBYTES;
			}
		}
		break;

	case SCROLL_RIGHT:
		for (pl = 0; pl < PLANESIZE * 4; pl += PLANESIZE) {
			short fh;
			uint8_t *src = CHADDR(srcy, srcx) + pl;
			uint8_t *dest = CHADDR(srcy, srcx + count) + pl;

			siz = ip->cols - (srcx + count);
			for (fh = 0; fh < FONTHEIGHT; fh++) {
				memcpy(dest, src, siz);
				src += ROWBYTES;
				dest += ROWBYTES;
			}
		}
		break;
	}
}

#if defined(ITE_SIXEL)
/*
 * put SIXEL graphics
 */
void
tv_sixel(struct ite_softc *ip, int sy, int sx)
{
	uint8_t *p;
	int width;
	int y;
	int cx;
	int px;
	uint16_t data[3];
	uint8_t color;

	width = MIN(ip->decsixel_ph, MAX_SIXEL_WIDTH);
	width = MIN(width, PLANEWIDTH - sx * FONTWIDTH);

	p = CHADDR(sy, sx);
	p += ROWBYTES * ip->decsixel_y;
	/* boundary check */
	if (p < tv_row[0]) {
		p = tv_end + (p - tv_row[0]);
	}

	for (y = 0; y < 6; y++) {
		/* for each 16dot word */
		for (cx = 0; cx < howmany(width, 16); cx++) {
			data[0] = 0;
			data[1] = 0;
			data[2] = 0;
			for (px = 0; px < 16; px++) {
				color = ip->decsixel_buf[cx * 16 + px] >> (y * 4);
				/* x68k console is 8 colors */
				data[0] = (data[0] << 1) | ((color >> 0) & 1);
				data[1] = (data[1] << 1) | ((color >> 1) & 1);
				data[2] = (data[2] << 1) | ((color >> 2) & 1);
			}
			*(uint16_t *)(p + cx * 2          ) = data[0];
			*(uint16_t *)(p + cx * 2 + 0x20000) = data[1];
			*(uint16_t *)(p + cx * 2 + 0x40000) = data[2];
		}

		p += ROWBYTES;
		if (p >= tv_end) {
			p = tv_row[0] + (p - tv_end);
		}
	}
}
#endif /* ITE_SIXEL */
