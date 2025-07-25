#	$NetBSD: bsd.x11.mk,v 1.162 2025/06/24 05:59:45 mrg Exp $

.include <bsd.init.mk>

BINDIR=			${X11BINDIR}
LIBDIR=			${X11USRLIBDIR}
MANDIR=			${X11MANDIR}

COPTS+=			-fno-strict-aliasing

.include <bsd.sys.mk>

.if !defined(NOSSP) && (${USE_SSP:Uno} != "no")
CPPFLAGS+=		-DNO_ALLOCA
.endif

X11FLAGS.VERSION=	-DOSMAJORVERSION=5 -DOSMINORVERSION=99		# XXX

#	 THREADS_DEFINES
X11FLAGS.THREADS=	-DXTHREADS -D_REENTRANT -DXUSE_MTSAFE_API \
			-DXNO_MTSAFE_PWDAPI

#	 CONNECTION_FLAGS
X11FLAGS.CONNECTION=	-DTCPCONN -DUNIXCONN -DHAS_STICKY_DIR_BIT \
			-DHAS_FCHOWN -DHAVE_GETADDRINFO -DHAVE_INET_NTOP \
			-DHAVE_STRUCT_SOCKADDR_STORAGE

.if (${USE_INET6} != "no")
X11FLAGS.CONNECTION+=	-DIPv6
.endif

#	 EXT_DEFINES
X11FLAGS.BASE_EXTENSION=	-DMITMISC -DXTEST -DXTRAP -DXSYNC -DXCMISC \
				-DXRECORD -DMITSHM -DBIGREQS -DXF86VIDMODE \
				-DDPMSExtension -DEVI \
				-DSCREENSAVER -DXV -DXVMC -DGLXEXT \
				-DRES

X11FLAGS.PERVASIVE_EXTENSION=	-DSHAPE -DXINPUT -DXKB -DLBX -DXAPPGROUP \
				-DXCSECURITY -DTOGCUP -DXF86BIGFONT \
				-DDPMSExtension -DPIXPRIV -DPANORAMIX \
				-DRENDER -DRANDR -DXFIXES -DDAMAGE \
				-DCOMPOSITE -DXEVIE
X11FLAGS.EXTENSION=	${X11FLAGS.BASE_EXTENSION} \
			${X11FLAGS.PERVASIVE_EXTENSION}

X11FLAGS.DIX=		-DHAVE_DIX_CONFIG_H -D_BSD_SOURCE -DHAS_FCHOWN \
			-DHAS_STICKY_DIR_BIT -D_POSIX_THREAD_SAFE_FUNCTIONS=200112L \
			-DHAVE_XORG_CONFIG_H
X11INCS.DIX=		-I${DESTDIR}${X11INCDIR}/freetype2  \
			-I${DESTDIR}${X11INCDIR}/pixman-1 \
			-I$(X11SRCDIR.xorg-server)/include \
			-I$(X11SRCDIR.xorg-server)/Xext \
			-I$(X11SRCDIR.xorg-server)/composite \
			-I$(X11SRCDIR.xorg-server)/damageext \
			-I$(X11SRCDIR.xorg-server)/xfixes \
			-I$(X11SRCDIR.xorg-server)/Xi \
			-I$(X11SRCDIR.xorg-server)/mi \
			-I$(X11SRCDIR.xorg-server)/miext/shadow \
			-I$(X11SRCDIR.xorg-server)/miext/damage \
			-I$(X11SRCDIR.xorg-server)/render \
			-I$(X11SRCDIR.xorg-server)/randr \
			-I$(X11SRCDIR.xorg-server)/fb \
			-I$(X11SRCDIR.xorg-server)/../include

X11FLAGS.DRI=		-DGLXEXT -DXF86DRI -DGLX_DIRECT_RENDERING \
			-DGLX_USE_DLOPEN -DGLX_USE_MESA

.if ${X11DRI} != "no"
X11FLAGS.EXTENSION+=	${X11FLAGS.DRI}
.endif

#	 ServerDefines
X11FLAGS.SERVER=	-DSHAPE -DXKB -DLBX -DXAPPGROUP -DXCSECURITY \
			-DTOGCUP -DXF86BIGFONT -DDPMSExtension -DPIXPRIV \
			-DPANORAMIX -DRENDER -DRANDR -DGCCUSESGAS \
			-DAVOID_GLYPHBLT -DSINGLEDEPTH -DXvExtension \
			-DXFree86Server -DXvMCExtension -DSMART_SCHEDULE \
			-DBUILDDEBUG -DXResExtension -DNDEBUG

