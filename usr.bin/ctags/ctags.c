/*	$NetBSD: ctags.c,v 1.17 2024/10/30 11:37:00 kre Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994, 1995
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
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
#if defined(__COPYRIGHT) && !defined(lint)
__COPYRIGHT("@(#) Copyright (c) 1987, 1993, 1994, 1995\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#if defined(__RCSID) && !defined(lint)
#if 0
static char sccsid[] = "@(#)ctags.c	8.4 (Berkeley) 2/7/95";
#endif
__RCSID("$NetBSD: ctags.c,v 1.17 2024/10/30 11:37:00 kre Exp $");
#endif /* not lint */

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "ctags.h"

/*
 * ctags: create a tags file
 */

NODE	*head;			/* head of the sorted binary tree */

				/* boolean "func" (see init()) */
bool	_wht[256], _etk[256], _itk[256], _btk[256], _gd[256];

FILE	*inf;			/* ioptr for current input file */
FILE	*outf;			/* ioptr for tags file */

long	lineftell;		/* ftell after getc( inf ) == '\n' */

int	lineno;			/* line number of current line */
int	dflag;			/* -d: non-macro defines */
int	tflag;			/* -t: create tags for typedefs */
int	vflag;			/* -v: vgrind style index output */
int	wflag;			/* -w: suppress warnings */
int	xflag;			/* -x: cxref style output */

char	*curfile;		/* current input file name */
char	searchar = '/';		/* use /.../ searches by default */
char	lbuf[LINE_MAX];

void	init(void);
void	find_entries(char *);

int
main(int argc, char **argv)
{
	static const char	*outfile = "tags";	/* output file */
	int	aflag;				/* -a: append to tags */
	int	uflag;				/* -u: update tags */
	int	exit_val;			/* exit value */
	int	step;				/* step through args */
	int	ch;				/* getopts char */
	char	cmd[100];			/* too ugly to explain */

	aflag = uflag = NO;
	while ((ch = getopt(argc, argv, "BFadf:tuwvx")) != -1)
		switch(ch) {
		case 'B':
			searchar = '?';
			break;
		case 'F':
			searchar = '/';
			break;
		case 'a':
			aflag++;
			break;
		case 'd':
			dflag++;
			break;
		case 'f':
			outfile = optarg;
			break;
		case 't':
			tflag++;
			break;
		case 'u':
			uflag++;
			break;
		case 'w':
			wflag++;
			break;
		case 'v':
			vflag++;
			/* FALLTHROUGH */
		case 'x':
			xflag++;
			break;
		case '?':
		default:
			goto usage;
		}
	argv += optind;
	argc -= optind;
	if (!argc) {
 usage:;	(void)fprintf(stderr,
			"usage: ctags [-BFadtuwvx] [-f tagsfile] file ...\n");
		exit(EXIT_FAILURE);
	}

	init();

	exit_val = EXIT_SUCCESS;
	for (step = 0; step < argc; ++step)
		if (!(inf = fopen(argv[step], "r"))) {
			warn("%s", argv[step]);
			exit_val = EXIT_FAILURE;
		}
		else {
			curfile = argv[step];
			find_entries(argv[step]);
			(void)fclose(inf);
		}

	if (head) {
		if (xflag)
			put_entries(head);
		else {
			if (uflag) {
				for (step = 0; step < argc; step++) {
					if (snprintf(cmd, sizeof(cmd),
					    "mv %s OTAGS &&\n"
					      "\tfgrep -v '\t%s\t' OTAGS >%s &&"
					      "\n\trm OTAGS",
					    outfile, argv[step], outfile)
							>= (int)sizeof(cmd))
						errx(EXIT_FAILURE,
						  "Command to update %s for -u"
						     " %s too long",
						  argv[step], outfile);
					 if (system(cmd) != 0)
						errx(EXIT_FAILURE,
						  "Update (-u) of %s failed.   "
						  "Cmd:\n    %s", outfile, cmd);
				}
				++aflag;
			}
			if (!(outf = fopen(outfile, aflag ? "a" : "w")))
				err(EXIT_FAILURE, "%s", outfile);
			put_entries(head);
			(void)fflush(outf);
			if (ferror(outf))
				err(EXIT_FAILURE, "output error (%s)", outfile);
			(void)fclose(outf);
			if (uflag) {
				if (snprintf(cmd, sizeof(cmd), "sort -o %s %s",
				    outfile, outfile) >= (int)sizeof(cmd))
					errx(EXIT_FAILURE,
					    "sort command (-u) for %s too long",
					    outfile);
				if (system(cmd) != 0)
					errx(EXIT_FAILURE, "-u: sort %s failed"
					    "\t[ %s ]", outfile, cmd);
			}
		}
	}
	if ((vflag || xflag) && (fflush(stdout) != 0 || ferror(stdout) != 0))
		errx(EXIT_FAILURE, "write error (stdout)");
	exit(exit_val);
}

