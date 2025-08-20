/*-
 * Copyright (c) 2006 Michael Lorenz
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: obiovar.h,v 1.6 2025/08/20 07:00:31 macallan Exp $");

#ifndef OBIOVAR_H
#define OBIOVAR_H

/*
 * access obio registers
 * Since only some PowerBooks have more than one obio and in these cases we
 * need to special-case it anyway we can safely assume that whoever wants to
 * mess with obio registers means obio0
 */

void obio_write_4(int, uint32_t);
void obio_write_1(int, uint8_t);
uint32_t obio_read_4(int);
uint8_t obio_read_1(int);
int obio_space_map(bus_addr_t, bus_size_t, bus_space_handle_t *);

/* some common offsets */
#define HEATHROW_FCR	0x38
#define KEYLARGO_FCR1	0x3c

#define GPIO_OUTSEL	0xf0	/* Output select */
		/*	0x00	GPIO bit0 is output
			0x10	media-bay power
			0x20	reserved
			0x30	MPIC */

#define GPIO_ALTOE	0x08	/* Alternate output enable */
		/*	0x00	Use DDR
			0x08	Use output select */
#define GPIO_DDR	0x04	/* Data direction */
#define GPIO_DDR_OUTPUT	0x04	/* Output */
#define GPIO_DDR_INPUT	0x00	/* Input */

#define GPIO_LEVEL	0x02	/* Pin level (RO) */

#define	GPIO_DATA	0x01	/* Data */

#endif /* OBIOVAR_H */
