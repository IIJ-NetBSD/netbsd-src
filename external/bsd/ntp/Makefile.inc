#	$NetBSD: Makefile.inc,v 1.25 2025/07/19 14:59:04 christos Exp $

.if !defined(NTP_MAKEFILE_INC)
NTP_MAKEFILE_INC=yes

USE_FORT?= yes	# network client/server

CWARNFLAGS+=	-Wno-missing-noreturn
CWARNFLAGS.clang+=	-Wno-unneeded-internal-declaration \
			-Wno-parentheses -Wno-constant-logical-operand \
			-Wno-error=unused-const-variable

WARNS?=	4

IDIST=		${NETBSDSRCDIR}/external/bsd/ntp/dist
NTP_SRCDIR=	${NETBSDSRCDIR}/external/bsd/ntp
NTP_HTMLDIR=	/usr/share/doc/reference/ref8

SYSCONFDIR=/etc
LOCALSTATEDIR=/var

CPPFLAGS+=-DHAVE_CONFIG_H
CPPFLAGS+=-I${NTP_SRCDIR}/include \
	  -I${IDIST}/include \
	  -I${IDIST}/libntp/lib/isc/include \
	  -I${IDIST}/libntp/lib/isc/unix/include \
	  -I${IDIST}/libntp/lib/isc/nothreads/include

.if defined(PROG)
LIBNTP!=	cd ${.CURDIR}/../../lib/libntp && ${PRINTOBJDIR}
LIBISC!=	cd ${.CURDIR}/../../lib/libiscntp && ${PRINTOBJDIR}
LIBOPTS!=	cd ${.CURDIR}/../../lib/libopts && ${PRINTOBJDIR}
LIBPARSE!=	cd ${.CURDIR}/../../lib/libparse && ${PRINTOBJDIR}

LDADD+=		-L${LIBNTP} -lntp -L${LIBOPTS} -lopts
DPADD+=		${LIBNTP}/libntp.a ${LIBOPTS}/libopts.a

LDADD+=		-lcrypto -lcrypt -lpthread
DPADD+=		${LIBCRYPTO} ${LIBCRYPT} ${LIBPTHREAD}

.if ${MKMDNS:Uno} != "no"
CPPFLAGS+=-DHAVE_DNSREGISTRATION=1
LDADD+=-ldns_sd
.endif

SRCS+=	version.c

CLEANFILES+=version.c version.o
version.c: ${LIBNTP}/libntp.a ${.CURDIR}/../../scripts/mkver
	${_MKTARGET_CREATE}
	${HOST_SH} ${.CURDIR}/../../scripts/mkver ${PROG} > /dev/null

.endif # defined(PROG)

CPPFLAGS+=-DOPENSSL -DAUTOKEY -DUSE_OPENSSL_CRYPTO_RAND

.if ${MKINET6:Uno} != "no"
CPPFLAGS+=-DWANT_IPV6
.else
CPPFLAGS+=-DISC_PLATFORM_NEEDIN6ADDRANY
.endif

.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif

COPTS.configfile.c+=		${CC_WNO_IMPLICIT_FALLTHROUGH}
COPTS.libopts.c+=		${CC_WNO_IMPLICIT_FALLTHROUGH}
COPTS.ntp_loopfilter.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}
COPTS.refclock_ulink.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}

.SUFFIXES: .1 .5 .8 .1ntpdmdoc .5mdoc .1ntp-keygenmdoc .1ntpdcmdoc .1ntpqmdoc .1sntpmdoc

.5mdoc.5:
	${TOOL_SED} -e 's/5mdoc/5/g' < ${.IMPSRC} > ${.TARGET}

.1ntpdmdoc.8:
	${TOOL_SED} -e 's/1ntpdmdoc/8/g' < ${.IMPSRC} > ${.TARGET}

.1ntp-keygenmdoc.8:
	${TOOL_SED} -e 's/1ntp-keygenmdoc/8/g' < ${.IMPSRC} > ${.TARGET}

.1ntpdcmdoc.8:
	${TOOL_SED} -e 's/1ntpdcmdoc/8/g' < ${.IMPSRC} > ${.TARGET}

.1ntpqmdoc.8:
	${TOOL_SED} -e 's/1ntpqmdoc/8/g' < ${.IMPSRC} > ${.TARGET}

.1sntpmdoc.1:
	${TOOL_SED} -e 's/1sntpmdoc/1/g' < ${.IMPSRC} > ${.TARGET}
.endif
