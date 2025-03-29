/* $NetBSD: isctype.c,v 1.28 2025/03/29 21:45:08 riastradh Exp $ */

/*-
 * Copyright (c)2008 Citrus Project,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: isctype.c,v 1.28 2025/03/29 21:45:08 riastradh Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"
#include <sys/types.h>
#include <sys/ctype_bits.h>
#define _CTYPE_NOINLINE
#include <ctype.h>
#include <langinfo.h>
#define __SETLOCALE_SOURCE__
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if EOF != -1
#error "EOF != -1"
#endif

#include "runetype_local.h"
#include "setlocale_local.h"

#define _RUNE_LOCALE(loc) \
    ((_RuneLocale *)((loc)->part_impl[LC_CTYPE]))

static void __noinline __dead
ctype_nasaldemon(const char *func, int c)
{
	char buf[128];

	snprintf_ss(buf, sizeof(buf), "ctype(3) %s: invalid input: %d\n", func,
	    c);
	(void)write(STDERR_FILENO, buf, strlen(buf));
	abort();
}

static inline void
ctype_check(const char *func, int c)
{

	if (__predict_false((c != EOF && c < 0) || c > UCHAR_MAX))
		ctype_nasaldemon(func, c);
}

#define	CTYPE_CHECK(c)	ctype_check(__func__, c)

#define _ISCTYPE_FUNC(name, bit) \
int \
is##name(int c) \
{ \
	CTYPE_CHECK(c); \
	return (int)_ctype_tab_[c + 1] & (bit); \
} \
int \
is##name ## _l(int c, locale_t loc) \
{ \
	CTYPE_CHECK(c); \
	return (int)((_RUNE_LOCALE(loc)->rl_ctype_tab[c + 1]) & (bit)); \
}

_ISCTYPE_FUNC(alnum, (_CTYPE_A|_CTYPE_D))
_ISCTYPE_FUNC(alpha,  _CTYPE_A)
_ISCTYPE_FUNC(blank,  _CTYPE_BL)
_ISCTYPE_FUNC(cntrl,  _CTYPE_C            )
_ISCTYPE_FUNC(digit,  _CTYPE_D)
_ISCTYPE_FUNC(graph,  _CTYPE_G)
_ISCTYPE_FUNC(lower,  _CTYPE_L            )
_ISCTYPE_FUNC(print,  _CTYPE_R)
_ISCTYPE_FUNC(punct,  _CTYPE_P            )
_ISCTYPE_FUNC(space,  _CTYPE_S            )
_ISCTYPE_FUNC(upper,  _CTYPE_U            )
_ISCTYPE_FUNC(xdigit, _CTYPE_X)

int
toupper(int c)
{
	CTYPE_CHECK(c);
	return (int)_toupper_tab_[c + 1];
}

int
toupper_l(int c, locale_t loc)
{
	CTYPE_CHECK(c);
	return (int)(_RUNE_LOCALE(loc)->rl_toupper_tab[c + 1]);
}

int
tolower(int c)
{
	CTYPE_CHECK(c);
	return (int)_tolower_tab_[c + 1];
}

int
tolower_l(int c, locale_t loc)
{
	CTYPE_CHECK(c);
	return (int)(_RUNE_LOCALE(loc)->rl_tolower_tab[c + 1]);
}

int
_toupper(int c)
{
	return (c - 'a' + 'A');
}

int
_tolower(int c)
{
	return (c - 'A' + 'a');
}
