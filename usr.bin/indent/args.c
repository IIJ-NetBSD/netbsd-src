/*	$NetBSD: args.c,v 1.88 2024/12/12 05:51:50 rillig Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1985 Sun Microsystems, Inc.
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: args.c,v 1.88 2024/12/12 05:51:50 rillig Exp $");

/* Read options from profile files and from the command line. */

#include <err.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "indent.h"

#if __STDC_VERSION__ >= 201112L
#define get_offset(name, type) \
	_Generic((&opt.name), type *: offsetof(struct options, name))
#else
#define get_offset(name, type) (offsetof(struct options, name))
#endif

#define bool_option(name, value, var) \
	{name, true, false, value, 0, 0, get_offset(var, bool)}
#define bool_options(name, var) \
	{name, true, true, false, 0, 0, get_offset(var, bool)}
#define int_option(name, var, min, max) \
	{name, false, false, false, min, max, get_offset(var, int)}

/* See set_special_option for special options. */
static const struct pro {
	const char p_name[5];	/* e.g. "bl", "cli" */
	bool p_is_bool:1;
	bool p_may_negate:1;
	bool p_bool_value:1;	/* only relevant if !p_may_negate */
	short i_min;
	short i_max;
	unsigned short opt_offset;	/* the associated variable */
} pro[] = {
	bool_options("bacc", blank_line_around_conditional_compilation),
	bool_options("bad", blank_line_after_decl),
	bool_options("badp", blank_line_after_decl_at_top),
	bool_options("bap", blank_line_after_proc),
	bool_options("bbb", blank_line_before_block_comment),
	bool_options("bc", break_after_comma),
	bool_option("bl", false, brace_same_line),
	bool_option("br", true, brace_same_line),
	bool_options("bs", blank_after_sizeof),
	int_option("c", comment_column, 1, 999),
	int_option("cd", decl_comment_column, 1, 999),
	bool_options("cdb", comment_delimiter_on_blank_line),
	bool_options("ce", cuddle_else),
	int_option("ci", continuation_indent, 0, 999),
	/* "cli" is special */
	bool_options("cs", space_after_cast),
	int_option("d", unindent_displace, -999, 999),
	int_option("di", decl_indent, 0, 999),
	bool_options("dj", left_justify_decl),
	bool_options("eei", extra_expr_indent),
	bool_options("ei", else_if_in_same_line),
	bool_options("fbs", function_brace_split),
	bool_options("fc1", format_col1_comments),
	bool_options("fcb", format_block_comments),
	int_option("i", indent_size, 1, 80),
	bool_options("ip", indent_parameters),
	int_option("l", max_line_length, 1, 999),
	int_option("lc", block_comment_max_line_length, 1, 999),
	int_option("ldi", local_decl_indent, 0, 999),
	bool_options("lp", lineup_to_parens),
	bool_options("lpl", lineup_to_parens_always),
	/* "npro" is special */
	/* "P" is special */
	bool_options("pcs", proc_calls_space),
	bool_options("psl", procnames_start_line),
	bool_options("sc", star_comment_cont),
	bool_options("sob", swallow_optional_blank_lines),
	/* "st" is special */
	bool_option("ta", true, auto_typedefs),
	/* "T" is special */
	int_option("ts", tabsize, 1, 80),
	/* "U" is special */
	bool_options("ut", use_tabs),
	bool_options("v", verbose),
	/* "-version" is special */
};


static void
add_typedefs_from_file(const char *fname)
{
	FILE *file;
	char line[BUFSIZ];

	if ((file = fopen(fname, "r")) == NULL) {
		(void)fprintf(stderr, "indent: cannot open file %s\n", fname);
		exit(1);
	}
	while ((fgets(line, sizeof(line), file)) != NULL) {
		/* Only keep the first word of the line. */
		line[strcspn(line, " \t\n\r")] = '\0';
		register_typename(line);
	}
	(void)fclose(file);
}