/*
 * init --
 *	this routine sets up the boolean pseudo-functions which work by
 *	setting boolean flags dependent upon the corresponding character.
 *	Every char which is NOT in that string is false with respect to
 *	the pseudo-function.  Therefore, all of the array "_wht" is NO
 *	by default and then the elements subscripted by the chars in
 *	CWHITE are set to YES.  Thus, "_wht" of a char is YES if it is in
 *	the string CWHITE, else NO.
 */
void
init(void)
{
	int		i;
	const char	*sp;

	for (i = 0; i < 256; i++) {
		_wht[i] = _etk[i] = _itk[i] = _btk[i] = NO;
		_gd[i] = YES;
	}
#define	CWHITE	" \f\t\n"
	for (sp = CWHITE; *sp; sp++)	/* white space chars */
		_wht[(unsigned)*sp] = YES;
#define	CTOKEN	" \t\n\"'#()[]{}=-+%*/&|^~!<>;,.:?"
	for (sp = CTOKEN; *sp; sp++)	/* token ending chars */
		_etk[(unsigned)*sp] = YES;
#define	CINTOK	"ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz0123456789"
	for (sp = CINTOK; *sp; sp++)	/* valid in-token chars */
		_itk[(unsigned)*sp] = YES;
#define	CBEGIN	"ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz"
	for (sp = CBEGIN; *sp; sp++)	/* token starting chars */
		_btk[(unsigned)*sp] = YES;
#define	CNOTGD	",;"
	for (sp = CNOTGD; *sp; sp++)	/* invalid after-function chars */
		_gd[(unsigned)*sp] = NO;
}

/*
 * find_entries --
 *	this routine opens the specified file and calls the function
 *	which searches the file.
 */
void
find_entries(char *file)
{
	char	*cp;

	lineno = 0;				/* should be 1 ?? KB */
	if ((cp = strrchr(file, '.')) != NULL) {
		if (cp[1] == 'l' && !cp[2]) {
			int	c;

			for (;;) {
				if (GETC(==, EOF))
					return;
				if (!iswhite(c)) {
					rewind(inf);
					break;
				}
			}
#define	LISPCHR	";(["
/* lisp */		if (strchr(LISPCHR, c)) {
				l_entries();
				return;
			}
/* lex */		else {
				/*
				 * we search all 3 parts of a lex file
				 * for C references.  This may be wrong.
				 */
				toss_yysec();
				(void)strlcpy(lbuf, "%%$", sizeof(lbuf));
				pfnote("yylex", lineno);
				rewind(inf);
			}
		}
/* yacc */	else if (cp[1] == 'y' && !cp[2]) {
			/*
			 * we search only the 3rd part of a yacc file
			 * for C references.  This may be wrong.
			 */
			toss_yysec();
			(void)strlcpy(lbuf, "%%$", sizeof(lbuf));
			pfnote("yyparse", lineno);
			y_entries();
		}
/* fortran */	else if ((cp[1] != 'c' && cp[1] != 'h') && !cp[2]) {
			if (PF_funcs())
				return;
			rewind(inf);
		}
	}
/* C */	c_entries();
}
