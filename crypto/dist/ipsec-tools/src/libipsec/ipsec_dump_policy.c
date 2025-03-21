/*	$NetBSD: ipsec_dump_policy.c,v 1.13 2025/03/10 15:59:04 christos Exp $	*/

/* Id: ipsec_dump_policy.c,v 1.10 2005/06/29 09:12:37 manubsd Exp */

/*
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include PATH_IPSEC_H

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include "ipsec_strerror.h"
#include "libpfkey.h"

static const char *ipsp_dir_strs[] = {
	"any", "in", "out", "fwd",
#ifdef __linux__
	"in(socket)", "out(socket)"
#endif
};

static const char *ipsp_policy_strs[] = {
	"discard", "none", "ipsec", "entrust", "bypass",
};

static char *ipsec_dump_ipsecrequest(char *, size_t,
	const struct sadb_x_ipsecrequest *, size_t, int);
static char *ipsec_dump_policy1(const void *, const char *, int);
static int set_addresses(char *, size_t, const struct sockaddr *,
	const struct sockaddr *, int);
static char *set_address(char *, size_t, const struct sockaddr *, int);

/*
 * policy is sadb_x_policy buffer.
 * Must call free() later.
 * When delimiter == NULL, alternatively ' '(space) is applied.
 */
char *
ipsec_dump_policy(ipsec_policy_t policy, __ipsec_const char *delimiter)
{
	return ipsec_dump_policy1(policy, delimiter, 0);
}

char *
ipsec_dump_policy_withports(void *policy, const char *delimiter)
{
	return ipsec_dump_policy1(policy, delimiter, 1);
}

