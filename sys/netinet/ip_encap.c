/*	$NetBSD: ip_encap.c,v 1.78 2025/02/26 04:49:45 andvar Exp $	*/
/*	$KAME: ip_encap.c,v 1.73 2001/10/02 08:30:58 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
/*
 * My grandfather said that there's a devil inside tunnelling technology...
 *
 * We have surprisingly many protocols that want packets with IP protocol
 * #4 or #41.  Here's a list of protocols that want protocol #41:
 *	RFC1933 configured tunnel
 *	RFC1933 automatic tunnel
 *	RFC2401 IPsec tunnel
 *	RFC2473 IPv6 generic packet tunnelling
 *	RFC2529 6over4 tunnel
 *	RFC3056 6to4 tunnel
 *	isatap tunnel
 *	mobile-ip6 (uses RFC2473)
 * Here's a list of protocol that want protocol #4:
 *	RFC1853 IPv4-in-IPv4 tunnelling
 *	RFC2003 IPv4 encapsulation within IPv4
 *	RFC2344 reverse tunnelling for mobile-ip4
 *	RFC2401 IPsec tunnel
 * Well, what can I say.  They impose different en/decapsulation mechanism
 * from each other, so they need separate protocol handler.  The only one
 * we can easily determine by protocol # is IPsec, which always has
 * AH/ESP/IPComp header right after outer IP header.
 *
 * So, clearly good old protosw does not work for protocol #4 and #41.
 * The code will let you match protocol via src/dst address pair.
 */
/* XXX is M_NETADDR correct? */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ip_encap.c,v 1.78 2025/02/26 04:49:45 andvar Exp $");

#ifdef _KERNEL_OPT
#include "opt_mrouting.h"
#include "opt_inet.h"
#include "opt_net_mpsafe.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/socketvar.h> /* for softnet_lock */
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/queue.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/psref.h>
#include <sys/pslist.h>
#include <sys/thmap.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_encap.h>
#ifdef MROUTING
#include <netinet/ip_mroute.h>
#endif /* MROUTING */

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip6protosw.h> /* for struct ip6ctlparam */
#include <netinet6/in6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet/icmp6.h>
#endif

#ifdef NET_MPSAFE
#define ENCAP_MPSAFE	1
#endif

enum direction { INBOUND, OUTBOUND };

#ifdef INET
static struct encaptab *encap4_lookup(struct mbuf *, int, int, enum direction,
    struct psref *);
#endif
#ifdef INET6
static struct encaptab *encap6_lookup(struct mbuf *, int, int, enum direction,
    struct psref *);
#endif
static int encap_add(struct encaptab *);
static int encap_remove(struct encaptab *);
static void encap_afcheck(int, const struct sockaddr *, const struct sockaddr *);
static void encap_key_init(struct encap_key *, const struct sockaddr *,
    const struct sockaddr *);
static void encap_key_inc(struct encap_key *);

/*
 * In encap[46]_lookup(), ep->func can sleep(e.g. rtalloc1) while walking
 * encap_table. So, it cannot use pserialize_read_enter()
 */
static struct {
	struct pslist_head	list;
	pserialize_t		psz;
	struct psref_class	*elem_class; /* for the element of et_list */
} encaptab  __cacheline_aligned = {
	.list = PSLIST_INITIALIZER,
};
#define encap_table encaptab.list

static struct {
	kmutex_t	lock;
	kcondvar_t	cv;
	struct lwp	*busy;
} encap_whole __cacheline_aligned;

static thmap_t *encap_map[2];	/* 0 for AF_INET, 1 for AF_INET6 */

static bool encap_initialized = false;
/*
 * must be done before other encap interfaces initialization.
 */
void
encapinit(void)
{

	if (encap_initialized)
		return;

	encaptab.psz = pserialize_create();
	encaptab.elem_class = psref_class_create("encapelem", IPL_SOFTNET);

	mutex_init(&encap_whole.lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&encap_whole.cv, "ip_encap cv");
	encap_whole.busy = NULL;

	encap_initialized = true;
}