#	 OS_DEFINES
X11FLAGS.OS_DEFINES=	-DDDXOSINIT -DSERVER_LOCK -DDDXOSFATALERROR \
			-DDDXOSVERRORF -DDDXTIME -DUSB_HID

.if !(${MACHINE} == "acorn32"	|| \
    ${MACHINE} == "sun3"	|| \
    ${MACHINE} == "x68k")
#	EXT_DEFINES
X11FLAGS.EXTENSION+=	-DXF86VIDMODE

X11FLAGS.DIX+=		-DDBE -DXRECORD -DPRESENT

#	ServerDefines
X11FLAGS.SERVER+=	-DXFreeXDGA -DXF86VIDMODE
X11FLAGS.SERVER+=	-DXINPUT -DXSERVER_LIBPCIACCESS
.endif

.if ${MACHINE_ARCH} == "alpha"	|| \
    ${MACHINE_ARCH} == "ia64"   || \
    ${MACHINE_ARCH} == "powerpc64" || \
    ${MACHINE_ARCH} == "sparc64" || \
    ${MACHINE_ARCH} == "x86_64" || \
    ${MACHINE_CPU} == "aarch64"
#	ServerDefines
X11FLAGS.SERVER+=	-D_XSERVER64
X11FLAGS.EXTENSION+=	-D__GLX_ALIGN64
.endif

.if ${MACHINE} == "amd64"	|| \
    ${MACHINE} == "cats"	|| \
    ${MACHINE} == "i386"	|| \
    ${MACHINE} == "macppc"	|| \
    ${MACHINE} == "netwinder"	|| \
    ${MACHINE} == "ofppc"	|| \
    ${MACHINE} == "prep"	|| \
    ${MACHINE} == "sgimips"	|| \
    ${MACHINE} == "sparc64"	|| \
    ${MACHINE} == "sparc"	|| \
    ${MACHINE} == "shark"	|| \
    ${MACHINE} == "zaurus"
#	LOADABLE
.if ${XORG_SERVER_SUBDIR:Uxorg-server.old} == "xorg-server.old"
X11FLAGS.LOADABLE=	-DXFree86LOADER
.endif
X11FLAGS.LOADABLE+=	${${ACTIVE_CXX} == "gcc":? -fno-merge-constants :}
.endif

# XXX FIX ME
.if ${XORG_SERVER_SUBDIR:Uxorg-server.old} == "xorg-server.old"
XORG_SERVER_MAJOR=	1
XORG_SERVER_MINOR=	10
XORG_SERVER_TEENY=	6
XORG_VERSION_CURRENT="(((${XORG_SERVER_MAJOR}) * 10000000) + ((${XORG_SERVER_MINOR}) * 100000) + ((${XORG_SERVER_TEENY}) * 1000) + 0)"
.else
XORG_SERVER_MAJOR=	21
XORG_SERVER_MINOR=	1
XORG_SERVER_TEENY=	18
XORG_VERSION_CURRENT="((10000000) + ((${XORG_SERVER_MAJOR}) * 100000) + ((${XORG_SERVER_MINOR}) * 1000) + ${XORG_SERVER_TEENY})"
.endif

XVENDORNAMESHORT=	'"X.Org"'
XVENDORNAME=		'"The X.Org Foundation"'
XORG_RELEASE=		'"Release ${XORG_SERVER_MAJOR}.${XORG_SERVER_MINOR}.${XORG_SERVER_TEENY}"'
__XKBDEFRULES__=	'"xorg"'
XLOCALE.DEFINES=	-DXLOCALEDIR=\"${X11LIBDIR}/locale\" \
			-DXLOCALELIBDIR=\"${X11LIBDIR}/locale\"

PRINT_PACKAGE_VERSION=	${TOOL_AWK} '/^PACKAGE_VERSION=/ {		\
				match($$1, "([0-9]+\\.)+[0-9]+");	\
				version = substr($$1, RSTART, RLENGTH);	\
			} END { print version }'

