/*	$NetBSD: si_vme.c,v 1.33 2024/12/20 23:52:00 tsutsui Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Glass, David Jones, and Gordon W. Ross.
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
 * This file contains only the machine-dependent parts of the
 * Sun3 SCSI driver.  (Autoconfig stuff and DMA functions.)
 * The machine-independent parts are in ncr5380sbc.c
 *
 * Supported hardware includes:
 * Sun SCSI-3 on OBIO (Sun3/50,Sun3/60)
 * Sun SCSI-3 on VME (Sun3/160,Sun3/260)
 *
 * Could be made to support the Sun3/E if someone wanted to.
 *
 * Note:  Both supported variants of the Sun SCSI-3 adapter have
 * some really unusual "features" for this driver to deal with,
 * generally related to the DMA engine.  The OBIO variant will
 * ignore any attempt to write the FIFO count register while the
 * SCSI bus is in DATA_IN or DATA_OUT phase.  This is dealt with
 * by setting the FIFO count early in COMMAND or MSG_IN phase.
 *
 * The VME variant has a bit to enable or disable the DMA engine,
 * but that bit also gates the interrupt line from the NCR5380!
 * Therefore, in order to get any interrupt from the 5380, (i.e.
 * for reselect) one must clear the DMA engine transfer count and
 * then enable DMA.  This has the further complication that you
 * CAN NOT touch the NCR5380 while the DMA enable bit is set, so
 * we have to turn DMA back off before we even look at the 5380.
 *
 * What wonderfully whacky hardware this is!
 *
 * Credits, history:
 *
 * David Jones wrote the initial version of this module, which
 * included support for the VME adapter only. (no reselection).
 *
 * Gordon Ross added support for the OBIO adapter, and re-worked
 * both the VME and OBIO code to support disconnect/reselect.
 * (Required figuring out the hardware "features" noted above.)
 *
 * The autoconfiguration boilerplate came from Adam Glass.
 */

/*****************************************************************
 * VME functions for DMA
 ****************************************************************/

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: si_vme.c,v 1.33 2024/12/20 23:52:00 tsutsui Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/proc.h>

#include <dev/scsipi/scsi_all.h>
#include <dev/scsipi/scsipi_all.h>
#include <dev/scsipi/scsipi_debug.h>
#include <dev/scsipi/scsiconf.h>

#include <machine/autoconf.h>
#include <machine/dvma.h>

/* #define DEBUG XXX */

#include <dev/ic/ncr5380reg.h>
#include <dev/ic/ncr5380var.h>

#include "sireg.h"
#include "sivar.h"

void si_vme_dma_setup(struct ncr5380_softc *);
void si_vme_dma_start(struct ncr5380_softc *);
void si_vme_dma_eop(struct ncr5380_softc *);
void si_vme_dma_stop(struct ncr5380_softc *);

void si_vme_intr_on (struct ncr5380_softc *);
void si_vme_intr_off(struct ncr5380_softc *);

static void si_vme_reset(struct ncr5380_softc *);

/*
 * New-style autoconfig attachment
 */

static int	si_vme_match(device_t, cfdata_t, void *);
static void	si_vme_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(si_vme, sizeof(struct si_softc),
    si_vme_match, si_vme_attach, NULL, NULL);

/*
 * Options for disconnect/reselect, DMA, and interrupts.
 * By default, allow disconnect/reselect on targets 4-6.
 * Those are normally tapes that really need it enabled.
 */
int si_vme_options = 0x0f;


static int
si_vme_match(device_t parent, cfdata_t cf, void *aux)
{
	struct confargs *ca = aux;
	int probe_addr;

	/* No default VME address. */
	if (ca->ca_paddr == -1)
		return 0;

	/* Make sure something is there... */
	probe_addr = ca->ca_paddr + 1;
	if (bus_peek(ca->ca_bustype, probe_addr, 1) == -1)
		return 0;

	/*
	 * If this is a VME SCSI board, we have to determine whether
	 * it is an "sc" (Sun2) or "si" (Sun3) SCSI board.  This can
	 * be determined using the fact that the "sc" board occupies
	 * 4K bytes in VME space but the "si" board occupies 2K bytes.
	 */
	/* Note: the "si" board should NOT respond here. */
	probe_addr = ca->ca_paddr + 0x801;
	if (bus_peek(ca->ca_bustype, probe_addr, 1) != -1) {
		/* Something responded at 2K+1.  Maybe an "sc" board? */
#ifdef	DEBUG
		printf("%s: May be an `sc' board at pa=0x%lx\n",
		    __func__, ca->ca_paddr);
#endif
		return 0;
	}

	/* Default interrupt priority. */
	if (ca->ca_intpri == -1)
		ca->ca_intpri = 2;

	return 1;
}

