/*	$NetBSD: fparseln.c,v 1.2 2025/02/11 17:48:30 christos Exp $	*/

/*
 * Copyright (c) 1997 Christos Zoulas.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: fparseln.c,v 1.2 2025/02/11 17:48:30 christos Exp $");
#endif /* LIBC_SCCS and not lint */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if ! HAVE_FPARSELN || BROKEN_FPARSELN

#define FLOCKFILE(fp)
#define FUNLOCKFILE(fp)

#if defined(_REENTRANT) && !HAVE_NBTOOL_CONFIG_H
#define __fgetln(f, l) __fgetstr(f, l, '\n')
#else
#define __fgetln(f, l) fgetln(f, l)
#endif

static int isescaped(const char *, const char *, int);

/* isescaped():
 *	Return true if the character in *p that belongs to a string
 *	that starts in *sp, is escaped by the escape character esc.
 */
static int
isescaped(const char *sp, const char *p, int esc)
{
	const char     *cp;
	size_t		ne;

	/* No escape character */
	if (esc == '\0')
		return 0;

	/* Count the number of escape characters that precede ours */
	for (ne = 0, cp = p; --cp >= sp && *cp == esc; ne++)
		continue;

	/* Return true if odd number of escape characters */
	return (ne & 1) != 0;
}


/* fparseln():
 *	Read a line from a file parsing continuations ending in \
 *	and eliminating trailing newlines, or comments starting with
 *	the comment char.
 */
char *
fparseln(FILE *fp, size_t *size, size_t *lineno, const char str[3], int flags)
{
	static const char dstr[3] = { '\\', '\\', '#' };

	size_t	s, len;
	char   *buf;
	char   *ptr, *cp;
	int	cnt;
	char	esc, con, nl, com;

	len = 0;
	buf = NULL;
	cnt = 1;

	if (str == NULL)
		str = dstr;

	esc = str[0];
	con = str[1];
	com = str[2];
	/*
	 * XXX: it would be cool to be able to specify the newline character,
	 * but unfortunately, fgetln does not let us
	 */
	nl  = '\n';

	FLOCKFILE(fp);

	while (cnt) {
		cnt = 0;

		if (lineno)
			(*lineno)++;

		if ((ptr = __fgetln(fp, &s)) == NULL)
			break;

		if (s && com) {		/* Check and eliminate comments */
			for (cp = ptr; cp < ptr + s; cp++)
				if (*cp == com && !isescaped(ptr, cp, esc)) {
					s = cp - ptr;
					cnt = s == 0 && buf == NULL;
					break;
				}
		}

		if (s && nl) { 		/* Check and eliminate newlines */
			cp = &ptr[s - 1];

			if (*cp == nl)
				s--;	/* forget newline */
		}

		if (s && con) {		/* Check and eliminate continuations */
			cp = &ptr[s - 1];

			if (*cp == con && !isescaped(ptr, cp, esc)) {
				s--;	/* forget continuation char */
				cnt = 1;
			}
		}

		if (s == 0) {
			/*
			 * nothing to add, skip realloc except in case
			 * we need a minimal buf to return an empty line
			 */
			if (cnt || buf != NULL)
				continue;
		}

		if ((cp = realloc(buf, len + s + 1)) == NULL) {
			FUNLOCKFILE(fp);
			free(buf);
			return NULL;
		}
		buf = cp;

		(void) memcpy(buf + len, ptr, s);
		len += s;
		buf[len] = '\0';
	}

	FUNLOCKFILE(fp);

	if ((flags & FPARSELN_UNESCALL) != 0 && esc && buf != NULL &&
	    strchr(buf, esc) != NULL) {
		ptr = cp = buf;
		while (cp[0] != '\0') {
			int skipesc;

			while (cp[0] != '\0' && cp[0] != esc)
				*ptr++ = *cp++;
			if (cp[0] == '\0' || cp[1] == '\0')
				break;

			skipesc = 0;
			if (cp[1] == com)
				skipesc += (flags & FPARSELN_UNESCCOMM);
			if (cp[1] == con)
				skipesc += (flags & FPARSELN_UNESCCONT);
			if (cp[1] == esc)
				skipesc += (flags & FPARSELN_UNESCESC);
			if (cp[1] != com && cp[1] != con && cp[1] != esc)
				skipesc = (flags & FPARSELN_UNESCREST);

			if (skipesc)
				cp++;
			else
				*ptr++ = *cp++;
			*ptr++ = *cp++;
		}
		*ptr = '\0';
		len = strlen(buf);
	}

	if (size)
		*size = len;
	return buf;
}

#ifdef TEST

int main(int, char **);

int
main(int argc, char **argv)
{
	char   *ptr;
	size_t	size, line;

	line = 0;
	while ((ptr = fparseln(stdin, &size, &line, NULL,
	    FPARSELN_UNESCALL)) != NULL)
		printf("line %d (%d) |%s|\n", line, size, ptr);
	return 0;
}

/*

# This is a test
line 1
line 2 \
line 3 # Comment
line 4 \# Not comment \\\\

# And a comment \
line 5 \\\
line 6

*/

#endif /* TEST */
#endif	/* ! HAVE_FPARSELN || BROKEN_FPARSELN */
