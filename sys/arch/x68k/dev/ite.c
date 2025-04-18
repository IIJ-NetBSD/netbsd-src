/*	$NetBSD: ite.c,v 1.72 2024/10/05 03:56:54 isaki Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Utah $Hdr: ite.c 1.1 90/07/09$
 *
 *	@(#)ite.c	7.6 (Berkeley) 5/16/91
 */

/*
 * ite - bitmaped terminal.
 * Supports VT200, a few terminal features will be unavailable until
 * the system actually probes the device (i.e. not after consinit())
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ite.c,v 1.72 2024/10/05 03:56:54 isaki Exp $");

#include "ite.h"
#if NITE > 0

#include "bell.h"
#include "kbd.h"

#include "opt_ite.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/kauth.h>
#include <sys/device_impl.h>	/* XXX autoconf abuse */

#include <machine/cpu.h>
#include <machine/kbio.h>
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/grfioctl.h>
#include <machine/iteioctl.h>

#include <arch/x68k/dev/grfvar.h>
#include <arch/x68k/dev/itevar.h>
#include <arch/x68k/dev/kbdmap.h>
#include <arch/x68k/dev/mfp.h>

#include "ioconf.h"

#if NBELL > 0
void opm_bell(void);
#endif

#define SUBR_INIT(ip)		ip->isw->ite_init(ip)
#define SUBR_DEINIT(ip)		ip->isw->ite_deinit(ip)
#define SUBR_PUTC(ip,c,dy,dx,m)	ip->isw->ite_putc(ip,c,dy,dx,m)
#define SUBR_CURSOR(ip,flg)	ip->isw->ite_cursor(ip,flg)
#define SUBR_CLEAR(ip,sy,sx,h,w)	ip->isw->ite_clear(ip,sy,sx,h,w)
#define SUBR_SCROLL(ip,sy,sx,count,dir)	\
    ip->isw->ite_scroll(ip,sy,sx,count,dir)
#if defined(ITE_SIXEL)
#define SUBR_SIXEL(ip,sy,sx)	ip->isw->ite_sixel(ip,sy,sx)
#endif

struct consdev;

static inline void itesendch(int);
static inline void alignment_display(struct ite_softc *);
static inline void snap_cury(struct ite_softc *);
static inline void ite_dnchar(struct ite_softc *, int);
static void ite_inchar(struct ite_softc *,	int);
static inline void ite_clrtoeol(struct ite_softc *);
static inline void ite_clrtobol(struct ite_softc *);
static inline void ite_clrline(struct ite_softc *);
static inline void ite_clrtoeos(struct ite_softc *);
static inline void ite_clrtobos(struct ite_softc *);
static inline void ite_clrscreen(struct ite_softc *);
static inline void ite_dnline(struct ite_softc *, int);
static inline void ite_inline(struct ite_softc *, int);
static inline void ite_index(struct ite_softc *);
static inline void ite_lf(struct ite_softc *);
static inline void ite_crlf(struct ite_softc *);
static inline void ite_cr(struct ite_softc *);
static inline void ite_rlf(struct ite_softc *);
static void iteprecheckwrap(struct ite_softc *);
static void itecheckwrap(struct ite_softc *);
static int ite_argnum(struct ite_softc *);
static int ite_zargnum(struct ite_softc *);
static void ite_sendstr(struct ite_softc *, const char *);
static inline int atoi(const char *);
static struct ite_softc *getitesp(dev_t);

static struct itesw itesw[] = {
	{0,	tv_init,	tv_deinit,	0,
	 0,	0,		0}
};

/*
 * # of chars are output in a single itestart() call.
 * If this is too big, user processes will be blocked out for
 * long periods of time while we are emptying the queue in itestart().
 * If it is too small, console output will be very ragged.
 */
#define ITEBURST 64

struct	tty *ite_tty[NITE];

static struct ite_softc *kbd_ite = NULL;
static struct ite_softc con_itesoftc;
static struct device con_itedev;

static struct tty *kbd_tty = NULL;

static int start_repeat_timeo = 20;	/* /100: initial timeout till pressed
						 key repeats */
static int next_repeat_timeo  = 3;	/* /100: timeout when repeating for
						 next char */

static u_char cons_tabs[MAX_TABS];

static void itestart(struct tty *);

static void iteputchar(int, struct ite_softc *);
#if defined(ITE_SIXEL)
static int ite_dcs(const int, struct ite_softc *);
#endif
static void ite_putstr(const u_char *, int, dev_t);

static int itematch(device_t, cfdata_t, void *);
static void iteattach(device_t, device_t, void *);

CFATTACH_DECL_NEW(ite, sizeof(struct ite_softc),
    itematch, iteattach, NULL, NULL);

static dev_type_open(iteopen);
static dev_type_close(iteclose);
static dev_type_read(iteread);
static dev_type_write(itewrite);
static dev_type_ioctl(iteioctl);
static dev_type_tty(itetty);
static dev_type_poll(itepoll);

const struct cdevsw ite_cdevsw = {
	.d_open = iteopen,
	.d_close = iteclose,
	.d_read = iteread,
	.d_write = itewrite,
	.d_ioctl = iteioctl,
	.d_stop = nostop,
	.d_tty = itetty,
	.d_poll = itepoll,
	.d_mmap = nommap,
	.d_kqfilter = ttykqfilter,
	.d_discard = nodiscard,
	.d_flag = D_TTY
};

static int
itematch(device_t parent, cfdata_t cf, void *aux)
{
	struct grf_softc *gp;

	gp = aux;
	if (cf->cf_loc[GRFCF_GRFADDR] != gp->g_cfaddr)
		return 0;

	return 1;
}

/*
 * iteinit() is the standard entry point for initialization of
 * an ite device, it is also called from ite_cninit().
 */
static void
iteattach(device_t parent, device_t self, void *aux)
{
	struct ite_softc *ip;
	struct grf_softc *gp;

	gp = aux;
	ip = device_private(self);
	ip->device = self;
	if(con_itesoftc.grf != NULL
		/*&& con_itesoftc.grf->g_unit == gp->g_unit*/) {
		/*
		 * console reinit copy params over.
		 * and console always gets keyboard
		 */
		memcpy(&ip->grf, &con_itesoftc.grf,
		    (char *)&ip[1] - (char *)&ip->grf);
		con_itesoftc.grf = NULL;
		kbd_ite = ip;
	}
	ip->grf = gp;
	iteinit(device_unit(self)); /* XXX */
	aprint_normal(": rows %d cols %d", ip->rows, ip->cols);
	if (kbd_ite == NULL)
		kbd_ite = ip;
	aprint_normal("\n");
}

static struct ite_softc *
getitesp(dev_t dev)
{

	if (x68k_realconfig && con_itesoftc.grf == NULL)
		return device_lookup_private(&ite_cd, UNIT(dev));

	if (con_itesoftc.grf == NULL)
		panic("no ite_softc for console");
	return &con_itesoftc;
}

void
iteinit(dev_t dev)
{
	struct ite_softc *ip;

	ip = getitesp(dev);

	if (ip->flags & ITE_INITED)
		return;
	memcpy(&kbdmap, &ascii_kbdmap, sizeof(struct kbdmap));

	ip->curx = 0;
	ip->cury = 0;
	ip->cursorx = 0;
	ip->cursory = 0;

	ip->isw = &itesw[device_unit(ip->device)]; /* XXX */
	SUBR_INIT(ip);
	SUBR_CURSOR(ip, DRAW_CURSOR);
	if (ip->tabs == NULL)
		ip->tabs = malloc(MAX_TABS * sizeof(u_char),
		    M_DEVBUF, M_WAITOK);
	ite_reset(ip);
	ip->flags |= ITE_INITED;
}

void
ite_config_console(void)
{
	struct grf_softc *gp = &congrf;

	if (con_itesoftc.grf != NULL)
		return;
	con_itesoftc.grf = gp;
	con_itesoftc.tabs = cons_tabs;
	con_itesoftc.device = &con_itedev;
}

/*
 * Perform functions necessary to setup device as a terminal emulator.
 */
int
iteon(dev_t dev, int flag)
{
	int unit = UNIT(dev);
	struct ite_softc *ip;

	if (unit < 0 || unit >= ite_cd.cd_ndevs ||
	    (ip = getitesp(dev)) == NULL || (ip->flags & ITE_ALIVE) == 0)
		return ENXIO;
	/* force ite active, overriding graphics mode */
	if ((flag & 1) != 0) {
		ip->flags |= ITE_ACTIVE;
		ip->flags &= ~(ITE_INGRF|ITE_INITED);
	}
	/* leave graphics mode */
	if ((flag & 2) != 0) {
		ip->flags &= ~ITE_INGRF;
		if ((ip->flags & ITE_ACTIVE) == 0)
			return 0;
	}
	ip->flags |= ITE_ACTIVE;
	if ((ip->flags & ITE_INGRF) != 0)
		return 0;
	iteinit(dev);
	if ((flag & 2) != 0)
		ite_reset(ip);
#if NKBD > 0
	mfp_send_usart(0x49);	/* XXX */
#endif
	return 0;
}

/*
 * "Shut down" device as terminal emulator.
 * Note that we do not deinit the console device unless forced.
 * Deinit'ing the console every time leads to a very active
 * screen when processing /etc/rc.
 */
