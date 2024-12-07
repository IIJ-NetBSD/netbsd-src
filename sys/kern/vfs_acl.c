/*	$NetBSD: vfs_acl.c,v 1.2 2024/12/07 02:11:42 riastradh Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2006, 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Developed by the TrustedBSD Project.
 *
 * ACL system calls and other functions common across different ACL types.
 * Type-specific routines go into subr_acl_<type>.c.
 */

#include <sys/cdefs.h>
#if 0
__FBSDID("$FreeBSD: head/sys/kern/vfs_acl.c 356337 2020-01-03 22:29:58Z mjg $");
#endif
__KERNEL_RCSID(0, "$NetBSD: vfs_acl.c,v 1.2 2024/12/07 02:11:42 riastradh Exp $");

#include <sys/param.h>
#include <sys/types.h>

#include <sys/acl.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/syscallargs.h>
#include <sys/systm.h>
#include <sys/vnode.h>

__CTASSERT(ACL_MAX_ENTRIES >= OLDACL_MAX_ENTRIES);

int
acl_copy_oldacl_into_acl(const struct oldacl *source, struct acl *dest)
{
	int i;

	if (source->acl_cnt < 0 || source->acl_cnt > OLDACL_MAX_ENTRIES)
		return EINVAL;

	memset(dest, 0, sizeof(*dest));

	dest->acl_cnt = source->acl_cnt;
	dest->acl_maxcnt = ACL_MAX_ENTRIES;

	for (i = 0; i < dest->acl_cnt; i++) {
		dest->acl_entry[i].ae_tag = source->acl_entry[i].ae_tag;
		dest->acl_entry[i].ae_id = source->acl_entry[i].ae_id;
		dest->acl_entry[i].ae_perm = source->acl_entry[i].ae_perm;
	}

	return 0;
}

int
acl_copy_acl_into_oldacl(const struct acl *source, struct oldacl *dest)
{
	int i;

	if (source->acl_cnt > OLDACL_MAX_ENTRIES)
		return EINVAL;

	memset(dest, 0, sizeof(*dest));

	dest->acl_cnt = source->acl_cnt;

	for (i = 0; i < dest->acl_cnt; i++) {
		dest->acl_entry[i].ae_tag = source->acl_entry[i].ae_tag;
		dest->acl_entry[i].ae_id = source->acl_entry[i].ae_id;
		dest->acl_entry[i].ae_perm = source->acl_entry[i].ae_perm;
	}

	return 0;
}

/*
 * At one time, "struct ACL" was extended in order to add support for NFSv4
 * ACLs.  Instead of creating compatibility versions of all the ACL-related
 * syscalls, they were left intact.  It's possible to find out what the code
 * calling these syscalls (libc) expects basing on "type" argument - if it's
 * either ACL_TYPE_ACCESS_OLD or ACL_TYPE_DEFAULT_OLD (which previously were
 * known as ACL_TYPE_ACCESS and ACL_TYPE_DEFAULT), then it's the "struct
 * oldacl".  If it's something else, then it's the new "struct acl".  In the
 * latter case, the routines below just copyin/copyout the contents.  In the
 * former case, they copyin the "struct oldacl" and convert it to the new
 * format.
 */
static int
acl_copyin(const void *user_acl, struct acl *kernel_acl, acl_type_t type)
{
	int error;
	struct oldacl old;

	switch (type) {
	case ACL_TYPE_ACCESS_OLD:
	case ACL_TYPE_DEFAULT_OLD:
		error = copyin(user_acl, &old, sizeof(old));
		if (error != 0)
			break;
		acl_copy_oldacl_into_acl(&old, kernel_acl);
		break;

	default:
		error = copyin(user_acl, kernel_acl, sizeof(*kernel_acl));
		if (kernel_acl->acl_maxcnt != ACL_MAX_ENTRIES)
			return EINVAL;
	}

	return error;
}

static int
acl_copyout(const struct acl *kernel_acl, void *user_acl, acl_type_t type)
{
	uint32_t am;
	int error;
	struct oldacl old;

	switch (type) {
	case ACL_TYPE_ACCESS_OLD:
	case ACL_TYPE_DEFAULT_OLD:
		error = acl_copy_acl_into_oldacl(kernel_acl, &old);
		if (error != 0)
			break;

		error = copyout(&old, user_acl, sizeof(old));
		break;

	default:
		error = ufetch_32((const uint32_t *)
		    (const void *)((const char *)user_acl +
		    offsetof(struct acl, acl_maxcnt)), &am);
		if (error)
			return error;
		if (am != ACL_MAX_ENTRIES)
			return EINVAL;

		error = copyout(kernel_acl, user_acl, sizeof(*kernel_acl));
	}

	return error;
}

/*
 * Convert "old" type - ACL_TYPE_{ACCESS,DEFAULT}_OLD - into its "new"
 * counterpart.  It's required for old (pre-NFSv4 ACLs) libc to work
 * with new kernel.  Fixing 'type' for old binaries with new libc
 * is being done in lib/libc/posix1e/acl_support.c:_acl_type_unold().
 */
