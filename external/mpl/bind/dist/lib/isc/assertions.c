/*	$NetBSD: assertions.c,v 1.9 2025/01/26 16:25:36 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

/*! \file */

#include <stdio.h>
#include <stdlib.h>

#include <isc/assertions.h>
#include <isc/backtrace.h>
#include <isc/result.h>
#include <isc/strerr.h>

/*
 * The maximum number of stack frames to dump on assertion failure.
 */
#ifndef BACKTRACE_MAXFRAME
#define BACKTRACE_MAXFRAME 128
#endif /* ifndef BACKTRACE_MAXFRAME */

/*%
 * Forward.
 */
static void
default_callback(const char *, int, isc_assertiontype_t, const char *);

static isc_assertioncallback_t isc_assertion_failed_cb = default_callback;

/*%
 * Public.
 */

/*% assertion failed handler */
/* coverity[+kill] */
void
isc_assertion_failed(const char *file, int line, isc_assertiontype_t type,
		     const char *cond) {
	isc_assertion_failed_cb(file, line, type, cond);
	abort();
}

/*% Set callback. */
void
isc_assertion_setcallback(isc_assertioncallback_t cb) {
	if (cb == NULL) {
		isc_assertion_failed_cb = default_callback;
	} else {
		isc_assertion_failed_cb = cb;
	}
}

/*% Type to Text */
const char *
isc_assertion_typetotext(isc_assertiontype_t type) {
	const char *result;

	/*
	 * These strings have purposefully not been internationalized
	 * because they are considered to essentially be keywords of
	 * the ISC development environment.
	 */
	switch (type) {
	case isc_assertiontype_require:
		result = "REQUIRE";
		break;
	case isc_assertiontype_ensure:
		result = "ENSURE";
		break;
	case isc_assertiontype_insist:
		result = "INSIST";
		break;
	case isc_assertiontype_invariant:
		result = "INVARIANT";
		break;
	default:
		result = "UNKNOWN";
	}
	return result;
}

/*
 * Private.
 */

static void
default_callback(const char *file, int line, isc_assertiontype_t type,
		 const char *cond) {
	void *tracebuf[ISC_BACKTRACE_MAXFRAME];
	int nframes = isc_backtrace(tracebuf, ISC_BACKTRACE_MAXFRAME);

	fprintf(stderr, "%s:%d: %s(%s) failed%s\n", file, line,
		isc_assertion_typetotext(type), cond,
		(nframes > 0) ? ", back trace" : ".");

	if (nframes > 0) {
		isc_backtrace_symbols_fd(tracebuf, nframes, fileno(stderr));
	}

	fflush(stderr);
}
