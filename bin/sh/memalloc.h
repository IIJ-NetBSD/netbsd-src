/*	$NetBSD: memalloc.h,v 1.21 2025/05/07 14:01:01 kre Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 *	@(#)memalloc.h	8.2 (Berkeley) 5/4/95
 */

struct stackmark {
	struct stack_block *stackp;
	char *stacknxt;
	int stacknleft;
	int sstrnleft;
	struct stackmark *marknext;
};


extern char *stacknxt;
extern int stacknleft;
extern int sstrnleft;

pointer ckmalloc(size_t);
pointer ckrealloc(pointer, int);
char *savestr(const char *);
pointer stalloc(int);
void stunalloc(pointer);
void setstackmark(struct stackmark *);
void popstackmark(struct stackmark *);
void rststackmark(struct stackmark *);
void growstackblock(void);
void grabstackblock(int);
char *growstackstr(void);
char *makestrspace(void);
void ungrabstackstr(char *, char *);

char *ststrcat(size_t *, ...);
#define STSTRC_END	((const char *)0)



#define stackblock() stacknxt
#define stackblocksize() stacknleft
#define STARTSTACKSTR(p)	p = stackblock(), sstrnleft = stackblocksize()
#define STPUTC(c, p)	(--sstrnleft >= 0? (*p++ = (c)) : (p = growstackstr(), *p++ = (c)))
#define CHECKSTRSPACE(n, p)	{ if (sstrnleft < n) p = makestrspace(); }
#define USTPUTC(c, p)	(--sstrnleft, *p++ = (c))
#define STACKSTRNUL(p)	(sstrnleft == 0? (p = growstackstr(), sstrnleft++, *p = '\0') : (*p = '\0'))
#define STUNPUTC(p)	(++sstrnleft, --p)
#define STTOPC(p)	p[-1]
#define STADJUST(amount, p)	(p += (amount), sstrnleft -= (amount))
#define grabstackstr(p)	stalloc((p) - stackblock())

#define ckfree(p)	free((pointer)(p))
