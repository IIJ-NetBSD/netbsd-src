#	$NetBSD: Makefile,v 1.1 2025/07/17 14:25:24 christos Exp $

# RCSid:
#	Id: Makefile,v 1.33 1998/11/11 11:53:53 sjg Exp
#
#	@(#) Copyright (c) 1994 Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that
#	the above copyright notice and this notice are
#	left intact.
#
#	Please send copies of changes and bug-fixes to:
#	sjg@quick.com.au
#

LIB=	crypto
USE_FORT?= yes	# cryptographic software
USE_SHLIBDIR=	yes
USE_FIPS=	no
#DBG=-g

.include <bsd.own.mk>
.include <bsd.shlib.mk>

CPPFLAGS+= -pthread
CPPFLAGS+= -Dlib${LIB} -I. -I${OPENSSLSRC}/crypto -I${OPENSSLSRC}
CPPFLAGS+= -I${OPENSSLSRC}/include -I${OPENSSLSRC}/crypto/include
CPPFLAGS+= -I${OPENSSLSRC}/crypto/asn1 -I${OPENSSLSRC}/crypto/evp
CPPFLAGS+= -I${OPENSSLSRC}/crypto/modes
CPPFLAGS+= -I${OPENSSLSRC}/../include

CRYPTODIST=	${NETBSDSRCDIR}/crypto
OPENSSLINC=	${OPENSSLSRC}/include/openssl

.include "${NETBSDSRCDIR}/crypto/Makefile.openssl"
.PATH: ${OPENSSLSRC} ${OPENSSLINC}
.PATH: ${OPENSSLSRC}/../include/openssl

.include "srcs.inc"
.include "${.CURDIR}/../libdefault/srcs.inc"
CPPFLAGS+= -I${.CURDIR}/../libdefault
CPPFLAGS+= -I${OPENSSLSRC}/providers/common/include
CPPFLAGS+= -I${OPENSSLSRC}/providers/fips/include
CPPFLAGS+= -I${OPENSSLSRC}/providers/implementations/include
.PATH: ${.CURDIR}/../libdefault
.include "${.CURDIR}/../libcommon/srcs.inc"
.PATH: ${.CURDIR}/../libcommon

AFLAGS+=-DELF
LIBDPLIBS+= crypt ${NETBSDSRCDIR}/lib/libcrypt
LIBDPLIBS+= pthread ${NETBSDSRCDIR}/lib/libpthread

OS_VERSION!= ${HOST_SH} ${NETBSDSRCDIR}/sys/conf/osrelease.sh

# XXX CFLAGS: While it would be nice to know which compiler flags
# XXX the library was built with, we don't want pathname information
# XXX for the host toolchain embedded in the image.
${SRCS}: buildinf.h
buildinf.h: Makefile
	@echo "#ifndef MK1MF_BUILD" >buildinf.h
	@echo "#define compiler_flags \"`${CC} -v 2>&1 | grep 'gcc version'`\"" >>buildinf.h
	@echo "#define PLATFORM \"NetBSD-${MACHINE_ARCH}\"" >>buildinf.h
	@echo "#define DATE \"NetBSD ${OS_VERSION}\"" >>buildinf.h
	@echo "#endif" >>buildinf.h

CLEANFILES+= buildinf.h

