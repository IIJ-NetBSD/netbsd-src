/*	$NetBSD: pthread_cancelstub.c,v 1.51 2025/04/04 20:53:38 riastradh Exp $	*/

/*-
 * Copyright (c) 2002, 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nathan J. Williams and Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Disable namespace mangling, Fortification is useless here anyway. */
#undef _FORTIFY_SOURCE

#include <sys/cdefs.h>
__RCSID("$NetBSD: pthread_cancelstub.c,v 1.51 2025/04/04 20:53:38 riastradh Exp $");

/* Need to use libc-private names for atomic operations. */
#include "../../common/lib/libc/atomic/atomic_op_namespace.h"

#ifndef lint


/*
 * This is necessary because the names are always weak (they are not
 * POSIX functions).
 */
#define	fsync_range	_fsync_range
#define	pollts		_pollts

/*
 * XXX this is necessary to get the prototypes for the __sigsuspend14
 * XXX and __msync13 internal names, instead of the application-visible
 * XXX sigsuspend and msync names. It's kind of gross, but we're pretty
 * XXX intimate with libc already.
 */
#define __LIBC12_SOURCE__

#include <sys/msg.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>

#include <signal.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/resource.h>

#include <compat/sys/mman.h>
#include <compat/sys/poll.h>
#include <compat/sys/select.h>
#include <compat/sys/event.h>
#include <compat/sys/wait.h>
#include <compat/sys/resource.h>
#include <compat/include/aio.h>
#include <compat/include/mqueue.h>
#include <compat/include/signal.h>
#include <compat/include/time.h>

#include "pthread.h"
#include "pthread_int.h"
#include "reentrant.h"

#define	atomic_load_relaxed(p)						      \
	atomic_load_explicit(p, memory_order_relaxed)

int	pthread__cancel_stub_binder;

/*
 * Provide declarations for the underlying libc syscall stubs.  These
 * _sys_* functions are symbols defined by libc which invoke the system
 * call, without testing for cancellation.  Below, we define non-_sys_*
 * wrappers which surround calls to _sys_* by the equivalent of
 * pthread_testcancel().  Both libc and libpthread define the
 * non-_sys_* wrappers, but they are weak in libc and strong in
 * libpthread, so programs linked against both will get the libpthread
 * wrappers that test for cancellation.
 */
__typeof(accept) _sys_accept;
__typeof(__aio_suspend50) _sys___aio_suspend50;
__typeof(clock_nanosleep) _sys_clock_nanosleep;
__typeof(close) _sys_close;
__typeof(connect) _sys_connect;
__typeof(fcntl) _sys_fcntl;
__typeof(fdatasync) _sys_fdatasync;
__typeof(fsync) _sys_fsync;
__typeof(fsync_range) _sys_fsync_range;
__typeof(__kevent100) _sys___kevent100;
__typeof(mq_receive) _sys_mq_receive;
__typeof(mq_send) _sys_mq_send;
__typeof(__mq_timedreceive50) _sys___mq_timedreceive50;
__typeof(__mq_timedsend50) _sys___mq_timedsend50;
__typeof(msgrcv) _sys_msgrcv;
__typeof(msgsnd) _sys_msgsnd;
__typeof(__msync13) _sys___msync13;
__typeof(__nanosleep50) _sys___nanosleep50;
__typeof(open) _sys_open;
__typeof(openat) _sys_openat;
__typeof(paccept) _sys_paccept;
__typeof(poll) _sys_poll;
__typeof(__pollts50) _sys___pollts50;
__typeof(pread) _sys_pread;
__typeof(__pselect50) _sys___pselect50;
__typeof(pwrite) _sys_pwrite;
__typeof(read) _sys_read;
__typeof(readv) _sys_readv;
__typeof(recvfrom) _sys_recvfrom;
__typeof(recvmmsg) _sys_recvmmsg;
__typeof(recvmsg) _sys_recvmsg;
__typeof(__select50) _sys___select50;
__typeof(sendmmsg) _sys_sendmmsg;
__typeof(sendmsg) _sys_sendmsg;
__typeof(sendto) _sys_sendto;
__typeof(__sigsuspend14) _sys___sigsuspend14;
__typeof(__wait450) _sys___wait450;
__typeof(write) _sys_write;
__typeof(writev) _sys_writev;

#define TESTCANCEL(id) 	do {						\
	if (__predict_true(!__uselibcstub) &&				\
	    __predict_false(atomic_load_relaxed(&(id)->pt_cancel) &	\
		PT_CANCEL_CANCELLED)) {					\
		membar_acquire();					\
		pthread__cancelled();					\
	}								\
	} while (0)


int
accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_accept(s, addr, addrlen);
	TESTCANCEL(self);

	return retval;
}

int
accept4(int s, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_paccept(s, addr, addrlen, NULL, flags);
	TESTCANCEL(self);

	return retval;
}

int
__aio_suspend50(const struct aiocb * const list[], int nent,
    const struct timespec *timeout)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___aio_suspend50(list, nent, timeout);
	TESTCANCEL(self);

	return retval;
}

