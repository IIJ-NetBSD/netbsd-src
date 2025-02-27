/* $NetBSD: ckgetopt.c,v 1.28 2025/02/27 22:37:37 rillig Exp $ */

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
__RCSID("$NetBSD: ckgetopt.c,v 1.28 2025/02/27 22:37:37 rillig Exp $");
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lint1.h"

/*
 * In a typical while loop for parsing getopt options, ensure that each
 * option from the options string is handled, and that each handled option
 * is listed in the options string.
 */

static struct {
	/*-
	 * 0	means outside a while loop with a getopt call.
	 * 1	means directly inside a while loop with a getopt call.
	 * > 1	means in a nested while loop; this is used for finishing the
	 *	check at the correct point.
	 */
	int while_level;

	/*
	 * The options string from the getopt call.  Whenever an option is
	 * handled by a case label, it is set to ' '.  In the end, only ' ' and
	 * ':' should remain.
	 */
	pos_t options_pos;
	int options_lwarn;
	char *options;

	/*
	 * The nesting level of switch statements, is only modified if
	 * while_level > 0.  Only the case labels at switch_level == 1 are
	 * relevant, all nested case labels are ignored.
	 */
	int switch_level;
} ck;

/* Return whether tn has the form '(c = getopt(argc, argv, "str")) != -1'. */
static bool
is_getopt_condition(const tnode_t *tn, char **out_options)
{
	const function_call *call;
	const tnode_t *last_arg;
	const buffer *str;

	if (tn != NULL
	    && tn->tn_op == NE

	    && tn->u.ops.right->tn_op == CON
	    && tn->u.ops.right->u.value.v_tspec == INT
	    && tn->u.ops.right->u.value.u.integer == -1

	    && tn->u.ops.left->tn_op == ASSIGN
	    && tn->u.ops.left->u.ops.right->tn_op == CALL
	    && (call = tn->u.ops.left->u.ops.right->u.call)->func->tn_op == ADDR
	    && call->func->u.ops.left->tn_op == NAME
	    && strcmp(call->func->u.ops.left->u.sym->s_name, "getopt") == 0
	    && call->args_len == 3
	    && (last_arg = call->args[2]) != NULL
	    && last_arg->tn_op == CVT
	    && last_arg->u.ops.left->tn_op == ADDR
	    && last_arg->u.ops.left->u.ops.left->tn_op == STRING
	    && (str = last_arg->u.ops.left->u.ops.left->u.str_literals)->data != NULL) {
		buffer buf;
		buf_init(&buf);
		quoted_iterator it = { .end = 0 };
		while (quoted_next(str, &it))
			buf_add_char(&buf, (char)it.value);
		*out_options = buf.data;
		return true;
	}
	return false;
}

static void
check_unlisted_option(char opt)
{
	if (opt == ':' && ck.options[0] != ':')
		goto warn;

	char *optptr = strchr(ck.options, opt);
	if (optptr != NULL)
		*optptr = ' ';
	else if (opt != '?')
	warn:
		/* option '%c' should be listed in the options string */
		warning(339, opt);
}

static void
check_unhandled_option(void)
{
	for (const char *opt = ck.options; *opt != '\0'; opt++) {
		if (*opt == ' ' || *opt == ':')
			continue;

		int prev_lwarn = lwarn;
		lwarn = ck.options_lwarn;
		/* option '%c' should be handled in the switch */
		warning_at(338, &ck.options_pos, *opt);
		lwarn = prev_lwarn;
	}
}


void
check_getopt_begin_while(const tnode_t *tn)
{
	if (ck.while_level == 0) {
		if (!is_getopt_condition(tn, &ck.options))
			return;
		ck.options_lwarn = lwarn;
		ck.options_pos = curr_pos;
	}
	ck.while_level++;
}

void
check_getopt_begin_switch(void)
{
	if (ck.while_level > 0)
		ck.switch_level++;
}

void
check_getopt_case_label(int64_t value)
{
	if (ck.switch_level == 1 && value == (char)value)
		check_unlisted_option((char)value);
}

void
check_getopt_end_switch(void)
{
	if (ck.switch_level == 0)
		return;

	ck.switch_level--;
	if (ck.switch_level == 0)
		check_unhandled_option();
}

void
check_getopt_end_while(void)
{
	if (ck.while_level == 0)
		return;

	ck.while_level--;
	if (ck.while_level != 0)
		return;

	free(ck.options);
	ck.options = NULL;
}
