# $NetBSD: Makefile.inc,v 1.1 2025/01/15 15:30:47 christos Exp $

CPPFLAGS+= \
-DPACKAGE_VERSION=\"1.3.1\" \
-DPACKAGE_STRING=\"pam_u2f\ 1.3.1\" \
-DPACKAGE_BUGREPORT=\"https://github.com/Yubico/pam-u2f/issues\" \
-DPACKAGE_URL=\"https://developers.yubico.com/pam-u2f/\" \
-DPACKAGE=\"pam_u2f\" \
-DVERSION=\"1.3.1\" \
-DHAVE_STDIO_H=1 \
-DHAVE_STDLIB_H=1 \
-DHAVE_STRING_H=1 \
-DHAVE_INTTYPES_H=1 \
-DHAVE_STDINT_H=1 \
-DHAVE_STRINGS_H=1 \
-DHAVE_SYS_STAT_H=1 \
-DHAVE_SYS_TYPES_H=1 \
-DHAVE_UNISTD_H=1 \
-DHAVE_WCHAR_H=1 \
-DSTDC_HEADERS=1 \
-D_ALL_SOURCE=1 \
-D_DARWIN_C_SOURCE=1 \
-D_GNU_SOURCE=1 \
-D_HPUX_ALT_XOPEN_SOCKET_API=1 \
-D_NETBSD_SOURCE=1 \
-D_OPENBSD_SOURCE=1 \
-D_POSIX_PTHREAD_SEMANTICS=1 \
-D__STDC_WANT_IEC_60559_ATTRIBS_EXT__=1 \
-D__STDC_WANT_IEC_60559_BFP_EXT__=1 \
-D__STDC_WANT_IEC_60559_DFP_EXT__=1 \
-D__STDC_WANT_IEC_60559_EXT__=1 \
-D__STDC_WANT_IEC_60559_FUNCS_EXT__=1 \
-D__STDC_WANT_IEC_60559_TYPES_EXT__=1 \
-D__STDC_WANT_LIB_EXT2__=1 \
-D__STDC_WANT_MATH_SPEC_FUNCS__=1 \
-D_TANDEM_SOURCE=1 \
-D__EXTENSIONS__=1 \
-DHAVE_DLFCN_H=1 \
-DLT_OBJDIR=\".libs/\" \
-DHAVE_SECURITY_PAM_APPL_H=1 \
-DHAVE_SECURITY_PAM_MODULES_H=1 \
-DHAVE_SECURITY_OPENPAM_H=1 \
-DHAVE_LIBPAM=1 \
-DHAVE_OPENPAM_BORROW_CRED=1 \
-DOPENSSL_API_COMPAT=0x10100000L \
-DHAVE_STRLCPY=1
