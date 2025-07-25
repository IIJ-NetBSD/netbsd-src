# $NetBSD: Makefile,v 1.96 2025/07/17 19:50:40 kre Exp $

NOMAN=		# defined

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/kernel

TESTS_SUBDIRS+=	kqueue
TESTS_C+=	t_cloexec
TESTS_C+=	t_clofork
#TESTS_C+=	t_epoll
TESTS_C+=	t_execregs
TESTS_C+=	t_fcntl
.if ${MKRUMP} != "no"
TESTS_C+=	t_fdrestart
.endif
TESTS_C+=	t_kauth_pr_47598
TESTS_C+=	t_ksem
TESTS_C+=	t_lock
TESTS_C+=	t_lockf
TESTS_C+=	t_memfd_create
TESTS_C+=	t_mqueue
TESTS_C+=	t_nanosleep
TESTS_C+=	t_open_pr_57260
TESTS_C+=	t_proccwd
TESTS_C+=	t_pty
TESTS_C+=	t_semtimedop
TESTS_C+=	t_signal_and_sp
TESTS_C+=	t_sysv
TESTS_C+=	t_subr_prf
TESTS_C+=	t_sysctl
TESTS_C+=	t_timeleft
.if ${MKRUMP} != "no"
TESTS_C+=	t_unmount
.endif
TESTS_C+=	t_zombie

TESTS_SH=	t_fexecve
TESTS_SH+=	t_fpufork
TESTS_SH+=	t_interp
TESTS_SH+=	t_magic_symlinks
TESTS_SH+=	t_nointerpreter
TESTS_SH+=	t_origin
TESTS_SH+=	t_procpath
TESTS_SH+=	t_ps_strings
.if ${MKKMOD} != "no"
TESTS_SH+=	t_setjmp
.endif
.if ${MKRUMP} != "no"
TESTS_SH+=	t_simplehook
.endif
TESTS_SH+=	t_trapsignal
TESTS_SH+=	t_umountstress
TESTS_SH+=	t_umount

BINDIR=		${TESTSDIR}
PROGS+=		h_cloexec
PROGS+=		h_execregs
PROGS+=		h_execsp_dynamic
PROGS+=		h_execsp_static
PROGS+=		h_fexecve
PROGS+=		h_fpufork
PROGS+=		h_getprocpath
PROGS+=		h_ps_strings1
PROGS+=		h_ps_strings2
PROGS+=		h_segv

SCRIPTSDIR=	${TESTSDIR}
SCRIPTS=	h_interpreter.sh

LDADD.t_fdrestart+=	-lpthread
LDADD.t_fdrestart+=	-lrumpnet_local
LDADD.t_fdrestart+=	-lrumpnet_net
LDADD.t_fdrestart+=	-lrumpnet_netinet
LDADD.t_fdrestart+=	-lrumpnet
LDADD.t_fdrestart+=	${LIBRUMPBASE}

LDADD.t_mqueue+= -lrt

LDADD.t_unmount+=	-lrumpfs_tmpfs
LDADD.t_unmount+=	${LIBRUMPBASE}


.if (${MKRUMP} != "no") && !defined(BSD_MK_COMPAT_FILE)
TESTS_SUBDIRS+=	tty

TESTS_C+=	t_extattrctl
TESTS_C+=	t_filedesc
TESTS_C+=	t_rnd
LDADD.t_extattrctl+= ${LIBRUMPBASE}
LDADD.t_filedesc+=  ${LDADD.t_rnd}
LDADD.t_rnd+=  -lrumpdev_rnd -lrumpdev ${LIBRUMPBASE}

.endif

LDADD.t_timeleft+=	-lpthread

CPPFLAGS+=	-D_KERNTYPES
CPPFLAGS.t_unmount.c+=	-D_KMEMUSER -D__EXPOSE_MOUNT

.if exists(${.PARSEDIR}/arch/${MACHINE_ARCH})
ARCHDIR:=		${.PARSEDIR}/arch/${MACHINE_ARCH}
.else
ARCHDIR:=		${.PARSEDIR}/arch/${MACHINE_CPU}
.endif
.PATH:			${ARCHDIR}

.if exists(${ARCHDIR}/stack_pointer.h)
CPPFLAGS.t_signal_and_sp.c+=	-I${ARCHDIR} -DHAVE_STACK_POINTER_H
.endif