void
iteoff(dev_t dev, int flag)
{
	int unit = UNIT(dev);
	struct ite_softc *ip;

	/* XXX check whether when call from grf.c */
	if (unit < 0 || unit >= ite_cd.cd_ndevs ||
	    (ip = getitesp(dev)) == NULL || (ip->flags & ITE_ALIVE) == 0)
		return;
	if ((flag & 2) != 0)
		ip->flags |= ITE_INGRF;

	if ((ip->flags & ITE_ACTIVE) == 0)
		return;
	if ((flag & 1) != 0 ||
	    (ip->flags & (ITE_INGRF | ITE_ISCONS | ITE_INITED)) == ITE_INITED)
		SUBR_DEINIT(ip);

	/*
	 * XXX When the system is rebooted with "reboot", init(8)
	 * kills the last process to have the console open.
	 * If we don't revent the ITE_ACTIVE bit from being
	 * cleared, we will never see messages printed during
	 * the process of rebooting.
	 */
	if ((flag & 2) == 0 && (ip->flags & ITE_ISCONS) == 0) {
		ip->flags &= ~ITE_ACTIVE;
#if NKBD > 0
		mfp_send_usart(0x48);	/* XXX */
#endif
	}
}

/*
 * standard entry points to the device.
 */

/* ARGSUSED */
static int
iteopen(dev_t dev, int mode, int devtype, struct lwp *l)
{
	int unit = UNIT(dev);
	struct tty *tp;
	struct ite_softc *ip;
	int error;
	int first = 0;

	if (unit >= ite_cd.cd_ndevs || (ip = getitesp(dev)) == NULL)
		return ENXIO;
	if (ite_tty[unit] == NULL) {
		tp = ite_tty[unit] = tty_alloc();
		tty_attach(tp);
	} else
		tp = ite_tty[unit];
	if (kauth_authorize_device_tty(l->l_cred, KAUTH_DEVICE_TTY_OPEN, tp))
		return EBUSY;
	if ((ip->flags & ITE_ACTIVE) == 0) {
		error = iteon(dev, 0);
		if (error)
			return error;
		first = 1;
	}
	tp->t_oproc = itestart;
	tp->t_param = NULL;
	tp->t_dev = dev;
	if ((tp->t_state&TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		tp->t_state = TS_ISOPEN|TS_CARR_ON;
		ttsetwater(tp);
	}
	error = (*tp->t_linesw->l_open)(dev, tp);
	if (error == 0) {
		tp->t_winsize.ws_row = ip->rows;
		tp->t_winsize.ws_col = ip->cols;
	} else if (first)
		iteoff(dev, 0);
	return error;
}

/*ARGSUSED*/
static int
iteclose(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct tty *tp = ite_tty[UNIT(dev)];

	(*tp->t_linesw->l_close)(tp, flag);
	ttyclose(tp);
	iteoff(dev, 0);
#if 0
	tty_free(tp);
	ite_tty[UNIT(dev)] = NULL;
#endif
	return 0;
}

static int
iteread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = ite_tty[UNIT(dev)];

	return (*tp->t_linesw->l_read)(tp, uio, flag);
}

static int
itewrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = ite_tty[UNIT(dev)];

	return (*tp->t_linesw->l_write)(tp, uio, flag);
}

static int
itepoll(dev_t dev, int events, struct lwp *l)
{
	struct tty *tp = ite_tty[UNIT(dev)];

	return (*tp->t_linesw->l_poll)(tp, events, l);
}

static struct tty *
itetty(dev_t dev)
{

	return ite_tty[UNIT(dev)];
}

static int
iteioctl(dev_t dev, u_long cmd, void *addr, int flag, struct lwp *l)
{
	struct iterepeat *irp;
	struct tty *tp = ite_tty[UNIT(dev)];
	int error;

	error = (*tp->t_linesw->l_ioctl)(tp, cmd, addr, flag, l);
	if (error != EPASSTHROUGH)
		return error;

	error = ttioctl(tp, cmd, addr, flag, l);
	if (error != EPASSTHROUGH)
		return error;

	switch (cmd) {
	case ITEIOCSKMAP:
		if (addr == NULL)
			return EFAULT;
		memcpy(&kbdmap, addr, sizeof(struct kbdmap));
		return 0;

	case ITEIOCGKMAP:
		if (addr == NULL)
			return EFAULT;
		memcpy(addr, &kbdmap, sizeof(struct kbdmap));
		return 0;

	case ITEIOCGREPT:
		irp = (struct iterepeat *)addr;
		irp->start = start_repeat_timeo;
		irp->next = next_repeat_timeo;

	case ITEIOCSREPT:
		irp = (struct iterepeat *)addr;
		if (irp->start < ITEMINREPEAT && irp->next < ITEMINREPEAT)
			return(EINVAL);
		start_repeat_timeo = irp->start;
		next_repeat_timeo = irp->next;
#if x68k
	case ITELOADFONT:
		if (addr != NULL) {
			memcpy(kern_font, addr, 4096 /*sizeof(kernel_font)*/);
			ite_set_glyph();
			return 0;
		} else
			return EFAULT;

	case ITETVCTRL:
		if (addr != NULL && *(uint8_t *)addr < 0x40) {
			return mfp_send_usart(*(uint8_t *)addr);
		} else {
			return EFAULT;
		}
#endif
	}
	return EPASSTHROUGH;
}

static void
itestart(struct tty *tp)
{
	struct clist *rbp;
	uint8_t buf[ITEBURST];
	int s, len;

	getitesp(tp->t_dev);
	/*
	 * (Potentially) lower priority.  We only need to protect ourselves
	 * from keyboard interrupts since that is all that can affect the
	 * state of our tty (kernel printf doesn't go through this routine).
	 */
	s = spltty();
	if ((tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) != 0)
		goto out;
	tp->t_state |= TS_BUSY;
	rbp = &tp->t_outq;
	len = q_to_b(rbp, buf, ITEBURST);
	/*splx(s);*/

	/* Here is a really good place to implement pre/jumpscroll() */
	ite_putstr(buf, len, tp->t_dev);

	/*s = spltty();*/
	tp->t_state &= ~TS_BUSY;
	/* we have characters remaining. */
	if (ttypull(tp)) {
		tp->t_state |= TS_TIMEOUT;
		callout_schedule(&tp->t_rstrt_ch, 1);
	}
 out:
	splx(s);
}

/* XXX called after changes made in underlying grf layer. */
/* I want to nuke this */
void
ite_reinit(dev_t dev)
{
	struct ite_softc *ip;
	int unit = UNIT(dev);

	/* XXX check whether when call from grf.c */
	if (unit < 0 || unit >= ite_cd.cd_ndevs ||
	    (ip = getitesp(dev)) == NULL)
		return;

	ip->flags &= ~ITE_INITED;
	iteinit(dev);
}

void
ite_reset(struct ite_softc *ip)
{
	int i;

	ip->curx = 0;
	ip->cury = 0;
	ip->attribute = 0;
	ip->save_curx = 0;
	ip->save_cury = 0;
	ip->save_attribute = 0;
	ip->ap = ip->argbuf;
	ip->emul_level = EMUL_VT300_8;
	ip->eightbit_C1 = 0;
	ip->top_margin = 0;
	ip->bottom_margin = ip->rows - 1;
	ip->inside_margins = 0; /* origin mode == absolute */
	ip->linefeed_newline = 0;
	ip->auto_wrap = 1;
	ip->cursor_appmode = 0;
	ip->keypad_appmode = 0;
	ip->imode = 0;
	ip->key_repeat = 1;
	ip->G0 = CSET_ASCII;
	ip->G1 = CSET_JIS1983;
	ip->G2 = CSET_JISKANA;
	ip->G3 = CSET_JIS1990;
	ip->GL = &ip->G0;
	ip->GR = &ip->G1;
	ip->save_GL = 0;
	ip->save_char = 0;
	ip->fgcolor = 7;
	ip->bgcolor = 0;
	for (i = 0; i < ip->cols; i++)
		ip->tabs[i] = ((i & 7) == 0);
	/* XXX clear screen */
	SUBR_CLEAR(ip, 0, 0, ip->rows, ip->cols);
	attrclr(ip, 0, 0, ip->rows, ip->cols);
}

/* Used in console at startup only */
int
ite_cnfilter(u_char c)
{
	static u_char mod = 0;
	struct key key;
	uint8_t code, up, mask;
	int s;

	up = c & 0x80 ? 1 : 0;
	c &= 0x7f;
	code = 0;

	s = spltty();

	mask = 0;
	if (c >= KBD_LEFT_ALT &&
	    !(c >= 0x63 && c <= 0x6c)) {	/* 0x63: F1, 0x6c:F10 */
		switch (c) {
		case KBD_LEFT_SHIFT:
			mask = KBD_MOD_SHIFT;
			break;

		case KBD_LEFT_ALT:
			mask = KBD_MOD_LALT;
			break;

		case KBD_RIGHT_ALT:
			mask = KBD_MOD_RALT;
			break;

		case KBD_LEFT_META:
			mask = KBD_MOD_LMETA;
			break;

		case KBD_RIGHT_META:
			mask = KBD_MOD_RMETA;
			break;

		case KBD_CAPS_LOCK:
			/*
			 * capslock already behaves `right', don't need to
			 * keep track of the state in here.
			 */
			mask = KBD_MOD_CAPS;
			break;

		case KBD_CTRL:
			mask = KBD_MOD_CTRL;
			break;

		case KBD_RECONNECT:
			/* ite got 0xff */
			if (up)
				kbd_setLED();
			break;
		}
		if (mask & KBD_MOD_CAPS) {
			if (!up) {
				mod ^= KBD_MOD_CAPS;
				kbdled ^= LED_CAPS_LOCK;
				kbd_setLED();
			}
		} else if (up)
			mod &= ~mask;
		else
			mod |= mask;
		splx(s);
		return -1;
	}

	if (up) {
		splx(s);
		return -1;
	}

	/* translate modifiers */
	if ((mod & KBD_MOD_SHIFT) != 0) {
		if ((mod & KBD_MOD_ALT) != 0)
			key = kbdmap.alt_shift_keys[c];
		else
			key = kbdmap.shift_keys[c];
	} else if ((mod & KBD_MOD_ALT) != 0)
		key = kbdmap.alt_keys[c];
	else {
		key = kbdmap.keys[c];
		/* if CAPS and key is CAPable (no pun intended) */
		if ((mod & KBD_MOD_CAPS) != 0 &&
		    (key.mode & KBD_MODE_CAPS) != 0)
			key = kbdmap.shift_keys[c];
	}
	code = key.code;

	/* if string return */
	if ((key.mode & (KBD_MODE_STRING | KBD_MODE_KPAD)) != 0) {
		splx(s);
		return -1;
	}
	/* handle dead keys */
	if ((key.mode & KBD_MODE_DEAD) != 0) {
		splx(s);
		return -1;
	}
	if ((mod & KBD_MOD_CTRL) != 0)
		code &= 0x1f;
	if ((mod & KBD_MOD_META) != 0)
		code |= 0x80;

	/* do console mapping. */
	code = code == '\r' ? '\n' : code;

	splx(s);
	return code;
}

