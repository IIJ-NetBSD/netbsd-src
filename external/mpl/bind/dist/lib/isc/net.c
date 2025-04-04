/*	$NetBSD: net.c,v 1.4 2025/01/26 16:25:37 christos Exp $	*/

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

#include <stdbool.h>
#include <sys/types.h>

#if defined(HAVE_SYS_SYSCTL_H) && !defined(__linux__)
#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif /* if defined(HAVE_SYS_PARAM_H) */
#include <sys/sysctl.h>
#endif /* if defined(HAVE_SYS_SYSCTL_H) && !defined(__linux__) */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/uio.h>
#include <unistd.h>

#include <isc/log.h>
#include <isc/net.h>
#include <isc/once.h>
#include <isc/strerr.h>
#include <isc/string.h>
#include <isc/util.h>

#ifndef socklen_t
#define socklen_t unsigned int
#endif /* ifndef socklen_t */

/*%
 * Definitions about UDP port range specification.  This is a total mess of
 * portability variants: some use sysctl (but the sysctl names vary), some use
 * system-specific interfaces, some have the same interface for IPv4 and IPv6,
 * some separate them, etc...
 */

/*%
 * The last resort defaults: use all non well known port space
 */
#ifndef ISC_NET_PORTRANGELOW
#define ISC_NET_PORTRANGELOW 1024
#endif /* ISC_NET_PORTRANGELOW */
#ifndef ISC_NET_PORTRANGEHIGH
#define ISC_NET_PORTRANGEHIGH 65535
#endif /* ISC_NET_PORTRANGEHIGH */

#ifdef HAVE_SYSCTLBYNAME

/*%
 * sysctl variants
 */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
#define USE_SYSCTL_PORTRANGE
#define SYSCTL_V4PORTRANGE_LOW	"net.inet.ip.portrange.hifirst"
#define SYSCTL_V4PORTRANGE_HIGH "net.inet.ip.portrange.hilast"
#define SYSCTL_V6PORTRANGE_LOW	"net.inet.ip.portrange.hifirst"
#define SYSCTL_V6PORTRANGE_HIGH "net.inet.ip.portrange.hilast"
#endif /* if defined(__FreeBSD__) || defined(__APPLE__) || \
	* defined(__DragonFly__) */

#ifdef __NetBSD__
#define USE_SYSCTL_PORTRANGE
#define SYSCTL_V4PORTRANGE_LOW	"net.inet.ip.anonportmin"
#define SYSCTL_V4PORTRANGE_HIGH "net.inet.ip.anonportmax"
#define SYSCTL_V6PORTRANGE_LOW	"net.inet6.ip6.anonportmin"
#define SYSCTL_V6PORTRANGE_HIGH "net.inet6.ip6.anonportmax"
#endif /* ifdef __NetBSD__ */

#else /* !HAVE_SYSCTLBYNAME */

#ifdef __OpenBSD__
#define USE_SYSCTL_PORTRANGE
#define SYSCTL_V4PORTRANGE_LOW \
	{ CTL_NET, PF_INET, IPPROTO_IP, IPCTL_IPPORT_HIFIRSTAUTO }
#define SYSCTL_V4PORTRANGE_HIGH \
	{ CTL_NET, PF_INET, IPPROTO_IP, IPCTL_IPPORT_HILASTAUTO }
/* Same for IPv6 */
#define SYSCTL_V6PORTRANGE_LOW	SYSCTL_V4PORTRANGE_LOW
#define SYSCTL_V6PORTRANGE_HIGH SYSCTL_V4PORTRANGE_HIGH
#endif /* ifdef __OpenBSD__ */

#endif /* HAVE_SYSCTLBYNAME */

static isc_once_t once_ipv6only = ISC_ONCE_INIT;
#ifdef __notyet__
static isc_once_t once_ipv6pktinfo = ISC_ONCE_INIT;
#endif /* ifdef __notyet__ */

