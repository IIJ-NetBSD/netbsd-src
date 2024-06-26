#	$NetBSD: Makefile,v 1.15 2024/06/30 16:10:21 christos Exp $

CPPFLAGS+=	-I${.CURDIR}/arch/${BINUTILS_MACHINE_ARCH}
CPPFLAGS+=	-I${DIST}/gas -I${DIST}/gas/config
CPPFLAGS+=	-I${DIST}

.include "${.CURDIR}/../common/Makefile.inc"
.include "${.CURDIR}/arch/${BINUTILS_MACHINE_ARCH}/defs.mk"

PROG=		as
SRCS=		${G_OBJS:M*.o:Nitbl-[pl]*:Nm68k-parse.o:.o=.c} \
		${G_OBJS:M*.o:Mitbl-parse.o:.o=.y} \
		${G_OBJS:M*.o:Mitbl-lex-wrapper.o:S/-wrapper.o/.l/} \
		${G_OBJS:M*.o:Mm68k-parse.o:.o=.y}

COPTS.obj-elf.c = -Wno-stack-protector
COPTS.atof-generic.c = -Wno-stack-protector
COPTS.dwarf2dbg.c = -Wno-stack-protector
COPTS.symbols.c = -Wno-stack-protector
COPTS.stabs.c = -Wno-stack-protector
COPTS.macro.c = -Wno-stack-protector

PROGDPLIBS+=	opcodes	${TOP}/${BFDSUBDIR}/libopcodes
PROGDPLIBS+=	bfd	${TOP}/${BFDSUBDIR}/libbfd
PROGDPLIBS+=	sframe	${TOP}/${BFDSUBDIR}/libsframe
PROGDPLIBS+=	iberty	${TOP}/${BFDSUBDIR}/libiberty

TEXINFO=	as.texinfo
COMMONOBJDIR!=	cd ${TOP}/usr.bin/common && ${PRINTOBJDIR}
INFOFLAGS=	-I${DIST}/gas/doc -I${COMMONOBJDIR} -I${DIST}/libiberty

as.info:	bfdver.texi

.PATH: ${COMMONOBJDIR} ${DIST}/gas ${DIST}/gas/config ${DIST}/gas/doc

.include <bsd.prog.mk>
.include <bsd.info.mk>

LDADD+=		-lintl -lz -lm
DPADD+=		${LIBINTL} ${LIBZ} ${LIBM}
