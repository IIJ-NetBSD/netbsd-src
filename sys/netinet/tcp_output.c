/*	$NetBSD: tcp_output.c,v 1.222 2024/09/08 09:36:52 rillig Exp $	*/

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
 *      @(#)COPYRIGHT   1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 *      This product includes software developed at the Information
 *      Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

/*-
 * Copyright (c) 1997, 1998, 2001, 2005, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe and Kevin M. Lahey of the Numerical Aerospace Simulation
 * Facility, NASA Ames Research Center.
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
 * This code is derived from software contributed to The NetBSD Foundation
 * by Rui Paulo.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_output.c	8.4 (Berkeley) 5/24/95
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: tcp_output.c,v 1.222 2024/09/08 09:36:52 rillig Exp $");

#ifdef _KERNEL_OPT
#include "opt_inet.h"
#include "opt_ipsec.h"
#include "opt_tcp_debug.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#ifdef TCP_SIGNATURE
#include <sys/md5.h>
#endif

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/nd6.h>
#endif

#ifdef IPSEC
#include <netipsec/ipsec.h>
#include <netipsec/key.h>
#ifdef INET6
#include <netipsec/ipsec6.h>
#endif
#endif

#include <netinet/tcp.h>
#define	TCPOUTFLAGS
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_private.h>
#include <netinet/tcp_congctl.h>
#include <netinet/tcp_debug.h>
#include <netinet/in_offload.h>
#include <netinet6/in6_offload.h>

/*
 * Knob to enable Congestion Window Monitoring, and control
 * the burst size it allows.  Default burst is 4 packets, per
 * the Internet draft.
 */
int	tcp_cwm = 0;
int	tcp_cwm_burstsize = 4;

int	tcp_do_autosndbuf = 1;
int	tcp_autosndbuf_inc = 8 * 1024;
int	tcp_autosndbuf_max = 256 * 1024;

#ifdef TCP_OUTPUT_COUNTERS
#include <sys/device.h>

extern struct evcnt tcp_output_bigheader;
extern struct evcnt tcp_output_predict_hit;
extern struct evcnt tcp_output_predict_miss;
extern struct evcnt tcp_output_copysmall;
extern struct evcnt tcp_output_copybig;
extern struct evcnt tcp_output_refbig;

#define	TCP_OUTPUT_COUNTER_INCR(ev)	(ev)->ev_count++
#else

#define	TCP_OUTPUT_COUNTER_INCR(ev)	/* nothing */

#endif /* TCP_OUTPUT_COUNTERS */

static int
tcp_segsize(struct tcpcb *tp, int *txsegsizep, int *rxsegsizep,
    bool *alwaysfragp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = NULL;
	struct rtentry *rt;
	struct ifnet *ifp;
	int size;
	int hdrlen;
	int optlen;

	*alwaysfragp = false;
	size = tcp_mssdflt;

	switch (tp->t_family) {
	case AF_INET:
		hdrlen = sizeof(struct ip) + sizeof(struct tcphdr);
		break;
#ifdef INET6
	case AF_INET6:
		hdrlen = sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
		break;
#endif
	default:
		hdrlen = 1; /* prevent zero sized segments */
		goto out;
	}

	rt = inpcb_rtentry(inp);
	so = inp->inp_socket;
	if (rt == NULL) {
		goto out;
	}

	ifp = rt->rt_ifp;

	if (tp->t_mtudisc && rt->rt_rmx.rmx_mtu != 0) {
#ifdef INET6
		if (inp->inp_af == AF_INET6 && rt->rt_rmx.rmx_mtu < IPV6_MMTU) {
			/*
			 * RFC2460 section 5, last paragraph: if path MTU is
			 * smaller than 1280, use 1280 as packet size and
			 * attach fragment header.
			 */
			size = IPV6_MMTU - hdrlen - sizeof(struct ip6_frag);
			*alwaysfragp = true;
		} else
			size = rt->rt_rmx.rmx_mtu - hdrlen;
#else
		size = rt->rt_rmx.rmx_mtu - hdrlen;
#endif
	} else if (ifp->if_flags & IFF_LOOPBACK)
		size = ifp->if_mtu - hdrlen;
	else if (inp->inp_af == AF_INET && tp->t_mtudisc)
		size = ifp->if_mtu - hdrlen;
	else if (inp->inp_af == AF_INET && in_localaddr(in4p_faddr(inp)))
		size = ifp->if_mtu - hdrlen;
#ifdef INET6
	else if (inp->inp_af == AF_INET6) {
		if (IN6_IS_ADDR_V4MAPPED(&in6p_faddr(inp))) {
			/* mapped addr case */
			struct in_addr d;
			memcpy(&d, &in6p_faddr(inp).s6_addr32[3], sizeof(d));
			if (tp->t_mtudisc || in_localaddr(d))
				size = ifp->if_mtu - hdrlen;
		} else {
			/*
			 * for IPv6, path MTU discovery is always turned on,
			 * or the node must use packet size <= 1280.
			 */
			size = tp->t_mtudisc ? ifp->if_mtu : IPV6_MMTU;
			size -= hdrlen;
		}
	}
#endif
	inpcb_rtentry_unref(rt, inp);
 out:
	/*
	 * Now we must make room for whatever extra TCP/IP options are in
	 * the packet.
	 */
	optlen = tcp_optlen(tp);

	/*
	 * XXX tp->t_ourmss should have the right size, but without this code
	 * fragmentation will occur... need more investigation
	 */

	if (inp->inp_af == AF_INET) {
#if defined(IPSEC)
		if (ipsec_used &&
		    !ipsec_pcb_skip_ipsec(inp->inp_sp, IPSEC_DIR_OUTBOUND))
			optlen += ipsec4_hdrsiz_tcp(tp);
#endif
		optlen += ip_optlen(inp);
	}

#ifdef INET6
	if (inp->inp_af == AF_INET6 && tp->t_family == AF_INET) {
#if defined(IPSEC)
		if (ipsec_used &&
		    !ipsec_pcb_skip_ipsec(inp->inp_sp, IPSEC_DIR_OUTBOUND))
			optlen += ipsec4_hdrsiz_tcp(tp);
#endif
		/* XXX size -= ip_optlen(in6p); */
	} else if (inp->inp_af == AF_INET6) {
#if defined(IPSEC)
		if (ipsec_used &&
		    !ipsec_pcb_skip_ipsec(inp->inp_sp, IPSEC_DIR_OUTBOUND))
			optlen += ipsec6_hdrsiz_tcp(tp);
#endif
		optlen += ip6_optlen(inp);
	}
#endif
	size -= optlen;

	/*
	 * There may not be any room for data if mtu is too small. This
	 * includes zero-sized.
	 */
	if (size <= 0) {
		return EMSGSIZE;
	}

	/*
	 * *rxsegsizep holds *estimated* inbound segment size (estimation
	 * assumes that path MTU is the same for both ways).  this is only
	 * for silly window avoidance, do not use the value for other purposes.
	 *
	 * ipseclen is subtracted from both sides, this may not be right.
	 * I'm not quite sure about this (could someone comment).
	 */
	*txsegsizep = uimin(tp->t_peermss - optlen, size);
	*rxsegsizep = uimin(tp->t_ourmss - optlen, size);

	/*
	 * Never send more than half a buffer full.  This insures that we can
	 * always keep 2 packets on the wire, no matter what SO_SNDBUF is, and
	 * therefore acks will never be delayed unless we run out of data to
	 * transmit.
	 */
	if (so) {
		*txsegsizep = uimin(so->so_snd.sb_hiwat >> 1, *txsegsizep);
	}

	/*
	 * A segment must at least store header + options
	 */
	if (*txsegsizep < hdrlen + optlen) {
		return EMSGSIZE;
	}

	if (*txsegsizep != tp->t_segsz) {
		/*
		 * If the new segment size is larger, we don't want to
		 * mess up the congestion window, but if it is smaller
		 * we'll have to reduce the congestion window to ensure
		 * that we don't get into trouble with initial windows
		 * and the rest.  In any case, if the segment size
		 * has changed, chances are the path has, too, and
		 * our congestion window will be different.
		 */
		if (*txsegsizep < tp->t_segsz) {
			tp->snd_cwnd = uimax((tp->snd_cwnd / tp->t_segsz)
			    * *txsegsizep, *txsegsizep);
			tp->snd_ssthresh = uimax((tp->snd_ssthresh / tp->t_segsz)
			    * *txsegsizep, *txsegsizep);
		}
		tp->t_segsz = *txsegsizep;
	}

	return 0;
}

static int
tcp_build_datapkt(struct tcpcb *tp, struct socket *so, int off,
    long len, int hdrlen, struct mbuf **mp)
{
	struct mbuf *m, *m0;
	net_stat_ref_t tcps;

	tcps = TCP_STAT_GETREF();
	if (tp->t_force && len == 1)
		_NET_STATINC_REF(tcps, TCP_STAT_SNDPROBE);
	else if (SEQ_LT(tp->snd_nxt, tp->snd_max)) {
		tp->t_sndrexmitpack++;
		_NET_STATINC_REF(tcps, TCP_STAT_SNDREXMITPACK);
		_NET_STATADD_REF(tcps, TCP_STAT_SNDREXMITBYTE, len);
	} else {
		_NET_STATINC_REF(tcps, TCP_STAT_SNDPACK);
		_NET_STATADD_REF(tcps, TCP_STAT_SNDBYTE, len);
	}
	TCP_STAT_PUTREF();

	MGETHDR(m, M_DONTWAIT, MT_HEADER);
	if (__predict_false(m == NULL))
		return ENOBUFS;
	MCLAIM(m, &tcp_tx_mowner);

	/*
	 * XXX Because other code assumes headers will fit in
	 * XXX one header mbuf.
	 *
	 * (This code should almost *never* be run.)
	 */
	if (__predict_false((max_linkhdr + hdrlen) > MHLEN)) {
		TCP_OUTPUT_COUNTER_INCR(&tcp_output_bigheader);
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return ENOBUFS;
		}
	}

	m->m_data += max_linkhdr;
	m->m_len = hdrlen;

	/*
	 * To avoid traversing the whole sb_mb chain for correct
	 * data to send, remember last sent mbuf, its offset and
	 * the sent size.  When called the next time, see if the
	 * data to send is directly following the previous transfer.
	 * This is important for large TCP windows.
	 */
	if (off == 0 || tp->t_lastm == NULL ||
	    (tp->t_lastoff + tp->t_lastlen) != off) {
		TCP_OUTPUT_COUNTER_INCR(&tcp_output_predict_miss);
		/*
		 * Either a new packet or a retransmit.
		 * Start from the beginning.
		 */
		tp->t_lastm = so->so_snd.sb_mb;
		tp->t_inoff = off;
	} else {
		TCP_OUTPUT_COUNTER_INCR(&tcp_output_predict_hit);
		tp->t_inoff += tp->t_lastlen;
	}

	/* Traverse forward to next packet */
	while (tp->t_inoff > 0) {
		if (tp->t_lastm == NULL)
			panic("tp->t_lastm == NULL");
		if (tp->t_inoff < tp->t_lastm->m_len)
			break;
		tp->t_inoff -= tp->t_lastm->m_len;
		tp->t_lastm = tp->t_lastm->m_next;
	}

	tp->t_lastoff = off;
	tp->t_lastlen = len;
	m0 = tp->t_lastm;
	off = tp->t_inoff;

	if (len <= M_TRAILINGSPACE(m)) {
		m_copydata(m0, off, (int)len, mtod(m, char *) + hdrlen);
		m->m_len += len;
		TCP_OUTPUT_COUNTER_INCR(&tcp_output_copysmall);
	} else {
		m->m_next = m_copym(m0, off, (int)len, M_DONTWAIT);
		if (m->m_next == NULL) {
			m_freem(m);
			return ENOBUFS;
		}
#ifdef TCP_OUTPUT_COUNTERS
		if (m->m_next->m_flags & M_EXT)
			TCP_OUTPUT_COUNTER_INCR(&tcp_output_refbig);
		else
			TCP_OUTPUT_COUNTER_INCR(&tcp_output_copybig);
#endif
	}

	*mp = m;
	return 0;
}