#ifndef ISC_CMSG_IP_TOS
#ifdef __APPLE__
#define ISC_CMSG_IP_TOS 0 /* As of 10.8.2. */
#else			  /* ! __APPLE__ */
#define ISC_CMSG_IP_TOS 1
#endif /* ! __APPLE__ */
#endif /* ! ISC_CMSG_IP_TOS */

static isc_once_t once = ISC_ONCE_INIT;

static isc_result_t ipv4_result = ISC_R_NOTFOUND;
static isc_result_t ipv6_result = ISC_R_NOTFOUND;
static isc_result_t ipv6only_result = ISC_R_NOTFOUND;
static isc_result_t ipv6pktinfo_result = ISC_R_NOTFOUND;

static isc_result_t
try_proto(int domain) {
	int s;
	isc_result_t result = ISC_R_SUCCESS;

	s = socket(domain, SOCK_STREAM, 0);
	if (s == -1) {
		switch (errno) {
#ifdef EAFNOSUPPORT
		case EAFNOSUPPORT:
#endif /* ifdef EAFNOSUPPORT */
#ifdef EPFNOSUPPORT
		case EPFNOSUPPORT:
#endif /* ifdef EPFNOSUPPORT */
#ifdef EPROTONOSUPPORT
		case EPROTONOSUPPORT:
#endif /* ifdef EPROTONOSUPPORT */
#ifdef EINVAL
		case EINVAL:
#endif /* ifdef EINVAL */
			return ISC_R_NOTFOUND;
		default:
			UNEXPECTED_SYSERROR(errno, "socket()");
			return ISC_R_UNEXPECTED;
		}
	}

	if (domain == PF_INET6) {
		struct sockaddr_in6 sin6;
		unsigned int len;

		/*
		 * Check to see if IPv6 is broken, as is common on Linux.
		 */
		len = sizeof(sin6);
		if (getsockname(s, (struct sockaddr *)&sin6, (void *)&len) < 0)
		{
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "retrieving the address of an IPv6 "
				      "socket from the kernel failed.");
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "IPv6 is not supported.");
			result = ISC_R_NOTFOUND;
		} else {
			if (len == sizeof(struct sockaddr_in6)) {
				result = ISC_R_SUCCESS;
			} else {
				isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
					      ISC_LOGMODULE_SOCKET,
					      ISC_LOG_ERROR,
					      "IPv6 structures in kernel and "
					      "user space do not match.");
				isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
					      ISC_LOGMODULE_SOCKET,
					      ISC_LOG_ERROR,
					      "IPv6 is not supported.");
				result = ISC_R_NOTFOUND;
			}
		}
	}

	(void)close(s);

	return result;
}

static void
initialize_action(void) {
	ipv4_result = try_proto(PF_INET);
	ipv6_result = try_proto(PF_INET6);
}

static void
initialize(void) {
	isc_once_do(&once, initialize_action);
}

isc_result_t
isc_net_probeipv4(void) {
	initialize();
	return ipv4_result;
}

isc_result_t
isc_net_probeipv6(void) {
	initialize();
	return ipv6_result;
}

static void
try_ipv6only(void) {
#ifdef IPV6_V6ONLY
	int s, on;
#endif /* ifdef IPV6_V6ONLY */
	isc_result_t result;

	result = isc_net_probeipv6();
	if (result != ISC_R_SUCCESS) {
		ipv6only_result = result;
		return;
	}

#ifndef IPV6_V6ONLY
	ipv6only_result = ISC_R_NOTFOUND;
	return;
#else  /* ifndef IPV6_V6ONLY */
	/* check for TCP sockets */
	s = socket(PF_INET6, SOCK_STREAM, 0);
	if (s == -1) {
		UNEXPECTED_SYSERROR(errno, "socket()");
		ipv6only_result = ISC_R_UNEXPECTED;
		return;
	}

	on = 1;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
		ipv6only_result = ISC_R_NOTFOUND;
		goto close;
	}

	close(s);

	/* check for UDP sockets */
	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if (s == -1) {
		UNEXPECTED_SYSERROR(errno, "socket()");
		ipv6only_result = ISC_R_UNEXPECTED;
		return;
	}

	on = 1;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
		ipv6only_result = ISC_R_NOTFOUND;
		goto close;
	}

	ipv6only_result = ISC_R_SUCCESS;

