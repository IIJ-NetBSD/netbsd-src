#	$NetBSD: Makefile,v 1.24 2025/06/04 18:23:11 tsutsui Exp $

DRIVER=		xf86-input-keyboard
DRIVER_NAME=	kbd_drv

SRCS=		kbd.c bsd_KbdMap.c bsd_kbd.c at_scancode.c
MAN=		kbd.4

.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64" || ${MACHINE} == "cats"
CPPFLAGS+=	-DPCVT_SUPPORT
.endif

# turns out we can't use wskbd everywhere without a couple more translation
# tables in the X driver so make it the default only where we know it will work

.if ${MACHINE_ARCH} == "powerpc" || \
    ${MACHINE} == "amiga" || \
    ${MACHINE} == "evbarm" || \
    ${MACHINE} == "ews4800mips" || \
    ${MACHINE} == "hp300"	|| \
    ${MACHINE} == "luna68k"	|| \
    ${MACHINE} == "mac68k" || \
    ${MACHINE} == "newsmips" || \
    ${MACHINE} == "pmax" || \
    ${MACHINE} == "sgimips" || \
    ${MACHINE} == "shark" || \
    ${MACHINE} == "sparc" || \
    ${MACHINE} == "sparc64" || \
    ${MACHINE} == "vax"
CPPFLAGS+=	-DDEFAULT_TO_WSKBD
.endif

CPPFLAGS+=	-DWSCONS_SUPPORT
CPPFLAGS+=	-DXKB
CPPFLAGS.kbd.c=	-D__XKBDEFRULES__=${__XKBDEFRULES__}

.if \
    ${MACHINE} == "ews4800mips"	|| \
    ${MACHINE} == "hp300"	|| \
    ${MACHINE} == "luna68k"	|| \
    ${MACHINE} == "newsmips"	|| \
    0
CPPFLAGS+=	-DUSE_WSKBD_GETMAP
SRCS+=		ws_KbdMap.c
.endif

X11EXTRAMANDEFS+=	-e 's,__xkb_path__,${X11LIBDIR}/xkb,g'

COPTS.kbd.c=		-Wno-error	# XXX deprecated
COPTS.bsd_kbd.c=	-Wno-error	# XXX deprecated

.include "../Makefile.xf86-driver"
