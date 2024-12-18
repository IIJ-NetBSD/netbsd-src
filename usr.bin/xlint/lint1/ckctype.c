/* $NetBSD: ckctype.c,v 1.13 2024/12/18 18:14:54 rillig Exp $ */

/*-
 * Copyright (c) 2021 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland Illig <rillig@NetBSD.org>.
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

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>

#if defined(__RCSID)
__RCSID("$NetBSD: ckctype.c,v 1.13 2024/12/18 18:14:54 rillig Exp $");
#endif

#include <string.h>

#include "lint1.h"

/*
 * Check that the functions from <ctype.h> are used properly.  They must not
 * be called with an argument of type 'char'.  In such a case, the argument
 * must be converted to 'unsigned char'.  The tricky thing is that even though
 * the parameter type is declared as 'int', a 'char' argument must not be
 * directly cast to 'int', as that would preserve negative argument values.
 *
 * See also:
 *	ctype(3)
 *	https://stackoverflow.com/a/60696378
 */

static bool
is_ctype_function(const char *name)
{

	if (name[0] == 'i' && name[1] == 's')
		return strcmp(name, "isalnum") == 0 ||
		    strcmp(name, "isalpha") == 0 ||
		    strcmp(name, "isblank") == 0 ||
		    strcmp(name, "iscntrl") == 0 ||
		    strcmp(name, "isdigit") == 0 ||
		    strcmp(name, "isgraph") == 0 ||
		    strcmp(name, "islower") == 0 ||
		    strcmp(name, "isprint") == 0 ||
		    strcmp(name, "ispunct") == 0 ||
		    strcmp(name, "isspace") == 0 ||
		    strcmp(name, "isupper") == 0 ||
		    strcmp(name, "isxdigit") == 0;

	if (name[0] == 't' && name[1] == 'o')
		return strcmp(name, "tolower") == 0 ||
		    strcmp(name, "toupper") == 0;

	return false;
}

static bool
is_ctype_table(const char *name)
{

	/* NetBSD sys/ctype_bits.h 1.6 from 2016-01-22 */
	if (strcmp(name, "_ctype_tab_") == 0 ||
	    strcmp(name, "_tolower_tab_") == 0 ||
	    strcmp(name, "_toupper_tab_") == 0)
		return true;

	/* NetBSD sys/ctype_bits.h 1.1 from 2010-06-01 */
	return strcmp(name, "_ctype_") == 0;
}

static void
check_ctype_arg(const char *func, const tnode_t *arg)
{
	const tnode_t *on, *cn;

	for (on = arg; on->tn_op == CVT; on = on->u.ops.left)
		if (on->tn_type->t_tspec == UCHAR)
			return;
	if (on->tn_type->t_tspec == UCHAR)
		return;

	if (arg->tn_op == CVT && arg->tn_cast) {
		/* argument to '%s' must be cast to 'unsigned char', not ... */
		warning(342, func, type_name(arg->tn_type));
		return;
	}

	cn = before_conversion(arg);
	if (cn->tn_type->t_tspec != UCHAR && cn->tn_type->t_tspec != INT) {
		/* argument to '%s' must be 'unsigned char' or EOF, not '%s' */
		warning(341, func, type_name(cn->tn_type));
		return;
	}
}

void
check_ctype_function_call(const function_call *call)
{

	if (call->args_len == 1 &&
	    call->func->tn_op == NAME &&
	    is_ctype_function(call->func->u.sym->s_name))
		check_ctype_arg(call->func->u.sym->s_name, call->args[0]);
}

void
check_ctype_macro_invocation(const tnode_t *ln, const tnode_t *rn)
{

	if (ln->tn_op == PLUS &&
	    ln->u.ops.left != NULL &&
	    ln->u.ops.left->tn_op == LOAD &&
	    ln->u.ops.left->u.ops.left != NULL &&
	    ln->u.ops.left->u.ops.left->tn_op == NAME &&
	    is_ctype_table(ln->u.ops.left->u.ops.left->u.sym->s_name))
		check_ctype_arg("function from <ctype.h>", rn);
}
