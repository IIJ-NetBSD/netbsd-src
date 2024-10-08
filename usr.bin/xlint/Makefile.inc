#	$NetBSD: Makefile.inc,v 1.29 2024/03/29 08:35:31 rillig Exp $

.include <bsd.own.mk>

.PATH:		${.CURDIR}/../common

# Special cases first, then check for a MACHINE_ARCH specific
# subdir before looking for MACHINE_CPU.
.if exists(${.CURDIR}/../arch/${MACHINE_ARCH})
ARCHSUBDIR=	${MACHINE_ARCH}
.elif exists(${.CURDIR}/../arch/${MACHINE_ARCH:C/e[lb]$//})
ARCHSUBDIR=	${MACHINE_ARCH:C/e[lb]$//}
.else
ARCHSUBDIR=	${MACHINE_CPU}
.endif

CPPFLAGS+=	-I${.CURDIR}/../arch/${ARCHSUBDIR}
CPPFLAGS+=	-I${.CURDIR}/../common

CLEANFILES+=	*.gcno *.gcda *.gcov

.if ${MACHINE_CPU:Nalpha:Nvax}
CFLAGS+=	${ACTIVE_CC:Mgcc:%=-ftrapv}
.endif

.include "${.CURDIR}/../../Makefile.inc"

LINTFLAGS+=	-aa	# warn about all lossy conversions
LINTFLAGS+=	-e	# strict enum mode
LINTFLAGS+=	-T	# strict bool mode
LINTFLAGS+=	-w	# treat warnings as errors
