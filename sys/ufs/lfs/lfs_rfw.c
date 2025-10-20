/*	$NetBSD: lfs_rfw.c,v 1.40 2025/10/20 04:20:37 perseant Exp $	*/

/*-
 * Copyright (c) 1999, 2000, 2001, 2002, 2003, 2025 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Konrad E. Schroder <perseant@hhhh.org>.
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: lfs_rfw.c,v 1.40 2025/10/20 04:20:37 perseant Exp $");

#if defined(_KERNEL_OPT)
#include "opt_quota.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kthread.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/file.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/conf.h>
#include <sys/kauth.h>

#include <miscfs/specfs/specdev.h>

#include <ufs/lfs/ulfs_quotacommon.h>
#include <ufs/lfs/ulfs_inode.h>
#include <ufs/lfs/ulfsmount.h>
#include <ufs/lfs/ulfs_extern.h>

#include <uvm/uvm_extern.h>

#include <ufs/lfs/lfs.h>
#include <ufs/lfs/lfs_accessors.h>
#include <ufs/lfs/lfs_kernel.h>
#include <ufs/lfs/lfs_extern.h>

#include <miscfs/genfs/genfs.h>
#include <miscfs/genfs/genfs_node.h>

/*
 * Roll-forward code.
 */
static bool all_selector(void *, struct vnode *);
static void drop_vnode_pages(struct mount *, struct lwp *);
static void update_inoblk_copy_dinode(struct lfs *, union lfs_dinode *,
				      const union lfs_dinode *);
static int update_inogen(struct lfs_inofuncarg *);
static int update_inoblk(struct lfs_inofuncarg *);
static int ino_func_setclean(struct lfs_inofuncarg *);
static int finfo_func_rfw(struct lfs_finfofuncarg *);
static int finfo_func_rewrite(struct lfs_finfofuncarg *);
static int finfo_func_setclean(struct lfs_finfofuncarg *);

static int update_meta(struct lfs *, ino_t, int, daddr_t, daddr_t, size_t,
		       struct lwp *l);
static int skip_superblock(struct lfs *, daddr_t *);
static int rewrite_block(struct lfs *, struct vnode *, daddr_t, daddr_t, size_t, int *);
#if 0
static bool lfs_isseq(const struct lfs *fs, long int lbn1, long int lbn2);
#endif

extern int lfs_do_rfw;
int rblkcnt;
int lfs_rfw_max_psegs = 0;

/*
 * Allocate a particular inode with a particular version number, freeing
 * any previous versions of this inode that may have gone before.
 * Used by the roll-forward code.
 *
 * XXX this function does not have appropriate locking to be used on a live fs;
 * XXX but something similar could probably be used for an "undelete" call.
 *
 * Called with the Ifile inode locked.
 */
int
lfs_rf_valloc(struct lfs *fs, ino_t ino, int vers, struct lwp *l,
	      struct vnode **vpp, union lfs_dinode *dip)
{
	struct vattr va;
	struct vnode *vp;
	struct inode *ip;
	int error;

	KASSERT(ino > LFS_IFILE_INUM);
	ASSERT_SEGLOCK(fs); /* XXX it doesn't, really */

	/*
	 * First, just try a vget. If the version number is the one we want,
	 * we don't have to do anything else.  If the version number is wrong,
	 * take appropriate action.
	 */
	error = VFS_VGET(fs->lfs_ivnode->v_mount, ino, LK_EXCLUSIVE, &vp);
	if (error == 0) {
		DLOG((DLOG_RF, "lfs_rf_valloc[1]: ino %d vp %p\n",
			(int)ino, vp));

		*vpp = vp;
		ip = VTOI(vp);
		DLOG((DLOG_RF, "  ip->i_gen=%jd dip nlink %jd seeking"
			" version %jd\n", (intmax_t)ip->i_gen,
			(intmax_t)(dip == NULL ? -1
				: lfs_dino_getnlink(fs, dip)), (intmax_t)vers));
		if (ip->i_gen == vers) {
			/*
			 * We have what we wanted already.
			 */
			DLOG((DLOG_RF, "  pre-existing\n"));
			return 0;
		} else if (ip->i_gen < vers && dip != NULL
			&& lfs_dino_getnlink(fs, dip) > 0) {
			/*
			 * We have found a newer version.  Truncate
			 * the old vnode to zero and re-initialize
			 * from the given dinode.
			 */
			DLOG((DLOG_RF, "  replace old version %jd\n",
				(intmax_t)ip->i_gen));
			lfs_truncate(vp, (off_t)0, 0, NOCRED);
			ip->i_gen = vers;
			vp->v_type = IFTOVT(lfs_dino_getmode(fs, dip));
			update_inoblk_copy_dinode(fs, ip->i_din, dip);
			LFS_SET_UINO(ip, IN_CHANGE | IN_UPDATE);
			return 0;
		} else {
			/*
			 * Not the right version and nothing to
			 * initialize from.  Don't recover this data.
			 */
			DLOG((DLOG_RF, "ino %d: sought version %d, got %d\n",
				(int)ino, (int)vers,
				(int)lfs_dino_getgen(fs, ip->i_din)));
			vput(vp);
			*vpp = NULLVP;
			return EEXIST;
		}
	}

	/*
	 * No version of this inode was found in the cache.
	 * Make a new one from the dinode.  We will add data blocks
	 * as they come in, so scrub any block addresses off of the
	 * inode and reset block counts to zero.
	 */
	if (dip == NULL)
		return ENOENT;

	vattr_null(&va);
	va.va_type = IFTOVT(lfs_dino_getmode(fs, dip));
	va.va_mode = lfs_dino_getmode(fs, dip) & ALLPERMS;
	va.va_fileid = ino;
	va.va_gen = vers;
	error = vcache_new(fs->lfs_ivnode->v_mount, NULL, &va, NOCRED, NULL,
	    &vp);
	if (error)
		return error;
	error = vn_lock(vp, LK_EXCLUSIVE);
	if (error)
		goto err;

	ip = VTOI(vp);
	update_inoblk_copy_dinode(fs, ip->i_din, dip);

	DLOG((DLOG_RF, "lfs_valloc[2] ino %d vp %p size=%lld effnblks=%d,"
		" blocks=%d\n", (int)ino, vp, (long long)ip->i_size,
		(int)ip->i_lfs_effnblks,
		(int)lfs_dino_getblocks(fs, ip->i_din)));
	*vpp = vp;
	return 0;

err:
	vrele(vp);
	*vpp = NULLVP;
	return error;
}

/*
 * Load the appropriate indirect block, and change the appropriate pointer.
 * Mark the block dirty.  Do segment and avail accounting.
 */
