/*	$NetBSD: syslog.c,v 1.3 2025/01/26 16:25:38 christos Exp $	*/

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

#include <stdlib.h>
#include <syslog.h>

#include <isc/result.h>
#include <isc/string.h>
#include <isc/syslog.h>
#include <isc/util.h>

static struct dsn_c_pvt_sfnt {
	int val;
	const char *strval;
} facilities[] = { { LOG_KERN, "kern" },
		   { LOG_USER, "user" },
		   { LOG_MAIL, "mail" },
		   { LOG_DAEMON, "daemon" },
		   { LOG_AUTH, "auth" },
		   { LOG_SYSLOG, "syslog" },
		   { LOG_LPR, "lpr" },
#ifdef LOG_NEWS
		   { LOG_NEWS, "news" },
#endif /* ifdef LOG_NEWS */
#ifdef LOG_UUCP
		   { LOG_UUCP, "uucp" },
#endif /* ifdef LOG_UUCP */
#ifdef LOG_CRON
		   { LOG_CRON, "cron" },
#endif /* ifdef LOG_CRON */
#ifdef LOG_AUTHPRIV
		   { LOG_AUTHPRIV, "authpriv" },
#endif /* ifdef LOG_AUTHPRIV */
#ifdef LOG_FTP
		   { LOG_FTP, "ftp" },
#endif /* ifdef LOG_FTP */
		   { LOG_LOCAL0, "local0" },
		   { LOG_LOCAL1, "local1" },
		   { LOG_LOCAL2, "local2" },
		   { LOG_LOCAL3, "local3" },
		   { LOG_LOCAL4, "local4" },
		   { LOG_LOCAL5, "local5" },
		   { LOG_LOCAL6, "local6" },
		   { LOG_LOCAL7, "local7" },
		   { 0, NULL } };

isc_result_t
isc_syslog_facilityfromstring(const char *str, int *facilityp) {
	int i;

	REQUIRE(str != NULL);
	REQUIRE(facilityp != NULL);

	for (i = 0; facilities[i].strval != NULL; i++) {
		if (strcasecmp(facilities[i].strval, str) == 0) {
			*facilityp = facilities[i].val;
			return ISC_R_SUCCESS;
		}
	}
	return ISC_R_NOTFOUND;
}
