include $(top_srcdir)/Makefile.top

EXTRA_DIST = .

# Source tarballs must not contain configure/build artifacts.
dist-hook:
	git clean -n -x -d | \
	        grep -v "Makefile.in$$" | \
	        sed -n "s|^Would remove \(.*\)|$(distdir)/\1|p" | \
	        xargs -I{} rm -rf "{}"

SUBDIRS = dyndb/driver dlzexternal/driver hooks/driver

if DNSRPS
SUBDIRS += rpz/testlib
endif

AM_CPPFLAGS +=			\
	$(LIBISC_CFLAGS) 	\
	$(LIBDNS_CFLAGS)

LDADD +=			\
	$(LIBISC_LIBS)		\
	$(LIBDNS_LIBS)

if HAVE_PERL
if HAVE_PYTHON
if HAVE_PYTEST

noinst_PROGRAMS =		\
	feature-test		\
	makejournal		\
	pipelined/pipequeries	\
	rndc/gencheck		\
	rpz/dnsrps

feature_test_CPPFLAGS =		\
	$(AM_CPPFLAGS)		\
	$(LIBDNS_CFLAGS)	\
	$(OPENSSL_CFLAGS)

feature_test_LDADD =		\
	$(LDADD)		\
	$(LIBDNS_LIBS)		\
	$(OPENSSL_LIBS)

makejournal_CPPFLAGS =		\
	$(AM_CPPFLAGS)		\
	$(LIBDNS_CFLAGS)

makejournal_LDADD =		\
	$(LDADD)		\
	$(LIBDNS_LIBS)

pipelined_pipequeries_CPPFLAGS =	\
	$(AM_CPPFLAGS)			\
	$(LIBDNS_CFLAGS)

pipelined_pipequeries_LDADD =	\
	$(LDADD)		\
	$(LIBDNS_LIBS)

rpz_dnsrps_CPPFLAGS =		\
	$(AM_CPPFLAGS)		\
	$(LIBDNS_CFLAGS)	\
	-DLIBRPZ_LIB_OPEN=\"$(abs_builddir)/rpz/testlib/.libs/libdummyrpz.so\"

rpz_dnsrps_LDADD =		\
	$(LDADD)		\
	$(LIBDNS_LIBS)		\
	$(DLOPEN_LIBS)

# Longer running tests are listed (and executed) first to take the most
# advantage of parallel execution.
TESTS =				\
	rpz			\
	rpzrecurse		\
	serve-stale		\
	timeouts		\
	upforwd			\
	acl			\
	additional		\
	addzone			\
	allow-query		\
	auth			\
	autosign		\
	builtin			\
	cacheclean		\
	camp			\
	case			\
	catz			\
	cds			\
	chain			\
	checkconf		\
	checkds			\
	checknames		\
	checkzone		\
	cookie			\
	cpu			\
	database		\
	dialup			\
	digdelv			\
	dispatch		\
	dlzexternal		\
	dns64			\
	dnssec			\
	dnstap			\
	doth			\
	dsdigest		\
	dyndb			\
	ecdsa			\
	eddsa			\
	ednscompliance		\
	emptyzones		\
	enginepkcs11		\
	filter-aaaa		\
	fetchlimit		\
	formerr			\
	forward			\
	geoip2			\
	glue			\
	idna			\
	include-multiplecfg	\
	inline			\
	integrity		\
	ixfr			\
	hooks			\
	host			\
	journal			\
	kasp			\
	keepalive		\
	keyfromlabel		\
	ksr			\
	legacy			\
	limits			\
	logfileconfig		\
	masterfile		\
	masterformat		\
	metadata		\
	mirror			\
	mkeys			\
	multisigner		\
	names			\
	notify			\
	nsec3			\
	nslookup		\
	nsupdate		\
	nzd2nzf			\
	padding			\
	pending			\
	proxy			\
	pipelined		\
	qmin			\
	query-source		\
	reclimit		\
	redirect		\
	resolver		\
	rndc			\
	rollover		\
	rootkeysentinel		\
	rpzextra		\
	rrchecker		\
	rrl			\
	rrsetorder		\
	rsabigexponent		\
	runtime			\
	sfcache			\
	shutdown		\
	smartsign		\
	sortlist		\
	spf			\
	staticstub		\
	statistics		\
	statschannel		\
	stress			\
	stub			\
	synthfromdnssec		\
	tcp			\
	tools			\
	transport-acl		\
	tsig			\
	tsiggss			\
	ttl			\
	unknown			\
	verify			\
	views			\
	wildcard		\
	xfer			\
	xferquota		\
	zero			\
	zonechecks

else !HAVE_PYTEST
check:
	echo pytest is not available, no tests were ran
	exit 1
endif !HAVE_PYTEST
else !HAVE_PYTHON
check:
	echo Python is not available, no tests were ran
	exit 1
endif !HAVE_PYTHON
else !HAVE_PERL
check:
	echo Perl is not available, no tests were ran
	exit 1
endif !HAVE_PERL

LOG_DRIVER_V = $(LOG_DRIVER_V_@AM_V@)
LOG_DRIVER_V_ = $(LOG_DRIVER_V_@AM_DEFAULT_V@)
LOG_DRIVER_V_0 = --verbose no
LOG_DRIVER_V_1 = --verbose yes

LOG_DRIVER = $(srcdir)/custom-test-driver
AM_LOG_DRIVER_FLAGS = $(LOG_DRIVER_V)

LOG_COMPILER = $(srcdir)/run.sh

test-local: check

clean-local::
	-find -L . -mindepth 1 -maxdepth 1 -type d -name "*_*" -and -not -name "_common" -exec rm -rf {} \;
