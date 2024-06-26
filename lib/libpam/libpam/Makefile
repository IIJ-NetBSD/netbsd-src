# $NetBSD: Makefile,v 1.32 2024/04/05 01:16:00 christos Exp $
#-
# Copyright (c) 1998 Juniper Networks, Inc.
# All rights reserved.
# Copyright (c) 2002 Networks Associates Technology, Inc.
# All rights reserved.
#
# Portions of this software was developed for the FreeBSD Project by
# ThinkSec AS and NAI Labs, the Security Research Division of Network
# Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
# ("CBOSS"), as part of the DARPA CHATS research program.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD: src/lib/libpam/libpam/Makefile,v 1.51 2004/10/24 15:32:32 ru Exp $

NOPROFILE=	# Don't build a profiling library

.include <bsd.own.mk>

.if ${MACHINE} == "vax"
# On VAX everything is PIC so we don't need to build a separate _pic.a
# libraries so we set MKPICLIB.vax=no in bsd.own.mk to save time and space.
# But in libpam.a we include the static modules which we don't want in the
# shared library, so we force the build system to build a separate _pic.a.
MKPICLIB=yes
.endif

SHLIB_MINOR=	1

DIST=   ${NETBSDSRCDIR}/external/bsd/openpam/dist
LIB=	pam

WARNS=		6
LINTFLAGS+=	-w

CPPFLAGS+= -DLIB_MAJ=${SHLIB_MAJOR} -DHAVE_CONFIG_H -I${.CURDIR}
#CPPFLAGS+= -DOPENPAM_DEBUG

SRCS=	openpam_borrow_cred.c \
	openpam_check_owner_perms.c \
	openpam_configure.c \
	openpam_constants.c \
	openpam_dispatch.c \
	openpam_dynamic.c \
	openpam_features.c \
	openpam_findenv.c \
	openpam_free_data.c \
	openpam_free_envlist.c \
	openpam_get_feature.c \
	openpam_get_option.c \
	openpam_load.c \
	openpam_log.c \
	openpam_nullconv.c \
	openpam_readline.c \
	openpam_readlinev.c \
	openpam_readword.c \
	openpam_restore_cred.c \
	openpam_set_feature.c \
	openpam_set_option.c \
	openpam_straddch.c \
	openpam_strlset.c \
	openpam_subst.c \
	openpam_ttyconv.c \
	pam_acct_mgmt.c \
	pam_authenticate.c \
	pam_chauthtok.c \
	pam_close_session.c \
	pam_end.c \
	pam_error.c \
	pam_get_authtok.c \
	pam_get_data.c \
	pam_get_item.c \
	pam_get_user.c \
	pam_getenv.c \
	pam_getenvlist.c \
	pam_info.c \
	pam_open_session.c \
	pam_prompt.c \
	pam_putenv.c \
	pam_set_data.c \
	pam_set_item.c \
	pam_setcred.c \
	pam_setenv.c \
	pam_start.c \
	pam_strerror.c \
	pam_verror.c \
	pam_vinfo.c \
	pam_vprompt.c
# Local additions
SRCS+=	pam_debug_log.c

MAN=	\
	openpam.3 \
	openpam_borrow_cred.3 \
	openpam_free_data.3 \
	openpam_free_envlist.3 \
	openpam_get_feature.3 \
	openpam_get_option.3 \
	openpam_log.3 \
	openpam_nullconv.3 \
	openpam_readline.3 \
	openpam_readlinev.3 \
	openpam_readword.3 \
	openpam_restore_cred.3 \
	openpam_set_feature.3 \
	openpam_set_option.3 \
	openpam_straddch.3 \
	openpam_subst.3 \
	openpam_ttyconv.3 \
	pam.3 \
	pam.conf.5 \
	pam_acct_mgmt.3 \
	pam_authenticate.3 \
	pam_chauthtok.3 \
	pam_close_session.3 \
	pam_conv.3 \
	pam_end.3 \
	pam_error.3 \
	pam_get_authtok.3 \
	pam_get_data.3 \
	pam_get_item.3 \
	pam_get_user.3 \
	pam_getenv.3 \
	pam_getenvlist.3 \
	pam_info.3 \
	pam_open_session.3 \
	pam_prompt.3 \
	pam_putenv.3 \
	pam_set_data.3 \
	pam_set_item.3 \
	pam_setcred.3 \
	pam_setenv.3 \
	pam_sm_acct_mgmt.3 \
	pam_sm_authenticate.3 \
	pam_sm_chauthtok.3 \
	pam_sm_close_session.3 \
	pam_sm_open_session.3 \
	pam_sm_setcred.3 \
	pam_start.3 \
	pam_strerror.3 \
	pam_verror.3 \
	pam_vinfo.3 \
	pam_vprompt.3

INCS=		security/openpam.h \
		security/openpam_attr.h \
		security/openpam_version.h \
		security/pam_appl.h \
		security/pam_constants.h \
		security/pam_modules.h \
		security/pam_types.h \

INCS+=		security/pam_mod_misc.h
INCSDIR=/usr/include

CLEANFILES+= openpam_static.o openpam_static_modules.o
STOBJS+= openpam_static_modules.o

# Static module support.  These go ONLY in the static libpam.a
STATIC_MODULES=	pam_chroot pam_deny pam_echo pam_exec pam_ftpusers pam_group \
    pam_guest pam_lastlog pam_login_access pam_nologin pam_permit pam_radius \
    pam_rhosts pam_rootok pam_securetty pam_self pam_unix

.if (${MKKERBEROS} != "no")
STATIC_MODULES+= pam_afslog pam_krb5 pam_ksu
.endif

.if (${MKSKEY} != "no")
STATIC_MODULES+= pam_skey
.endif

STATIC_MODULES+= pam_ssh
 
MODULE_DIR=../modules
# XXX
.ifdef _CURDIR
PAM_DIR=${_CURDIR}
.else
PAM_DIR=${.CURDIR}
.endif
.for MOD in ${STATIC_MODULES}
MODOBJDIR!=	cd ${PAM_DIR}/${MODULE_DIR}/${MOD} && ${PRINTOBJDIR}
STATIC_MODULE_LIBS:= ${STATIC_MODULE_LIBS} ${MODOBJDIR}/${MOD}.a
.endfor

# XXX Using ${.ALLSRC} in the ld command causes openpam_static.c to be
# XXX used.  Why??!!  --thorpej
openpam_static_modules.o: openpam_static.o ${STATIC_MODULE_LIBS}
	${CC} ${LDFLAGS} -nostdlib -o ${.TARGET} -r -Wl,--whole-archive \
	    openpam_static.o ${STATIC_MODULE_LIBS}

CWARNFLAGS.clang+=	-Wno-error=tautological-pointer-compare
COPTS.openpam_dynamic.c+=	${CC_WNO_CAST_FUNCTION_TYPE}

.include <bsd.lib.mk>
.PATH.h: ${DIST}/include
.PATH.c: ${DIST}/lib/libpam
.PATH.3: ${DIST}/doc/man
.PATH.5: ${DIST}/doc/man
