#	$NetBSD: Makefile,v 1.9 2024/06/24 18:00:32 riastradh Exp $

PROG=		prekern
SRCS=		locore.S trap.S prekern.c mm.c console.c elf.c prng.c

NOSSP=		# defined
NOPIE=		# defined
NOLIBCSANITIZER=# defined
NOSANITIZER=	# defined
NOMAN=		1

S=	${.CURDIR}/../../../..

.PATH: ${.CURDIR}

BINDIR=		/usr/mdec
BINMODE=	444

.include <bsd.init.mk>

CPPFLAGS+=	-I. -I${S} -I${.OBJDIR} -I${.CURDIR}
CPPFLAGS+=	-D_STANDALONE

.include <bsd.klinks.mk>

CPPFLAGS+=	-DKERNEL -D__x86_64__
CFLAGS+=	-Wall -Werror -Wstrict-prototypes
CFLAGS+=	-mno-red-zone -mno-mmx -mno-sse -mno-avx -ffreestanding
STRIPFLAG=
LINKFLAGS=	-X -z max-page-size=0x100000 -Ttext 0x100000 \
		-T ${S}/arch/amd64/stand/prekern/prekern.ldscript
DPADD+=		prekern.ldscript

KERN_AS=	library
.include	"${S}/lib/libkern/Makefile.inc"
LIBKERN=	${KERNLIB}

LIBCRT0=	# nothing
LIBCRTI=	# nothing
LIBC=		# nothing
LIBCRTBEGIN=	# nothing
LIBCRTEND=	# nothing

${PROG}: ${OBJS} ${LIBKERN}
	${_MKTARGET_LINK}
	${LD} ${LINKFLAGS} -o ${.TARGET} ${OBJS} ${LIBKERN}

all:	${PROG}

.include <bsd.prog.mk>

cleandir distclean: .WAIT cleanlibdir

cleanlibdir:
	-rm -rf lib
