/*	$NetBSD: uipc_syscalls.c,v 1.215 2025/07/16 19:14:13 kre Exp $	*/

/*-
 * Copyright (c) 2008, 2009, 2023 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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

/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)uipc_syscalls.c	8.6 (Berkeley) 2/14/95
 */

#define MBUFTYPES

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: uipc_syscalls.c,v 1.215 2025/07/16 19:14:13 kre Exp $");

#ifdef _KERNEL_OPT
#include "opt_pipe.h"
#include "opt_sctp.h"
#endif

#include <sys/param.h>
#include <sys/types.h>

#include <sys/atomic.h>
#include <sys/buf.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kauth.h>
#include <sys/ktrace.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/sdt.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscallargs.h>
#include <sys/systm.h>
#include <sys/un.h>

#ifdef SCTP
#include <netinet/sctp_peeloff.h>
#include <netinet/sctp_uio.h>
#endif

/*
 * System call interface to the socket abstraction.
 */
extern const struct fileops socketops;

static int	sockargs_sb(struct sockaddr_big *, const void *, socklen_t);

int
sys___socket30(struct lwp *l, const struct sys___socket30_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)	domain;
		syscallarg(int)	type;
		syscallarg(int)	protocol;
	} */
	int fd, error;
	file_t *fp;

	error = fsocreate(SCARG(uap, domain), NULL, SCARG(uap, type),
	    SCARG(uap, protocol), &fd, &fp, NULL);
	if (error == 0) {
		fd_affix(l->l_proc, fp, fd);
		*retval = fd;
	}
	return error;
}

int
sys_bind(struct lwp *l, const struct sys_bind_args *uap, register_t *retval)
{
	/* {
		syscallarg(int)				s;
		syscallarg(const struct sockaddr *)	name;
		syscallarg(unsigned int)		namelen;
	} */
	int		error;
	struct sockaddr_big sb;

	error = sockargs_sb(&sb, SCARG(uap, name), SCARG(uap, namelen));
	if (error)
		return error;

	return do_sys_bind(l, SCARG(uap, s), (struct sockaddr *)&sb);
}

int
do_sys_bind(struct lwp *l, int fd, struct sockaddr *nam)
{
	struct socket	*so;
	int		error;

	if ((error = fd_getsock(fd, &so)) != 0)
		return error;
	error = sobind(so, nam, l);
	fd_putfile(fd);
	return error;
}

int
sys_listen(struct lwp *l, const struct sys_listen_args *uap, register_t *retval)
{
	/* {
		syscallarg(int)	s;
		syscallarg(int)	backlog;
	} */
	struct socket	*so;
	int		error;

	if ((error = fd_getsock(SCARG(uap, s), &so)) != 0)
		return (error);
	error = solisten(so, SCARG(uap, backlog), l);
	fd_putfile(SCARG(uap, s));
	return error;
}

int
do_sys_accept(struct lwp *l, int sock, struct sockaddr *name,
    register_t *new_sock, const sigset_t *mask, int flags, int clrflags)
{
	file_t		*fp, *fp2;
	int		error, fd;
	struct socket	*so, *so2;
	short		wakeup_state = 0;

	if ((fp = fd_getfile(sock)) == NULL)
		return SET_ERROR(EBADF);
	if (fp->f_type != DTYPE_SOCKET) {
		fd_putfile(sock);
		return SET_ERROR(ENOTSOCK);
	}
	if ((error = fd_allocfile(&fp2, &fd)) != 0) {
		fd_putfile(sock);
		return error;
	}
	*new_sock = fd;
	so = fp->f_socket;
	solock(so);

	if (__predict_false(mask))
		sigsuspendsetup(l, mask);

	if (!(so->so_proto->pr_flags & PR_LISTEN)) {
		error = SET_ERROR(EOPNOTSUPP);
		goto bad;
	}
	if ((so->so_options & SO_ACCEPTCONN) == 0) {
		error = SET_ERROR(EINVAL);
		goto bad;
	}
	if ((so->so_state & SS_NBIO) && so->so_qlen == 0) {
		error = SET_ERROR(EWOULDBLOCK);
		goto bad;
	}
	while (so->so_qlen == 0 && so->so_error == 0) {
		if (so->so_state & SS_CANTRCVMORE) {
			so->so_error = SET_ERROR(ECONNABORTED);
			break;
		}
		if (wakeup_state & SS_RESTARTSYS) {
			error = SET_ERROR(ERESTART);
			goto bad;
		}
		error = sowait(so, true, 0);
		if (error) {
			goto bad;
		}
		wakeup_state = so->so_state;
	}
	if (so->so_error) {
		error = SET_ERROR(so->so_error);
		so->so_error = 0;
		goto bad;
	}
	/* connection has been removed from the listen queue */
	KNOTE(&so->so_rcv.sb_sel.sel_klist, NOTE_SUBMIT);
	so2 = TAILQ_FIRST(&so->so_q);
	if (soqremque(so2, 1) == 0)
		panic("accept");
	fp2->f_type = DTYPE_SOCKET;
	fp2->f_flag = (fp->f_flag & ~clrflags) |
	    ((flags & SOCK_NONBLOCK) ? FNONBLOCK : 0)|
	    ((flags & SOCK_NOSIGPIPE) ? FNOSIGPIPE : 0);
	fp2->f_ops = &socketops;
	fp2->f_socket = so2;
	if (fp2->f_flag & FNONBLOCK)
		so2->so_state |= SS_NBIO;
	else
		so2->so_state &= ~SS_NBIO;
	error = soaccept(so2, name);
	so2->so_cred = kauth_cred_hold(so->so_cred);
	sounlock(so);
	if (error) {
		/* an error occurred, free the file descriptor and mbuf */
		mutex_enter(&fp2->f_lock);
		fp2->f_count++;
		mutex_exit(&fp2->f_lock);
		closef(fp2);
		fd_abort(curproc, NULL, fd);
	} else {
		fd_set_exclose(l, fd, (flags & SOCK_CLOEXEC) != 0);
		fd_set_foclose(l, fd, (flags & SOCK_CLOFORK) != 0);
		fd_affix(curproc, fp2, fd);
	}
	fd_putfile(sock);
	if (__predict_false(mask))
		sigsuspendteardown(l);
	return error;
 bad:
	sounlock(so);
	fd_putfile(sock);
	fd_abort(curproc, fp2, fd);
	if (__predict_false(mask))
		sigsuspendteardown(l);
	return error;
}