# This list is built from the contents of the include/openssl
# directory in the OpenSSL source distribution.
INCS+= \
aes.h \
asn1.h \
asn1err.h \
asn1t.h \
async.h \
asyncerr.h \
bio.h \
bioerr.h \
blowfish.h \
bn.h \
bnerr.h \
buffer.h \
buffererr.h \
camellia.h \
cast.h \
cmac.h \
cmp.h \
cmp_util.h \
cmperr.h \
cms.h \
cmserr.h \
comp.h \
comperr.h \
conf.h \
conf_api.h \
conferr.h \
configuration.h \
conftypes.h \
core.h \
core_dispatch.h \
core_names.h \
core_object.h \
crmf.h \
crmferr.h \
crypto.h \
cryptoerr.h \
cryptoerr_legacy.h \
ct.h \
cterr.h \
decoder.h \
decodererr.h \
des.h \
dh.h \
dherr.h \
dsa.h \
dsaerr.h \
dtls1.h \
e_os2.h \
e_ostime.h \
ebcdic.h \
ec.h \
ecdh.h \
ecdsa.h \
ecerr.h \
encoder.h \
encodererr.h \
engine.h \
engineerr.h \
err.h \
ess.h \
esserr.h \
evp.h \
evperr.h \
fips_names.h \
fipskey.h \
hmac.h \
http.h \
httperr.h \
indicator.h \
kdf.h \
kdferr.h \
lhash.h \
macros.h \
md2.h \
md4.h \
md5.h \
modes.h \
obj_mac.h \
objects.h \
objectserr.h \
ocsp.h \
ocsperr.h \
opensslconf.h \
opensslv.h \
ossl_typ.h \
param_build.h \
params.h \
pem.h \
pem2.h \
pemerr.h \
pkcs12.h \
pkcs12err.h \
pkcs7.h \
pkcs7err.h \
prov_ssl.h \
proverr.h \
provider.h \
quic.h \
rand.h \
randerr.h \
rc2.h \
rc4.h \
ripemd.h \
rsa.h \
rsaerr.h \
safestack.h \
seed.h \
self_test.h \
sha.h \
srp.h \
srtp.h \
ssl.h \
ssl2.h \
ssl3.h \
sslerr.h \
sslerr_legacy.h \
stack.h \
store.h \
storeerr.h \
symhacks.h \
tls1.h \
trace.h \
ts.h \
tserr.h \
txt_db.h \
types.h \
ui.h \
uierr.h \
whrlpool.h \
x509.h \
x509_acert.h \
x509_vfy.h \
x509err.h \
x509v3.h \
x509v3err.h

# IDEA - patented, but we install the header anyways
INCS+=	idea.h

# RC5 - patented, but we install the header anyways
INCS+=	rc5.h

# MDC2 - patented, but we install the header anyways
INCS+=	mdc2.h

.if (${USE_FIPS} != "no")
# FIPS
# This part is always included, because OpenSSL does not protect
# The FIPS include files
.PATH: ${OPENSSLSRC}/fips ${OPENSSLSRC}/fips/rand
INCS+=	fips.h fips_rand.h
.endif

.if (${USE_FIPS} != "no")
CPPFLAGS+=-DOPENSSL_FIPS
.PATH: ${OPENSSLSRC}/fips/aes
SRCS+=fips_aes_core.c fips_aes_selftest.c
.PATH: ${OPENSSLSRC}/fips/des
SRCS+=fips_des_enc.c fips_des_selftest.c fips_set_key.c
# asm/fips-dx86-elf.s
.PATH: ${OPENSSLSRC}/fips/dh
SRCS+=fips_dh_check.c fips_dh_gen.c fips_dh_key.c
.PATH: ${OPENSSLSRC}/fips/dsa
SRCS+=fips_dsa_ossl.c fips_dsa_gen.c fips_dsa_selftest.c
#.PATH: ${OPENSSLSRC}/fips/rand
SRCS+=fips_rand.c
.PATH: ${OPENSSLSRC}/fips/rsa
SRCS+=fips_rsa_eay.c fips_rsa_gen.c fips_rsa_selftest.c
SRCS+=fips_sha1dgst.c fips_sha1_selftest.c
# asm/sx86-elf.s
.PATH: ${OPENSSLSRC}/fips/sha1
#.PATH: ${OPENSSLSRC}/fips
SRCS+=fips.c fips_err_wrapper.c

SRCS+=rc5_skey.c i_skey.c mdc2dgst.c
.endif

COPTS.eng_padlock.c = -Wno-error=stack-protector
COPTS.bn_exp.c = -Wno-error=stack-protector

INCSDIR=/usr/include/openssl

VERSION_MAP=	crypto.map

PKGCONFIG=libcrypto
.include "${.CURDIR}/../../pkgconfig.mk"

# too much of a pain to generate opensslv.h / configuration.h / fipskey.h
# core_names.h
GENH = \
asn1.h \
asn1t.h \
bio.h \
cmp.h \
cms.h \
comp.h \
conf.h \
crmf.h \
crypto.h \
ct.h \
err.h \
ess.h \
lhash.h \
ocsp.h \
pkcs12.h \
pkcs7.h \
safestack.h \
srp.h \
ssl.h \
ui.h \
x509.h \
x509_acert.h \
x509_vfy.h \
x509v3.h

.for i in ${GENH}
$i: $i.in
	${_MKTARGET_CREATE}
	${HOST_SH} ${.CURDIR}/gen ${.ALLSRC} > ${.TARGET}
.endfor

.include <bsd.lib.mk>