/*
 * Tcp output routine: figure out what should be sent and send it.
 */
int
tcp_output(struct tcpcb *tp)
{
	struct rtentry *rt = NULL;
	struct socket *so;
	struct route *ro;
	long len, win;
	int off, flags, error;
	struct mbuf *m;
	struct ip *ip;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct tcphdr *th;
	u_char opt[MAX_TCPOPTLEN], *optp;
#define OPT_FITS(more)	((optlen + (more)) <= sizeof(opt))
	unsigned optlen, hdrlen, packetlen;
	unsigned int sack_numblks;
	int idle, sendalot, txsegsize, rxsegsize;
	int txsegsize_nosack;
	int maxburst = TCP_MAXBURST;
	int af;		/* address family on the wire */
	int iphdrlen;
	int has_tso4, has_tso6;
	int has_tso, use_tso;
	bool alwaysfrag;
	int sack_rxmit;
	int sack_bytes_rxmt;
	int ecn_tos;
	struct sackhole *p;
#ifdef TCP_SIGNATURE
	int sigoff = 0;
#endif
	net_stat_ref_t tcps;

	so = tp->t_inpcb->inp_socket;
	ro = &tp->t_inpcb->inp_route;

	switch (af = tp->t_family) {
	case AF_INET:
	case AF_INET6:
		if (tp->t_inpcb)
			break;
		return EINVAL;
	default:
		return EAFNOSUPPORT;
	}

	if (tcp_segsize(tp, &txsegsize, &rxsegsize, &alwaysfrag))
		return EMSGSIZE;

	idle = (tp->snd_max == tp->snd_una);

	/*
	 * Determine if we can use TCP segmentation offload:
	 * - If we're using IPv4
	 * - If there is not an IPsec policy that prevents it
	 * - If the interface can do it
	 */
	has_tso4 = has_tso6 = false;

	has_tso4 = tp->t_inpcb->inp_af == AF_INET &&
#if defined(IPSEC)
	    (!ipsec_used || ipsec_pcb_skip_ipsec(tp->t_inpcb->inp_sp,
	    IPSEC_DIR_OUTBOUND)) &&
#endif
	    (rt = rtcache_validate(&tp->t_inpcb->inp_route)) != NULL &&
	    (rt->rt_ifp->if_capenable & IFCAP_TSOv4) != 0;
	if (rt != NULL) {
		rtcache_unref(rt, &tp->t_inpcb->inp_route);
		rt = NULL;
	}

#if defined(INET6)
	has_tso6 = tp->t_inpcb->inp_af == AF_INET6 &&
#if defined(IPSEC)
	    (!ipsec_used || ipsec_pcb_skip_ipsec(tp->t_inpcb->inp_sp,
	    IPSEC_DIR_OUTBOUND)) &&
#endif
	    (rt = rtcache_validate(&tp->t_inpcb->inp_route)) != NULL &&
	    (rt->rt_ifp->if_capenable & IFCAP_TSOv6) != 0;
	if (rt != NULL)
		rtcache_unref(rt, &tp->t_inpcb->inp_route);
#endif /* defined(INET6) */
	has_tso = (has_tso4 || has_tso6) && !alwaysfrag;

	/*
	 * Restart Window computation.  From draft-floyd-incr-init-win-03:
	 *
	 *	Optionally, a TCP MAY set the restart window to the
	 *	minimum of the value used for the initial window and
	 *	the current value of cwnd (in other words, using a
	 *	larger value for the restart window should never increase
	 *	the size of cwnd).
	 */
	if (tcp_cwm) {
		/*
		 * Hughes/Touch/Heidemann Congestion Window Monitoring.
		 * Count the number of packets currently pending
		 * acknowledgement, and limit our congestion window
		 * to a pre-determined allowed burst size plus that count.
		 * This prevents bursting once all pending packets have
		 * been acknowledged (i.e. transmission is idle).
		 *
		 * XXX Link this to Initial Window?
		 */
		tp->snd_cwnd = uimin(tp->snd_cwnd,
		    (tcp_cwm_burstsize * txsegsize) +
		    (tp->snd_nxt - tp->snd_una));
	} else {
		if (idle && (tcp_now - tp->t_rcvtime) >= tp->t_rxtcur) {
			/*
			 * We have been idle for "a while" and no acks are
			 * expected to clock out any data we send --
			 * slow start to get ack "clock" running again.
			 */
			int ss = tcp_init_win;
			if (tp->t_inpcb->inp_af == AF_INET &&
			    in_localaddr(in4p_faddr(tp->t_inpcb)))
				ss = tcp_init_win_local;
#ifdef INET6
			else if (tp->t_inpcb->inp_af == AF_INET6 &&
			    in6_localaddr(&in6p_faddr(tp->t_inpcb)))
				ss = tcp_init_win_local;
#endif
			tp->snd_cwnd = uimin(tp->snd_cwnd,
			    TCP_INITIAL_WINDOW(ss, txsegsize));
		}
	}

	txsegsize_nosack = txsegsize;
again:
	ecn_tos = 0;
	use_tso = has_tso;
	if ((tp->t_flags & (TF_ECN_SND_CWR|TF_ECN_SND_ECE)) != 0) {
		/* don't duplicate CWR/ECE. */
		use_tso = 0;
	}
	TCP_REASS_LOCK(tp);
	sack_numblks = tcp_sack_numblks(tp);
	if (sack_numblks) {
		int sackoptlen;

		sackoptlen = TCP_SACK_OPTLEN(sack_numblks);
		if (sackoptlen > txsegsize_nosack) {
			sack_numblks = 0; /* give up SACK */
			txsegsize = txsegsize_nosack;
		} else {
			if ((tp->rcv_sack_flags & TCPSACK_HAVED) != 0) {
				/* don't duplicate D-SACK. */
				use_tso = 0;
			}
			txsegsize = txsegsize_nosack - sackoptlen;
		}
	} else {
		txsegsize = txsegsize_nosack;
	}

	/*
	 * Determine length of data that should be transmitted, and
	 * flags that should be used.  If there is some data or critical
	 * controls (SYN, RST) to send, then transmit; otherwise,
	 * investigate further.
	 *
	 * Readjust SACK information to avoid resending duplicate data.
	 */
	if (TCP_SACK_ENABLED(tp) && SEQ_LT(tp->snd_nxt, tp->snd_max))
		tcp_sack_adjust(tp);
	sendalot = 0;
	off = tp->snd_nxt - tp->snd_una;
	win = uimin(tp->snd_wnd, tp->snd_cwnd);

	flags = tcp_outflags[tp->t_state];

	/*
	 * Send any SACK-generated retransmissions.  If we're explicitly trying
	 * to send out new data (when sendalot is 1), bypass this function.
	 * If we retransmit in fast recovery mode, decrement snd_cwnd, since
	 * we're replacing a (future) new transmission with a retransmission
	 * now, and we previously incremented snd_cwnd in tcp_input().
	 */
	/*
	 * Still in sack recovery, reset rxmit flag to zero.
	 */
	sack_rxmit = 0;
	sack_bytes_rxmt = 0;
	len = 0;
	p = NULL;
	do {
		long cwin;
		if (!TCP_SACK_ENABLED(tp))
			break;
		if (tp->t_partialacks < 0)
			break;
		p = tcp_sack_output(tp, &sack_bytes_rxmt);
		if (p == NULL)
			break;

		cwin = uimin(tp->snd_wnd, tp->snd_cwnd) - sack_bytes_rxmt;
		if (cwin < 0)
			cwin = 0;
		/* Do not retransmit SACK segments beyond snd_recover */
		if (SEQ_GT(p->end, tp->snd_recover)) {
			/*
			 * (At least) part of sack hole extends beyond
			 * snd_recover. Check to see if we can rexmit data
			 * for this hole.
			 */
			if (SEQ_GEQ(p->rxmit, tp->snd_recover)) {
				/*
				 * Can't rexmit any more data for this hole.
				 * That data will be rexmitted in the next
				 * sack recovery episode, when snd_recover
				 * moves past p->rxmit.
				 */
				p = NULL;
				break;
			}
			/* Can rexmit part of the current hole */
			len = ((long)ulmin(cwin, tp->snd_recover - p->rxmit));
		} else
			len = ((long)ulmin(cwin, p->end - p->rxmit));
		off = p->rxmit - tp->snd_una;
		if (off + len > so->so_snd.sb_cc) {
			/* 1 for TH_FIN */
			KASSERT(off + len == so->so_snd.sb_cc + 1);
			KASSERT(p->rxmit + len == tp->snd_max);
			len = so->so_snd.sb_cc - off;
		}
		if (len > 0) {
			sack_rxmit = 1;
			sendalot = 1;
		}
	} while (/*CONSTCOND*/0);

	/*
	 * If in persist timeout with window of 0, send 1 byte.
	 * Otherwise, if window is small but nonzero
	 * and timer expired, we will send what we can
	 * and go to transmit state.
	 */
	if (tp->t_force) {
		if (win == 0) {
			/*
			 * If we still have some data to send, then
			 * clear the FIN bit.  Usually this would
			 * happen below when it realizes that we
			 * aren't sending all the data.  However,
			 * if we have exactly 1 byte of unset data,
			 * then it won't clear the FIN bit below,
			 * and if we are in persist state, we wind
			 * up sending the packet without recording
			 * that we sent the FIN bit.
			 *
			 * We can't just blindly clear the FIN bit,
			 * because if we don't have any more data
			 * to send then the probe will be the FIN
			 * itself.
			 */
			if (off < so->so_snd.sb_cc)
				flags &= ~TH_FIN;
			win = 1;
		} else {
			TCP_TIMER_DISARM(tp, TCPT_PERSIST);
			tp->t_rxtshift = 0;
		}
	}

	if (sack_rxmit == 0) {
		if (TCP_SACK_ENABLED(tp) && tp->t_partialacks >= 0) {
			long cwin;

			/*
			 * We are inside of a SACK recovery episode and are
			 * sending new data, having retransmitted all the
			 * data possible in the scoreboard.
			 */
			if (tp->snd_wnd < so->so_snd.sb_cc) {
				len = tp->snd_wnd - off;
				flags &= ~TH_FIN;
			} else {
				len = so->so_snd.sb_cc - off;
			}

			/*
			 * From FreeBSD:
			 *  Don't remove this (len > 0) check !
			 *  We explicitly check for len > 0 here (although it
			 *  isn't really necessary), to work around a gcc
			 *  optimization issue - to force gcc to compute
			 *  len above. Without this check, the computation
			 *  of len is bungled by the optimizer.
			 */
			if (len > 0) {
				cwin = tp->snd_cwnd -
				    (tp->snd_nxt - tp->sack_newdata) -
				    sack_bytes_rxmt;
				if (cwin < 0)
					cwin = 0;
				if (cwin < len) {
					len = cwin;
					flags &= ~TH_FIN;
				}
			}
		} else if (win < so->so_snd.sb_cc) {
			len = win - off;
			flags &= ~TH_FIN;
		} else {
			len = so->so_snd.sb_cc - off;
		}
	}

	if (len < 0) {
		/*
		 * If FIN has been sent but not acked,
		 * but we haven't been called to retransmit,
		 * len will be -1.  Otherwise, window shrank
		 * after we sent into it.  If window shrank to 0,
		 * cancel pending retransmit, pull snd_nxt back
		 * to (closed) window, and set the persist timer
		 * if it isn't already going.  If the window didn't
		 * close completely, just wait for an ACK.
		 *
		 * If we have a pending FIN, either it has already been
		 * transmitted or it is outside the window, so drop it.
		 * If the FIN has been transmitted, but this is not a
		 * retransmission, then len must be -1.  Therefore we also
		 * prevent here the sending of `gratuitous FINs'.  This
		 * eliminates the need to check for that case below (e.g.
		 * to back up snd_nxt before the FIN so that the sequence
		 * number is correct).
		 */
		len = 0;
		flags &= ~TH_FIN;
		if (win == 0) {
			TCP_TIMER_DISARM(tp, TCPT_REXMT);
			tp->t_rxtshift = 0;
			tp->snd_nxt = tp->snd_una;
			if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0)
				tcp_setpersist(tp);
		}
	}

	/*
	 * Automatic sizing enables the performance of large buffers
	 * and most of the efficiency of small ones by only allocating
	 * space when it is needed.
	 *
	 * The criteria to step up the send buffer one notch are:
	 *  1. receive window of remote host is larger than send buffer
	 *     (with a fudge factor of 5/4th);
	 *  2. send buffer is filled to 7/8th with data (so we actually
	 *     have data to make use of it);
	 *  3. send buffer fill has not hit maximal automatic size;
	 *  4. our send window (slow start and cogestion controlled) is
	 *     larger than sent but unacknowledged data in send buffer.
	 *
	 * The remote host receive window scaling factor may limit the
	 * growing of the send buffer before it reaches its allowed
	 * maximum.
	 *
	 * It scales directly with slow start or congestion window
	 * and does at most one step per received ACK.  This fast
	 * scaling has the drawback of growing the send buffer beyond
	 * what is strictly necessary to make full use of a given
	 * delay*bandwidth product.  However testing has shown this not
	 * to be much of a problem.  At worst we are trading wasting
	 * of available bandwidth (the non-use of it) for wasting some
	 * socket buffer memory.
	 *
	 * TODO: Shrink send buffer during idle periods together
	 * with congestion window.  Requires another timer.
	 */
	if (tcp_do_autosndbuf && so->so_snd.sb_flags & SB_AUTOSIZE) {
		if ((tp->snd_wnd / 4 * 5) >= so->so_snd.sb_hiwat &&
		    so->so_snd.sb_cc >= (so->so_snd.sb_hiwat / 8 * 7) &&
		    so->so_snd.sb_cc < tcp_autosndbuf_max &&
		    win >= (so->so_snd.sb_cc - (tp->snd_nxt - tp->snd_una))) {
			if (!sbreserve(&so->so_snd,
			    uimin(so->so_snd.sb_hiwat + tcp_autosndbuf_inc,
			     tcp_autosndbuf_max), so))
				so->so_snd.sb_flags &= ~SB_AUTOSIZE;
		}
	}

	if (len > txsegsize) {
		if (use_tso) {
			/*
			 * Truncate TSO transfers to IP_MAXPACKET, and make
			 * sure that we send equal size transfers down the
			 * stack (rather than big-small-big-small-...).
			 */
#ifdef INET6
			CTASSERT(IPV6_MAXPACKET == IP_MAXPACKET);
#endif
			len = (uimin(len, IP_MAXPACKET) / txsegsize) * txsegsize;
			if (len <= txsegsize) {
				use_tso = 0;
			}
		} else
			len = txsegsize;
		flags &= ~TH_FIN;
		sendalot = 1;
	} else
		use_tso = 0;
	if (sack_rxmit) {
		if (SEQ_LT(p->rxmit + len, tp->snd_una + so->so_snd.sb_cc))
			flags &= ~TH_FIN;
	}

	win = sbspace(&so->so_rcv);

	/*
	 * Sender silly window avoidance.  If connection is idle
	 * and can send all data, a maximum segment,
	 * at least a maximum default-size segment do it,
	 * or are forced, do it; otherwise don't bother.
	 * If peer's buffer is tiny, then send
	 * when window is at least half open.
	 * If retransmitting (possibly after persist timer forced us
	 * to send into a small window), then must resend.
	 */
	if (len) {
		if (len >= txsegsize)
			goto send;
		if ((so->so_state & SS_MORETOCOME) == 0 &&
		    ((idle || tp->t_flags & TF_NODELAY) &&
		     len + off >= so->so_snd.sb_cc))
			goto send;
		if (tp->t_force)
			goto send;
		if (len >= tp->max_sndwnd / 2)
			goto send;
		if (SEQ_LT(tp->snd_nxt, tp->snd_max))
			goto send;
		if (sack_rxmit)
			goto send;
	}

	/*
	 * Compare available window to amount of window known to peer
	 * (as advertised window less next expected input).  If the
	 * difference is at least twice the size of the largest segment
	 * we expect to receive (i.e. two segments) or at least 50% of
	 * the maximum possible window, then want to send a window update
	 * to peer.
	 */
	if (win > 0) {
		/*
		 * "adv" is the amount we can increase the window,
		 * taking into account that we are limited by
		 * TCP_MAXWIN << tp->rcv_scale.
		 */
		long recwin = uimin(win, (long)TCP_MAXWIN << tp->rcv_scale);
		long oldwin, adv;

		/*
		 * rcv_nxt may overtake rcv_adv when we accept a
		 * zero-window probe.
		 */
		if (SEQ_GT(tp->rcv_adv, tp->rcv_nxt))
			oldwin = tp->rcv_adv - tp->rcv_nxt;
		else
			oldwin = 0;

		/*
		 * If the new window size ends up being the same as or
		 * less than the old size when it is scaled, then
		 * don't force a window update.
		 */
		if (recwin >> tp->rcv_scale <= oldwin >> tp->rcv_scale)
			goto dontupdate;

		adv = recwin - oldwin;
		if (adv >= (long) (2 * rxsegsize))
			goto send;
		if (2 * adv >= (long) so->so_rcv.sb_hiwat)
			goto send;
	}
