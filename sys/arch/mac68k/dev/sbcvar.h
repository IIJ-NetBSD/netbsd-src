/*	$NetBSD: sbcvar.h,v 1.15 2024/11/22 07:16:01 nat Exp $	*/

/*
 * Copyright (C) 1996 Scott Reynolds.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
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

/*
 * Transfers smaller than this are done using PIO
 * (on assumption they're not worth PDMA overhead)
 */
#define	MIN_DMA_LEN 128

/*
 * Transfers larger than 8192 bytes need to be split up
 * due to the size of the PDMA space.
 */
#define	MAX_DMA_LEN 0x2000

/*
 * This structure is used to keep track of PDMA requests.
 */
struct sbc_pdma_handle {
	int	dh_flags;	/* flags */
#define	SBC_DH_BUSY	0x01	/* This handle is in use */
#define	SBC_DH_OUT	0x02	/* PDMA data out (write) */
#define	SBC_DH_DONE	0x04	/* PDMA transfer is complete */
	u_char	*dh_addr;	/* data buffer */
	int	dh_len;		/* length of data buffer */
};

/*
 * The first structure member has to be the ncr5380_softc
 * so we can just cast to go back and forth between them.
 */
struct sbc_softc {
	struct ncr5380_softc ncr_sc;
	volatile struct sbc_regs *sc_regs;
	volatile vaddr_t	sc_drq_addr;
	volatile vaddr_t	sc_nodrq_addr;
	void			(*sc_clrintr)(struct ncr5380_softc *);
	volatile int		sc_resid;
	int			sc_options;	/* options for this instance. */
	struct sbc_pdma_handle sc_pdma[SCI_OPENINGS];
	kmutex_t		sc_drq_lock;
};

/*
 * Options.  By default, SCSI interrupts and reselect are disabled.
 * You may enable either of these features with the `flags' directive
 * in your kernel's configuration file.
 *
 * Alternatively, you can patch your kernel with DDB or some other
 * mechanism.  The sc_options member of the softc is OR'd with
 * the value in sbc_options.
 *
 * The options code is based on the sparc 'si' driver's version of
 * the same.
 */     
#define	SBC_PDMA		0x01	/* Use PDMA for polled transfers */
#define	SBC_INTR		0x02	/* Allow SCSI IRQ/DRQ interrupts */
#define	SBC_RESELECT		0x04	/* Allow disconnect/reselect */
#define	SBC_PDMA_NO_WRITE	0x08	/* No PDMA for writes */
#define	SBC_OPTIONS_MASK (SBC_PDMA_NO_WRITE|SBC_RESELECT|SBC_INTR|SBC_PDMA)
#define	SBC_OPTIONS_BITS	"\10\4NOWRITE\3RESELECT\2INTR\1PDMA"

extern int	sbc_debug;
extern int	sbc_link_flags;
extern int	sbc_options;

int	sbc_pdma_in(struct ncr5380_softc *, int, int, u_char *);
int	sbc_pdma_out(struct ncr5380_softc *, int, int, u_char *);
void	sbc_irq_intr(void *);
void	sbc_drq_intr(void *);
void	sbc_dma_alloc(struct ncr5380_softc *);
void	sbc_dma_free(struct ncr5380_softc *);
void	sbc_dma_poll(struct ncr5380_softc *);
void	sbc_dma_setup(struct ncr5380_softc *);
void	sbc_dma_start(struct ncr5380_softc *);
void	sbc_dma_eop(struct ncr5380_softc *);
void	sbc_dma_stop(struct ncr5380_softc *);
#ifdef SBC_DEBUG
void	decode_5380_intr(struct ncr5380_softc *);
#endif
