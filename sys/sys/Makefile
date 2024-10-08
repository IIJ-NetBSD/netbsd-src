#	$NetBSD: Makefile,v 1.185 2024/10/08 22:53:20 christos Exp $

.include <bsd.own.mk>

INCSDIR= /usr/include/sys

INCS=	acct.h acl.h agpio.h aio.h ansi.h aout_mids.h ataio.h atomic.h \
	audioio.h \
	bitops.h bootblock.h bswap.h buf.h \
	callback.h callout.h cdbr.h cdefs.h cdefs_aout.h \
	cdefs_elf.h cdio.h chio.h clock.h clockctl.h \
	common_ansi.h common_int_const.h common_int_fmtio.h \
	common_int_limits.h common_int_mwgwtypes.h common_int_types.h \
	common_limits.h common_lock.h common_wchar_limits.h \
	condvar.h conf.h container_of.h core.h \
	cpufreq.h cpuio.h ctype_bits.h ctype_inline.h \
	device.h device_if.h \
	dir.h dirent.h \
	disk.h disklabel.h disklabel_acorn.h disklabel_gpt.h disklabel_rdb.h \
	dkbad.h dkio.h dkstat.h domain.h drvctlio.h dvdio.h \
	efiio.h endian.h envsys.h errno.h evcnt.h event.h eventfd.h exec.h \
	exec_aout.h exec_coff.h exec_ecoff.h exec_elf.h exec_script.h \
	extattr.h extent.h \
	fault.h \
	fcntl.h fd_set.h fdio.h featuretest.h file.h filedesc.h filio.h \
	flashio.h float_ieee754.h fstypes.h futex.h gcq.h gmon.h gpio.h hash.h \
	hook.h idtype.h ieee754.h intr.h intrio.h inttypes.h ioccom.h ioctl.h \
	ioctl_compat.h iostat.h ipc.h ipmi.h \
	joystick.h \
	kcore.h kcov.h kcpuset.h kgdb.h kmem.h ksem.h ksyms.h ktrace.h \
	localcount.h localedef.h lock.h lockf.h lua.h lwp.h lwpctl.h \
	malloc.h mallocvar.h mbuf.h md4.h md5.h midiio.h \
	mman.h module.h mount.h mqueue.h msg.h msgbuf.h mtio.h mutex.h \
	namei.h null.h \
	param.h pcu.h pipe.h pmf.h poll.h pool.h power.h proc.h \
	protosw.h pset.h psref.h ptrace.h ptree.h \
	queue.h quota.h quotactl.h \
	radioio.h radixtree.h random.h ras.h rbtree.h reboot.h resource.h \
	resourcevar.h rmd160.h rnd.h rndio.h rwlock.h \
	scanio.h sched.h scsiio.h sdt.h select.h selinfo.h sem.h semaphore.h \
	sha1.h sha2.h sha3.h shm.h siginfo.h signal.h signalvar.h sigtypes.h \
	sleepq.h socket.h \
	socketvar.h sockio.h spawn.h specificdata.h stat.h \
	statvfs.h syscall.h syscallargs.h sysctl.h stdalign.h stdarg.h \
	stdbool.h stddef.h stdint.h swap.h syncobj.h syslimits.h syslog.h \
	tape.h termios.h time.h timeb.h timepps.h timerfd.h times.h timespec.h \
	timex.h tls.h trace.h tree.h tty.h ttychars.h ttycom.h \
	ttydefaults.h ttydev.h types.h \
	ucontext.h ucred.h uio.h un.h unistd.h unpcb.h utsname.h uuid.h \
	verified_exec.h videoio.h vmmeter.h vnode.h vnode_if.h vnode_impl.h \
	wait.h wapbl.h wapbl_replay.h wchan.h wdog.h xattr.h

INCSYMLINKS=\
	sys/cdbr.h /usr/include/cdbr.h \
	sys/exec_elf.h /usr/include/elf.h \
	sys/fcntl.h /usr/include/fcntl.h \
	sys/md4.h /usr/include/md4.h \
	sys/md5.h /usr/include/md5.h \
	sys/poll.h /usr/include/poll.h \
	sys/rmd160.h /usr/include/rmd160.h \
	sys/sha1.h /usr/include/sha1.h \
	sys/sha2.h /usr/include/sha2.h \
	sys/stdalign.h /usr/include/stdalign.h \
	sys/stdarg.h /usr/include/stdarg.h \
	sys/stdbool.h /usr/include/stdbool.h \
	sys/stddef.h /usr/include/stddef.h \
	sys/stdint.h /usr/include/stdint.h \
	sys/syslog.h /usr/include/syslog.h \
	sys/termios.h /usr/include/termios.h

INCSYMLINKS+=	../soundcard.h ${INCSDIR}/soundcard.h

namei: namei.src gennameih.awk
	${TOOL_AWK} -f gennameih.awk < namei.src

device_calls.h: ${.CURDIR}/../kern/device_calls
	echo "${TOOL_AWK} -f ${.CURDIR}/../kern/gendevcalls.awk \
	    ${.CURDIR}/../kern/device_calls > ${.CURDIR}/device_calls.h"
	${TOOL_AWK} -f ${.CURDIR}/../kern/gendevcalls.awk \
	    ${.CURDIR}/../kern/device_calls > ${.CURDIR}/device_calls.h

.include <bsd.kinc.mk>