dontupdate:

	/*
	 * Send if we owe peer an ACK.
	 */
	if (tp->t_flags & TF_ACKNOW)
		goto send;
	if (flags & (TH_SYN|TH_FIN|TH_RST))
		goto send;
	if (SEQ_GT(tp->snd_up, tp->snd_una))
		goto send;
	/*
	 * In SACK, it is possible for tcp_output to fail to send a segment
	 * after the retransmission timer has been turned off.  Make sure
	 * that the retransmission timer is set.
	 */
	if (TCP_SACK_ENABLED(tp) && SEQ_GT(tp->snd_max, tp->snd_una) &&
	    !TCP_TIMER_ISARMED(tp, TCPT_REXMT) &&
	    !TCP_TIMER_ISARMED(tp, TCPT_PERSIST)) {
		TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
		goto just_return;
	}

	/*
	 * TCP window updates are not reliable, rather a polling protocol
	 * using ``persist'' packets is used to insure receipt of window
	 * updates.  The three ``states'' for the output side are:
	 *	idle			not doing retransmits or persists
	 *	persisting		to move a small or zero window
	 *	(re)transmitting	and thereby not persisting
	 *
	 * tp->t_timer[TCPT_PERSIST]
	 *	is set when we are in persist state.
	 * tp->t_force
	 *	is set when we are called to send a persist packet.
	 * tp->t_timer[TCPT_REXMT]
	 *	is set when we are retransmitting
	 * The output side is idle when both timers are zero.
	 *
	 * If send window is too small, there is data to transmit, and no
	 * retransmit or persist is pending, then go to persist state.
	 * If nothing happens soon, send when timer expires:
	 * if window is nonzero, transmit what we can,
	 * otherwise force out a byte.
	 */
	if (so->so_snd.sb_cc && TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0 &&
	    TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
		tp->t_rxtshift = 0;
		tcp_setpersist(tp);
	}

	/*
	 * No reason to send a segment, just return.
	 */
