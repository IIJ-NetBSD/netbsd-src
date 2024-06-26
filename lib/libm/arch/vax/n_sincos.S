/*	$NetBSD: n_sincos.S,v 1.10 2024/05/06 15:33:04 riastradh Exp $	*/
/*
 * Copyright (c) 1985, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)sincos.s	8.1 (Berkeley) 6/4/93
 */

/*
 *  This is the implementation of Peter Tang's double precision
 *  sine and cosine for the VAX using Bob Corbett's argument reduction.
 *
 *  Notes:
 *       under 1,024,000 random arguments testing on [0,2*pi]
 *       sin() observed maximum error = 0.814 ulps
 *       cos() observed maximum error = 0.792 ulps
 *
 * double sin(arg)
 * double arg;
 * method: true range reduction to [-pi/4,pi/4], P. Tang  &  B. Corbett
 * S. McDonald, April 4,  1985
 */
#include <machine/asm.h>

WEAK_ALIAS(sinf, _sinf)
ENTRY(_sinf, 0)
	cvtfd	4(%ap),-(%sp)
	calls	$2,_C_LABEL(_sin)
	cvtdf	%r0,%r0
	ret
END(_sinf)

WEAK_ALIAS(sinl, _sinl)
STRONG_ALIAS(_sinl, _sin)

WEAK_ALIAS(sin, _sin)
ENTRY(_sin, 0xfc0)
	movq	4(%ap),%r0
	bicw3	$0x807f,%r0,%r2
	beql	1f		# if x is zero or reserved operand then return x
/*
 * Save the PSL's IV & FU bits on the stack.
 */
	movpsl	%r2
	bicw3	$0xff9f,%r2,-(%sp)
/*
 * Clear the IV & FU bits.
 */
	bicpsw	$0x0060
/*
 *  Entered by  sine    ; save  0  in  %r4 .
 */
	jsb	_C_LABEL(__libm_argred)+2
	movl	$0,%r4
	jsb	_C_LABEL(__libm_sincos)+2
	bispsw	(%sp)+
1:	ret
END(_sin)

/*
 * double cos(arg)
 * double arg;
 * method: true range reduction to [-pi/4,pi/4], P. Tang  &  B. Corbett
 * S. McDonald, April 4,  1985
 */

WEAK_ALIAS(cosf, _cosf)
ENTRY(_cosf, 0)
	cvtfd	4(%ap),-(%sp)
	calls	$2,_C_LABEL(_cos)
	cvtdf	%r0,%r0
	ret
END(_cosf)

WEAK_ALIAS(cosl, _cosl)
STRONG_ALIAS(_cosl, _cos)

WEAK_ALIAS(cos, _cos)
ENTRY(_cos, 0x0fc0)
	movq	4(%ap),%r0
	bicw3	$0x7f,%r0,%r2
	cmpw	$0x8000,%r2
	beql	1f		# if x is reserved operand then return x
/*
 * Save the PSL's IV & FU bits on the stack.
 */
	movpsl	%r2
	bicw3	$0xff9f,%r2,-(%sp)
/*
 * Clear the IV & FU bits.
 */
	bicpsw	$0x0060
/*
 *  Entered by  cosine  ; save  1  in  %r4 .
 */
	jsb	_C_LABEL(__libm_argred)+2
	movl	$1,%r4
	jsb	_C_LABEL(__libm_sincos)+2
	bispsw	(%sp)+
1:	ret
END(_cos)
