/* 	$NetBSD: rasops.h,v 1.51 2025/07/25 18:19:12 martin Exp $ */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _RASOPS_H_
#define _RASOPS_H_ 1

#include <sys/param.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wsfont/wsfont.h>

/* For rasops_info::ri_flg */
#define RI_FULLCLEAR	0x01	/* eraserows() hack to clear full screen */
#define RI_FORCEMONO	0x02	/* monochrome output even if we can do color */
#define RI_BSWAP	0x04	/* framebuffer endianness doesn't match CPU */
#define RI_CURSOR	0x08	/* cursor is switched on */
#define RI_CLEAR	0x10	/* clear display on startup */
#define RI_CENTER	0x20	/* center onscreen output */
#define RI_CURSORCLIP	0x40	/* cursor is currently clipped */
#define RI_CFGDONE	0x80	/* rasops_reconfig() completed successfully */
#define RI_ROTATE_CW	0x100	/* display is rotated, quarter clockwise */
#define RI_ROTATE_CCW	0x200	/* display is rotated, quarter counter-clockwise */
#define RI_ROTATE_UD	0x400	/* display is rotated, upside-down */
#define RI_ROTATE_MASK	0x700
/*
 * if you call rasops_init() or rasops_reconfig() in a context where it is not
 * safe to call kmem_alloc(), like early on during kernel startup, you MUST set
 * RI_NO_AUTO to keep rasops from trying to allocate memory for autogenerated
 * box drawing characters
 */
#define	RI_NO_AUTO	0x800	/* do not generate box drawing characters */
/*
 * Set this if your driver's putchar() method supports anti-aliased fonts in
 * the given video mode. Without this flag rasops_init() will only ever pick
 * monochrome bitmap fonts. 
 */
#define RI_ENABLE_ALPHA	0x1000
/* set this in order to use r3g3b2 'true' colour in 8 bit */ 
#define RI_8BIT_IS_RGB	0x2000
/*
 * drivers can set this to tell the font selection code that they'd rather
 * use alpha fonts
 */ 
#define RI_PREFER_ALPHA	0x4000
/*
 * Set this to prefer a wider font.
 */
#define RI_PREFER_WIDEFONT	0x8000

struct rasops_info {
	/* These must be filled in by the caller */
	int	ri_depth;	/* depth in bits */
	uint8_t	*ri_bits;	/* ptr to bits */
	int	ri_width;	/* width (pels) */
	int	ri_height;	/* height (pels) */
	int	ri_stride;	/* stride in bytes */

	/*
	 * If you want shadow framebuffer support, point ri_hwbits
	 * to the real framebuffer, and ri_bits to the shadow framebuffer
	 */
	uint8_t	*ri_hwbits;

	/*
	 * These can optionally be left zeroed out. If you fill ri_font,
	 * but aren't using wsfont, set ri_wsfcookie to -1.
	 */
	struct	wsdisplay_font *ri_font;
	struct	wsdisplay_font ri_optfont;
	int	ri_wsfcookie;	/* wsfont cookie */
	void	*ri_hw;		/* driver private data; ignored by rasops */
	int	ri_crow;	/* cursor row */
	int	ri_ccol;	/* cursor column */
	int	ri_flg;		/* various operational flags */

	/*
	 * These are optional and will default if zero. Meaningless
	 * on depths other than 15, 16, 24 and 32 bits per pel. On
	 * 24 bit displays, ri_{r,g,b}num must be 8.
	 */
	uint8_t	ri_rnum;	/* number of bits for red */
	uint8_t	ri_gnum;	/* number of bits for green */
	uint8_t	ri_bnum;	/* number of bits for blue */
	uint8_t	ri_rpos;	/* which bit red starts at */
	uint8_t	ri_gpos;	/* which bit green starts at */
	uint8_t	ri_bpos;	/* which bit blue starts at */

	/* These are filled in by rasops_init() */
	int	ri_emuwidth;	/* width we actually care about */
	int	ri_emuheight;	/* height we actually care about */
	int	ri_emustride;	/* bytes per row we actually care about */
	int	ri_rows;	/* number of rows (characters, not pels) */
	int	ri_cols;	/* number of columns (characters, not pels) */
#if __NetBSD_Prereq__(9, 99, 1)
	struct {
		int	off;	/* offset of underline from bottom */
		int	height;	/* height of underline */
	} ri_ul;
#else
	/*
	 * XXX
	 * hack to keep ABI compatibility for netbsd-9, -8, and -7.
	 */
	// int	ri_delta;	/* obsoleted */
	struct {
		short	off;
		short	height;
	} __packed ri_ul;
#endif
	int	ri_pelbytes;	/* bytes per pel (may be zero) */
	int	ri_fontscale;	/* fontheight * fontstride */
	int	ri_xscale;	/* fontwidth * pelbytes */
	int	ri_yscale;	/* fontheight * stride */
	uint8_t	*ri_origbits;	/* where screen bits actually start */
	uint8_t *ri_hworigbits;	/* where hw bits actually start */
	int	ri_xorigin;	/* where ri_bits begins (x) */
	int	ri_yorigin;	/* where ri_bits begins (y) */
	uint32_t
		ri_devcmap[16]; /* color -> framebuffer data */