int
clock_nanosleep(clockid_t clock_id, int flags,
    const struct timespec *rqtp, struct timespec *rmtp)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_clock_nanosleep(clock_id, flags, rqtp, rmtp);
	TESTCANCEL(self);

	return retval;
}

int
close(int d)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_close(d);
	TESTCANCEL(self);

	return retval;
}

int
connect(int s, const struct sockaddr *addr, socklen_t namelen)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_connect(s, addr, namelen);
	TESTCANCEL(self);

	return retval;
}

int
fcntl(int fd, int cmd, ...)
{
	int retval;
	pthread_t self;
	va_list ap;

	self = pthread__self();
	TESTCANCEL(self);
	va_start(ap, cmd);
	retval = _sys_fcntl(fd, cmd, va_arg(ap, void *));
	va_end(ap);
	TESTCANCEL(self);

	return retval;
}

int
fdatasync(int d)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_fdatasync(d);
	TESTCANCEL(self);

	return retval;
}

int
fsync(int d)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_fsync(d);
	TESTCANCEL(self);

	return retval;
}

int
fsync_range(int d, int f, off_t s, off_t e)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_fsync_range(d, f, s, e);
	TESTCANCEL(self);

	return retval;
}

int
__kevent100(int fd, const struct kevent *ev, size_t nev, struct kevent *rev,
    size_t nrev, const struct timespec *ts)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___kevent100(fd, ev, nev, rev, nrev, ts);
	TESTCANCEL(self);

	return retval;
}

ssize_t
mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
	TESTCANCEL(self);

	return retval;
}

int
mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
	TESTCANCEL(self);

	return retval;
}

ssize_t
__mq_timedreceive50(mqd_t mqdes, char *msg_ptr, size_t msg_len,
    unsigned *msg_prio, const struct timespec *abst)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___mq_timedreceive50(mqdes, msg_ptr, msg_len, msg_prio,
	    abst);
	TESTCANCEL(self);

	return retval;
}

int
__mq_timedsend50(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
    unsigned msg_prio, const struct timespec *abst)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___mq_timedsend50(mqdes, msg_ptr, msg_len, msg_prio,
	    abst);
	TESTCANCEL(self);

	return retval;
}

ssize_t
msgrcv(int msgid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_msgrcv(msgid, msgp, msgsz, msgtyp, msgflg);
	TESTCANCEL(self);

	return retval;
}

int
msgsnd(int msgid, const void *msgp, size_t msgsz, int msgflg)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_msgsnd(msgid, msgp, msgsz, msgflg);
	TESTCANCEL(self);

	return retval;
}

int
__msync13(void *addr, size_t len, int flags)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___msync13(addr, len, flags);
	TESTCANCEL(self);

	return retval;
}

int
__nanosleep50(const struct timespec *rqtp, struct timespec *rmtp)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	/*
	 * For now, just nanosleep.  In the future, maybe pass a ucontext_t
	 * to _lwp_nanosleep() and allow it to recycle our kernel stack.
	 */
	retval = _sys___nanosleep50(rqtp, rmtp);
	TESTCANCEL(self);

	return retval;
}

int
open(const char *path, int flags, ...)
{
	int retval;
	pthread_t self;
	va_list ap;

	self = pthread__self();
	TESTCANCEL(self);
	va_start(ap, flags);
	retval = _sys_open(path, flags, va_arg(ap, mode_t));
	va_end(ap);
	TESTCANCEL(self);

	return retval;
}

int
openat(int fd, const char *path, int flags, ...)
{
	int retval;
	pthread_t self;
	va_list ap;

	self = pthread__self();
	TESTCANCEL(self);
	va_start(ap, flags);
	retval = _sys_openat(fd, path, flags, va_arg(ap, mode_t));
	va_end(ap);
	TESTCANCEL(self);

	return retval;
}

int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_poll(fds, nfds, timeout);
	TESTCANCEL(self);

	return retval;
}

int
__pollts50(struct pollfd *fds, nfds_t nfds, const struct timespec *ts,
    const sigset_t *sigmask)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___pollts50(fds, nfds, ts, sigmask);
	TESTCANCEL(self);

	return retval;
}

ssize_t
pread(int d, void *buf, size_t nbytes, off_t offset)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_pread(d, buf, nbytes, offset);
	TESTCANCEL(self);

	return retval;
}

int
__pselect50(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    const struct timespec *timeout, const sigset_t *sigmask)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___pselect50(nfds, readfds, writefds, exceptfds, timeout,
	    sigmask);
	TESTCANCEL(self);

	return retval;
}

ssize_t
pwrite(int d, const void *buf, size_t nbytes, off_t offset)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_pwrite(d, buf, nbytes, offset);
	TESTCANCEL(self);

	return retval;
}

ssize_t
read(int d, void *buf, size_t nbytes)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_read(d, buf, nbytes);
	TESTCANCEL(self);

	return retval;
}