int
sys_accept(struct lwp *l, const struct sys_accept_args *uap, register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(struct sockaddr *)	name;
		syscallarg(unsigned int *)	anamelen;
	} */
	int error, fd;
	struct sockaddr_big name;

	name.sb_len = UCHAR_MAX;
	error = do_sys_accept(l, SCARG(uap, s), (struct sockaddr *)&name,
	    retval, NULL, 0, 0);
	if (error != 0)
		return error;
	error = copyout_sockname_sb(SCARG(uap, name), SCARG(uap, anamelen),
	    MSG_LENUSRSPACE, &name);
	if (error != 0) {
		fd = (int)*retval;
		if (fd_getfile(fd) != NULL)
			(void)fd_close(fd);
	}
	return error;
}

int
sys_paccept(struct lwp *l, const struct sys_paccept_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(struct sockaddr *)	name;
		syscallarg(unsigned int *)	anamelen;
		syscallarg(const sigset_t *)	mask;
		syscallarg(int)			flags;
	} */
	int error, fd;
	struct sockaddr_big name;
	sigset_t *mask, amask;

	if (SCARG(uap, mask) != NULL) {
		error = copyin(SCARG(uap, mask), &amask, sizeof(amask));
		if (error)
			return error;
		mask = &amask;
	} else
		mask = NULL;

	name.sb_len = UCHAR_MAX;
	error = do_sys_accept(l, SCARG(uap, s), (struct sockaddr *)&name,
	    retval, mask, SCARG(uap, flags), FNONBLOCK);
	if (error != 0)
		return error;
	error = copyout_sockname_sb(SCARG(uap, name), SCARG(uap, anamelen),
	    MSG_LENUSRSPACE, &name);
	if (error != 0) {
		fd = (int)*retval;
		if (fd_getfile(fd) != NULL)
			(void)fd_close(fd);
	}
	return error;
}

int
sys_connect(struct lwp *l, const struct sys_connect_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)				s;
		syscallarg(const struct sockaddr *)	name;
		syscallarg(unsigned int)		namelen;
	} */
	int		error;
	struct sockaddr_big sbig;

	error = sockargs_sb(&sbig, SCARG(uap, name), SCARG(uap, namelen));
	if (error)
		return error;
	return do_sys_connect(l, SCARG(uap, s), (struct sockaddr *)&sbig);
}

int
do_sys_connect(struct lwp *l, int fd, struct sockaddr *nam)
{
	struct socket	*so;
	int		error;
	int		interrupted = 0;

	if ((error = fd_getsock(fd, &so)) != 0) {
		return (error);
	}
	solock(so);
	if ((so->so_state & SS_ISCONNECTING) != 0) {
		error = SET_ERROR(EALREADY);
		goto out;
	}

	error = soconnect(so, nam, l);
	if (error)
		goto bad;
	if ((so->so_state & (SS_NBIO|SS_ISCONNECTING)) ==
	    (SS_NBIO|SS_ISCONNECTING)) {
		error = SET_ERROR(EINPROGRESS);
		goto out;
	}
	while ((so->so_state & SS_ISCONNECTING) != 0 && so->so_error == 0) {
		error = sowait(so, true, 0);
		if (__predict_false((so->so_state & SS_ISABORTING) != 0)) {
			error = SET_ERROR(EPIPE);
			interrupted = 1;
			break;
		}
		if (error) {
			if (error == EINTR || error == ERESTART)
				interrupted = 1;
			break;
		}
	}
	if (error == 0) {
		error = SET_ERROR(so->so_error);
		so->so_error = 0;
	}
 bad:
	if (!interrupted)
		so->so_state &= ~SS_ISCONNECTING;
	if (error == ERESTART)
		error = SET_ERROR(EINTR);
 out:
	sounlock(so);
	fd_putfile(fd);
	return error;
}

int
sys_socketpair(struct lwp *l, const struct sys_socketpair_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)		domain;
		syscallarg(int)		type;
		syscallarg(int)		protocol;
		syscallarg(int *)	rsv;
	} */
	file_t		*fp1, *fp2;
	struct socket	*so1, *so2;
	int		fd, error, sv[2];
	proc_t		*p = curproc;
	int		flags = SCARG(uap, type) & SOCK_FLAGS_MASK;
	int		type = SCARG(uap, type) & ~SOCK_FLAGS_MASK;
	int		domain = SCARG(uap, domain);
	int		proto = SCARG(uap, protocol);

	error = fsocreate(domain, &so1, type|flags, proto, &fd, &fp1, NULL);
	if (error)
		return error;
	sv[0] = fd;

	error = fsocreate(domain, &so2, type|flags, proto, &fd, &fp2, so1);
	if (error)
		goto out;
	sv[1] = fd;

	solock(so1);
	error = soconnect2(so1, so2);
	if (error == 0 && type == SOCK_DGRAM) {
		/*
		 * Datagram socket connection is asymmetric.
		 */
		error = soconnect2(so2, so1);
	}
	sounlock(so1);

	if (error == 0)
		error = copyout(sv, SCARG(uap, rsv), sizeof(sv));
	if (error == 0) {
		fd_affix(p, fp2, sv[1]);
		fd_affix(p, fp1, sv[0]);
		return 0;
	}
	fd_abort(p, fp2, sv[1]);
	(void)soclose(so2);
out:
	fd_abort(p, fp1, sv[0]);
	(void)soclose(so1);
	return error;
}