just_return:
	TCP_REASS_UNLOCK(tp);
	return 0;

send:
	/*
	 * Before ESTABLISHED, force sending of initial options unless TCP set
	 * not to do any options.
	 *
	 * Note: we assume that the IP/TCP header plus TCP options always fit
	 * in a single mbuf, leaving room for a maximum link header, i.e.:
	 *     max_linkhdr + IP_header + TCP_header + optlen <= MCLBYTES
	 */
	optlen = 0;
	optp = opt;
	switch (af) {
	case AF_INET:
		iphdrlen = sizeof(struct ip) + sizeof(struct tcphdr);
		break;
#ifdef INET6
	case AF_INET6:
		iphdrlen = sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
		break;
#endif
	default:	/*pacify gcc*/
		iphdrlen = 0;
		break;
	}
	hdrlen = iphdrlen;
	if (flags & TH_SYN) {
		struct rtentry *synrt;

		synrt = inpcb_rtentry(tp->t_inpcb);
		tp->snd_nxt = tp->iss;
		tp->t_ourmss = tcp_mss_to_advertise(synrt != NULL ?
						    synrt->rt_ifp : NULL, af);
		inpcb_rtentry_unref(synrt, tp->t_inpcb);
		if ((tp->t_flags & TF_NOOPT) == 0 && OPT_FITS(TCPOLEN_MAXSEG)) {
			*optp++ = TCPOPT_MAXSEG;
			*optp++ = TCPOLEN_MAXSEG;
			*optp++ = (tp->t_ourmss >> 8) & 0xff;
			*optp++ = tp->t_ourmss & 0xff;
			optlen += TCPOLEN_MAXSEG;

			if ((tp->t_flags & TF_REQ_SCALE) &&
			    ((flags & TH_ACK) == 0 ||
			    (tp->t_flags & TF_RCVD_SCALE)) &&
			    OPT_FITS(TCPOLEN_WINDOW + TCPOLEN_NOP)) {
				*((uint32_t *)optp) = htonl(
					TCPOPT_NOP << 24 |
					TCPOPT_WINDOW << 16 |
					TCPOLEN_WINDOW << 8 |
					tp->request_r_scale);
				optp += TCPOLEN_WINDOW + TCPOLEN_NOP;
				optlen += TCPOLEN_WINDOW + TCPOLEN_NOP;
			}
			if (tcp_do_sack && OPT_FITS(TCPOLEN_SACK_PERMITTED)) {
				*optp++ = TCPOPT_SACK_PERMITTED;
				*optp++ = TCPOLEN_SACK_PERMITTED;
				optlen += TCPOLEN_SACK_PERMITTED;
			}
		}
	}

	/*
	 * Send a timestamp and echo-reply if this is a SYN and our side
	 * wants to use timestamps (TF_REQ_TSTMP is set) or both our side
	 * and our peer have sent timestamps in our SYN's.
	 */
	if ((tp->t_flags & (TF_REQ_TSTMP|TF_NOOPT)) == TF_REQ_TSTMP &&
	     (flags & TH_RST) == 0 &&
	    ((flags & (TH_SYN|TH_ACK)) == TH_SYN ||
	     (tp->t_flags & TF_RCVD_TSTMP))) {
		int alen = 0;
		while (optlen % 4 != 2) {
			optlen += TCPOLEN_NOP;
			*optp++ = TCPOPT_NOP;
			alen++;
		}
		if (OPT_FITS(TCPOLEN_TIMESTAMP)) {
			*optp++ = TCPOPT_TIMESTAMP;
			*optp++ = TCPOLEN_TIMESTAMP;
			uint32_t *lp = (uint32_t *)optp;
			/* Form timestamp option (appendix A of RFC 1323) */
			*lp++ = htonl(TCP_TIMESTAMP(tp));
			*lp   = htonl(tp->ts_recent);
			optp += TCPOLEN_TIMESTAMP - 2;
			optlen += TCPOLEN_TIMESTAMP;

			/* Set receive buffer autosizing timestamp. */
			if (tp->rfbuf_ts == 0 &&
			    (so->so_rcv.sb_flags & SB_AUTOSIZE))
				tp->rfbuf_ts = TCP_TIMESTAMP(tp);
		} else {
			optp -= alen;
			optlen -= alen;
		}
	}

