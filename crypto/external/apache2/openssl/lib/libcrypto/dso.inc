#	$NetBSD: dso.inc,v 1.1 2025/07/17 14:25:25 christos Exp $

.PATH.c:	${OPENSSLSRC}/crypto/dso

DSO_SRCS += \
dso_dl.c \
dso_dlfcn.c \
dso_err.c \
dso_lib.c \
dso_openssl.c \
dso_vms.c \
dso_win32.c \

SRCS += ${DSO_SRCS}

.for cryptosrc in ${DSO_SRCS}
CPPFLAGS.${cryptosrc} += -I${OPENSSLSRC}/crypto/dso ${DSOCPPFLAGS}
.endfor