int
sys_sendto(struct lwp *l, const struct sys_sendto_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)				s;
		syscallarg(const void *)		buf;
		syscallarg(size_t)			len;
		syscallarg(int)				flags;
		syscallarg(const struct sockaddr *)	to;
		syscallarg(unsigned int)		tolen;
	} */
	struct msghdr	msg = {0};
	struct iovec	aiov;

	msg.msg_name = __UNCONST(SCARG(uap, to)); /* XXXUNCONST kills const */
	msg.msg_namelen = SCARG(uap, tolen);
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = 0;
	aiov.iov_base = __UNCONST(SCARG(uap, buf)); /* XXXUNCONST kills const */
	aiov.iov_len = SCARG(uap, len);
	return do_sys_sendmsg(l, SCARG(uap, s), &msg, SCARG(uap, flags),
	    retval);
}

int
sys_sendmsg(struct lwp *l, const struct sys_sendmsg_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)				s;
		syscallarg(const struct msghdr *)	msg;
		syscallarg(int)				flags;
	} */
	struct msghdr	msg;
	int		error;

	error = copyin(SCARG(uap, msg), &msg, sizeof(msg));
	if (error)
		return (error);

	msg.msg_flags = MSG_IOVUSRSPACE;
	return do_sys_sendmsg(l, SCARG(uap, s), &msg, SCARG(uap, flags),
	    retval);
}

int
do_sys_sendmsg_so(struct lwp *l, int s, struct socket *so, file_t *fp,
    struct msghdr *mp, int flags, register_t *retsize)
{

	struct iovec	aiov[UIO_SMALLIOV], *iov = aiov, *tiov, *ktriov = NULL;
	struct sockaddr *sa = NULL;
	struct mbuf	*to, *control;
	struct uio	auio;
	size_t		len, iovsz;
	int		i, error;

	ktrkuser("msghdr", mp, sizeof(*mp));

	/* If the caller passed us stuff in mbufs, we must free them. */
	to = (mp->msg_flags & MSG_NAMEMBUF) ? mp->msg_name : NULL;
	control = (mp->msg_flags & MSG_CONTROLMBUF) ? mp->msg_control : NULL;
	iovsz = mp->msg_iovlen * sizeof(struct iovec);

	if (mp->msg_flags & MSG_IOVUSRSPACE) {
		if ((unsigned int)mp->msg_iovlen > UIO_SMALLIOV) {
			if ((unsigned int)mp->msg_iovlen > IOV_MAX) {
				error = SET_ERROR(EMSGSIZE);
				goto bad;
			}
			iov = kmem_alloc(iovsz, KM_SLEEP);
		}
		if (mp->msg_iovlen != 0) {
			error = copyin(mp->msg_iov, iov, iovsz);
			if (error)
				goto bad;
		}
		auio.uio_iov = iov;
	} else
		auio.uio_iov = mp->msg_iov;

	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_rw = UIO_WRITE;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	KASSERT(l == curlwp);
	auio.uio_vmspace = l->l_proc->p_vmspace;

	tiov = auio.uio_iov;
	for (i = 0; i < auio.uio_iovcnt; i++, tiov++) {
		/*
		 * Writes return ssize_t because -1 is returned on error.
		 * Therefore, we must restrict the length to SSIZE_MAX to
		 * avoid garbage return values.
		 */
		auio.uio_resid += tiov->iov_len;
		if (tiov->iov_len > SSIZE_MAX || auio.uio_resid > SSIZE_MAX) {
			error = SET_ERROR(EINVAL);
			goto bad;
		}
	}

	if (mp->msg_name && to == NULL) {
		error = sockargs(&to, mp->msg_name, mp->msg_namelen,
		    UIO_USERSPACE, MT_SONAME);
		if (error)
			goto bad;
	}

	if (mp->msg_control) {
		if (mp->msg_controllen < CMSG_ALIGN(sizeof(struct cmsghdr))) {
			error = SET_ERROR(EINVAL);
			goto bad;
		}
		if (control == NULL) {
			error = sockargs(&control, mp->msg_control,
			    mp->msg_controllen, UIO_USERSPACE, MT_CONTROL);
			if (error)
				goto bad;
		}
	}

	if (ktrpoint(KTR_GENIO) && iovsz > 0) {
		ktriov = kmem_alloc(iovsz, KM_SLEEP);
		memcpy(ktriov, auio.uio_iov, iovsz);
	}

	if (mp->msg_name)
		MCLAIM(to, so->so_mowner);
	if (mp->msg_control)
		MCLAIM(control, so->so_mowner);

	if (to) {
		sa = mtod(to, struct sockaddr *);
	}

	len = auio.uio_resid;
	error = (*so->so_send)(so, sa, &auio, NULL, control, flags, l);
	/* Protocol is responsible for freeing 'control' */
	control = NULL;

	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE && (fp->f_flag & FNOSIGPIPE) == 0 &&
		    (flags & MSG_NOSIGNAL) == 0) {
			mutex_enter(&proc_lock);
			psignal(l->l_proc, SIGPIPE);
			mutex_exit(&proc_lock);
		}
	}
	if (error == 0)
		*retsize = len - auio.uio_resid;

bad:
	if (ktriov != NULL) {
		ktrgeniov(s, UIO_WRITE, ktriov, *retsize, error);
		kmem_free(ktriov, iovsz);
	}

	if (iov != aiov)
		kmem_free(iov, iovsz);
	m_freem(to);
	m_freem(control);

	return error;
}

int
do_sys_sendmsg(struct lwp *l, int s, struct msghdr *mp, int flags,
    register_t *retsize)
{
	int		error;
	struct socket	*so;
	file_t		*fp;

	if ((error = fd_getsock1(s, &so, &fp)) != 0) {
		/* We have to free msg_name and msg_control ourselves */
		if (mp->msg_flags & MSG_NAMEMBUF)
			m_freem(mp->msg_name);
		if (mp->msg_flags & MSG_CONTROLMBUF)
			m_freem(mp->msg_control);
		return error;
	}
	error = do_sys_sendmsg_so(l, s, so, fp, mp, flags, retsize);
	/* msg_name and msg_control freed */
	fd_putfile(s);
	return error;
}