#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE) {
		/*
		 * Initialize TCP-MD5 option (RFC2385)
		 */
		if (!OPT_FITS(TCPOLEN_SIGNATURE))
			goto reset;

		*optp++ = TCPOPT_SIGNATURE;
		*optp++ = TCPOLEN_SIGNATURE;
		sigoff = optlen + 2;
		memset(optp, 0, TCP_SIGLEN);
		optlen += TCPOLEN_SIGNATURE;
		optp += TCP_SIGLEN;
	}
#endif

	/*
	 * Tack on the SACK block if it is necessary.
	 */
	if (sack_numblks) {
		int alen = 0;
		int sack_len = sack_numblks * 8;
		while (optlen % 4 != 2) {
			optlen += TCPOLEN_NOP;
			*optp++ = TCPOPT_NOP;
			alen++;
		}
		if (OPT_FITS(sack_len + 2)) {
			struct ipqent *tiqe;
			*optp++ = TCPOPT_SACK;
			*optp++ = sack_len + 2;
			uint32_t *lp = (uint32_t *)optp;
			if ((tp->rcv_sack_flags & TCPSACK_HAVED) != 0) {
				sack_numblks--;
				*lp++ = htonl(tp->rcv_dsack_block.left);
				*lp++ = htonl(tp->rcv_dsack_block.right);
				tp->rcv_sack_flags &= ~TCPSACK_HAVED;
			}
			for (tiqe = TAILQ_FIRST(&tp->timeq);
			    sack_numblks > 0;
			    tiqe = TAILQ_NEXT(tiqe, ipqe_timeq)) {
				KASSERT(tiqe != NULL);
				sack_numblks--;
				*lp++ = htonl(tiqe->ipqe_seq);
				*lp++ = htonl(tiqe->ipqe_seq + tiqe->ipqe_len +
				    ((tiqe->ipqe_flags & TH_FIN) != 0 ? 1 : 0));
			}
			optlen += sack_len + 2;
			optp += sack_len;
		} else {
			optp -= alen;
			optlen -= alen;
		}
	}

	/* Terminate and pad TCP options to a 4 byte boundary. */
	if (optlen % 4) {
		if (!OPT_FITS(TCPOLEN_EOL)) {
reset:			TCP_REASS_UNLOCK(tp);
			error = ECONNABORTED;
			goto out;
		}
		optlen += TCPOLEN_EOL;
		*optp++ = TCPOPT_EOL;
	}
	/*
	 * According to RFC 793 (STD0007):
	 *   "The content of the header beyond the End-of-Option option
	 *    must be header padding (i.e., zero)."
	 *   and later: "The padding is composed of zeros."
	 */
	while (optlen % 4) {
		if (!OPT_FITS(TCPOLEN_PAD))
			goto reset;
		optlen += TCPOLEN_PAD;
		*optp++ = TCPOPT_PAD;
	}

	TCP_REASS_UNLOCK(tp);

	hdrlen += optlen;

