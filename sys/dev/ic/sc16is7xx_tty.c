/*	$NetBSD: sc16is7xx_tty.c,v 1.1 2025/10/24 23:16:11 brad Exp $	*/

/*
 * Copyright (c) 2025 Brad Spencer <brad@anduin.eldar.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: sc16is7xx_tty.c,v 1.1 2025/10/24 23:16:11 brad Exp $");

/* TTY specific common driver to the NXP SC16IS7xx UART bridge */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/tty.h>
#ifdef __REMOVE
#include <sys/sysctl.h>
#endif
#include <sys/systm.h>

#include <sys/bus.h>

#include <dev/ic/sc16is7xxreg.h>
#include <dev/ic/sc16is7xxvar.h>
#include <dev/ic/sc16is7xx_ttyvar.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

static int sc16is7xx_tty_match(device_t, cfdata_t, void *);
static void sc16is7xx_tty_attach(device_t, device_t, void *);
static int sc16is7xx_tty_detach(device_t, int);
#ifdef __REMOVE
static int sc16is7xx_tty_verify_prescale(SYSCTLFN_ARGS);
#endif

CFATTACH_DECL_NEW(sc16is7xx_tty, sizeof(struct sc16is7xx_tty_softc),
    sc16is7xx_tty_match, sc16is7xx_tty_attach, sc16is7xx_tty_detach, NULL);

#ifdef __REMOVE
int
sc16is7xx_tty_verify_prescale(SYSCTLFN_ARGS)
{
	struct sc16is7xx_tty_softc *sc;
	int error;
	bool t = false;
	struct sysctlnode node;

	node = *rnode;
	sc = node.sysctl_data;
	if (ISSET(sc->sc_com.sc_mcr, MCR_PRESCALE))
		t = true;
	node.sysctl_data = &t;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;

	if (t)
		SET(sc->sc_com.sc_mcr, MCR_PRESCALE);
	else
		CLR(sc->sc_com.sc_mcr, MCR_PRESCALE);

	return 0;
}

static int
sc16is7xx_tty_sysctl_init(struct sc16is7xx_tty_softc *sc)
{
	int error;
	const struct sysctlnode *cnode;
	int sysctlroot_num;

	if ((error = sysctl_createv(&sc->sc_sc16is7xx_tty_log, 0, NULL, &cnode,
	    0, CTLTYPE_NODE, device_xname(sc->sc_com.sc_dev),
	    SYSCTL_DESCR("sc16ix7xx tty controls"), NULL, 0, NULL, 0, CTL_HW,
	    CTL_CREATE, CTL_EOL)) != 0)
		return error;

	sysctlroot_num = cnode->sysctl_num;

	if ((error = sysctl_createv(&sc->sc_sc16is7xx_tty_log, 0, NULL, &cnode,
	    CTLFLAG_READWRITE, CTLTYPE_BOOL, "prescale",
	    SYSCTL_DESCR("Prescale"), sc16is7xx_tty_verify_prescale, 0,
	    (void *)sc, 0, CTL_HW, sysctlroot_num, CTL_CREATE,
	    CTL_EOL)) != 0)
		return error;

	return 0;
}
#endif

static int
sc16is7xx_tty_match(device_t parent, cfdata_t match, void *aux)
{
	return 1;
}

void
sc16is7xx_tty_attach(device_t parent, device_t self, void *aux)
{
	struct sc16is7xx_tty_attach_args *caa = aux;
	struct sc16is7xx_tty_softc *sc = device_private(self);
	struct sc16is7xx_sc *psc = device_private(parent);
#ifdef __REMOVE
	int error;
#endif

	sc->sc_com.sc_dev = self;

#ifdef __REMOVE
	if ((error = sc16is7xx_tty_sysctl_init(sc)) != 0) {
		aprint_error_dev(sc->sc_com.sc_dev, "Can't setup sysctl tree (%d)\n", error);
		goto out;
	}
#endif

	/* Set and then override the callouts to read and write the registers. */
	com_init_regs(&sc->sc_com.sc_regs, 0, 0, 0);
	sc->sc_com.sc_regs.cr_read = psc->sc_com_funcs->com_read_1;
	sc->sc_com.sc_regs.cr_write = psc->sc_com_funcs->com_write_1;
	sc->sc_com.sc_regs.cr_write_multi = psc->sc_com_funcs->com_write_multi_1;
	sc->sc_com.sc_regs.cr_channel = caa->aa_channel;

	psc->sc_funcs->copy_handles(psc, &sc->sc_com.sc_regs);

	/* We will get a 64 byte FIFO and hardware flow control if we use this */
	sc->sc_com.sc_type = COM_TYPE_SC16IS7XX;

	/* The master frequency is pushed down from the bus layer as both
	 * channels use the same clock.  However it needs to be set here as
	 * com(4) wants it. */
	sc->sc_com.sc_frequency = psc->sc_frequency;

	/* This will always be 0, as the polling will occur at the bus layer,
	 * if needed. */
	sc->sc_com.sc_poll_ticks = 0;

	/* Tell com(4) to run in a soft context whenever possible */
	SET(sc->sc_com.sc_hwflags, COM_HW_SOFTIRQ);

	com_attach_subr(&sc->sc_com);
#ifdef __REMOVE
out:
#endif

	return;
}

int
sc16is7xx_tty_detach(device_t self, int flags)
{
#ifdef __REMOVE	
	struct sc16is7xx_tty_softc *sc = device_private(self);
#endif
	int error;

#ifdef __REMOVE
	sysctl_teardown(&sc->sc_sc16is7xx_tty_log);
#endif

	if ((error = com_detach(self, flags)) != 0)
		return error;

	return 0;

}