int
sys_recvfrom(struct lwp *l, const struct sys_recvfrom_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(void *)		buf;
		syscallarg(size_t)		len;
		syscallarg(int)			flags;
		syscallarg(struct sockaddr *)	from;
		syscallarg(unsigned int *)	fromlenaddr;
	} */
	struct msghdr	msg = {0};
	struct iovec	aiov;
	int		error;
	struct mbuf	*from;

	msg.msg_name = NULL;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = SCARG(uap, buf);
	aiov.iov_len = SCARG(uap, len);
	msg.msg_control = NULL;
	msg.msg_flags = SCARG(uap, flags) & MSG_USERFLAGS;

	error = do_sys_recvmsg(l, SCARG(uap, s), &msg, &from, NULL, retval);
	if (error != 0)
		return error;

	error = copyout_sockname(SCARG(uap, from), SCARG(uap, fromlenaddr),
	    MSG_LENUSRSPACE, from);
	if (from != NULL)
		m_free(from);
	return error;
}

int
sys_recvmsg(struct lwp *l, const struct sys_recvmsg_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(struct msghdr *)	msg;
		syscallarg(int)			flags;
	} */
	struct msghdr	msg;
	int		error;
	struct mbuf	*from, *control;

	error = copyin(SCARG(uap, msg), &msg, sizeof(msg));
	if (error)
		return error;

	msg.msg_flags = (SCARG(uap, flags) & MSG_USERFLAGS) | MSG_IOVUSRSPACE;

	error = do_sys_recvmsg(l, SCARG(uap, s), &msg, &from,
	    msg.msg_control != NULL ? &control : NULL, retval);
	if (error != 0)
		return error;

	if (msg.msg_control != NULL)
		error = copyout_msg_control(l, &msg, control);

	if (error == 0)
		error = copyout_sockname(msg.msg_name, &msg.msg_namelen, 0,
			from);
	if (from != NULL)
		m_free(from);
	if (error == 0) {
		ktrkuser("msghdr", &msg, sizeof(msg));
		error = copyout(&msg, SCARG(uap, msg), sizeof(msg));
	}

	return error;
}

int
sys_sendmmsg(struct lwp *l, const struct sys_sendmmsg_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(struct mmsghdr *)	mmsg;
		syscallarg(unsigned int)	vlen;
		syscallarg(unsigned int)	flags;
	} */
	struct mmsghdr mmsg;
	struct socket *so;
	file_t *fp;
	struct msghdr *msg = &mmsg.msg_hdr;
	int error, s;
	unsigned int vlen, flags, dg;

	s = SCARG(uap, s);
	if ((error = fd_getsock1(s, &so, &fp)) != 0)
		return error;

	vlen = SCARG(uap, vlen);
	if (vlen > 1024)
		vlen = 1024;

	flags = (SCARG(uap, flags) & MSG_USERFLAGS) | MSG_IOVUSRSPACE;

	for (dg = 0; dg < vlen;) {
		error = copyin(SCARG(uap, mmsg) + dg, &mmsg, sizeof(mmsg));
		if (error)
			break;

		msg->msg_flags = flags;

		error = do_sys_sendmsg_so(l, s, so, fp, msg, flags, retval);
		if (error)
			break;

		ktrkuser("msghdr", msg, sizeof(*msg));
		mmsg.msg_len = *retval;
		error = copyout(&mmsg, SCARG(uap, mmsg) + dg, sizeof(mmsg));
		if (error)
			break;
		dg++;

	}

	*retval = dg;

	fd_putfile(s);

	/*
	 * If we succeeded at least once, return 0.
	 */
	if (dg)
		return 0;
	return error;
}

/*
 * Adjust for a truncated SCM_RIGHTS control message.
 *  This means closing any file descriptors that aren't present
 *  in the returned buffer.
 *  m is the mbuf holding the (already externalized) SCM_RIGHTS message.
 */
static void
free_rights(struct mbuf *m)
{
	struct cmsghdr *cm;
	int *fdv;
	unsigned int nfds, i;

	KASSERT(sizeof(*cm) <= m->m_len);
	cm = mtod(m, struct cmsghdr *);

	KASSERT(CMSG_ALIGN(sizeof(*cm)) <= cm->cmsg_len);
	KASSERT(cm->cmsg_len <= m->m_len);
	nfds = (cm->cmsg_len - CMSG_ALIGN(sizeof(*cm))) / sizeof(int);
	fdv = (int *)CMSG_DATA(cm);

	for (i = 0; i < nfds; i++)
		if (fd_getfile(fdv[i]) != NULL)
			(void)fd_close(fdv[i]);
}

void
free_control_mbuf(struct lwp *l, struct mbuf *control, struct mbuf *uncopied)
{
	struct mbuf *next;
	struct cmsghdr *cmsg;
	bool do_free_rights = false;

	while (control != NULL) {
		cmsg = mtod(control, struct cmsghdr *);
		if (control == uncopied)
			do_free_rights = true;
		if (do_free_rights && cmsg->cmsg_level == SOL_SOCKET
		    && cmsg->cmsg_type == SCM_RIGHTS)
			free_rights(control);
		next = control->m_next;
		m_free(control);
		control = next;
	}
}

