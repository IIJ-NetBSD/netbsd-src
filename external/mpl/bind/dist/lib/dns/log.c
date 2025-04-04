/*	$NetBSD: log.c,v 1.8 2025/01/26 16:25:23 christos Exp $	*/

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

#include <isc/util.h>

#include <dns/log.h>

/*%
 * When adding a new category, be sure to add the appropriate
 * \#define to <dns/log.h>.
 */
isc_logcategory_t dns_categories[] = {
	{ "notify", 0 },	{ "database", 0 },
	{ "security", 0 },	{ "_placeholder", 0 },
	{ "dnssec", 0 },	{ "resolver", 0 },
	{ "xfer-in", 0 },	{ "xfer-out", 0 },
	{ "dispatch", 0 },	{ "lame-servers", 0 },
	{ "edns-disabled", 0 }, { "rpz", 0 },
	{ "rate-limit", 0 },	{ "cname", 0 },
	{ "spill", 0 },		{ "dnstap", 0 },
	{ "zoneload", 0 },	{ "nsid", 0 },
	{ "rpz-passthru", 0 },	{ NULL, 0 }
};

/*%
 * When adding a new module, be sure to add the appropriate
 * \#define to <dns/log.h>.
 */
isc_logmodule_t dns_modules[] = {
	{ "dns/db", 0 },	 { "dns/rbtdb", 0 },	{ "dns/rbt", 0 },
	{ "dns/rdata", 0 },	 { "dns/master", 0 },	{ "dns/message", 0 },
	{ "dns/cache", 0 },	 { "dns/config", 0 },	{ "dns/resolver", 0 },
	{ "dns/zone", 0 },	 { "dns/journal", 0 },	{ "dns/adb", 0 },
	{ "dns/xfrin", 0 },	 { "dns/xfrout", 0 },	{ "dns/acl", 0 },
	{ "dns/validator", 0 },	 { "dns/dispatch", 0 }, { "dns/request", 0 },
	{ "dns/masterdump", 0 }, { "dns/tsig", 0 },	{ "dns/tkey", 0 },
	{ "dns/sdb", 0 },	 { "dns/diff", 0 },	{ "dns/hints", 0 },
	{ "dns/unused1", 0 },	 { "dns/dlz", 0 },	{ "dns/dnssec", 0 },
	{ "dns/crypto", 0 },	 { "dns/packets", 0 },	{ "dns/nta", 0 },
	{ "dns/dyndb", 0 },	 { "dns/dnstap", 0 },	{ "dns/ssu", 0 },
	{ "dns/qp", 0 },	 { NULL, 0 },
};

isc_log_t *dns_lctx = NULL;

void
dns_log_init(isc_log_t *lctx) {
	REQUIRE(lctx != NULL);

	isc_log_registercategories(lctx, dns_categories);
	isc_log_registermodules(lctx, dns_modules);
}

void
dns_log_setcontext(isc_log_t *lctx) {
	dns_lctx = lctx;
}
