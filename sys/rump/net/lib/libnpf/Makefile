#	$NetBSD: Makefile,v 1.28 2025/06/01 00:24:19 joe Exp $
#
# Public Domain.
#

NOLINT=		# defined
S=${.CURDIR}/../../../../

.PATH:	${S}/net/npf ${S}/external/bsd/libnv/dist


LIB=	rumpnet_npf
COMMENT=NPF packet filter

IOCONF=	NPF.ioconf
SRCS=	npf.c npf_alg.c npf_conf.c npf_ctl.c npf_handler.c
SRCS+=	npf_bpf.c npf_if.c npf_inet.c npf_mbuf.c npf_nat.c
SRCS+=	npf_params.c npf_ruleset.c npf_rproc.c
SRCS+=	npf_conn.c npf_conndb.c npf_connkey.c npf_portmap.c
SRCS+=	npf_state.c npf_state_tcp.c npf_tableset.c npf_socket.c
SRCS+=	lpm.c npf_sendpkt.c npf_worker.c npf_os.c npf_ifaddr.c

SRCS+=	nvlist.c nvpair.c nv_kern_netbsd.c dnvlist.c

SRCS+=	if_npflog.c

SRCS+=	npf_alg_icmp.c

SRCS+=	npf_ext_log.c npf_ext_normalize.c npf_ext_rndblock.c

SRCS+=	npf_component.c

.include <bsd.own.mk>

CPPFLAGS+=	-I ${NETBSDSRCDIR}/sys/external/bsd/libnv/dist

.if ${RUMP_NPF_TESTING:Uyes} == "yes"
CPPFLAGS+=	-D_NPF_TESTING
.endif

.include <bsd.lib.mk>
.include <bsd.klinks.mk>