	/* The emulops you need to use, and the screen caps for wscons */
	struct	wsdisplay_emulops ri_ops;
	int	ri_caps;

	/* Callbacks so we can share some code */
	void	(*ri_do_cursor)(struct rasops_info *);

	/* Used to intercept putchar to permit display rotation */
	struct	wsdisplay_emulops ri_real_ops;
};

#define CHAR_IN_FONT(c, font)						\
	((c) >= (font)->firstchar &&					\
	    (c) - (font)->firstchar < (font)->numchars)

#define PICK_FONT(ri, c)						\
	((((c) & WSFONT_FLAGS_MASK) == WSFONT_FLAG_OPT &&		\
	    (ri)->ri_optfont.data != NULL) ?				\
		&(ri)->ri_optfont : (ri)->ri_font)

/*
 * rasops_init().
 *
 * Integer parameters are the number of rows and columns we'd *like*.
 *
 * In terms of optimization, fonts that are a multiple of 8 pixels wide
 * work the best.
 *
 * rasops_init() takes care of rasops_reconfig(). The parameters to both
 * are the same. If calling rasops_reconfig() to change the font and
 * ri_wsfcookie >= 0, you must call wsfont_unlock() on it, and reset it
 * to -1 (or a new, valid cookie).
 */

/* rasops.c */
int	rasops_init(struct rasops_info *, int, int);
int	rasops_reconfig(struct rasops_info *, int, int);
void	rasops_unpack_attr(long, int *, int *, int *);
void	rasops_eraserows(void *, int, int, long);
void	rasops_erasecols(void *, int, int, int, long);
int	rasops_get_cmap(struct rasops_info *, uint8_t *, size_t);

extern const uint8_t	rasops_cmap[256 * 3];

#ifdef _RASOPS_PRIVATE
/*
 * Per-depth initialization functions.
 */
void	rasops1_init(struct rasops_info *);
void	rasops2_init(struct rasops_info *);
void	rasops4_init(struct rasops_info *);
void	rasops8_init(struct rasops_info *);
void	rasops15_init(struct rasops_info *);
void	rasops24_init(struct rasops_info *);
void	rasops32_init(struct rasops_info *);

#define	ATTR_BG(ri, attr) ((ri)->ri_devcmap[((uint32_t)(attr) >> 16) & 0xf])
#define	ATTR_FG(ri, attr) ((ri)->ri_devcmap[((uint32_t)(attr) >> 24) & 0xf])

#define	ATTR_MASK_BG __BITS(16, 19)
#define	ATTR_MASK_FG __BITS(24, 27)

#define	DELTA(p, d, cast) ((p) = (cast)((uint8_t *)(p) + (d)))

#define	FBOFFSET(ri, row, col)						\
	((row) * (ri)->ri_yscale + (col) * (ri)->ri_xscale)

#define	FONT_GLYPH(uc, font, ri)					\
	((uint8_t *)(font)->data + ((uc) - ((font)->firstchar)) *	\
	    (ri)->ri_fontscale)

static __inline void
rasops_memset32(void *p, uint32_t val, size_t bytes)
{
	int slop1, slop2, full;
	uint8_t *dp = (uint8_t *)p;

	if (bytes == 1) {
		*dp = val;
		return;
	}

	slop1 = (4 - ((uintptr_t)dp & 3)) & 3;
	slop2 = (bytes - slop1) & 3;
	full = (bytes - slop1 /* - slop2 */) >> 2;

	if (slop1 & 1)
		*dp++ = val;

	if (slop1 & 2) {
		*(uint16_t *)dp = val;
		dp += 2;
	}

	for (; full; full--) {
		*(uint32_t *)dp = val;
		dp += 4;
	}

	if (slop2 & 2) {
		*(uint16_t *)dp = val;
		dp += 2;
	}

	if (slop2 & 1)
		*dp = val;

	return;
}

static __inline uint32_t
rasops_be32uatoh(uint8_t *p)
{
	uint32_t u;

	u  = p[0]; u <<= 8;
	u |= p[1]; u <<= 8;
	u |= p[2]; u <<= 8;
	u |= p[3];
	return u;
}
#endif /* _RASOPS_PRIVATE */

#endif /* _RASOPS_H_ */