SRCS.t_signal_and_sp+=		t_signal_and_sp.c
.if exists(${ARCHDIR}/contextspfunc.S)
CPPFLAGS.t_signal_and_sp.c+=	-DHAVE_CONTEXTSPFUNC
SRCS.t_signal_and_sp+=		contextspfunc.S
.endif
.if exists(${ARCHDIR}/signalsphandler.S)
CPPFLAGS.t_signal_and_sp.c+=	-DHAVE_SIGNALSPHANDLER
SRCS.t_signal_and_sp+=		signalsphandler.S
.endif
.if exists(${ARCHDIR}/threadspfunc.S)
CPPFLAGS.t_signal_and_sp.c+=	-DHAVE_THREADSPFUNC
SRCS.t_signal_and_sp+=		threadspfunc.S
LDADD.t_signal_and_sp+=		-pthread
.endif

SRCS.h_execsp_dynamic+=		h_execsp.c
SRCS.h_execsp_static+=		h_execsp.c
.if exists(${ARCHDIR}/execsp.S)
CPPFLAGS.t_signal_and_sp.c+=	-DHAVE_EXECSP
SRCS.h_execsp_dynamic+=		execsp.S
SRCS.h_execsp_static+=		execsp.S
LDFLAGS.h_execsp_dynamic+=	-Wl,-e,execsp_start
LDFLAGS.h_execsp_static+=	-Wl,-e,execsp_start
.else
CPPFLAGS.h_execsp.c+=		-Dexecsp_main=main
.endif
LDSTATIC.h_execsp_static=	-static

.if ${MACHINE_CPU} == "mips"
CPPFLAGS.contextspfunc.S+=	-I${NETBSDSRCDIR}/lib/libc/arch/mips
CPPFLAGS.execsp.S+=		-I${NETBSDSRCDIR}/lib/libc/arch/mips
CPPFLAGS.signalsphandler.S+=	-I${NETBSDSRCDIR}/lib/libc/arch/mips
.endif

.PATH:			${NETBSDSRCDIR}/sys/kern
TESTS_C+=		t_extent
SRCS.t_extent=		t_extent.c subr_extent.c
CPPFLAGS.t_extent.c=	-D_EXTENT_TESTING -D__POOL_EXPOSE -D_KERNTYPES
CPPFLAGS.subr_extent.c=	-D_EXTENT_TESTING -D__POOL_EXPOSE -D_KERNTYPES

TESTS_C+=		t_time_arith
SRCS.t_time_arith=	t_time_arith.c subr_time_arith.c
CPPFLAGS.t_time_arith.c+=	-I${NETBSDSRCDIR}/sys -D_TIME_TESTING
CPPFLAGS.subr_time_arith.c+=	-I${NETBSDSRCDIR}/sys -D_TIME_TESTING
LDADD.t_time_arith+=	-lutil

t_subr_prf.c: gen_t_subr_prf ${NETBSDSRCDIR}/sys/kern/subr_prf.c
	${HOST_SH} ${.ALLSRC} ${.TARGET}
.if ${MKSANITIZER:Uno} == "yes"
	# These symbols will be redefined by MKSANITIZER
	${TOOL_SED} -i '/undef .*printf/d' ${.TARGET}
.endif

CPPFLAGS.t_subr_prf.c=	-Wno-pointer-sign	# XXX platform vs kernel SHA2

# Test explicitly tests failure modes.
CPPFLAGS.t_subr_prf.c+=	${CC_WNO_FORMAT_TRUNCATION}

SANITIZER_RENAME_CLASSES+=		t_subr_prf
SANITIZER_RENAME_FILES.t_subr_prf+=	t_subr_prf.c
SANITIZER_RENAME_SYMBOL.t_subr_prf+=	snprintf
SANITIZER_RENAME_SYMBOL.t_subr_prf+=	vsnprintf
SANITIZER_RENAME_SYMBOL.t_subr_prf+=	sprintf
SANITIZER_RENAME_SYMBOL.t_subr_prf+=	vsnprintf
SANITIZER_RENAME_SYMBOL.t_subr_prf+=	vasprintf

CLEANFILES+=	t_subr_prf.c

LDADD.h_segv+=	-lm

.if exists(arch/${MACHINE_ARCH}/execregs.h)
CPPFLAGS.t_execregs.c+=	-I${ARCHDIR}
CPPFLAGS.t_execregs.c+=	-I${.CURDIR}/../lib/libc/gen # isqemu.h
CPPFLAGS.t_execregs.c+=	-DHAVE_EXECREGS_TEST
SRCS.t_execregs+=	t_execregs.c
SRCS.t_execregs+=	execregs.c
LDSTATIC.h_execregs=	-static
LDFLAGS.h_execregs+=	-Wl,-e,execregs_start
.else
SRCS.h_execregs=	h_execregs_unimpl.c
.endif

.if ${MKKMOD} != "no"
SUBDIR+=	setjmp_tester
.endif

.include <bsd.test.mk>
