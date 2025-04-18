/*	$NetBSD: options.c,v 1.62 2024/10/14 09:10:35 kre Exp $	*/

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
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)options.c	8.2 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: options.c,v 1.62 2024/10/14 09:10:35 kre Exp $");
#endif
#endif /* not lint */

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "shell.h"
#define DEFINE_OPTIONS
#include "options.h"
#undef DEFINE_OPTIONS
#include "builtins.h"
#include "nodes.h"	/* for other header files */
#include "eval.h"
#include "jobs.h"
#include "input.h"
#include "output.h"
#include "trap.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#include "syntax.h"
#ifndef SMALL
#include "myhistedit.h"
#endif
#include "show.h"

char *arg0;			/* value of $0 */
struct shparam shellparam;	/* current positional parameters */
char **argptr;			/* argument list for builtin commands */
char *optionarg;		/* set by nextopt (like getopt) */
char *optptr;			/* used by nextopt */

char *minusc;			/* argument to -c option */


STATIC void options(int);
STATIC void minus_o(char *, int);
STATIC void setoption(int, int);
STATIC int getopts(char *, char *, char **, char ***, char **);


/*
 * Process the shell command line arguments.
 */

void
procargs(int argc, char **argv)
{
	size_t i;
	int psx;

	argptr = argv;
	if (argc > 0)
		argptr++;

	psx = posix;		/* save what we set it to earlier */
	/*
	 * option values are mostly boolean 0:off 1:on
	 * we use 2 (just in this routine) to mean "unknown yet"
	 */
	for (i = 0; i < NOPTS; i++)
		optlist[i].val = 2;
	posix = psx;		/* restore before processing -o ... */

	options(1);

	if (*argptr == NULL && minusc == NULL)
		sflag = 1;
	if (iflag == 2 && sflag == 1 && isatty(0) && isatty(2))
		iflag = 1;
	if (iflag == 1 && sflag == 2)
		iflag = 2;
	if (mflag == 2)
		mflag = iflag;
#ifndef DO_SHAREDVFORK
	if (usefork == 2)
		usefork = 1;
#endif
#if DEBUG >= 2
	if (debug == 2)
		debug = 1;
#endif
	arg0 = argv[0];
	if (loginsh == 2 && arg0 != NULL && arg0[0] == '-')
		loginsh = 1;

	/*
	 * Any options not dealt with as special cases just above,
	 * and which were not set on the command line, are set to
	 * their expected default values (mostly "off")
	 *
	 * then as each option is initialised, save its setting now
	 * as its "default" value for future use ("set -o default").
	 */
	for (i = 0; i < NOPTS; i++) {
		if (optlist[i].val == 2)
			optlist[i].val = optlist[i].dflt;
		optlist[i].dflt = optlist[i].val;
	}

	if (sflag == 0 && minusc == NULL) {
		commandname = argv[0];
		arg0 = *argptr++;
		setinputfile(arg0, 0);
		commandname = arg0;
	}
	/* POSIX 1003.2: first arg after -c cmd is $0, remainder $1... */
	if (minusc != NULL) {
		if (argptr == NULL || *argptr == NULL)
			error("Bad -c option");
		minusc = *argptr++;
		if (*argptr != 0)
			arg0 = *argptr++;
	}

	shellparam.p = argptr;
	shellparam.reset = 1;
	/* assert(shellparam.malloc == 0 && shellparam.nparam == 0); */
	while (*argptr) {
		shellparam.nparam++;
		argptr++;
	}
	optschanged();
}


void
optschanged(void)
{
	setinteractive(iflag);
#ifndef SMALL
	histedit();
#endif
	setjobctl(mflag);

	if (privileged && !pflag) {
		setuid(getuid());
		setgid(getgid());
		privileged = 0;
		setvarsafe("PSc", (getuid() == 0 ? "#" : "$"), 0);
	}
}

/*
 * Process shell options.  The global variable argptr contains a pointer
 * to the argument list; we advance it past the options.
 */

