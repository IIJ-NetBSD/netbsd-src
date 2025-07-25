#	$NetBSD: Makefile,v 1.36 2025/07/19 10:40:44 christos Exp $
#
#	$eterna: Makefile,v 1.30 2010/07/11 00:34:27 mrg Exp $
#
# berkeley (netbsd) makefile.  see Makefile.boot for other systems.

# compile-time options are:
#	NO_DEBUG		/* don't include debugging support */
#	NO_USER_SUPPORT		/* don't support /~user requests */
#	NO_CGIBIN_SUPPORT	/* don't support cgi-bin requests */
#	NO_DIRINDEX_SUPPORT	/* don't support directory indexing */
#	NO_DAEMON_MODE		/* don't support daemon mode */
#	NO_DYNAMIC_CONTENT	/* don't support dynamic content updates */
#	NO_SSL_SUPPORT		/* don't support ssl (https) */
#	DO_HTPASSWD		/* support .htpasswd files */
#	NO_LUA_SUPPORT		/* don't support Lua for dynamic content */
#	NO_BLOCKLIST_SUPPORT	/* don't support blocklist */
#
# other system specific defines:
#	HAVE_NBUTIL_H		/* netbsd compat is in <nbutil.h>
#				(don't forget to also enable -lnbutil)
#
# these are usually set via the "COPTS" variable, or some other method
# for setting CFLAGS relevant to your make, eg
#   % make COPTS="-DDO_HTPASSWD"

COPTS+=	-DDO_HTPASSWD
PROG=	bozohttpd
LINKS=	${BINDIR}/bozohttpd ${BINDIR}/httpd
MAN=	bozohttpd.8
MLINKS+=bozohttpd.8 httpd.8
SRCS=	bozohttpd.c ssl-bozo.c auth-bozo.c cgi-bozo.c daemon-bozo.c \
	tilde-luzah-bozo.c dir-index-bozo.c content-bozo.c lua-bozo.c
SRCS+=	main.c

COPTS.ssl-bozo.c+=-Wno-error=deprecated-declarations

LDADD=	-lblocklist -lcrypt -llua -lm
DPADD=	${LIBBLOCKLIST} ${LIBCRYPT} ${LIBLUA} ${LIBM}

WARNS?=	4
LINTFLAGS+=	-X 193	# statement not reached

.if defined(.OS.MAKE)
OPSYS=	${.OS.MAKE}
.else
OPSYS:=	${:!uname -s!:S/-//g:S/\///g}
.endif

.if ${OPSYS} == "QNX"
CPPFLAGS+=	-DHAVE_NBUTIL_H
LDADD+=		-lnbutil
.endif

LDADD+=	-lssl -lcrypto -lpthread
DPADD+=	${LIBSSL} ${LIBCRYPTO} ${LIBPTHREAD}

#
# Build release things.
#
NROFF?=		nroff

PREHTMLFROB=	sed \
		    -e 's/&/\&amp;/' \
		    -e 's/</\&lt;/' \
		    -e 's/>/\&gt;/'

HTMLFROB=	sed \
		    -e 's/\([MC] "[^"]*\)<dd>$$/\1<b>"<\/b><dd>/' \
		    -e 's/'"''"'/\&rdquo;/' \
		    -e 's/""/\&ldquo;/' \
		    -e 's/<a href="\.\.\/html[^>]*>\(.*\)<\/a>/\1/'

TXTFROB=	col -b

bozohttpd.8.html: bozohttpd.8
	$(PREHTMLFROB) $> | $(NROFF) -mdoc2html | $(HTMLFROB) > $@

bozohttpd.8.txt: bozohttpd.8
	$(NROFF) -mdoc -Tascii $> | $(TXTFROB) > $@

CLEANFILES+=	bozohttpd.8.html bozohttpd.8.txt

check:
	cd ${.CURDIR}/testsuite && ${MAKE} check

.if empty(BOZOVER)
BOZOVER!=	sed -n \
		    -e s/\"$$// -e \
		    's/\#define[ 	]*SERVER_SOFTWARE[ 	]*\"bozohttpd\///p'  ${.PARSEDIR}/bozohttpd.c
.endif

# Create a distfile: uses /tmp
BASE=bozohttpd-${BOZOVER}
TAR=${BASE}.tar
export-distfile:
	dir=`mktemp -d /tmp/bozo-export-XXXXXX`; \
	cd "$${dir}" || exit; \
	mkdir ${BASE}; \
	( cd ${BASE} || exit; \
	  cp -r "${.CURDIR}/." "."; \
	  find . -name .CVS | xargs rm -r; \
	); \
	pax -wf ${TAR} ${BASE}; \
	gzip -nc9 ${TAR} > ${TAR}.gz; \
	bzip2 -9 ${TAR}; \
	echo "Exported two files in $${dir}:"; \
	echo ${TAR}.gz; \
	echo ${TAR}.bz2

.include <bsd.prog.mk>
