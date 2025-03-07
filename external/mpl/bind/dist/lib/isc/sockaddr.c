/*	$NetBSD: sockaddr.c,v 1.13 2025/01/26 16:25:38 christos Exp $	*/

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

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>

#include <isc/buffer.h>
#include <isc/hash.h>
#include <isc/netaddr.h>
#include <isc/region.h>
#include <isc/sockaddr.h>
#include <isc/string.h>
#include <isc/util.h>

bool
isc_sockaddr_equal(const isc_sockaddr_t *a, const isc_sockaddr_t *b) {
	return isc_sockaddr_compare(a, b,
				    ISC_SOCKADDR_CMPADDR |
					    ISC_SOCKADDR_CMPPORT |
					    ISC_SOCKADDR_CMPSCOPE);
}

bool
isc_sockaddr_eqaddr(const isc_sockaddr_t *a, const isc_sockaddr_t *b) {
	return isc_sockaddr_compare(
		a, b, ISC_SOCKADDR_CMPADDR | ISC_SOCKADDR_CMPSCOPE);
}

bool
isc_sockaddr_compare(const isc_sockaddr_t *a, const isc_sockaddr_t *b,
		     unsigned int flags) {
	REQUIRE(a != NULL && b != NULL);

	if (a->length != b->length) {
		return false;
	}

	/*
	 * We don't just memcmp because the sin_zero field isn't always
	 * zero.
	 */

	if (a->type.sa.sa_family != b->type.sa.sa_family) {
		return false;
	}
	switch (a->type.sa.sa_family) {
	case AF_INET:
		if ((flags & ISC_SOCKADDR_CMPADDR) != 0 &&
		    memcmp(&a->type.sin.sin_addr, &b->type.sin.sin_addr,
			   sizeof(a->type.sin.sin_addr)) != 0)
		{
			return false;
		}
		if ((flags & ISC_SOCKADDR_CMPPORT) != 0 &&
		    a->type.sin.sin_port != b->type.sin.sin_port)
		{
			return false;
		}
		break;
	case AF_INET6:
		if ((flags & ISC_SOCKADDR_CMPADDR) != 0 &&
		    memcmp(&a->type.sin6.sin6_addr, &b->type.sin6.sin6_addr,
			   sizeof(a->type.sin6.sin6_addr)) != 0)
		{
			return false;
		}
		/*
		 * If ISC_SOCKADDR_CMPSCOPEZERO is set then don't return
		 * false if one of the scopes in zero.
		 */
		if ((flags & ISC_SOCKADDR_CMPSCOPE) != 0 &&
		    a->type.sin6.sin6_scope_id != b->type.sin6.sin6_scope_id &&
		    ((flags & ISC_SOCKADDR_CMPSCOPEZERO) == 0 ||
		     (a->type.sin6.sin6_scope_id != 0 &&
		      b->type.sin6.sin6_scope_id != 0)))
		{
			return false;
		}
		if ((flags & ISC_SOCKADDR_CMPPORT) != 0 &&
		    a->type.sin6.sin6_port != b->type.sin6.sin6_port)
		{
			return false;
		}
		break;
	default:
		if (memcmp(&a->type, &b->type, a->length) != 0) {
			return false;
		}
	}
	return true;
}

bool
isc_sockaddr_eqaddrprefix(const isc_sockaddr_t *a, const isc_sockaddr_t *b,
			  unsigned int prefixlen) {
	isc_netaddr_t na, nb;
	isc_netaddr_fromsockaddr(&na, a);
	isc_netaddr_fromsockaddr(&nb, b);
	return isc_netaddr_eqprefix(&na, &nb, prefixlen);
}

isc_result_t
isc_sockaddr_totext(const isc_sockaddr_t *sockaddr, isc_buffer_t *target) {
	isc_result_t result;
	isc_netaddr_t netaddr;
	char pbuf[sizeof("65000")];
	unsigned int plen;
	isc_region_t avail;

	REQUIRE(sockaddr != NULL);

	/*
	 * Do the port first, giving us the opportunity to check for
	 * unsupported address families before calling
	 * isc_netaddr_fromsockaddr().
	 */
	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		snprintf(pbuf, sizeof(pbuf), "%u",
			 ntohs(sockaddr->type.sin.sin_port));
		break;
	case AF_INET6:
		snprintf(pbuf, sizeof(pbuf), "%u",
			 ntohs(sockaddr->type.sin6.sin6_port));
		break;
	default:
		return ISC_R_FAILURE;
	}

	plen = strlen(pbuf);
	INSIST(plen < sizeof(pbuf));

	isc_netaddr_fromsockaddr(&netaddr, sockaddr);
	result = isc_netaddr_totext(&netaddr, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	if (1 + plen + 1 > isc_buffer_availablelength(target)) {
		return ISC_R_NOSPACE;
	}

	isc_buffer_putmem(target, (const unsigned char *)"#", 1);
	isc_buffer_putmem(target, (const unsigned char *)pbuf, plen);

	/*
	 * Null terminate after used region.
	 */
	isc_buffer_availableregion(target, &avail);
	INSIST(avail.length >= 1);
	avail.base[0] = '\0';

	return ISC_R_SUCCESS;
}