_CONFIGURE_PATH=
.if exists(${X11SRCDIR.${PROG}}/configure)
_CONFIGURE_PATH=${X11SRCDIR.${PROG}}/configure
.elif exists(${X11SRCDIR.${LIB}}/configure)
_CONFIGURE_PATH=${X11SRCDIR.${LIB}}/configure
.endif

.if exists(${_CONFIGURE_PATH})
_PRINT_PACKAGE_STRING=	${TOOL_AWK} -F= '/^PACKAGE_STRING=/ { print $$2 }' \
			${_CONFIGURE_PATH}
PACKAGE_STRING!=	${_PRINT_PACKAGE_STRING}
.else
PACKAGE_STRING=		"X11 program"
.endif

# Commandline to convert 'XCOMM' comments and 'XHASH' to '#', among other
# things. Transformed from the "CppSedMagic" macro from "Imake.rules".
#
X11TOOL_UNXCOMM=    ${TOOL_SED}	-e '/^\#  *[0-9][0-9]*  *.*$$/d' \
			-e '/^\#line  *[0-9][0-9]*  *.*$$/d' \
			-e '/^[ 	]*XCOMM$$/s/XCOMM/\#/' \
			-e '/^[ 	]*XCOMM[^a-zA-Z0-9_]/s/XCOMM/\#/' \
			-e '/^[ 	]*XHASH/s/XHASH/\#/' \
			-e '/\@\@$$/s/\@\@$$/\\/'


CPPFLAGS+=		-DCSRG_BASED -DFUNCPROTO=15 -DNARROWPROTO
CPPFLAGS+=		-I${DESTDIR}${X11INCDIR}

.if ${MACHINE_ARCH} == "x86_64"
CPPFLAGS+=		-D__AMD64__
.endif

LDFLAGS+=		-Wl,-rpath,${X11USRLIBDIR} -L=${X11USRLIBDIR}


#
# .cpp -> "" handling
# CPPSCRIPTS		list of files/scripts to run through cpp
# CPPSCRIPTFLAGS	extra flags to ${CPP}
# CPPSCRIPTFLAGS_fn	extra flags to ${CPP} for file `fn'
#
.if defined(CPPSCRIPTS)						# {
.SUFFIXES:	.cpp

.cpp:
	${_MKTARGET_CREATE}
	rm -f ${.TARGET}
	${CC} -E -undef -traditional - \
	    ${CPPSCRIPTFLAGS_${.TARGET}:U${CPPSCRIPTFLAGS}} \
	    < ${.IMPSRC} | ${X11TOOL_UNXCOMM} > ${.TARGET}

realall: ${CPPSCRIPTS}

CLEANFILES+= ${CPPSCRIPTS}
.endif								# }

# Used by pkg-config and manual handling to ensure we picked up all
# the necessary changes.
#
# Skip any line that starts with .IN (old X11 indexing method),
# or between a tab(@) and .TE.
_X11SKIP_FALSE_POSITIVE_GREP_CMD= \
	${TOOL_SED} -e '/tab(@)/,/^\.TE/d' -e '/^\.IN /d' ${.TARGET}.tmp | \
	${TOOL_GREP} -E '@([^ 	]+)@'

#
# X.Org pkgconfig files handling
#
# PKGCONFIG is expected to contain a list of pkgconfig module names.
# They will produce the files <module1>.pc, <module2>.pc, etc, to be
# put in X11USRLIBDIR/pkgconfig.
#
# PKGDIST contains the name of a X11SRCDIR subscript where to find the
# source file for the pkgconfig files.
#
# If PKGDIST is not suitable, a consumer can set PKGDIST.<module> with
# the full path to the source file.
#
# Also, the consumer can use PKGDIST alone, and a PKGCONFIG will be
# derived from it.  Many times, PKGDIST is capitalized and PKGCONFIG is
# the lower case version.
#

.if defined(PKGDIST) && !defined(PKGCONFIG)
PKGCONFIG=	${PKGDIST:tl}
.endif
.if defined(PKGCONFIG) && !defined(MLIBDIR)

.include <bsd.files.mk>

_PKGCONFIG_FILES=	${PKGCONFIG:C/$/.pc/}

.PHONY:	pkgconfig-install
pkgconfig-install:

realall:	${_PKGCONFIG_FILES:O:u}
realinstall:	pkgconfig-install

.for _pkg in ${PKGCONFIG:O:u}	# {

