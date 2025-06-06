/*	$NetBSD: ld.c,v 1.117 2025/04/13 14:00:59 jakllsch Exp $	*/

/*-
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran and Charles M. Hannum.
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
 * Disk driver for use by RAID controllers.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ld.c,v 1.117 2025/04/13 14:00:59 jakllsch Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/endian.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/dkio.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/syslog.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/reboot.h>

#include <dev/ldvar.h>

#include "ioconf.h"

static void	ldminphys(struct buf *bp);
static bool	ld_suspend(device_t, const pmf_qual_t *);
static bool	ld_resume(device_t, const pmf_qual_t *);
static bool	ld_shutdown(device_t, int);
static int	ld_diskstart(device_t, struct buf *bp);
static void	ld_iosize(device_t, int *);
static int	ld_dumpblocks(device_t, void *, daddr_t, int);
static void	ld_fake_geometry(struct ld_softc *);
static void	ld_set_geometry(struct ld_softc *);
static void	ld_config_interrupts (device_t);
static int	ld_lastclose(device_t);
static int	ld_discard(device_t, off_t, off_t);
static int	ld_flush(device_t, bool);

static dev_type_open(ldopen);
static dev_type_close(ldclose);
static dev_type_read(ldread);
static dev_type_write(ldwrite);
static dev_type_ioctl(ldioctl);
static dev_type_strategy(ldstrategy);
static dev_type_dump(lddump);
static dev_type_size(ldsize);
static dev_type_discard(lddiscard);

const struct bdevsw ld_bdevsw = {
	.d_open = ldopen,
	.d_close = ldclose,
	.d_strategy = ldstrategy,
	.d_ioctl = ldioctl,
	.d_dump = lddump,
	.d_psize = ldsize,
	.d_discard = lddiscard,
	.d_flag = D_DISK | D_MPSAFE
};

const struct cdevsw ld_cdevsw = {
	.d_open = ldopen,
	.d_close = ldclose,
	.d_read = ldread,
	.d_write = ldwrite,
	.d_ioctl = ldioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = lddiscard,
	.d_flag = D_DISK | D_MPSAFE
};

static const struct	dkdriver lddkdriver = {
	.d_open = ldopen,
	.d_close = ldclose,
	.d_strategy = ldstrategy,
	.d_iosize = ld_iosize,
	.d_minphys  = ldminphys,
	.d_diskstart = ld_diskstart,
	.d_dumpblocks = ld_dumpblocks,
	.d_lastclose = ld_lastclose,
	.d_discard = ld_discard
};

void
ldattach(struct ld_softc *sc, const char *default_strategy)
{
	device_t self = sc->sc_dv;
	struct dk_softc *dksc = &sc->sc_dksc;

	mutex_init(&sc->sc_mutex, MUTEX_DEFAULT, IPL_VM);
	cv_init(&sc->sc_drain, "lddrain");

	if ((sc->sc_flags & LDF_ENABLED) == 0) {
		return;
	}

	/* don't attach a disk that we cannot handle */
	if (sc->sc_secsize < DEV_BSIZE) {
		sc->sc_flags &= ~LDF_ENABLED;
		return;
	}

	/* Initialise dk and disk structure. */
	dk_init(dksc, self, DKTYPE_LD);
	disk_init(&dksc->sc_dkdev, dksc->sc_xname, &lddkdriver);

	if (sc->sc_maxxfer > MAXPHYS)
		sc->sc_maxxfer = MAXPHYS;

	/* Build synthetic geometry if necessary. */
	if (sc->sc_nheads == 0 || sc->sc_nsectors == 0 ||
	    sc->sc_ncylinders == 0)
	    ld_fake_geometry(sc);

	sc->sc_disksize512 = sc->sc_secperunit * sc->sc_secsize / DEV_BSIZE;

	if (sc->sc_flags & LDF_NO_RND)
		dksc->sc_flags |= DKF_NO_RND;

	/* Attach dk and disk subsystems */
	dk_attach(dksc);
	disk_attach(&dksc->sc_dkdev);
	ld_set_geometry(sc);

	bufq_alloc(&dksc->sc_bufq, default_strategy, BUFQ_SORT_RAWBLOCK);

	/* Register with PMF */
	if (!pmf_device_register1(dksc->sc_dev, ld_suspend, ld_resume,
		ld_shutdown))
		aprint_error_dev(dksc->sc_dev,
		    "couldn't establish power handler\n");

	/* Discover wedges on this disk. */
	config_interrupts(sc->sc_dv, ld_config_interrupts);
}

