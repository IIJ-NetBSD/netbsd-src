#	$NetBSD: Makefile,v 1.8 2024/07/01 01:35:53 christos Exp $

.include "../Makefile.inc"
.include "../Makefile.assym"

KMOD=	compat_linux

CPPFLAGS+=	-DSYSVSHM -DSYSVSEM -DSYSVMSG -DCOMPAT_LINUX

.PATH:	${S}/compat/linux/common
SRCS=	linux_blkio.c linux_cdrom.c linux_errno.c linux_exec.c
SRCS+=	linux_fdio.c linux_file.c linux_hdio.c linux_ioctl.c
SRCS+=	linux_ipc.c linux_misc.c linux_mtio.c linux_sched.c
SRCS+=	linux_sg.c linux_signal.c linux_signo.c linux_socket.c
SRCS+=	linux_sysctl.c linux_termios.c linux_time.c linux_mod.c
SRCS+=	linux_inotify.c linux_mqueue.c

.if ${MACHINE_CPU} == "aarch64"
CPPFLAGS+=	-DEXEC_ELF64
.PATH:	${S}/arch/aarch64/aarch64
.PATH:	${S}/compat/linux/arch/aarch64
SRCS+=	linux_machdep.c linux_syscalls.c linux_sysent.c linux_commons.c
SRCS+=	linux_exec_elf64.c
SRCS+=	linux_sigcode.c linux_syscall.c
.endif

.if ${MACHINE_ARCH} == "i386"
CPPFLAGS+=	-DEXEC_ELF32 -DEXEC_AOUT
.PATH:	${S}/arch/i386/i386
.PATH:	${S}/compat/linux/arch/i386
.PATH:	${S}/arch/x86/x86
SRCS+=	linux_machdep.c linux_syscalls.c linux_sysent.c linux_commons.c
SRCS+=	linux_ptrace.c linux_exec_machdep.c linux_exec_elf32.c
SRCS+=	linux_exec_aout.c linux_sigcode.S linux_syscall.c linux_trap.c
SRCS+=	linux_uselib.c
.endif

.if ${MACHINE_ARCH} == "x86_64"
CPPFLAGS+=	-DEXEC_ELF64
.PATH:	${S}/compat/linux/arch/amd64
.PATH:	${S}/arch/amd64/amd64
.PATH:	${S}/arch/x86/x86
SRCS+=	linux_machdep.c linux_syscalls.c linux_sysent.c linux_commons.c
SRCS+=	linux_exec_machdep.c linux_exec_elf64.c
SRCS+=	linux_sigcode.S linux_syscall.c linux_trap.c
.endif

WARNS=	3

.include <bsd.kmodule.mk>
