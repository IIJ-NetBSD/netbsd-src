#	$NetBSD: Makefile,v 1.14 2025/07/06 06:47:59 nat Exp $

USE_FORT?=	yes # data-driven bugs?

.include <bsd.own.mk>

LIB=	Xfont
XFONTDIR= ${X11SRCDIR.${LIB}}
.PATH:	${XFONTDIR}/src
.PATH:	${XFONTDIR}/src/FreeType
#.PATH:	${XFONTDIR}/src/Speedo
#.PATH:	${XFONTDIR}/src/Type1
.PATH:	${XFONTDIR}/src/bitmap
.PATH:	${XFONTDIR}/src/builtins
.PATH:	${XFONTDIR}/src/fc
.PATH:	${XFONTDIR}/src/fontcache
.PATH:	${XFONTDIR}/src/fontfile
.PATH:	${XFONTDIR}/src/stubs
.PATH:	${XFONTDIR}/src/util
.PATH:	${XFONTDIR}/include/X11/fonts
.PATH:	${XFONTDIR}/../include

SRCS=	ftenc.c ftfuncs.c fttools.c xttcap.c \
	bdfread.c bdfutils.c bitmap.c bitmapfunc.c bitmaputil.c \
	bitscale.c fontink.c pcfread.c pcfwrite.c snfread.c dir.c file.c \
	fonts.c fpe.c render.c fsconvert.c fserve.c fsio.c fstrans.c \
	bitsource.c bufio.c catalogue.c decompress.c defaults.c \
	dirfile.c fileio.c filewr.c fontdir.c fontencc.c fontfile.c \
	fontscale.c gunzip.c register.c renderers.c bunzip2.c cauthgen.c \
	csignal.c delfntcid.c errorf.c findoldfnt.c \
	getcres.c getdefptsize.c getnewfntcid.c gettime.c initfshdl.c \
	regfpefunc.c rmfshdl.c servclient.c setfntauth.c stfntcfnt.c \
	atom.c fontaccel.c fontnames.c fontutil.c fontxlfd.c \
	format.c miscutil.c patcache.c private.c utilbitmap.c stubsinit.c

INCS=	bdfint.h bitmap.h bufio.h fntfil.h fntfilio.h fntfilst.h \
	fontconf.h fontencc.h fontmisc.h fontshow.h \
	fontutil.h fontxlfd.h ft.h ftfuncs.h pcf.h
INCSDIR=${X11INCDIR}/X11/fonts

CPPFLAGS+=	${X11FLAGS.THREADLIB} ${X11FLAGS.CONNECTION}
CPPFLAGS+=	-DHAS_SNPRINTF -DHAVE_CONFIG_H
CPPFLAGS+=      -DBDFFORMAT -DSNFFORMAT -DPCFFORMAT -DX_GZIP_FONT_COMPRESSION -DX_BZIP2_FONT_COMPRESSION
CPPFLAGS+=	-DFONT_ENCODINGS_DIRECTORY=\"${X11LIBDIR}/X11/fonts/encodings/encodings.dir\" 
CPPFLAGS+=	-I${DESTDIR}${X11INCDIR}
CPPFLAGS+=	-I${DESTDIR}${X11INCDIR}/X11/fonts
CPPFLAGS+=	-I${DESTDIR}${X11INCDIR}/freetype2
CPPFLAGS+=	-I${XFONTDIR}/src/stubs
CPPFLAGS+=	-I${XFONTDIR}/../include
CPPFLAGS+=	-I${DESTDIR}${X11INCDIR}/freetype2/freetype

LIBDPLIBS=\
	Xt	${.CURDIR}/../libXt \
	SM	${.CURDIR}/../libSM \
	ICE	${.CURDIR}/../libICE \
	Xext	${.CURDIR}/../libXext \
	fontenc	${.CURDIR}/../libfontenc \
	freetype	${.CURDIR}/../freetype \
	X11	${.CURDIR}/../libX11/dynamic \
	m	${.CURDIR}/../../../../../lib/libm \
	z	${.CURDIR}/../../../../../lib/libz \
	bz2	${.CURDIR}/../../../../../lib/libbz2

# XXX
COPTS.ffcheck.c+=	-Wno-error
COPTS.register.c+=	-Wno-error

PKGDIST=	${LIB}

CWARNFLAGS.clang+=	-Wno-tautological-compare -Wno-string-plus-int
COPTS.ftfuncs.c+=	${${ACTIVE_CC} == "gcc" && ${HAVE_GCC:U0} >= 10:? -Wno-error=overflow :}

.if ${HAVE_GCC:U0} >= 12 && ${MACHINE_ARCH} == "m68k"
COPTS.fontxlfd.c+=	-fno-stack-protector
.endif

.include <bsd.x11.mk>
.include <bsd.lib.mk>