static int
update_meta(struct lfs *fs, ino_t ino, int vers, daddr_t lbn,
	    daddr_t ndaddr, size_t size, struct lwp *l)
{
	int error;
	struct vnode *vp;
	struct inode *ip;
	daddr_t odaddr;
	struct indir a[ULFS_NIADDR];
	int num;
	struct buf *bp;
	SEGUSE *sup;
	u_int64_t newsize, loff;

	KASSERT(lbn >= 0);	/* no indirect blocks */
	KASSERT(ino > LFS_IFILE_INUM);
	
	DLOG((DLOG_RF, "update_meta: ino %d lbn %d size %d at 0x%jx\n",
	      (int)ino, (int)lbn, (int)size, (uintmax_t)ndaddr));

	if ((error = lfs_rf_valloc(fs, ino, vers, l, &vp, NULL)) != 0)
		return error;
	ip = VTOI(vp);

	/*
	 * If block already exists, note its new location
	 * but do not account it as new.
	 */
	ulfs_bmaparray(vp, lbn, &odaddr, &a[0], &num, NULL, NULL);
	if (odaddr == UNASSIGNED) {
		if ((error = lfs_balloc(vp, (lbn << lfs_sb_getbshift(fs)),
					size, NOCRED, 0, &bp)) != 0) {
			vput(vp);
			return (error);
		}
		/* No need to write, the block is already on disk */
		if (bp->b_oflags & BO_DELWRI) {
			LFS_UNLOCK_BUF(bp);
			/* Account recovery of the previous version */
			lfs_sb_addavail(fs, lfs_btofsb(fs, bp->b_bcount));
		}
		brelse(bp, BC_INVAL);
		DLOG((DLOG_RF, "balloc ip->i_lfs_effnblks = %d,"
			" lfs_dino_getblocks(fs, ip->i_din) = %d\n",
			(int)ip->i_lfs_effnblks,
			(int)lfs_dino_getblocks(fs, ip->i_din)));
	} else {
		/* XXX fragextend? */
		DLOG((DLOG_RF, "block exists, no balloc\n"));
	}

	/*
	 * Extend the file, if it is not large enough already.
	 * XXX This is not exactly right, we don't know how much of the
	 * XXX last block is actually used.
	 *
	 * XXX We should be able to encode the actual data length of the
	 * XXX last block in fi_lastlength, since we can infer the
	 * XXX necessary block length from that using a variant of
	 * XXX lfs_blksize().
	 */
	loff = lfs_lblktosize(fs, lbn);
	if (loff >= (ULFS_NDADDR << lfs_sb_getbshift(fs))) {
		/* No fragments */
		newsize = loff + 1;
	} else {
		/* Subtract only a fragment to account for block size */
		newsize = loff + size - lfs_fsbtob(fs, 1) + 1;
	}
	
	if (ip->i_size < newsize) {
		DLOG((DLOG_RF, "ino %d size %d -> %d\n",
		      (int)ino, (int)ip->i_size, (int)newsize));
		lfs_dino_setsize(fs, ip->i_din, newsize);
		ip->i_size = newsize;
		/*
		 * tell vm our new size for the case the inode won't
		 * appear later.
		 */
		uvm_vnp_setsize(vp, newsize);
	}

	lfs_update_single(fs, NULL, vp, lbn, ndaddr, size);

	LFS_SEGENTRY(sup, fs, lfs_dtosn(fs, ndaddr), bp);
	sup->su_nbytes += size;
	LFS_WRITESEGENTRY(sup, fs, lfs_dtosn(fs, ndaddr), bp);

	/* differences here should be due to UNWRITTEN indirect blocks. */
	if (vp->v_type != VLNK) {
		if (!(ip->i_lfs_effnblks >= lfs_dino_getblocks(fs, ip->i_din))
#if 0
		    || !(lfs_lblkno(fs, ip->i_size) > ULFS_NDADDR ||
			 ip->i_lfs_effnblks == lfs_dino_getblocks(fs, ip->i_din))
#endif /* 0 */
			) {
			vprint("vnode", vp);
			printf("effnblks=%jd dino_getblocks=%jd\n",
			       (intmax_t)ip->i_lfs_effnblks,
			       (intmax_t)lfs_dino_getblocks(fs, ip->i_din));
		}
		KASSERT(ip->i_lfs_effnblks >= lfs_dino_getblocks(fs, ip->i_din));
#if 0
		KASSERT(lfs_lblkno(fs, ip->i_size) > ULFS_NDADDR ||
			ip->i_lfs_effnblks == lfs_dino_getblocks(fs, ip->i_din));
#endif /* 0 */
	}

#ifdef DEBUG
	/* Now look again to make sure it worked */
	ulfs_bmaparray(vp, lbn, &odaddr, &a[0], &num, NULL, NULL);
	if (LFS_DBTOFSB(fs, odaddr) != ndaddr)
		DLOG((DLOG_RF, "update_meta: failed setting ino %jd lbn %jd"
		      " to %jd\n", (intmax_t)ino, (intmax_t)lbn, (intmax_t)ndaddr));
#endif /* DEBUG */
	vput(vp);
	return 0;
}

/*
 * Copy some the fields of the dinode as needed by update_inoblk().
 */
static void
update_inoblk_copy_dinode(struct lfs *fs,
    union lfs_dinode *dstu, const union lfs_dinode *srcu)
{
	if (fs->lfs_is64) {
		struct lfs64_dinode *dst = &dstu->u_64;
		const struct lfs64_dinode *src = &srcu->u_64;
		unsigned i;

		/*
		 * Copy everything but the block pointers and di_blocks.
		 * XXX what about di_extb?
		 */
		dst->di_mode = src->di_mode;
		dst->di_nlink = src->di_nlink;
		dst->di_uid = src->di_uid;
		dst->di_gid = src->di_gid;
		dst->di_blksize = src->di_blksize;
		dst->di_size = src->di_size;
		dst->di_atime = src->di_atime;
		dst->di_mtime = src->di_mtime;
		dst->di_ctime = src->di_ctime;
		dst->di_birthtime = src->di_birthtime;
		dst->di_mtimensec = src->di_mtimensec;
		dst->di_atimensec = src->di_atimensec;
		dst->di_ctimensec = src->di_ctimensec;
		dst->di_birthnsec = src->di_birthnsec;
		dst->di_gen = src->di_gen;
		dst->di_kernflags = src->di_kernflags;
		dst->di_flags = src->di_flags;
		dst->di_extsize = src->di_extsize;
		dst->di_modrev = src->di_modrev;
		dst->di_inumber = src->di_inumber;
		for (i = 0; i < __arraycount(src->di_spare); i++) {
			dst->di_spare[i] = src->di_spare[i];
		}
		/* Short symlinks store their data in di_db. */
		if ((src->di_mode & LFS_IFMT) == LFS_IFLNK
		    && src->di_size < lfs_sb_getmaxsymlinklen(fs)) {
			memcpy(dst->di_db, src->di_db, src->di_size);
		}
	} else {
		struct lfs32_dinode *dst = &dstu->u_32;
		const struct lfs32_dinode *src = &srcu->u_32;

		/* Get mode, link count, size, and times */
		memcpy(dst, src, offsetof(struct lfs32_dinode, di_db[0]));

		/* Then the rest, except di_blocks */
		dst->di_flags = src->di_flags;
		dst->di_gen = src->di_gen;
		dst->di_uid = src->di_uid;
		dst->di_gid = src->di_gid;
		dst->di_modrev = src->di_modrev;

		/* Short symlinks store their data in di_db. */
		if ((src->di_mode & LFS_IFMT) == LFS_IFLNK
		    && src->di_size < lfs_sb_getmaxsymlinklen(fs)) {
			memcpy(dst->di_db, src->di_db, src->di_size);
		}
	}
}