int
ldadjqparam(struct ld_softc *sc, int xmax)
{

	mutex_enter(&sc->sc_mutex);
	sc->sc_maxqueuecnt = xmax;
	mutex_exit(&sc->sc_mutex);

	return (0);
}

int
ldbegindetach(struct ld_softc *sc, int flags)
{
	struct dk_softc *dksc = &sc->sc_dksc;
	int error;

	/* If we never attached properly, no problem with detaching.  */
	if ((sc->sc_flags & LDF_ENABLED) == 0)
		return 0;

	/*
	 * If the disk is still open, back out before we commit to
	 * detaching.
	 */
	error = disk_begindetach(&dksc->sc_dkdev, ld_lastclose, dksc->sc_dev,
	    flags);
	if (error)
		return error;

	/* We are now committed to detaching.  Prevent new xfers.  */
	ldadjqparam(sc, 0);

	return 0;
}

void
ldenddetach(struct ld_softc *sc)
{
	struct dk_softc *dksc = &sc->sc_dksc;
	int bmaj, cmaj, i, mn;

	if ((sc->sc_flags & LDF_ENABLED) == 0)
		return;

	/* Wait for commands queued with the hardware to complete. */
	mutex_enter(&sc->sc_mutex);
	while (sc->sc_queuecnt > 0) {
		if (cv_timedwait(&sc->sc_drain, &sc->sc_mutex, 30 * hz)) {
			/*
			 * XXX This seems like a recipe for crashing on
			 * use after free...
			 */
			printf("%s: not drained\n", dksc->sc_xname);
			break;
		}
	}
	mutex_exit(&sc->sc_mutex);

	/* Kill off any queued buffers. */
	dk_drain(dksc);
	bufq_free(dksc->sc_bufq);

	/* Locate the major numbers. */
	bmaj = bdevsw_lookup_major(&ld_bdevsw);
	cmaj = cdevsw_lookup_major(&ld_cdevsw);

	/* Nuke the vnodes for any open instances. */
	for (i = 0; i < MAXPARTITIONS; i++) {
		mn = DISKMINOR(device_unit(dksc->sc_dev), i);
		vdevgone(bmaj, mn, mn, VBLK);
		vdevgone(cmaj, mn, mn, VCHR);
	}

	/* Delete all of our wedges. */
	dkwedge_delall(&dksc->sc_dkdev);

	/* Detach from the disk list. */
	disk_detach(&dksc->sc_dkdev);
	disk_destroy(&dksc->sc_dkdev);

	dk_detach(dksc);

	/* Deregister with PMF */
	pmf_device_deregister(dksc->sc_dev);

	/*
	 * XXX We can't really flush the cache here, because the
	 * XXX device may already be non-existent from the controller's
	 * XXX perspective.
	 */
#if 0
	ld_flush(dksc->sc_dev, false);
#endif
	cv_destroy(&sc->sc_drain);
	mutex_destroy(&sc->sc_mutex);
}

/* ARGSUSED */
static bool
ld_suspend(device_t dev, const pmf_qual_t *qual)
{
	struct ld_softc *sc = device_private(dev);
	int queuecnt;
	bool ok = false;

	/* Block new requests and wait for outstanding requests to drain.  */
	mutex_enter(&sc->sc_mutex);
	KASSERT((sc->sc_flags & LDF_SUSPEND) == 0);
	sc->sc_flags |= LDF_SUSPEND;
	while ((queuecnt = sc->sc_queuecnt) > 0) {
		if (cv_timedwait(&sc->sc_drain, &sc->sc_mutex, 30 * hz))
			break;
	}
	mutex_exit(&sc->sc_mutex);

	/* Block suspend if we couldn't drain everything in 30sec.  */
	if (queuecnt > 0) {
		device_printf(dev, "timeout draining buffers\n");
		goto out;
	}

	/* Flush cache before we lose power.  If we can't, block suspend.  */
	if (ld_flush(dev, /*poll*/false) != 0) {
		device_printf(dev, "failed to flush cache\n");
		goto out;
	}

	/* Success!  */
	ok = true;

out:	if (!ok)
		(void)ld_resume(dev, qual);
	return ok;
}