/* And now the old stuff. */
static inline void
itesendch(int ch)
{

	(*kbd_tty->t_linesw->l_rint)(ch, kbd_tty);
}

void
ite_filter(u_char c)
{
	static uint16_t mod = 0;
	uint8_t code, *str;
	uint16_t up, mask;
	struct key key;
	int s, i;

	if (kbd_ite == NULL ||
	    (kbd_tty = ite_tty[device_unit(kbd_ite->device)]) == NULL)
		return;

	/* have to make sure we're at spltty in here */
	s = spltty();

	up = c & 0x80 ? 1 : 0;
	c &= 0x7f;
	code = 0;

	mask = 0;
	if (c >= KBD_LEFT_ALT &&
	    !(c >= 0x63 && c <= 0x6c)) {	/* 0x63: F1, 0x6c:F10 */
		switch (c) {
		case KBD_LEFT_SHIFT:
			mask = KBD_MOD_SHIFT;
			break;

		case KBD_LEFT_ALT:
			mask = KBD_MOD_LALT;
			break;

		case KBD_RIGHT_ALT:
			mask = KBD_MOD_RALT;
			break;

		case KBD_LEFT_META:
			mask = KBD_MOD_LMETA;
			break;

		case KBD_RIGHT_META:
			mask = KBD_MOD_RMETA;
			break;

		case KBD_CAPS_LOCK:
			/*
			 * capslock already behaves `right', don't need to keep
			 * track of the state in here.
			 */
			mask = KBD_MOD_CAPS;
			break;

		case KBD_CTRL:
			mask = KBD_MOD_CTRL;
			break;

		case KBD_OPT1:
			mask = KBD_MOD_OPT1;
			break;

		case KBD_OPT2:
			mask = KBD_MOD_OPT2;
			break;

		case KBD_RECONNECT:
			if (up) { /* ite got 0xff */
				kbd_setLED();
			}
			break;
		}

		if ((mask & KBD_MOD_CAPS) != 0) {
			if (!up) {
				mod ^= KBD_MOD_CAPS;
				kbdled ^= LED_CAPS_LOCK;
				kbd_setLED();
			}
		} else if (up) {
			mod &= ~mask;
		} else
			mod |= mask;

		/*
		 * return even if it wasn't a modifier key, the other
		 * codes up here are either special (like reset warning),
		 * or not yet defined
		 */
		splx(s);
		return;
	}

	if (up) {
		splx(s);
		return;
	}

	/*
	 * intercept LAlt-LMeta-F1 here to switch back to original ascii-keymap.
	 * this should probably be configurable..
	 */
	if (mod == (KBD_MOD_LALT|KBD_MOD_LMETA) && c == 0x63) {
		memcpy(&kbdmap, &ascii_kbdmap, sizeof(struct kbdmap));
		splx(s);
		return;
	}

	/* translate modifiers */
	if ((mod & KBD_MOD_SHIFT) != 0) {
		if ((mod & KBD_MOD_ALT) != 0)
			key = kbdmap.alt_shift_keys[c];
		else
			key = kbdmap.shift_keys[c];
	} else if ((mod & KBD_MOD_ALT) != 0)
		key = kbdmap.alt_keys[c];
	else {
		key = kbdmap.keys[c];
		/* if CAPS and key is CAPable (no pun intended) */
		if ((mod & KBD_MOD_CAPS) != 0 &&
		    (key.mode & KBD_MODE_CAPS) != 0)
			key = kbdmap.shift_keys[c];
		else if ((mod & KBD_MOD_OPT2) != 0 &&
		         (key.mode & KBD_MODE_KPAD) != 0)
			key = kbdmap.shift_keys[c];
	}
	code = key.code;

	/* handle dead keys */
	if ((key.mode & KBD_MODE_DEAD) != 0) {
		splx(s);
		return;
	}
	/* if not string, apply META and CTRL modifiers */
	if ((key.mode & KBD_MODE_STRING) == 0 &&
	    ((key.mode & KBD_MODE_KPAD) == 0 ||
	     (kbd_ite != NULL && kbd_ite->keypad_appmode == 0))) {
		if ((mod & KBD_MOD_CTRL) != 0 &&
		    (code == ' ' || (code >= '@' && code <= 'z')))
			code &= 0x1f;
		if ((mod & KBD_MOD_META) != 0)
			code |= 0x80;
	} else if ((key.mode & KBD_MODE_KPAD) != 0 &&
	       (kbd_ite != NULL && kbd_ite->keypad_appmode != 0)) {
		static const char * const in = "0123456789-+.\r()/*";
		static const char * const out = "pqrstuvwxymlnMPQRS";
		char *cp = strchr(in, code);

		/*
		 * keypad-appmode sends SS3 followed by the above
		 * translated character
		 */
		(*kbd_tty->t_linesw->l_rint)(27, kbd_tty);
		(*kbd_tty->t_linesw->l_rint)('O', kbd_tty);
		(*kbd_tty->t_linesw->l_rint)(out[cp - in], kbd_tty);
		splx(s);
		return;
	} else {
		/* *NO* I don't like this.... */
		static u_char app_cursor[] = {
		  3, 27, 'O', 'A',
		  3, 27, 'O', 'B',
		  3, 27, 'O', 'C',
		  3, 27, 'O', 'D'
		};

		str = kbdmap.strings + code;
		/*
		 * if this is a cursor key, AND it has the default
		 * keymap setting, AND we're in app-cursor mode, switch
		 * to the above table. This is *nasty* !
		 */
		if (c >= 0x3b && c <= 0x3e && kbd_ite->cursor_appmode != 0 &&
		    memcmp(str, "\x03\x1b[", 3) == 0 &&
		    strchr("ABCD", str[3]) != 0)
			str = app_cursor + 4 * (str[3] - 'A');

		/*
		 * using a length-byte instead of 0-termination allows
		 * to embed \0 into strings, although this is not used
		 * in the default keymap
		 */
		for (i = *str++; i; i--)
			(*kbd_tty->t_linesw->l_rint)(*str++, kbd_tty);
		splx(s);
		return;
	}
	(*kbd_tty->t_linesw->l_rint)(code, kbd_tty);

	splx(s);
	return;
}

/* helper functions, makes the code below more readable */
static inline void
ite_sendstr(struct ite_softc *ip, const char *str)
{

	while (*str != 0)
		itesendch(*str++);
}

static inline void
alignment_display(struct ite_softc *ip)
{
	int i, j;

	for (j = 0; j < ip->rows; j++)
		for (i = 0; i < ip->cols; i++)
			SUBR_PUTC(ip, 'E', j, i, ATTR_NOR);
	attrclr(ip, 0, 0, ip->rows, ip->cols);
}

static inline void
snap_cury(struct ite_softc *ip)
{

	if (ip->inside_margins != 0) {
		if (ip->cury < ip->top_margin)
			ip->cury = ip->top_margin;
		if (ip->cury > ip->bottom_margin)
			ip->cury = ip->bottom_margin;
	}
}

static inline void
ite_dnchar(struct ite_softc *ip, int n)
{

	n = uimin(n, ip->cols - ip->curx);
	if (n < ip->cols - ip->curx) {
		SUBR_SCROLL(ip, ip->cury, ip->curx + n, n, SCROLL_LEFT);
		attrmov(ip, ip->cury, ip->curx + n, ip->cury, ip->curx,
		    1, ip->cols - ip->curx - n);
		attrclr(ip, ip->cury, ip->cols - n, 1, n);
	}
	while (n-- > 0)
		SUBR_PUTC(ip, ' ', ip->cury, ip->cols - n - 1, ATTR_NOR);
}

static void
ite_inchar(struct ite_softc *ip, int n)
{
	int c = ip->save_char;

	ip->save_char = 0;
	n = uimin(n, ip->cols - ip->curx);
	if (n < ip->cols - ip->curx) {
		SUBR_SCROLL(ip, ip->cury, ip->curx, n, SCROLL_RIGHT);
		attrmov(ip, ip->cury, ip->curx, ip->cury, ip->curx + n,
		    1, ip->cols - ip->curx - n);
		attrclr(ip, ip->cury, ip->curx, 1, n);
	}
	while (n-- != 0)
		SUBR_PUTC(ip, ' ', ip->cury, ip->curx + n, ATTR_NOR);
	ip->save_char = c;
}