static int
update_inoblk(struct lfs_inofuncarg *lifa)
{
	struct lfs *fs;
	daddr_t offset, daddr;
	struct lwp *l;
	struct vnode *devvp, *vp;
	struct inode *ip;
	union lfs_dinode *dip;
	struct buf *dbp, *ibp;
	int error;
	IFILE *ifp;
	SEGUSE *sup;
	unsigned i, num;
	uint32_t gen, osn, nsn;
	char *buf;

	fs = lifa->fs;
	offset = lifa->offset;
	l = lifa->l;
	devvp = VTOI(fs->lfs_ivnode)->i_devvp;

	/*
	 * Get the inode, update times and perms.
	 * DO NOT update disk blocks, we do that separately.
	 */
	error = bread(devvp, LFS_FSBTODB(fs, offset), lfs_sb_getibsize(fs),
	    0, &dbp);
	if (error) {
		DLOG((DLOG_RF, "update_inoblk: bread returned %d\n", error));
		return error;
	}
	buf = malloc(dbp->b_bcount, M_SEGMENT, M_WAITOK);
	memcpy(buf, dbp->b_data, dbp->b_bcount);
	brelse(dbp, BC_AGE);
	num = LFS_INOPB(fs);
	for (i = num; i-- > 0; ) {
		dip = DINO_IN_BLOCK(fs, buf, i);
		if (lfs_dino_getinumber(fs, dip) <= LFS_IFILE_INUM)
			continue;

		/* Check generation number */
		LFS_IENTRY(ifp, fs, lfs_dino_getinumber(fs, dip), ibp);
		gen = lfs_if_getversion(fs, ifp);
		brelse(ibp, 0);
		if (lfs_dino_getgen(fs, dip) < gen) {
			continue;
		}

		/*
		 * This inode is the newest generation.  Load it.
		 */
		error = lfs_rf_valloc(fs, lfs_dino_getinumber(fs, dip),
				      lfs_dino_getgen(fs, dip),
				      l, &vp, dip);
		if (error) {
			DLOG((DLOG_RF, "update_inoblk: lfs_rf_valloc"
			      " returned %d\n", error));
			continue;
		}
		ip = VTOI(vp);
		if (lfs_dino_getsize(fs, dip) != ip->i_size
		    && vp->v_type != VLNK) {
			/* XXX What should we do with symlinks? */
			DLOG((DLOG_RF, "  ino %jd size %jd -> %jd\n",
				(intmax_t)lfs_dino_getinumber(fs, dip),
				(intmax_t)ip->i_size,
				(intmax_t)lfs_dino_getsize(fs, dip)));
			lfs_truncate(vp, lfs_dino_getsize(fs, dip), 0,
				     NOCRED);
		}
		update_inoblk_copy_dinode(fs, ip->i_din, dip);

		ip->i_flags = lfs_dino_getflags(fs, dip);
		ip->i_gen = lfs_dino_getgen(fs, dip);
		ip->i_uid = lfs_dino_getuid(fs, dip);
		ip->i_gid = lfs_dino_getgid(fs, dip);

		ip->i_mode = lfs_dino_getmode(fs, dip);
		ip->i_nlink = lfs_dino_getnlink(fs, dip);
		ip->i_size = lfs_dino_getsize(fs, dip);

		LFS_SET_UINO(ip, IN_CHANGE | IN_UPDATE);

		/* Re-initialize to get type right */
		ulfs_vinit(vp->v_mount, lfs_specop_p, lfs_fifoop_p,
			  &vp);

		/* Record change in location */
		LFS_IENTRY(ifp, fs, lfs_dino_getinumber(fs, dip), ibp);
		daddr = lfs_if_getdaddr(fs, ifp);
		lfs_if_setdaddr(fs, ifp, offset);
		error = LFS_BWRITE_LOG(ibp); /* Ifile */
		/* And do segment accounting */
		osn = lfs_dtosn(fs, daddr);
		nsn = lfs_dtosn(fs, offset);
		if (DADDR_IS_BAD(daddr) || osn != nsn) {
			if (!DADDR_IS_BAD(daddr)) {
				LFS_SEGENTRY(sup, fs, osn, ibp);
				sup->su_nbytes -= DINOSIZE(fs);
				LFS_WRITESEGENTRY(sup, fs, osn, ibp);
			}
			LFS_SEGENTRY(sup, fs, nsn, ibp);
			sup->su_nbytes += DINOSIZE(fs);
			LFS_WRITESEGENTRY(sup, fs, nsn, ibp);
		}
		vput(vp);
	}
	free(buf, M_SEGMENT);
	
	return 0;
}

/*
 * Note the highest generation number of each inode in the Ifile.
 * This allows us to skip processing data for intermediate versions.
 */
static int
update_inogen(struct lfs_inofuncarg *lifa)
{
	struct lfs *fs;
	daddr_t offset;
	struct vnode *devvp;
	union lfs_dinode *dip;
	struct buf *dbp, *ibp;
	int error;
	IFILE *ifp;
	unsigned i, num;

	fs = lifa->fs;
	offset = lifa->offset;
	devvp = VTOI(fs->lfs_ivnode)->i_devvp;

	/* Read inode block */
	error = bread(devvp, LFS_FSBTODB(fs, offset), lfs_sb_getibsize(fs),
	    0, &dbp);
	if (error) {
		DLOG((DLOG_RF, "update_inoblk: bread returned %d\n", error));
		return error;
	}

	/* Check each inode against ifile entry */
	num = LFS_INOPB(fs);
	for (i = num; i-- > 0; ) {
		dip = DINO_IN_BLOCK(fs, dbp->b_data, i);
		if (lfs_dino_getinumber(fs, dip) == LFS_IFILE_INUM)
			continue;

		/* Update generation number */
		LFS_IENTRY(ifp, fs, lfs_dino_getinumber(fs, dip), ibp);
		if (lfs_if_getversion(fs, ifp) < lfs_dino_getgen(fs, dip))
			lfs_if_setversion(fs, ifp, lfs_dino_getgen(fs, dip));
		error = LFS_BWRITE_LOG(ibp); /* Ifile */
		if (error)
			break;
	}
	brelse(dbp, 0);

	return error;
}

static int
finfo_func_rfw(struct lfs_finfofuncarg *lffa)
{
	struct lfs *fs;
	FINFO *fip;
	daddr_t *offsetp;
	struct lwp *l;
	int j;
	size_t size;

	fs = lffa->fs;
	fip = lffa->finfop;
	offsetp = lffa->offsetp;
	l = lffa->l;
	size = lfs_sb_getbsize(fs);
	for (j = 0; j < lfs_fi_getnblocks(fs, fip); ++j) {
		if (j == lfs_fi_getnblocks(fs, fip) - 1)
			size = lfs_fi_getlastlength(fs, fip);
		
		/* Account for and update any direct blocks */
		if (lfs_fi_getino(fs, fip) > LFS_IFILE_INUM &&
		    lfs_fi_getblock(fs, fip, j) >= 0) {
			update_meta(fs, lfs_fi_getino(fs, fip),
				    lfs_fi_getversion(fs, fip),
				    lfs_fi_getblock(fs, fip, j),
				    *offsetp, size, l);
			++rblkcnt;
		}
		*offsetp += lfs_btofsb(fs, size);
	}

	return 0;
}

static int
skip_superblock(struct lfs *fs, daddr_t *offsetp)
{
	daddr_t offset;
	int i;
	
	/*
	 * If this is segment 0, skip the label.
	 * If the segment has a superblock and we're at the top
	 * of the segment, skip the superblock.
	 */
	offset = *offsetp;
	if (offset == lfs_sb_gets0addr(fs)) {
		offset += lfs_btofsb(fs, LFS_LABELPAD);
	}
	for (i = 0; i < LFS_MAXNUMSB; i++) {
		if (offset == lfs_sb_getsboff(fs, i)) {
			offset += lfs_btofsb(fs, LFS_SBPAD);
			break;
		}
	}
	*offsetp = offset;
	return 0;
}

/*
 * Read the partial sement at offset.
 *
 * If finfo_func and ino_func are both NULL, check the summary
 * and data checksums.  During roll forward, this must be done in its
 * entirety before processing any blocks.
 *
 * If finfo_func is given, use that to process every file block
 * in the segment summary.  If ino_func is given, use that to process
 * every inode block.
 */
#define CKSEG_NONE  0x0000
#define CKSEG_CKSUM 0x0001
#define CKSEG_AVAIL 0x0002

