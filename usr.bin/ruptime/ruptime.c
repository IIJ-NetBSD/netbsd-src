/*	$NetBSD: ruptime.c,v 1.16 2025/09/19 05:07:38 mrg Exp $	*/

/*
 * Copyright (c) 1983, 1993, 1994
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

#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1983, 1993, 1994\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
/*static char sccsid[] = "from: @(#)ruptime.c	8.2 (Berkeley) 4/5/94";*/
__RCSID("$NetBSD: ruptime.c,v 1.16 2025/09/19 05:07:38 mrg Exp $");
#endif /* not lint */

#include <sys/param.h>

#include <protocols/rwhod.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tzfile.h>
#include <unistd.h>

static struct hs {
	struct	whod *hs_wd;
	int	hs_nusers;
} *hs;

#define	ISDOWN(h)	(now - (h)->hs_wd->wd_recvtime > 11 * 60)
#define	WHDRSIZE	(sizeof(struct whod) - \
    sizeof (((struct whod *)0)->wd_we))

static size_t nhosts;
static time_t now;
static int rflg = 1;

static int	 hscmp(const void *, const void *);
static char	*interval(time_t, const char *);
static int	 lcmp(const void *, const void *);
static int	 tcmp(const void *, const void *);
static int	 ucmp(const void *, const void *);
__dead static void	 usage(void);

int
main(int argc, char **argv)
{
	struct dirent *dp;
	struct hs *hsp;
	struct whod *wd;
	struct whoent *we;
	DIR *dirp;
	size_t hspace, i;
	int aflg, cc, ch, fd, maxloadav;
	char buf[sizeof(struct whod)];
	int (*cmp)(const void *, const void *);

	hsp = NULL;
	aflg = 0;
	cmp = hscmp;
	while ((ch = getopt(argc, argv, "alrtu")) != -1)
		switch (ch) {
		case 'a':
			aflg = 1;
			break;
		case 'l':
			cmp = lcmp;
			break;
		case 'r':
			rflg = -1;
			break;
		case 't':
			cmp = tcmp;
			break;
		case 'u':
			cmp = ucmp;
			break;
		default: 
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (chdir(_PATH_RWHODIR) || (dirp = opendir(".")) == NULL)
		err(1, "%s", _PATH_RWHODIR);

	maxloadav = -1;
	for (nhosts = hspace = 0; (dp = readdir(dirp)) != NULL;) {
		if (dp->d_ino == 0 || strncmp(dp->d_name, "whod.", 5))
			continue;
		if ((fd = open(dp->d_name, O_RDONLY, 0)) < 0) {
			warn("%s", dp->d_name);
			continue;
		}
		cc = read(fd, buf, sizeof(struct whod));
		(void)close(fd);

		if (cc < (int)WHDRSIZE)
			continue;
		if (nhosts == hspace) {
			if ((hs =
			    realloc(hs, (hspace += 40) * sizeof(*hs))) == NULL)
				err(1, "realloc");
			hsp = hs + nhosts;
		}

		if ((hsp->hs_wd = malloc(sizeof(*hsp->hs_wd))) == NULL)
			err(1, "malloc");
		memmove(hsp->hs_wd, buf, (size_t)WHDRSIZE);

		for (wd = (struct whod *)buf, i = 0; i < 2; ++i)
			if (wd->wd_loadav[i] > maxloadav)
				maxloadav = wd->wd_loadav[i];

		for (hsp->hs_nusers = 0,
		    we = (struct whoent *)(buf + cc); --we >= wd->wd_we;)
			if (aflg || we->we_idle < 3600)
				++hsp->hs_nusers;
		++hsp;
		++nhosts;
	}
	if (nhosts == 0)
		errx(0, "no hosts in %s", _PATH_RWHODIR);

	(void)time(&now);
	qsort(hs, nhosts, sizeof (hs[0]), cmp);
	for (i = 0; i < nhosts; i++) {
		hsp = &hs[i];
		if (ISDOWN(hsp)) {
			(void)printf("%-12.12s%s\n", hsp->hs_wd->wd_hostname,
			    interval(now - hsp->hs_wd->wd_recvtime, "down"));
			continue;
		}
		(void)printf(
		    "%-12.12s%s,  %4d user%s  load %*.2f, %*.2f, %*.2f\n",
		    hsp->hs_wd->wd_hostname,
		    interval((time_t)hsp->hs_wd->wd_sendtime -
			(time_t)hsp->hs_wd->wd_boottime, "  up"),
		    hsp->hs_nusers,
		    hsp->hs_nusers == 1 ? ", " : "s,",
		    maxloadav >= 1000 ? 5 : 4,
			hsp->hs_wd->wd_loadav[0] / 100.0,
		    maxloadav >= 1000 ? 5 : 4,
		        hsp->hs_wd->wd_loadav[1] / 100.0,
		    maxloadav >= 1000 ? 5 : 4,
		        hsp->hs_wd->wd_loadav[2] / 100.0);
	}
	exit(0);
}

static char *
interval(time_t tval, const char *updown)
{
	static char resbuf[32];
	int days, hours, minutes;

	if (tval < 0) {
		(void)snprintf(resbuf, sizeof(resbuf), "   %s ??:??", updown);
		return (resbuf);
	}
						/* round to minutes. */
	minutes = (tval + (SECSPERMIN - 1)) / SECSPERMIN;
	hours = minutes / MINSPERHOUR;
	minutes %= MINSPERHOUR;
	days = hours / HOURSPERDAY;
	hours %= HOURSPERDAY;
	if (days)
		(void)snprintf(resbuf, sizeof(resbuf),
		    "%s%4d+%02d:%02d", updown, days, hours, minutes);
	else
		(void)snprintf(resbuf, sizeof(resbuf),
		    "%s     %2d:%02d", updown, hours, minutes);
	return (resbuf);
}

#define	HS(a)	((const struct hs *)(a))

/* Alphabetical comparison. */
static int
hscmp(const void *a1, const void *a2)
{
	return (rflg *
	    strcmp(HS(a1)->hs_wd->wd_hostname, HS(a2)->hs_wd->wd_hostname));
}

/* Load average comparison. */
static int
lcmp(const void *a1, const void *a2)
{
	if (ISDOWN(HS(a1))) {
		if (ISDOWN(HS(a2)))
			return (tcmp(a1, a2));
		else
			return (rflg);
	} else if (ISDOWN(HS(a2))) {
		return (-rflg);
	} else
		return (rflg *
		   (HS(a2)->hs_wd->wd_loadav[0] - HS(a1)->hs_wd->wd_loadav[0]));
}

/* Number of users comparison. */
static int
ucmp(const void *a1, const void *a2)
{
	if (ISDOWN(HS(a1))) {
		if (ISDOWN(HS(a2)))
			return (tcmp(a1, a2));
		else
			return (rflg);
	} else if (ISDOWN(HS(a2))) {
		return (-rflg);
	} else
		return (rflg * (HS(a2)->hs_nusers - HS(a1)->hs_nusers));
}

/* Uptime comparison. */
static int
tcmp(const void *a1, const void *a2)
{
	return (rflg * (
		(ISDOWN(HS(a2)) ? HS(a2)->hs_wd->wd_recvtime - now
		    : HS(a2)->hs_wd->wd_sendtime - HS(a2)->hs_wd->wd_boottime)
		-
		(ISDOWN(HS(a1)) ? HS(a1)->hs_wd->wd_recvtime - now
		    : HS(a1)->hs_wd->wd_sendtime - HS(a1)->hs_wd->wd_boottime)
	));
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: ruptime [-alrtu]\n");
	exit(1);
}
