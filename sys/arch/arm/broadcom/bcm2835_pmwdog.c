/*	$NetBSD: bcm2835_pmwdog.c,v 1.3 2024/06/10 06:03:48 mlelstv Exp $	*/

/*-
 * Copyright (c) 2012, 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: bcm2835_pmwdog.c,v 1.3 2024/06/10 06:03:48 mlelstv Exp $");


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/timetc.h>
#include <sys/wdog.h>
#include <sys/bus.h>

#include <dev/sysmon/sysmonvar.h>

#include <arm/broadcom/bcm2835reg.h>
#include <arm/broadcom/bcm2835_intr.h>
#include <arm/broadcom/bcm2835_pmwdogvar.h>

#include <dev/fdt/fdtvar.h>

#ifndef BCM2835_PM_DEFAULT_PERIOD
#define BCM2835_PM_DEFAULT_PERIOD	15	/* seconds */
#endif

#define	 BCM2835_PM_PASSWORD		0x5a000000
#define	 BCM2835_PM_PASSWORD_MASK	0xff000000

#define	BCM2835_PM_RSTC		0x1c
#define	 BCM2835_PM_RSTC_CONFIGMASK	0x00000030
#define	 BCM2835_PM_RSTC_FULL_RESET	0x00000020
#define	 BCM2835_PM_RSTC_RESET		0x00000102

#define	BCM2835_PM_RSTS		0x20
#define  BCM2835_PM_RSTS_PART(x) \
		((uint16_t)((x) & 0x01) << 0 | (uint16_t)((x) & 0x02) << 1 | \
		 (uint16_t)((x) & 0x04) << 2 | (uint16_t)((x) & 0x08) << 3 | \
		 (uint16_t)((x) & 0x10) << 4 | (uint16_t)((x) & 0x20) << 5)

#define	BCM2835_PM_WDOG		0x24
#define	 BCM2835_PM_WDOG_TIMEMASK	0x000fffff

struct bcm2835pmwdog_softc {
	device_t sc_dev;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	struct sysmon_wdog sc_smw;
};

static struct bcm2835pmwdog_softc *bcm2835pmwdog_sc;

static int bcmpmwdog_match(device_t, cfdata_t, void *);
static void bcmpmwdog_attach(device_t, device_t, void *);

static void bcmpmwdog_set_timeout(struct bcm2835pmwdog_softc *, uint32_t);

static int bcmpmwdog_setmode(struct sysmon_wdog *);
static int bcmpmwdog_tickle(struct sysmon_wdog *);

/* fdt power controller */
static void bcm2835_power_reset(device_t);
static void bcm2835_power_poweroff(device_t);

CFATTACH_DECL_NEW(bcmpmwdog_fdt, sizeof(struct bcm2835pmwdog_softc),
    bcmpmwdog_match, bcmpmwdog_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "brcm,bcm2835-pm-wdt" },
	DEVICE_COMPAT_EOL
};

static struct fdtbus_power_controller_func bcmpmwdog_power_funcs = {
	.reset = bcm2835_power_reset,
	.poweroff = bcm2835_power_poweroff
};

/* ARGSUSED */
static int
bcmpmwdog_match(device_t parent, cfdata_t match, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
bcmpmwdog_attach(device_t parent, device_t self, void *aux)
{
	struct bcm2835pmwdog_softc *sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;

	aprint_naive("\n");
	aprint_normal(": Power management, Reset and Watchdog controller\n");

	if (bcm2835pmwdog_sc == NULL)
		bcm2835pmwdog_sc = sc;

	sc->sc_dev = self;
	sc->sc_iot = faa->faa_bst;

	bus_addr_t addr;
	bus_size_t size;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get register address\n");
		return;
	}

	if (bus_space_map(faa->faa_bst, addr, size, 0, &sc->sc_ioh) != 0) {
		aprint_error_dev(sc->sc_dev, "unable to map device\n");
		return;
	}

	/* watchdog */
	sc->sc_smw.smw_name = device_xname(sc->sc_dev);
	sc->sc_smw.smw_cookie = sc;
	sc->sc_smw.smw_setmode = bcmpmwdog_setmode;
	sc->sc_smw.smw_tickle = bcmpmwdog_tickle;
	sc->sc_smw.smw_period = BCM2835_PM_DEFAULT_PERIOD;
	if (sysmon_wdog_register(&sc->sc_smw) != 0)
		aprint_error_dev(self, "couldn't register watchdog\n");

	fdtbus_register_power_controller(self, phandle,
	    &bcmpmwdog_power_funcs);
}