void
encap_init(void)
{
	static int initialized = 0;

	if (initialized)
		return;
	initialized++;
#if 0
	/*
	 * we cannot use LIST_INIT() here, since drivers may want to call
	 * encap_attach(), on driver attach.  encap_init() will be called
	 * on AF_INET{,6} initialization, which happens after driver
	 * initialization - using LIST_INIT() here can nuke encap_attach()
	 * from drivers.
	 */
	PSLIST_INIT(&encap_table);
#endif

	encap_map[0] = thmap_create(0, NULL, THMAP_NOCOPY);
#ifdef INET6
	encap_map[1] = thmap_create(0, NULL, THMAP_NOCOPY);
#endif
}

#ifdef INET
static struct encaptab *
encap4_lookup(struct mbuf *m, int off, int proto, enum direction dir,
    struct psref *match_psref)
{
	struct ip *ip;
	struct ip_pack4 pack;
	struct encaptab *ep, *match;
	int prio, matchprio;
	int s;
	thmap_t *emap = encap_map[0];
	struct encap_key key;

	KASSERT(m->m_len >= sizeof(*ip));

	ip = mtod(m, struct ip *);

	memset(&pack, 0, sizeof(pack));
	pack.p.sp_len = sizeof(pack);
	pack.mine.sin_family = pack.yours.sin_family = AF_INET;
	pack.mine.sin_len = pack.yours.sin_len = sizeof(struct sockaddr_in);
	if (dir == INBOUND) {
		pack.mine.sin_addr = ip->ip_dst;
		pack.yours.sin_addr = ip->ip_src;
	} else {
		pack.mine.sin_addr = ip->ip_src;
		pack.yours.sin_addr = ip->ip_dst;
	}

	match = NULL;
	matchprio = 0;

	s = pserialize_read_enter();

	encap_key_init(&key, sintosa(&pack.mine), sintosa(&pack.yours));
	while ((ep = thmap_get(emap, &key, sizeof(key))) != NULL) {
		struct psref elem_psref;

		KASSERT(ep->af == AF_INET);

		if (ep->proto >= 0 && ep->proto != proto) {
			encap_key_inc(&key);
			continue;
		}

		psref_acquire(&elem_psref, &ep->psref,
		    encaptab.elem_class);
		if (ep->func) {
			pserialize_read_exit(s);
			prio = (*ep->func)(m, off, proto, ep->arg);
			s = pserialize_read_enter();
		} else {
			prio = pack.mine.sin_len + pack.yours.sin_len;
		}

		if (prio <= 0) {
			psref_release(&elem_psref, &ep->psref,
			    encaptab.elem_class);
			encap_key_inc(&key);
			continue;
		}
		if (prio > matchprio) {
			/* release last matched ep */
			if (match != NULL)
				psref_release(match_psref, &match->psref,
				    encaptab.elem_class);

			psref_copy(match_psref, &elem_psref,
			    encaptab.elem_class);
			matchprio = prio;
			match = ep;
		}

		psref_release(&elem_psref, &ep->psref,
		    encaptab.elem_class);
		encap_key_inc(&key);
	}

	PSLIST_READER_FOREACH(ep, &encap_table, struct encaptab, chain) {
		struct psref elem_psref;

		if (ep->af != AF_INET)
			continue;
		if (ep->proto >= 0 && ep->proto != proto)
			continue;

		psref_acquire(&elem_psref, &ep->psref,
		    encaptab.elem_class);
		pserialize_read_exit(s);
		/* ep->func is sleepable. e.g. rtalloc1 */
		prio = (*ep->func)(m, off, proto, ep->arg);
		s = pserialize_read_enter();

		/*
		 * We prioritize the matches by using bit length of the
		 * matches.  user-supplied matching function
		 * should return the bit length of the matches (for example,
		 * if both src/dst are matched for IPv4, 64 should be returned).
		 * 0 or negative return value means "it did not match".
		 *
		 * We need to loop through all the possible candidates
		 * to get the best match - the search takes O(n) for
		 * n attachments (i.e. interfaces).
		 */
		if (prio <= 0) {
			psref_release(&elem_psref, &ep->psref,
			    encaptab.elem_class);
			continue;
		}
		if (prio > matchprio) {
			/* release last matched ep */
			if (match != NULL)
				psref_release(match_psref, &match->psref,
				    encaptab.elem_class);

			psref_copy(match_psref, &elem_psref,
			    encaptab.elem_class);
			matchprio = prio;
			match = ep;
		}
		KASSERTMSG((match == NULL) || psref_held(&match->psref,
			encaptab.elem_class),
		    "current match = %p, but not hold its psref", match);

		psref_release(&elem_psref, &ep->psref,
		    encaptab.elem_class);
	}
	pserialize_read_exit(s);

