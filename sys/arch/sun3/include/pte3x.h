/*	$NetBSD: pte3x.h,v 1.9 2024/12/20 23:50:00 tsutsui Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jeremy Cooper.
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
 * This file should contain the machine-dependent details about
 * Page Table Entries (PTEs) and related things.  For example,
 * things that depend on the MMU configuration (number of levels
 * in the translation structure) should go here.
 */

#ifndef _MACHINE_PTE3X_H
#define _MACHINE_PTE3X_H

#include <machine/mc68851.h>

/*************************************************************************
 * Translation Control Register Settings                                 *
 *************************************************************************
 * The following settings are set by the ROM monitor and used by the
 * kernel.  If they are changed, appropriate code must be written into
 * the kernel startup to set them.
 *
 * A virtual address is translated into a physical address by dividing its
 * bits into four fields.  The first three fields are used as indexes into
 * descriptor tables and the last field (the 13 lowest significant
 * bits) is an offset to be added to the base address found at the final
 * table.  The first three fields are named TIA, TIB and TIC respectively.
 *  31                                    12                        0
 *  +-.-.-.-.-.-.-+-.-.-.-.-.-+-.-.-.-.-.-+-.-.-.-.-.-.-.-.-.-.-.-.-+
 *  |     TIA     |    TIB    |    TIC    |        OFFSET           |
 *  +-.-.-.-.-.-.-+-.-.-.-.-.-+-.-.-.-.-.-+-.-.-.-.-.-.-.-.-.-.-.-.-+
 */
#define MMU_TIA_SHIFT (13+6+6)
#define MMU_TIA_MASK  (0xfe000000)
#define MMU_TIA_RANGE (0x02000000)
#define MMU_TIB_SHIFT (13+6)
#define MMU_TIB_MASK  (0x01f80000)
#define MMU_TIB_RANGE (0x00080000)
#define MMU_TIC_SHIFT (13)
#define MMU_TIC_MASK  (0x0007e000)
#define MMU_TIC_RANGE (0x00002000)
#define MMU_PAGE_SHIFT (13)
#define MMU_PAGE_MASK (0xffffe000)
#define MMU_PAGE_SIZE (0x00002000)

/*
 * Macros which extract each of these fields out of a given
 * VA.
 */
#define MMU_TIA(va) \
	((unsigned long) ((va) & MMU_TIA_MASK) >> MMU_TIA_SHIFT)
#define MMU_TIB(va) \
	((unsigned long) ((va) & MMU_TIB_MASK) >> MMU_TIB_SHIFT)
#define MMU_TIC(va) \
	((unsigned long) ((va) & MMU_TIC_MASK) >> MMU_TIC_SHIFT)

/*
 * The widths of the TIA, TIB, and TIC fields determine the size (in
 * elements) of the tables they index.
 */
#define MMU_A_TBL_SIZE (128)
#define MMU_B_TBL_SIZE (64)
#define MMU_C_TBL_SIZE (64)

/*
 * Rounding macros.
 * The MMU_ROUND macros are named misleadingly.  MMU_ROUND_A actually
 * rounds an address to the nearest B table boundary, and so on.
 * MMU_ROUND_C() is synonmous with m68k_round_page().
 */
#define	MMU_ROUND_A(pa)\
	((unsigned long) (pa) & MMU_TIA_MASK)
#define	MMU_ROUND_UP_A(pa)\
	((unsigned long) (pa + MMU_TIA_RANGE - 1) & MMU_TIA_MASK)
#define	MMU_ROUND_B(pa)\
	((unsigned long) (pa) & (MMU_TIA_MASK|MMU_TIB_MASK))
#define	MMU_ROUND_UP_B(pa)\
	((unsigned long) (pa + MMU_TIB_RANGE - 1) & (MMU_TIA_MASK|MMU_TIB_MASK))
#define	MMU_ROUND_C(pa)\
	((unsigned long) (pa) & MMU_PAGE_MASK)
#define	MMU_ROUND_UP_C(pa)\
	((unsigned long) (pa + MMU_PAGE_SIZE - 1) & MMU_PAGE_MASK)

/* Compatibility... */
#define PG_FRAME MMU_SHORT_PTE_BASEADDR
#define PG_PA(pte)  	((pte) & PG_FRAME)
#define PG_PFNUM(pte)	(PG_PA(pte) >> PGSHIFT)
#define PG_VALID    	MMU_DT_PAGE

#endif	/* _MACHINE_PTE3X_H */