void
isc_sockaddr_format(const isc_sockaddr_t *sa, char *array, unsigned int size) {
	isc_result_t result;
	isc_buffer_t buf;

	if (size == 0U) {
		return;
	}

	isc_buffer_init(&buf, array, size);
	result = isc_sockaddr_totext(sa, &buf);
	if (result != ISC_R_SUCCESS) {
		/*
		 * The message is the same as in netaddr.c.
		 */
		snprintf(array, size, "<unknown address, family %u>",
			 sa->type.sa.sa_family);
		array[size - 1] = '\0';
	}
}

void
isc_sockaddr_hash_ex(isc_hash32_t *hash, const isc_sockaddr_t *sockaddr,
		     bool address_only) {
	REQUIRE(sockaddr != NULL);

	size_t len = 0;
	const uint8_t *s = NULL;
	unsigned int p = 0;
	const struct in6_addr *in6;

	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		s = (const uint8_t *)&sockaddr->type.sin.sin_addr;
		len = sizeof(sockaddr->type.sin.sin_addr.s_addr);
		if (!address_only) {
			p = ntohs(sockaddr->type.sin.sin_port);
		}
		break;
	case AF_INET6:
		in6 = &sockaddr->type.sin6.sin6_addr;
		s = (const uint8_t *)in6;
		if (IN6_IS_ADDR_V4MAPPED(in6)) {
			s += 12;
			len = sizeof(sockaddr->type.sin.sin_addr.s_addr);
		} else {
			len = sizeof(sockaddr->type.sin6.sin6_addr);
		}
		if (!address_only) {
			p = ntohs(sockaddr->type.sin6.sin6_port);
		}
		break;
	default:
		UNREACHABLE();
	}

	isc_hash32_hash(hash, s, len, true);
	if (!address_only) {
		isc_hash32_hash(hash, &p, sizeof(p), true);
	}
}

uint32_t
isc_sockaddr_hash(const isc_sockaddr_t *sockaddr, bool address_only) {
	isc_hash32_t hash;

	isc_hash32_init(&hash);

	isc_sockaddr_hash_ex(&hash, sockaddr, address_only);

	return isc_hash32_finalize(&hash);
}

