/*	$NetBSD: asm.h,v 1.1 2025/04/26 22:34:52 uwe Exp $	*/

/*-
 * Copyright (c) 2025 The NetBSD Foundation, Inc.
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

#ifndef	TESTS_KERNEL_ARCH_SH3_ASM_H
#define	TESTS_KERNEL_ARCH_SH3_ASM_H

#include <machine/asm.h>

/*
 * We define convenience macros to hide the difference between direct
 * and PIC function calls, but with variable accesses it gets a bit
 * more unwieldy, so don't impose this on the general audience
 */

#ifdef __PIC__

#define VAR_DATUM(v)	.long	v@GOT

#define MOVL_VAR(label, reg)		\
		mov.l	label, r0;	\
		mov.l	@(r0, r12), reg

#else

#define VAR_DATUM(v)	.long	v

#define MOVL_VAR(label, reg)		\
		mov.l	label, reg;	\

#endif


#endif	/* TESTS_KERNEL_ARCH_SH3_ASM_H */