static char *
ipsec_dump_policy1(const void *policy, const char *delimiter, int withports)
{
	const struct sadb_x_policy *xpl = policy;
	const struct sadb_x_ipsecrequest *xisr;
	size_t off, buflen, extlen;
	char *buf;
	char isrbuf[4096];
	char *newbuf;

#ifdef HAVE_PFKEY_POLICY_PRIORITY
	int32_t priority_offset;
	char *priority_str;
	char operator;
#endif

	/* sanity check */
	if (policy == NULL)
		return NULL;
	if (xpl->sadb_x_policy_exttype != SADB_X_EXT_POLICY) {
		__ipsec_errcode = EIPSEC_INVAL_EXTTYPE;
		return NULL;
	}

	/* set delimiter */
	if (delimiter == NULL)
		delimiter = " ";

#ifdef HAVE_PFKEY_POLICY_PRIORITY
	if (xpl->sadb_x_policy_priority == 0)
	{
		priority_offset = 0;
		priority_str = "";
	}
	/* find which constant the priority is closest to */
	else if (xpl->sadb_x_policy_priority < 
	         (u_int32_t) (PRIORITY_DEFAULT / 4) * 3)
	{
		priority_offset = xpl->sadb_x_policy_priority - PRIORITY_HIGH;
		priority_str = "prio high";
	}
	else if (xpl->sadb_x_policy_priority >= 
	         (u_int32_t) (PRIORITY_DEFAULT / 4) * 3 &&
	         xpl->sadb_x_policy_priority < 
	         (u_int32_t) (PRIORITY_DEFAULT / 4) * 5)
	{
		priority_offset = xpl->sadb_x_policy_priority - PRIORITY_DEFAULT;
		priority_str = "prio def";
	}
	else
	{
		priority_offset = xpl->sadb_x_policy_priority - PRIORITY_LOW;
		priority_str = "prio low";
	}

	/* fix sign to match the way it is input */
	priority_offset *= -1;
	if (priority_offset < 0)
	{
		operator = '-';
		priority_offset *= -1;
	}
	else
	{
		operator = '+';
	}
#endif
	
	switch (xpl->sadb_x_policy_dir) {
	case IPSEC_DIR_ANY:
	case IPSEC_DIR_INBOUND:
	case IPSEC_DIR_OUTBOUND:
#ifdef HAVE_POLICY_FWD
	case IPSEC_DIR_FWD:
	case IPSEC_DIR_FWD + 1:
	case IPSEC_DIR_FWD + 2:
#endif
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_DIR;
		return NULL;
	}

	switch (xpl->sadb_x_policy_type) {
	case IPSEC_POLICY_DISCARD:
	case IPSEC_POLICY_NONE:
	case IPSEC_POLICY_IPSEC:
	case IPSEC_POLICY_BYPASS:
	case IPSEC_POLICY_ENTRUST:
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_POLICY;
		return NULL;
	}

	buflen = strlen(ipsp_dir_strs[xpl->sadb_x_policy_dir])
		+ 1	/* space */
#ifdef HAVE_PFKEY_POLICY_PRIORITY
		+ strlen(priority_str)
		+ ((priority_offset != 0) ? 13 : 0) /* [space operator space int] */
		+ ((strlen(priority_str) != 0) ? 1 : 0) /* space */
#endif
		+ strlen(ipsp_policy_strs[xpl->sadb_x_policy_type])
		+ 1;	/* NUL */

	if ((buf = malloc(buflen)) == NULL) {
		__ipsec_errcode = EIPSEC_NO_BUFS;
		return NULL;
	}
#ifdef HAVE_PFKEY_POLICY_PRIORITY
	if (priority_offset != 0)
	{
		snprintf(buf, buflen, "%s %s %c %u %s", 
	    	ipsp_dir_strs[xpl->sadb_x_policy_dir], priority_str, operator, 
			priority_offset, ipsp_policy_strs[xpl->sadb_x_policy_type]);
	}
	else if (strlen (priority_str) != 0)
	{
		snprintf(buf, buflen, "%s %s %s", 
	    	ipsp_dir_strs[xpl->sadb_x_policy_dir], priority_str, 
			ipsp_policy_strs[xpl->sadb_x_policy_type]);
	}
	else
	{
		snprintf(buf, buflen, "%s %s", 
	    	ipsp_dir_strs[xpl->sadb_x_policy_dir],
			ipsp_policy_strs[xpl->sadb_x_policy_type]);
	}
#else
	snprintf(buf, buflen, "%s %s", ipsp_dir_strs[xpl->sadb_x_policy_dir],
	    ipsp_policy_strs[xpl->sadb_x_policy_type]);
#endif

	if (xpl->sadb_x_policy_type != IPSEC_POLICY_IPSEC) {
		__ipsec_errcode = EIPSEC_NO_ERROR;
		return buf;
	}

	/* count length of buffer for use */
	off = sizeof(*xpl);
	extlen = PFKEY_EXTLEN(xpl);
	while (off < extlen) {
		xisr = (const void *)((const char *)xpl + off);
		off += xisr->sadb_x_ipsecrequest_len;
	}

	/* validity check */
	if (off != extlen) {
		__ipsec_errcode = EIPSEC_INVAL_SADBMSG;
		free(buf);
		return NULL;
	}

	off = sizeof(*xpl);
	while (off < extlen) {
		size_t offset;
		xisr = (const void *)((const char *)xpl + off);

		if (ipsec_dump_ipsecrequest(isrbuf, sizeof(isrbuf), xisr,
		    extlen - off, withports) == NULL) {
			free(buf);
			return NULL;
		}

		offset = strlen(buf);
		buflen = offset + strlen(delimiter) + strlen(isrbuf) + 1;
		newbuf = realloc(buf, buflen);
		if (newbuf == NULL) {
			__ipsec_errcode = EIPSEC_NO_BUFS;
			free(buf);
			return NULL;
		}
		buf = newbuf;
		snprintf(buf+offset, buflen-offset, "%s%s", delimiter, isrbuf);

		off += xisr->sadb_x_ipsecrequest_len;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return buf;
}

static char *
ipsec_dump_ipsecrequest(char *buf, size_t len,
    const struct sadb_x_ipsecrequest *xisr,
    size_t bound /* boundary */, int withports)
{
	const char *proto, *mode, *level;
	char abuf[(NI_MAXHOST + NI_MAXSERV + 3) * 2 + 2];

	if (xisr->sadb_x_ipsecrequest_len > bound) {
		__ipsec_errcode = EIPSEC_INVAL_PROTO;
		return NULL;
	}

	switch (xisr->sadb_x_ipsecrequest_proto) {
	case IPPROTO_ESP:
		proto = "esp";
		break;
	case IPPROTO_AH:
		proto = "ah";
		break;
	case IPPROTO_IPCOMP:
		proto = "ipcomp";
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_PROTO;
		return NULL;
	}

	switch (xisr->sadb_x_ipsecrequest_mode) {
	case IPSEC_MODE_ANY:
		mode = "any";
		break;
	case IPSEC_MODE_TRANSPORT:
		mode = "transport";
		break;
	case IPSEC_MODE_TUNNEL:
		mode = "tunnel";
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_MODE;
		return NULL;
	}

	abuf[0] = '\0';
	if (xisr->sadb_x_ipsecrequest_len > sizeof(*xisr)) {
		const struct sockaddr *sa1, *sa2;
		const char *p;

		p = (const void *)(xisr + 1);
		sa1 = (const void *)p;
		sa2 = (const void *)(p + sysdep_sa_len(sa1));
		if (sizeof(*xisr) + sysdep_sa_len(sa1) + sysdep_sa_len(sa2) !=
		    xisr->sadb_x_ipsecrequest_len) {
			__ipsec_errcode = EIPSEC_INVAL_ADDRESS;
			return NULL;
		}
		if (set_addresses(abuf, sizeof(abuf), 
		    sa1, sa2, withports) != 0) {
			__ipsec_errcode = EIPSEC_INVAL_ADDRESS;
			return NULL;
		}
	}

	switch (xisr->sadb_x_ipsecrequest_level) {
	case IPSEC_LEVEL_DEFAULT:
		level = "default";
		break;
	case IPSEC_LEVEL_USE:
		level = "use";
		break;
	case IPSEC_LEVEL_REQUIRE:
		level = "require";
		break;
	case IPSEC_LEVEL_UNIQUE:
		level = "unique";
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_LEVEL;
		return NULL;
	}

	if (xisr->sadb_x_ipsecrequest_reqid == 0)
		snprintf(buf, len, "%s/%s/%s/%s", proto, mode, abuf, level);
	else {
		int ch;

		if (xisr->sadb_x_ipsecrequest_reqid > IPSEC_MANUAL_REQID_MAX)
			ch = '#';
		else
			ch = ':';
		snprintf(buf, len, "%s/%s/%s/%s%c%u", proto, mode, abuf, level,
		    ch, xisr->sadb_x_ipsecrequest_reqid);
	}

	return buf;
}

static int
set_addresses(char *buf, size_t len, const struct sockaddr *sa1,
    const struct sockaddr *sa2, int withports)
{
	char tmp1[NI_MAXHOST + NI_MAXSERV + 3], tmp2[sizeof(tmp1)];

	if (set_address(tmp1, sizeof(tmp1), sa1, withports) == NULL ||
	    set_address(tmp2, sizeof(tmp2), sa2, withports) == NULL)
		return -1;
	if (strlen(tmp1) + 1 + strlen(tmp2) + 1 > len)
		return -1;
	snprintf(buf, len, "%s-%s", tmp1, tmp2);
	return 0;
}

static char *
set_address(char *buf, size_t len, const struct sockaddr *sa, int withports)
{
	const int niflags = NI_NUMERICHOST | NI_NUMERICSERV;
	char host[NI_MAXHOST];
	char serv[NI_MAXSERV];

	if (len < 1)
		return NULL;
	buf[0] = '\0';
	if (getnameinfo(sa, (socklen_t)sysdep_sa_len(sa), host, sizeof(host), 
	    serv, sizeof(serv), niflags) != 0)
		return NULL;

	if (withports)
		snprintf(buf, len, "%s[%s]", host, serv);
	else
		snprintf(buf, len, "%s", host);

	return buf;
}
