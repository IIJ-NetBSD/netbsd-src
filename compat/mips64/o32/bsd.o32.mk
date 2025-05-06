#	$NetBSD: bsd.o32.mk,v 1.17 2025/05/06 10:14:36 nia Exp $

.if !empty(MACHINE_ARCH:M*eb)
LD+=		-m elf32btsmip
.else
LD+=		-m elf32ltsmip
.endif
.ifndef MLIBDIR
MLIBDIR=	o32

LIBC_MACHINE_ARCH=	${MACHINE_ARCH:S/mipsn/mips/:S/64//}
LIBGCC_MACHINE_ARCH=	${LIBC_MACHINE_ARCH}
GOMP_MACHINE_ARCH=	${LIBC_MACHINE_ARCH}
XORG_MACHINE_ARCH=	${LIBC_MACHINE_ARCH}
BFD_MACHINE_ARCH=	${LIBC_MACHINE_ARCH}

COPTS+=		-mabi=32 -march=mips3
CPUFLAGS+=	-mabi=32 -march=mips3
LDADD+=		-mabi=32 -march=mips3
LDFLAGS+=	-mabi=32 -march=mips3
MKDEPFLAGS+=	-mabi=32 -march=mips3
.endif

# sync with MKRELRO in bsd.own.mk
NORELRO=	# defined

.include "${.PARSEDIR}/../../Makefile.compat"