STATIC void
options(int cmdline)
{
	static char empty[] = "";
	char *p;
	int val;
	int c;

	if (cmdline)
		minusc = NULL;
	while ((p = *argptr) != NULL) {
		argptr++;
		if ((c = *p++) == '-') {
			val = 1;
                        if (p[0] == '\0' || (p[0] == '-' && p[1] == '\0')) {
                                if (!cmdline) {
                                        /* "-" means turn off -x and -v */
                                        if (p[0] == '\0')
                                                xflag = vflag = 0;
                                        /* "--" means reset params */
                                        else if (*argptr == NULL)
						setparam(argptr);
                                }
				break;	  /* "-" or  "--" terminates options */
			}
		} else if (c == '+') {
			val = 0;
		} else {
			argptr--;
			break;
		}
		while ((c = *p++) != '\0') {
			if (val == 1 && c == 'c' && cmdline) {
				/* command is after shell args*/
				minusc = empty;
			} else if (c == 'o') {
				if (*p != '\0')
					minus_o(p, val + (cmdline ? val : 0));
				else if (*argptr)
					minus_o(*argptr++,
					    val + (cmdline ? val : 0));
				else if (!cmdline)
					minus_o(NULL, val);
				else
					error("arg for %co missing", "+-"[val]);
				break;
#ifdef DEBUG
			} else if (c == 'D') {
				if (*p) {
					set_debug(p, val);
					break;
				} else if (*argptr)
					set_debug(*argptr++, val);
				else
					set_debug("*$", val);
#endif
			} else if (cmdline && c == 'r') {
				out1fmt("NetBSD shell: %s\n",
				    lookupvar("NETBSD_SHELL"));
				sh_exit(0);
			} else {
				setoption(c, val);
			}
		}
	}
}

static void
set_opt_val(size_t i, int val)
{
	size_t j;
	int flag;

	if (val && (flag = optlist[i].opt_set)) {
		/* some options (eg vi/emacs) are mutually exclusive */
		for (j = 0; j < NOPTS; j++)
		    if (optlist[j].opt_set == flag)
			optlist[j].val = 0;
	}
#ifndef SMALL
	if (i == _SH_OPT_Xflag)
		xtracefdsetup(val);
#endif
	optlist[i].val = val;
#ifdef DEBUG
	if (&optlist[i].val == &debug)
		opentrace();	/* different "trace" than the -x one... */
#endif
}

STATIC void
minus_o(char *name, int val)
{
	size_t i;
	const char *sep = ": ";

	if (name == NULL) {
		if (val) {
			out1str("Current option settings");
			for (i = 0; i < NOPTS; i++) {
				if (optlist[i].name == NULL)  {
					out1fmt("%s%c%c", sep,
					    "+-"[optlist[i].val],
					    optlist[i].letter);
					sep = ", ";
				}
			}
			out1c('\n');
			for (i = 0; i < NOPTS; i++) {
				if (optlist[i].name)
				    out1fmt("%-19s %s\n", optlist[i].name,
					optlist[i].val ? "on" : "off");
			}
		} else {
			out1str("set -o default");
			for (i = 0; i < NOPTS; i++) {
				if (optlist[i].val == optlist[i].dflt)
					continue;
				if (optlist[i].name)
				    out1fmt(" %co %s",
					"+-"[optlist[i].val], optlist[i].name);
				else
				    out1fmt(" %c%c", "+-"[optlist[i].val],
					optlist[i].letter);
			}
			out1c('\n');
		}
	} else {
		if (val == 1 && equal(name, "default")) { /* special case */
			for (i = 0; i < NOPTS; i++)
				set_opt_val(i, optlist[i].dflt);
			return;
		}
		if (val)
			val = 1;
		for (i = 0; i < NOPTS; i++)
			if (optlist[i].name && equal(name, optlist[i].name)) {
				set_opt_val(i, val);
#ifndef SMALL
				if (i == _SH_OPT_Xflag)
					set_opt_val(_SH_OPT_xflag, val);
#endif
				return;
			}
		error("Unknown option %co %s", "+-"[val], name);
	}
}


STATIC void
setoption(int flag, int val)
{
	size_t i;

	for (i = 0; i < NOPTS; i++)
		if (optlist[i].letter == flag) {
			set_opt_val(i, val);
#ifndef SMALL
			if (i == _SH_OPT_Xflag)
				set_opt_val(_SH_OPT_xflag, val);
#endif
			return;
		}
	error("Unknown option %c%c", "+-"[val], flag);
	/* NOTREACHED */
}



#ifdef mkinit
INCLUDE "options.h"

SHELLPROC {
	int i;

	for (i = 0; optlist[i].name; i++)
		optlist[i].val = 0;
	optschanged();

}
#endif


/*
 * Set the shell parameters.
 */