static inline void
ite_clrtoeol(struct ite_softc *ip)
{
	int y = ip->cury, x = ip->curx;

	if (ip->cols - x > 0) {
		SUBR_CLEAR(ip, y, x, 1, ip->cols - x);
		attrclr(ip, y, x, 1, ip->cols - x);
	}
}

static inline void
ite_clrtobol(struct ite_softc *ip)
{
	int y = ip->cury, x = uimin(ip->curx + 1, ip->cols);

	SUBR_CLEAR(ip, y, 0, 1, x);
	attrclr(ip, y, 0, 1, x);
}

static inline void
ite_clrline(struct ite_softc *ip)
{
	int y = ip->cury;

	SUBR_CLEAR(ip, y, 0, 1, ip->cols);
	attrclr(ip, y, 0, 1, ip->cols);
}

static inline void
ite_clrtoeos(struct ite_softc *ip)
{

	ite_clrtoeol(ip);
	if (ip->cury < ip->rows - 1) {
		SUBR_CLEAR(ip, ip->cury + 1, 0,
		    ip->rows - 1 - ip->cury, ip->cols);
		attrclr(ip, ip->cury, 0, ip->rows - ip->cury, ip->cols);
	}
}

static inline void
ite_clrtobos(struct ite_softc *ip)
{

	ite_clrtobol(ip);
	if (ip->cury > 0) {
		SUBR_CLEAR(ip, 0, 0, ip->cury, ip->cols);
		attrclr(ip, 0, 0, ip->cury, ip->cols);
	}
}

static inline void
ite_clrscreen(struct ite_softc *ip)
{

	SUBR_CLEAR(ip, 0, 0, ip->rows, ip->cols);
	attrclr(ip, 0, 0, ip->rows, ip->cols);
}

static inline void
ite_dnline(struct ite_softc *ip, int n)
{

	/*
	 * interesting.. if the cursor is outside the scrolling
	 * region, this command is simply ignored..
	 */
	if (ip->cury < ip->top_margin || ip->cury > ip->bottom_margin)
		return;

	n = uimin(n, ip->bottom_margin + 1 - ip->cury);
	if (n <= ip->bottom_margin - ip->cury) {
		SUBR_SCROLL(ip, ip->cury + n, 0, n, SCROLL_UP);
		attrmov(ip, ip->cury + n, 0, ip->cury, 0,
		    ip->bottom_margin + 1 - ip->cury - n, ip->cols);
	}
	SUBR_CLEAR(ip, ip->bottom_margin - n + 1, 0, n, ip->cols);
	attrclr(ip, ip->bottom_margin - n + 1, 0, n, ip->cols);
}

static inline void
ite_inline(struct ite_softc *ip, int n)
{

	/*
	 * interesting.. if the cursor is outside the scrolling
	 * region, this command is simply ignored..
	 */
	if (ip->cury < ip->top_margin || ip->cury > ip->bottom_margin)
		return;

	if (n <= 0)
		n = 1;
	else
		n = uimin(n, ip->bottom_margin + 1 - ip->cury);
	if (n <= ip->bottom_margin  - ip->cury) {
		SUBR_SCROLL(ip, ip->cury, 0, n, SCROLL_DOWN);
		attrmov(ip, ip->cury, 0, ip->cury + n, 0,
		    ip->bottom_margin + 1 - ip->cury - n, ip->cols);
	}
	SUBR_CLEAR(ip, ip->cury, 0, n, ip->cols);
	attrclr(ip, ip->cury, 0, n, ip->cols);
	ip->curx = 0;
}

static inline void
ite_index(struct ite_softc *ip)
{

	++ip->cury;
	if ((ip->cury == ip->bottom_margin+1) || (ip->cury == ip->rows)) {
		ip->cury--;
		SUBR_SCROLL(ip, ip->top_margin + 1, 0, 1, SCROLL_UP);
		ite_clrline(ip);
	}
	/*clr_attr(ip, ATTR_INV);*/
}

static inline void
ite_lf(struct ite_softc *ip)
{

	++ip->cury;
	if (ip->cury > ip->bottom_margin) {
		ip->cury--;
		SUBR_SCROLL(ip, ip->top_margin + 1, 0, 1, SCROLL_UP);
		ite_clrline(ip);
	}
	/*SUBR_CURSOR(ip, MOVE_CURSOR);*/
	/*clr_attr(ip, ATTR_INV);*/
	/* reset character set ... thanks for mohta. */
	ip->G0 = CSET_ASCII;
	ip->G1 = CSET_JIS1983;
	ip->G2 = CSET_JISKANA;
	ip->G3 = CSET_JIS1990;
	ip->GL = &ip->G0;
	ip->GR = &ip->G1;
	ip->save_GL = 0;
	ip->save_char = 0;
}

static inline void
ite_crlf(struct ite_softc *ip)
{

	ip->curx = 0;
	ite_lf(ip);
}

static inline void
ite_cr(struct ite_softc *ip)
{

	if (ip->curx != 0) {
		ip->curx = 0;
	}
}

static inline void
ite_rlf(struct ite_softc *ip)
{

	ip->cury--;
	if ((ip->cury < 0) || (ip->cury == ip->top_margin - 1)) {
		ip->cury++;
		SUBR_SCROLL(ip, ip->top_margin, 0, 1, SCROLL_DOWN);
		ite_clrline(ip);
	}
	clr_attr(ip, ATTR_INV);
}

static inline int
atoi(const char *cp)
{
	int n;

	for (n = 0; *cp && *cp >= '0' && *cp <= '9'; cp++)
		n = n * 10 + *cp - '0';
	return n;
}

static inline int
ite_argnum(struct ite_softc *ip)
{
	char ch;
	int n;

	/* convert argument string into number */
	if (ip->ap == ip->argbuf)
		return 1;
	ch = *ip->ap;
	*ip->ap = 0;
	n = atoi(ip->argbuf);
	*ip->ap = ch;

	return n;
}

static inline int
ite_zargnum(struct ite_softc *ip)
{
	char ch;
	int n;

	/* convert argument string into number */
	if (ip->ap == ip->argbuf)
		return 0;
	ch = *ip->ap;
	*ip->ap = 0;	/* terminate string */
	n = atoi(ip->argbuf);
	*ip->ap = ch;

	return n;	/* don't "n ? n : 1" here, <CSI>0m != <CSI>1m ! */
}

static void
ite_putstr(const u_char *s, int len, dev_t dev)
{
	struct ite_softc *ip;
	int i;

	ip = getitesp(dev);

	/* XXX avoid problems */
	if ((ip->flags & (ITE_ACTIVE|ITE_INGRF)) != ITE_ACTIVE)
		return;

#if defined(ITE_SIXEL)
	/* avoid flicking cursor */
	if (ip->escape == 0)
#endif
	SUBR_CURSOR(ip, START_CURSOROPT);
	for (i = 0; i < len; i++)
		if (s[i] != 0)
			iteputchar(s[i], ip);
#if defined(ITE_SIXEL)
	/* avoid flicking cursor */
	if (ip->escape == 0)
#endif
	SUBR_CURSOR(ip, END_CURSOROPT);
}

