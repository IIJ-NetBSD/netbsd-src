/*	$NetBSD: obiofan.c,v 1.3 2025/07/01 06:36:35 macallan Exp $	*/

/*-
 * Copyright (c) 2021 Michael Lorenz
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

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/time.h>

#include <dev/ofw/openfirm.h>
#include <dev/sysmon/sysmonvar.h>
#include <machine/autoconf.h>
#include <macppc/dev/obiovar.h>

#include "opt_obiofan.h"

#ifdef OBIOFAN_DEBUG
#define DPRINTF printf
#else
#define DPRINTF if (0) printf
#endif

struct obiofan_softc {
	device_t sc_dev;
	struct sysmon_envsys	*sc_sme;
	envsys_data_t		sc_sensors[4];
	callout_t		sc_callout;
	time_t			sc_stamp;
	int 			sc_node;
	int 			sc_reg[4], sc_shift[4];
	int			sc_count[4], sc_rpm[4];
	int			sc_fans;
};

int obiofan_match(device_t, cfdata_t, void *);
void obiofan_attach(device_t, device_t, void *);
static void obiofan_refresh(struct sysmon_envsys *, envsys_data_t *);
static void obiofan_update(void *);

CFATTACH_DECL_NEW(obiofan, sizeof(struct obiofan_softc), obiofan_match,
	obiofan_attach, NULL, NULL);

int
obiofan_match(device_t parent, cfdata_t match, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "fans") == 0)
		return 1;

	return 0;
}

void
obiofan_attach(device_t parent, device_t self, void *aux)
{
	struct obiofan_softc *sc = device_private(self);
	struct confargs *ca = aux;
	char descr[32] = "fan";
	int node = ca->ca_node, tc_node, f, cnt;
	uint32_t regs[8], tc_regs[32];

	printf("\n");

	if (OF_getprop(node, "reg", regs, sizeof(regs)) <= 0)
		return;

	sc->sc_node = node;

#ifdef OBIOFAN_DEBUG
	int i;
	for (i = 0; i < 7; i += 2)
		printf("%02x: %08x\n", regs[i], obio_read_4(regs[i]));
#endif

	tc_node = OF_parent(node);

	if (OF_getprop(tc_node, "platform-do-getTACHCount", tc_regs, 128) <= 0) {
		aprint_error("no platform-do-getTACHCount\n");
		return;
	}

	/*
	 * XXX this is guesswork based on poking around & observation
	 *
	 * the parameter format seems to be:
	 * reg[0] - node number for the fan, or pointer into OF space
	 * reg[1] - 0x08000000
	 * reg[2] - 0x0000001a - varies with different platform-do-*
	 * reg[3] - register number in obio space
	 * reg[4] - bitmask in the register
	 * reg[5] - unknown
	 * reg[6] - unknown
	 */

	sc->sc_sme = sysmon_envsys_create();
	sc->sc_sme->sme_name = device_xname(self);
	sc->sc_sme->sme_cookie = sc;
	sc->sc_sme->sme_refresh = obiofan_refresh;

	/* now look for which fan register(s) we can use */
	f = OF_child(node);
	cnt = 0;
	while (f != 0) {
		int num = -1;
		OF_getprop(f, "reg", &num, 4);
		if (OF_getprop(f, "hwctrl-location", descr, 32) <= 0)
			goto skip;
		sc->sc_reg[cnt] = tc_regs[num * 7 + 3];
		sc->sc_shift[cnt] = ffs(tc_regs[num * 7 + 4]) - 1;

		DPRINTF("%s: %d %02x %d\n", descr, num, sc->sc_reg[cnt], sc->sc_shift[cnt]);

		sc->sc_stamp = time_second;
		sc->sc_count[cnt] = 
		    obio_read_4(sc->sc_reg[cnt]) >> sc->sc_shift[cnt];
		sc->sc_rpm[cnt] = -1;
	
		sc->sc_sensors[cnt].units = ENVSYS_SFANRPM;
		sc->sc_sensors[cnt].state = ENVSYS_SINVALID;
		sc->sc_sensors[cnt].private = cnt;
		strcpy(sc->sc_sensors[cnt].desc, descr);
		sysmon_envsys_sensor_attach(sc->sc_sme, &sc->sc_sensors[cnt]);
		cnt++;
skip:
		f = OF_peer(f);
	}
	sc->sc_fans = cnt;
	sysmon_envsys_register(sc->sc_sme);

	callout_init(&sc->sc_callout, 0);
	callout_setfunc(&sc->sc_callout, obiofan_update, sc);
	callout_schedule(&sc->sc_callout, mstohz(5000));
}

static void
obiofan_update(void *cookie)
{
	struct obiofan_softc *sc = cookie;
	time_t now = time_second, diff;
	int spin, spins, i;
	for (i = 0; i < sc->sc_fans; i++) {
		diff = now - sc->sc_stamp;
		if (diff < 5) return;
		spin = obio_read_4(sc->sc_reg[i]) >> sc->sc_shift[i];
		spins = (spin - sc->sc_count[i]) & 0xffff;
		sc->sc_rpm[i] = spins * 60 / diff;
		sc->sc_count[i] = spin;
		DPRINTF("%s %lld %d\n", __func__, diff, spins);
	}
	sc->sc_stamp = now;
	callout_schedule(&sc->sc_callout, mstohz(5000));
}

static void
obiofan_refresh(struct sysmon_envsys *sme, envsys_data_t *edata)
{
	struct obiofan_softc *sc = sme->sme_cookie;
	int i = edata->private;

	if (sc->sc_rpm[i] >= 0) {
		edata->state = ENVSYS_SVALID;
		edata->value_cur = sc->sc_rpm[i];
	}
}
