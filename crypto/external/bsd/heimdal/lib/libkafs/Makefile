# $NetBSD: Makefile,v 1.3 2017/01/28 21:31:52 christos Exp $

USE_FORT?= yes	# network protocol library

NOLINT=		# defined

.include <bsd.own.mk>
.include <${.CURDIR}/../../Makefile.inc>

.PATH:		${HEIMDIST}/lib/kafs

LIB=		kafs

LIBDPLIBS+=	krb5	${.CURDIR}/../libkrb5 \
		asn1	${.CURDIR}/../libasn1 \
		roken	${.CURDIR}/../libroken

SRCS=		afssys.c afskrb5.c common.c rxkad_kdf.c

INCS=		kafs.h
INCSDIR=	/usr/include/krb5
MAN=		kafs.3

.include <${HEIMBASE}/Makefile.rules.inc>
.include <bsd.lib.mk>
