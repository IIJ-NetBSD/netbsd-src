/*	$NetBSD: memerr.c,v 1.23 2024/12/20 23:52:00 tsutsui Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)memreg.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: memerr.c,v 1.23 2024/12/20 23:52:00 tsutsui Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/idprom.h>
#include <machine/pte.h>

#include <sun3/sun3/machdep.h>
#include <sun3/dev/memerr.h>
/* #include <sun3/dev/eccreg.h> - not yet */

#define	ME_PRI	7	/* Interrupt level (NMI) */

enum memerr_type { ME_PAR = 0, ME_ECC = 1 };

struct memerr_softc {
	device_t sc_dev;
	struct memerr *sc_reg;
	enum memerr_type sc_type;
	const char *sc_typename;	/* "Parity" or "ECC" */
	const char *sc_csrbits;		/* how to print csr bits */
	/* XXX: counters? */
};

static int  memerr_match(device_t, cfdata_t, void *);
static void memerr_attach(device_t, device_t, void *);
static int  memerr_interrupt(void *);
static void memerr_correctable(struct memerr_softc *);

CFATTACH_DECL_NEW(memerr, sizeof(struct memerr_softc),
    memerr_match, memerr_attach, NULL, NULL);

static int memerr_attached;

static int
memerr_match(device_t parent, cfdata_t cf, void *args)
{
	struct confargs *ca = args;

	/* This driver only supports one instance. */
	if (memerr_attached)
		return 0;

	/* Make sure there is something there... */
	if (bus_peek(ca->ca_bustype, ca->ca_paddr, 1) == -1)
		return 0;

	/* Default interrupt priority. */
	if (ca->ca_intpri == -1)
		ca->ca_intpri = ME_PRI;

	return 1;
}

static void
memerr_attach(device_t parent, device_t self, void *args)
{
	struct memerr_softc *sc = device_private(self);
	struct confargs *ca = args;
	struct memerr *mer;

	sc->sc_dev = self;

	/*
	 * Which type of memory subsystem do we have?
	 */
	switch (cpu_machine_id) {
	case ID_SUN3_160:		/* XXX: correct? */
	case ID_SUN3_260:
	case ID_SUN3X_470:
		sc->sc_type = ME_ECC;
		sc->sc_typename = "ECC";
		sc->sc_csrbits = ME_ECC_STR;
		break;

	default:
		sc->sc_type = ME_PAR;
		sc->sc_typename = "Parity";
		sc->sc_csrbits = ME_PAR_STR;
		break;
	}
	aprint_normal(": (%s memory)\n", sc->sc_typename);

	mer = bus_mapin(ca->ca_bustype, ca->ca_paddr, sizeof(*mer));
	if (mer == NULL)
		panic("%s: can not map register", device_xname(self));
	sc->sc_reg = mer;

	/* Install interrupt handler. */
	isr_add_autovect(memerr_interrupt, sc, ca->ca_intpri);

	/* Enable error interrupt (and checking). */
	if (sc->sc_type == ME_PAR)
		mer->me_csr = ME_CSR_IENA | ME_PAR_CHECK;
	else {
		/*
		 * XXX:  Some day, figure out how to decode
		 * correctable errors and set ME_ECC_CE_ENA
		 * here so we can log them...
		 */
		mer->me_csr = ME_CSR_IENA; /* | ME_ECC_CE_ENA */
	}
	memerr_attached = 1;
}

/*****************************************************************
 * Functions for ECC memory
 *****************************************************************/

static int
memerr_interrupt(void *arg)
{
	struct memerr_softc *sc = arg;
	volatile struct memerr *me = sc->sc_reg;
	uint8_t csr, ctx;
	u_int pa, va;
	int pte;
	uint8_t bits[64];

	csr = me->me_csr;
	if ((csr & ME_CSR_IPEND) == 0)
		return 0;

	va = me->me_vaddr;
 	ctx = (va >> 28) & 0xF;
	va &= 0x0FFFffff;
	pte = get_pte(va);
	pa = PG_PA(pte);

	printf("\nMemory error on %s cycle!\n",
	    (ctx & 8) ? "DVMA" : "CPU");
	printf(" ctx=%d, vaddr=0x%x, paddr=0x%x\n",
	    (ctx & 7), va, pa);
	snprintb(bits, sizeof(bits), sc->sc_csrbits, csr);
	printf(" csr=%s\n", bits);

	/*
	 * If we have parity-checked memory, there is
	 * not much to be done.  Any error is fatal.
	 */
	if (sc->sc_type == ME_PAR) {
		if (csr & ME_PAR_EMASK) {
			/* Parity errors are fatal. */
			goto die;
		}
		/* The IPEND bit was set, but no error bits. */
		goto noerror;
	}

	/*
	 * We have ECC memory.  More complicated...
	 */
	if (csr & (ME_ECC_WBTMO | ME_ECC_WBERR)) {
		printf(" write-back failed, pte=0x%x\n", pte);
		goto die;
	}
	if (csr & ME_ECC_UE) {
		printf(" uncorrectable ECC error\n");
		goto die;
	}
	if (csr & ME_ECC_CE) {
		/* Just log this and continue. */
		memerr_correctable(sc);
		goto recover;
	}
	/* The IPEND bit was set, but no error bits. */
	goto noerror;

die:
	panic("all bets are off...");

noerror:
	printf("memerr: no error bits set?\n");

recover:
	/* Clear the error by writing the address register. */
	me->me_vaddr = 0;
	return 1;
}

/*
 * Announce (and log) a correctable ECC error.
 * Need to look at the ECC syndrome register on
 * the memory board that caused the error...
 */
void
memerr_correctable(struct memerr_softc *sc)
{

	/* XXX: Not yet... */
}
