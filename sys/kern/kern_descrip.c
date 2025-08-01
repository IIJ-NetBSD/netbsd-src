/*	$NetBSD: kern_descrip.c,v 1.266 2025/07/16 19:14:13 kre Exp $	*/

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
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_descrip.c	8.8 (Berkeley) 2/14/95
 */

/*
 * File descriptor management.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: kern_descrip.c,v 1.266 2025/07/16 19:14:13 kre Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/pool.h>
#include <sys/unistd.h>
#include <sys/resourcevar.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/kauth.h>
#include <sys/atomic.h>
#include <sys/syscallargs.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/ktrace.h>

/*
 * A list (head) of open files, counter, and lock protecting them.
 */
struct filelist		filehead	__cacheline_aligned;
static u_int		nfiles		__cacheline_aligned;
kmutex_t		filelist_lock	__cacheline_aligned;

static pool_cache_t	filedesc_cache	__read_mostly;
static pool_cache_t	file_cache	__read_mostly;

static int	file_ctor(void *, void *, int);
static void	file_dtor(void *, void *);
static void	fdfile_ctor(fdfile_t *);
static void	fdfile_dtor(fdfile_t *);
static int	filedesc_ctor(void *, void *, int);
static void	filedesc_dtor(void *, void *);
static int	filedescopen(dev_t, int, int, lwp_t *);

static int sysctl_kern_file(SYSCTLFN_PROTO);
static int sysctl_kern_file2(SYSCTLFN_PROTO);
static void fill_file(struct file *, const struct file *);
static void fill_file2(struct kinfo_file *, const file_t *, const fdfile_t *,
		      int, pid_t);

const struct cdevsw filedesc_cdevsw = {
	.d_open = filedescopen,
	.d_close = noclose,
	.d_read = noread,
	.d_write = nowrite,
	.d_ioctl = noioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER | D_MPSAFE
};

/* For ease of reading. */
__strong_alias(fd_putvnode,fd_putfile)
__strong_alias(fd_putsock,fd_putfile)

/*
 * Initialize the descriptor system.
 */
void
fd_sys_init(void)
{
	static struct sysctllog *clog;

	mutex_init(&filelist_lock, MUTEX_DEFAULT, IPL_NONE);

	LIST_INIT(&filehead);

	file_cache = pool_cache_init(sizeof(file_t), coherency_unit, 0,
	    0, "file", NULL, IPL_NONE, file_ctor, file_dtor, NULL);
	KASSERT(file_cache != NULL);

	filedesc_cache = pool_cache_init(sizeof(filedesc_t), coherency_unit,
	    0, 0, "filedesc", NULL, IPL_NONE, filedesc_ctor, filedesc_dtor,
	    NULL);
	KASSERT(filedesc_cache != NULL);

	sysctl_createv(&clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_STRUCT, "file",
		       SYSCTL_DESCR("System open file table"),
		       sysctl_kern_file, 0, NULL, 0,
		       CTL_KERN, KERN_FILE, CTL_EOL);
	sysctl_createv(&clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_STRUCT, "file2",
		       SYSCTL_DESCR("System open file table"),
		       sysctl_kern_file2, 0, NULL, 0,
		       CTL_KERN, KERN_FILE2, CTL_EOL);
}

static bool
fd_isused(filedesc_t *fdp, unsigned fd)
{
	u_int off = fd >> NDENTRYSHIFT;

	KASSERT(fd < atomic_load_consume(&fdp->fd_dt)->dt_nfiles);

	return (fdp->fd_lomap[off] & (1U << (fd & NDENTRYMASK))) != 0;
}

/*
 * Verify that the bitmaps match the descriptor table.
 */
static inline void
fd_checkmaps(filedesc_t *fdp)
{
#ifdef DEBUG
	fdtab_t *dt;
	u_int fd;

	KASSERT(fdp->fd_refcnt <= 1 || mutex_owned(&fdp->fd_lock));

	dt = fdp->fd_dt;
	if (fdp->fd_refcnt == -1) {
		/*
		 * fd_free tears down the table without maintaining its bitmap.
		 */
		return;
	}
	for (fd = 0; fd < dt->dt_nfiles; fd++) {
		if (fd < NDFDFILE) {
			KASSERT(dt->dt_ff[fd] ==
			    (fdfile_t *)fdp->fd_dfdfile[fd]);
		}
		if (dt->dt_ff[fd] == NULL) {
			KASSERT(!fd_isused(fdp, fd));
		} else if (dt->dt_ff[fd]->ff_file != NULL) {
			KASSERT(fd_isused(fdp, fd));
		}
	}
#endif
}

static int
fd_next_zero(filedesc_t *fdp, uint32_t *bitmap, int want, u_int bits)
{
	int i, off, maxoff;
	uint32_t sub;

	KASSERT(mutex_owned(&fdp->fd_lock));

	fd_checkmaps(fdp);

	if (want > bits)
		return -1;

	off = want >> NDENTRYSHIFT;
	i = want & NDENTRYMASK;
	if (i) {
		sub = bitmap[off] | ((u_int)~0 >> (NDENTRIES - i));
		if (sub != ~0)
			goto found;
		off++;
	}

	maxoff = NDLOSLOTS(bits);
	while (off < maxoff) {
		if ((sub = bitmap[off]) != ~0)
			goto found;
		off++;
	}

	return -1;

 found:
	return (off << NDENTRYSHIFT) + ffs(~sub) - 1;
}

static int
fd_last_set(filedesc_t *fd, int last)
{
	int off, i;
	fdfile_t **ff = fd->fd_dt->dt_ff;
	uint32_t *bitmap = fd->fd_lomap;

	KASSERT(mutex_owned(&fd->fd_lock));

	fd_checkmaps(fd);

	off = (last - 1) >> NDENTRYSHIFT;

	while (off >= 0 && !bitmap[off])
		off--;

	if (off < 0)
		return -1;

	i = ((off + 1) << NDENTRYSHIFT) - 1;
	if (i >= last)
		i = last - 1;

	/* XXX should use bitmap */
	while (i > 0 && (ff[i] == NULL || !ff[i]->ff_allocated))
		i--;

	return i;
}

static inline void
fd_used(filedesc_t *fdp, unsigned fd)
{
	u_int off = fd >> NDENTRYSHIFT;
	fdfile_t *ff;

	ff = fdp->fd_dt->dt_ff[fd];

	KASSERT(mutex_owned(&fdp->fd_lock));
	KASSERT((fdp->fd_lomap[off] & (1U << (fd & NDENTRYMASK))) == 0);
	KASSERT(ff != NULL);
	KASSERT(ff->ff_file == NULL);
	KASSERT(!ff->ff_allocated);

	ff->ff_allocated = true;
	fdp->fd_lomap[off] |= 1U << (fd & NDENTRYMASK);
	if (__predict_false(fdp->fd_lomap[off] == ~0)) {
		KASSERT((fdp->fd_himap[off >> NDENTRYSHIFT] &
		    (1U << (off & NDENTRYMASK))) == 0);
		fdp->fd_himap[off >> NDENTRYSHIFT] |= 1U << (off & NDENTRYMASK);
	}

	if ((int)fd > fdp->fd_lastfile) {
		fdp->fd_lastfile = fd;
	}

	fd_checkmaps(fdp);
}

static inline void
fd_unused(filedesc_t *fdp, unsigned fd)
{
	u_int off = fd >> NDENTRYSHIFT;
	fdfile_t *ff;

	ff = fdp->fd_dt->dt_ff[fd];

	KASSERT(mutex_owned(&fdp->fd_lock));
	KASSERT(ff != NULL);
	KASSERT(ff->ff_file == NULL);
	KASSERT(ff->ff_allocated);

	if (fd < fdp->fd_freefile) {
		fdp->fd_freefile = fd;
	}

	if (fdp->fd_lomap[off] == ~0) {
		KASSERT((fdp->fd_himap[off >> NDENTRYSHIFT] &
		    (1U << (off & NDENTRYMASK))) != 0);
		fdp->fd_himap[off >> NDENTRYSHIFT] &=
		    ~(1U << (off & NDENTRYMASK));
	}
	KASSERT((fdp->fd_lomap[off] & (1U << (fd & NDENTRYMASK))) != 0);
	fdp->fd_lomap[off] &= ~(1U << (fd & NDENTRYMASK));
	ff->ff_allocated = false;

	KASSERT(fd <= fdp->fd_lastfile);
	if (fd == fdp->fd_lastfile) {
		fdp->fd_lastfile = fd_last_set(fdp, fd);
	}
	fd_checkmaps(fdp);
}

/*
 * Look up the file structure corresponding to a file descriptor
 * and return the file, holding a reference on the descriptor.
 */
