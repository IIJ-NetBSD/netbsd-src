# $NetBSD: Makefile,v 1.14 2025/07/13 20:15:16 christos Exp $
# PAM module for SSH
# $FreeBSD: src/lib/libpam/modules/pam_ssh/Makefile,v 1.18 2004/08/06 07:27:04 cperciva Exp $

NOLINT=		# don't build a lint library
NOPROFILE=	# don't build a profile library
NOPICINSTALL=	# don't install _pic.a library

.include <bsd.own.mk>

SSHSRC=	${NETBSDSRCDIR}/crypto/external/bsd/openssh/dist

.PATH: ${SSHSRC}
LIB=	pam_ssh
MAN=	pam_ssh.8
SRCS=	pam_ssh.c

CPPFLAGS+= -I${SSHSRC}

LIBDPLIBS+=	ssh	${NETBSDSRCDIR}/crypto/external/bsd/openssh/lib \
		crypt	${.CURDIR}/../../../libcrypt \
		crypto	${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libcrypto

.include "${.CURDIR}/../mod.mk"