void
setparam(char **argv)
{
	char **newparam;
	char **ap;
	int nparam;

	for (nparam = 0 ; argv[nparam] ; nparam++)
		continue;
	ap = newparam = ckmalloc((nparam + 1) * sizeof *ap);
	while (*argv) {
		*ap++ = savestr(*argv++);
	}
	*ap = NULL;
	freeparam(&shellparam);
	shellparam.malloc = 1;
	shellparam.nparam = nparam;
	shellparam.p = newparam;
	shellparam.optnext = NULL;
}


/*
 * Free the list of positional parameters.
 */

void
freeparam(volatile struct shparam *param)
{
	char **ap;

	if (param->malloc) {
		for (ap = param->p ; *ap ; ap++)
			ckfree(*ap);
		ckfree(param->p);
	}
}



/*
 * The shift builtin command.
 */

#ifndef TINY
/* first the rotate variant */
static inline int
rotatecmd(int argc, char **argv)
{
	int n;
	char **ap1, **ap2, **ss;

	(void) nextopt(NULL);	/* ignore '--' as leading option */

	/*
	 * half this is just in case it ever becomes
	 * a separate named command, while it remains
	 * puerly an inline inside shift, the compiler
	 * should optimise most of it to nothingness
	 */
	if (argptr[0] && argptr[1])
		error("Usage: rotate [n]");
	n = 1;
	if (*argptr) {
		if (**argptr == '-')
			n = number(*argptr + 1);
		else		/* anti-clockwise n == clockwise $# - n */
			n = shellparam.nparam - number(*argptr);
	}

	if (n == 0 || n == shellparam.nparam)		/* nothing to do */
		return 0;

	if (n < 0 || n > shellparam.nparam)
		error("can't rotate that many");

	ap2 = ss = (char **)stalloc(n * sizeof(char *));
	INTOFF;
	for (ap1 = shellparam.p + shellparam.nparam - n;
	     ap1 < shellparam.p + shellparam.nparam; )
		*ap2++ = *ap1++;
	for (ap2 = shellparam.p + shellparam.nparam, ap1 = ap2 - n;
	     ap1 > shellparam.p; )
		*--ap2 = *--ap1;
	for (ap1 = ss + n; ap1 > ss; )
		*--ap2 = *--ap1;
	shellparam.optnext = NULL;
	INTON;
	stunalloc(ss);

	return 0;
}
#endif

int
shiftcmd(int argc, char **argv)
{
	int n;
	char **ap1, **ap2;

	(void) nextopt(NULL);	/* ignore '--' as leading option */

	if (argptr[0] && argptr[1])
		error("Usage: shift [n]");

#ifndef TINY
	if (*argptr && **argptr == '-') {
		argptr = argv + 1;	/* reinit nextopt() */
		optptr = NULL;
		return rotatecmd(argc, argv);
	}
#endif

	n = 1;
	if (*argptr)
		n = number(*argptr);
	if (n > shellparam.nparam)
		error("can't shift that many");
	INTOFF;
	shellparam.nparam -= n;
	for (ap1 = shellparam.p ; --n >= 0 ; ap1++) {
		if (shellparam.malloc)
			ckfree(*ap1);
	}
	ap2 = shellparam.p;
	while ((*ap2++ = *ap1++) != NULL)
		continue;
	shellparam.optnext = NULL;
	INTON;
	return 0;
}



/*
 * The set command builtin.
 */

int
setcmd(int argc, char **argv)
{
	if (argc == 1)
		return showvars(0, 0, 1, 0);
	INTOFF;
	options(0);
	optschanged();
	if (*argptr != NULL) {
		setparam(argptr);
	}
	INTON;
	return 0;
}


void
getoptsreset(char *value, int flags __unused)
{
	/*
	 * This is just to detect the case where OPTIND=1
	 * is executed.   Any other string assigned to OPTIND
	 * is OK, but is not a reset.   No errors, so cannot use number()
	 */
	if (is_digit(*value) && strtol(value, NULL, 10) == 1) {
		shellparam.optnext = NULL;
		shellparam.reset = 1;
	}
}

/*
 * The getopts builtin.  Shellparam.optnext points to the next argument
 * to be processed.  Shellparam.optptr points to the next character to
 * be processed in the current argument.  If shellparam.optnext is NULL,
 * then it's the first time getopts has been called.
 */