void
isc_sockaddr_any(isc_sockaddr_t *sockaddr) {
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin.sin_family = AF_INET;
	sockaddr->type.sin.sin_addr.s_addr = INADDR_ANY;
	sockaddr->type.sin.sin_port = 0;
	sockaddr->length = sizeof(sockaddr->type.sin);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_any6(isc_sockaddr_t *sockaddr) {
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin6.sin6_family = AF_INET6;
	sockaddr->type.sin6.sin6_addr = in6addr_any;
	sockaddr->type.sin6.sin6_port = 0;
	sockaddr->length = sizeof(sockaddr->type.sin6);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_fromin(isc_sockaddr_t *sockaddr, const struct in_addr *ina,
		    in_port_t port) {
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin.sin_family = AF_INET;
	sockaddr->type.sin.sin_addr = *ina;
	sockaddr->type.sin.sin_port = htons(port);
	sockaddr->length = sizeof(sockaddr->type.sin);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_anyofpf(isc_sockaddr_t *sockaddr, int pf) {
	switch (pf) {
	case AF_INET:
		isc_sockaddr_any(sockaddr);
		break;
	case AF_INET6:
		isc_sockaddr_any6(sockaddr);
		break;
	default:
		UNREACHABLE();
	}
}

void
isc_sockaddr_fromin6(isc_sockaddr_t *sockaddr, const struct in6_addr *ina6,
		     in_port_t port) {
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin6.sin6_family = AF_INET6;
	sockaddr->type.sin6.sin6_addr = *ina6;
	sockaddr->type.sin6.sin6_port = htons(port);
	sockaddr->length = sizeof(sockaddr->type.sin6);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_v6fromin(isc_sockaddr_t *sockaddr, const struct in_addr *ina,
		      in_port_t port) {
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin6.sin6_family = AF_INET6;
	sockaddr->type.sin6.sin6_addr.s6_addr[10] = 0xff;
	sockaddr->type.sin6.sin6_addr.s6_addr[11] = 0xff;
	memmove(&sockaddr->type.sin6.sin6_addr.s6_addr[12], ina, 4);
	sockaddr->type.sin6.sin6_port = htons(port);
	sockaddr->length = sizeof(sockaddr->type.sin6);
	ISC_LINK_INIT(sockaddr, link);
}

int
isc_sockaddr_pf(const isc_sockaddr_t *sockaddr) {
	/*
	 * Get the protocol family of 'sockaddr'.
	 */

#if (AF_INET == PF_INET && AF_INET6 == PF_INET6)
	/*
	 * Assume that PF_xxx == AF_xxx for all AF and PF.
	 */
	return sockaddr->type.sa.sa_family;
#else  /* if (AF_INET == PF_INET && AF_INET6 == PF_INET6) */
	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		return PF_INET;
	case AF_INET6:
		return PF_INET6;
	default:
		FATAL_ERROR("unknown address family: %d",
			    (int)sockaddr->type.sa.sa_family);
	}
#endif /* if (AF_INET == PF_INET && AF_INET6 == PF_INET6) */
}

void
isc_sockaddr_fromnetaddr(isc_sockaddr_t *sockaddr, const isc_netaddr_t *na,
			 in_port_t port) {
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin.sin_family = na->family;
	switch (na->family) {
	case AF_INET:
		sockaddr->length = sizeof(sockaddr->type.sin);
		sockaddr->type.sin.sin_addr = na->type.in;
		sockaddr->type.sin.sin_port = htons(port);
		break;
	case AF_INET6:
		sockaddr->length = sizeof(sockaddr->type.sin6);
		memmove(&sockaddr->type.sin6.sin6_addr, &na->type.in6, 16);
		sockaddr->type.sin6.sin6_scope_id = isc_netaddr_getzone(na);
		sockaddr->type.sin6.sin6_port = htons(port);
		break;
	default:
		UNREACHABLE();
	}
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_setport(isc_sockaddr_t *sockaddr, in_port_t port) {
	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		sockaddr->type.sin.sin_port = htons(port);
		break;
	case AF_INET6:
		sockaddr->type.sin6.sin6_port = htons(port);
		break;
	default:
		FATAL_ERROR("unknown address family: %d",
			    (int)sockaddr->type.sa.sa_family);
	}
}

in_port_t
isc_sockaddr_getport(const isc_sockaddr_t *sockaddr) {
	in_port_t port = 0;

	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		port = ntohs(sockaddr->type.sin.sin_port);
		break;
	case AF_INET6:
		port = ntohs(sockaddr->type.sin6.sin6_port);
		break;
	default:
		FATAL_ERROR("unknown address family: %d",
			    (int)sockaddr->type.sa.sa_family);
	}

	return port;
}

bool
isc_sockaddr_ismulticast(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET ||
	    sockaddr->type.sa.sa_family == AF_INET6)
	{
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return isc_netaddr_ismulticast(&netaddr);
	}
	return false;
}

bool
isc_sockaddr_isexperimental(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return isc_netaddr_isexperimental(&netaddr);
	}
	return false;
}

bool
isc_sockaddr_issitelocal(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET6) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return isc_netaddr_issitelocal(&netaddr);
	}
	return false;
}

bool
isc_sockaddr_islinklocal(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET6) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return isc_netaddr_islinklocal(&netaddr);
	}
	return false;
}

bool
isc_sockaddr_isnetzero(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return isc_netaddr_isnetzero(&netaddr);
	}
	return false;
}

isc_result_t
isc_sockaddr_fromsockaddr(isc_sockaddr_t *isa, const struct sockaddr *sa) {
	unsigned int length = 0;

	switch (sa->sa_family) {
	case AF_INET:
		length = sizeof(isa->type.sin);
		break;
	case AF_INET6:
		length = sizeof(isa->type.sin6);
		break;
	default:
		return ISC_R_NOTIMPLEMENTED;
	}

	*isa = (isc_sockaddr_t){ .length = length,
				 .link = ISC_LINK_INITIALIZER };
	memmove(isa, sa, length);

	return ISC_R_SUCCESS;
}

bool
isc_sockaddr_disabled(const isc_sockaddr_t *sockaddr) {
	if ((sockaddr->type.sa.sa_family == AF_INET &&
	     isc_net_probeipv4() == ISC_R_DISABLED) ||
	    (sockaddr->type.sa.sa_family == AF_INET6 &&
	     isc_net_probeipv6() == ISC_R_DISABLED))
	{
		return true;
	}
	return false;
}