static void
iteputchar(int c, struct ite_softc *ip)
{
	int n, x, y;
	char *cp;

#if defined(ITE_SIXEL)
	if ((c >= 0x20 && ip->escape != 0) || ip->escape == DCS) {
#else
	if (c >= 0x20 && ip->escape != 0) {
#endif
		switch (ip->escape) {

		case ESC:
			switch (c) {
			/*
			 * first 7bit equivalents for the 8bit control
			 * characters
			 */

			case 'D':
				c = IND;
				ip->escape = 0;
				break;
				/*
				 * and fall into the next switch below
				 * (same for all `break')
				 */

			case 'E':
				/* next line */
				c = NEL;
				ip->escape = 0;
				break;

			case 'H':
				/* set TAB at current col */
				c = HTS;
				ip->escape = 0;
				break;

			case 'M':
				/* reverse index */
				c = RI;
				ip->escape = 0;
				break;

			case 'N':
				/* single shift G2 */
				c = SS2;
				ip->escape = 0;
				break;

			case 'O':
				/* single shift G3 */
				c = SS3;
				ip->escape = 0;
				break;

			case 'P':
				/* DCS detected */
				c = DCS;
				ip->escape = 0;
				break;

			case '[':
				c = CSI;
				ip->escape = 0;
				break;

			case '\\':
				/* String Terminator */
				c = ST;
				ip->escape = 0;
#if defined(ITE_SIXEL)
				if (ip->decsixel_y != 0) {
					ite_lf(ip);
				}
#endif
				break;

			case ']':
				c = OSC;
				ip->escape = 0;
				break;

			case '^':
				c = PM;
				ip->escape = 0;
				break;

			case '_':
				c = APC;
				ip->escape = 0;
				break;

			/* introduces 7/8bit control */
			case ' ':
				/* can be followed by either F or G */
				ip->escape = ' ';
				break;

			/* a lot of character set selections, not yet used... */
			/* 94-character sets: */
			case '(':	/* G0 */
			case ')':	/* G1 */
				ip->escape = c;
				return;

			case '*':	/* G2 */
			case '+':	/* G3 */
			case 'B':	/* ASCII */
			case 'A':	/* ISO latin 1 */
			case '<':	/* user preferred suplemental */
			case '0':	/* DEC special graphics */

			/* 96-character sets: */
			case '-':	/* G1 */
			case '.':	/* G2 */
			case '/':	/* G3 */

			/* national character sets: */
			case '4':	/* dutch */
			case '5':
			case 'C':	/* finnish */
			case 'R':	/* french */
			case 'Q':	/* french canadian */
			case 'K':	/* german */
			case 'Y':	/* italian */
			case '6':	/* norwegian/danish */
			/* note: %5 and %6 are not supported (two chars..) */

				ip->escape = 0;
				/* just ignore for now */
				return;

			/* 94-multibyte character sets designate */
			case '$':
				ip->escape = '$';
				return;

			/* locking shift modes */
			case '`':
				ip->GR = &ip->G1;
				ip->escape = 0;
				return;

			case 'n':
				ip->GL = &ip->G2;
				ip->escape = 0;
				return;

			case '}':
				ip->GR = &ip->G2;
				ip->escape = 0;
				return;

			case 'o':
				ip->GL = &ip->G3;
				ip->escape = 0;
				return;

			case '|':
				ip->GR = &ip->G3;
				ip->escape = 0;
				return;

			case '~':
				ip->GR = &ip->G1;
				ip->escape = 0;
				return;

			/* font width/height control */
			case '#':
				ip->escape = '#';
				return;

			case 'c':
				/* hard terminal reset .. */
				ite_reset(ip);
				SUBR_CURSOR(ip, MOVE_CURSOR);
				ip->escape = 0;
				return;

			case '7':
				/* save cursor */
				ip->save_curx = ip->curx;
				ip->save_cury = ip->cury;
				ip->save_attribute = ip->attribute;
				ip->sc_om = ip->inside_margins;
				ip->sc_G0 = ip->G0;
				ip->sc_G1 = ip->G1;
				ip->sc_G2 = ip->G2;
				ip->sc_G3 = ip->G3;
				ip->sc_GL = ip->GL;
				ip->sc_GR = ip->GR;
				ip->escape = 0;
				return;

			case '8':
				/* restore cursor */
				ip->curx = ip->save_curx;
				ip->cury = ip->save_cury;
				ip->attribute = ip->save_attribute;
				ip->inside_margins = ip->sc_om;
				ip->G0 = ip->sc_G0;
				ip->G1 = ip->sc_G1;
				ip->G2 = ip->sc_G2;
				ip->G3 = ip->sc_G3;
				ip->GL = ip->sc_GL;
				ip->GR = ip->sc_GR;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				ip->escape = 0;
				return;

			case '=':
				/* keypad application mode */
				ip->keypad_appmode = 1;
				ip->escape = 0;
				return;

			case '>':
				/* keypad numeric mode */
				ip->keypad_appmode = 0;
				ip->escape = 0;
				return;

			case 'Z':	/* request ID */
				if (ip->emul_level == EMUL_VT100) {
					/* XXX not clean */
					ite_sendstr(ip, "\033[61;0c");
				} else {
					/* XXX not clean */
					ite_sendstr(ip, "\033[63;0c");
				}
				ip->escape = 0;
				return;

			/* default catch all for not recognized ESC sequences */
			default:
				ip->escape = 0;
				return;
			}
			break;

		case '(': /* designate G0 */
			switch (c) {
			case 'B': /* USASCII */
				ip->G0 = CSET_ASCII;
				ip->escape = 0;
				return;
			case 'I':
				ip->G0 = CSET_JISKANA;
				ip->escape = 0;
				return;
			case 'J':
				ip->G0 = CSET_JISROMA;
				ip->escape = 0;
				return;
			case '0': /* DEC special graphics */
				ip->G0 = CSET_DECGRAPH;
				ip->escape = 0;
				return;
			case 'A': /* British or ISO-Latin-1 */
			case 'H': /* Swedish */
			case 'K': /* German */
			case 'R': /* French */
			case 'Y': /* Italian */
			case 'Z': /* Spanish */
			default:
				/* not supported */
				ip->escape = 0;
				return;
			}

		case ')': /* designate G1 */
			ip->escape = 0;
			return;

		case '$': /* 94-multibyte character set */
			switch (c) {
			case '@':
				ip->G0 = CSET_JIS1978;
				ip->escape = 0;
				return;
			case 'B':
				ip->G0 = CSET_JIS1983;
				ip->escape = 0;
				return;
			case 'D':
				ip->G0 = CSET_JIS1990;
				ip->escape = 0;
				return;
			default:
				/* not supported */
				ip->escape = 0;
				return;
			}

		case ' ':
			switch (c) {
			case 'F':
				ip->eightbit_C1 = 0;
				ip->escape = 0;
				return;

			case 'G':
				ip->eightbit_C1 = 1;
				ip->escape = 0;
				return;

			default:
				/* not supported */
				ip->escape = 0;
				return;
			}
			break;

		case '#':
			switch (c) {
			case '5':
				/* single height, single width */
				ip->escape = 0;
				return;

			case '6':
				/* double width, single height */
				ip->escape = 0;
				return;

			case '3':
				/* top half */
				ip->escape = 0;
				return;

			case '4':
				/* bottom half */
				ip->escape = 0;
				return;

			case '8':
				/* screen alignment pattern... */
				alignment_display(ip);
				ip->escape = 0;
				return;

			default:
				ip->escape = 0;
				return;
			}
			break;

		case CSI:
			/* the biggie... */
			switch (c) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case ';':
			case '\"':
			case '$':
			case '>':
				if (ip->ap < ip->argbuf + MAX_ARGSIZE)
					*ip->ap++ = c;
				return;

			case 'p':
				*ip->ap = 0;
				if (!strncmp(ip->argbuf, "61\"", 3))
					ip->emul_level = EMUL_VT100;
				else if (!strncmp(ip->argbuf, "63;1\"", 5) ||
					 !strncmp(ip->argbuf, "62;1\"", 5))
					ip->emul_level = EMUL_VT300_7;
				else
					ip->emul_level = EMUL_VT300_8;
				ip->escape = 0;
				return;

			case '?':
				*ip->ap = 0;
				ip->escape = '?';
				ip->ap = ip->argbuf;
				return;

			case 'c':
				/* device attributes */
				*ip->ap = 0;
				if (ip->argbuf[0] == '>') {
					ite_sendstr(ip, "\033[>24;0;0;0c");
				} else {
					switch (ite_zargnum(ip)) {
					case 0:
						/*
						 * primary DA request, send
						 * primary DA response
						 */
						if (ip->emul_level ==
						    EMUL_VT100)
							ite_sendstr(ip,
							    "\033[?1;1c");
						else
#if defined(ITE_SIXEL)
							ite_sendstr(ip,
							    "\033[63;4c");
#else
							ite_sendstr(ip,
							    "\033[63;0c");
#endif
						break;
					}
				}
				ip->escape = 0;
				return;

			case 'n':
				switch (ite_zargnum(ip)) {
				case 5:
					/* no malfunction */
					ite_sendstr(ip, "\033[0n");
					break;
				case 6:
					/* cursor position report */
					snprintf(ip->argbuf, sizeof(ip->argbuf),
					    "\033[%d;%dR",
					     ip->cury + 1, ip->curx + 1);
					ite_sendstr(ip, ip->argbuf);
					break;
				}
				ip->escape = 0;
				return;

			case 'x':
				switch (ite_zargnum(ip)) {
				case 0:
					/* Fake some terminal parameters.  */
					ite_sendstr(ip,
					    "\033[2;1;1;112;112;1;0x");
					break;
				case 1:
					ite_sendstr(ip,
					    "\033[3;1;1;112;112;1;0x");
					break;
				}
				ip->escape = 0;
				return;

			case 'g':
				/* clear tabs */
				switch (ite_zargnum(ip)) {
				case 0:
					if (ip->curx < ip->cols)
						ip->tabs[ip->curx] = 0;
					break;
				case 3:
					for (n = 0; n < ip->cols; n++)
						ip->tabs[n] = 0;
					break;

				default:
					/* ignore */
					break;
				}
				ip->escape = 0;
				return;

			case 'h': /* set mode */
			case 'l': /* reset mode */
				n = ite_zargnum(ip);
				switch (n) {
				case 4:
					/* insert/replace mode */
					ip->imode = (c == 'h');
					break;
				case 20:
					ip->linefeed_newline = (c == 'h');
					break;
				}
				ip->escape = 0;
				return;

			case 'M':
				/* delete line */
				ite_dnline(ip, ite_argnum(ip));
				ip->escape = 0;
				return;

			case 'L':
				/* insert line */
				ite_inline(ip, ite_argnum(ip));
				ip->escape = 0;
				return;

			case 'P':
				/* delete char */
				ite_dnchar(ip, ite_argnum(ip));
				ip->escape = 0;
				return;

			case '@':
				/* insert char(s) */
				ite_inchar(ip, ite_argnum(ip));
				ip->escape = 0;
				return;

			case '!':
				/* soft terminal reset */
				ip->escape = 0; /* XXX */
				return;

			case 'G':
				/*
				 * this one was *not* in my vt320 manual but
				 * in a vt320 termcap entry.. who is right?
				 * It's supposed to set the horizontal cursor
				 * position.
				 */
				*ip->ap = 0;
				x = atoi(ip->argbuf);
				if (x != 0)
					x--;
				ip->curx = uimin(x, ip->cols - 1);
				ip->escape = 0;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				clr_attr(ip, ATTR_INV);
				return;

			case 'd':
				/*
				 * same thing here, this one's for setting
				 * the absolute vertical cursor position.
				 * Not documented...
				 */
				*ip->ap = 0;
				y = atoi(ip->argbuf);
				if (y)
					y--;
				if (ip->inside_margins)
					y += ip->top_margin;
				ip->cury = uimin(y, ip->rows - 1);
				ip->escape = 0;
				snap_cury(ip);
				SUBR_CURSOR(ip, MOVE_CURSOR);
				clr_attr(ip, ATTR_INV);
				return;

			case 'H':
			case 'f':
				*ip->ap = 0;
				y = atoi(ip->argbuf);
				x = 0;
				cp = strchr(ip->argbuf, ';');
				if (cp != NULL)
					x = atoi(cp + 1);
				if (x != 0)
					x--;
				if (y != 0)
					y--;
				if (ip->inside_margins != 0)
					y += ip->top_margin;
				ip->cury = uimin(y, ip->rows - 1);
				ip->curx = uimin(x, ip->cols - 1);
				ip->escape = 0;
				snap_cury(ip);
				SUBR_CURSOR(ip, MOVE_CURSOR);
				/*clr_attr(ip, ATTR_INV);*/
				return;

			case 'A':
				/* cursor up */
				n = ite_argnum(ip);
				n = ip->cury - (n ? n : 1);
				if (n < 0)
					n = 0;
				if (ip->inside_margins)
					n = uimax(ip->top_margin, n);
				else if (n == ip->top_margin - 1)
					/*
					 * allow scrolling outside region,
					 * but don't scroll out of active
					 * region without explicit CUP
					 */
					n = ip->top_margin;
				ip->cury = n;
				ip->escape = 0;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				clr_attr(ip, ATTR_INV);
				return;

			case 'B':
				/* cursor down */
				n = ite_argnum(ip);
				n = ip->cury + (n ? n : 1);
				n = uimin(ip->rows - 1, n);
#if 0
				if (ip->inside_margins)
#endif
					n = uimin(ip->bottom_margin, n);
#if 0
				else if (n == ip->bottom_margin + 1)
					/*
					 * allow scrolling outside region,
					 * but don't scroll out of active
					 * region without explicit CUP
					 */
					n = ip->bottom_margin;
#endif
				ip->cury = n;
				ip->escape = 0;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				clr_attr(ip, ATTR_INV);
				return;

			case 'C':
				/* cursor forward */
				n = ite_argnum(ip);
				n = n ? n : 1;
				ip->curx = uimin(ip->curx + n, ip->cols - 1);
				ip->escape = 0;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				clr_attr(ip, ATTR_INV);
				return;

			case 'D':
				/* cursor backward */
				n = ite_argnum(ip);
				n = n ? n : 1;
				n = ip->curx - n;
				ip->curx = n >= 0 ? n : 0;
				ip->escape = 0;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				clr_attr(ip, ATTR_INV);
				return;

			case 'J':
				/* erase screen */
				*ip->ap = 0;
				n = ite_zargnum(ip);
				if (n == 0)
					ite_clrtoeos(ip);
				else if (n == 1)
					ite_clrtobos(ip);
				else if (n == 2)
					ite_clrscreen(ip);
				ip->escape = 0;
				return;

			case 'K':
				/* erase line */
				n = ite_zargnum(ip);
				if (n == 0)
					ite_clrtoeol(ip);
				else if (n == 1)
					ite_clrtobol(ip);
				else if (n == 2)
					ite_clrline(ip);
				ip->escape = 0;
				return;

			case 'S':
				/* scroll up */
				n = ite_zargnum(ip);
				if (n <= 0)
					n = 1;
				else if (n > ip->rows-1)
					n = ip->rows-1;
				SUBR_SCROLL(ip, ip->rows-1, 0, n, SCROLL_UP);
				ip->escape = 0;
				return;

			case 'T':
				/* scroll down */
				n = ite_zargnum(ip);
				if (n <= 0)
					n = 1;
				else if (n > ip->rows-1)
					n = ip->rows-1;
				SUBR_SCROLL(ip, 0, 0, n, SCROLL_DOWN);
				ip->escape = 0;
				return;

			case 'X':
				/* erase character */
				n = ite_argnum(ip) - 1;
				n = uimin(n, ip->cols - 1 - ip->curx);
				for (; n >= 0; n--) {
					attrclr(ip, ip->cury, ip->curx + n,
					    1, 1);
					SUBR_PUTC(ip, ' ',
					    ip->cury, ip->curx + n, ATTR_NOR);
				}
				ip->escape = 0;
				return;

			case '}': case '`':
				/* status line control */
				ip->escape = 0;
				return;

			case 'r':
				/* set scrolling region */
				ip->escape = 0;
				*ip->ap = 0;
				x = atoi(ip->argbuf);
				x = x ? x : 1;
				y = ip->rows;
				cp = strchr(ip->argbuf, ';');
				if (cp) {
					y = atoi(cp + 1);
					y = y ? y : ip->rows;
				}
				if (y <= x)
					return;
				x--;
				y--;
				ip->top_margin = uimin(x, ip->rows - 2);
				ip->bottom_margin = uimin(y, ip->rows - 1);
				if (ip->inside_margins) {
					ip->cury = ip->top_margin;
				} else
					ip->cury = 0;
				ip->curx = 0;
				return;

			case 'm':
				/* big attribute setter/resetter */
			{
				char *c_p;

				*ip->ap = 0;
				/* kludge to make CSIm work (== CSI0m) */
				if (ip->ap == ip->argbuf)
					ip->ap++;
				for (c_p = ip->argbuf; c_p < ip->ap; ) {
					switch (*c_p) {
					case 0:
					case '0':
						clr_attr(ip, ATTR_ALL);
						ip->fgcolor = 7;
						ip->bgcolor = 0;
						c_p++;
						break;

					case '1':
						set_attr(ip, ATTR_BOLD);
						c_p++;
						break;

					case '2':
						switch (c_p[1]) {
						case '2':
							clr_attr(ip, ATTR_BOLD);
							c_p += 2;
							break;

						case '4':
							clr_attr(ip, ATTR_UL);
							c_p += 2;
							break;

						case '5':
							clr_attr(ip,
							    ATTR_BLINK);
							c_p += 2;
							break;

						case '7':
							clr_attr(ip, ATTR_INV);
							c_p += 2;
							break;

						default:
							c_p++;
							break;
						}
						break;

					case '3':
						switch (c_p[1]) {
						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
							/* foreground colors */
							ip->fgcolor =
							    c_p[1] - '0';
							c_p += 2;
							break;
						default:
							c_p++;
							break;
						}
						break;

					case '4':
						switch (c_p[1]) {
						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
							/* background colors */
							ip->bgcolor =
							    c_p[1] - '0';
							c_p += 2;
							break;
						default:
							set_attr(ip, ATTR_UL);
							c_p++;
							break;
						}
						break;

					case '5':
						set_attr(ip, ATTR_BLINK);
						c_p++;
						break;

					case '7':
						set_attr(ip, ATTR_INV);
						c_p++;
						break;

					default:
						c_p++;
						break;
					}
				}

			}
				ip->escape = 0;
				return;

			case 'u':
				/* DECRQTSR */
				ite_sendstr(ip, "\033P\033\\");
				ip->escape = 0;
				return;

			default:
				ip->escape = 0;
				return;
			}
			break;

		case '?':	/* CSI ? */
			switch (c) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case ';':
			case '\"':
			case '$':
				/*
				 * Don't fill the last character; it's needed.
				 */
				/* XXX yeah, where ?? */
				if (ip->ap < ip->argbuf + MAX_ARGSIZE - 1)
					*ip->ap++ = c;
				return;

			case 'n':
				/* Terminal Reports */
				*ip->ap = 0;
				if (ip->ap == &ip->argbuf[2]) {
					if (!strncmp(ip->argbuf, "15", 2))
						/* printer status: no printer */
						ite_sendstr(ip, "\033[13n");
					else if (!strncmp(ip->argbuf, "25", 2))
						/* udk status */
						ite_sendstr(ip, "\033[20n");
					else if (!strncmp(ip->argbuf, "26", 2))
						/* keyboard dialect: US */
						ite_sendstr(ip, "\033[27;1n");
				}
				ip->escape = 0;
				return;

			case 'h': /* set dec private modes */
			case 'l': /* reset dec private modes */
				n = ite_zargnum(ip);
				switch (n) {
				case 1:
					/* CKM - cursor key mode */
					ip->cursor_appmode = (c == 'h');
					break;

				case 3:
					/* 132/80 columns (132 == 'h') */
					break;

				case 4: /* smooth scroll */
					break;

				case 5:
					/*
					 * light background (=='h') /
					 * dark background (=='l')
					 */
					break;

				case 6: /* origin mode */
					ip->inside_margins = (c == 'h');
#if 0
					ip->curx = 0;
					ip->cury = ip->inside_margins ?
					    ip->top_margin : 0;
					SUBR_CURSOR(ip, MOVE_CURSOR);
#endif
					break;

				case 7: /* auto wraparound */
					ip->auto_wrap = (c == 'h');
					break;

				case 8: /* keyboard repeat */
					ip->key_repeat = (c == 'h');
					break;

				case 20: /* newline mode */
					ip->linefeed_newline = (c == 'h');
					break;

				case 25: /* cursor on/off */
					SUBR_CURSOR(ip, (c == 'h') ?
					    DRAW_CURSOR : ERASE_CURSOR);
					break;
				}
				ip->escape = 0;
				return;

			case 'K':
				/* selective erase in line */
			case 'J':
				/* selective erase in display */

			default:
				ip->escape = 0;
				return;
			}
			break;

#if defined(ITE_SIXEL)
		case DCS:
			if (ite_dcs(c, ip) == 0) {
				return;
			}
			break;
#endif /* ITE_SIXEL */

		default:
			ip->escape = 0;
			return;
		}
	}

	switch (c) {
	case 0x00:	/* NUL */
	case 0x01:	/* SOH */
	case 0x02:	/* STX */
	case 0x03:	/* ETX */
	case 0x04:	/* EOT */
	case 0x05:	/* ENQ */
	case 0x06:	/* ACK */
		break;

	case BEL:
#if NBELL > 0
		if (kbd_ite && ite_tty[device_unit(kbd_ite->device)])
			opm_bell();
#endif
		break;

	case BS:
		if (--ip->curx < 0)
			ip->curx = 0;
		else
			SUBR_CURSOR(ip, MOVE_CURSOR);
		break;

	case HT:
		for (n = ip->curx + 1; n < ip->cols; n++) {
			if (ip->tabs[n]) {
				ip->curx = n;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				break;
			}
		}
		break;

	case VT:	/* VT is treated like LF */
	case FF:	/* so is FF */
	case LF:
		/*
		 * cr->crlf distinction is done here, on output,
		 * not on input!
		 */
		if (ip->linefeed_newline)
			ite_crlf(ip);
		else
			ite_lf(ip);
		break;

	case CR:
		ite_cr(ip);
		break;

	case SO:
		ip->GL = &ip->G1;
		break;

	case SI:
		ip->GL = &ip->G0;
		break;

	case 0x10:	/* DLE */
	case 0x11:	/* DC1/XON */
	case 0x12:	/* DC2 */
	case 0x13:	/* DC3/XOFF */
	case 0x14:	/* DC4 */
	case 0x15:	/* NAK */
	case 0x16:	/* SYN */
	case 0x17:	/* ETB */
		break;

	case CAN:
		ip->escape = 0;	/* cancel any escape sequence in progress */
		break;

	case 0x19:	/* EM */
		break;

	case SUB:
		ip->escape = 0;	/* dito, but see below */
		/* should also display a reverse question mark!! */
		break;

	case ESC:
		ip->escape = ESC;
		break;

	case 0x1c:	/* FS */
	case 0x1d:	/* GS */
	case 0x1e:	/* RS */
	case 0x1f:	/* US */
		break;

	/* now it gets weird.. 8bit control sequences.. */
	case IND:	/* index: move cursor down, scroll */
		ite_index(ip);
		break;

	case NEL:	/* next line. next line, first pos. */
		ite_crlf(ip);
		break;

	case HTS:	/* set horizontal tab */
		if (ip->curx < ip->cols)
			ip->tabs[ip->curx] = 1;
		break;

	case RI:	/* reverse index */
		ite_rlf(ip);
		break;

	case SS2:	/* go into G2 for one character */
		ip->save_GL = ip->GR;	/* GL XXX EUC */
		ip->GR = &ip->G2;	/* GL XXX */
		break;

	case SS3:	/* go into G3 for one character */
		ip->save_GL = ip->GR;	/* GL XXX EUC */
		ip->GR = &ip->G3;	/* GL XXX */
		break;

	case DCS:	/* device control string introducer */
		ip->escape = DCS;
#if defined(ITE_SIXEL)
		ip->dcs_cmd = DCS_START;
#endif
		ip->ap = ip->argbuf;
		break;

	case CSI:	/* control sequence introducer */
		ip->escape = CSI;
		ip->ap = ip->argbuf;
		break;

	case ST:	/* string terminator */
		/* ignore, if not used as terminator */
		break;

	case OSC:	/* introduces OS command. */
		/* Ignore everything upto ST */
		ip->escape = OSC;
		break;

	case PM:	/* privacy message */
		/* ignore everything upto ST */
		ip->escape = PM;
		break;

	case APC:	/* application program command */
		/* ignore everything upto ST */
		ip->escape = APC;
		break;

	case DEL:
		break;

	default:
		if (ip->save_char == 0 &&
		    (*((c & 0x80) ? ip->GR : ip->GL) & CSET_MULTI) != 0) {
			ip->save_char = c;
			break;
		}
		if (ip->imode != 0)
			ite_inchar(ip, ip->save_char ? 2 : 1);
		iteprecheckwrap(ip);
#ifdef DO_WEIRD_ATTRIBUTES
		if ((ip->attribute & ATTR_INV) || attrtest(ip, ATTR_INV)) {
			attrset(ip, ATTR_INV);
			SUBR_PUTC(ip, c, ip->cury, ip->curx, ATTR_INV);
		}
		else
			SUBR_PUTC(ip, c, ip->cury, ip->curx, ATTR_NOR);
#else
		SUBR_PUTC(ip, c, ip->cury, ip->curx, ip->attribute);
#endif
		/*SUBR_CURSOR(ip, DRAW_CURSOR);*/
		itecheckwrap(ip);
		if (ip->save_char) {
			itecheckwrap(ip);
			ip->save_char = 0;
		}
		if (ip->save_GL) {
			/*
			 * reset single shift
			 */
			ip->GR = ip->save_GL;
			ip->save_GL = 0;
		}
		break;
	}
}