static bool
ld_resume(device_t dev, const pmf_qual_t *qual)
{
	struct ld_softc *sc = device_private(dev);

	/* Allow new requests to come in.  */
	mutex_enter(&sc->sc_mutex);
	KASSERT(sc->sc_flags & LDF_SUSPEND);
	sc->sc_flags &= ~LDF_SUSPEND;
	mutex_exit(&sc->sc_mutex);

	/* Restart any pending queued requests.  */
	dk_start(&sc->sc_dksc, NULL);

	return true;
}

/* ARGSUSED */
static bool
ld_shutdown(device_t dev, int flags)
{
	if ((flags & RB_NOSYNC) == 0 && ld_flush(dev, true) != 0)
		return false;

	return true;
}

/* ARGSUSED */
static int
ldopen(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit;

	unit = DISKUNIT(dev);
	if ((sc = device_lookup_private(&ld_cd, unit)) == NULL)
		return (ENXIO);

	if ((sc->sc_flags & LDF_ENABLED) == 0)
		return (ENODEV);

	dksc = &sc->sc_dksc;

	return dk_open(dksc, dev, flags, fmt, l);
}

static int
ld_lastclose(device_t self)
{
	ld_flush(self, false);

	return 0;
}

/* ARGSUSED */
static int
ldclose(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit;

	unit = DISKUNIT(dev);
	sc = device_lookup_private(&ld_cd, unit);
	dksc = &sc->sc_dksc;

	return dk_close(dksc, dev, flags, fmt, l);
}

/* ARGSUSED */
static int
ldread(dev_t dev, struct uio *uio, int ioflag)
{

	return (physio(ldstrategy, NULL, dev, B_READ, ldminphys, uio));
}

/* ARGSUSED */
static int
ldwrite(dev_t dev, struct uio *uio, int ioflag)
{

	return (physio(ldstrategy, NULL, dev, B_WRITE, ldminphys, uio));
}

/* ARGSUSED */
static int
ldioctl(dev_t dev, u_long cmd, void *addr, int32_t flag, struct lwp *l)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit, error;

	unit = DISKUNIT(dev);
	sc = device_lookup_private(&ld_cd, unit);
	dksc = &sc->sc_dksc;

	error = 0;

	/*
	 * Some common checks so that individual attachments wouldn't need
	 * to duplicate them.
	 */
	switch (cmd) {
	case DIOCCACHESYNC:
		/*
		 * XXX Do we really need to care about having a writable
		 * file descriptor here?
		 */
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else
			error = 0;
		break;
	}

	if (error != 0)
		return (error);

	if (sc->sc_ioctl) {
		if ((sc->sc_flags & LDF_MPSAFE) == 0)
			KERNEL_LOCK(1, curlwp);
		error = (*sc->sc_ioctl)(sc, cmd, addr, flag, 0);
		if ((sc->sc_flags & LDF_MPSAFE) == 0)
			KERNEL_UNLOCK_ONE(curlwp);
		if (error != EPASSTHROUGH)
			return (error);
	}

	/* something not handled by the attachment */
	return dk_ioctl(dksc, dev, cmd, addr, flag, l);
}

/*
 * Flush the device's cache.
 */
static int
ld_flush(device_t self, bool poll)
{
	int error = 0;
	struct ld_softc *sc = device_private(self);

	if (sc->sc_ioctl) {
		if ((sc->sc_flags & LDF_MPSAFE) == 0)
			KERNEL_LOCK(1, curlwp);
		error = (*sc->sc_ioctl)(sc, DIOCCACHESYNC, NULL, 0, poll);
		if ((sc->sc_flags & LDF_MPSAFE) == 0)
			KERNEL_UNLOCK_ONE(curlwp);
		if (error != 0)
			device_printf(self, "unable to flush cache\n");
	}

	return error;
}

static void
ldstrategy(struct buf *bp)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit;

	unit = DISKUNIT(bp->b_dev);
	sc = device_lookup_private(&ld_cd, unit);
	dksc = &sc->sc_dksc;

	dk_strategy(dksc, bp);
}