int
lfs_parse_pseg(struct lfs *fs, daddr_t *offsetp, u_int64_t nextserial,
	       kauth_cred_t cred, int *pseg_flags, struct lwp *l,
	       int (*ino_func)(struct lfs_inofuncarg *),
	       int (*finfo_func)(struct lfs_finfofuncarg *),
	       int flags, void *arg)
{
	struct vnode *devvp;
	struct buf *bp, *dbp;
	int error, ninos, i, j;
	SEGSUM *ssp;
	daddr_t offset, prevoffset;
	IINFO *iip;
	FINFO *fip;
	size_t size;
	uint32_t datasum, foundsum;
	char *buf;
	struct lfs_inofuncarg lifa;
	struct lfs_finfofuncarg lffa;

	KASSERT(fs != NULL);
	KASSERT(offsetp != NULL);
	
	devvp = VTOI(fs->lfs_ivnode)->i_devvp;

	/* Set up callback arguments */
	lifa.fs = fs;
	/* lifa.offset = offset; */
	lifa.cred = cred;
	lifa.l = l;
	lifa.buf = malloc(lfs_sb_getbsize(fs), M_SEGMENT, M_WAITOK);

	lifa.arg = arg;
	
	lffa.fs = fs;
	/* lffa.offsetp = offsetp; */
	/* lffa.finfop = finfop; */
	lffa.cred = cred;
	lffa.l = l;
	lffa.arg = arg;

	prevoffset = *offsetp;
	skip_superblock(fs, offsetp);
	offset = *offsetp;
	
	/* Read in the segment summary */
	buf = malloc(lfs_sb_getsumsize(fs), M_SEGMENT, M_WAITOK);
	error = bread(devvp, LFS_FSBTODB(fs, offset), lfs_sb_getsumsize(fs),
	    0, &bp);
	if (error)
		goto err;
	memcpy(buf, bp->b_data, bp->b_bcount);
	brelse(bp, BC_AGE);

	ssp = (SEGSUM *)buf;

	/*
	 * Phase I: Check summary checksum.
	 */
	if (flags & CKSEG_CKSUM) {
		size_t sumstart;

		if (lfs_ss_getmagic(fs, ssp) != SS_MAGIC) {
			DLOG((DLOG_RF, "Bad magic at 0x%" PRIx64 "\n",
			      offset));
			offset = -1;
			goto err;
		}
			
		sumstart = lfs_ss_getsumstart(fs);
		if (lfs_ss_getsumsum(fs, ssp) !=
		    cksum((char *)ssp + sumstart,
			  lfs_sb_getsumsize(fs) - sumstart)) {
			DLOG((DLOG_RF, "Sumsum error at 0x%" PRIx64 "\n",
				offset));
			offset = -1;
			goto err;
		}
#if 0
		/*
		 * Under normal conditions, we should never be producing
		 * a partial segment with neither inode blocks nor data blocks.
		 * However, these do sometimes appear and they need not
		 * prevent us from continuing.
		 */
		if (lfs_ss_getnfinfo(fs, ssp) == 0 &&
		    lfs_ss_getninos(fs, ssp) == 0) {
			DLOG((DLOG_RF, "Empty pseg at 0x%" PRIx64 "\n",
				offset));
			offset = -1;
			goto err;
		}
#endif /* 0 */
		if (lfs_sb_getversion(fs) == 1) {
			if (lfs_ss_getcreate(fs, ssp) < lfs_sb_gettstamp(fs)) {
				DLOG((DLOG_RF, "Old data at 0x%" PRIx64 "\n", offset));
				offset = -1;
				goto err;
			}
		} else {
			if (nextserial > 0
			    && lfs_ss_getserial(fs, ssp) != nextserial) {
				DLOG((DLOG_RF, "Serial number at 0x%jx given as 0x%jx,"
				      " expected 0x%jx\n", (intmax_t)offset,
				      (intmax_t)lfs_ss_getserial(fs, ssp),
				      (intmax_t)nextserial));
				offset = -1;
				goto err;
			}
			if (lfs_ss_getident(fs, ssp) != lfs_sb_getident(fs)) {
				DLOG((DLOG_RF, "Incorrect fsid (0x%x vs 0x%x) at 0x%"
				      PRIx64 "\n", lfs_ss_getident(fs, ssp),
				      lfs_sb_getident(fs), offset));
				offset = -1;
				goto err;
			}
		}
	}
	if (pseg_flags)
		*pseg_flags = lfs_ss_getflags(fs, ssp);
	offset += lfs_btofsb(fs, lfs_sb_getsumsize(fs));

	/* Handle individual blocks */
	foundsum = 0;
	ninos = howmany(lfs_ss_getninos(fs, ssp), LFS_INOPB(fs));
	iip = SEGSUM_IINFOSTART(fs, buf);
	fip = SEGSUM_FINFOBASE(fs, (SEGSUM *)buf);
	KASSERT(lfs_ss_getnfinfo(fs, ssp) <= lfs_sb_getssize(fs) / lfs_sb_getfsize(fs));
	KASSERT(lfs_ss_getnfinfo(fs, ssp) <= lfs_sb_getfsize(fs) / sizeof(FINFO32));
	for (i = 0; i < lfs_ss_getnfinfo(fs, ssp) || ninos; ++i) {
		/* Inode block? */
		if (ninos && lfs_ii_getblock(fs, iip) == offset) {
			if (flags & CKSEG_CKSUM) {
				/* Read in the head and add to the buffer */
				error = bread(devvp, LFS_FSBTODB(fs, offset),
					lfs_sb_getbsize(fs), 0, &dbp);
				if (error) {
					offset = -1;
					goto err;
				}
				foundsum = lfs_cksum_part(dbp->b_data,
					sizeof(uint32_t), foundsum);
				brelse(dbp, BC_AGE);
			} else if (ino_func != NULL) {
				lifa.offset = offset;
				error = (*ino_func)(&lifa);
				if (error != 0) {
					offset = -1;
					goto err;
				}
			}
			
			offset += lfs_btofsb(fs, lfs_sb_getibsize(fs));
			iip = NEXTLOWER_IINFO(fs, iip);
			--ninos;
			--i; /* compensate for ++i in loop header */
			continue;
		}
		
		/* File block */
		size = lfs_sb_getbsize(fs);
		if (flags & CKSEG_CKSUM) {
			for (j = 0; j < lfs_fi_getnblocks(fs, fip); ++j) {
				if (j == lfs_fi_getnblocks(fs, fip) - 1)
					size = lfs_fi_getlastlength(fs, fip);
				error = bread(devvp, LFS_FSBTODB(fs, offset),
					      size, 0, &dbp);
				if (error) {
					offset = -1;
					goto err;
				}
				foundsum = lfs_cksum_part(dbp->b_data,
							  sizeof(uint32_t), foundsum);
				brelse(dbp, BC_AGE);
				offset += lfs_btofsb(fs, size);
			}
		} else if (finfo_func != NULL) {
			lffa.offsetp = &offset;
			lffa.finfop = fip;
			(*finfo_func)(&lffa);
		} else {
			int n = lfs_fi_getnblocks(fs, fip);
			size = lfs_fi_getlastlength(fs, fip);
			offset += lfs_btofsb(fs, lfs_sb_getbsize(fs) * (n - 1)
					     + size);
		}
		fip = NEXT_FINFO(fs, fip);
	}

	/* Checksum the array, compare */
	if (flags & CKSEG_CKSUM) {
		datasum = lfs_ss_getdatasum(fs, ssp);
		foundsum = lfs_cksum_fold(foundsum);
		if (datasum != foundsum) {
			DLOG((DLOG_RF, "Datasum error at 0x%" PRIx64
			      " (wanted %x got %x)\n",
			      offset, datasum, foundsum));
			offset = -1;
			goto err;
		}
	} else {
		/* Don't clog the buffer queue */
		mutex_enter(&lfs_lock);
		if (locked_queue_count > LFS_MAX_BUFS ||
		    locked_queue_bytes > LFS_MAX_BYTES) {
			lfs_flush(fs, SEGM_CKP, 0);
		}
		mutex_exit(&lfs_lock);
	}

	/*
	 * If we're at the end of the segment, move to the next.
	 * A partial segment needs space for a segment header (1 fsb)
	 * and a full block ("frag" fsb).  Thus, adding "frag" fsb should
	 * still be within the current segment (whereas frag + 1 might
	 * be at the start of the next segment).
	 *
	 * This needs to match the definition of LFS_PARTIAL_FITS
	 * in lfs_segment.c.
	 */
	if (lfs_dtosn(fs, offset + lfs_sb_getfrag(fs))
	    != lfs_dtosn(fs, offset)) {
		if (lfs_dtosn(fs, offset) == lfs_dtosn(fs, lfs_ss_getnext(fs,
									ssp))) {
			offset = -1;
			goto err;
		}
		offset = lfs_ss_getnext(fs, ssp);
		DLOG((DLOG_RF, "LFS roll forward: moving to offset 0x%" PRIx64
		       " -> segment %d\n", offset, lfs_dtosn(fs,offset)));
	}
	if (flags & CKSEG_AVAIL)
		lfs_sb_subavail(fs, offset - prevoffset);

    err:
	free(lifa.buf, M_SEGMENT);
	free(buf, M_SEGMENT);
	
	*offsetp = offset;
	return 0;
}