#if defined(ITE_SIXEL)
/*
 * Handle DCS.
 * 0:  return in the caller.
 * !0: break the switch-case in the caller.
 */
static int
ite_dcs(const int c, struct ite_softc *ip)
{
	static const uint32_t table[64] = {
		0x000000, 0x000001, 0x000010, 0x000011,
		0x000100, 0x000101, 0x000110, 0x000111,
		0x001000, 0x001001, 0x001010, 0x001011,
		0x001100, 0x001101, 0x001110, 0x001111,
		0x010000, 0x010001, 0x010010, 0x010011,
		0x010100, 0x010101, 0x010110, 0x010111,
		0x011000, 0x011001, 0x011010, 0x011011,
		0x011100, 0x011101, 0x011110, 0x011111,
		0x100000, 0x100001, 0x100010, 0x100011,
		0x100100, 0x100101, 0x100110, 0x100111,
		0x101000, 0x101001, 0x101010, 0x101011,
		0x101100, 0x101101, 0x101110, 0x101111,
		0x110000, 0x110001, 0x110010, 0x110011,
		0x110100, 0x110101, 0x110110, 0x110111,
		0x111000, 0x111001, 0x111010, 0x111011,
		0x111100, 0x111101, 0x111110, 0x111111,
	};

	switch (ip->dcs_cmd) {
	case DCS_DISCARD:
		/* discard sixel cause kernel message interrupted */
		switch (c) {
		case '-':
			/* restart from next SIXEL line */
			ite_lf(ip);
			goto sixel_restart;

		case CAN:
		case SUB:
			/* SUB should also display a reverse question mark... */
			ip->escape = 0;
			return 0;

		case ESC:
			ip->escape = ESC;
			return 0;

		default:
			return 0;
		}
		break;

	case DCS_START:
		/* the biggie... */
		switch (c) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case ';':
		case '$':
			if (ip->ap < ip->argbuf + MAX_ARGSIZE)
				*ip->ap++ = c;
			return 0;

		case 'q':
		{
			char *cp;

			/* init sixel */
			/*
			 * DCS <P1> ; <P2> ; <P3> q
			 * P1 is aspect ratio, XXX not supported.
			 * P2 is bgcolor mode.
			 *  0..2: bgcolor mode, XXX not supported here.
			 *  bit2 means 'OR'ed color mode.
			 *  This is an original extension.
			 */
			ip->ap = ip->argbuf;
			cp = strchr(ip->ap, ';');
			if (cp != NULL) {
				int mode;
				mode = atoi(cp + 1) - '0';
				ip->decsixel_ormode = (mode & 4);
			} else {
				ip->decsixel_ormode = 0;
			}
sixel_restart:
			ip->dcs_cmd = DCS_SIXEL;
			ip->decsixel_state = DECSIXEL_INIT;
			ip->decsixel_ph = MAX_SIXEL_WIDTH;
			ip->decsixel_x = 0;
			ip->decsixel_y = 0;
			ip->decsixel_repcount = 0;
			ip->decsixel_color = ip->fgcolor;
			memset(ip->decsixel_buf, 0, sizeof(ip->decsixel_buf));
			return 0;
		}

		case CAN:
		case SUB:
			/* SUB should also display a reverse question mark... */
			ip->escape = 0;
			return 0;

		case ESC:
			ip->escape = ESC;
			return 0;

		default:
			return 0;
		}
		break;

	case DCS_SIXEL:
sixel_loop:
		switch (ip->decsixel_state) {
		case DECSIXEL_INIT:
			switch (c) {
			case CAN:
			case SUB:
				/*
				 * SUB should also display a reverse question
				 * mark...
				 */
				ip->escape = 0;
				return 0;

			case ESC:
				ip->escape = ESC;
				return 0;

			case DECSIXEL_REPEAT:
				ip->decsixel_state = c;
				ip->decsixel_repcount = 0;
				return 0;

			case DECSIXEL_RASTER:
			case DECSIXEL_COLOR:
				ip->decsixel_state = c;
				ip->ap = ip->argbuf;
				return 0;

			case '$':	/* CR */
				ip->decsixel_x = 0;
				return 0;

			case '-':	/* LF */
				/*
				 * XXX
				 * FONTHEIGHT is defined in ite_tv.c, not here..
				 */
				if (ip->decsixel_y + 6 > 15) {
					ite_lf(ip);
					ip->decsixel_y -= 16;
				}
				SUBR_SIXEL(ip, ip->cury, ip->curx);
				memset(ip->decsixel_buf, 0,
				    sizeof(ip->decsixel_buf));
				ip->decsixel_x = 0;
				ip->decsixel_y += 6;
				return 0;

			default:
				if ('?' <= c && c <= '~'
				    && ip->decsixel_x < MAX_SIXEL_WIDTH) {
					uint32_t d;
					d = table[c - '?'] * ip->decsixel_color;
					ip->decsixel_buf[ip->decsixel_x] |= d;
					ip->decsixel_x++;
				} else {
					/* ignore */
				}
				return 0;
			}
			break;

		case DECSIXEL_REPEAT:
			if ('0' <= c && c <= '9') {
				ip->decsixel_repcount =
				    ip->decsixel_repcount * 10 + (c - '0');
			} else if ('?' <= c && c <= '~') {
				uint32_t d;
				int i;
				int cnt = MIN(ip->decsixel_repcount,
				    MAX_SIXEL_WIDTH - ip->decsixel_x);
				d = table[c - '?'] * ip->decsixel_color;
				for (i = 0; i < cnt; i++) {
					ip->decsixel_buf[ip->decsixel_x + i] |=
					    d;
				}
				ip->decsixel_x += cnt;
				ip->decsixel_state = DECSIXEL_INIT;
			} else {
				/* invalid ? */
				ip->decsixel_state = DECSIXEL_INIT;
			}
			return 0;

		case DECSIXEL_RASTER:
		case DECSIXEL_RASTER_PAD:
		case DECSIXEL_RASTER_PH:
		case DECSIXEL_RASTER_PV:
			switch (c) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				if (ip->ap < ip->argbuf + MAX_ARGSIZE)
					*ip->ap++ = c;
				return 0;

			case ';':
			default:
				switch (ip->decsixel_state) {
				case DECSIXEL_RASTER:
					/* ignore PAN */
					ip->ap = ip->argbuf;
					ip->decsixel_state =
					    DECSIXEL_RASTER_PAD;
					return 0;

				case DECSIXEL_RASTER_PAD:
					/* ignore PAD */
					ip->ap = ip->argbuf;
					ip->decsixel_state = DECSIXEL_RASTER_PH;
					return 0;

				case DECSIXEL_RASTER_PH:
					ip->decsixel_ph = ite_zargnum(ip);
					ip->ap = ip->argbuf;
					ip->decsixel_state = DECSIXEL_RASTER_PV;
					return 0;

				case DECSIXEL_RASTER_PV:
					/* ignore PV */
					ip->decsixel_state = DECSIXEL_INIT;
					/* c is a next sequence char. */
					goto sixel_loop;

				default:
					/* NOTREACHED */
					return 0;
				}
			}
			return 0;

		case DECSIXEL_COLOR:
			switch (c) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case ';':
				if (ip->ap < ip->argbuf + MAX_ARGSIZE)
					*ip->ap++ = c;
				return 0;

			default:
				*ip->ap = '\0';
				if (strchr(ip->argbuf, ';')) {
					/* ignore the palette definition. */
				} else {
					/* otherwise, it specifies color. */
					ip->decsixel_color =
					    ite_zargnum(ip) & 7;
				}
				ip->decsixel_state = DECSIXEL_INIT;
				ip->ap = ip->argbuf;
				/* c is a next sequence char. */
				goto sixel_loop;
			}
			return 0;
		}
		break;
	}

	/* Continue in caller's switch-case. */
	return 1;
}
#endif /* ITE_SIXEL */