/* Copy socket control/CMSG data to user buffer, frees the mbuf */
int
copyout_msg_control(struct lwp *l, struct msghdr *mp, struct mbuf *control)
{
	int i, len, error = 0;
	struct cmsghdr *cmsg;
	struct mbuf *m;
	char *q;

	len = mp->msg_controllen;
	if (len <= 0 || control == 0) {
		mp->msg_controllen = 0;
		free_control_mbuf(l, control, control);
		return 0;
	}

	q = (char *)mp->msg_control;

	for (m = control; m != NULL; ) {
		cmsg = mtod(m, struct cmsghdr *);
		i = m->m_len;
		if (len < i) {
			mp->msg_flags |= MSG_CTRUNC;
			if (cmsg->cmsg_level == SOL_SOCKET
			    && cmsg->cmsg_type == SCM_RIGHTS)
				/* Do not truncate me ... */
				break;
			i = len;
		}
		error = copyout(mtod(m, void *), q, i);
		ktrkuser(mbuftypes[MT_CONTROL], cmsg, cmsg->cmsg_len);
		if (error != 0) {
			/* We must free all the SCM_RIGHTS */
			m = control;
			break;
		}
		m = m->m_next;
		if (m)
			i = ALIGN(i);
		q += i;
		len -= i;
		if (len <= 0)
			break;
	}

	free_control_mbuf(l, control, m);

	mp->msg_controllen = q - (char *)mp->msg_control;
	return error;
}

int
do_sys_recvmsg_so(struct lwp *l, int s, struct socket *so, struct msghdr *mp,
    struct mbuf **from, struct mbuf **control, register_t *retsize)
{
	struct iovec	aiov[UIO_SMALLIOV], *iov = aiov, *tiov, *ktriov = NULL;
	struct uio	auio;
	size_t		len, iovsz;
	int		i, error;

	ktrkuser("msghdr", mp, sizeof(*mp));

	*from = NULL;
	if (control != NULL)
		*control = NULL;

	iovsz = mp->msg_iovlen * sizeof(struct iovec);

	if (mp->msg_flags & MSG_IOVUSRSPACE) {
		if ((unsigned int)mp->msg_iovlen > UIO_SMALLIOV) {
			if ((unsigned int)mp->msg_iovlen > IOV_MAX) {
				error = SET_ERROR(EMSGSIZE);
				goto out;
			}
			iov = kmem_alloc(iovsz, KM_SLEEP);
		}
		if (mp->msg_iovlen != 0) {
			error = copyin(mp->msg_iov, iov, iovsz);
			if (error)
				goto out;
		}
		auio.uio_iov = iov;
	} else
		auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_rw = UIO_READ;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	KASSERT(l == curlwp);
	auio.uio_vmspace = l->l_proc->p_vmspace;

	tiov = auio.uio_iov;
	for (i = 0; i < auio.uio_iovcnt; i++, tiov++) {
		/*
		 * Reads return ssize_t because -1 is returned on error.
		 * Therefore we must restrict the length to SSIZE_MAX to
		 * avoid garbage return values.
		 */
		auio.uio_resid += tiov->iov_len;
		if (tiov->iov_len > SSIZE_MAX || auio.uio_resid > SSIZE_MAX) {
			error = SET_ERROR(EINVAL);
			goto out;
		}
	}

	if (ktrpoint(KTR_GENIO) && iovsz > 0) {
		ktriov = kmem_alloc(iovsz, KM_SLEEP);
		memcpy(ktriov, auio.uio_iov, iovsz);
	}

	len = auio.uio_resid;
	mp->msg_flags &= MSG_USERFLAGS;
	error = (*so->so_receive)(so, from, &auio, NULL, control,
	    &mp->msg_flags);
	KASSERT(*from == NULL || (*from)->m_next == NULL);
	len -= auio.uio_resid;
	*retsize = len;
	if (error != 0 && len != 0
	    && (error == ERESTART || error == EINTR || error == EWOULDBLOCK))
		/* Some data transferred */
		error = 0;

	if (ktriov != NULL) {
		ktrgeniov(s, UIO_READ, ktriov, len, error);
		kmem_free(ktriov, iovsz);
	}

	if (error != 0) {
		m_freem(*from);
		*from = NULL;
		if (control != NULL) {
			free_control_mbuf(l, *control, *control);
			*control = NULL;
		}
	}
 out:
	if (iov != aiov)
		kmem_free(iov, iovsz);
	return error;
}


int
do_sys_recvmsg(struct lwp *l, int s, struct msghdr *mp,
    struct mbuf **from, struct mbuf **control, register_t *retsize)
{
	int error;
	struct socket *so;

	if ((error = fd_getsock(s, &so)) != 0)
		return error;
	error = do_sys_recvmsg_so(l, s, so, mp, from, control, retsize);
	fd_putfile(s);
	return error;
}

int
sys_recvmmsg(struct lwp *l, const struct sys_recvmmsg_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(struct mmsghdr *)	mmsg;
		syscallarg(unsigned int)	vlen;
		syscallarg(unsigned int)	flags;
		syscallarg(struct timespec *)	timeout;
	} */
	struct mmsghdr mmsg;
	struct socket *so;
	struct msghdr *msg = &mmsg.msg_hdr;
	int error, s;
	struct mbuf *from, *control;
	struct timespec ts, now;
	unsigned int vlen, flags, dg;

	if (SCARG(uap, timeout)) {
		if ((error = copyin(SCARG(uap, timeout), &ts, sizeof(ts))) != 0)
			return error;
		if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
			return SET_ERROR(EINVAL);
		getnanotime(&now);
		if (timespecaddok(&now, &ts)) {
			timespecadd(&now, &ts, &ts);
		} else {
			ts.tv_sec = __type_max(time_t);
			ts.tv_nsec = 999999999L;
		}
	}

	s = SCARG(uap, s);
	if ((error = fd_getsock(s, &so)) != 0)
		return error;

	/*
	 * If so->so_rerror holds a deferred error return it now.
	 */
	if (so->so_rerror) {
		error = SET_ERROR(so->so_rerror);
		so->so_rerror = 0;
		fd_putfile(s);
		return error;
	}

	vlen = SCARG(uap, vlen);
	if (vlen > 1024)
		vlen = 1024;

	from = NULL;
	flags = (SCARG(uap, flags) & MSG_USERFLAGS) | MSG_IOVUSRSPACE;

	for (dg = 0; dg < vlen;) {
		error = copyin(SCARG(uap, mmsg) + dg, &mmsg, sizeof(mmsg));
		if (error)
			break;

		msg->msg_flags = flags & ~MSG_WAITFORONE;

		if (from != NULL) {
			m_free(from);
			from = NULL;
		}

		error = do_sys_recvmsg_so(l, s, so, msg, &from,
		    msg->msg_control != NULL ? &control : NULL, retval);
		if (error) {
			if (error == EAGAIN && dg > 0)
				error = 0;
			break;
		}

		if (msg->msg_control != NULL)
			error = copyout_msg_control(l, msg, control);
		if (error)
			break;

		error = copyout_sockname(msg->msg_name, &msg->msg_namelen, 0,
		    from);
		if (error)
			break;

		ktrkuser("msghdr", msg, sizeof *msg);
		mmsg.msg_len = *retval;

		error = copyout(&mmsg, SCARG(uap, mmsg) + dg, sizeof(mmsg));
		if (error)
			break;

		dg++;
		if (msg->msg_flags & MSG_OOB)
			break;

		if (SCARG(uap, timeout)) {
			getnanotime(&now);
			if (timespeccmp(&ts, &now, <))
				break;
		}

		if (flags & MSG_WAITFORONE)
			flags |= MSG_DONTWAIT;

	}

	if (from != NULL)
		m_free(from);

	*retval = dg;

	/*
	 * If we succeeded at least once, return 0, hopefully so->so_rerror
	 * will catch it next time.
	 */
	if (error && dg > 0) {
		so->so_rerror = error;
		error = 0;
	}

	fd_putfile(s);

	return error;
}