/*
 * Roll forward.
 */
void
lfs_roll_forward(struct lfs *fs, struct mount *mp, struct lwp *l)
{
	int flags, dirty;
	daddr_t startoffset, offset, nextoffset, endpseg;
	u_int64_t nextserial, startserial, endserial;
	int sn, curseg;
	struct proc *p;
	kauth_cred_t cred;
	SEGUSE *sup;
	struct buf *bp;

	p = l ? l->l_proc : NULL;
	cred = p ? p->p_cred : NOCRED;

	/*
	 * We don't roll forward for v1 filesystems, because
	 * of the danger that the clock was turned back between the last
	 * checkpoint and crash.  This would roll forward garbage.
	 *
	 * v2 filesystems don't have this problem because they use a
	 * monotonically increasing serial number instead of a timestamp.
	 */
	rblkcnt = 0;
	if ((lfs_sb_getpflags(fs) & LFS_PF_CLEAN) || !lfs_do_rfw
	    || lfs_sb_getversion(fs) <= 1 || p == NULL)
		return;

	DLOG((DLOG_RF, "%s: begin roll forward at serial 0x%jx\n",
		lfs_sb_getfsmnt(fs), (intmax_t)lfs_sb_getserial(fs)));
	DEBUG_CHECK_FREELIST(fs);
		
	/*
	 * Phase I: Find the address of the last good partial
	 * segment that was written after the checkpoint.  Mark
	 * the segments in question dirty, so they won't be
	 * reallocated.
	 */
	endpseg = startoffset = offset = lfs_sb_getoffset(fs);
	flags = 0x0;
	DLOG((DLOG_RF, "LFS roll forward phase 1: start at offset 0x%"
	      PRIx64 "\n", offset));
	LFS_SEGENTRY(sup, fs, lfs_dtosn(fs, offset), bp);
	if (!(sup->su_flags & SEGUSE_DIRTY))
		lfs_sb_subnclean(fs, 1);
	sup->su_flags |= SEGUSE_DIRTY;
	LFS_WRITESEGENTRY(sup, fs, lfs_dtosn(fs, offset), bp);

	startserial = lfs_sb_getserial(fs);
	endserial = nextserial = startserial + 1;
	nextoffset = offset;
	while (1) {
		nextoffset = offset;
		lfs_parse_pseg(fs, &nextoffset, nextserial,
			     cred, &flags, l, NULL, NULL, CKSEG_CKSUM, NULL);
		if (nextoffset == -1)
			break;
		if (lfs_sntod(fs, offset) != lfs_sntod(fs, nextoffset)) {
			LFS_SEGENTRY(sup, fs, lfs_dtosn(fs, offset),
				     bp);
			if (!(sup->su_flags & SEGUSE_DIRTY))
				lfs_sb_subnclean(fs, 1);
			sup->su_flags |= SEGUSE_DIRTY;
			LFS_WRITESEGENTRY(sup, fs, lfs_dtosn(fs, offset), bp);
		}

		DLOG((DLOG_RF, "LFS roll forward phase 1: offset=0x%jx"
			" serial=0x%jx\n", (intmax_t)nextoffset,
			(intmax_t)nextserial));
		if (flags & SS_DIROP) {
			DLOG((DLOG_RF, "lfs_mountfs: dirops at 0x%"
			      PRIx64 "\n", offset));
			if (!(flags & SS_CONT)) {
			     DLOG((DLOG_RF, "lfs_mountfs: dirops end "
				   "at 0x%" PRIx64 "\n", offset));
			}
		}
		offset = nextoffset;
		++nextserial;
		
		if (!(flags & SS_CONT)) {
			endpseg = nextoffset;
			endserial = nextserial;
		}
		if (lfs_rfw_max_psegs > 0
		    && nextserial > startserial + lfs_rfw_max_psegs)
			break;
	}
	if (flags & SS_CONT) {
		DLOG((DLOG_RF, "LFS roll forward: warning: incomplete "
			"dirops discarded (0x%jx < 0x%jx)\n",
			endpseg, nextoffset));
	}
	if (lfs_sb_getversion(fs) > 1)
		lfs_sb_setserial(fs, endserial);
	DLOG((DLOG_RF, "LFS roll forward phase 1: completed: "
	      "endpseg=0x%" PRIx64 "\n", endpseg));
	offset = startoffset;
	if (offset != endpseg) {
		/* Don't overwrite what we're trying to preserve */
		lfs_sb_setoffset(fs, endpseg);
		lfs_sb_setcurseg(fs, lfs_sntod(fs, lfs_dtosn(fs, endpseg)));
		for (sn = curseg = lfs_dtosn(fs, lfs_sb_getcurseg(fs));;) {
			sn = (sn + 1) % lfs_sb_getnseg(fs);
			/* XXX could we just fail to roll forward? */
			if (sn == curseg)
				panic("lfs_mountfs: no clean segments");
			LFS_SEGENTRY(sup, fs, sn, bp);
			dirty = (sup->su_flags & SEGUSE_DIRTY);
			brelse(bp, 0);
			if (!dirty)
				break;
		}
		lfs_sb_setnextseg(fs, lfs_sntod(fs, sn));
		/* Explicitly set this segment dirty */
		LFS_SEGENTRY(sup, fs, lfs_dtosn(fs, endpseg), bp);
		sup->su_flags |= SEGUSE_DIRTY | SEGUSE_ACTIVE;
		LFS_WRITESEGENTRY(sup, fs, lfs_dtosn(fs, endpseg), bp);

		/*
		 * Phase II: Identify the highest generation of each
		 * inode.  We will ignore inodes and data blocks
		 * belonging to old versions.
		 */
		offset = startoffset;
		nextserial = startserial + 1;
		DLOG((DLOG_RF, "LFS roll forward phase 2 beginning\n"));
		while (offset > 0 && offset != endpseg) {
			lfs_parse_pseg(fs, &offset, nextserial++, cred,
				     NULL, l, update_inogen, NULL,
				     CKSEG_NONE, NULL);
			DEBUG_CHECK_FREELIST(fs);
		}

		/*
		 * Phase III: Update inodes.
		 */
		offset = startoffset;
		nextserial = startserial + 1;
		DLOG((DLOG_RF, "LFS roll forward phase 3 beginning\n"));
		while (offset > 0 && offset != endpseg) {
			lfs_parse_pseg(fs, &offset, nextserial++, cred,
				     NULL, l, update_inoblk, NULL,
				     CKSEG_NONE, NULL);
			DEBUG_CHECK_FREELIST(fs);
		}
		
		/*
		 * Phase IV: Roll forward, updating data blocks.
		 */
		offset = startoffset;
		nextserial = startserial + 1;
		DLOG((DLOG_RF, "LFS roll forward phase 4 beginning\n"));
		while (offset > 0 && offset != endpseg) {
			lfs_parse_pseg(fs, &offset, nextserial++, cred,
				     NULL, l, NULL, finfo_func_rfw,
				     CKSEG_AVAIL, NULL);
			DEBUG_CHECK_FREELIST(fs);
		}

		/*
		 * Finish: flush our changes to disk.
		 */
		lfs_sb_setserial(fs, endserial);

		lfs_segwrite(mp, SEGM_CKP | SEGM_SYNC);
		DLOG((DLOG_RF, "lfs_mountfs: roll forward "
		      "examined %jd blocks\n",
		      (intmax_t)(endpseg - startoffset)));
	}

	/* Get rid of our vnodes, except the ifile */
	drop_vnode_pages(mp, l);
	DLOG((DLOG_RF, "LFS roll forward complete\n"));
	printf("%s: roll forward recovered %d data blocks\n",
		lfs_sb_getfsmnt(fs), rblkcnt);

	/*
	 * At this point we have no more changes to write to disk.
	 * Reset the "avail" count to match the segments as they
	 * appear on disk, and the clean segment count.
	 */
	lfs_reset_avail(fs);
}

