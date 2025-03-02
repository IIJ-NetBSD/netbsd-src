/*	$NetBSD: ifiter_getifaddrs.c,v 1.1 2024/02/18 20:57:57 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

/*! \file
 * \brief
 * Obtain the list of network interfaces using the getifaddrs(3) library.
 */

#include <ifaddrs.h>
#include <stdbool.h>

#include <isc/strerr.h>

/*% Iterator Magic */
#define IFITER_MAGIC ISC_MAGIC('I', 'F', 'I', 'G')
/*% Valid Iterator */
#define VALID_IFITER(t) ISC_MAGIC_VALID(t, IFITER_MAGIC)

#ifdef __linux
static bool seenv6 = false;
#endif /* ifdef __linux */

/*% Iterator structure */
struct isc_interfaceiter {
	unsigned int magic; /*%< Magic number. */
	isc_mem_t *mctx;
	void *buf;		 /*%< (unused) */
	unsigned int bufsize;	 /*%< (always 0) */
	struct ifaddrs *ifaddrs; /*%< List of ifaddrs */
	struct ifaddrs *pos;	 /*%< Ptr to current ifaddr */
	isc_interface_t current; /*%< Current interface data. */
	isc_result_t result;	 /*%< Last result code. */
#ifdef __linux
	FILE *proc;
	char entry[ISC_IF_INET6_SZ];
	isc_result_t valid;
#endif /* ifdef __linux */
};

isc_result_t
isc_interfaceiter_create(isc_mem_t *mctx, isc_interfaceiter_t **iterp) {
	isc_interfaceiter_t *iter;
	isc_result_t result;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(mctx != NULL);
	REQUIRE(iterp != NULL);
	REQUIRE(*iterp == NULL);

	iter = isc_mem_get(mctx, sizeof(*iter));

	iter->mctx = mctx;
	iter->buf = NULL;
	iter->bufsize = 0;
	iter->ifaddrs = NULL;
#ifdef __linux
	/*
	 * Only open "/proc/net/if_inet6" if we have never seen a IPv6
	 * address returned by getifaddrs().
	 */
	if (!seenv6) {
		iter->proc = fopen("/proc/net/if_inet6", "r");
	} else {
		iter->proc = NULL;
	}
	iter->valid = ISC_R_FAILURE;
#endif /* ifdef __linux */

	if (getifaddrs(&iter->ifaddrs) < 0) {
		strerror_r(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "getting interface addresses: getifaddrs: %s",
				 strbuf);
		result = ISC_R_UNEXPECTED;
		goto failure;
	}

	/*
	 * A newly created iterator has an undefined position
	 * until isc_interfaceiter_first() is called.
	 */
	iter->pos = NULL;
	iter->result = ISC_R_FAILURE;

	iter->magic = IFITER_MAGIC;
	*iterp = iter;
	return (ISC_R_SUCCESS);

failure:
#ifdef __linux
	if (iter->proc != NULL) {
		fclose(iter->proc);
	}
#endif				     /* ifdef __linux */
	if (iter->ifaddrs != NULL) { /* just in case */
		freeifaddrs(iter->ifaddrs);
	}
	isc_mem_put(mctx, iter, sizeof(*iter));
	return (result);
}

/*
 * Get information about the current interface to iter->current.
 * If successful, return ISC_R_SUCCESS.
 * If the interface has an unsupported address family,
 * return ISC_R_IGNORE.
 */

static isc_result_t
internal_current(isc_interfaceiter_t *iter) {
	struct ifaddrs *ifa;
	int family;
	unsigned int namelen;

	REQUIRE(VALID_IFITER(iter));

	ifa = iter->pos;

#ifdef __linux
	if (iter->pos == NULL) {
		return (linux_if_inet6_current(iter));
	}
#endif /* ifdef __linux */

	INSIST(ifa != NULL);
	INSIST(ifa->ifa_name != NULL);

	if (ifa->ifa_addr == NULL) {
		return (ISC_R_IGNORE);
	}

	family = ifa->ifa_addr->sa_family;
	if (family != AF_INET && family != AF_INET6) {
		return (ISC_R_IGNORE);
	}

#ifdef __linux
	if (family == AF_INET6) {
		seenv6 = true;
	}
#endif /* ifdef __linux */

	memset(&iter->current, 0, sizeof(iter->current));

	namelen = strlen(ifa->ifa_name);
	if (namelen > sizeof(iter->current.name) - 1) {
		namelen = sizeof(iter->current.name) - 1;
	}

	memset(iter->current.name, 0, sizeof(iter->current.name));
	memmove(iter->current.name, ifa->ifa_name, namelen);

	iter->current.flags = 0;

	if ((ifa->ifa_flags & IFF_UP) != 0) {
		iter->current.flags |= INTERFACE_F_UP;
	}

	if ((ifa->ifa_flags & IFF_POINTOPOINT) != 0) {
		iter->current.flags |= INTERFACE_F_POINTTOPOINT;
	}

	if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) {
		iter->current.flags |= INTERFACE_F_LOOPBACK;
	}

	iter->current.af = family;

	get_addr(family, &iter->current.address, ifa->ifa_addr, ifa->ifa_name);

	if (ifa->ifa_netmask != NULL) {
		get_addr(family, &iter->current.netmask, ifa->ifa_netmask,
			 ifa->ifa_name);
	}

	if (ifa->ifa_dstaddr != NULL &&
	    (iter->current.flags & INTERFACE_F_POINTTOPOINT) != 0)
	{
		get_addr(family, &iter->current.dstaddress, ifa->ifa_dstaddr,
			 ifa->ifa_name);
	}

	return (ISC_R_SUCCESS);
}

/*
 * Step the iterator to the next interface.  Unlike
 * isc_interfaceiter_next(), this may leave the iterator
 * positioned on an interface that will ultimately
 * be ignored.  Return ISC_R_NOMORE if there are no more
 * interfaces, otherwise ISC_R_SUCCESS.
 */
static isc_result_t
internal_next(isc_interfaceiter_t *iter) {
	if (iter->pos != NULL) {
		iter->pos = iter->pos->ifa_next;
	}
	if (iter->pos == NULL) {
#ifdef __linux
		if (!seenv6) {
			return (linux_if_inet6_next(iter));
		}
#endif /* ifdef __linux */
		return (ISC_R_NOMORE);
	}

	return (ISC_R_SUCCESS);
}

static void
internal_destroy(isc_interfaceiter_t *iter) {
#ifdef __linux
	if (iter->proc != NULL) {
		fclose(iter->proc);
	}
	iter->proc = NULL;
#endif /* ifdef __linux */
	if (iter->ifaddrs) {
		freeifaddrs(iter->ifaddrs);
	}
	iter->ifaddrs = NULL;
}

static void
internal_first(isc_interfaceiter_t *iter) {
#ifdef __linux
	linux_if_inet6_first(iter);
#endif /* ifdef __linux */
	iter->pos = iter->ifaddrs;
}