close:
	close(s);
	return;
#endif /* IPV6_V6ONLY */
}

static void
initialize_ipv6only(void) {
	isc_once_do(&once_ipv6only, try_ipv6only);
}

#ifdef __notyet__
static void
try_ipv6pktinfo(void) {
	int s, on;
	isc_result_t result;
	int optname;

	result = isc_net_probeipv6();
	if (result != ISC_R_SUCCESS) {
		ipv6pktinfo_result = result;
		return;
	}

	/* we only use this for UDP sockets */
	s = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1) {
		UNEXPECTED_SYSERROR(errno, "socket()");
		ipv6pktinfo_result = ISC_R_UNEXPECTED;
		return;
	}

#ifdef IPV6_RECVPKTINFO
	optname = IPV6_RECVPKTINFO;
#else  /* ifdef IPV6_RECVPKTINFO */
	optname = IPV6_PKTINFO;
#endif /* ifdef IPV6_RECVPKTINFO */
	on = 1;
	if (setsockopt(s, IPPROTO_IPV6, optname, &on, sizeof(on)) < 0) {
		ipv6pktinfo_result = ISC_R_NOTFOUND;
		goto close;
	}

	ipv6pktinfo_result = ISC_R_SUCCESS;

close:
	close(s);
	return;
}

static void
initialize_ipv6pktinfo(void) {
	isc_once_do(&once_ipv6pktinfo, try_ipv6pktinfo);
}
#endif /* ifdef __notyet__ */

isc_result_t
isc_net_probe_ipv6only(void) {
	initialize_ipv6only();
	return ipv6only_result;
}

isc_result_t
isc_net_probe_ipv6pktinfo(void) {
/*
 * XXXWPK if pktinfo were supported then we could listen on :: for ipv6 and get
 * the information about the destination address from pktinfo structure passed
 * in recvmsg but this method is not portable and libuv doesn't support it - so
 * we need to listen on all interfaces.
 * We should verify that this doesn't impact performance (we already do it for
 * ipv4) and either remove all the ipv6pktinfo detection code from above
 * or think of fixing libuv.
 */
#ifdef __notyet__
	initialize_ipv6pktinfo();
#endif /* ifdef __notyet__ */
	return ipv6pktinfo_result;
}