static bool
all_selector(void *cl, struct vnode *vp)
{  
	return true;
}

/*
 * Dump any pages from vnodes that may have been put on
 * during truncation.
 */
static void
drop_vnode_pages(struct mount *mp, struct lwp *l)
{       
       struct vnode_iterator *marker;
       struct lfs *fs;
       struct vnode *vp;
      
       fs = VFSTOULFS(mp)->um_lfs;
       vfs_vnode_iterator_init(mp, &marker);
       while ((vp = vfs_vnode_iterator_next(marker,
               all_selector, NULL)) != NULL) {
               if (vp == fs->lfs_ivnode)
                       continue;
               VOP_LOCK(vp, LK_EXCLUSIVE | LK_RETRY);
               uvm_vnp_setsize(vp, 0);
               uvm_vnp_setsize(vp, VTOI(vp)->i_size);
               VOP_UNLOCK(vp);
               vrele(vp);
       }
       vfs_vnode_iterator_destroy(marker);
}     

static int
ino_func_setclean(struct lfs_inofuncarg *lifa)
{
	struct lfs *fs;
	daddr_t offset;
	struct vnode *devvp, *vp;
	union lfs_dinode *dip;
	struct buf *dbp, *ibp;
	int error;
	IFILE *ifp;
	unsigned i, num;
	daddr_t true_addr;
	ino_t ino;

	fs = lifa->fs;
	offset = lifa->offset;
	devvp = VTOI(fs->lfs_ivnode)->i_devvp;

	/* Read inode block */
	error = bread(devvp, LFS_FSBTODB(fs, offset), lfs_sb_getibsize(fs),
	    0, &dbp);
	if (error) {
		DLOG((DLOG_RF, "ino_func_setclean: bread returned %d\n",
		      error));
		return error;
	}
	memcpy(lifa->buf, dbp->b_data, dbp->b_bcount);
	brelse(dbp, BC_AGE);

	/* Check each inode against ifile entry */
	num = LFS_INOPB(fs);
	for (i = num; i-- > 0; ) {
		dip = DINO_IN_BLOCK(fs, lifa->buf, i);
		ino = lfs_dino_getinumber(fs, dip);
		if (ino == LFS_IFILE_INUM) {
			/* Check address against superblock */
			true_addr = lfs_sb_getidaddr(fs);
		} else {
			/* Not ifile.  Check address against ifile. */
			LFS_IENTRY(ifp, fs, ino, ibp);
			true_addr = lfs_if_getdaddr(fs, ifp);
			brelse(ibp, 0);
		}
		if (offset != true_addr)
			continue;

		/* XXX We can use fastvget here! */

		/*
		 * An inode we need to relocate.
		 * Get it if we can.
		 */
		error = VFS_VGET(fs->lfs_ivnode->v_mount, ino,
				 LK_EXCLUSIVE | LK_NOWAIT, &vp);
		if (error)
			continue;

		KASSERT(VTOI(vp)->i_gen == lfs_dino_getgen(fs, dip));
		lfs_setclean(fs, vp);
		VOP_UNLOCK(vp);
		vrele(vp);

	}

	return error;
}

static int
ino_func_rewrite(struct lfs_inofuncarg *lifa)
{
	struct lfs *fs;
	daddr_t offset;
	struct vnode *devvp, *vp;
	union lfs_dinode *dip;
	struct buf *dbp, *ibp;
	int error;
	IFILE *ifp;
	unsigned i, num;
	daddr_t true_addr;
	ino_t ino;

	fs = lifa->fs;
	offset = lifa->offset;
	devvp = VTOI(fs->lfs_ivnode)->i_devvp;

	/* Read inode block */
	error = bread(devvp, LFS_FSBTODB(fs, offset), lfs_sb_getibsize(fs),
	    0, &dbp);
	if (error) {
		DLOG((DLOG_RF, "ino_func_rewrite: bread returned %d\n",
		      error));
		return error;
	}
	memcpy(lifa->buf, dbp->b_data, dbp->b_bcount);
	brelse(dbp, BC_AGE);

	/* Check each inode against ifile entry */
	num = LFS_INOPB(fs);
	for (i = num; i-- > 0; ) {
		dip = DINO_IN_BLOCK(fs, lifa->buf, i);
		ino = lfs_dino_getinumber(fs, dip);
		if (ino == LFS_IFILE_INUM) {
			/* Check address against superblock */
			true_addr = lfs_sb_getidaddr(fs);
		} else {
			/* Not ifile.  Check address against ifile. */
			LFS_IENTRY(ifp, fs, ino, ibp);
			true_addr = lfs_if_getdaddr(fs, ifp);
			brelse(ibp, 0);
		}
		if (offset != true_addr)
			continue;

		if (ino == LFS_IFILE_INUM)
			continue;
		
		/* XXX We can use fastvget here! */

		/*
		 * An inode we need to relocate.
		 * Get it if we can.
		 */
		error = VFS_VGET(fs->lfs_ivnode->v_mount, ino,
				 LK_EXCLUSIVE | LK_NOWAIT, &vp);
		if (error)
			continue;

		KASSERT(VTOI(vp)->i_gen == lfs_dino_getgen(fs, dip));
		
		if (!(VTOI(vp)->i_state & IN_CLEANING)) {
			lfs_setclean(fs, vp);
			lfs_writeinode(fs, fs->lfs_sp, VTOI(vp));
		}

		VOP_UNLOCK(vp);
		vrele(vp);

	}

	return error;
}

static int
rewrite_block(struct lfs *fs, struct vnode *vp, daddr_t lbn, daddr_t offset, size_t size, int *have_finfop)
{
	daddr_t daddr;
	int error;
	struct buf *bp;
	struct inode *ip;

	KASSERT(have_finfop != NULL);
	
	/* Look up current location of this block. */
	error = VOP_BMAP(vp, lbn, NULL, &daddr, NULL);
	if (error)
		return error;
	
	/* Skip any block that is not here. */
	if (offset != 0 && LFS_DBTOFSB(fs, daddr) != offset)
		return ESTALE;
	
	/*
	 * It is (was recently) here.  Read the block.
	 */
	//size = lfs_blksize(fs, VTOI(vp), lbn);
	error = bread(vp, lbn, size, 0, &bp);
	if (error)
		return error;

	if (vp == fs->lfs_ivnode) {
		VOP_BWRITE(vp, bp);
	} else {
		/* Get ready to write. */
		if (!*have_finfop) {
			ip = VTOI(vp);
			lfs_acquire_finfo(fs, ip->i_number, ip->i_gen);
			fs->lfs_sp->vp = vp;
			*have_finfop = 1;
		}
	
		KASSERT(bp->b_vp == vp);
		/* bp->b_cflags |= BC_INVAL; */ /* brelse will kill the buffer */
		lfs_bwrite_ext(bp, BW_CLEAN);
		KASSERT(bp->b_vp == vp);
		mutex_enter(&bufcache_lock);
		while (lfs_gatherblock(fs->lfs_sp, bp, &bufcache_lock)) {
			KASSERT(bp->b_vp != NULL);
		}
		mutex_exit(&bufcache_lock);
	
		KASSERT(bp->b_flags & B_GATHERED);
		KASSERT(fs->lfs_sp->cbpp[-1] == bp);
	}
	return 0;
}

