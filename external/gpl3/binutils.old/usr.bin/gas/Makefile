#	$NetBSD: Makefile,v 1.9 2024/06/29 16:36:07 christos Exp $

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