static int
acl_type_unold(int type)
{
	switch (type) {
	case ACL_TYPE_ACCESS_OLD:
		return ACL_TYPE_ACCESS;

	case ACL_TYPE_DEFAULT_OLD:
		return ACL_TYPE_DEFAULT;

	default:
		return type;
	}
}

/*
 * These calls wrap the real vnode operations, and are called by the syscall
 * code once the syscall has converted the path or file descriptor to a vnode
 * (unlocked).  The aclp pointer is assumed still to point to userland, so
 * this should not be consumed within the kernel except by syscall code.
 * Other code should directly invoke VOP_{SET,GET}ACL.
 */

/*
 * Given a vnode, set its ACL.
 */
int
vacl_set_acl(struct lwp *l, struct vnode *vp, acl_type_t type,
    const struct acl *aclp)
{
	struct acl *inkernelacl;
	int error;

	inkernelacl = acl_alloc(KM_SLEEP);
	error = acl_copyin(aclp, inkernelacl, type);
	if (error != 0)
		goto out;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_SETACL(vp, acl_type_unold(type), inkernelacl, l->l_cred);
	VOP_UNLOCK(vp);
out:
	acl_free(inkernelacl);
	return error;
}

/*
 * Given a vnode, get its ACL.
 */
int
vacl_get_acl(struct lwp *l, struct vnode *vp, acl_type_t type,
    struct acl *aclp)
{
	struct acl *inkernelacl;
	int error;

	inkernelacl = acl_alloc(KM_SLEEP);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_GETACL(vp, acl_type_unold(type), inkernelacl, l->l_cred);

	VOP_UNLOCK(vp);
	if (error == 0)
		error = acl_copyout(inkernelacl, aclp, type);
	acl_free(inkernelacl);
	return error;
}

/*
 * Given a vnode, delete its ACL.
 */
int
vacl_delete(struct lwp *l, struct vnode *vp, acl_type_t type)
{
	int error;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_SETACL(vp, acl_type_unold(type), 0, l->l_cred);
	VOP_UNLOCK(vp);
	return error;
}

/*
 * Given a vnode, check whether an ACL is appropriate for it
 *
 * XXXRW: No vnode lock held so can't audit vnode state...?
 */
int
vacl_aclcheck(struct lwp *l, struct vnode *vp, acl_type_t type,
    const struct acl *aclp)
{
	struct acl *inkernelacl;
	int error;

	inkernelacl = acl_alloc(KM_SLEEP);
	error = acl_copyin(aclp, inkernelacl, type);
	if (error != 0)
		goto out;
	error = VOP_ACLCHECK(vp, acl_type_unold(type), inkernelacl,
	    l->l_cred);
out:
	acl_free(inkernelacl);
	return error;
}

/*
 * syscalls -- convert the path/fd to a vnode, and call vacl_whatever.  Don't
 * need to lock, as the vacl_ code will get/release any locks required.
 */

/*
 * Given a file path, get an ACL for it
 */
int
sys___acl_get_file(struct lwp *l,
     const struct sys___acl_get_file_args *uap, register_t *retval)
{

	return kern___acl_get_path(l, SCARG(uap, path), SCARG(uap, type),
	    SCARG(uap, aclp), NSM_FOLLOW_NOEMULROOT);
}

/*
 * Given a file path, get an ACL for it; don't follow links.
 */
int
sys___acl_get_link(struct lwp *l,
    const struct sys___acl_get_link_args *uap, register_t *retval)
{

	return kern___acl_get_path(l, SCARG(uap, path), SCARG(uap, type),
	    SCARG(uap, aclp), NSM_NOFOLLOW_NOEMULROOT);
}

int
kern___acl_get_path(struct lwp *l, const char *path, acl_type_t type,
    struct acl *aclp, namei_simple_flags_t flags)
{
	struct vnode *path_vp;
	int error;

	error = namei_simple_user(path, flags, &path_vp);
	if (error == 0) {
		error = vacl_get_acl(l, path_vp, type, aclp);
		vrele(path_vp);
	}
	return error;
}

/*
 * Given a file path, set an ACL for it.
 */
int
sys___acl_set_file(struct lwp *l,
    const struct sys___acl_set_file_args *uap, register_t *retval)
{

	return kern___acl_set_path(l, SCARG(uap, path), SCARG(uap, type),
	    SCARG(uap, aclp), NSM_FOLLOW_NOEMULROOT);
}

/*
 * Given a file path, set an ACL for it; don't follow links.
 */
int
sys___acl_set_link(struct lwp *l,
    const struct sys___acl_set_link_args *uap, register_t *retval)
{

	return kern___acl_set_path(l, SCARG(uap, path), SCARG(uap, type),
	    SCARG(uap, aclp), NSM_NOFOLLOW_NOEMULROOT);
}

