#	$NetBSD: Makefile,v 1.16 2024/06/18 04:07:18 mrg Exp $

.include <bsd.init.mk>

SUBDIR=	sanitizer

.if exists(${.CURDIR}/arch/${GCC_MACHINE_ARCH}.mk)
.include "${.CURDIR}/arch/${GCC_MACHINE_ARCH}.mk"
.endif

INCS= ${G_EXTRA_HEADERS:T}

.include "../rs6000-inc.mk"

# Part of the standard headers, the rest of which we replace.
INCS+=	stdatomic.h

# XXX missed some how
.if ${GCC_MACHINE_ARCH} == "i386" || ${GCC_MACHINE_ARCH} == "x86_64"
INCS+=	mm_malloc.h

.if !target(obj)
mm_malloc.h: ${DIST}/gcc/config/i386/pmm_malloc.h
	cp -f $> ${.TARGET}
.endif

CLEANFILES+=	mm_malloc.h
.endif

INCSDIR=	${GCC_INCSDIR}

.include <bsd.subdir.mk>
.include <bsd.prog.mk>

.PATH: ${G_EXTRA_HEADERS:H:O:u}