#if defined(USE_SYSCTL_PORTRANGE)
#if defined(HAVE_SYSCTLBYNAME)
static isc_result_t
getudpportrange_sysctl(int af, in_port_t *low, in_port_t *high) {
	int port_low, port_high;
	size_t portlen;
	const char *sysctlname_lowport, *sysctlname_hiport;

	if (af == AF_INET) {
		sysctlname_lowport = SYSCTL_V4PORTRANGE_LOW;
		sysctlname_hiport = SYSCTL_V4PORTRANGE_HIGH;
	} else {
		sysctlname_lowport = SYSCTL_V6PORTRANGE_LOW;
		sysctlname_hiport = SYSCTL_V6PORTRANGE_HIGH;
	}
	portlen = sizeof(port_low);
	if (sysctlbyname(sysctlname_lowport, &port_low, &portlen, NULL, 0) < 0)
	{
		return ISC_R_FAILURE;
	}
	portlen = sizeof(port_high);
	if (sysctlbyname(sysctlname_hiport, &port_high, &portlen, NULL, 0) < 0)
	{
		return ISC_R_FAILURE;
	}
	if ((port_low & ~0xffff) != 0 || (port_high & ~0xffff) != 0) {
		return ISC_R_RANGE;
	}

	*low = (in_port_t)port_low;
	*high = (in_port_t)port_high;

	return ISC_R_SUCCESS;
}
#else  /* !HAVE_SYSCTLBYNAME */
static isc_result_t
getudpportrange_sysctl(int af, in_port_t *low, in_port_t *high) {
	int mib_lo4[4] = SYSCTL_V4PORTRANGE_LOW;
	int mib_hi4[4] = SYSCTL_V4PORTRANGE_HIGH;
	int mib_lo6[4] = SYSCTL_V6PORTRANGE_LOW;
	int mib_hi6[4] = SYSCTL_V6PORTRANGE_HIGH;
	int *mib_lo, *mib_hi, miblen;
	int port_low, port_high;
	size_t portlen;

	if (af == AF_INET) {
		mib_lo = mib_lo4;
		mib_hi = mib_hi4;
		miblen = sizeof(mib_lo4) / sizeof(mib_lo4[0]);
	} else {
		mib_lo = mib_lo6;
		mib_hi = mib_hi6;
		miblen = sizeof(mib_lo6) / sizeof(mib_lo6[0]);
	}

	portlen = sizeof(port_low);
	if (sysctl(mib_lo, miblen, &port_low, &portlen, NULL, 0) < 0) {
		return ISC_R_FAILURE;
	}

	portlen = sizeof(port_high);
	if (sysctl(mib_hi, miblen, &port_high, &portlen, NULL, 0) < 0) {
		return ISC_R_FAILURE;
	}

	if ((port_low & ~0xffff) != 0 || (port_high & ~0xffff) != 0) {
		return ISC_R_RANGE;
	}

	*low = (in_port_t)port_low;
	*high = (in_port_t)port_high;

	return ISC_R_SUCCESS;
}
#endif /* HAVE_SYSCTLBYNAME */
#endif /* USE_SYSCTL_PORTRANGE */

isc_result_t
isc_net_getudpportrange(int af, in_port_t *low, in_port_t *high) {
	int result = ISC_R_FAILURE;
#if !defined(USE_SYSCTL_PORTRANGE) && defined(__linux)
	FILE *fp;
#endif /* if !defined(USE_SYSCTL_PORTRANGE) && defined(__linux) */

	REQUIRE(low != NULL && high != NULL);

#if defined(USE_SYSCTL_PORTRANGE)
	result = getudpportrange_sysctl(af, low, high);
#elif defined(__linux)

	UNUSED(af);

	/*
	 * Linux local ports are address family agnostic.
	 */
	fp = fopen("/proc/sys/net/ipv4/ip_local_port_range", "r");
	if (fp != NULL) {
		int n;
		unsigned int l, h;

		n = fscanf(fp, "%u %u", &l, &h);
		if (n == 2 && (l & ~0xffff) == 0 && (h & ~0xffff) == 0) {
			*low = l;
			*high = h;
			result = ISC_R_SUCCESS;
		}
		fclose(fp);
	}
#else  /* if defined(USE_SYSCTL_PORTRANGE) */
	UNUSED(af);
#endif /* if defined(USE_SYSCTL_PORTRANGE) */

	if (result != ISC_R_SUCCESS) {
		*low = ISC_NET_PORTRANGELOW;
		*high = ISC_NET_PORTRANGEHIGH;
	}

	return ISC_R_SUCCESS; /* we currently never fail in this function */
}

void
isc_net_disableipv4(void) {
	initialize();
	if (ipv4_result == ISC_R_SUCCESS) {
		ipv4_result = ISC_R_DISABLED;
	}
}

void
isc_net_disableipv6(void) {
	initialize();
	if (ipv6_result == ISC_R_SUCCESS) {
		ipv6_result = ISC_R_DISABLED;
	}
}

void
isc_net_enableipv4(void) {
	initialize();
	if (ipv4_result == ISC_R_DISABLED) {
		ipv4_result = ISC_R_SUCCESS;
	}
}

void
isc_net_enableipv6(void) {
	initialize();
	if (ipv6_result == ISC_R_DISABLED) {
		ipv6_result = ISC_R_SUCCESS;
	}
}