static void
si_vme_attach(device_t parent, device_t self, void *args)
{
	struct si_softc *sc = device_private(self);
	struct ncr5380_softc *ncr_sc = &sc->ncr_sc;
	struct cfdata *cf = device_cfdata(self);
	struct confargs *ca = args;

	ncr_sc->sc_dev = self;
	sc->sc_bst = ca->ca_bustag;
	sc->sc_dmat = ca->ca_dmatag;

	if (bus_space_map(sc->sc_bst, ca->ca_paddr, sizeof(struct si_regs), 0,
	    &sc->sc_bsh) != 0) {
		aprint_error(": can't map register\n");
		return;
	}
	sc->sc_regs = bus_space_vaddr(sc->sc_bst, sc->sc_bsh);

	if (bus_dmamap_create(sc->sc_dmat, MAXPHYS, 1, MAXPHYS, 0,
	    BUS_DMA_NOWAIT, &sc->sc_dmap) != 0) {
		aprint_error(": can't create DMA map\n");
		return;
	}

	/* Get options from config flags if specified. */
	if (cf->cf_flags)
		sc->sc_options = cf->cf_flags;
	else
		sc->sc_options = si_vme_options;

	aprint_normal(": options=0x%x\n", sc->sc_options);

	sc->sc_adapter_type = ca->ca_bustype;
	sc->sc_adapter_iv_am = VME_SUPV_DATA_24 | (ca->ca_intvec & 0xFF);

	/*
	 * MD function pointers used by the MI code.
	 */
	ncr_sc->sc_pio_out = ncr5380_pio_out;
	ncr_sc->sc_pio_in =  ncr5380_pio_in;
	ncr_sc->sc_dma_alloc = si_dma_alloc;
	ncr_sc->sc_dma_free  = si_dma_free;
	ncr_sc->sc_dma_setup = si_vme_dma_setup;
	ncr_sc->sc_dma_start = si_vme_dma_start;
	ncr_sc->sc_dma_poll  = si_dma_poll;
	ncr_sc->sc_dma_eop   = si_vme_dma_eop;
	ncr_sc->sc_dma_stop  = si_vme_dma_stop;
	ncr_sc->sc_intr_on   = si_vme_intr_on;
	ncr_sc->sc_intr_off  = si_vme_intr_off;

	/* Attach interrupt handler. */
	isr_add_vectored(si_intr, (void *)sc, ca->ca_intpri, ca->ca_intvec);

	/* Reset the hardware. */
	si_vme_reset(ncr_sc);

	/* Do the common attach stuff. */
	si_attach(sc);
}

static void
si_vme_reset(struct ncr5380_softc *ncr_sc)
{
	struct si_softc *sc = (struct si_softc *)ncr_sc;
	volatile struct si_regs *si = sc->sc_regs;

#ifdef	DEBUG
	if (si_debug) {
		printf("%s\n", __func__);
	}
#endif

	/*
	 * The SCSI3 controller has an 8K FIFO to buffer data between the
	 * 5380 and the DMA.  Make sure it starts out empty.
	 *
	 * The reset bits in the CSR are active low.
	 */
	si->si_csr = 0;
	delay(10);
	si->si_csr = SI_CSR_FIFO_RES | SI_CSR_SCSI_RES | SI_CSR_INTR_EN;
	delay(10);
	si->fifo_count = 0;

	/* Make sure the DMA engine is stopped. */
	si->dma_addrh = 0;
	si->dma_addrl = 0;
	si->dma_counth = 0;
	si->dma_countl = 0;
	si->si_iv_am = sc->sc_adapter_iv_am;
	si->fifo_cnt_hi = 0;
}

/*
 * This is called when the bus is going idle,
 * so we want to enable the SBC interrupts.
 * That is controlled by the DMA enable!
 * Who would have guessed!
 * What a NASTY trick!
 */
void
si_vme_intr_on(struct ncr5380_softc *ncr_sc)
{
	struct si_softc *sc = (struct si_softc *)ncr_sc;
	volatile struct si_regs *si = sc->sc_regs;

	/* receive mode should be safer */
	si->si_csr &= ~SI_CSR_SEND;

	/* Clear the count so nothing happens. */
	si->dma_counth = 0;
	si->dma_countl = 0;
	
	/* Clear the start address too. (paranoid?) */
	si->dma_addrh = 0;
	si->dma_addrl = 0;

	/* Finally, enable the DMA engine. */
	si->si_csr |= SI_CSR_DMA_EN;
}

/*
 * This is called when the bus is idle and we are
 * about to start playing with the SBC chip.
 */
void
si_vme_intr_off(struct ncr5380_softc *ncr_sc)
{
	struct si_softc *sc = (struct si_softc *)ncr_sc;
	volatile struct si_regs *si = sc->sc_regs;

	si->si_csr &= ~SI_CSR_DMA_EN;
}