static int
finfo_func_rewrite(struct lfs_finfofuncarg *lffa)
{
	struct lfs *fs;
	FINFO *fip;
	daddr_t *offsetp;
	int j, have_finfo, error;
	size_t size, bytes;
	ino_t ino;
	uint32_t gen;
	struct vnode *vp;
	daddr_t lbn;
	int *fragsp;

	fs = lffa->fs;
	fip = lffa->finfop;
	offsetp = lffa->offsetp;
	fragsp = (int *)lffa->arg;
	
	/* Get the inode and check its version. */
	ino = lfs_fi_getino(fs, fip);
	gen = lfs_fi_getversion(fs, fip);
	error = VFS_VGET(fs->lfs_ivnode->v_mount, ino, LK_EXCLUSIVE|LK_NOWAIT,
			 &vp);

	/*
	 * If we can't, or if version is wrong, or it has dirop blocks on it,
	 * we can't relocate its blocks; but we still have to count
	 * blocks through the partial segment to return the right offset.
	 * XXX actually we can move DIROP vnodes' *old* data, as long
	 * XXX as we are sure that we are moving *only* the old data---?
	 */
	if (error || VTOI(vp)->i_gen != gen || (vp->v_uflag & VU_DIROP)) {
		if (error == 0)
			error = ESTALE;
		
		if (vp != NULL) {
			VOP_UNLOCK(vp);
			vrele(vp);
			vp = NULL;
		}
		bytes = ((lfs_fi_getnblocks(fs, fip) - 1) << lfs_sb_getbshift(fs))
			+ lfs_fi_getlastlength(fs, fip);
		*offsetp += lfs_btofsb(fs, bytes);
		
		return error;
	}

	/*
	 * We have the vnode and its version is correct.
	 * Take a cleaning reference; and loop through the blocks
	 * and rewrite them.
	 */
	lfs_setclean(fs, vp);
	size = lfs_sb_getbsize(fs);
	have_finfo = 0;
	for (j = 0; j < lfs_fi_getnblocks(fs, fip); ++j) {
		if (j == lfs_fi_getnblocks(fs, fip) - 1)
			size = lfs_fi_getlastlength(fs, fip);
		/*
		 * An error of ESTALE indicates that there was nothing
		 * to rewrite; this is not a problem.  Any other error
		 * causes us to skip the rest of this FINFO.
		 */
		if (vp != NULL && error == 0) {
			lbn = lfs_fi_getblock(fs, fip, j);
			error = rewrite_block(fs, vp, lbn, *offsetp,
					      size, &have_finfo);
			if (error == ESTALE)
				error = 0;
			if (fragsp != NULL && error == 0)
				*fragsp += lfs_btofsb(fs, size);
		}
		*offsetp += lfs_btofsb(fs, size);
	}

	/*
	 * If we acquired finfo, release it and write the blocks.
	 */
	if (have_finfo) {
		lfs_updatemeta(fs->lfs_sp);
		fs->lfs_sp->vp = NULL;
		lfs_release_finfo(fs);
		lfs_writeinode(fs, fs->lfs_sp, VTOI(vp));
	}
	
	/* Release vnode */
	VOP_UNLOCK(vp);
	vrele(vp);

	return error;
}

static int
finfo_func_setclean(struct lfs_finfofuncarg *lffa)
{
	struct lfs *fs;
	FINFO *fip;
	daddr_t *offsetp;
	int error;
	size_t bytes;
	ino_t ino;
	uint32_t gen;
	struct vnode *vp;

	fs = lffa->fs;
	fip = lffa->finfop;
	offsetp = lffa->offsetp;

	/* Get the inode and check its version. */
	ino = lfs_fi_getino(fs, fip);
	gen = lfs_fi_getversion(fs, fip);
	error = VFS_VGET(fs->lfs_ivnode->v_mount, ino, LK_EXCLUSIVE|LK_NOWAIT,
			 &vp);

	/* If we have it and its version is right, take a cleaning reference */
	if (error == 0 && VTOI(vp)->i_gen == gen)
		lfs_setclean(fs, vp);
	
	if (vp != NULL) {
		VOP_UNLOCK(vp);
		vrele(vp);
		vp = NULL;
	}

	/* Skip to the next block */
	bytes = ((lfs_fi_getnblocks(fs, fip) - 1) << lfs_sb_getbshift(fs))
		+ lfs_fi_getlastlength(fs, fip);
	*offsetp += lfs_btofsb(fs, bytes);
	
	return error;
}

/*
 * Use the partial-segment parser to rewrite (clean) a segment.
 */
int
lfs_rewrite_segment(struct lfs *fs, int sn, int *fragsp, kauth_cred_t cred, struct lwp *l)
{
	daddr_t ooffset, offset, endpseg;

	ASSERT_SEGLOCK(fs);

	offset = lfs_sntod(fs, sn);
	skip_superblock(fs, &offset);
	endpseg = lfs_sntod(fs, sn + 1);
	
	while (offset > 0 && offset != endpseg) {
		/* First check summary validity (XXX unnecessary?) */
		ooffset = offset;
		lfs_parse_pseg(fs, &offset, 0, cred, NULL, l,
			     NULL, NULL, CKSEG_CKSUM, NULL);
		if (offset == ooffset)
			break;

		/*
		 * Valid, proceed.
		 *
		 * First write the file blocks, marking their
		 * inodes IN_CLEANING.
		 */
		offset = ooffset;
		lfs_parse_pseg(fs, &offset, 0, cred, NULL, l,
			       NULL, finfo_func_rewrite,
			       CKSEG_NONE, fragsp);

		/*
		 * Now go back and pick up any inodes that
		 * were not already marked IN_CLEANING, and
		 * write them as well.
		 */
		offset = ooffset;
		lfs_parse_pseg(fs, &offset, 0, cred, NULL, l,
			       ino_func_rewrite, NULL,
			       CKSEG_NONE, fragsp);
	}
	return 0;
}

/*
 * Rewrite the contents of one or more segments, in preparation for
 * marking them clean.
 */