file_t *
fd_getfile(unsigned fd)
{
	filedesc_t *fdp;
	fdfile_t *ff;
	file_t *fp;
	fdtab_t *dt;

	/*
	 * Look up the fdfile structure representing this descriptor.
	 * We are doing this unlocked.  See fd_tryexpand().
	 */
	fdp = curlwp->l_fd;
	dt = atomic_load_consume(&fdp->fd_dt);
	if (__predict_false(fd >= dt->dt_nfiles)) {
		return NULL;
	}
	ff = dt->dt_ff[fd];
	KASSERT(fd >= NDFDFILE || ff == (fdfile_t *)fdp->fd_dfdfile[fd]);
	if (__predict_false(ff == NULL)) {
		return NULL;
	}

	/* Now get a reference to the descriptor. */
	if (fdp->fd_refcnt == 1) {
		/*
		 * Single threaded: don't need to worry about concurrent
		 * access (other than earlier calls to kqueue, which may
		 * hold a reference to the descriptor).
		 */
		ff->ff_refcnt++;
	} else {
		/*
		 * Multi threaded: issue a memory barrier to ensure that we
		 * acquire the file pointer _after_ adding a reference.  If
		 * no memory barrier, we could fetch a stale pointer.
		 *
		 * In particular, we must coordinate the following four
		 * memory operations:
		 *
		 *	A. fd_close store ff->ff_file = NULL
		 *	B. fd_close refcnt = atomic_dec_uint_nv(&ff->ff_refcnt)
		 *	C. fd_getfile atomic_inc_uint(&ff->ff_refcnt)
		 *	D. fd_getfile load fp = ff->ff_file
		 *
		 * If the order is D;A;B;C:
		 *
		 *	1. D: fp = ff->ff_file
		 *	2. A: ff->ff_file = NULL
		 *	3. B: refcnt = atomic_dec_uint_nv(&ff->ff_refcnt)
		 *	4. C: atomic_inc_uint(&ff->ff_refcnt)
		 *
		 * then fd_close determines that there are no more
		 * references and decides to free fp immediately, at
		 * the same that fd_getfile ends up with an fp that's
		 * about to be freed.  *boom*
		 *
		 * By making B a release operation in fd_close, and by
		 * making C an acquire operation in fd_getfile, since
		 * they are atomic operations on the same object, which
		 * has a total modification order, we guarantee either:
		 *
		 *	- B happens before C.  Then since A is
		 *	  sequenced before B in fd_close, and C is
		 *	  sequenced before D in fd_getfile, we
		 *	  guarantee A happens before D, so fd_getfile
		 *	  reads a null fp and safely fails.
		 *
		 *	- C happens before B.  Then fd_getfile may read
		 *	  null or nonnull, but either way, fd_close
		 *	  will safely wait for references to drain.
		 */
		atomic_inc_uint(&ff->ff_refcnt);
		membar_acquire();
	}

	/*
	 * If the file is not open or is being closed then put the
	 * reference back.
	 */
	fp = atomic_load_consume(&ff->ff_file);
	if (__predict_true(fp != NULL)) {
		return fp;
	}
	fd_putfile(fd);
	return NULL;
}

/*
 * Release a reference to a file descriptor acquired with fd_getfile().
 */
void
fd_putfile(unsigned fd)
{
	filedesc_t *fdp;
	fdfile_t *ff;
	u_int u, v;

	fdp = curlwp->l_fd;
	KASSERT(fd < atomic_load_consume(&fdp->fd_dt)->dt_nfiles);
	ff = atomic_load_consume(&fdp->fd_dt)->dt_ff[fd];

	KASSERT(ff != NULL);
	KASSERT((ff->ff_refcnt & FR_MASK) > 0);
	KASSERT(fd >= NDFDFILE || ff == (fdfile_t *)fdp->fd_dfdfile[fd]);

	if (fdp->fd_refcnt == 1) {
		/*
		 * Single threaded: don't need to worry about concurrent
		 * access (other than earlier calls to kqueue, which may
		 * hold a reference to the descriptor).
		 */
		if (__predict_false((ff->ff_refcnt & FR_CLOSING) != 0)) {
			fd_close(fd);
			return;
		}
		ff->ff_refcnt--;
		return;
	}

	/*
	 * Ensure that any use of the file is complete and globally
	 * visible before dropping the final reference.  If no membar,
	 * the current CPU could still access memory associated with
	 * the file after it has been freed or recycled by another
	 * CPU.
	 */
	membar_release();

	/*
	 * Be optimistic and start out with the assumption that no other
	 * threads are trying to close the descriptor.  If the CAS fails,
	 * we lost a race and/or it's being closed.
	 */
	for (u = ff->ff_refcnt & FR_MASK;; u = v) {
		v = atomic_cas_uint(&ff->ff_refcnt, u, u - 1);
		if (__predict_true(u == v)) {
			return;
		}
		if (__predict_false((v & FR_CLOSING) != 0)) {
			break;
		}
	}

	/* Another thread is waiting to close the file: join it. */
	(void)fd_close(fd);
}

/*
 * Convenience wrapper around fd_getfile() that returns reference
 * to a vnode.
 */
int
fd_getvnode(unsigned fd, file_t **fpp)
{
	vnode_t *vp;
	file_t *fp;

	fp = fd_getfile(fd);
	if (__predict_false(fp == NULL)) {
		return EBADF;
	}
	if (__predict_false(fp->f_type != DTYPE_VNODE)) {
		fd_putfile(fd);
		return EINVAL;
	}
	vp = fp->f_vnode;
	if (__predict_false(vp->v_type == VBAD)) {
		/* XXX Is this case really necessary? */
		fd_putfile(fd);
		return EBADF;
	}
	*fpp = fp;
	return 0;
}

/*
 * Convenience wrapper around fd_getfile() that returns reference
 * to a socket.
 */
int
fd_getsock1(unsigned fd, struct socket **sop, file_t **fp)
{
	*fp = fd_getfile(fd);
	if (__predict_false(*fp == NULL)) {
		return EBADF;
	}
	if (__predict_false((*fp)->f_type != DTYPE_SOCKET)) {
		fd_putfile(fd);
		return ENOTSOCK;
	}
	*sop = (*fp)->f_socket;
	return 0;
}

int
fd_getsock(unsigned fd, struct socket **sop)
{
	file_t *fp;
	return fd_getsock1(fd, sop, &fp);
}

/*
 * Look up the file structure corresponding to a file descriptor
 * and return it with a reference held on the file, not the
 * descriptor.
 *
 * This is heavyweight and only used when accessing descriptors
 * from a foreign process.  The caller must ensure that `p' does
 * not exit or fork across this call.
 *
 * To release the file (not descriptor) reference, use closef().
 */
file_t *
fd_getfile2(proc_t *p, unsigned fd)
{
	filedesc_t *fdp;
	fdfile_t *ff;
	file_t *fp;
	fdtab_t *dt;

	fdp = p->p_fd;
	mutex_enter(&fdp->fd_lock);
	dt = fdp->fd_dt;
	if (fd >= dt->dt_nfiles) {
		mutex_exit(&fdp->fd_lock);
		return NULL;
	}
	if ((ff = dt->dt_ff[fd]) == NULL) {
		mutex_exit(&fdp->fd_lock);
		return NULL;
	}
	if ((fp = atomic_load_consume(&ff->ff_file)) == NULL) {
		mutex_exit(&fdp->fd_lock);
		return NULL;
	}
	mutex_enter(&fp->f_lock);
	fp->f_count++;
	mutex_exit(&fp->f_lock);
	mutex_exit(&fdp->fd_lock);

	return fp;
}

/*
 * Internal form of close.  Must be called with a reference to the
 * descriptor, and will drop the reference.  When all descriptor
 * references are dropped, releases the descriptor slot and a single
 * reference to the file structure.
 */
int
fd_close(unsigned fd)
{
	struct flock lf;
	filedesc_t *fdp;
	fdfile_t *ff;
	file_t *fp;
	proc_t *p;
	lwp_t *l;
	u_int refcnt;

	l = curlwp;
	p = l->l_proc;
	fdp = l->l_fd;
	ff = atomic_load_consume(&fdp->fd_dt)->dt_ff[fd];

	KASSERT(fd >= NDFDFILE || ff == (fdfile_t *)fdp->fd_dfdfile[fd]);

	mutex_enter(&fdp->fd_lock);
	KASSERT((ff->ff_refcnt & FR_MASK) > 0);
	fp = atomic_load_consume(&ff->ff_file);
	if (__predict_false(fp == NULL)) {
		/*
		 * Another user of the file is already closing, and is
		 * waiting for other users of the file to drain.  Release
		 * our reference, and wake up the closer.
		 */
		membar_release();
		atomic_dec_uint(&ff->ff_refcnt);
		cv_broadcast(&ff->ff_closing);
		mutex_exit(&fdp->fd_lock);

		/*
		 * An application error, so pretend that the descriptor
		 * was already closed.  We can't safely wait for it to
		 * be closed without potentially deadlocking.
		 */
		return (EBADF);
	}
	KASSERT((ff->ff_refcnt & FR_CLOSING) == 0);

	/*
	 * There may be multiple users of this file within the process.
	 * Notify existing and new users that the file is closing.  This
	 * will prevent them from adding additional uses to this file
	 * while we are closing it.
	 */
	atomic_store_relaxed(&ff->ff_file, NULL);
	ff->ff_exclose = false;
	ff->ff_foclose = false;

	/*
	 * We expect the caller to hold a descriptor reference - drop it.
	 * The reference count may increase beyond zero at this point due
	 * to an erroneous descriptor reference by an application, but
	 * fd_getfile() will notice that the file is being closed and drop
	 * the reference again.
	 */
	if (fdp->fd_refcnt == 1) {
		/* Single threaded. */
		refcnt = --(ff->ff_refcnt);
	} else {
		/* Multi threaded. */
		membar_release();
		refcnt = atomic_dec_uint_nv(&ff->ff_refcnt);
		membar_acquire();
	}
	if (__predict_false(refcnt != 0)) {
		/*
		 * Wait for other references to drain.  This is typically
		 * an application error - the descriptor is being closed
		 * while still in use.
		 * (Or just a threaded application trying to unblock its
		 * thread that sleeps in (say) accept()).
		 */
		atomic_or_uint(&ff->ff_refcnt, FR_CLOSING);

		/*
		 * Remove any knotes attached to the file.  A knote
		 * attached to the descriptor can hold references on it.
		 */
		mutex_exit(&fdp->fd_lock);
		if (!SLIST_EMPTY(&ff->ff_knlist)) {
			knote_fdclose(fd);
		}

		/*
		 * Since the file system code doesn't know which fd
		 * each request came from (think dup()), we have to
		 * ask it to return ERESTART for any long-term blocks.
		 * The re-entry through read/write/etc will detect the
		 * closed fd and return EBAFD.
		 * Blocked partial writes may return a short length.
		 */
		(*fp->f_ops->fo_restart)(fp);
		mutex_enter(&fdp->fd_lock);

		/*
		 * We need to see the count drop to zero at least once,
		 * in order to ensure that all pre-existing references
		 * have been drained.  New references past this point are
		 * of no interest.
		 * XXX (dsl) this may need to call fo_restart() after a
		 * timeout to guarantee that all the system calls exit.
		 */
		while ((ff->ff_refcnt & FR_MASK) != 0) {
			cv_wait(&ff->ff_closing, &fdp->fd_lock);
		}
		atomic_and_uint(&ff->ff_refcnt, ~FR_CLOSING);
	} else {
		/* If no references, there must be no knotes. */
		KASSERT(SLIST_EMPTY(&ff->ff_knlist));
	}

	/*
	 * POSIX record locking dictates that any close releases ALL
	 * locks owned by this process.  This is handled by setting
	 * a flag in the unlock to free ONLY locks obeying POSIX
	 * semantics, and not to free BSD-style file locks.
	 * If the descriptor was in a message, POSIX-style locks
	 * aren't passed with the descriptor.
	 */
	if (__predict_false((p->p_flag & PK_ADVLOCK) != 0) &&
	    fp->f_ops->fo_advlock != NULL) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		lf.l_type = F_UNLCK;
		mutex_exit(&fdp->fd_lock);
		(void)(*fp->f_ops->fo_advlock)(fp, p, F_UNLCK, &lf, F_POSIX);
		mutex_enter(&fdp->fd_lock);
	}

	/* Free descriptor slot. */
	fd_unused(fdp, fd);
	mutex_exit(&fdp->fd_lock);

	/* Now drop reference to the file itself. */
	return closef(fp);
}