/*
 * This function is called during the COMMAND or MSG_IN phase
 * that precedes a DATA_IN or DATA_OUT phase, in case we need
 * to setup the DMA engine before the bus enters a DATA phase.
 *
 * XXX: The VME adapter appears to suppress SBC interrupts
 * when the FIFO is not empty or the FIFO count is non-zero!
 *
 * On the VME version, setup the start address, but clear the
 * count (to make sure it stays idle) and set that later.
 */
void
si_vme_dma_setup(struct ncr5380_softc *ncr_sc)
{
	struct si_softc *sc = (struct si_softc *)ncr_sc;
	struct sci_req *sr = ncr_sc->sc_current;
	struct si_dma_handle *dh = sr->sr_dma_hand;
	volatile struct si_regs *si = sc->sc_regs;
	long data_pa;
	int xlen;

	/*
	 * Get the DVMA mapping for this segment.
	 * XXX - Should separate allocation and mapin.
	 */
	data_pa = dh->dh_dmaaddr;
	if (data_pa & 1)
		panic("%s: bad pa=0x%lx", __func__, data_pa);
	xlen = dh->dh_dmalen;
	xlen &= ~1;				/* XXX: necessary? */
	sc->sc_reqlen = xlen; 	/* XXX: or less? */

#ifdef	DEBUG
	if (si_debug & 2) {
		printf("%s: dh=%p, pa=0x%lx, xlen=0x%x\n",
		    __func__, dh, data_pa, xlen);
	}
#endif

	/* Set direction (send/recv) */
	if (dh->dh_flags & SIDH_OUT) {
		si->si_csr |= SI_CSR_SEND;
	} else {
		si->si_csr &= ~SI_CSR_SEND;
	}

	/* Reset the FIFO. */
	si->si_csr &= ~SI_CSR_FIFO_RES; 	/* active low */
	si->si_csr |= SI_CSR_FIFO_RES;

	if (data_pa & 2) {
		si->si_csr |= SI_CSR_BPCON;
	} else {
		si->si_csr &= ~SI_CSR_BPCON;
	}

	/* Load the start address. */
	si->dma_addrh = (uint16_t)(data_pa >> 16);
	si->dma_addrl = (uint16_t)(data_pa & 0xFFFF);

	/*
	 * Keep the count zero or it may start early!
	 */
	si->dma_counth = 0;
	si->dma_countl = 0;

#if 0
	/* Clear FIFO counter. (also hits dma_count) */
	si->fifo_cnt_hi = 0;
	si->fifo_count = 0;		
#endif
}


void
si_vme_dma_start(struct ncr5380_softc *ncr_sc)
{
	struct si_softc *sc = (struct si_softc *)ncr_sc;
	struct sci_req *sr = ncr_sc->sc_current;
	struct si_dma_handle *dh = sr->sr_dma_hand;
	volatile struct si_regs *si = sc->sc_regs;
	int s, xlen;

	xlen = sc->sc_reqlen;

	/* This MAY be time critical (not sure). */
	s = splhigh();

	si->dma_counth = (uint16_t)(xlen >> 16);
	si->dma_countl = (uint16_t)(xlen & 0xFFFF);

	/* Set it anyway, even though dma_count hits it. */
	si->fifo_cnt_hi = (uint16_t)(xlen >> 16);
	si->fifo_count  = (uint16_t)(xlen & 0xFFFF);

	/*
	 * Acknowledge the phase change.  (After DMA setup!)
	 * Put the SBIC into DMA mode, and start the transfer.
	 */
	if (dh->dh_flags & SIDH_OUT) {
		*ncr_sc->sci_tcmd = PHASE_DATA_OUT;
		SCI_CLR_INTR(ncr_sc);
		*ncr_sc->sci_icmd = SCI_ICMD_DATA;
		*ncr_sc->sci_mode |= (SCI_MODE_DMA | SCI_MODE_DMA_IE);
		*ncr_sc->sci_dma_send = 0;	/* start it */
	} else {
		*ncr_sc->sci_tcmd = PHASE_DATA_IN;
		SCI_CLR_INTR(ncr_sc);
		*ncr_sc->sci_icmd = 0;
		*ncr_sc->sci_mode |= (SCI_MODE_DMA | SCI_MODE_DMA_IE);
		*ncr_sc->sci_irecv = 0;	/* start it */
	}

	/* Let'er rip! */
	si->si_csr |= SI_CSR_DMA_EN;

	splx(s);
	ncr_sc->sc_state |= NCR_DOINGDMA;

#ifdef	DEBUG
	if (si_debug & 2) {
		printf("%s: started, flags=0x%x\n",
		    __func__, ncr_sc->sc_state);
	}
#endif
}


