#	$NetBSD: crypto_md5.inc,v 1.1 2025/07/17 14:26:12 christos Exp $

.PATH:	${OPENSSLSRC}/crypto/md5

CRYPTO_MD5_SRCS += \
md5_dgst.c \
md5_one.c \
md5_sha1.c \

SRCS += ${CRYPTO_MD5_SRCS}

.for legacysrc in ${CRYPTO_MD5_SRCS}
CPPFLAGS.${legacysrc} += -I${OPENSSLSRC}/crypto/md5 ${CRYPTO_MD5CPPFLAGS}
.endfor