int
lfs_rewrite_segments(struct lfs *fs, int *snn, int len, int *directp, int *offsetp, struct lwp *l)
{
	kauth_cred_t cred;
	int i, error;
	struct buf *bp;
	SEGUSE *sup;
	daddr_t offset, endpseg;
	
	ASSERT_NO_SEGLOCK(fs);
	
	cred = l ? l->l_cred : NOCRED;

	/* Prevent new dirops and acquire the cleaner lock. */
	lfs_writer_enter(fs, "rewritesegs");
	if ((error = lfs_cleanerlock(fs)) != 0) {
		lfs_writer_leave(fs);
		return error;
	}
	
	/*
	 * Pre-reference vnodes now that we have cleaner lock
	 * but before we take the segment lock.  We don't want to
	 * mix cleaning blocks with flushed vnodes.
	 */
	for (i = 0; i < len; i++) {
		error = 0;
		/* Refuse to clean segments that are ACTIVE */
		LFS_SEGENTRY(sup, fs, snn[i], bp);
		if (sup->su_flags & SEGUSE_ACTIVE
		    || !(sup->su_flags & SEGUSE_DIRTY))
			error = EINVAL;

		brelse(bp, 0);
		if (error)
			break;

		offset = lfs_sntod(fs, snn[i]);
		skip_superblock(fs, &offset);
		endpseg = lfs_sntod(fs, snn[i] + 1);
		
		while (offset > 0 && offset != endpseg) {
			lfs_parse_pseg(fs, &offset, 0, cred, NULL, l,
				       ino_func_setclean, finfo_func_setclean,
				       CKSEG_NONE, NULL);
		}
	}

	/*
	 * Actually rewrite the contents of the segment.
	 */
	lfs_seglock(fs, SEGM_CLEAN);

	for (i = 0; i < len; i++) {
		error = 0;
		/* Refuse to clean segments that are ACTIVE */
		LFS_SEGENTRY(sup, fs, snn[i], bp);
		if (sup->su_flags & SEGUSE_ACTIVE
		    || !(sup->su_flags & SEGUSE_DIRTY))
			error = EINVAL;

		brelse(bp, 0);
		if (error)
			break;

		error = lfs_rewrite_segment(fs, snn[i], directp, cred, l);
		if (error) {
			printf("  rewrite_segment returned %d\n", error);
			break;
		}
	}
	while (lfs_writeseg(fs, fs->lfs_sp))
		;

	*offsetp = lfs_btofsb(fs, fs->lfs_sp->bytes_written);
	lfs_segunlock(fs);
	lfs_cleanerunlock(fs);
	lfs_writer_leave(fs);

	return error;
}

#if 0
static bool
lfs_isseq(const struct lfs *fs, long int lbn1, long int lbn2)
{
	return lbn2 == lbn1 + lfs_sb_getfrag(__UNCONST(fs));
}

/*
 * Rewrite the contents of a file in order to coalesce it.
 * We don't bother rewriting indirect blocks because they will have to
 * be rewritten anyway when we rewrite the direct blocks.
 */
int
lfs_rewrite_file(struct lfs *fs, ino_t ino, struct lwp *l)
{
	daddr_t lbn, hiblk, daddr;
	int i, error, num, run;
	struct vnode *vp;
	struct indir indirs[ULFS_NIADDR+2];
	size_t size;
	
	ASSERT_SEGLOCK(fs);

	error = VFS_VGET(fs->lfs_ivnode->v_mount, ino, LK_EXCLUSIVE, &vp);
	if (error)
		return error;

	lfs_acquire_finfo(fs, ino, VTOI(vp)->i_gen);
	for (lbn = 0, hiblk = VTOI(vp)->i_lfs_hiblk; lbn < hiblk; ++lbn) {
		error = ulfs_bmaparray(vp, lbn, &daddr, &indirs[0], &num, &run,
				       lfs_isseq);
		if (daddr == UNASSIGNED)
			continue;
		for (i = 0; i <= run; i++) {
			size = lfs_blksize(fs, VTOI(vp), lbn);
			error = rewrite_block(fs, vp, lbn++, 0x0, size, NULL);
			if (error)
				break;
		}
	}
	lfs_release_finfo(fs);
	while (lfs_writeseg(fs, fs->lfs_sp))
		;
	lfs_segunlock(fs);

	return error;
}
#endif /* 0 */


static int
ino_func_checkempty(struct lfs_inofuncarg *lifa)
{
	struct lfs *fs;
	daddr_t offset;
	struct vnode *devvp;
	union lfs_dinode *dip;
	struct buf *dbp, *ibp;
	int error;
	IFILE *ifp;
	unsigned i, num;
	daddr_t true_addr;
	ino_t ino;

	fs = lifa->fs;
	offset = lifa->offset;
	devvp = VTOI(fs->lfs_ivnode)->i_devvp;

	/* Read inode block */
	error = bread(devvp, LFS_FSBTODB(fs, offset), lfs_sb_getibsize(fs),
	    0, &dbp);
	if (error) {
		DLOG((DLOG_RF, "ino_func_checkempty: bread returned %d\n",
		      error));
		return error;
	}

	/* Check each inode against ifile entry */
	num = LFS_INOPB(fs);
	for (i = num; i-- > 0; ) {
		dip = DINO_IN_BLOCK(fs, dbp->b_data, i);
		ino = lfs_dino_getinumber(fs, dip);
		if (ino == LFS_IFILE_INUM) {
			/* Check address against superblock */
			true_addr = lfs_sb_getidaddr(fs);
		} else {
			/* Not ifile.  Check address against ifile. */
			LFS_IENTRY(ifp, fs, ino, ibp);
			true_addr = lfs_if_getdaddr(fs, ifp);
			brelse(ibp, 0);
		}
		if (offset == true_addr) {
			error = EEXIST;
			break;
		}
	}
	brelse(dbp, BC_AGE);

	return error;
}

static int
finfo_func_checkempty(struct lfs_finfofuncarg *lffa)
{
	struct lfs *fs;
	FINFO *fip;
	daddr_t *offsetp;
	int j, error;
	size_t size, bytes;
	ino_t ino;
	uint32_t gen;
	struct vnode *vp;
	daddr_t lbn, daddr;
	
	fs = lffa->fs;
	fip = lffa->finfop;
	offsetp = lffa->offsetp;

	/* Get the inode and check its version. */
	ino = lfs_fi_getino(fs, fip);
	gen = lfs_fi_getversion(fs, fip);
	error = VFS_VGET(fs->lfs_ivnode->v_mount, ino, LK_EXCLUSIVE, &vp);

	/*
	 * If we can't, or if version is wrong, this FINFO does not refer
	 * to a live file.  Skip over it and continue.
	 */
	if (error || VTOI(vp)->i_gen != gen) {
		if (error == 0)
			error = ESTALE;
		
		if (vp != NULL) {
			VOP_UNLOCK(vp);
			vrele(vp);
			vp = NULL;
		}
		bytes = ((lfs_fi_getnblocks(fs, fip) - 1)
			 << lfs_sb_getbshift(fs))
			+ lfs_fi_getlastlength(fs, fip);
		*offsetp += lfs_btofsb(fs, bytes);
		
		return error;
	}

	/*
	 * We have the vnode and its version is correct.
	 * Loop through the blocks and check their currency.
	 */
	size = lfs_sb_getbsize(fs);
	for (j = 0; j < lfs_fi_getnblocks(fs, fip); ++j) {
		if (j == lfs_fi_getnblocks(fs, fip) - 1)
			size = lfs_fi_getlastlength(fs, fip);
		if (vp != NULL) {
			lbn = lfs_fi_getblock(fs, fip, j);
			
			/* Look up current location of this block. */
			error = VOP_BMAP(vp, lbn, NULL, &daddr, NULL);
			if (error)
				break;
	
			/* If it is here, the segment is not empty. */
			if (LFS_DBTOFSB(fs, daddr) == *offsetp) {
				error = EEXIST;
				break;
			}
		}
		*offsetp += lfs_btofsb(fs, size);
	}

	/* Release vnode */
	VOP_UNLOCK(vp);
	vrele(vp);

	return error;
}

int
lfs_checkempty(struct lfs *fs, int sn, kauth_cred_t cred, struct lwp *l)
{
	daddr_t offset, endpseg;
	int error;

	ASSERT_SEGLOCK(fs);

	offset = lfs_sntod(fs, sn);
	skip_superblock(fs, &offset);
	endpseg = lfs_sntod(fs, sn + 1);
	
	while (offset > 0 && offset < endpseg) {
		error = lfs_parse_pseg(fs, &offset, 0, cred, NULL, l,
				     ino_func_checkempty,
				     finfo_func_checkempty,
				     CKSEG_NONE, NULL);
		if (error)
			return error;
	}
	return 0;
}