static void
bcmpmwdog_set_timeout(struct bcm2835pmwdog_softc *sc, uint32_t ticks)
{
	uint32_t tmp, rstc, wdog;

	tmp = bus_space_read_4(sc->sc_iot, sc->sc_ioh, BCM2835_PM_RSTC);

	rstc = wdog = BCM2835_PM_PASSWORD;

	rstc |= tmp & ~BCM2835_PM_RSTC_CONFIGMASK;
	rstc |= BCM2835_PM_RSTC_FULL_RESET;

	wdog |= ticks & BCM2835_PM_WDOG_TIMEMASK;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, BCM2835_PM_WDOG, wdog);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, BCM2835_PM_RSTC, rstc);
}

static int
bcmpmwdog_setmode(struct sysmon_wdog *smw)
{
	struct bcm2835pmwdog_softc *sc = smw->smw_cookie;
	int error = 0;

	if ((smw->smw_mode & WDOG_MODE_MASK) == WDOG_MODE_DISARMED) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, BCM2835_PM_RSTC,
		    BCM2835_PM_PASSWORD | BCM2835_PM_RSTC_RESET);
	} else {
		if (smw->smw_period == WDOG_PERIOD_DEFAULT)
			smw->smw_period = BCM2835_PM_DEFAULT_PERIOD;
		if (smw->smw_period > (BCM2835_PM_WDOG_TIMEMASK >> 16))
			return EINVAL;
		error = bcmpmwdog_tickle(smw);
	}

	return error;
}

static void
bcmpmwdog_set_partition(struct bcm2835pmwdog_softc *sc, uint8_t part)
{
	uint32_t tmp;

	tmp = bus_space_read_4(sc->sc_iot, sc->sc_ioh, BCM2835_PM_RSTS);
	tmp &= ~(BCM2835_PM_PASSWORD_MASK | BCM2835_PM_RSTS_PART(~0));
	tmp |= BCM2835_PM_PASSWORD | BCM2835_PM_RSTS_PART(part);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, BCM2835_PM_RSTS, tmp);
}

static int
bcmpmwdog_tickle(struct sysmon_wdog *smw)
{
	struct bcm2835pmwdog_softc *sc = smw->smw_cookie;
	uint32_t timeout = smw->smw_period << 16;

	bcmpmwdog_set_timeout(sc, timeout);

	return 0;
}

static void
bcm2835_restart(struct bcm2835pmwdog_softc *sc, int partition)
{
	uint32_t timeout = 10;

	bcmpmwdog_set_partition(sc, partition);
	bcmpmwdog_set_timeout(sc, timeout);
}

void
bcm2835_system_reset(void)
{
	struct bcm2835pmwdog_softc *sc = bcm2835pmwdog_sc;

	bcm2835_restart(sc, 0);
}

static void
bcm2835_power_reset(device_t self)
{
	struct bcm2835pmwdog_softc *sc = device_private(self);

	bcm2835_restart(sc, 0);

	for (;;) continue;
	/* NOTREACHED */
}

static void
bcm2835_power_poweroff(device_t self)
{
	struct bcm2835pmwdog_softc *sc = device_private(self);

	/* Boot from partition 63 is magic to halt boot process */
	bcm2835_restart(sc, 63);

	for (;;) continue;
	/* NOTREACHED */
}

