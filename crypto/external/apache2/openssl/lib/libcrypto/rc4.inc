#	$NetBSD: rc4.inc,v 1.1 2025/07/17 14:25:26 christos Exp $

.PATH.c:	${OPENSSLSRC}/crypto/rc4

RC4_SRCS ?= rc4_skey.c rc4_enc.c

SRCS += ${RC4_SRCS}

.for cryptosrc in ${RC4_SRCS}
CPPFLAGS.${cryptosrc} = -I${OPENSSLSRC}/crypto/rc4
.endfor
