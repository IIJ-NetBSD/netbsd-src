# $NetBSD: Makefile,v 1.44 2025/02/27 23:48:22 rillig Exp $

WARNS?=	0	# XXX third-party program, many issues
NOCLANGERROR=	# defined

.include <bsd.own.mk>

USE_FORT?= yes	# network client and server

PROG=	racoon
SRCS=	main.c session.c isakmp.c handler.c 			\
	isakmp_ident.c isakmp_agg.c isakmp_base.c 		\
	isakmp_quick.c isakmp_inf.c isakmp_newg.c 		\
	gssapi.c dnssec.c getcertsbyname.c privsep.c		\
	pfkey.c admin.c evt.c ipsec_doi.c oakley.c 		\
	grabmyaddr.c vendorid.c policy.c localconf.c 		\
	remoteconf.c crypto_openssl.c algorithm.c		\
	proposal.c sainfo.c strnames.c plog.c logger.c 		\
	schedule.c str2val.c safefile.c backupsa.c 		\
	genlist.c rsalist.c cftoken.l cfparse.y 		\
	prsa_tok.l prsa_par.y isakmp_xauth.c isakmp_cfg.c 	\
	isakmp_unity.c throttle.c isakmp_frag.c 		\
	nattraversal.c vmbuf.c sockmisc.c misc.c
MAN=	racoon.8 racoon.conf.5

DIST=	${NETBSDSRCDIR}/crypto/dist/ipsec-tools
CPPFLAGS+= -I${DIST}/src/racoon -I${DIST}/src/racoon/missing
CPPFLAGS+= -I${DIST}/src/libipsec
CPPFLAGS+= -I${NETBSDSRCDIR}/lib/libipsec -I.
CPPFLAGS+= -DIPSEC_DEBUG -DHAVE_CONFIG_H -DENABLE_WILDCARD_MATCH
CPPFLAGS+= -DADMINPORTDIR=\"/var/run\"
CPPFLAGS+= -DSYSCONFDIR=\"/etc/racoon\"
.if ${HAVE_OPENSSL} < 11
CPPFLAGS+= -DHAVE_OPENSSL_RC5_H
CPPFLAGS+= -DOPENSSL_API_COMPAT=0x10100000L
.endif

LDADD+= -ll -ly -lipsec -lutil
DPADD+= ${LIBL} ${LIBY} ${LIBCRYPTO} ${LIBUTIL}

YHEADER=cfparse.h

CPPFLAGS+=-DHAVE_LIBRADIUS
LDADD+=-lradius
DPADD+= ${LIBRADIUS}
.if (${USE_PAM} != "no")
CPPFLAGS+=-DHAVE_LIBPAM
LDADD+=	-lpam -lutil ${PAM_STATIC_LDADD}
DPADD+=	${LIBPAM} ${PAM_STATIC_DPADD}
.endif
.if (${USE_KERBEROS} != "no")
CPPFLAGS+=-DHAVE_GSSAPI
LDADD+= -lgssapi -lheimntlm ${LIBKRB5_LDADD}
DPADD+= ${LIBGSSAPI} ${LIBHEIMNTLM} ${LIBKRB5_DPADD}
.endif
CPPFLAGS+=-DHAVE_OPENSSL_IDEA_H
.if (${USE_INET6} != "no")
CPPFLAGS+=-DINET6
.endif

.if (${USE_LDAP} != "no")
CPPFLAGS+=-DHAVE_LIBLDAP
LDADD+= ${LIBLDAP_LDADD}
DPADD+= ${LIBLDAP_DPADD}
.endif

LDADD+= -lcrypto -lcrypt
DPADD+= ${LIBIPSEC} ${LIBCRYPT}

#CPPFLAGS+= -DDEBUG_RECORD_MALLOCATION
#SRCS+= debugrm.c

#CFLAGS+=-fsanitize=address
#LDFLAGS+=-fsanitize=address

.PATH:  ${NETBSDSRCDIR}/lib/libipsec ${DIST}/src/racoon

prsa_tok.c: ${DIST}/src/racoon/prsa_tok.l
	${LEX} -Pprsa -o${.TARGET} ${.ALLSRC}

prsa_par.c: ${DIST}/src/racoon/prsa_par.y
	${YACC} -pprsa -d -o ${.TARGET} ${.ALLSRC}

CWARNFLAGS.gcc+=        ${CC_WNO_ADDRESS_OF_PACKED_MEMBER}

COPTS+=		-fcommon
COPTS.crypto_openssl.c+= -Wno-error=deprecated-declarations
COPTS.rsalist.c+= -Wno-error=deprecated-declarations
COPTS.prsa_par.c+= -Wno-error=deprecated-declarations
COPTS.plainrsa-gen.c+= -Wno-error=deprecated-declarations

# Has to be fixed upstream.
LINTFLAGS+=	-X 384		# function definition with identifier list

.include <bsd.prog.mk>