void
si_vme_dma_eop(struct ncr5380_softc *ncr_sc)
{

	/* Not needed - DMA was stopped prior to examining sci_csr */
}


void
si_vme_dma_stop(struct ncr5380_softc *ncr_sc)
{
	struct si_softc *sc = (struct si_softc *)ncr_sc;
	struct sci_req *sr = ncr_sc->sc_current;
	struct si_dma_handle *dh = sr->sr_dma_hand;
	volatile struct si_regs *si = sc->sc_regs;
	int resid, ntrans;

	if ((ncr_sc->sc_state & NCR_DOINGDMA) == 0) {
#ifdef	DEBUG
		printf("%s: DMA not running\n", __func__);
#endif
		return;
	}
	ncr_sc->sc_state &= ~NCR_DOINGDMA;

	/* First, halt the DMA engine. */
	si->si_csr &= ~SI_CSR_DMA_EN;	/* VME only */

	/* Set an impossible phase to prevent data movement? */
	*ncr_sc->sci_tcmd = PHASE_INVALID;

	if (si->si_csr & (SI_CSR_DMA_CONFLICT | SI_CSR_DMA_BUS_ERR)) {
		printf("si: DMA error, csr=0x%x, reset\n", si->si_csr);
		sr->sr_xs->error = XS_DRIVER_STUFFUP;
		ncr_sc->sc_state |= NCR_ABORTING;
		si_vme_reset(ncr_sc);
		goto out;
	}

	/* Note that timeout may have set the error flag. */
	if (ncr_sc->sc_state & NCR_ABORTING)
		goto out;

	/* XXX: Wait for DMA to actually finish? */

	/*
	 * Now try to figure out how much actually transferred
	 *
	 * The fifo_count does not reflect how many bytes were
	 * actually transferred for VME.
	 *
	 * SCSI-3 VME interface is a little funny on writes:
	 * if we have a disconnect, the DMA has overshot by
	 * one byte and the resid needs to be incremented.
	 * Only happens for partial transfers.
	 * (Thanks to Matt Jacob)
	 */

	resid = si->fifo_count & 0xFFFF;
	if (dh->dh_flags & SIDH_OUT)
		if ((resid > 0) && (resid < sc->sc_reqlen))
			resid++;
	ntrans = sc->sc_reqlen - resid;

#ifdef	DEBUG
	if (si_debug & 2) {
		printf("%s: resid=0x%x ntrans=0x%x\n",
		    __func__, resid, ntrans);
	}
#endif

	if (ntrans < MIN_DMA_LEN) {
		printf("si: fifo count: 0x%x\n", resid);
		ncr_sc->sc_state |= NCR_ABORTING;
		goto out;
	}
	if (ntrans > ncr_sc->sc_datalen)
		panic("%s: excess transfer", __func__);

	/* Adjust data pointer */
	ncr_sc->sc_dataptr += ntrans;
	ncr_sc->sc_datalen -= ntrans;

	/*
	 * After a read, we may need to clean-up
	 * "Left-over bytes" (yuck!)
	 */
	if (((dh->dh_flags & SIDH_OUT) == 0) &&
		((si->si_csr & SI_CSR_LOB) != 0)) {
		uint8_t *cp = ncr_sc->sc_dataptr;
#ifdef DEBUG
		printf("si: Got Left-over bytes!\n");
#endif
		if (si->si_csr & SI_CSR_BPCON) {
			/* have SI_CSR_BPCON */
			cp[-1] = (si->si_bprl & 0xff00) >> 8;
		} else {
			switch (si->si_csr & SI_CSR_LOB) {
			case SI_CSR_LOB_THREE:
				cp[-3] = (si->si_bprh & 0xff00) >> 8;
				cp[-2] = (si->si_bprh & 0x00ff);
				cp[-1] = (si->si_bprl & 0xff00) >> 8;
				break;
			case SI_CSR_LOB_TWO:
				cp[-2] = (si->si_bprh & 0xff00) >> 8;
				cp[-1] = (si->si_bprh & 0x00ff);
				break;
			case SI_CSR_LOB_ONE:
				cp[-1] = (si->si_bprh & 0xff00) >> 8;
				break;
			}
		}
	}

out:
	si->dma_addrh = 0;
	si->dma_addrl = 0;

	si->dma_counth = 0;
	si->dma_countl = 0;

	si->fifo_cnt_hi = 0;
	si->fifo_count  = 0;

	/* Put SBIC back in PIO mode. */
	*ncr_sc->sci_mode &= ~(SCI_MODE_DMA | SCI_MODE_DMA_IE);
	*ncr_sc->sci_icmd = 0;
}