PKGDIST.${_pkg}?=	${X11SRCDIR.${PKGDIST:U${_pkg}}}
_PKGDEST.${_pkg}=	${DESTDIR}${X11USRLIBDIR}/pkgconfig/${_pkg}.pc

.PATH:	${PKGDIST.${_pkg}}

FILESOWN_${_pkg}.pc=	${BINOWN}
FILESGRP_${_pkg}.pc=	${BINGRP}
FILESMODE_${_pkg}.pc=	${NONBINMODE}

${_PKGDEST.${_pkg}}: ${_pkg}.pc __fileinstall
pkgconfig-install: ${_PKGDEST.${_pkg}}

# Add a dependancy on the configure file if it exists; this way we
# will rebuild the .pc file if the version in configure changes.
.if exists(${PKGDIST.${_pkg}}/configure)
${_pkg}.pc: ${PKGDIST.${_pkg}}/configure Makefile
.endif

.endfor				# }

# XXX
# The sed script is very, very ugly.  What we actually need is a
# mknative-xorg script that will generate all the .pc files from
# running the autoconfigure script.
# And yes, it has to be split in multiple parts otherwise it's
# too long for sed to handle.

# hacky transforms:
#   @XCBPROTO_VERSION@

.SUFFIXES:	.pc.in .pc
.pc.in.pc:
	${_MKTARGET_CREATE}
	rm -f ${.TARGET}
	if [ -n '${PKGCONFIG_VERSION.${.PREFIX}}' ]; then \
		_pkg_version='${PKGCONFIG_VERSION.${.PREFIX}}'; \
	else \
		_pkg_version=$$(${PRINT_PACKAGE_VERSION} \
		    ${PKGDIST.${.PREFIX}}/configure); \
	fi; \
	${TOOL_SED} \
		${PKGCONFIG_SED_FLAGS} \
		-e "s,@prefix@,${X11ROOTDIR},; \
		s,@INSTALL_DIR@,${X11ROOTDIR},; \
		s,@exec_prefix@,\\$$\{prefix\},; \
		s,@libdir@,\\$$\{prefix\}/lib,; \
		s,@includedir@,\\$$\{prefix\}/include,; \
		s,@datarootdir@,\\$$\{prefix\}/share,; \
		s,@datadir@,\\$$\{datarootdir\},; \
		s,@appdefaultdir@,\\$$\{libdir}/X11/app-defaults,; \
		s,@MAPDIR@,\\$$\{libdir\}/X11/fonts/util,; \
		s,@ICONDIR@,\\$$\{datarootdir\}/icons,; \
		s,@PACKAGE_VERSION@,$${_pkg_version},; \
		s,@VERSION@,$${_pkg_version},; \
		s,@COMPOSITEEXT_VERSION@,$${_pkg_version%.*},; \
		s,@DAMAGEEXT_VERSION@,$${_pkg_version%.*},; \
		s,@FIXESEXT_VERSION@,$${_pkg_version%.*},; \
		s,@PRESENTEXT_VERSION@,$${_pkg_version%.*},; \
		s,@RANDR_VERSION@,$${_pkg_version%.*},; \
		s,@RENDER_VERSION@,$${_pkg_version%.*}," \
		-e "s,@LIBS@,,; \
		s,@Z_LIBS@,-lz,; \
		s,@LIBZ@,-lz,; \
		s,@LIBBZ2@,-lbz2,; \
		s,@xkb_base@,\\$$\{prefix\}/lib/X11/xkb,; \
		s,@xcbincludedir@,\\$$\{prefix\}/share/xcb,; \
		s,@fontrootdir@,\\$$\{libdir\}/X11/fonts,; \
		s,@LIBXML2_LIBS@,,; \
		s,@LIBXML2_CFLAGS@,,; \
		s,@ICONV_CFLAGS@,,; \
		s,@ICONV_LIBS@,,; \
		s,@NEEDED@,,; \
		s,@FT2_EXTRA_LIBS@,," \
		-e "s,@moduledir@,\\$$\{libdir\}/modules,; \
		s,@sdkdir@,\\$$\{includedir\}/xorg,; \
		s,@PIXMAN_CFLAGS@,,; \
		s,@LIB_DIR@,/lib,; \
		s,@INSTALL_LIB_DIR@,\\$$\{prefix\}/lib,; \
		s,@INSTALL_INC_DIR@,\\$$\{prefix\}/include,; \
		s,@XKBPROTO_REQUIRES@,kbproto,; \
		s,@XCBPROTO_VERSION@,1.7,; \
		s,@FREETYPE_REQUIRES@,freetype2,; \
		s,@EXPAT_LIBS@,-lexpat,; \
		s,@FREETYPE_LIBS@,-lfreetype,; \
		s,@DEP_CFLAGS@,,; \
		s,@DEP_LIBS@,,; \
		s,@X11_EXTRA_DEPS@,,; \
		s,@XTHREAD_CFLAGS@,-D_REENTRANT,; \
		s,@XTHREADLIB@,-lpthread,; \
		s,@GL_LIB@,GL,; \
		s,@GL_PC_REQ_PRIV@,x11 xext,; \
		s,@GL_PC_LIB_PRIV@,-lm -lpthread,; \
		s,@GL_PC_CFLAGS@,,; \
		s,@GLX_TLS@,no," \
		-e "s,@GLU_LIB@,GLU,; \
		s,@GLU_PC_REQ@,gl,; \
		s,@GLU_PC_REQ_PRIV@,,; \
		s,@GLU_PC_LIB_PRIV@,-lGLU,; \
		s,@GLU_PC_CFLAGS@,,; \
		s,@GLUT_LIB@,glut,; \
		s,@GLUT_PC_REQ_PRIV@,gl glu,; \
		s,@GLUT_PC_LIB_PRIV@,-lglut,; \
		s,@GLUT_PC_CFLAGS@,,; \
		s,@GLW_PC_CFLAGS@,,; \
		s,@GLW_PC_REQ_PRIV@,x11 xt,; \
		s,@GLW_PC_LIB_PRIV@,,; \
		s,@DRI_DRIVER_DIR@,\\$$\{libdir\}/modules/dri,; \
		s,@DRI_PC_REQ_PRIV@,,; \
		s,@GLW_LIB@,GLw,; \
		s,@GBM_PC_REQ_PRIV@,,; \
		s,@GBM_PC_LIB_PRIV@,,; \
		s,@abi_ansic@,0.4,; \
		s,@abi_videodrv@,5.0,; \
		s,@abi_xinput@,4.0,; \
		s,@abi_extension@,2.0,; \
		s,@abi_font@,0.6,; \
		s,@fchown_define@,-DHAS_FCHOWN,; \
		s,@sticky_bit_define@,-DHAS_STICKY_DIR_BIT,;" \
		-e "s,@PKG_CONFIG_LIBS@,${PKG_CONFIG_LIBS},; \
		s,@PACKAGE@,${PKGDIST},; \
		s,@PKGCONFIG_REQUIRES@,${PKGCONFIG_REQUIRES},; \
		s,@PKGCONFIG_REQUIRES_PRIVATELY@,${PKGCONFIG_REQUIRES_PRIVATELY},; \
		s,@ERRORDBDIR@,${X11LIBDIR},; \
		s,@EXPAT_CFLAGS@,,; \
		s,@FREETYPE_CFLAGS@,-I${X11ROOTDIR}/include/freetype2 -I${X11ROOTDIR}/include,;" \
		-e '/^Libs:/ s%-L\([^ 	]*\)%-Wl,-rpath,\1 &%g' \
		< ${.IMPSRC} > ${.TARGET}.tmp
	if ${_X11SKIP_FALSE_POSITIVE_GREP_CMD}; then \
		echo "pkg-config ${.TARGET} matches @.*@, probably missing updates" 1>&2; \
		false; \
	else \
		${MV} ${.TARGET}.tmp ${.TARGET}; \
	fi

