/*	$NetBSD: if_ie_vme.c,v 1.24 2024/12/20 23:52:00 tsutsui Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Gordon W. Ross.
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

/*
 * Machine-dependent glue for the Intel Ethernet (ie) driver.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: if_ie_vme.c,v 1.24 2024/12/20 23:52:00 tsutsui Exp $");

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_ether.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_inarp.h>
#endif

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/dvma.h>
#include <machine/idprom.h>
#include <machine/vmparam.h>

#include "i82586.h"
#include "if_iereg.h"
#include "if_ievar.h"

static void ie_vmereset(struct ie_softc *);
static void ie_vmeattend(struct ie_softc *);
static void ie_vmerun(struct ie_softc *);

/*
 * zero/copy functions: OBIO can use the normal functions, but VME
 *    must do only byte or half-word (16 bit) accesses...
 */
static void *wmemcpy(void *, const void *, size_t);
static void *wmemset(void *, int, size_t);


/*
 * New-style autoconfig attachment
 */

static int  ie_vme_match(device_t, cfdata_t, void *);
static void ie_vme_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(ie_vme, sizeof(struct ie_softc),
    ie_vme_match, ie_vme_attach, NULL, NULL);

static int
ie_vme_match(device_t parent, cfdata_t cf, void *args)
{
	struct confargs *ca = args;

	/* No default VME address. */
	if (ca->ca_paddr == -1)
		return 0;

	/* Make sure something is there... */
	if (bus_peek(ca->ca_bustype, ca->ca_paddr, 2) == -1)
		return 0;

	/* Default interrupt priority. */
	if (ca->ca_intpri == -1)
		ca->ca_intpri = 3;

	return 1;
}

/*
 * *note*: we don't detect the difference between a VME3E and
 * a multibus/vme card.   if you want to use a 3E you'll have
 * to fix this.
 */
void
ie_vme_attach(device_t parent, device_t self, void *args)
{
	struct ie_softc *sc = device_private(self);
	struct confargs *ca = args;
	volatile struct ievme *iev;
	u_long  rampaddr;
	int     lcv, off;

	sc->sc_dev = self;
	sc->hard_type = IE_VME;
	sc->reset_586 = ie_vmereset;
	sc->chan_attn = ie_vmeattend;
	sc->run_586 = ie_vmerun;
	sc->sc_memcpy = wmemcpy;
	sc->sc_memset = wmemset;

	/*
	 * There is 64K of memory on the VME board.
	 * (determined by hardware - NOT configurable!)
	 */
	sc->sc_msize = 0x10000; /* MEMSIZE 64K */

	/* Map in the board control regs. */
	sc->sc_reg = bus_mapin(ca->ca_bustype, ca->ca_paddr,
						   sizeof(struct ievme));
	iev = (volatile struct ievme *) sc->sc_reg;

	/*
	 * Find and map in the board memory.
	 */
	/* top 12 bits */
	rampaddr = ca->ca_paddr & 0xfff00000;
	/* 4 more */
	rampaddr |= ((iev->status & IEVME_HADDR) << 16);
	sc->sc_maddr = bus_mapin(ca->ca_bustype, rampaddr, sc->sc_msize);

	/*
	 * On this hardware, the i82586 address is just
	 * masked to 16 bits, so sc_iobase == sc_maddr
	 */
	sc->sc_iobase = sc->sc_maddr;

	/*
	 * Set up on-board mapping registers for linear map.
	 */
	iev->pectrl |= IEVME_PARACK; /* clear to start */
	for (lcv = 0; lcv < IEVME_MAPSZ; lcv++)
		iev->pgmap[lcv] = IEVME_SBORDR | IEVME_OBMEM | lcv;
	(sc->sc_memset)(sc->sc_maddr, 0, sc->sc_msize);

	/*
	 * Set the System Configuration Pointer (SCP).
	 * Its location is system-dependent because the
	 * i82586 reads it from a fixed physical address.
	 * On this hardware, the i82586 address is just
	 * masked down to 16 bits, so the SCP is found
	 * at the end of the RAM on the VME board.
	 */
	off = IE_SCP_ADDR & 0xFFFF;
	sc->scp = (volatile void *)((char *)sc->sc_maddr + off);

	/*
	 * The rest of ram is used for buffers, etc.
	 */
	sc->buf_area = sc->sc_maddr;
	sc->buf_area_sz = off;

	/* Set the ethernet address. */
	idprom_etheraddr(sc->sc_addr);

	/* Do machine-independent parts of attach. */
	ie_attach(sc);

	/* Install interrupt handler. */
	isr_add_vectored(ie_intr, sc, ca->ca_intpri, ca->ca_intvec);
}


/*
 * MULTIBUS/VME support
 */

void
ie_vmeattend(struct ie_softc *sc)
{
	volatile struct ievme *iev = (struct ievme *)sc->sc_reg;

	iev->status |= IEVME_ATTEN;	/* flag! */
	iev->status &= ~IEVME_ATTEN;	/* down. */
}

void
ie_vmereset(struct ie_softc *sc)
{
	volatile struct ievme *iev = (struct ievme *)sc->sc_reg;

	iev->status = IEVME_RESET;
	delay(20);
	iev->status = (IEVME_ONAIR | IEVME_IENAB | IEVME_PEINT);
}

void
ie_vmerun(struct ie_softc *sc)
{

	/* do it all in reset */
}

/*
 * wmemcpy/wmemset - like memcpy/memset but largest access is 16-bits,
 * and also does byte swaps...
 * XXX - Would be nice to have asm versions in some library...
 */

static void *
wmemset(void *vb, int val, size_t l)
{
	uint8_t *b = vb;
	uint8_t *be = b + l;
	uint16_t *sp;

	if (l == 0)
		return vb;

	/* front, */
	if ((uint32_t)b & 1)
		*b++ = val;

	/* back, */
	if (b != be && ((uint32_t)be & 1) != 0) {
		be--;
		*be = val;
	}

	/* and middle. */
	sp = (uint16_t *)b;
	while (sp != (uint16_t *)be)
		*sp++ = val;

	return vb;
}

static void *
wmemcpy(void *dst, const void *src, size_t l)
{
	const uint8_t *b1e, *b1 = src;
	uint8_t *b2 = dst;
	const uint16_t *sp;
	int bstore = 0;

	if (l == 0)
		return dst;

	/* front, */
	if ((uint32_t)b1 & 1) {
		*b2++ = *b1++;
		l--;
	}

	/* middle, */
	sp = (const uint16_t *)b1;
	b1e = b1 + l;
	if (l & 1)
		b1e--;
	bstore = (uint32_t)b2 & 1;

	while (sp < (const uint16_t *)b1e) {
		if (bstore) {
			b2[1] = *sp & 0xff;
			b2[0] = *sp >> 8;
		} else
			*((uint16_t *)b2) = *sp;
		sp++;
		b2 += 2;
	}

	/* and back. */
	if (l & 1)
		*b2 = *b1e;

	return dst;
}