#ifdef DIAGNOSTIC
	if (!use_tso && len > txsegsize)
		panic("tcp data to be sent is larger than segment");
	else if (use_tso && len > IP_MAXPACKET)
		panic("tcp data to be sent is larger than max TSO size");
	if (max_linkhdr + hdrlen > MCLBYTES)
		panic("tcphdr too big");
#endif

	/*
	 * Grab a header mbuf, attaching a copy of data to
	 * be transmitted, and initialize the header from
	 * the template for sends on this connection.
	 */
	if (len) {
		error = tcp_build_datapkt(tp, so, off, len, hdrlen, &m);
		if (error)
			goto out;
		/*
		 * If we're sending everything we've got, set PUSH.
		 * (This will keep happy those implementations which only
		 * give data to the user when a buffer fills or
		 * a PUSH comes in.)
		 */
		if (off + len == so->so_snd.sb_cc)
			flags |= TH_PUSH;
	} else {
		tcps = TCP_STAT_GETREF();
		if (tp->t_flags & TF_ACKNOW)
			_NET_STATINC_REF(tcps, TCP_STAT_SNDACKS);
		else if (flags & (TH_SYN|TH_FIN|TH_RST))
			_NET_STATINC_REF(tcps, TCP_STAT_SNDCTRL);
		else if (SEQ_GT(tp->snd_up, tp->snd_una))
			_NET_STATINC_REF(tcps, TCP_STAT_SNDURG);
		else
			_NET_STATINC_REF(tcps, TCP_STAT_SNDWINUP);
		TCP_STAT_PUTREF();

		MGETHDR(m, M_DONTWAIT, MT_HEADER);
		if (m != NULL && max_linkhdr + hdrlen > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				m = NULL;
			}
		}
		if (m == NULL) {
			error = ENOBUFS;
			goto out;
		}
		MCLAIM(m, &tcp_tx_mowner);
		m->m_data += max_linkhdr;
		m->m_len = hdrlen;
	}
	m_reset_rcvif(m);
	switch (af) {
	case AF_INET:
		ip = mtod(m, struct ip *);
#ifdef INET6
		ip6 = NULL;
#endif
		th = (struct tcphdr *)(ip + 1);
		break;
#ifdef INET6
	case AF_INET6:
		ip = NULL;
		ip6 = mtod(m, struct ip6_hdr *);
		th = (struct tcphdr *)(ip6 + 1);
		break;
#endif
	default:	/*pacify gcc*/
		ip = NULL;
#ifdef INET6
		ip6 = NULL;
#endif
		th = NULL;
		break;
	}
	if (tp->t_template == NULL)
		panic("%s: no template", __func__);
	if (tp->t_template->m_len < iphdrlen)
		panic("%s: %d < %d", __func__, tp->t_template->m_len, iphdrlen);
	bcopy(mtod(tp->t_template, void *), mtod(m, void *), iphdrlen);

	/*
	 * If we are starting a connection, send ECN setup
	 * SYN packet. If we are on a retransmit, we may
	 * resend those bits a number of times as per
	 * RFC 3168.
	 */
	if (tp->t_state == TCPS_SYN_SENT && tcp_do_ecn) {
		if (tp->t_flags & TF_SYN_REXMT) {
			if (tp->t_ecn_retries--)
				flags |= TH_ECE|TH_CWR;
		} else {
			flags |= TH_ECE|TH_CWR;
			tp->t_ecn_retries = tcp_ecn_maxretries;
		}
	}

	if (TCP_ECN_ALLOWED(tp)) {
		/*
		 * If the peer has ECN, mark data packets
		 * ECN capable. Ignore pure ack packets, retransmissions
		 * and window probes.
		 */
		if (len > 0 && SEQ_GEQ(tp->snd_nxt, tp->snd_max) &&
		    !(tp->t_force && len == 1)) {
			ecn_tos = IPTOS_ECN_ECT0;
			TCP_STATINC(TCP_STAT_ECN_ECT);
		}

		/*
		 * Reply with proper ECN notifications.
		 */
		if (tp->t_flags & TF_ECN_SND_CWR) {
			flags |= TH_CWR;
			tp->t_flags &= ~TF_ECN_SND_CWR;
		}
		if (tp->t_flags & TF_ECN_SND_ECE) {
			flags |= TH_ECE;
		}
	}

	/*
	 * If we are doing retransmissions, then snd_nxt will
	 * not reflect the first unsent octet.  For ACK only
	 * packets, we do not want the sequence number of the
	 * retransmitted packet, we want the sequence number
	 * of the next unsent octet.  So, if there is no data
	 * (and no SYN or FIN), use snd_max instead of snd_nxt
	 * when filling in ti_seq.  But if we are in persist
	 * state, snd_max might reflect one byte beyond the
	 * right edge of the window, so use snd_nxt in that
	 * case, since we know we aren't doing a retransmission.
	 * (retransmit and persist are mutually exclusive...)
	 */
	if (TCP_SACK_ENABLED(tp) && sack_rxmit) {
		th->th_seq = htonl(p->rxmit);
		p->rxmit += len;
	} else {
		if (len || (flags & (TH_SYN|TH_FIN)) ||
		    TCP_TIMER_ISARMED(tp, TCPT_PERSIST))
			th->th_seq = htonl(tp->snd_nxt);
		else
			th->th_seq = htonl(tp->snd_max);
	}
	th->th_ack = htonl(tp->rcv_nxt);
	if (optlen) {
		memcpy(th + 1, opt, optlen);
		th->th_off = (sizeof (struct tcphdr) + optlen) >> 2;
	}
	th->th_flags = flags;
	/*
	 * Calculate receive window.  Don't shrink window,
	 * but avoid silly window syndrome.
	 */
	if (win < (long)(so->so_rcv.sb_hiwat / 4) && win < (long)rxsegsize)
		win = 0;
	if (win > (long)TCP_MAXWIN << tp->rcv_scale)
		win = (long)TCP_MAXWIN << tp->rcv_scale;
	if (win < (long)(int32_t)(tp->rcv_adv - tp->rcv_nxt))
		win = (long)(int32_t)(tp->rcv_adv - tp->rcv_nxt);
	th->th_win = htons((u_int16_t) (win>>tp->rcv_scale));
	if (th->th_win == 0) {
		tp->t_sndzerowin++;
	}
	if (SEQ_GT(tp->snd_up, tp->snd_nxt)) {
		u_int32_t urp = tp->snd_up - tp->snd_nxt;
		if (urp > IP_MAXPACKET)
			urp = IP_MAXPACKET;
		th->th_urp = htons((u_int16_t)urp);
		th->th_flags |= TH_URG;
	} else
		/*
		 * If no urgent pointer to send, then we pull
		 * the urgent pointer to the left edge of the send window
		 * so that it doesn't drift into the send window on sequence
		 * number wraparound.
		 */
		tp->snd_up = tp->snd_una;		/* drag it along */

