# $NetBSD: Makefile.inc,v 1.10 2025/07/19 16:06:03 christos Exp $

.include <bsd.own.mk>

USE_FORT?= yes  # network client/server

NSD:=${.PARSEDIR}/dist

CPPFLAGS+=-D_NETBSD_SOURCE
CPPFLAGS+=-I${NSD} -I${NSD}/../include
.PATH: ${NSD}

.if (${USE_INET6} != "no")
CPPFLAGS+=      -DINET6
.endif

.if ${HAVE_OPENSSL} < 11
CPPFLAGS+=	-DOPENSSL_API_COMPAT=0x10100000L
.endif

DPLIBS+= event ${NETBSDSRCDIR}/external/bsd/libevent/lib/libevent
DPLIBS+= ssl ${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libssl
DPLIBS+= crypto ${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libcrypto
DPLIBS+= crypt ${NETBSDSRCDIR}/lib/libcrypt
DPLIBS+= util ${NETBSDSRCDIR}/lib/libutil
DPLIBS+= pthread ${NETBSDSRCDIR}/lib/libpthread

__subst: .USE
	${TOOL_SED} \
	    -e s,@nsdconfigfile@,/etc/nsd/nsd.conf, \
	    -e s,@configdir@,/etc/nsd, \
	    -e s,@dbfile@,/var/db/nsd/nsd.db, \
	    -e s,@pidfile@,/var/run/nsd.pid, \
	    -e s,@zonelistfile@,/var/db/nsd/zone.list, \
	    -e s,@zonesdir@,/etc/namedb, \
	    -e s,@user@,_nsd, \
	    -e s,@logfile@,/var/log/nsd.log, \
	    -e s,@xfrdfile@,/var/db/nsd/xfdr.state, \
	    -e s,@xfrddir@,/var/tmp, \
	    -e s,@chrootdir@,/var/chroot/nsd, \
	    -e s,@ratelimit_default@,0, < ${.ALLSRC} > ${.TARGET}