	return match;
}

void
encap4_input(struct mbuf *m, int off, int proto)
{
	const struct encapsw *esw;
	struct encaptab *match;
	struct psref match_psref;

	match = encap4_lookup(m, off, proto, INBOUND, &match_psref);
	if (match) {
		/* found a match, "match" has the best one */
		esw = match->esw;
		if (esw && esw->encapsw4.pr_input) {
			(*esw->encapsw4.pr_input)(m, off, proto, match->arg);
			psref_release(&match_psref, &match->psref,
			    encaptab.elem_class);
		} else {
			psref_release(&match_psref, &match->psref,
			    encaptab.elem_class);
			m_freem(m);
		}
		return;
	}

	/* last resort: inject to raw socket */
	SOFTNET_LOCK_IF_NET_MPSAFE();
	rip_input(m, off, proto);
	SOFTNET_UNLOCK_IF_NET_MPSAFE();
}
#endif

#ifdef INET6
static struct encaptab *
encap6_lookup(struct mbuf *m, int off, int proto, enum direction dir,
    struct psref *match_psref)
{
	struct ip6_hdr *ip6;
	struct ip_pack6 pack;
	int prio, matchprio;
	int s;
	struct encaptab *ep, *match;
	thmap_t *emap = encap_map[1];
	struct encap_key key;

	KASSERT(m->m_len >= sizeof(*ip6));

	ip6 = mtod(m, struct ip6_hdr *);

	memset(&pack, 0, sizeof(pack));
	pack.p.sp_len = sizeof(pack);
	pack.mine.sin6_family = pack.yours.sin6_family = AF_INET6;
	pack.mine.sin6_len = pack.yours.sin6_len = sizeof(struct sockaddr_in6);
	if (dir == INBOUND) {
		pack.mine.sin6_addr = ip6->ip6_dst;
		pack.yours.sin6_addr = ip6->ip6_src;
	} else {
		pack.mine.sin6_addr = ip6->ip6_src;
		pack.yours.sin6_addr = ip6->ip6_dst;
	}

	match = NULL;
	matchprio = 0;

	s = pserialize_read_enter();

	encap_key_init(&key, sin6tosa(&pack.mine), sin6tosa(&pack.yours));
	while ((ep = thmap_get(emap, &key, sizeof(key))) != NULL) {
		struct psref elem_psref;

		KASSERT(ep->af == AF_INET6);

		if (ep->proto >= 0 && ep->proto != proto) {
			encap_key_inc(&key);
			continue;
		}

		psref_acquire(&elem_psref, &ep->psref,
		    encaptab.elem_class);
		if (ep->func) {
			pserialize_read_exit(s);
			prio = (*ep->func)(m, off, proto, ep->arg);
			s = pserialize_read_enter();
		} else {
			prio = pack.mine.sin6_len + pack.yours.sin6_len;
		}

		if (prio <= 0) {
			psref_release(&elem_psref, &ep->psref,
			    encaptab.elem_class);
			encap_key_inc(&key);
			continue;
		}
		if (prio > matchprio) {
			/* release last matched ep */
			if (match != NULL)
				psref_release(match_psref, &match->psref,
				    encaptab.elem_class);

			psref_copy(match_psref, &elem_psref,
			    encaptab.elem_class);
			matchprio = prio;
			match = ep;
		}
		psref_release(&elem_psref, &ep->psref,
		    encaptab.elem_class);
		encap_key_inc(&key);
	}