static bool
set_special_option(const char *arg, const char *option_source)
{
	const char *arg_end;

	if (strcmp(arg, "-version") == 0) {
		printf("NetBSD indent 2.1\n");
		exit(0);
	}

	if (arg[0] == 'P' || strcmp(arg, "npro") == 0)
		return true;	/* see load_profiles */

	if (strncmp(arg, "cli", 3) == 0) {
		arg_end = arg + 3;
		if (arg_end[0] == '\0')
			goto need_arg;
		char *end;
		opt.case_indent = (float)strtod(arg_end, &end);
		if (*end != '\0')
			errx(1, "%s: argument \"%s\" to option \"-%.*s\" "
			    "must be numeric",
			    option_source, arg_end, (int)(arg_end - arg), arg);
		return true;
	}

	if (strcmp(arg, "st") == 0) {
		if (in.f == NULL)
			in.f = stdin;
		if (output == NULL)
			output = stdout;
		return true;
	}

	if (arg[0] == 'T') {
		arg_end = arg + 1;
		if (arg_end[0] == '\0')
			goto need_arg;
		register_typename(arg_end);
		return true;
	}

	if (arg[0] == 'U') {
		arg_end = arg + 1;
		if (arg_end[0] == '\0')
			goto need_arg;
		add_typedefs_from_file(arg_end);
		return true;
	}

	return false;

need_arg:
	errx(1, "%s: option \"-%.*s\" requires an argument",
	    option_source, (int)(arg_end - arg), arg);
	/* NOTREACHED */
}

static const char *
skip_over(const char *s, bool may_negate, const char *prefix)
{
	if (may_negate && s[0] == 'n')
		s++;
	while (*prefix != '\0') {
		if (*prefix++ != *s++)
			return NULL;
	}
	return s;
}

void
set_option(const char *arg, const char *option_source)
{
	const struct pro *p;
	const char *arg_arg;

	arg++;			/* skip leading '-' */
	if (set_special_option(arg, option_source))
		return;

	for (p = pro + array_length(pro); p-- != pro;) {
		arg_arg = skip_over(arg, p->p_may_negate, p->p_name);
		if (arg_arg != NULL)
			goto found;
	}
	errx(1, "%s: unknown option \"-%s\"", option_source, arg);
found:

	if (p->p_is_bool) {
		if (arg_arg[0] != '\0')
			errx(1, "%s: unknown option \"-%s\"",
			    option_source, arg);

		*(bool *)((unsigned char *)(void *)&opt + p->opt_offset) =
		    p->p_may_negate ? arg[0] != 'n' : p->p_bool_value;
		return;
	}

	char *end;
	long num = strtol(arg_arg, &end, 10);
	if (*end != '\0')
		errx(1, "%s: argument \"%s\" to option \"-%s\" "
		    "must be an integer",
		    option_source, arg_arg, p->p_name);

	if (!(ch_isdigit(*arg_arg) && p->i_min <= num && num <= p->i_max))
		errx(1,
		    "%s: argument \"%s\" to option \"-%s\" "
		    "must be between %d and %d",
		    option_source, arg_arg, p->p_name, p->i_min, p->i_max);

	*(int *)((unsigned char *)(void *)&opt + p->opt_offset) = (int)num;
}

static void
load_profile(const char *fname, bool must_exist)
{
	FILE *f;

	if ((f = fopen(fname, "r")) == NULL) {
		if (must_exist)
			err(EXIT_FAILURE, "profile %s", fname);
		return;
	}

	for (;;) {
		char buf[BUFSIZ];
		size_t n = 0;
		int ch, comment_ch = -1;

		while ((ch = getc(f)) != EOF) {
			if (ch == '*' && comment_ch == -1
			    && n > 0 && buf[n - 1] == '/') {
				n--;
				comment_ch = '*';
			} else if (comment_ch != -1) {
				comment_ch = ch == '/' && comment_ch == '*'
				    ? -1 : ch;
			} else if (ch_isspace((char)ch)) {
				break;
			} else if (n >= array_length(buf) - 2) {
				errx(1, "buffer overflow in %s, "
				    "starting with '%.10s'",
				    fname, buf);
			} else
				buf[n++] = (char)ch;
		}

		if (n > 0) {
			buf[n] = '\0';
			if (opt.verbose)
				(void)fprintf(stderr, "profile: %s\n", buf);
			if (buf[0] != '-')
				errx(1,
				    "%s: option \"%s\" must start with '-'",
				    fname, buf);
			set_option(buf, fname);
		}
		if (ch == EOF)
			break;
	}
	(void)fclose(f);
}

void
load_profile_files(const char *path)
{
	if (path != NULL)
		load_profile(path, true);
	else {
		const char *home = getenv("HOME");
		if (home != NULL) {
			char fname[PATH_MAX];
			snprintf(fname, sizeof(fname), "%s/.indent.pro", home);
			load_profile(fname, false);
		}
	}
	load_profile(".indent.pro", false);
}