/*
 * Duplicate a file descriptor.
 */
int
fd_dup(file_t *fp, int minfd, int *newp, bool exclose, bool foclose)
{
	proc_t *p = curproc;
	int error;

	while ((error = fd_alloc(p, minfd, newp)) != 0) {
		if (error != ENOSPC) {
			return error;
		}
		fd_tryexpand(p);
	}

	fd_set_exclose(curlwp, *newp, exclose);
	fd_set_foclose(curlwp, *newp, foclose);
	fd_affix(p, fp, *newp);
	return 0;
}

/*
 * dup2 operation.
 */
int
fd_dup2(file_t *fp, unsigned newfd, int flags)
{
	filedesc_t *fdp = curlwp->l_fd;
	fdfile_t *ff;
	fdtab_t *dt;

	if (flags & ~(O_CLOEXEC|O_CLOFORK|O_NONBLOCK|O_NOSIGPIPE))
		return EINVAL;
	/*
	 * Ensure there are enough slots in the descriptor table,
	 * and allocate an fdfile_t up front in case we need it.
	 */
	while (newfd >= atomic_load_consume(&fdp->fd_dt)->dt_nfiles) {
		fd_tryexpand(curproc);
	}
	ff = kmem_alloc(sizeof(*ff), KM_SLEEP);
	fdfile_ctor(ff);

	/*
	 * If there is already a file open, close it.  If the file is
	 * half open, wait for it to be constructed before closing it.
	 * XXX Potential for deadlock here?
	 */
	mutex_enter(&fdp->fd_lock);
	while (fd_isused(fdp, newfd)) {
		mutex_exit(&fdp->fd_lock);
		if (fd_getfile(newfd) != NULL) {
			(void)fd_close(newfd);
		} else {
			/*
			 * Crummy, but unlikely to happen.
			 * Can occur if we interrupt another
			 * thread while it is opening a file.
			 */
			kpause("dup2", false, 1, NULL);
		}
		mutex_enter(&fdp->fd_lock);
	}
	dt = fdp->fd_dt;
	if (dt->dt_ff[newfd] == NULL) {
		KASSERT(newfd >= NDFDFILE);
		dt->dt_ff[newfd] = ff;
		ff = NULL;
	}
	fd_used(fdp, newfd);
	mutex_exit(&fdp->fd_lock);

	fd_set_exclose(curlwp, newfd, (flags & O_CLOEXEC) != 0);
	fd_set_foclose(curlwp, newfd, (flags & O_CLOFORK) != 0);
	fp->f_flag |= flags & (FNONBLOCK|FNOSIGPIPE);
	/* Slot is now allocated.  Insert copy of the file. */
	fd_affix(curproc, fp, newfd);
	if (ff != NULL) {
		cv_destroy(&ff->ff_closing);
		kmem_free(ff, sizeof(*ff));
	}
	return 0;
}

/*
 * Drop reference to a file structure.
 */
int
closef(file_t *fp)
{
	struct flock lf;
	int error;

	/*
	 * Drop reference.  If referenced elsewhere it's still open
	 * and we have nothing more to do.
	 */
	mutex_enter(&fp->f_lock);
	KASSERT(fp->f_count > 0);
	if (--fp->f_count > 0) {
		mutex_exit(&fp->f_lock);
		return 0;
	}
	KASSERT(fp->f_count == 0);
	mutex_exit(&fp->f_lock);

	/* We held the last reference - release locks, close and free. */
	if (fp->f_ops->fo_advlock == NULL) {
		KASSERT((fp->f_flag & FHASLOCK) == 0);
	} else if (fp->f_flag & FHASLOCK) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		lf.l_type = F_UNLCK;
		(void)(*fp->f_ops->fo_advlock)(fp, fp, F_UNLCK, &lf, F_FLOCK);
	}
	if (fp->f_ops != NULL) {
		error = (*fp->f_ops->fo_close)(fp);

		/*
		 * .fo_close is final, so real errors are frowned on
		 * (but allowed and passed on to close(2)), and
		 * ERESTART is absolutely forbidden because the file
		 * descriptor is gone and there is no chance to retry.
		 */
		KASSERTMSG(error != ERESTART,
		    "file %p f_ops %p fo_close %p returned ERESTART",
		    fp, fp->f_ops, fp->f_ops->fo_close);
	} else {
		error = 0;
	}
	KASSERT(fp->f_count == 0);
	KASSERT(fp->f_cred != NULL);
	pool_cache_put(file_cache, fp);

	return error;
}

/*
 * Allocate a file descriptor for the process.
 *
 * Future idea for experimentation: replace all of this with radixtree.
 */