	PSLIST_READER_FOREACH(ep, &encap_table, struct encaptab, chain) {
		struct psref elem_psref;

		if (ep->af != AF_INET6)
			continue;
		if (ep->proto >= 0 && ep->proto != proto)
			continue;

		psref_acquire(&elem_psref, &ep->psref,
		    encaptab.elem_class);

		pserialize_read_exit(s);
		/* ep->func is sleepable. e.g. rtalloc1 */
		prio = (*ep->func)(m, off, proto, ep->arg);
		s = pserialize_read_enter();

		/* see encap4_lookup() for issues here */
		if (prio <= 0) {
			psref_release(&elem_psref, &ep->psref,
			    encaptab.elem_class);
			continue;
		}
		if (prio > matchprio) {
			/* release last matched ep */
			if (match != NULL)
				psref_release(match_psref, &match->psref,
				    encaptab.elem_class);

			psref_copy(match_psref, &elem_psref,
			    encaptab.elem_class);
			matchprio = prio;
			match = ep;
		}
		KASSERTMSG((match == NULL) || psref_held(&match->psref,
			encaptab.elem_class),
		    "current match = %p, but not hold its psref", match);

		psref_release(&elem_psref, &ep->psref,
		    encaptab.elem_class);
	}
	pserialize_read_exit(s);

	return match;
}

int
encap6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	const struct encapsw *esw;
	struct encaptab *match;
	struct psref match_psref;
	int rv;

	match = encap6_lookup(m, *offp, proto, INBOUND, &match_psref);

	if (match) {
		/* found a match */
		esw = match->esw;
		if (esw && esw->encapsw6.pr_input) {
			int ret;
			ret = (*esw->encapsw6.pr_input)(mp, offp, proto,
			    match->arg);
			psref_release(&match_psref, &match->psref,
			    encaptab.elem_class);
			return ret;
		} else {
			psref_release(&match_psref, &match->psref,
			    encaptab.elem_class);
			m_freem(m);
			return IPPROTO_DONE;
		}
	}

	/* last resort: inject to raw socket */
	SOFTNET_LOCK_IF_NET_MPSAFE();
	rv = rip6_input(mp, offp, proto);
	SOFTNET_UNLOCK_IF_NET_MPSAFE();
	return rv;
}
#endif

static int
encap_add(struct encaptab *ep)
{

	KASSERT(encap_lock_held());

	PSLIST_WRITER_INSERT_HEAD(&encap_table, ep, chain);

	return 0;
}

static int
encap_remove(struct encaptab *ep)
{
	int error = 0;

	KASSERT(encap_lock_held());

	PSLIST_WRITER_REMOVE(ep, chain);

	return error;
}

static void
encap_afcheck(int af, const struct sockaddr *sp, const struct sockaddr *dp)
{

	KASSERT(sp != NULL && dp != NULL);
	KASSERT(sp->sa_len == dp->sa_len);
	KASSERT(af == sp->sa_family && af == dp->sa_family);

	socklen_t len __diagused = sockaddr_getsize_by_family(af);
	KASSERT(len != 0 && len == sp->sa_len && len == dp->sa_len);
}

const struct encaptab *
encap_attach_func(int af, int proto,
    encap_priofunc_t *func,
    const struct encapsw *esw, void *arg)
{
	struct encaptab *ep;
	int error;
#ifndef ENCAP_MPSAFE
	int s;

	s = splsoftnet();
#endif

	ASSERT_SLEEPABLE();

	/* sanity check on args */
	KASSERT(func != NULL);
	KASSERT(af == AF_INET
#ifdef INET6
	    || af == AF_INET6
#endif
	);

	ep = kmem_alloc(sizeof(*ep), KM_SLEEP);
	memset(ep, 0, sizeof(*ep));

	ep->af = af;
	ep->proto = proto;
	ep->func = func;
	ep->esw = esw;
	ep->arg = arg;
	psref_target_init(&ep->psref, encaptab.elem_class);

	error = encap_add(ep);
	if (error)
		goto gc;

	error = 0;
#ifndef ENCAP_MPSAFE
	splx(s);
#endif
	return ep;

gc:
	kmem_free(ep, sizeof(*ep));
#ifndef ENCAP_MPSAFE
	splx(s);
#endif
	return NULL;
}

static void
encap_key_init(struct encap_key *key,
    const struct sockaddr *local, const struct sockaddr *remote)
{

	memset(key, 0, sizeof(*key));

	sockaddr_copy(&key->local_sa, sizeof(key->local_u), local);
	sockaddr_copy(&key->remote_sa, sizeof(key->remote_u), remote);
}