static void
iteprecheckwrap(struct ite_softc *ip)
{

	if (ip->auto_wrap && ip->curx + (ip->save_char ? 1 : 0) == ip->cols) {
		ip->curx = 0;
		clr_attr(ip, ATTR_INV);
		if (++ip->cury >= ip->bottom_margin + 1) {
			ip->cury = ip->bottom_margin;
			/*SUBR_CURSOR(ip, MOVE_CURSOR);*/
			SUBR_SCROLL(ip, ip->top_margin + 1, 0, 1, SCROLL_UP);
			ite_clrtoeol(ip);
		} /*else
			SUBR_CURSOR(ip, MOVE_CURSOR);*/
	}
}

static void
itecheckwrap(struct ite_softc *ip)
{

#if 0
	if (++ip->curx == ip->cols) {
		if (ip->auto_wrap) {
			ip->curx = 0;
			clr_attr(ip, ATTR_INV);
			if (++ip->cury >= ip->bottom_margin + 1) {
				ip->cury = ip->bottom_margin;
				SUBR_CURSOR(ip, MOVE_CURSOR);
				SUBR_SCROLL(ip, ip->top_margin + 1, 0, 1,
				    SCROLL_UP);
				ite_clrtoeol(ip);
				return;
			}
		} else
			/* stay there if no autowrap.. */
			ip->curx--;
	}
#else
	if (ip->curx < ip->cols) {
		ip->curx++;
		/*SUBR_CURSOR(ip, MOVE_CURSOR);*/
	}
#endif
}