int
getoptscmd(int argc, char **argv)
{
	char **optbase;

	if (argc < 3)
		error("usage: getopts optstring var [arg]");
	else if (argc == 3)
		optbase = shellparam.p;
	else
		optbase = &argv[3];

	if (shellparam.reset == 1) {
		shellparam.optnext = optbase;
		shellparam.optptr = NULL;
		shellparam.reset = 0;
	}

	return getopts(argv[1], argv[2], optbase, &shellparam.optnext,
		       &shellparam.optptr);
}

STATIC int
getopts(char *optstr, char *optvar, char **optfirst, char ***optnext, char **optpptr)
{
	char *p, *q;
	char c = '?';
	int done = 0;
	int ind = 0;
	int err = 0;
	char s[12];

	if ((p = *optpptr) == NULL || *p == '\0') {
		/* Current word is done, advance */
		if (*optnext == NULL)
			return 1;
		p = **optnext;
		if (p == NULL || *p != '-' || *++p == '\0') {
atend:
			ind = *optnext - optfirst + 1;
			*optnext = NULL;
			p = NULL;
			done = 1;
			goto out;
		}
		(*optnext)++;
		if (p[0] == '-' && p[1] == '\0')	/* check for "--" */
			goto atend;
	}

	c = *p++;
	for (q = optstr; *q != c; ) {
		if (*q == '\0') {
			if (optstr[0] == ':') {
				s[0] = c;
				s[1] = '\0';
				err |= setvarsafe("OPTARG", s, 0);
			} else {
				outfmt(&errout, "Unknown option -%c\n", c);
				(void) unsetvar("OPTARG", 0);
			}
			c = '?';
			goto bad;
		}
		if (*++q == ':')
			q++;
	}

	if (*++q == ':') {
		if (*p == '\0' && (p = **optnext) == NULL) {
			if (optstr[0] == ':') {
				s[0] = c;
				s[1] = '\0';
				err |= setvarsafe("OPTARG", s, 0);
				c = ':';
			} else {
				outfmt(&errout, "No arg for -%c option\n", c);
				(void) unsetvar("OPTARG", 0);
				c = '?';
			}
			goto bad;
		}

		if (p == **optnext)
			(*optnext)++;
		err |= setvarsafe("OPTARG", p, 0);
		p = NULL;
	} else
		err |= setvarsafe("OPTARG", "", 0);
	ind = *optnext - optfirst + 1;
	goto out;

bad:
	ind = 1;
	*optnext = NULL;
	p = NULL;
out:
	*optpptr = p;
	fmtstr(s, sizeof(s), "%d", ind);
	err |= setvarsafe("OPTIND", s, VNOFUNC);
	s[0] = c;
	s[1] = '\0';
	err |= setvarsafe(optvar, s, 0);
	if (err) {
		*optnext = NULL;
		*optpptr = NULL;
		flushall();
		exraise(EXERROR);
	}
	return done;
}

/*
 * XXX - should get rid of.  have all builtins use getopt(3).  the
 * library getopt must have the BSD extension static variable "optreset"
 * otherwise it can't be used within the shell safely.
 *
 * Standard option processing (a la getopt) for builtin routines.  The
 * only argument that is passed to nextopt is the option string; the
 * other arguments are unnecessary.  It return the character, or '\0' on
 * end of input.  If optstring is NULL, then there are no options, and
 * args are allowed to begin with '-', but a single leading "--" will be
 * discarded.   This is for some POSIX special builtins that require
 * -- processing, have no args, and we never did opt processing before
 * and need to retain backwards compat.
 */

int
nextopt(const char *optstring)
{
	char *p;
	const char *q;
	char c;

	if ((p = optptr) == NULL || *p == '\0') {
		p = *argptr;
		if (p == NULL || *p != '-' || *++p == '\0')
			return '\0';
		argptr++;
		if (p[0] == '-' && p[1] == '\0')	/* check for "--" */
			return '\0';
		if (optstring == NULL)	/* not processing the "option" */
			argptr--;	/* so make it be an arg again */
	}
	if (optstring == NULL)
		return '\0';
	c = *p++;
	for (q = optstring ; *q != c ; ) {
		if (*q == '\0')
			error("Unknown option -%c", c);
		if (*++q == ':')
			q++;
	}
	if (*++q == ':') {
		if (*p == '\0' && (p = *argptr++) == NULL)
			error("No arg for -%c option", c);
		optionarg = p;
		p = NULL;
	}
	optptr = p;
	return c;
}