static void
encap_key_inc(struct encap_key *key)
{

	(key->seq)++;
}

static void
encap_key_dec(struct encap_key *key)
{

	(key->seq)--;
}

static void
encap_key_copy(struct encap_key *dst, const struct encap_key *src)
{

	memset(dst, 0, sizeof(*dst));
	*dst = *src;
}

/*
 * src is always my side, and dst is always remote side.
 * Return value will be necessary as input (cookie) for encap_detach().
 */
const struct encaptab *
encap_attach_addr(int af, int proto,
    const struct sockaddr *src, const struct sockaddr *dst,
    encap_priofunc_t *func,
    const struct encapsw *esw, void *arg)
{
	struct encaptab *ep;
	size_t l;
	thmap_t *emap;
	void *retep;
	struct ip_pack4 *pack4;
#ifdef INET6
	struct ip_pack6 *pack6;
#endif

	ASSERT_SLEEPABLE();

	encap_afcheck(af, src, dst);

	switch (af) {
	case AF_INET:
		l = sizeof(*pack4);
		emap = encap_map[0];
		break;
#ifdef INET6
	case AF_INET6:
		l = sizeof(*pack6);
		emap = encap_map[1];
		break;
#endif
	default:
		return NULL;
	}

	ep = kmem_zalloc(sizeof(*ep), KM_SLEEP);
	ep->addrpack = kmem_zalloc(l, KM_SLEEP);
	ep->addrpack->sa_len = l & 0xff;
	ep->af = af;
	ep->proto = proto;
	ep->flag = IP_ENCAP_ADDR_ENABLE;
	switch (af) {
	case AF_INET:
		pack4 = (struct ip_pack4 *)ep->addrpack;
		ep->src = (struct sockaddr *)&pack4->mine;
		ep->dst = (struct sockaddr *)&pack4->yours;
		break;
#ifdef INET6
	case AF_INET6:
		pack6 = (struct ip_pack6 *)ep->addrpack;
		ep->src = (struct sockaddr *)&pack6->mine;
		ep->dst = (struct sockaddr *)&pack6->yours;
		break;
#endif
	}
	memcpy(ep->src, src, src->sa_len);
	memcpy(ep->dst, dst, dst->sa_len);
	ep->esw = esw;
	ep->arg = arg;
	ep->func = func;
	psref_target_init(&ep->psref, encaptab.elem_class);

	encap_key_init(&ep->key, src, dst);
	while ((retep = thmap_put(emap, &ep->key, sizeof(ep->key), ep)) != ep)
		encap_key_inc(&ep->key);
	return ep;
}


/* XXX encap4_ctlinput() is necessary if we set DF=1 on outer IPv4 header */

#ifdef INET6
void *
encap6_ctlinput(int cmd, const struct sockaddr *sa, void *d0)
{
	void *d = d0;
	struct ip6_hdr *ip6;
	struct mbuf *m;
	int off;
	struct ip6ctlparam *ip6cp = NULL;
	int nxt;
	int s;
	struct encaptab *ep;
	const struct encapsw *esw;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return NULL;

	if ((unsigned)cmd >= PRC_NCMDS)
		return NULL;
	if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (cmd == PRC_MSGSIZE)
		; /* special code is present, see below */
	else if (inet6ctlerrmap[cmd] == 0)
		return NULL;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		nxt = ip6cp->ip6c_nxt;

		if (ip6 && cmd == PRC_MSGSIZE) {
			int valid = 0;
			struct encaptab *match;
			struct psref elem_psref;

			/*
		 	* Check to see if we have a valid encap configuration.
		 	*/
			match = encap6_lookup(m, off, nxt, OUTBOUND,
			    &elem_psref);
			if (match) {
				valid++;
				psref_release(&elem_psref, &match->psref,
				    encaptab.elem_class);
			}

			/*
		 	* Depending on the value of "valid" and routing table
		 	* size (mtudisc_{hi,lo}wat), we will:
		 	* - recalculate the new MTU and create the
		 	*   corresponding routing entry, or
		 	* - ignore the MTU change notification.
		 	*/
			icmp6_mtudisc_update((struct ip6ctlparam *)d, valid);
		}
	} else {
		m = NULL;
		ip6 = NULL;
		nxt = -1;
	}

	/* inform all listeners */

	s = pserialize_read_enter();
	PSLIST_READER_FOREACH(ep, &encap_table, struct encaptab, chain) {
		struct psref elem_psref;

		if (ep->af != AF_INET6)
			continue;
		if (ep->proto >= 0 && ep->proto != nxt)
			continue;

		/* should optimize by looking at address pairs */

		/* XXX need to pass ep->arg or ep itself to listeners */
		psref_acquire(&elem_psref, &ep->psref,
		    encaptab.elem_class);
		esw = ep->esw;
		if (esw && esw->encapsw6.pr_ctlinput) {
			pserialize_read_exit(s);
			/* pr_ctlinput is sleepable. e.g. rtcache_free */
			(*esw->encapsw6.pr_ctlinput)(cmd, sa, d, ep->arg);
			s = pserialize_read_enter();
		}
		psref_release(&elem_psref, &ep->psref,
		    encaptab.elem_class);
	}
	pserialize_read_exit(s);

	rip6_ctlinput(cmd, sa, d0);
	return NULL;
}
#endif