static int
ld_diskstart(device_t dev, struct buf *bp)
{
	struct ld_softc *sc = device_private(dev);
	int error;

	if (sc->sc_queuecnt >= sc->sc_maxqueuecnt ||
	    sc->sc_flags & LDF_SUSPEND) {
		if (sc->sc_flags & LDF_SUSPEND)
			aprint_debug_dev(dev, "i/o blocked while suspended\n");
		return EAGAIN;
	}

	if ((sc->sc_flags & LDF_MPSAFE) == 0)
		KERNEL_LOCK(1, curlwp);

	mutex_enter(&sc->sc_mutex);

	if (sc->sc_queuecnt >= sc->sc_maxqueuecnt ||
	    sc->sc_flags & LDF_SUSPEND) {
		if (sc->sc_flags & LDF_SUSPEND)
			aprint_debug_dev(dev, "i/o blocked while suspended\n");
		error = EAGAIN;
	} else {
		error = (*sc->sc_start)(sc, bp);
		if (error == 0)
			sc->sc_queuecnt++;
	}

	mutex_exit(&sc->sc_mutex);

	if ((sc->sc_flags & LDF_MPSAFE) == 0)
		KERNEL_UNLOCK_ONE(curlwp);

	return error;
}

void
lddone(struct ld_softc *sc, struct buf *bp)
{
	struct dk_softc *dksc = &sc->sc_dksc;

	dk_done(dksc, bp);

	mutex_enter(&sc->sc_mutex);
	if (--sc->sc_queuecnt <= sc->sc_maxqueuecnt) {
		cv_broadcast(&sc->sc_drain);
		mutex_exit(&sc->sc_mutex);
		dk_start(dksc, NULL);
	} else
		mutex_exit(&sc->sc_mutex);
}

static int
ldsize(dev_t dev)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit;

	unit = DISKUNIT(dev);
	if ((sc = device_lookup_private(&ld_cd, unit)) == NULL)
		return (-1);
	dksc = &sc->sc_dksc;

	if ((sc->sc_flags & LDF_ENABLED) == 0)
		return (-1);

	return dk_size(dksc, dev);
}

/*
 * Take a dump.
 */
static int
lddump(dev_t dev, daddr_t blkno, void *va, size_t size)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit;

	unit = DISKUNIT(dev);
	if ((sc = device_lookup_private(&ld_cd, unit)) == NULL)
		return (ENXIO);
	dksc = &sc->sc_dksc;

	if ((sc->sc_flags & LDF_ENABLED) == 0)
		return (ENODEV);

	return dk_dump(dksc, dev, blkno, va, size, 0);
}

static int
ld_dumpblocks(device_t dev, void *va, daddr_t blkno, int nblk)
{
	struct ld_softc *sc = device_private(dev);

	if (sc->sc_dump == NULL)
		return (ENODEV);

	/*
	 * Minimum consistency check; sc_dump() should check
	 * device-dependent constraints if necessary.
	 */
	if (blkno < 0)
		return (EIO);

	return (*sc->sc_dump)(sc, va, blkno, nblk);
}

/*
 * Adjust the size of a transfer.
 */
static void
ldminphys(struct buf *bp)
{
	int unit;
	struct ld_softc *sc;

	unit = DISKUNIT(bp->b_dev);
	sc = device_lookup_private(&ld_cd, unit);

	ld_iosize(sc->sc_dv, &bp->b_bcount);
	minphys(bp);
}

static void
ld_iosize(device_t d, int *countp)
{
	struct ld_softc *sc = device_private(d);

	if (*countp > sc->sc_maxxfer)
		*countp = sc->sc_maxxfer;
}

static void
ld_fake_geometry(struct ld_softc *sc)
{
	uint64_t ncyl;

	if (sc->sc_secperunit <= 528 * 2048)		/* 528MB */
		sc->sc_nheads = 16;
	else if (sc->sc_secperunit <= 1024 * 2048)	/* 1GB */
		sc->sc_nheads = 32;
	else if (sc->sc_secperunit <= 21504 * 2048)	/* 21GB */
		sc->sc_nheads = 64;
	else if (sc->sc_secperunit <= 43008 * 2048)	/* 42GB */
		sc->sc_nheads = 128;
	else
		sc->sc_nheads = 255;

	sc->sc_nsectors = 63;
	sc->sc_ncylinders = INT_MAX;
	ncyl = sc->sc_secperunit /
	    (sc->sc_nheads * sc->sc_nsectors);
	if (ncyl < INT_MAX)
		sc->sc_ncylinders = (int)ncyl;
}