CLEANFILES+= ${_PKGCONFIG_FILES} ${_PKGCONFIG_FILES:C/$/.tmp/}
.endif

#
# APPDEFS (app defaults) handling
#
.if defined(APPDEFS)						# {
appdefsinstall:: .PHONY ${APPDEFS:@S@${DESTDIR}${X11LIBDIR}/app-defaults/${S:T:R}@}
.PRECIOUS:	${APPDEFS:@S@${DESTDIR}${X11LIBDIR}/app-defaults/${S:T:R}@}

__appdefinstall: .USE
	${_MKTARGET_INSTALL}
	${INSTALL_FILE} -o ${BINOWN} -g ${BINGRP} -m ${NONBINMODE} \
	    ${.ALLSRC} ${.TARGET}

.for S in ${APPDEFS:O:u}
${DESTDIR}${X11LIBDIR}/app-defaults/${S:T:R}: ${S} __appdefinstall
.endfor

realinstall: appdefsinstall
.endif								# }


#
# .man page handling
#
.if (${MKMAN} != "no" && (${MAN:U} != "" || ${PROG:U} != ""))	# {
CLEANDIRFILES+= ${MAN:U${PROG:D${PROG.1}}}
.endif								# }

.SUFFIXES:	.man .man.pre .1 .3 .4 .5 .7 .8

