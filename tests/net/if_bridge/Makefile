# $NetBSD: Makefile,v 1.4 2024/09/03 08:01:38 ozaki-r Exp $
#

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/net/if_bridge
FILESDIR=	${TESTSDIR}

.for name in bridge rtable
TESTS_SH+=		t_${name}
TESTS_SH_SRC_t_${name}=	../net_common.sh t_${name}.sh
.endfor

FILES+=		broadcast.pcap.uue unicast.pcap.uue

.include <bsd.test.mk>