static void
ld_set_geometry(struct ld_softc *sc)
{
	struct dk_softc *dksc = &sc->sc_dksc;
	struct disk_geom *dg = &dksc->sc_dkdev.dk_geom;
	char tbuf[9];

	format_bytes(tbuf, sizeof(tbuf), sc->sc_secperunit *
	    sc->sc_secsize);
	aprint_normal_dev(dksc->sc_dev, "%s, %d cyl, %d head, %d sec, "
	    "%d bytes/sect x %"PRIu64" sectors",
	    tbuf, sc->sc_ncylinders, sc->sc_nheads,
	    sc->sc_nsectors, sc->sc_secsize, sc->sc_secperunit);
	if (sc->sc_physsecsize != sc->sc_secsize) {
		aprint_normal(" (%d bytes/physsect", sc->sc_physsecsize);
		if (sc->sc_alignedsec != 0)
			aprint_normal("; first aligned sector %u",
			    sc->sc_alignedsec);
		aprint_normal(")");
	}
	aprint_normal("\n");

	memset(dg, 0, sizeof(*dg));
	dg->dg_secperunit = sc->sc_secperunit;
	dg->dg_secsize = sc->sc_secsize;
	dg->dg_nsectors = sc->sc_nsectors;
	dg->dg_ntracks = sc->sc_nheads;
	dg->dg_ncylinders = sc->sc_ncylinders;
	dg->dg_physsecsize = sc->sc_physsecsize;
	dg->dg_alignedsec = sc->sc_alignedsec;

	disk_set_info(dksc->sc_dev, &dksc->sc_dkdev, sc->sc_typename);
}

static void
ld_config_interrupts(device_t d)
{
	struct ld_softc *sc = device_private(d);
	struct dk_softc *dksc = &sc->sc_dksc;

	dkwedge_discover(&dksc->sc_dkdev);
}

static int
ld_discard(device_t dev, off_t pos, off_t len)
{
	struct ld_softc *sc = device_private(dev);
	struct buf dbuf, *bp = &dbuf;
	int error = 0;

	KASSERT(len <= INT_MAX);

	if (sc->sc_discard == NULL)
		return (ENODEV);

	if ((sc->sc_flags & LDF_MPSAFE) == 0)
		KERNEL_LOCK(1, curlwp);

	buf_init(bp);
	bp->b_vp = NULL;
	bp->b_data = NULL;
	bp->b_bufsize = 0;
	bp->b_rawblkno = pos / sc->sc_secsize;
	bp->b_bcount = len;
	bp->b_flags = B_WRITE;
	bp->b_cflags = BC_BUSY;

	error = (*sc->sc_discard)(sc, bp);
	if (error == 0)
		error = biowait(bp);

	buf_destroy(bp);

	if ((sc->sc_flags & LDF_MPSAFE) == 0)
		KERNEL_UNLOCK_ONE(curlwp);

	return error;
}

void
lddiscardend(struct ld_softc *sc, struct buf *bp)
{

	if (bp->b_error)
		bp->b_resid = bp->b_bcount;
	biodone(bp);
}

static int
lddiscard(dev_t dev, off_t pos, off_t len)
{
	struct ld_softc *sc;
	struct dk_softc *dksc;
	int unit;

	unit = DISKUNIT(dev);
	sc = device_lookup_private(&ld_cd, unit);
	dksc = &sc->sc_dksc;

	return dk_discard(dksc, dev, pos, len);
}

MODULE(MODULE_CLASS_DRIVER, ld, "dk_subr");

#ifdef _MODULE
CFDRIVER_DECL(ld, DV_DISK, NULL);
#endif

static int
ld_modcmd(modcmd_t cmd, void *opaque)
{
#ifdef _MODULE
	devmajor_t bmajor, cmajor;
#endif
	int error = 0;

#ifdef _MODULE
	switch (cmd) {
	case MODULE_CMD_INIT:
		bmajor = cmajor = -1;
		error = devsw_attach(ld_cd.cd_name, &ld_bdevsw, &bmajor,
		    &ld_cdevsw, &cmajor);
		if (error)
			break;
		error = config_cfdriver_attach(&ld_cd);
		break;
	case MODULE_CMD_FINI:
		error = config_cfdriver_detach(&ld_cd);
		if (error)
			break;
		devsw_detach(&ld_bdevsw, &ld_cdevsw);
		break;
	default:
		error = ENOTTY;
		break;
	}
#endif

	return error;
}