#ifdef TCP_SIGNATURE
	if (sigoff && (tp->t_flags & TF_SIGNATURE)) {
		struct secasvar *sav;
		u_int8_t *sigp;

		sav = tcp_signature_getsav(m);
		if (sav == NULL) {
			m_freem(m);
			return EPERM;
		}

		m->m_pkthdr.len = hdrlen + len;
		sigp = (char *)th + sizeof(*th) + sigoff;
		tcp_signature(m, th, (char *)th - mtod(m, char *), sav, sigp);

		key_sa_recordxfer(sav, m);
		KEY_SA_UNREF(&sav);
	}
#endif

	/*
	 * Set ourselves up to be checksummed just before the packet
	 * hits the wire.
	 */
	switch (af) {
	case AF_INET:
		m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
		if (use_tso) {
			m->m_pkthdr.segsz = txsegsize;
			m->m_pkthdr.csum_flags = M_CSUM_TSOv4;
		} else {
			m->m_pkthdr.csum_flags = M_CSUM_TCPv4;
			if (len + optlen) {
				/* Fixup the pseudo-header checksum. */
				/* XXXJRT Not IP Jumbogram safe. */
				th->th_sum = in_cksum_addword(th->th_sum,
				    htons((u_int16_t) (len + optlen)));
			}
		}
		break;
#ifdef INET6
	case AF_INET6:
		m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
		if (use_tso) {
			m->m_pkthdr.segsz = txsegsize;
			m->m_pkthdr.csum_flags = M_CSUM_TSOv6;
		} else {
			m->m_pkthdr.csum_flags = M_CSUM_TCPv6;
			if (len + optlen) {
				/* Fixup the pseudo-header checksum. */
				/* XXXJRT: Not IPv6 Jumbogram safe. */
				th->th_sum = in_cksum_addword(th->th_sum,
				    htons((u_int16_t) (len + optlen)));
			}
		}
		break;
#endif
	}

	/*
	 * In transmit state, time the transmission and arrange for
	 * the retransmit.  In persist state, just set snd_max.
	 */
	if (tp->t_force == 0 || TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
		tcp_seq startseq = tp->snd_nxt;

		/*
		 * Advance snd_nxt over sequence space of this segment.
		 * There are no states in which we send both a SYN and a FIN,
		 * so we collapse the tests for these flags.
		 */
		if (flags & (TH_SYN|TH_FIN))
			tp->snd_nxt++;
		if (sack_rxmit)
			goto timer;
		tp->snd_nxt += len;
		if (SEQ_GT(tp->snd_nxt, tp->snd_max)) {
			tp->snd_max = tp->snd_nxt;
			/*
			 * Time this transmission if not a retransmission and
			 * not currently timing anything.
			 */
			if (tp->t_rtttime == 0) {
				tp->t_rtttime = tcp_now;
				tp->t_rtseq = startseq;
				TCP_STATINC(TCP_STAT_SEGSTIMED);
			}
		}

		/*
		 * Set retransmit timer if not currently set,
		 * and not doing an ack or a keep-alive probe.
		 * Initial value for retransmit timer is smoothed
		 * round-trip time + 2 * round-trip time variance.
		 * Initialize shift counter which is used for backoff
		 * of retransmit time.
		 */
timer:
		if (TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0) {
			if ((sack_rxmit && tp->snd_nxt != tp->snd_max)
			    || tp->snd_nxt != tp->snd_una) {
				if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST)) {
					TCP_TIMER_DISARM(tp, TCPT_PERSIST);
					tp->t_rxtshift = 0;
				}
				TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
			} else if (len == 0 && so->so_snd.sb_cc > 0
			    && TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
				/*
				 * If we are sending a window probe and there's
				 * unacked data in the socket, make sure at
				 * least the persist timer is running.
				 */
				tp->t_rxtshift = 0;
				tcp_setpersist(tp);
			}
		}
	} else
		if (SEQ_GT(tp->snd_nxt + len, tp->snd_max))
			tp->snd_max = tp->snd_nxt + len;