static int
encap_detach_addr(const struct encaptab *ep)
{
	thmap_t *emap;
	struct encaptab *retep;
	struct encaptab *target;
	void *thgc;
	struct encap_key key;

	KASSERT(encap_lock_held());
	KASSERT(ep->flag & IP_ENCAP_ADDR_ENABLE);

	switch (ep->af) {
	case AF_INET:
		emap = encap_map[0];
		break;
#ifdef INET6
	case AF_INET6:
		emap = encap_map[1];
		break;
#endif
	default:
		return EINVAL;
	}

	retep = thmap_del(emap, &ep->key, sizeof(ep->key));
	if (retep != ep) {
		return ENOENT;
	}
	target = retep;

	/*
	 * To keep continuity, decrement seq after detached encaptab.
	 */
	encap_key_copy(&key, &ep->key);
	encap_key_inc(&key);
	while ((retep = thmap_del(emap, &key, sizeof(key))) != NULL) {
		void *pp;

		encap_key_dec(&retep->key);
		pp = thmap_put(emap, &retep->key, sizeof(retep->key), retep);
		KASSERT(retep == pp);

		encap_key_inc(&key);
	}

	thgc = thmap_stage_gc(emap);
	pserialize_perform(encaptab.psz);
	thmap_gc(emap, thgc);
	psref_target_destroy(&target->psref, encaptab.elem_class);
	kmem_free(target->addrpack, target->addrpack->sa_len);
	kmem_free(target, sizeof(*target));

	return 0;
}

int
encap_detach(const struct encaptab *cookie)
{
	const struct encaptab *ep = cookie;
	struct encaptab *p;
	int error;

	KASSERT(encap_lock_held());

	if (ep->flag & IP_ENCAP_ADDR_ENABLE)
		return encap_detach_addr(ep);

	PSLIST_WRITER_FOREACH(p, &encap_table, struct encaptab, chain) {
		if (p == ep) {
			error = encap_remove(p);
			if (error)
				return error;
			else
				break;
		}
	}
	if (p == NULL)
		return ENOENT;

	pserialize_perform(encaptab.psz);
	psref_target_destroy(&p->psref,
	    encaptab.elem_class);
	kmem_free(p, sizeof(*p));

	return 0;
}

int
encap_lock_enter(void)
{
	int error;

	mutex_enter(&encap_whole.lock);
	while (encap_whole.busy != NULL) {
		error = cv_wait_sig(&encap_whole.cv, &encap_whole.lock);
		if (error) {
			mutex_exit(&encap_whole.lock);
			return error;
		}
	}
	KASSERT(encap_whole.busy == NULL);
	encap_whole.busy = curlwp;
	mutex_exit(&encap_whole.lock);

	return 0;
}

void
encap_lock_exit(void)
{

	mutex_enter(&encap_whole.lock);
	KASSERT(encap_whole.busy == curlwp);
	encap_whole.busy = NULL;
	cv_broadcast(&encap_whole.cv);
	mutex_exit(&encap_whole.lock);
}

bool
encap_lock_held(void)
{

	return (encap_whole.busy == curlwp);
}
