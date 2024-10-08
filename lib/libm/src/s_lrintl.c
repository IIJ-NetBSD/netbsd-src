#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD: head/lib/msun/src/s_lrintl.c 175309 2008-01-14 02:12:07Z das $");
#else
__RCSID("$NetBSD: s_lrintl.c,v 1.4 2024/02/24 15:16:53 christos Exp $");
#endif

#include <math.h>

#define stype		long double
#define	roundit		rintl
#define dtype		long
#define	fn		lrintl

#include "s_lrint.c"