ssize_t
readv(int d, const struct iovec *iov, int iovcnt)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_readv(d, iov, iovcnt);
	TESTCANCEL(self);

	return retval;
}

ssize_t
recvfrom(int s, void * restrict buf, size_t len, int flags,
    struct sockaddr * restrict from, socklen_t * restrict fromlen)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_recvfrom(s, buf, len, flags, from, fromlen);
	TESTCANCEL(self);

	return retval;
}

int
recvmmsg(int s, struct mmsghdr *mmsg, unsigned int vlen,
    unsigned int flags, struct timespec *timeout)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_recvmmsg(s, mmsg, vlen, flags, timeout);
	TESTCANCEL(self);

	return retval;
}

ssize_t
recvmsg(int s, struct msghdr *msg, int flags)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_recvmsg(s, msg, flags);
	TESTCANCEL(self);

	return retval;
}

int
__select50(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___select50(nfds, readfds, writefds, exceptfds, timeout);
	TESTCANCEL(self);

	return retval;
}

int
sendmmsg(int s, struct mmsghdr *mmsg, unsigned int vlen,
    unsigned int flags)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_sendmmsg(s, mmsg, vlen, flags);
	TESTCANCEL(self);

	return retval;
}

ssize_t
sendmsg(int s, const struct msghdr *msg, int flags)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_sendmsg(s, msg, flags);
	TESTCANCEL(self);

	return retval;
}

ssize_t
sendto(int s, const void *msg, size_t len, int flags,
    const struct sockaddr *to, socklen_t tolen)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_sendto(s, msg, len, flags, to, tolen);
	TESTCANCEL(self);

	return retval;
}

int
__sigsuspend14(const sigset_t *sigmask)
{
	pthread_t self;
	int retval;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___sigsuspend14(sigmask);
	TESTCANCEL(self);

	return retval;
}

int
__sigtimedwait50(const sigset_t * restrict set, siginfo_t * restrict info,
    const struct timespec * restrict timeout)
{
	pthread_t self;
	int retval;
	struct timespec tout, *tp;

	if (timeout) {
		tout = *timeout;
		tp = &tout;
	} else
		tp = NULL;

	self = pthread__self();
	TESTCANCEL(self);
	retval = ____sigtimedwait50(set, info, tp);
	TESTCANCEL(self);

	return retval;
}

int
sigwait(const sigset_t * restrict set, int * restrict sig)
{
	pthread_t	self;
	int		saved_errno;
	int		new_errno;
	int		retval;

	self = pthread__self();
	saved_errno = errno;
	TESTCANCEL(self);
	retval = ____sigtimedwait50(set, NULL, NULL);
	TESTCANCEL(self);
	new_errno = errno;
	errno = saved_errno;
	if (retval < 0) {
		return new_errno;
	}
	*sig = retval;
	return 0;
}

int
tcdrain(int fd)
{
	int retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = ioctl(fd, TIOCDRAIN, 0);
	TESTCANCEL(self);

	return retval;
}

pid_t
__wait450(pid_t wpid, int *status, int options, struct rusage *rusage)
{
	pid_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys___wait450(wpid, status, options, rusage);
	TESTCANCEL(self);

	return retval;
}

ssize_t
write(int d, const void *buf, size_t nbytes)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_write(d, buf, nbytes);
	TESTCANCEL(self);

	return retval;
}

ssize_t
writev(int d, const struct iovec *iov, int iovcnt)
{
	ssize_t retval;
	pthread_t self;

	self = pthread__self();
	TESTCANCEL(self);
	retval = _sys_writev(d, iov, iovcnt);
	TESTCANCEL(self);

	return retval;
}

__strong_alias(_clock_nanosleep, clock_nanosleep)
__strong_alias(_close, close)
__strong_alias(_fcntl, fcntl)
__strong_alias(_fdatasync, fdatasync)
__strong_alias(_fsync, fsync)
__weak_alias(fsync_range, _fsync_range)
__strong_alias(_mq_receive, mq_receive)
__strong_alias(_mq_send, mq_send)
__strong_alias(_msgrcv, msgrcv)
__strong_alias(_msgsnd, msgsnd)
__strong_alias(___msync13, __msync13)
__strong_alias(___nanosleep50, __nanosleep50)
__strong_alias(_open, open)
__strong_alias(_openat, openat)
__strong_alias(_poll, poll)
__strong_alias(_pread, pread)
__strong_alias(_pwrite, pwrite)
__strong_alias(_read, read)
__strong_alias(_readv, readv)
__strong_alias(_recvfrom, recvfrom)
__strong_alias(_recvmmsg, recvmmsg)
__strong_alias(_recvmsg, recvmsg)
__strong_alias(_sendmmsg, sendmmsg)
__strong_alias(_sendmsg, sendmsg)
__strong_alias(_sendto, sendto)
__strong_alias(_sigwait, sigwait)
__strong_alias(_write, write)
__strong_alias(_writev, writev)

#endif	/* !lint */