int
sys_shutdown(struct lwp *l, const struct sys_shutdown_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)	s;
		syscallarg(int)	how;
	} */
	struct socket	*so;
	int		error;

	if ((error = fd_getsock(SCARG(uap, s), &so)) != 0)
		return error;
	solock(so);
	error = soshutdown(so, SCARG(uap, how));
	sounlock(so);
	fd_putfile(SCARG(uap, s));
	return error;
}

int
sys_setsockopt(struct lwp *l, const struct sys_setsockopt_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(int)			level;
		syscallarg(int)			name;
		syscallarg(const void *)	val;
		syscallarg(unsigned int)	valsize;
	} */
	struct sockopt	sopt;
	struct socket	*so;
	file_t		*fp;
	int		error;
	unsigned int	len;

	len = SCARG(uap, valsize);
	if (len > 0 && SCARG(uap, val) == NULL)
		return SET_ERROR(EINVAL);

	if (len > MCLBYTES)
		return SET_ERROR(EINVAL);

	if ((error = fd_getsock1(SCARG(uap, s), &so, &fp)) != 0)
		return (error);

	sockopt_init(&sopt, SCARG(uap, level), SCARG(uap, name), len);

	if (len > 0) {
		error = copyin(SCARG(uap, val), sopt.sopt_data, len);
		if (error)
			goto out;
	}

	error = sosetopt(so, &sopt);
	if (so->so_options & SO_NOSIGPIPE)
		atomic_or_uint(&fp->f_flag, FNOSIGPIPE);
	else
		atomic_and_uint(&fp->f_flag, ~FNOSIGPIPE);

 out:
	sockopt_destroy(&sopt);
	fd_putfile(SCARG(uap, s));
	return error;
}

static int
getsockopt(struct lwp *l, const struct sys_getsockopt_args *uap,
    register_t *retval, bool copyarg)
{
	struct sockopt	sopt;
	struct socket	*so;
	file_t		*fp;
	unsigned int	valsize, len;
	int		error;

	if (SCARG(uap, val) != NULL) {
		error = copyin(SCARG(uap, avalsize), &valsize, sizeof(valsize));
		if (error)
			return error;
	} else
		valsize = 0;

	if (valsize > MCLBYTES)
		return SET_ERROR(EINVAL);

	if ((error = fd_getsock1(SCARG(uap, s), &so, &fp)) != 0)
		return error;

	sockopt_init(&sopt, SCARG(uap, level), SCARG(uap, name), valsize);
	if (copyarg && valsize > 0) {
		error = copyin(SCARG(uap, val), sopt.sopt_data, valsize);
		if (error)
			goto out;
	}

	if (fp->f_flag & FNOSIGPIPE)
		so->so_options |= SO_NOSIGPIPE;
	else
		so->so_options &= ~SO_NOSIGPIPE;

	error = sogetopt(so, &sopt);
	if (error || valsize == 0)
		goto out;

	len = uimin(valsize, sopt.sopt_retsize);
	error = copyout(sopt.sopt_data, SCARG(uap, val), len);
	if (error)
		goto out;

	error = copyout(&len, SCARG(uap, avalsize), sizeof(len));
 out:
	sockopt_destroy(&sopt);
	fd_putfile(SCARG(uap, s));
	return error;
}

int
sys_getsockopt(struct lwp *l, const struct sys_getsockopt_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(int)			level;
		syscallarg(int)			name;
		syscallarg(void *)		val;
		syscallarg(unsigned int *)	avalsize;
	} */
	return getsockopt(l, uap, retval, false);
}

int
sys_getsockopt2(struct lwp *l, const struct sys_getsockopt2_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			s;
		syscallarg(int)			level;
		syscallarg(int)			name;
		syscallarg(void *)		val;
		syscallarg(unsigned int *)	avalsize;
	} */
	return getsockopt(l, (const struct sys_getsockopt_args *) uap, retval, true);
}

#ifdef PIPE_SOCKETPAIR