/*
 * A convertion table from DEC special graphics characters to ASCII characters.
 * Mostly for box drawing on sysinst(8).
 */
const uint8_t ite_decgraph2ascii[128] = {
	/* same as ASCII from 0x00 to 0x5e */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e,

	/* special graphics characters from 0x5f to 0x7e */
	' ',	/* 0x5f NBSP */
	'*',	/* 0x60 diamond */
	' ',	/* 0x61 medium shade */
	' ',	/* 0x62 HT */
	' ',	/* 0x63 FF */
	' ',	/* 0x64 CR */
	' ',	/* 0x65 LF */
	' ',	/* 0x66 degree symbol */
	' ',	/* 0x67 plus-minus sign */
	' ',	/* 0x68 NL */
	' ',	/* 0x69 VT */
	'+',	/* 0x6a box drawings up left */
	'+',	/* 0x6b box drawings down left */
	'+',	/* 0x6c box drawings down right */
	'+',	/* 0x6d box drawings up right */
	'+',	/* 0x6e box drawings vertical horizontal */
	'~',	/* 0x6f scan line 1 */
	'-',	/* 0x70 scan line 3 */
	'-',	/* 0x71 scan line 5 */
	'-',	/* 0x72 scan line 7 */
	'_',	/* 0x73 scan line 9 */
	'+',	/* 0x74 box drawings vertical right */
	'+',	/* 0x75 box drawings vertical left */
	'+',	/* 0x76 box drawings horizontal up */
	'+',	/* 0x77 box drawings horizontal down */
	'|',	/* 0x78 box drawings vertical */
	'<',	/* 0x79 less than or equal to */
	'>',	/* 0x7a greater than or equal to */
	' ',	/* 0x7b pi */
	' ',	/* 0x7c not equal */
	' ',	/* 0x7d pound sign */
	'.',	/* 0x7e middle dot */
	/* end of special graphics characters */
	0x7f
};

#endif

#if NITE > 0 && NKBD > 0

/*
 * Console functions
 */
#include <dev/cons.h>
extern void kbdenable(int);
extern int kbdcngetc(void);

/*
 * Return a priority in consdev->cn_pri field highest wins.  This function
 * is called before any devices have been probed.
 */
void
itecnprobe(struct consdev *cd)
{
	int maj;

	/* locate the major number */
	maj = cdevsw_lookup_major(&ite_cdevsw);

	/*
	 * return priority of the best ite (already picked from attach)
	 * or CN_DEAD.
	 */
	if (con_itesoftc.grf == NULL)
		cd->cn_pri = CN_DEAD;
	else {
		con_itesoftc.flags = (ITE_ALIVE|ITE_CONSOLE);
		/*
		 * hardcode the minor number.
		 * currently we support only one ITE, it is enough for now.
		 */
		con_itesoftc.isw = &itesw[0];
		cd->cn_pri = CN_INTERNAL;
		cd->cn_dev = makedev(maj, 0);
	}
}

void
itecninit(struct consdev *cd)
{
	struct ite_softc *ip;

	ip = getitesp(cd->cn_dev);
	iteinit(cd->cn_dev);	       /* init console unit */
	ip->flags |= ITE_ACTIVE | ITE_ISCONS;
	kbdenable(0);
	mfp_send_usart(0x49);
}

/*
 * itecnfinish() is called in ite_init() when the device is
 * being probed in the normal fashion, thus we can finish setting
 * up this ite now that the system is more functional.
 */
void
itecnfinish(struct ite_softc *ip)
{
	static int done;

	if (done)
		return;
	done = 1;
}

/*ARGSUSED*/
int
itecngetc(dev_t dev)
{
	int c;

	do {
		c = kbdcngetc();
		c = ite_cnfilter(c);
	} while (c == -1);
	return (c);
}

void
itecnputc(dev_t dev, int c)
{
	static int paniced = 0;
	struct ite_softc *ip = getitesp(dev);
	char ch = c;
#ifdef ITE_KERNEL_ATTR
	short save_attribute;
#endif
#if defined(ITE_SIXEL)
	int save_escape;
#endif

	if (panicstr && !paniced &&
	    (ip->flags & (ITE_ACTIVE|ITE_INGRF)) != ITE_ACTIVE) {
		(void) iteon(dev, 3);
		paniced = 1;
	}

#if defined(ITE_SIXEL)
	save_escape = ip->escape;
	if (ip->escape == DCS) {
		ip->escape = 0;
		ip->dcs_cmd = DCS_DISCARD;
	}
#endif
#ifdef ITE_KERNEL_ATTR
	save_attribute = ip->attribute;
	ip->attribute = ITE_KERNEL_ATTR;
#endif
	ite_putstr(&ch, 1, dev);
#ifdef ITE_KERNEL_ATTR
	ip->attribute = save_attribute;
#endif
#if defined(ITE_SIXEL)
	if (ip->escape == 0) {
		ip->escape = save_escape;
	}
#endif
}
#endif
