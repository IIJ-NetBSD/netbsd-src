/*	$NetBSD: byaddr.h,v 1.1 2024/02/18 20:57:35 christos Exp $	*/

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

#ifndef DNS_BYADDR_H
#define DNS_BYADDR_H 1

/*****
***** Module Info
*****/

/*! \file dns/byaddr.h
 * \brief
 * The byaddr module provides reverse lookup services for IPv4 and IPv6
 * addresses.
 *
 * MP:
 *\li	The module ensures appropriate synchronization of data structures it
 *	creates and manipulates.
 *
 * Reliability:
 *\li	No anticipated impact.
 *
 * Resources:
 *\li	TBS
 *
 * Security:
 *\li	No anticipated impact.
 *
 * Standards:
 *\li	RFCs:	1034, 1035, 2181, TBS
 *\li	Drafts:	TBS
 */

#include <isc/event.h>
#include <isc/lang.h>

#include <dns/types.h>

ISC_LANG_BEGINDECLS

/*%
 * A 'dns_byaddrevent_t' is returned when a byaddr completes.
 * The sender field will be set to the byaddr that completed.  If 'result'
 * is ISC_R_SUCCESS, then 'names' will contain a list of names associated
 * with the address.  The recipient of the event must not change the list
 * and must not refer to any of the name data after the event is freed.
 */
typedef struct dns_byaddrevent {
	ISC_EVENT_COMMON(struct dns_byaddrevent);
	isc_result_t   result;
	dns_namelist_t names;
} dns_byaddrevent_t;

isc_result_t
dns_byaddr_create(isc_mem_t *mctx, const isc_netaddr_t *address,
		  dns_view_t *view, unsigned int options, isc_task_t *task,
		  isc_taskaction_t action, void *arg, dns_byaddr_t **byaddrp);
/*%<
 * Find the domain name of 'address'.
 *
 * Notes:
 *
 *\li	There is a reverse lookup format for IPv6 addresses, 'nibble'
 *
 *\li	The 'nibble' format for that address is
 *
 * \code
 *   1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.e.f.ip6.arpa.
 * \endcode
 *
 * Requires:
 *
 *\li	'mctx' is a valid mctx.
 *
 *\li	'address' is a valid IPv4 or IPv6 address.
 *
 *\li	'view' is a valid view which has a resolver.
 *
 *\li	'task' is a valid task.
 *
 *\li	byaddrp != NULL && *byaddrp == NULL
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *
 *\li	Any resolver-related error (e.g. #ISC_R_SHUTTINGDOWN) may also be
 *	returned.
 */

void
dns_byaddr_cancel(dns_byaddr_t *byaddr);
/*%<
 * Cancel 'byaddr'.
 *
 * Notes:
 *
 *\li	If 'byaddr' has not completed, post its #DNS_EVENT_BYADDRDONE
 *	event with a result code of #ISC_R_CANCELED.
 *
 * Requires:
 *
 *\li	'byaddr' is a valid byaddr.
 */

void
dns_byaddr_destroy(dns_byaddr_t **byaddrp);
/*%<
 * Destroy 'byaddr'.
 *
 * Requires:
 *
 *\li	'*byaddrp' is a valid byaddr.
 *
 *\li	The caller has received the #DNS_EVENT_BYADDRDONE event (either because
 *	the byaddr completed or because dns_byaddr_cancel() was called).
 *
 * Ensures:
 *
 *\li	*byaddrp == NULL.
 */

isc_result_t
dns_byaddr_createptrname(const isc_netaddr_t *address, unsigned int options,
			 dns_name_t *name);
/*%<
 * Creates a name that would be used in a PTR query for this address.  The
 * nibble flag indicates that the 'nibble' format is to be used if an IPv6
 * address is provided, instead of the 'bitstring' format.  Since we dropped
 * the support of the bitstring labels, it is expected that the flag is always
 * set.  'options' are the same as for dns_byaddr_create().
 *
 * Requires:
 *
 * \li	'address' is a valid address.
 * \li	'name' is a valid name with a dedicated buffer.
 */

ISC_LANG_ENDDECLS

#endif /* DNS_BYADDR_H */