int
pipe1(struct lwp *l, int *fildes, int flags)
{
	file_t		*rf, *wf;
	struct socket	*rso, *wso;
	int		error, soflags = 0;
	unsigned	rfd, wfd;
	proc_t		*p = l->l_proc;

	if (flags & ~(O_CLOEXEC|O_CLOFORK|O_NONBLOCK|O_NOSIGPIPE))
		return SET_ERROR(EINVAL);
	if (flags & O_CLOEXEC)
		soflags |= SOCK_CLOEXEC;
	if (flags & O_CLOFORK)
		soflags |= SOCK_CLOFORK;
	if (flags & O_NONBLOCK)
		soflags |= SOCK_NONBLOCK;
	if (flags & O_NOSIGPIPE)
		soflags |= SOCK_NOSIGPIPE;

	error = fsocreate(AF_LOCAL, &rso, SOCK_STREAM|soflags, 0, &rfd, &rf,
	    NULL);
	if (error)
		goto free1;
	error = fsocreate(AF_LOCAL, &wso, SOCK_STREAM|soflags, 0, &wfd, &wf,
	    rso);
	if (error)
		goto free2;

	/* make sure the descriptors are uni-directional */
	rf->f_type = rf->f_type & ~(FWRITE);
	wf->f_type = wf->f_type & ~(FREAD);

	/* remember this socket pair implements a pipe */
	rso->so_state |= SS_ISAPIPE;
	wso->so_state |= SS_ISAPIPE;

	solock(wso);
	/*
	 * Pipes must be readable when there is at least 1
	 * byte of data available in the receive buffer.
	 *
	 * Pipes must be writable when there is space for
	 * at least PIPE_BUF bytes in the send buffer.
	 * If we're increasing the low water mark for the
	 * send buffer, then mimic how soreserve() would
	 * have set the high water mark.
	 */
	rso->so_rcv.sb_lowat = 1;
	if (wso->so_snd.sb_lowat < PIPE_BUF) {
		wso->so_snd.sb_hiwat = PIPE_BUF * 2;
	}
	wso->so_snd.sb_lowat = PIPE_BUF;
	error = unp_connect2(wso, rso);
	sounlock(wso);

	if (error != 0)
		goto free3;

	fd_affix(p, wf, wfd);
	fd_affix(p, rf, rfd);
	fildes[0] = rfd;
	fildes[1] = wfd;
	return (0);
 free3:
	(void)soclose(wso);
	fd_abort(p, wf, wfd);
 free2:
	(void)soclose(rso);
	fd_abort(p, rf, rfd);
 free1:
	return error;
}
#endif /* PIPE_SOCKETPAIR */

/*
 * Get peer socket name.
 */
int
do_sys_getpeername(int fd, struct sockaddr *nam)
{
	struct socket	*so;
	int		error;

	if ((error = fd_getsock(fd, &so)) != 0)
		return error;

	solock(so);
	if ((so->so_state & SS_ISCONNECTED) == 0)
		error = SET_ERROR(ENOTCONN);
	else {
		error = (*so->so_proto->pr_usrreqs->pr_peeraddr)(so, nam);
	}
	sounlock(so);
	fd_putfile(fd);
	return error;
}

/*
 * Get local socket name.
 */
int
do_sys_getsockname(int fd, struct sockaddr *nam)
{
	struct socket	*so;
	int		error;

	if ((error = fd_getsock(fd, &so)) != 0)
		return error;

	solock(so);
	error = (*so->so_proto->pr_usrreqs->pr_sockaddr)(so, nam);
	sounlock(so);
	fd_putfile(fd);
	return error;
}

int
copyout_sockname_sb(struct sockaddr *asa, unsigned int *alen, int flags,
    struct sockaddr_big *addr)
{
	unsigned int len;
	int error;

	if (asa == NULL)
		/* Assume application not interested */
		return 0;

	if (flags & MSG_LENUSRSPACE) {
		error = copyin(alen, &len, sizeof(len));
		if (error)
			return error;
	} else
		len = *alen;

	if (addr == NULL) {
		len = 0;
		error = 0;
	} else {
		if (len > addr->sb_len)
			len = addr->sb_len;
		/* XXX addr isn't an mbuf... */
		ktrkuser(mbuftypes[MT_SONAME], addr, len);
		error = copyout(addr, asa, len);
	}

	if (error == 0) {
		if (flags & MSG_LENUSRSPACE)
			error = copyout(&len, alen, sizeof(len));
		else
			*alen = len;
	}

	return error;
}

int
copyout_sockname(struct sockaddr *asa, unsigned int *alen, int flags,
    struct mbuf *addr)
{
	int len;
	int error;

	if (asa == NULL)
		/* Assume application not interested */
		return 0;

	if (flags & MSG_LENUSRSPACE) {
		error = copyin(alen, &len, sizeof(len));
		if (error)
			return error;
	} else
		len = *alen;
	if (len < 0)
		return SET_ERROR(EINVAL);

	if (addr == NULL) {
		len = 0;
		error = 0;
	} else {
		if (len > addr->m_len)
			len = addr->m_len;
		/* Maybe this ought to copy a chain ? */
		ktrkuser(mbuftypes[MT_SONAME], mtod(addr, void *), len);
		error = copyout(mtod(addr, void *), asa, len);
	}

	if (error == 0) {
		if (flags & MSG_LENUSRSPACE)
			error = copyout(&len, alen, sizeof(len));
		else
			*alen = len;
	}

	return error;
}

/*
 * Get socket name.
 */
int
sys_getsockname(struct lwp *l, const struct sys_getsockname_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			fdes;
		syscallarg(struct sockaddr *)	asa;
		syscallarg(unsigned int *)	alen;
	} */
	struct sockaddr_big sbig;
	int		    error;

	sbig.sb_len = UCHAR_MAX;
	error = do_sys_getsockname(SCARG(uap, fdes), (struct sockaddr *)&sbig);
	if (error != 0)
		return error;

	error = copyout_sockname_sb(SCARG(uap, asa), SCARG(uap, alen),
	    MSG_LENUSRSPACE, &sbig);
	return error;
}

/*
 * Get name of peer for connected socket.
 */
int
sys_getpeername(struct lwp *l, const struct sys_getpeername_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(int)			fdes;
		syscallarg(struct sockaddr *)	asa;
		syscallarg(unsigned int *)	alen;
	} */
	struct sockaddr_big sbig;
	int		    error;

	sbig.sb_len = UCHAR_MAX;
	error = do_sys_getpeername(SCARG(uap, fdes), (struct sockaddr *)&sbig);
	if (error != 0)
		return error;

	error = copyout_sockname_sb(SCARG(uap, asa), SCARG(uap, alen),
	    MSG_LENUSRSPACE, &sbig);
	return error;
}

