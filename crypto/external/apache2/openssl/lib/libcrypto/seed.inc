#	$NetBSD: seed.inc,v 1.1 2025/07/17 14:25:26 christos Exp $

.PATH.c:	${OPENSSLSRC}/crypto/seed

SEED_SRCS += \
seed.c \
seed_cbc.c \
seed_cfb.c \
seed_ecb.c \
seed_ofb.c \

SRCS += ${SEED_SRCS}

.for cryptosrc in ${SEED_SRCS}
CPPFLAGS.${cryptosrc} += -I${OPENSSLSRC}/crypto/seed ${SEEDCPPFLAGS}
.endfor