#ifdef TCP_DEBUG
	/*
	 * Trace.
	 */
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_OUTPUT, tp->t_state, tp, m, 0);
#endif

	/*
	 * Fill in IP length and desired time to live and
	 * send to IP level.  There should be a better way
	 * to handle ttl and tos; we could keep them in
	 * the template, but need a way to checksum without them.
	 */
	m->m_pkthdr.len = hdrlen + len;

	switch (af) {
	case AF_INET:
		ip->ip_len = htons(m->m_pkthdr.len);
		packetlen = m->m_pkthdr.len;
		if (tp->t_inpcb->inp_af == AF_INET) {
			ip->ip_ttl = in4p_ip(tp->t_inpcb).ip_ttl;
			ip->ip_tos = in4p_ip(tp->t_inpcb).ip_tos | ecn_tos;
		}
#ifdef INET6
		else if (tp->t_inpcb->inp_af == AF_INET6) {
			ip->ip_ttl = in6pcb_selecthlim(tp->t_inpcb, NULL); /*XXX*/
			ip->ip_tos = ecn_tos;	/*XXX*/
		}
#endif
		break;
#ifdef INET6
	case AF_INET6:
		packetlen = m->m_pkthdr.len;
		ip6->ip6_nxt = IPPROTO_TCP;
		if (tp->t_family == AF_INET6) {
			/*
			 * we separately set hoplimit for every segment, since
			 * the user might want to change the value via
			 * setsockopt. Also, desired default hop limit might
			 * be changed via Neighbor Discovery.
			 */
			ip6->ip6_hlim = in6pcb_selecthlim_rt(tp->t_inpcb);
		}
		ip6->ip6_flow |= htonl(ecn_tos << 20);
		/* ip6->ip6_flow = ??? (from template) */
		/* ip6_plen will be filled in ip6_output(). */
		break;
#endif
	default:	/*pacify gcc*/
		packetlen = 0;
		break;
	}

	switch (af) {
	case AF_INET:
	    {
		struct mbuf *opts;

		if (tp->t_inpcb->inp_af == AF_INET)
			opts = tp->t_inpcb->inp_options;
		else
			opts = NULL;
		error = ip_output(m, opts, ro,
			(tp->t_mtudisc ? IP_MTUDISC : 0) |
			(so->so_options & SO_DONTROUTE), NULL, tp->t_inpcb);
		break;
	    }
#ifdef INET6
	case AF_INET6:
	    {
		struct ip6_pktopts *opts;

		if (tp->t_inpcb->inp_af == AF_INET6)
			opts = in6p_outputopts(tp->t_inpcb);
		else
			opts = NULL;
		error = ip6_output(m, opts, ro, so->so_options & SO_DONTROUTE,
			NULL, tp->t_inpcb, NULL);
		break;
	    }
#endif
	default:
		error = EAFNOSUPPORT;
		break;
	}
	if (error) {
out:
		if (error == ENOBUFS) {
			TCP_STATINC(TCP_STAT_SELFQUENCH);
			tcp_quench(tp->t_inpcb);
			error = 0;
		} else if ((error == EHOSTUNREACH || error == ENETDOWN ||
		    error == EHOSTDOWN) && TCPS_HAVERCVDSYN(tp->t_state)) {
			tp->t_softerror = error;
			error = 0;
		}

		/* Back out the sequence number advance. */
		if (sack_rxmit)
			p->rxmit -= len;

		/* Restart the delayed ACK timer, if necessary. */
		if (tp->t_flags & TF_DELACK)
			TCP_RESTART_DELACK(tp);

		return error;
	}

	if (packetlen > tp->t_pmtud_mtu_sent)
		tp->t_pmtud_mtu_sent = packetlen;

	tcps = TCP_STAT_GETREF();
	_NET_STATINC_REF(tcps, TCP_STAT_SNDTOTAL);
	if (tp->t_flags & TF_DELACK)
		_NET_STATINC_REF(tcps, TCP_STAT_DELACK);
	TCP_STAT_PUTREF();

	/*
	 * Data sent (as far as we can tell).
	 * If this advertises a larger window than any other segment,
	 * then remember the size of the advertised window.
	 * Any pending ACK has now been sent.
	 */
	if (win > 0 && SEQ_GT(tp->rcv_nxt+win, tp->rcv_adv))
		tp->rcv_adv = tp->rcv_nxt + win;
	tp->last_ack_sent = tp->rcv_nxt;
	tp->t_flags &= ~TF_ACKNOW;
	TCP_CLEAR_DELACK(tp);
#ifdef DIAGNOSTIC
	if (maxburst < 0)
		printf("tcp_output: maxburst exceeded by %d\n", -maxburst);
#endif
	if (sendalot && (tp->t_congctl == &tcp_reno_ctl || --maxburst))
		goto again;
	return 0;
}

void
tcp_setpersist(struct tcpcb *tp)
{
	int t = ((tp->t_srtt >> 2) + tp->t_rttvar) >> (1 + 2);
	int nticks;

	if (TCP_TIMER_ISARMED(tp, TCPT_REXMT))
		panic("tcp_output REXMT");
	/*
	 * Start/restart persistance timer.
	 */
	if (t < tp->t_rttmin)
		t = tp->t_rttmin;
	TCPT_RANGESET(nticks, t * tcp_backoff[tp->t_rxtshift],
	    TCPTV_PERSMIN, TCPTV_PERSMAX);
	TCP_TIMER_ARM(tp, TCPT_PERSIST, nticks);
	if (tp->t_rxtshift < TCP_MAXRXTSHIFT)
		tp->t_rxtshift++;
}