# Note the escaping trick for _X11MANTRANSFORM using % to replace spaces
XORGVERSION=	'"X Version 11"'

_X11MANTRANSFORM= \
	${X11EXTRAMANTRANSFORMS}

# These ones used to appear as __foo__ but may be now @foo@.
_X11MANTRANSFORMS_BOTH=\
	${X11EXTRAMANTRANSFORMS_BOTH} \
	appmansuffix		1 \
	APP_MAN_SUFFIX		1 \
	LIB_MAN_SUFFIX		3 \
	libmansuffix		3 \
	oslibmansuffix		3 \
	drivermansuffix		4 \
	filemansuffix		5 \
	MISC_MAN_SUFFIX		7 \
	miscmansuffix		7 \
	adminmansuffix		8 \
	XORG_MAN_PAGE		"X Version 11" \
	logdir			/var/log \
	sysconfdir		/etc \
	apploaddir		${X11ROOTDIR}/lib/X11/app-defaults \
	bindir			${X11BINDIR} \
	datadir			${X11LIBDIR} \
	libdir			${X11ROOTDIR}/lib \
	mandir			${X11MANDIR} \
	projectroot		${X11ROOTDIR} \
	xkbconfigroot		${X11LIBDIR}/xkb \
	vendorversion		${XORGVERSION:C/ /%/gW} \
	XCONFIGFILE		xorg.conf \
	xconfigfile		xorg.conf \
	XCONFIGFILEMAN		'xorg.conf(5)' \
	xlocaledir		${X11LIBDIR}/locale \
	xorgversion		${XORGVERSION:C/ /%/gW} \
	XSERVERNAME		Xorg \
	xservername		Xorg

.if !empty(PACKAGE_STRING)
_X11MANTRANSFORMS_BOTH+=\
	PACKAGE_STRING		${PACKAGE_STRING}
.endif

.for __def__ __value__ in ${_X11MANTRANSFORMS_BOTH}
_X11MANTRANSFORM+= \
	__${__def__}__		${__value__} \
	@${__def__}@		${__value__}
.endfor

_X11MANTRANSFORM+= \

_X11MANTRANSFORMCMD=	${TOOL_SED} -e 's/\\$$/\\ /' ${.IMPSRC}

# XXX document me.
X11MANCPP?=	no

.if ${X11MANCPP} != "no"
_X11MANTRANSFORMCMD+=	| ${CC} -E -undef -traditional -
. for __def__ __value__ in ${_X11MANTRANSFORM}
_X11MANTRANSFORMCMD+=	-D${__def__}=${__value__:C/%/ /gW}
. endfor
.else
_X11MANTRANSFORMCMD+=	| ${TOOL_SED}
. for __def__ __value__ in ${_X11MANTRANSFORM}
_X11MANTRANSFORMCMD+=	-e s,${__def__},${__value__:C/%/ /gW},g
. endfor
.endif
_X11MANTRANSFORMCMD+=	${X11EXTRAMANDEFS}

.man.1 .man.3 .man.4 .man.5 .man.7 .man.8 .man.pre.1 .man.pre.4 .man.pre.5:
	${_MKTARGET_CREATE}
	rm -f ${.TARGET}
	${_X11MANTRANSFORMCMD} | ${X11TOOL_UNXCOMM} > ${.TARGET}.tmp
	if ${_X11SKIP_FALSE_POSITIVE_GREP_CMD}; then \
		echo "manual ${.TARGET} matches @.*@, probably missing updates" 1>&2; \
		false; \
	else \
		${MV} ${.TARGET}.tmp ${.TARGET}; \
	fi

##### Pull in related .mk logic
.include <bsd.clean.mk>
