/*	$NetBSD: sysinfo.c,v 1.10 2024/09/25 08:32:44 rin Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Leo Weppelman.
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

#ifdef TOSTOOLS
#include <stdio.h>
#include <sys/types.h>
#else

#include <lib/libsa/stand.h>
#include <atari_stand.h>
#include <libkern.h>
#include <machine/cpu.h>
#endif /* TOSTOOLS */

#include "libtos.h"
#include "tosdefs.h"
#include "kparamb.h"

/*
 * ADDR_* defined in tosdefs.h are in the 0-th page, even if 4KB page,
 * i.e., [0, 0x1000). This causes -Warray-bounds for GCC12 and later.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

/*
 * Extract memory and CPU/FPU info from system.
 */
void
sys_info(osdsc_t *od)
{
	long	*jar;
	OSH	*oshdr;

	od->cputype = 0;

	/*
	 * Some GEMDOS versions use a different year-base in the RTC.
	 */
	oshdr = *ADDR_OSHEAD;
	oshdr = oshdr->os_beg;
	if ((oshdr->os_version > 0x0300) && (oshdr->os_version < 0x0306))
		od->cputype |= ATARI_CLKBROKEN;

	/*
	 * Auto configure memory sizes when they are not pre-set.
	 */
	if (od->stmem_size <= 0)
		od->stmem_size  = *ADDR_PHYSTOP;

	if (od->ttmem_size)
		od->ttmem_start  = TTRAM_BASE;
	else {
		if (*ADDR_CHKRAMTOP == RAMTOP_MAGIC) {
			od->ttmem_size  = *ADDR_RAMTOP;
			if (od->ttmem_size > TTRAM_BASE) {
				od->ttmem_size  -= TTRAM_BASE;
				od->ttmem_start  = TTRAM_BASE;
			}
			else od->ttmem_size = 0;
		}
	}

	/*
	 * Scan cookiejar for CPU types, accellerator boards, etc.
	 */
	jar = *ADDR_P_COOKIE;
	if (jar != NULL) {
		do {
		    if (jar[0] == 0x5f435055) { /* _CPU	*/
			switch (jar[1]) {
				case 0:
					od->cputype |= ATARI_68000;
					break;
				case 10:
					od->cputype |= ATARI_68010;
					break;
				case 20:
					od->cputype |= ATARI_68020;
					break;
				case 30:
					od->cputype |= ATARI_68030;
					break;
				case 40:
					od->cputype |= ATARI_68040;
					break;
				case 60:
					od->cputype |= ATARI_68060;
					break;
				default:
					/* This error is caught later on */
					break;
		        }
		    }
		    if (jar[0] == 0x42504658) { /* BPFX	*/
			unsigned long	*p;

			p = (unsigned long*)jar[1];

			od->ttmem_start = p[1];
			od->ttmem_size  = p[2];
		    }
		    if (jar[0] == 0x5f435432) { /* _CT2	*/
			/*
			 * The CT2 board has a different physical base address!
			 */
			od->ttmem_start = CTRAM_BASE;
		    }
		    jar = &jar[2];
		} while (jar[-2]);
	}
}

#pragma GCC diagnostic pop
