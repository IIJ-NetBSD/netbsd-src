/*	$NetBSD: modetoa.c,v 1.10 2024/08/18 20:47:13 christos Exp $	*/

/*
 * modetoa - return an asciized mode
 */
#include <config.h>
#include <stdio.h>

#include "ntp_stdlib.h"

const char *
modetoa(
	size_t mode
	)
{
	char *bp;
	static const char * const modestrings[] = {
		"unspec",
		"sym_active",
		"sym_passive",
		"client",
		"server",
		"broadcast",
		"control",
		"private",
		"bclient",
	};

	if (mode >= COUNTOF(modestrings)) {
		LIB_GETBUF(bp);
		snprintf(bp, LIB_BUFLENGTH, "mode#%zu", mode);
		return bp;
	}

	return modestrings[mode];
}