static int
sockargs_sb(struct sockaddr_big *sb, const void *name, socklen_t buflen)
{
	int error;

	/*
	 * We can't allow socket names > UCHAR_MAX in length, since that
	 * will overflow sb_len. Further no reasonable buflen is <=
	 * offsetof(sockaddr_big, sb_data) since it shall be at least
	 * the size of the preamble sb_len and sb_family members.
	 */
	if (buflen > UCHAR_MAX ||
	    buflen <= offsetof(struct sockaddr_big, sb_data))
		return SET_ERROR(EINVAL);

	error = copyin(name, (void *)sb, buflen);
	if (error)
		return error;

	ktrkuser(mbuftypes[MT_SONAME], sb, buflen);
#if BYTE_ORDER != BIG_ENDIAN
	/*
	 * 4.3BSD compat thing - need to stay, since bind(2),
	 * connect(2), sendto(2) were not versioned for COMPAT_43.
	 */
	if (sb->sb_family == 0 && sb->sb_len < AF_MAX)
		sb->sb_family = sb->sb_len;
#endif
	sb->sb_len = buflen;
	return 0;
}

/*
 * XXX In a perfect world, we wouldn't pass around socket control
 * XXX arguments in mbufs, and this could go away.
 */
int
sockargs(struct mbuf **mp, const void *bf, size_t buflen, enum uio_seg seg,
    int type)
{
	struct mbuf	*m;
	int		error;

	/*
	 * We can't allow socket names > UCHAR_MAX in length, since that
	 * will overflow sa_len.  Control data more than a page size in
	 * length is just too much.
	 */
	if (buflen > (type == MT_SONAME ? UCHAR_MAX : PAGE_SIZE))
		return SET_ERROR(EINVAL);

	/*
	 * length must greater than sizeof(sa_family) + sizeof(sa_len)
	 */
	if (type == MT_SONAME && buflen <= 2)
		return SET_ERROR(EINVAL);

	/* Allocate an mbuf to hold the arguments. */
	m = m_get(M_WAIT, type);
	/* can't claim.  don't who to assign it to. */
	if (buflen > MLEN) {
		/*
		 * Won't fit into a regular mbuf, so we allocate just
		 * enough external storage to hold the argument.
		 */
		MEXTMALLOC(m, buflen, M_WAITOK);
	}
	m->m_len = buflen;
	if (seg == UIO_USERSPACE) {
		error = copyin(bf, mtod(m, void *), buflen);
		if (error) {
			(void)m_free(m);
			return error;
		}
	} else {
		memcpy(mtod(m, void *), bf, buflen);
	}
	*mp = m;
	switch (type) {
	case MT_SONAME:
		ktrkuser(mbuftypes[type], mtod(m, void *), buflen);

		struct sockaddr *sa = mtod(m, struct sockaddr *);
#if BYTE_ORDER != BIG_ENDIAN
		/*
		 * 4.3BSD compat thing - need to stay, since bind(2),
		 * connect(2), sendto(2) were not versioned for COMPAT_43.
		 */
		if (sa->sa_family == 0 && sa->sa_len < AF_MAX)
			sa->sa_family = sa->sa_len;
#endif
		sa->sa_len = buflen;
		return 0;
	case MT_CONTROL:
		if (!KTRPOINT(curproc, KTR_USER))
			return 0;

		struct msghdr mhdr;
		mhdr.msg_control = mtod(m, void *);
		mhdr.msg_controllen = buflen;
		for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&mhdr); cmsg;
		    cmsg = CMSG_NXTHDR(&mhdr, cmsg)) {
			KASSERT(((char *)cmsg - mtod(m, char *)) <= buflen);
			if (cmsg->cmsg_len >
			    buflen - ((char *)cmsg - mtod(m, char *)))
				break;
			ktrkuser(mbuftypes[type], cmsg, cmsg->cmsg_len);
		}
		return 0;
	default:
		return SET_ERROR(EINVAL);
	}
}

int
do_sys_peeloff(struct socket *head, void *data)
{
#ifdef SCTP
	/*file_t *lfp = NULL;*/
	file_t *nfp = NULL;
	int error;
	struct socket *so;
	int fd;
	uint32_t name;
	/*short fflag;*/		/* type must match fp->f_flag */

	name = *(uint32_t *) data;
	error = sctp_can_peel_off(head, name);
	if (error) {
		printf("peeloff failed\n");
		return error;
	}
	/*
	 * At this point we know we do have a assoc to pull
	 * we proceed to get the fd setup. This may block
	 * but that is ok.
	 */
	error = fd_allocfile(&nfp, &fd);
	if (error) {
		/*
		 * Probably ran out of file descriptors. Put the
		 * unaccepted connection back onto the queue and
		 * do another wakeup so some other process might
		 * have a chance at it.
		 */
		return error;
	}
	*(int *) data = fd;

	so = sctp_get_peeloff(head, name, &error);
	if (so == NULL) {
		/*
		 * Either someone else peeled it off OR
		 * we can't get a socket.
		 * close the new descriptor, assuming someone hasn't ripped it
		 * out from under us.
		 */
		mutex_enter(&nfp->f_lock);
		nfp->f_count++;
		mutex_exit(&nfp->f_lock);
		fd_abort(curlwp->l_proc, nfp, fd);
		return error;
	}
	so->so_state &= ~SS_NOFDREF;
	so->so_state &= ~SS_ISCONNECTING;
	so->so_head = NULL;
	so->so_cred = kauth_cred_hold(head->so_cred);
	nfp->f_socket = so;
	nfp->f_flag = FREAD|FWRITE;
	nfp->f_ops = &socketops;
	nfp->f_type = DTYPE_SOCKET;

	fd_affix(curlwp->l_proc, nfp, fd);

	return error;
#else
	return SET_ERROR(EOPNOTSUPP);
#endif
}