int
kern___acl_set_path(struct lwp *l, const char *path,
    acl_type_t type, const struct acl *aclp, namei_simple_flags_t flags)
{
	struct vnode *path_vp;
	int error;

	error = namei_simple_user(path, flags, &path_vp);
	if (error == 0) {
		error = vacl_set_acl(l, path_vp, type, aclp);
		vrele(path_vp);
	}
	return error;
}

/*
 * Given a file descriptor, get an ACL for it.
 */
int
sys___acl_get_fd(struct lwp *l, const struct sys___acl_get_fd_args *uap,
    register_t *retval)
{
	struct file *fp;
	int error;
	error = fd_getvnode(SCARG(uap, filedes), &fp);
	if (error == 0) {
		error = vacl_get_acl(l, fp->f_vnode, SCARG(uap, type),
		    SCARG(uap, aclp));
		fd_putfile(SCARG(uap, filedes));
	}
	return error;
}

/*
 * Given a file descriptor, set an ACL for it.
 */
int
sys___acl_set_fd(struct lwp *l, const struct sys___acl_set_fd_args *uap,
    register_t *retval)
{
	struct file *fp;
	int error;

	error = fd_getvnode(SCARG(uap, filedes), &fp);
	if (error == 0) {
		error = vacl_set_acl(l, fp->f_vnode, SCARG(uap, type),
		    SCARG(uap, aclp));
		fd_putfile(SCARG(uap, filedes));
	}
	return error;
}

/*
 * Given a file path, delete an ACL from it.
 */
int
sys___acl_delete_file(struct lwp *l,
    const struct sys___acl_delete_file_args *uap, register_t *retval)
{

	return kern___acl_delete_path(l, SCARG(uap, path), SCARG(uap, type),
	    NSM_FOLLOW_NOEMULROOT);
}

/*
 * Given a file path, delete an ACL from it; don't follow links.
 */
int
sys___acl_delete_link(struct lwp *l,
    const struct sys___acl_delete_link_args *uap, register_t *retval)
{

	return kern___acl_delete_path(l, SCARG(uap, path), SCARG(uap, type),
	    NSM_NOFOLLOW_NOEMULROOT);
}

int
kern___acl_delete_path(struct lwp *l, const char *path,
    acl_type_t type, namei_simple_flags_t flags)
{
	struct vnode *path_vp;
	int error;

	error = namei_simple_user(path, flags, &path_vp);
	if (error == 0) {
		error = vacl_delete(l, path_vp, type);
		vrele(path_vp);
	}
	return error;
}

/*
 * Given a file path, delete an ACL from it.
 */
int
sys___acl_delete_fd(struct lwp *l,
    const struct sys___acl_delete_fd_args *uap, register_t *retval)
{
	struct file *fp;
	int error;

	error = fd_getvnode(SCARG(uap, filedes), &fp);
	if (error == 0) {
		error = vacl_delete(l, fp->f_vnode, SCARG(uap, type));
		fd_putfile(SCARG(uap, filedes));
	}
	return error;
}

/*
 * Given a file path, check an ACL for it.
 */
int
sys___acl_aclcheck_file(struct lwp *l,
    const struct sys___acl_aclcheck_file_args *uap, register_t *retval)
{

	return kern___acl_aclcheck_path(l, SCARG(uap, path), SCARG(uap, type),
	    SCARG(uap, aclp), NSM_FOLLOW_NOEMULROOT);
}

/*
 * Given a file path, check an ACL for it; don't follow links.
 */
int
sys___acl_aclcheck_link(struct lwp *l,
    const struct sys___acl_aclcheck_link_args *uap, register_t *retval)
{
	return kern___acl_aclcheck_path(l, SCARG(uap, path),
	    SCARG(uap, type), SCARG(uap, aclp), NSM_NOFOLLOW_NOEMULROOT);
}

int
kern___acl_aclcheck_path(struct lwp *l, const char *path, acl_type_t type,
    struct acl *aclp, namei_simple_flags_t flags)
{
	struct vnode *path_vp;
	int error;

	error = namei_simple_user(path, flags, &path_vp);
	if (error == 0) {
		error = vacl_aclcheck(l, path_vp, type, aclp);
		vrele(path_vp);

	}
	return error;
}

/*
 * Given a file descriptor, check an ACL for it.
 */
int
sys___acl_aclcheck_fd(struct lwp *l,
    const struct sys___acl_aclcheck_fd_args *uap, register_t *retval)
{
	struct file *fp;
	int error;

	error = fd_getvnode(SCARG(uap, filedes), &fp);
	if (error == 0) {
		error = vacl_aclcheck(l, fp->f_vnode, SCARG(uap, type),
		    SCARG(uap, aclp));
		fd_putfile(SCARG(uap, filedes));
	}
	return error;
}

struct acl *
acl_alloc(int flags)
{
	struct acl *aclp;

	aclp = kmem_zalloc(sizeof(*aclp), flags);
	if (aclp == NULL)
		return NULL;

	aclp->acl_maxcnt = ACL_MAX_ENTRIES;

	return aclp;
}

void
acl_free(struct acl *aclp)
{

	kmem_free(aclp, sizeof(*aclp));
}