int
fd_alloc(proc_t *p, int want, int *result)
{
	filedesc_t *fdp = p->p_fd;
	int i, lim, last, error, hi;
	u_int off;
	fdtab_t *dt;

	KASSERT(p == curproc || p == &proc0);

	/*
	 * Search for a free descriptor starting at the higher
	 * of want or fd_freefile.
	 */
	mutex_enter(&fdp->fd_lock);
	fd_checkmaps(fdp);
	dt = fdp->fd_dt;
	KASSERT(dt->dt_ff[0] == (fdfile_t *)fdp->fd_dfdfile[0]);
	lim = uimin((int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur, maxfiles);
	last = uimin(dt->dt_nfiles, lim);

	for (;;) {
		if ((i = want) < fdp->fd_freefile)
			i = fdp->fd_freefile;
		off = i >> NDENTRYSHIFT;
		hi = fd_next_zero(fdp, fdp->fd_himap, off,
		    (last + NDENTRIES - 1) >> NDENTRYSHIFT);
		if (hi == -1)
			break;
		i = fd_next_zero(fdp, &fdp->fd_lomap[hi],
		    hi > off ? 0 : i & NDENTRYMASK, NDENTRIES);
		if (i == -1) {
			/*
			 * Free file descriptor in this block was
			 * below want, try again with higher want.
			 */
			want = (hi + 1) << NDENTRYSHIFT;
			continue;
		}
		i += (hi << NDENTRYSHIFT);
		if (i >= last) {
			break;
		}
		if (dt->dt_ff[i] == NULL) {
			KASSERT(i >= NDFDFILE);
			dt->dt_ff[i] = kmem_alloc(sizeof(fdfile_t), KM_SLEEP);
			fdfile_ctor(dt->dt_ff[i]);
		}
		KASSERT(dt->dt_ff[i]->ff_file == NULL);
		fd_used(fdp, i);
		if (want <= fdp->fd_freefile) {
			fdp->fd_freefile = i;
		}
		*result = i;
		KASSERT(i >= NDFDFILE ||
		    dt->dt_ff[i] == (fdfile_t *)fdp->fd_dfdfile[i]);
		fd_checkmaps(fdp);
		mutex_exit(&fdp->fd_lock);
		return 0;
	}

	/* No space in current array.  Let the caller expand and retry. */
	error = (dt->dt_nfiles >= lim) ? EMFILE : ENOSPC;
	mutex_exit(&fdp->fd_lock);
	return error;
}

/*
 * Allocate memory for a descriptor table.
 */
static fdtab_t *
fd_dtab_alloc(int n)
{
	fdtab_t *dt;
	size_t sz;

	KASSERT(n > NDFILE);

	sz = sizeof(*dt) + (n - NDFILE) * sizeof(dt->dt_ff[0]);
	dt = kmem_alloc(sz, KM_SLEEP);
#ifdef DIAGNOSTIC
	memset(dt, 0xff, sz);
#endif
	dt->dt_nfiles = n;
	dt->dt_link = NULL;
	return dt;
}

/*
 * Free a descriptor table, and all tables linked for deferred free.
 */
static void
fd_dtab_free(fdtab_t *dt)
{
	fdtab_t *next;
	size_t sz;

	do {
		next = dt->dt_link;
		KASSERT(dt->dt_nfiles > NDFILE);
		sz = sizeof(*dt) +
		    (dt->dt_nfiles - NDFILE) * sizeof(dt->dt_ff[0]);
#ifdef DIAGNOSTIC
		memset(dt, 0xff, sz);
#endif
		kmem_free(dt, sz);
		dt = next;
	} while (dt != NULL);
}

/*
 * Allocate descriptor bitmap.
 */
static void
fd_map_alloc(int n, uint32_t **lo, uint32_t **hi)
{
	uint8_t *ptr;
	size_t szlo, szhi;

	KASSERT(n > NDENTRIES);

	szlo = NDLOSLOTS(n) * sizeof(uint32_t);
	szhi = NDHISLOTS(n) * sizeof(uint32_t);
	ptr = kmem_alloc(szlo + szhi, KM_SLEEP);
	*lo = (uint32_t *)ptr;
	*hi = (uint32_t *)(ptr + szlo);
}

/*
 * Free descriptor bitmap.
 */
static void
fd_map_free(int n, uint32_t *lo, uint32_t *hi)
{
	size_t szlo, szhi;

	KASSERT(n > NDENTRIES);

	szlo = NDLOSLOTS(n) * sizeof(uint32_t);
	szhi = NDHISLOTS(n) * sizeof(uint32_t);
	KASSERT(hi == (uint32_t *)((uint8_t *)lo + szlo));
	kmem_free(lo, szlo + szhi);
}

/*
 * Expand a process' descriptor table.
 */
void
fd_tryexpand(proc_t *p)
{
	filedesc_t *fdp;
	int i, numfiles, oldnfiles;
	fdtab_t *newdt, *dt;
	uint32_t *newhimap, *newlomap;

	KASSERT(p == curproc || p == &proc0);

	fdp = p->p_fd;
	newhimap = NULL;
	newlomap = NULL;
	oldnfiles = atomic_load_consume(&fdp->fd_dt)->dt_nfiles;

	if (oldnfiles < NDEXTENT)
		numfiles = NDEXTENT;
	else
		numfiles = 2 * oldnfiles;

	newdt = fd_dtab_alloc(numfiles);
	if (NDHISLOTS(numfiles) > NDHISLOTS(oldnfiles)) {
		fd_map_alloc(numfiles, &newlomap, &newhimap);
	}

	mutex_enter(&fdp->fd_lock);
	dt = fdp->fd_dt;
	KASSERT(dt->dt_ff[0] == (fdfile_t *)fdp->fd_dfdfile[0]);
	if (dt->dt_nfiles != oldnfiles) {
		/* fdp changed; caller must retry */
		mutex_exit(&fdp->fd_lock);
		fd_dtab_free(newdt);
		if (NDHISLOTS(numfiles) > NDHISLOTS(oldnfiles)) {
			fd_map_free(numfiles, newlomap, newhimap);
		}
		return;
	}

	/* Copy the existing descriptor table and zero the new portion. */
	i = sizeof(fdfile_t *) * oldnfiles;
	memcpy(newdt->dt_ff, dt->dt_ff, i);
	memset((uint8_t *)newdt->dt_ff + i, 0,
	    numfiles * sizeof(fdfile_t *) - i);

	/*
	 * Link old descriptor array into list to be discarded.  We defer
	 * freeing until the last reference to the descriptor table goes
	 * away (usually process exit).  This allows us to do lockless
	 * lookups in fd_getfile().
	 */
	if (oldnfiles > NDFILE) {
		if (fdp->fd_refcnt > 1) {
			newdt->dt_link = dt;
		} else {
			fd_dtab_free(dt);
		}
	}

	if (NDHISLOTS(numfiles) > NDHISLOTS(oldnfiles)) {
		i = NDHISLOTS(oldnfiles) * sizeof(uint32_t);
		memcpy(newhimap, fdp->fd_himap, i);
		memset((uint8_t *)newhimap + i, 0,
		    NDHISLOTS(numfiles) * sizeof(uint32_t) - i);

		i = NDLOSLOTS(oldnfiles) * sizeof(uint32_t);
		memcpy(newlomap, fdp->fd_lomap, i);
		memset((uint8_t *)newlomap + i, 0,
		    NDLOSLOTS(numfiles) * sizeof(uint32_t) - i);

		if (NDHISLOTS(oldnfiles) > NDHISLOTS(NDFILE)) {
			fd_map_free(oldnfiles, fdp->fd_lomap, fdp->fd_himap);
		}
		fdp->fd_himap = newhimap;
		fdp->fd_lomap = newlomap;
	}

	/*
	 * All other modifications must become globally visible before
	 * the change to fd_dt.  See fd_getfile().
	 */
	atomic_store_release(&fdp->fd_dt, newdt);
	KASSERT(newdt->dt_ff[0] == (fdfile_t *)fdp->fd_dfdfile[0]);
	fd_checkmaps(fdp);
	mutex_exit(&fdp->fd_lock);
}

/*
 * Create a new open file structure and allocate a file descriptor
 * for the current process.
 */
int
fd_allocfile(file_t **resultfp, int *resultfd)
{
	proc_t *p = curproc;
	kauth_cred_t cred;
	file_t *fp;
	int error;

	while ((error = fd_alloc(p, 0, resultfd)) != 0) {
		if (error != ENOSPC) {
			return error;
		}
		fd_tryexpand(p);
	}

	fp = pool_cache_get(file_cache, PR_WAITOK);
	if (fp == NULL) {
		fd_abort(p, NULL, *resultfd);
		return ENFILE;
	}
	KASSERT(fp->f_count == 0);
	KASSERT(fp->f_msgcount == 0);
	KASSERT(fp->f_unpcount == 0);

	/* Replace cached credentials if not what we need. */
	cred = curlwp->l_cred;
	if (__predict_false(cred != fp->f_cred)) {
		kauth_cred_free(fp->f_cred);
		fp->f_cred = kauth_cred_hold(cred);
	}

	/*
	 * Don't allow recycled files to be scanned.
	 * See uipc_usrreq.c.
	 */
	if (__predict_false((fp->f_flag & FSCAN) != 0)) {
		mutex_enter(&fp->f_lock);
		atomic_and_uint(&fp->f_flag, ~FSCAN);
		mutex_exit(&fp->f_lock);
	}

	fp->f_advice = 0;
	fp->f_offset = 0;
	*resultfp = fp;

	return 0;
}

/*
 * Successful creation of a new descriptor: make visible to the process.
 */
void
fd_affix(proc_t *p, file_t *fp, unsigned fd)
{
	fdfile_t *ff;
	filedesc_t *fdp;
	fdtab_t *dt;

	KASSERT(p == curproc || p == &proc0);

	/* Add a reference to the file structure. */
	mutex_enter(&fp->f_lock);
	fp->f_count++;
	mutex_exit(&fp->f_lock);

	/*
	 * Insert the new file into the descriptor slot.
	 */
	fdp = p->p_fd;
	dt = atomic_load_consume(&fdp->fd_dt);
	ff = dt->dt_ff[fd];

	KASSERT(ff != NULL);
	KASSERT(ff->ff_file == NULL);
	KASSERT(ff->ff_allocated);
	KASSERT(fd_isused(fdp, fd));
	KASSERT(fd >= NDFDFILE || ff == (fdfile_t *)fdp->fd_dfdfile[fd]);

	/* No need to lock in order to make file initially visible. */
	atomic_store_release(&ff->ff_file, fp);
}

/*
 * Abort creation of a new descriptor: free descriptor slot and file.
 */
void
fd_abort(proc_t *p, file_t *fp, unsigned fd)
{
	filedesc_t *fdp;
	fdfile_t *ff;

	KASSERT(p == curproc || p == &proc0);

	fdp = p->p_fd;
	ff = atomic_load_consume(&fdp->fd_dt)->dt_ff[fd];
	ff->ff_exclose = false;
	ff->ff_foclose = false;

	KASSERT(fd >= NDFDFILE || ff == (fdfile_t *)fdp->fd_dfdfile[fd]);

	mutex_enter(&fdp->fd_lock);
	KASSERT(fd_isused(fdp, fd));
	fd_unused(fdp, fd);
	mutex_exit(&fdp->fd_lock);

	if (fp != NULL) {
		KASSERT(fp->f_count == 0);
		KASSERT(fp->f_cred != NULL);
		pool_cache_put(file_cache, fp);
	}
}

static int
file_ctor(void *arg, void *obj, int flags)
{
	/*
	 * It's easy to exhaust the open file limit on a system with many
	 * CPUs due to caching.  Allow a bit of leeway to reduce the element
	 * of surprise.
	 */
	u_int slop = PCG_NOBJECTS_NORMAL * (ncpu - 1);
	file_t *fp = obj;

	memset(fp, 0, sizeof(*fp));

	mutex_enter(&filelist_lock);
	if (__predict_false(nfiles >= slop + maxfiles)) {
		mutex_exit(&filelist_lock);
		tablefull("file", "increase kern.maxfiles or MAXFILES");
		return ENFILE;
	}
	nfiles++;
	LIST_INSERT_HEAD(&filehead, fp, f_list);
	mutex_init(&fp->f_lock, MUTEX_DEFAULT, IPL_NONE);
	fp->f_cred = kauth_cred_hold(curlwp->l_cred);
	mutex_exit(&filelist_lock);

	return 0;
}

static void
file_dtor(void *arg, void *obj)
{
	file_t *fp = obj;

	mutex_enter(&filelist_lock);
	nfiles--;
	LIST_REMOVE(fp, f_list);
	mutex_exit(&filelist_lock);

	KASSERT(fp->f_count == 0);
	kauth_cred_free(fp->f_cred);
	mutex_destroy(&fp->f_lock);
}

static void
fdfile_ctor(fdfile_t *ff)
{

	memset(ff, 0, sizeof(*ff));
	cv_init(&ff->ff_closing, "fdclose");
}

static void
fdfile_dtor(fdfile_t *ff)
{

	cv_destroy(&ff->ff_closing);
}

file_t *
fgetdummy(void)
{
	file_t *fp;

	fp = kmem_zalloc(sizeof(*fp), KM_SLEEP);
	mutex_init(&fp->f_lock, MUTEX_DEFAULT, IPL_NONE);
	return fp;
}

void
fputdummy(file_t *fp)
{

	mutex_destroy(&fp->f_lock);
	kmem_free(fp, sizeof(*fp));
}

/*
 * Create an initial filedesc structure.
 */
filedesc_t *
fd_init(filedesc_t *fdp)
{
#ifdef DIAGNOSTIC
	unsigned fd;
#endif

	if (__predict_true(fdp == NULL)) {
		fdp = pool_cache_get(filedesc_cache, PR_WAITOK);
	} else {
		KASSERT(fdp == &filedesc0);
		filedesc_ctor(NULL, fdp, PR_WAITOK);
	}

#ifdef DIAGNOSTIC
	KASSERT(fdp->fd_lastfile == -1);
	KASSERT(fdp->fd_lastkqfile == -1);
	KASSERT(fdp->fd_knhash == NULL);
	KASSERT(fdp->fd_freefile == 0);
	KASSERT(fdp->fd_exclose == false);
	KASSERT(fdp->fd_foclose == false);
	KASSERT(fdp->fd_dt == &fdp->fd_dtbuiltin);
	KASSERT(fdp->fd_dtbuiltin.dt_nfiles == NDFILE);
	for (fd = 0; fd < NDFDFILE; fd++) {
		KASSERT(fdp->fd_dtbuiltin.dt_ff[fd] ==
		    (fdfile_t *)fdp->fd_dfdfile[fd]);
	}
	for (fd = NDFDFILE; fd < NDFILE; fd++) {
		KASSERT(fdp->fd_dtbuiltin.dt_ff[fd] == NULL);
	}
	KASSERT(fdp->fd_himap == fdp->fd_dhimap);
	KASSERT(fdp->fd_lomap == fdp->fd_dlomap);
#endif	/* DIAGNOSTIC */

	fdp->fd_refcnt = 1;
	fd_checkmaps(fdp);

	return fdp;
}

/*
 * Initialize a file descriptor table.
 */
static int
filedesc_ctor(void *arg, void *obj, int flag)
{
	filedesc_t *fdp = obj;
	fdfile_t **ffp;
	int i;

	memset(fdp, 0, sizeof(*fdp));
	mutex_init(&fdp->fd_lock, MUTEX_DEFAULT, IPL_NONE);
	fdp->fd_lastfile = -1;
	fdp->fd_lastkqfile = -1;
	fdp->fd_dt = &fdp->fd_dtbuiltin;
	fdp->fd_dtbuiltin.dt_nfiles = NDFILE;
	fdp->fd_himap = fdp->fd_dhimap;
	fdp->fd_lomap = fdp->fd_dlomap;

	CTASSERT(sizeof(fdp->fd_dfdfile[0]) >= sizeof(fdfile_t));
	for (i = 0, ffp = fdp->fd_dt->dt_ff; i < NDFDFILE; i++, ffp++) {
		fdfile_ctor(*ffp = (fdfile_t *)fdp->fd_dfdfile[i]);
	}

	return 0;
}

static void
filedesc_dtor(void *arg, void *obj)
{
	filedesc_t *fdp = obj;
	int i;

	for (i = 0; i < NDFDFILE; i++) {
		fdfile_dtor((fdfile_t *)fdp->fd_dfdfile[i]);
	}

	mutex_destroy(&fdp->fd_lock);
}

/*
 * Make p share curproc's filedesc structure.
 */
void
fd_share(struct proc *p)
{
	filedesc_t *fdp;

	fdp = curlwp->l_fd;
	p->p_fd = fdp;
	atomic_inc_uint(&fdp->fd_refcnt);
}

/*
 * Acquire a hold on a filedesc structure.
 */
void
fd_hold(lwp_t *l)
{
	filedesc_t *fdp = l->l_fd;

	atomic_inc_uint(&fdp->fd_refcnt);
}

/*
 * Copy a filedesc structure.
 */
filedesc_t *
fd_copy(void)
{
	filedesc_t *newfdp, *fdp;
	fdfile_t *ff, **ffp, **nffp, *ff2;
	int i, j, numfiles, lastfile, newlast;
	file_t *fp;
	fdtab_t *newdt;

	fdp = curproc->p_fd;
	newfdp = pool_cache_get(filedesc_cache, PR_WAITOK);
	newfdp->fd_refcnt = 1;

#ifdef DIAGNOSTIC
	KASSERT(newfdp->fd_lastfile == -1);
	KASSERT(newfdp->fd_lastkqfile == -1);
	KASSERT(newfdp->fd_knhash == NULL);
	KASSERT(newfdp->fd_freefile == 0);
	KASSERT(newfdp->fd_exclose == false);
	KASSERT(newfdp->fd_foclose == false);
	KASSERT(newfdp->fd_dt == &newfdp->fd_dtbuiltin);
	KASSERT(newfdp->fd_dtbuiltin.dt_nfiles == NDFILE);
	for (i = 0; i < NDFDFILE; i++) {
		KASSERT(newfdp->fd_dtbuiltin.dt_ff[i] ==
		    (fdfile_t *)&newfdp->fd_dfdfile[i]);
	}
	for (i = NDFDFILE; i < NDFILE; i++) {
		KASSERT(newfdp->fd_dtbuiltin.dt_ff[i] == NULL);
	}
#endif	/* DIAGNOSTIC */

	mutex_enter(&fdp->fd_lock);
	fd_checkmaps(fdp);
	numfiles = fdp->fd_dt->dt_nfiles;
	lastfile = fdp->fd_lastfile;

	/*
	 * If the number of open files fits in the internal arrays
	 * of the open file structure, use them, otherwise allocate
	 * additional memory for the number of descriptors currently
	 * in use.
	 */
	if (lastfile < NDFILE) {
		i = NDFILE;
		newdt = newfdp->fd_dt;
		KASSERT(newfdp->fd_dt == &newfdp->fd_dtbuiltin);
	} else {
		/*
		 * Compute the smallest multiple of NDEXTENT needed
		 * for the file descriptors currently in use,
		 * allowing the table to shrink.
		 */
		i = numfiles;
		while (i >= 2 * NDEXTENT && i > lastfile * 2) {
			i /= 2;
		}
		KASSERT(i > NDFILE);
		newdt = fd_dtab_alloc(i);
		newfdp->fd_dt = newdt;
		memcpy(newdt->dt_ff, newfdp->fd_dtbuiltin.dt_ff,
		    NDFDFILE * sizeof(fdfile_t **));
		memset(newdt->dt_ff + NDFDFILE, 0,
		    (i - NDFDFILE) * sizeof(fdfile_t **));
	}
	if (NDHISLOTS(i) <= NDHISLOTS(NDFILE)) {
		newfdp->fd_himap = newfdp->fd_dhimap;
		newfdp->fd_lomap = newfdp->fd_dlomap;
	} else {
		fd_map_alloc(i, &newfdp->fd_lomap, &newfdp->fd_himap);
		KASSERT(i >= NDENTRIES * NDENTRIES);
		memset(newfdp->fd_himap, 0, NDHISLOTS(i)*sizeof(uint32_t));
		memset(newfdp->fd_lomap, 0, NDLOSLOTS(i)*sizeof(uint32_t));
	}
	newfdp->fd_freefile = fdp->fd_freefile;
	newfdp->fd_exclose = fdp->fd_exclose;
	newfdp->fd_foclose = false;	/* no close-on-fork will be copied */

	ffp = fdp->fd_dt->dt_ff;
	nffp = newdt->dt_ff;
	newlast = -1;
	for (i = 0; i <= lastfile; i++, ffp++, nffp++) {
		KASSERT(i >= NDFDFILE ||
		    *nffp == (fdfile_t *)newfdp->fd_dfdfile[i]);
		ff = *ffp;
		if (ff == NULL ||
		    (fp = atomic_load_consume(&ff->ff_file)) == NULL) {
			/* Descriptor unused, or descriptor half open. */
			KASSERT(!fd_isused(newfdp, i));
			continue;
		}
		if (__predict_false(ff->ff_foclose ||
				    fp->f_type == DTYPE_KQUEUE)) {
			/* kqueue descriptors cannot be copied. */
			/* close-on-fork descriptors aren't either */
			if (i < newfdp->fd_freefile) {
				newfdp->fd_freefile = i;
			}
			continue;
		}
		/* It's active: add a reference to the file. */
		mutex_enter(&fp->f_lock);
		fp->f_count++;
		mutex_exit(&fp->f_lock);

		/* Allocate an fdfile_t to represent it. */
		if (i >= NDFDFILE) {
			ff2 = kmem_alloc(sizeof(*ff2), KM_SLEEP);
			fdfile_ctor(ff2);
			*nffp = ff2;
		} else {
			ff2 = newdt->dt_ff[i];
		}
		ff2->ff_file = fp;
		ff2->ff_exclose = ff->ff_exclose;
		ff2->ff_foclose = false;
		ff2->ff_allocated = true;

		/* Fix up bitmaps. */
		j = i >> NDENTRYSHIFT;
		KASSERT((newfdp->fd_lomap[j] & (1U << (i & NDENTRYMASK))) == 0);
		newfdp->fd_lomap[j] |= 1U << (i & NDENTRYMASK);
		if (__predict_false(newfdp->fd_lomap[j] == ~0)) {
			KASSERT((newfdp->fd_himap[j >> NDENTRYSHIFT] &
			    (1U << (j & NDENTRYMASK))) == 0);
			newfdp->fd_himap[j >> NDENTRYSHIFT] |=
			    1U << (j & NDENTRYMASK);
		}
		newlast = i;
	}
	KASSERT(newdt->dt_ff[0] == (fdfile_t *)newfdp->fd_dfdfile[0]);
	newfdp->fd_lastfile = newlast;
	fd_checkmaps(newfdp);
	mutex_exit(&fdp->fd_lock);

	return newfdp;
}

/*
 * Release a filedesc structure.
 */
void
fd_free(void)
{
	fdfile_t *ff;
	file_t *fp;
	int fd, nf;
	fdtab_t *dt;
	lwp_t * const l = curlwp;
	filedesc_t * const fdp = l->l_fd;
	const bool noadvlock = (l->l_proc->p_flag & PK_ADVLOCK) == 0;

	KASSERT(atomic_load_consume(&fdp->fd_dt)->dt_ff[0] ==
	    (fdfile_t *)fdp->fd_dfdfile[0]);
	KASSERT(fdp->fd_dtbuiltin.dt_nfiles == NDFILE);
	KASSERT(fdp->fd_dtbuiltin.dt_link == NULL);

	membar_release();
	if (atomic_dec_uint_nv(&fdp->fd_refcnt) > 0)
		return;
	membar_acquire();

	/*
	 * Close any files that the process holds open.
	 */
	dt = fdp->fd_dt;
	fd_checkmaps(fdp);
#ifdef DEBUG
	fdp->fd_refcnt = -1; /* see fd_checkmaps */
#endif
	for (fd = 0, nf = dt->dt_nfiles; fd < nf; fd++) {
		ff = dt->dt_ff[fd];
		KASSERT(fd >= NDFDFILE ||
		    ff == (fdfile_t *)fdp->fd_dfdfile[fd]);
		if (ff == NULL)
			continue;
		if ((fp = atomic_load_consume(&ff->ff_file)) != NULL) {
			/*
			 * Must use fd_close() here if there is
			 * a reference from kqueue or we might have posix
			 * advisory locks.
			 */
			if (__predict_true(ff->ff_refcnt == 0) &&
			    (noadvlock || fp->f_type != DTYPE_VNODE)) {
				ff->ff_file = NULL;
				ff->ff_exclose = false;
				ff->ff_foclose = false;
				ff->ff_allocated = false;
				closef(fp);
			} else {
				ff->ff_refcnt++;
				fd_close(fd);
			}
		}
		KASSERT(ff->ff_refcnt == 0);
		KASSERT(ff->ff_file == NULL);
		KASSERT(!ff->ff_exclose);
		KASSERT(!ff->ff_foclose);
		KASSERT(!ff->ff_allocated);
		if (fd >= NDFDFILE) {
			cv_destroy(&ff->ff_closing);
			kmem_free(ff, sizeof(*ff));
			dt->dt_ff[fd] = NULL;
		}
	}

	/*
	 * Clean out the descriptor table for the next user and return
	 * to the cache.
	 */
	if (__predict_false(dt != &fdp->fd_dtbuiltin)) {
		fd_dtab_free(fdp->fd_dt);
		/* Otherwise, done above. */
		memset(&fdp->fd_dtbuiltin.dt_ff[NDFDFILE], 0,
		    (NDFILE - NDFDFILE) * sizeof(fdp->fd_dtbuiltin.dt_ff[0]));
		fdp->fd_dt = &fdp->fd_dtbuiltin;
	}
	if (__predict_false(NDHISLOTS(nf) > NDHISLOTS(NDFILE))) {
		KASSERT(fdp->fd_himap != fdp->fd_dhimap);
		KASSERT(fdp->fd_lomap != fdp->fd_dlomap);
		fd_map_free(nf, fdp->fd_lomap, fdp->fd_himap);
	}
	if (__predict_false(fdp->fd_knhash != NULL)) {
		hashdone(fdp->fd_knhash, HASH_LIST, fdp->fd_knhashmask);
		fdp->fd_knhash = NULL;
		fdp->fd_knhashmask = 0;
	} else {
		KASSERT(fdp->fd_knhashmask == 0);
	}
	fdp->fd_dt = &fdp->fd_dtbuiltin;
	fdp->fd_lastkqfile = -1;
	fdp->fd_lastfile = -1;
	fdp->fd_freefile = 0;
	fdp->fd_exclose = false;
	fdp->fd_foclose = false;
	memset(&fdp->fd_startzero, 0, sizeof(*fdp) -
	    offsetof(filedesc_t, fd_startzero));
	fdp->fd_himap = fdp->fd_dhimap;
	fdp->fd_lomap = fdp->fd_dlomap;
	KASSERT(fdp->fd_dtbuiltin.dt_nfiles == NDFILE);
	KASSERT(fdp->fd_dtbuiltin.dt_link == NULL);
	KASSERT(fdp->fd_dt == &fdp->fd_dtbuiltin);
#ifdef DEBUG
	fdp->fd_refcnt = 0; /* see fd_checkmaps */
#endif
	fd_checkmaps(fdp);
	pool_cache_put(filedesc_cache, fdp);
}

/*
 * File Descriptor pseudo-device driver (/dev/fd/).
 *
 * Opening minor device N dup()s the file (if any) connected to file
 * descriptor N belonging to the calling process.  Note that this driver
 * consists of only the ``open()'' routine, because all subsequent
 * references to this file will be direct to the other driver.
 */
static int
filedescopen(dev_t dev, int mode, int type, lwp_t *l)
{

	/*
	 * XXX Kludge: set dupfd to contain the value of the
	 * the file descriptor being sought for duplication. The error
	 * return ensures that the vnode for this device will be released
	 * by vn_open. Open will detect this special error and take the
	 * actions in fd_dupopen below. Other callers of vn_open or VOP_OPEN
	 * will simply report the error.
	 */
	l->l_dupfd = minor(dev);	/* XXX */
	return EDUPFD;
}

/*
 * Duplicate the specified descriptor to a free descriptor.
 *
 * old is the original fd.
 * moveit is true if we should move rather than duplicate.
 * flags are the open flags (converted from O_* to F*).
 * newp returns the new fd on success.
 *
 * These two cases are produced by the EDUPFD and EMOVEFD magic
 * errnos, but in the interest of removing that regrettable interface,
 * vn_open has been changed to intercept them. Now vn_open returns
 * either a vnode or a filehandle, and the filehandle is accompanied
 * by a boolean that says whether we should dup (moveit == false) or
 * move (moveit == true) the fd.
 *
 * The dup case is used by /dev/stderr, /proc/self/fd, and such. The
 * move case is used by cloner devices that allocate a fd of their
 * own (a layering violation that should go away eventually) that
 * then needs to be put in the place open() expects it.
 */
int
fd_dupopen(int old, bool moveit, int flags, int *newp)
{
	filedesc_t *fdp;
	fdfile_t *ff;
	file_t *fp;
	fdtab_t *dt;
	int error;

	if ((fp = fd_getfile(old)) == NULL) {
		return EBADF;
	}
	fdp = curlwp->l_fd;
	dt = atomic_load_consume(&fdp->fd_dt);
	ff = dt->dt_ff[old];

	/*
	 * There are two cases of interest here.
	 *
	 * 1. moveit == false (used to be the EDUPFD magic errno):
	 *    simply dup (old) to file descriptor (new) and return.
	 *
	 * 2. moveit == true (used to be the EMOVEFD magic errno):
	 *    steal away the file structure from (old) and store it in
	 *    (new).  (old) is effectively closed by this operation.
	 */
	if (moveit == false) {
		/*
		 * Check that the mode the file is being opened for is a
		 * subset of the mode of the existing descriptor.
		 */
		if (((flags & (FREAD|FWRITE)) | fp->f_flag) != fp->f_flag) {
			error = EACCES;
			goto out;
		}

		/* Copy it. */
		error = fd_dup(fp, 0, newp, ff->ff_exclose, ff->ff_foclose);
	} else {
		/* Copy it. */
		error = fd_dup(fp, 0, newp, ff->ff_exclose, ff->ff_foclose);
		if (error != 0) {
			goto out;
		}

		/* Steal away the file pointer from 'old'. */
		(void)fd_close(old);
		return 0;
	}

out:
	fd_putfile(old);
	return error;
}

/*
 * Close open files on exec.
 */
void
fd_closeexec(void)
{
	proc_t *p;
	filedesc_t *fdp;
	fdfile_t *ff;
	lwp_t *l;
	fdtab_t *dt;
	int fd;

	l = curlwp;
	p = l->l_proc;
	fdp = p->p_fd;

	if (fdp->fd_refcnt > 1) {
		/*
		 * Always unshare fd table on any exec
		 */
		fdp = fd_copy();
		fd_free();
		p->p_fd = fdp;
		l->l_fd = fdp;
	}

	/*
	 * If there are no "close-on" fd's nothing more to do
	 */
	if (!(fdp->fd_exclose || fdp->fd_foclose))
		return;

	fdp->fd_exclose = false;	/* there will be none when done */
	fdp->fd_foclose = false;

	dt = atomic_load_consume(&fdp->fd_dt);

	for (fd = 0; fd <= fdp->fd_lastfile; fd++) {
		if ((ff = dt->dt_ff[fd]) == NULL) {
			KASSERT(fd >= NDFDFILE);
			continue;
		}
		KASSERT(fd >= NDFDFILE ||
		    ff == (fdfile_t *)fdp->fd_dfdfile[fd]);
		if (ff->ff_file == NULL)
			continue;
		if (ff->ff_exclose) {
			/*
			 * We need a reference to close the file.
			 * No other threads can see the fdfile_t at
			 * this point, so don't bother locking.
			 */
			KASSERT((ff->ff_refcnt & FR_CLOSING) == 0);
			ff->ff_refcnt++;
			fd_close(fd);
		} else if (ff->ff_foclose) {
			/*
			 * https://austingroupbugs.net/view.php?id=1851
			 * (not yet approved, but probably will be: 202507)
			 * FD_CLOFORK should not be preserved across exec
			 */
			ff->ff_foclose = false;
		}
	}
}


/*
 * Sets descriptor owner. If the owner is a process, 'pgid'
 * is set to positive value, process ID. If the owner is process group,
 * 'pgid' is set to -pg_id.
 */
int
fsetown(pid_t *pgid, u_long cmd, const void *data)
{
	pid_t id = *(const pid_t *)data;
	int error;

	if (id <= INT_MIN)
		return EINVAL;

	switch (cmd) {
	case TIOCSPGRP:
		if (id < 0)
			return EINVAL;
		id = -id;
		break;
	default:
		break;
	}
	if (id > 0) {
		mutex_enter(&proc_lock);
		error = proc_find(id) ? 0 : ESRCH;
		mutex_exit(&proc_lock);
	} else if (id < 0) {
		error = pgid_in_session(curproc, -id);
	} else {
		error = 0;
	}
	if (!error) {
		*pgid = id;
	}
	return error;
}

void
fd_set_exclose(struct lwp *l, int fd, bool exclose)
{
	filedesc_t *fdp = l->l_fd;
	fdfile_t *ff = atomic_load_consume(&fdp->fd_dt)->dt_ff[fd];

	ff->ff_exclose = exclose;
	if (exclose)
		fdp->fd_exclose = true;
}

void
fd_set_foclose(struct lwp *l, int fd, bool foclose)
{
	filedesc_t *fdp = l->l_fd;
	fdfile_t *ff = atomic_load_consume(&fdp->fd_dt)->dt_ff[fd];

	ff->ff_foclose = foclose;
	if (foclose)
		fdp->fd_foclose = true;
}

/*
 * Return descriptor owner information. If the value is positive,
 * it's process ID. If it's negative, it's process group ID and
 * needs the sign removed before use.
 */
int
fgetown(pid_t pgid, u_long cmd, void *data)
{

	switch (cmd) {
	case TIOCGPGRP:
		KASSERT(pgid > INT_MIN);
		*(int *)data = -pgid;
		break;
	default:
		*(int *)data = pgid;
		break;
	}
	return 0;
}

/*
 * Send signal to descriptor owner, either process or process group.
 */
void
fownsignal(pid_t pgid, int signo, int code, int band, void *fdescdata)
{
	ksiginfo_t ksi;

	KASSERT(!cpu_intr_p());

	if (pgid == 0) {
		return;
	}

	KSI_INIT(&ksi);
	ksi.ksi_signo = signo;
	ksi.ksi_code = code;
	ksi.ksi_band = band;

	mutex_enter(&proc_lock);
	if (pgid > 0) {
		struct proc *p1;

		p1 = proc_find(pgid);
		if (p1 != NULL) {
			kpsignal(p1, &ksi, fdescdata);
		}
	} else {
		struct pgrp *pgrp;

		KASSERT(pgid < 0 && pgid > INT_MIN);
		pgrp = pgrp_find(-pgid);
		if (pgrp != NULL) {
			kpgsignal(pgrp, &ksi, fdescdata, 0);
		}
	}
	mutex_exit(&proc_lock);
}

int
fd_clone(file_t *fp, unsigned fd, int flag, const struct fileops *fops,
	 void *data)
{

	fp->f_flag = flag & FMASK;
	fd_set_exclose(curlwp, fd, (flag & O_CLOEXEC) != 0);
	fd_set_foclose(curlwp, fd, (flag & O_CLOFORK) != 0);
	fp->f_type = DTYPE_MISC;
	fp->f_ops = fops;
	fp->f_data = data;
	curlwp->l_dupfd = fd;
	fd_affix(curproc, fp, fd);

	return EMOVEFD;
}

int
fnullop_fcntl(file_t *fp, u_int cmd, void *data)
{

	if (cmd == F_SETFL)
		return 0;

	return EOPNOTSUPP;
}

int
fnullop_poll(file_t *fp, int which)
{

	return 0;
}

int
fnullop_kqfilter(file_t *fp, struct knote *kn)
{

	return EOPNOTSUPP;
}

void
fnullop_restart(file_t *fp)
{

}

int
fbadop_read(file_t *fp, off_t *offset, struct uio *uio,
	    kauth_cred_t cred, int flags)
{

	return EOPNOTSUPP;
}

int
fbadop_write(file_t *fp, off_t *offset, struct uio *uio,
	     kauth_cred_t cred, int flags)
{

	return EOPNOTSUPP;
}

int
fbadop_ioctl(file_t *fp, u_long com, void *data)
{

	return EOPNOTSUPP;
}

int
fbadop_stat(file_t *fp, struct stat *sb)
{

	return EOPNOTSUPP;
}

int
fbadop_close(file_t *fp)
{

	return EOPNOTSUPP;
}

/*
 * sysctl routines pertaining to file descriptors
 */

/* Initialized in sysctl_init() for now... */
extern kmutex_t sysctl_file_marker_lock;
static u_int sysctl_file_marker = 1;

/*
 * Expects to be called with proc_lock and sysctl_file_marker_lock locked.
 */
static void
sysctl_file_marker_reset(void)
{
	struct proc *p;

	PROCLIST_FOREACH(p, &allproc) {
		struct filedesc *fd = p->p_fd;
		fdtab_t *dt;
		u_int i;

		mutex_enter(&fd->fd_lock);
		dt = fd->fd_dt;
		for (i = 0; i < dt->dt_nfiles; i++) {
			struct file *fp;
			fdfile_t *ff;

			if ((ff = dt->dt_ff[i]) == NULL) {
				continue;
			}
			if ((fp = atomic_load_consume(&ff->ff_file)) == NULL) {
				continue;
			}
			fp->f_marker = 0;
		}
		mutex_exit(&fd->fd_lock);
	}
}

/*
 * sysctl helper routine for kern.file pseudo-subtree.
 */
static int
sysctl_kern_file(SYSCTLFN_ARGS)
{
	const bool allowaddr = get_expose_address(curproc);
	struct filelist flist;
	int error;
	size_t buflen;
	struct file *fp, fbuf;
	char *start, *where;
	struct proc *p;

	start = where = oldp;
	buflen = *oldlenp;
	
	if (where == NULL) {
		/*
		 * overestimate by 10 files
		 */
		*oldlenp = sizeof(filehead) + (nfiles + 10) *
		    sizeof(struct file);
		return 0;
	}

	/*
	 * first sysctl_copyout filehead
	 */
	if (buflen < sizeof(filehead)) {
		*oldlenp = 0;
		return 0;
	}
	sysctl_unlock();
	if (allowaddr) {
		memcpy(&flist, &filehead, sizeof(flist));
	} else {
		memset(&flist, 0, sizeof(flist));
	}
	error = sysctl_copyout(l, &flist, where, sizeof(flist));
	if (error) {
		sysctl_relock();
		return error;
	}
	buflen -= sizeof(flist);
	where += sizeof(flist);

	/*
	 * followed by an array of file structures
	 */
	mutex_enter(&sysctl_file_marker_lock);
	mutex_enter(&proc_lock);
	PROCLIST_FOREACH(p, &allproc) {
		struct filedesc *fd;
		fdtab_t *dt;
		u_int i;

		if (p->p_stat == SIDL) {
			/* skip embryonic processes */
			continue;
		}
		mutex_enter(p->p_lock);
		error = kauth_authorize_process(l->l_cred,
		    KAUTH_PROCESS_CANSEE, p,
		    KAUTH_ARG(KAUTH_REQ_PROCESS_CANSEE_OPENFILES),
		    NULL, NULL);
		mutex_exit(p->p_lock);
		if (error != 0) {
			/*
			 * Don't leak kauth retval if we're silently
			 * skipping this entry.
			 */
			error = 0;
			continue;
		}

		/*
		 * Grab a hold on the process.
		 */
		if (!rw_tryenter(&p->p_reflock, RW_READER)) {
			continue;
		}
		mutex_exit(&proc_lock);

		fd = p->p_fd;
		mutex_enter(&fd->fd_lock);
		dt = fd->fd_dt;
		for (i = 0; i < dt->dt_nfiles; i++) {
			fdfile_t *ff;

			if ((ff = dt->dt_ff[i]) == NULL) {
				continue;
			}
			if ((fp = atomic_load_consume(&ff->ff_file)) == NULL) {
				continue;
			}

			mutex_enter(&fp->f_lock);

			if ((fp->f_count == 0) ||
			    (fp->f_marker == sysctl_file_marker)) {
				mutex_exit(&fp->f_lock);
				continue;
			}

			/* Check that we have enough space. */
			if (buflen < sizeof(struct file)) {
				*oldlenp = where - start;
				mutex_exit(&fp->f_lock);
				error = ENOMEM;
				break;
			}

			fill_file(&fbuf, fp);
			mutex_exit(&fp->f_lock);
			error = sysctl_copyout(l, &fbuf, where, sizeof(fbuf));
			if (error) {
				break;
			}
			buflen -= sizeof(struct file);
			where += sizeof(struct file);

			fp->f_marker = sysctl_file_marker;
		}
		mutex_exit(&fd->fd_lock);

		/*
		 * Release reference to process.
		 */
		mutex_enter(&proc_lock);
		rw_exit(&p->p_reflock);

		if (error)
			break;
	}

	sysctl_file_marker++;
	/* Reset all markers if wrapped. */
	if (sysctl_file_marker == 0) {
		sysctl_file_marker_reset();
		sysctl_file_marker++;
	}

	mutex_exit(&proc_lock);
	mutex_exit(&sysctl_file_marker_lock);

	*oldlenp = where - start;
	sysctl_relock();
	return error;
}

/*
 * sysctl helper function for kern.file2
 */
static int
sysctl_kern_file2(SYSCTLFN_ARGS)
{
	struct proc *p;
	struct file *fp;
	struct filedesc *fd;
	struct kinfo_file kf;
	char *dp;
	u_int i, op;
	size_t len, needed, elem_size, out_size;
	int error, arg, elem_count;
	fdfile_t *ff;
	fdtab_t *dt;

	if (namelen == 1 && name[0] == CTL_QUERY)
		return sysctl_query(SYSCTLFN_CALL(rnode));

	if (namelen != 4)
		return EINVAL;

	error = 0;
	dp = oldp;
	len = (oldp != NULL) ? *oldlenp : 0;
	op = name[0];
	arg = name[1];
	elem_size = name[2];
	elem_count = name[3];
	out_size = MIN(sizeof(kf), elem_size);
	needed = 0;

	if (elem_size < 1 || elem_count < 0)
		return EINVAL;

	switch (op) {
	case KERN_FILE_BYFILE:
	case KERN_FILE_BYPID:
		/*
		 * We're traversing the process list in both cases; the BYFILE
		 * case does additional work of keeping track of files already
		 * looked at.
		 */

		/* doesn't use arg so it must be zero */
		if ((op == KERN_FILE_BYFILE) && (arg != 0))
			return EINVAL;

		if ((op == KERN_FILE_BYPID) && (arg < -1))
			/* -1 means all processes */
			return EINVAL;

		sysctl_unlock();
		if (op == KERN_FILE_BYFILE)
			mutex_enter(&sysctl_file_marker_lock);
		mutex_enter(&proc_lock);
		PROCLIST_FOREACH(p, &allproc) {
			if (p->p_stat == SIDL) {
				/* skip embryonic processes */
				continue;
			}
			if (arg > 0 && p->p_pid != arg) {
				/* pick only the one we want */
				/* XXX want 0 to mean "kernel files" */
				continue;
			}
			mutex_enter(p->p_lock);
			error = kauth_authorize_process(l->l_cred,
			    KAUTH_PROCESS_CANSEE, p,
			    KAUTH_ARG(KAUTH_REQ_PROCESS_CANSEE_OPENFILES),
			    NULL, NULL);
			mutex_exit(p->p_lock);
			if (error != 0) {
				/*
				 * Don't leak kauth retval if we're silently
				 * skipping this entry.
				 */
				error = 0;
				continue;
			}

			/*
			 * Grab a hold on the process.
			 */
			if (!rw_tryenter(&p->p_reflock, RW_READER)) {
				continue;
			}
			mutex_exit(&proc_lock);

			fd = p->p_fd;
			mutex_enter(&fd->fd_lock);
			dt = fd->fd_dt;
			for (i = 0; i < dt->dt_nfiles; i++) {
				if ((ff = dt->dt_ff[i]) == NULL) {
					continue;
				}
				if ((fp = atomic_load_consume(&ff->ff_file)) ==
				    NULL) {
					continue;
				}

				if ((op == KERN_FILE_BYFILE) &&
				    (fp->f_marker == sysctl_file_marker)) {
					continue;
				}
				if (len >= elem_size && elem_count > 0) {
					mutex_enter(&fp->f_lock);
					fill_file2(&kf, fp, ff, i, p->p_pid);
					mutex_exit(&fp->f_lock);
					mutex_exit(&fd->fd_lock);
					error = sysctl_copyout(l,
					    &kf, dp, out_size);
					mutex_enter(&fd->fd_lock);
					if (error)
						break;
					dp += elem_size;
					len -= elem_size;
				}
				if (op == KERN_FILE_BYFILE)
					fp->f_marker = sysctl_file_marker;
				needed += elem_size;
				if (elem_count > 0 && elem_count != INT_MAX)
					elem_count--;
			}
			mutex_exit(&fd->fd_lock);

			/*
			 * Release reference to process.
			 */
			mutex_enter(&proc_lock);
			rw_exit(&p->p_reflock);
		}
		if (op == KERN_FILE_BYFILE) {
			sysctl_file_marker++;

			/* Reset all markers if wrapped. */
			if (sysctl_file_marker == 0) {
				sysctl_file_marker_reset();
				sysctl_file_marker++;
			}
		}
		mutex_exit(&proc_lock);
		if (op == KERN_FILE_BYFILE)
			mutex_exit(&sysctl_file_marker_lock);
		sysctl_relock();
		break;
	default:
		return EINVAL;
	}

	if (oldp == NULL)
		needed += KERN_FILESLOP * elem_size;
	*oldlenp = needed;

	return error;
}

static void
fill_file(struct file *fp, const struct file *fpsrc)
{
	const bool allowaddr = get_expose_address(curproc);

	memset(fp, 0, sizeof(*fp));

	fp->f_offset = fpsrc->f_offset;
	COND_SET_PTR(fp->f_cred, fpsrc->f_cred, allowaddr);
	COND_SET_CPTR(fp->f_ops, fpsrc->f_ops, allowaddr);
	COND_SET_STRUCT(fp->f_undata, fpsrc->f_undata, allowaddr);
	COND_SET_STRUCT(fp->f_list, fpsrc->f_list, allowaddr);
	fp->f_flag = fpsrc->f_flag;
	fp->f_marker = fpsrc->f_marker;
	fp->f_type = fpsrc->f_type;
	fp->f_advice = fpsrc->f_advice;
	fp->f_count = fpsrc->f_count;
	fp->f_msgcount = fpsrc->f_msgcount;
	fp->f_unpcount = fpsrc->f_unpcount;
	COND_SET_STRUCT(fp->f_unplist, fpsrc->f_unplist, allowaddr);
}

static void
fill_file2(struct kinfo_file *kp, const file_t *fp, const fdfile_t *ff,
	  int i, pid_t pid)
{
	const bool allowaddr = get_expose_address(curproc);

	memset(kp, 0, sizeof(*kp));

	COND_SET_VALUE(kp->ki_fileaddr, PTRTOUINT64(fp), allowaddr);
	kp->ki_flag =		fp->f_flag;
	kp->ki_iflags =		0;
	kp->ki_ftype =		fp->f_type;
	kp->ki_count =		fp->f_count;
	kp->ki_msgcount =	fp->f_msgcount;
	COND_SET_VALUE(kp->ki_fucred, PTRTOUINT64(fp->f_cred), allowaddr);
	kp->ki_fuid =		kauth_cred_geteuid(fp->f_cred);
	kp->ki_fgid =		kauth_cred_getegid(fp->f_cred);
	COND_SET_VALUE(kp->ki_fops, PTRTOUINT64(fp->f_ops), allowaddr);
	kp->ki_foffset =	fp->f_offset;
	COND_SET_VALUE(kp->ki_fdata, PTRTOUINT64(fp->f_data), allowaddr);

	/* vnode information to glue this file to something */
	if (fp->f_type == DTYPE_VNODE) {
		struct vnode *vp = fp->f_vnode;

		COND_SET_VALUE(kp->ki_vun, PTRTOUINT64(vp->v_un.vu_socket),
		    allowaddr);
		kp->ki_vsize =	vp->v_size;
		kp->ki_vtype =	vp->v_type;
		kp->ki_vtag =	vp->v_tag;
		COND_SET_VALUE(kp->ki_vdata, PTRTOUINT64(vp->v_data),
		    allowaddr);
	}

	/* process information when retrieved via KERN_FILE_BYPID */
	if (ff != NULL) {
		kp->ki_pid =		pid;
		kp->ki_fd =		i;
		kp->ki_ofileflags =	(ff->ff_exclose ? FD_CLOEXEC : 0) |
					(ff->ff_foclose ? FD_CLOFORK : 0);
		kp->ki_usecount =	ff->ff_refcnt;
	}
}
