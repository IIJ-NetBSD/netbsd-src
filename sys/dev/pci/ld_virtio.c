/*	$NetBSD: ld_virtio.c,v 1.44 2025/07/05 11:41:13 mlelstv Exp $	*/

/*
 * Copyright (c) 2010 Minoura Makoto.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ld_virtio.c,v 1.44 2025/07/05 11:41:13 mlelstv Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/kmem.h>

#include <dev/ldvar.h>
#include <dev/pci/virtioreg.h>
#include <dev/pci/virtiovar.h>

#include "ioconf.h"

/*
 * ld_virtioreg:
 */
/* Configuration registers */
#define VIRTIO_BLK_CONFIG_CAPACITY	0 /* 64bit */
#define VIRTIO_BLK_CONFIG_SIZE_MAX	8 /* 32bit */
#define VIRTIO_BLK_CONFIG_SEG_MAX	12 /* 32bit */
#define VIRTIO_BLK_CONFIG_GEOMETRY_C	16 /* 16bit */
#define VIRTIO_BLK_CONFIG_GEOMETRY_H	18 /* 8bit */
#define VIRTIO_BLK_CONFIG_GEOMETRY_S	19 /* 8bit */
#define VIRTIO_BLK_CONFIG_BLK_SIZE	20 /* 32bit */
#define VIRTIO_BLK_CONFIG_PHYSICAL_BLOCK_EXP		24 /* 8bit */
#define VIRTIO_BLK_CONFIG_ALIGNMENT_OFFSET		25 /* 8bit */
#define VIRTIO_BLK_CONFIG_MIN_IO_SIZE			26 /* 16bit */
#define VIRTIO_BLK_CONFIG_OPT_IO_SIZE			28 /* 32bit */
#define VIRTIO_BLK_CONFIG_WRITEBACK	32 /* 8bit */
#define VIRTIO_BLK_CONFIG_NUM_QUEUES			34 /* 16bit */
#define VIRTIO_BLK_CONFIG_MAX_DISCARD_SECTORS		36 /* 32bit */
#define VIRTIO_BLK_CONFIG_MAX_DISCARD_SEG		40 /* 32bit */
#define VIRTIO_BLK_CONFIG_DISCARD_SECTOR_ALIGNMENT	44 /* 32bit */

/* Feature bits */
#define VIRTIO_BLK_F_BARRIER	(1<<0)
#define VIRTIO_BLK_F_SIZE_MAX	(1<<1)
#define VIRTIO_BLK_F_SEG_MAX	(1<<2)
#define VIRTIO_BLK_F_GEOMETRY	(1<<4)
#define VIRTIO_BLK_F_RO		(1<<5)
#define VIRTIO_BLK_F_BLK_SIZE	(1<<6)
#define VIRTIO_BLK_F_SCSI	(1<<7)
#define VIRTIO_BLK_F_FLUSH	(1<<9)
#define VIRTIO_BLK_F_TOPOLOGY	(1<<10)
#define VIRTIO_BLK_F_CONFIG_WCE	(1<<11)
#define VIRTIO_BLK_F_MQ			(1<<12)
#define VIRTIO_BLK_F_DISCARD		(1<<13)
#define VIRTIO_BLK_F_WRITE_ZEROES	(1<<14)
#define VIRTIO_BLK_F_LIFETIME		(1<<15)
#define VIRTIO_BLK_F_SECURE_ERASE	(1<<16)

/*
 * Each block request uses at least two segments - one for the header
 * and one for the status.
*/
#define	VIRTIO_BLK_CTRL_SEGMENTS	2

#define VIRTIO_BLK_FLAG_BITS			\
	VIRTIO_COMMON_FLAG_BITS			\
	"b\x10" "SECURE_ERASE\0"		\
	"b\x0f" "LIFETIME\0"			\
	"b\x0e" "WRITE_ZEROES\0"		\
	"b\x0d" "DISCARD\0"			\
	"b\x0c" "MQ\0"				\
	"b\x0b" "CONFIG_WCE\0"			\
	"b\x0a" "TOPOLOGY\0"			\
	"b\x09" "FLUSH\0"			\
	"b\x07" "SCSI\0"			\
	"b\x06" "BLK_SIZE\0"			\
	"b\x05" "RO\0"				\
	"b\x04" "GEOMETRY\0"			\
	"b\x02" "SEG_MAX\0"			\
	"b\x01" "SIZE_MAX\0"			\
	"b\x00" "BARRIER\0"

/* Command */
#define VIRTIO_BLK_T_IN		0
#define VIRTIO_BLK_T_OUT	1
#define VIRTIO_BLK_T_FLUSH	4
#define VIRTIO_BLK_T_GET_ID		8
#define VIRTIO_BLK_T_GET_LIFETIME	10
#define VIRTIO_BLK_T_DISCARD		11
#define VIRTIO_BLK_T_WRITE_ZEROES	13
#define VIRTIO_BLK_T_SECURE_ERASE	14
#define VIRTIO_BLK_T_BARRIER	0x80000000

/* Sector */
#define VIRTIO_BLK_BSIZE	512

/* Status */
#define VIRTIO_BLK_S_OK		0
#define VIRTIO_BLK_S_IOERR	1
#define VIRTIO_BLK_S_UNSUPP	2

/* Request header structure */
struct virtio_blk_req_hdr {
	uint32_t	type;	/* VIRTIO_BLK_T_* */
	uint32_t	ioprio;
	uint64_t	sector;
} __packed;
/* payload and 1 byte status follows */

struct virtio_blk_discard_write_zeroes {
	uint64_t	sector;
	uint32_t	num_sectors;
	union {
		uint32_t	flags;
		struct {
			uint32_t	unmap:1;
			uint32_t	reserved:31;
		};
	};
} __packed;

/*
 * ld_virtiovar:
 */
struct virtio_blk_req {
	struct virtio_blk_req_hdr	vr_hdr;
	uint8_t				vr_status;
	struct buf			*vr_bp;
#define DUMMY_VR_BP				((void *)1)
	bus_dmamap_t			vr_cmdsts;
	bus_dmamap_t			vr_payload;
	void *				vr_datap;
	size_t				vr_datas;
};

struct ld_virtio_softc {
	struct ld_softc		sc_ld;
	device_t		sc_dev;

	uint32_t		sc_seg_max; /* max number of segs in xfer */
	uint32_t		sc_size_max; /* max size of single seg */

	struct virtio_softc	*sc_virtio;
	struct virtqueue	sc_vq;

	struct virtio_blk_req	*sc_reqs;
	bus_dma_segment_t	sc_reqs_seg;

	int			sc_readonly;

	enum {
		SYNC_FREE, SYNC_BUSY, SYNC_DONE
	}			sc_sync_use;
	kcondvar_t		sc_sync_wait;
	kmutex_t		sc_sync_wait_lock;
	uint8_t			sc_sync_status;
	uint8_t			*sc_typename;

	uint32_t		sc_max_discard_sectors;
	uint32_t		sc_max_discard_seg;
#if 0
	uint32_t		sc_discard_sector_alignment;
#endif
};

static int	ld_virtio_match(device_t, cfdata_t, void *);
static void	ld_virtio_attach(device_t, device_t, void *);
static int	ld_virtio_detach(device_t, int);

CFATTACH_DECL_NEW(ld_virtio, sizeof(struct ld_virtio_softc),
    ld_virtio_match, ld_virtio_attach, ld_virtio_detach, NULL);

static int
ld_virtio_match(device_t parent, cfdata_t match, void *aux)
{
	struct virtio_attach_args *va = aux;

	if (va->sc_childdevid == VIRTIO_DEVICE_ID_BLOCK)
		return 1;

	return 0;
}

static int ld_virtio_vq_done(struct virtqueue *);
static int ld_virtio_dump(struct ld_softc *, void *, daddr_t, int);
static int ld_virtio_start(struct ld_softc *, struct buf *);
static int ld_virtio_ioctl(struct ld_softc *, u_long, void *, int32_t, bool);
static int ld_virtio_info(struct ld_softc *, bool);
static int ld_virtio_discard(struct ld_softc *, struct buf *);

static int
ld_virtio_alloc_reqs(struct ld_virtio_softc *sc, int qsize)
{
	int allocsize, r, rsegs, i;
	struct ld_softc *ld = &sc->sc_ld;
	void *vaddr;

	allocsize = sizeof(struct virtio_blk_req) * qsize;
	r = bus_dmamem_alloc(virtio_dmat(sc->sc_virtio), allocsize, 0, 0,
			     &sc->sc_reqs_seg, 1, &rsegs, BUS_DMA_WAITOK);
	if (r != 0) {
		aprint_error_dev(sc->sc_dev,
				 "DMA memory allocation failed, size %d, "
				 "error code %d\n", allocsize, r);
		goto err_none;
	}
	r = bus_dmamem_map(virtio_dmat(sc->sc_virtio),
			   &sc->sc_reqs_seg, 1, allocsize,
			   &vaddr, BUS_DMA_WAITOK);
	if (r != 0) {
		aprint_error_dev(sc->sc_dev,
				 "DMA memory map failed, "
				 "error code %d\n", r);
		goto err_dmamem_alloc;
	}
	sc->sc_reqs = vaddr;
	memset(vaddr, 0, allocsize);
	for (i = 0; i < qsize; i++) {
		struct virtio_blk_req *vr = &sc->sc_reqs[i];
		r = bus_dmamap_create(virtio_dmat(sc->sc_virtio),
				      offsetof(struct virtio_blk_req, vr_bp),
				      1,
				      offsetof(struct virtio_blk_req, vr_bp),
				      0,
				      BUS_DMA_WAITOK|BUS_DMA_ALLOCNOW,
				      &vr->vr_cmdsts);
		if (r != 0) {
			aprint_error_dev(sc->sc_dev,
					 "command dmamap creation failed, "
					 "error code %d\n", r);
			goto err_reqs;
		}
		r = bus_dmamap_load(virtio_dmat(sc->sc_virtio), vr->vr_cmdsts,
				    &vr->vr_hdr,
				    offsetof(struct virtio_blk_req, vr_bp),
				    NULL, BUS_DMA_WAITOK);
		if (r != 0) {
			aprint_error_dev(sc->sc_dev,
					 "command dmamap load failed, "
					 "error code %d\n", r);
			goto err_reqs;
		}
		r = bus_dmamap_create(virtio_dmat(sc->sc_virtio),
				      /*size*/ld->sc_maxxfer,
				      /*nseg*/sc->sc_seg_max,
				      /*maxsegsz*/sc->sc_size_max,
				      /*boundary*/0,
				      BUS_DMA_WAITOK|BUS_DMA_ALLOCNOW,
				      &vr->vr_payload);
		if (r != 0) {
			aprint_error_dev(sc->sc_dev,
					 "payload dmamap creation failed, "
					 "error code %d\n", r);
			goto err_reqs;
		}
		vr->vr_datap = NULL;
		vr->vr_datas = 0;
	}
	return 0;

err_reqs:
	for (i = 0; i < qsize; i++) {
		struct virtio_blk_req *vr = &sc->sc_reqs[i];
		if (vr->vr_cmdsts) {
			bus_dmamap_destroy(virtio_dmat(sc->sc_virtio),
					   vr->vr_cmdsts);
			vr->vr_cmdsts = 0;
		}
		if (vr->vr_payload) {
			bus_dmamap_destroy(virtio_dmat(sc->sc_virtio),
					   vr->vr_payload);
			vr->vr_payload = 0;
		}
	}
	bus_dmamem_unmap(virtio_dmat(sc->sc_virtio), sc->sc_reqs, allocsize);
err_dmamem_alloc:
	bus_dmamem_free(virtio_dmat(sc->sc_virtio), &sc->sc_reqs_seg, 1);
err_none:
	return -1;
}

static void
ld_virtio_attach(device_t parent, device_t self, void *aux)
{
	struct ld_virtio_softc *sc = device_private(self);
	struct ld_softc *ld = &sc->sc_ld;
	struct virtio_softc *vsc = device_private(parent);
	uint64_t features;
	int qsize;

	if (virtio_child(vsc) != NULL) {
		aprint_normal(": child already attached for %s; "
			      "something wrong...\n", device_xname(parent));
		return;
	}

	sc->sc_dev = self;
	sc->sc_virtio = vsc;

	virtio_child_attach_start(vsc, self, IPL_BIO,
	    (VIRTIO_BLK_F_SIZE_MAX | VIRTIO_BLK_F_SEG_MAX |
	     VIRTIO_BLK_F_GEOMETRY | VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE |
	     VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_TOPOLOGY |
	     VIRTIO_BLK_F_CONFIG_WCE | VIRTIO_BLK_F_DISCARD),
	    VIRTIO_BLK_FLAG_BITS);

	features = virtio_features(vsc);
	if (features == 0)
		goto err;

	if (features & VIRTIO_BLK_F_RO)
		sc->sc_readonly = 1;
	else
		sc->sc_readonly = 0;

	if (features & VIRTIO_BLK_F_BLK_SIZE) {
		ld->sc_secsize = virtio_read_device_config_4(vsc,
					VIRTIO_BLK_CONFIG_BLK_SIZE);
	} else
		ld->sc_secsize = VIRTIO_BLK_BSIZE;

	if (features & VIRTIO_BLK_F_SEG_MAX) {
		sc->sc_seg_max = virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_SEG_MAX);
		if (sc->sc_seg_max == 0) {
			aprint_error_dev(sc->sc_dev,
			    "Invalid SEG_MAX %d\n", sc->sc_seg_max);
			goto err;
		}
	} else {
		sc->sc_seg_max = 1;
		aprint_verbose_dev(sc->sc_dev,
		    "Unknown SEG_MAX, assuming %"PRIu32"\n", sc->sc_seg_max);
	}

	/* At least genfs_io assumes size_max*seg_max >= MAXPHYS. */
	if (features & VIRTIO_BLK_F_SIZE_MAX) {
		sc->sc_size_max = virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_SIZE_MAX);
		if (sc->sc_size_max < MAXPHYS/sc->sc_seg_max) {
			aprint_error_dev(sc->sc_dev,
			    "Too small SIZE_MAX %d minimum is %d\n",
			    sc->sc_size_max, MAXPHYS/sc->sc_seg_max);
			// goto err;
			sc->sc_size_max = MAXPHYS/sc->sc_seg_max;
		} else if (sc->sc_size_max > MAXPHYS) {
			aprint_verbose_dev(sc->sc_dev,
			    "Clip SIZE_MAX from %d to %d\n",
			    sc->sc_size_max, MAXPHYS);
			sc->sc_size_max = MAXPHYS;
		}
	} else {
		sc->sc_size_max = MAXPHYS;
		aprint_verbose_dev(sc->sc_dev,
		    "Unknown SIZE_MAX, assuming %"PRIu32"\n",
		    sc->sc_size_max);
	}

	aprint_normal_dev(sc->sc_dev, "max %"PRIu32" segs"
	    " of max %"PRIu32" bytes\n",
	    sc->sc_seg_max, sc->sc_size_max);

	virtio_init_vq_vqdone(vsc, &sc->sc_vq, 0,
	    ld_virtio_vq_done);

	if (virtio_alloc_vq(vsc, &sc->sc_vq, sc->sc_size_max,
		sc->sc_seg_max + VIRTIO_BLK_CTRL_SEGMENTS, "I/O request") != 0)
		goto err;
	qsize = sc->sc_vq.vq_num;

	if (virtio_child_attach_finish(vsc, &sc->sc_vq, 1,
	    NULL, VIRTIO_F_INTR_MSIX) != 0)
		goto err;

	ld->sc_dv = self;
	ld->sc_secperunit = virtio_read_device_config_8(vsc,
	    VIRTIO_BLK_CONFIG_CAPACITY) / (ld->sc_secsize / VIRTIO_BLK_BSIZE);

	/*
	 * Clamp ld->sc_maxxfer to MAXPHYS before ld_virtio_alloc_reqs
	 * allocates DMA maps of at most ld->sc_maxxfer bytes.
	 * ldattach will also clamp to MAXPHYS, but not until after
	 * ld_virtio_alloc_reqs is done, so that doesn't help.
	 */
	ld->sc_maxxfer = MIN(MAXPHYS, sc->sc_size_max * sc->sc_seg_max);

	if (features & VIRTIO_BLK_F_GEOMETRY) {
		ld->sc_ncylinders = virtio_read_device_config_2(vsc,
					VIRTIO_BLK_CONFIG_GEOMETRY_C);
		ld->sc_nheads     = virtio_read_device_config_1(vsc,
					VIRTIO_BLK_CONFIG_GEOMETRY_H);
		ld->sc_nsectors   = virtio_read_device_config_1(vsc,
					VIRTIO_BLK_CONFIG_GEOMETRY_S);
	}
	if (features & VIRTIO_BLK_F_TOPOLOGY) {
		ld->sc_alignedsec = virtio_read_device_config_1(vsc,
		    VIRTIO_BLK_CONFIG_ALIGNMENT_OFFSET);
		ld->sc_physsecsize = ld->sc_secsize <<
		    virtio_read_device_config_1(vsc,
		    VIRTIO_BLK_CONFIG_PHYSICAL_BLOCK_EXP);
	}
	ld->sc_maxqueuecnt = qsize - 1; /* reserve slot for dumps, flushes */

	if (ld_virtio_alloc_reqs(sc, qsize) < 0)
		goto err;

	cv_init(&sc->sc_sync_wait, "vblksync");
	mutex_init(&sc->sc_sync_wait_lock, MUTEX_DEFAULT, IPL_BIO);
	sc->sc_sync_use = SYNC_FREE;

	ld->sc_dump = ld_virtio_dump;
	ld->sc_start = ld_virtio_start;
	ld->sc_ioctl = ld_virtio_ioctl;

	if (ld_virtio_info(ld, true) == 0)
		ld->sc_typename = sc->sc_typename;
	else
		ld->sc_typename = __UNCONST("Virtio Block Device");

	if (features & VIRTIO_BLK_F_DISCARD) {
		ld->sc_discard = ld_virtio_discard;
		sc->sc_max_discard_sectors = virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_MAX_DISCARD_SECTORS);
		sc->sc_max_discard_seg = virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_MAX_DISCARD_SEG);
#if 0
		sc->sc_discard_sector_alignment =
		    virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_DISCARD_SECTOR_ALIGNMENT);
#endif
	}

	ld->sc_flags = LDF_ENABLED | LDF_MPSAFE;
	ldattach(ld, BUFQ_DISK_DEFAULT_STRAT);

	return;

err:
	virtio_child_attach_failed(vsc);
	return;
}

static int __used
ld_virtio_info(struct ld_softc *ld, bool poll)
{
	struct ld_virtio_softc *sc = device_private(ld->sc_dv);
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq;
	struct virtio_blk_req *vr;
	int r;
	int slot;
	uint8_t *id_data; /* virtio v1.2 5.2.6 */
	size_t id_len = 20;
	bool unload = false;

	if (sc->sc_typename != NULL) {
		kmem_strfree(sc->sc_typename);
		sc->sc_typename = NULL;
	}

	id_data = kmem_alloc(id_len, KM_SLEEP);

	mutex_enter(&sc->sc_sync_wait_lock);
	while (sc->sc_sync_use != SYNC_FREE) {
		if (poll) {
			mutex_exit(&sc->sc_sync_wait_lock);
			ld_virtio_vq_done(vq);
			mutex_enter(&sc->sc_sync_wait_lock);
			continue;
		}
		cv_wait(&sc->sc_sync_wait, &sc->sc_sync_wait_lock);
	}
	sc->sc_sync_use = SYNC_BUSY;
	mutex_exit(&sc->sc_sync_wait_lock);

	r = virtio_enqueue_prep(vsc, vq, &slot);
	if (r != 0)
		goto done;

	vr = &sc->sc_reqs[slot];
	KASSERT(vr->vr_bp == NULL);

	r = bus_dmamap_load(virtio_dmat(vsc), vr->vr_payload,
			    id_data, id_len, NULL,
			    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (r != 0) {
		aprint_error_dev(sc->sc_dev,
		    "payload dmamap failed, error code %d\n", r);
		virtio_enqueue_abort(vsc, vq, slot);
		goto done;
	}
	unload = true;

	KASSERT(vr->vr_payload->dm_nsegs <= sc->sc_seg_max);
	r = virtio_enqueue_reserve(vsc, vq, slot, vr->vr_payload->dm_nsegs +
	    VIRTIO_BLK_CTRL_SEGMENTS);
	if (r != 0) {
		bus_dmamap_unload(virtio_dmat(vsc), vr->vr_payload);
		goto done;
	}

	vr->vr_bp = DUMMY_VR_BP;
	vr->vr_hdr.type   = virtio_rw32(vsc, VIRTIO_BLK_T_GET_ID);
	vr->vr_hdr.ioprio = virtio_rw32(vsc, 0);
	vr->vr_hdr.sector = virtio_rw64(vsc, 0);

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
			0, id_len,
			BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			offsetof(struct virtio_blk_req, vr_status),
			sizeof(uint8_t),
			BUS_DMASYNC_PREREAD);

	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 0, sizeof(struct virtio_blk_req_hdr),
			 true);
	virtio_enqueue(vsc, vq, slot, vr->vr_payload, false);
	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 offsetof(struct virtio_blk_req, vr_status),
			 sizeof(uint8_t),
	                 false);
	virtio_enqueue_commit(vsc, vq, slot, true);

done:
	mutex_enter(&sc->sc_sync_wait_lock);
	while (sc->sc_sync_use != SYNC_DONE) {
		if (poll) {
			mutex_exit(&sc->sc_sync_wait_lock);
			ld_virtio_vq_done(vq);
			mutex_enter(&sc->sc_sync_wait_lock);
			continue;
		}
		cv_wait(&sc->sc_sync_wait, &sc->sc_sync_wait_lock);
	}

	if (sc->sc_sync_status == VIRTIO_BLK_S_OK)
		r = 0;
	else
		r = EIO;

	sc->sc_sync_use = SYNC_FREE;
	cv_broadcast(&sc->sc_sync_wait);
	mutex_exit(&sc->sc_sync_wait_lock);

	if (unload) {
		bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
				0, id_len, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(virtio_dmat(vsc), vr->vr_payload);
	}

	if (r == 0) {
		if (id_data[0] == '\0')
			r = ENOENT;
		else
			sc->sc_typename = kmem_strndup(id_data, sizeof(id_data), KM_NOSLEEP);
	}

	kmem_free(id_data, id_len);

	return r;
}

static int
ld_virtio_start(struct ld_softc *ld, struct buf *bp)
{
	/* splbio */
	struct ld_virtio_softc *sc = device_private(ld->sc_dv);
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq;
	struct virtio_blk_req *vr;
	int r;
	int isread = (bp->b_flags & B_READ);
	int slot;

	if (sc->sc_readonly && !isread)
		return EIO;

	r = virtio_enqueue_prep(vsc, vq, &slot);
	if (r != 0)
		return r;

	vr = &sc->sc_reqs[slot];
	KASSERT(vr->vr_bp == NULL);

	r = bus_dmamap_load(virtio_dmat(vsc), vr->vr_payload,
			    bp->b_data, bp->b_bcount, NULL,
			    ((isread?BUS_DMA_READ:BUS_DMA_WRITE)
			     |BUS_DMA_NOWAIT));
	if (r != 0) {
		aprint_error_dev(sc->sc_dev,
		    "payload dmamap failed, error code %d\n", r);
		virtio_enqueue_abort(vsc, vq, slot);
		return r;
	}

	KASSERT(vr->vr_payload->dm_nsegs <= sc->sc_seg_max);
	r = virtio_enqueue_reserve(vsc, vq, slot, vr->vr_payload->dm_nsegs +
	    VIRTIO_BLK_CTRL_SEGMENTS);
	if (r != 0) {
		bus_dmamap_unload(virtio_dmat(vsc), vr->vr_payload);
		return r;
	}

	vr->vr_bp = bp;
	vr->vr_hdr.type   = virtio_rw32(vsc,
			isread ? VIRTIO_BLK_T_IN : VIRTIO_BLK_T_OUT);
	vr->vr_hdr.ioprio = virtio_rw32(vsc, 0);
	vr->vr_hdr.sector = virtio_rw64(vsc,
			bp->b_rawblkno * sc->sc_ld.sc_secsize /
			VIRTIO_BLK_BSIZE);

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
			0, bp->b_bcount,
			isread?BUS_DMASYNC_PREREAD:BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			offsetof(struct virtio_blk_req, vr_status),
			sizeof(uint8_t),
			BUS_DMASYNC_PREREAD);

	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 0, sizeof(struct virtio_blk_req_hdr),
			 true);
	virtio_enqueue(vsc, vq, slot, vr->vr_payload, !isread);
	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 offsetof(struct virtio_blk_req, vr_status),
			 sizeof(uint8_t),
			 false);
	virtio_enqueue_commit(vsc, vq, slot, true);

	return 0;
}

static void
ld_virtio_vq_done1(struct ld_virtio_softc *sc, struct virtio_softc *vsc,
		   struct virtqueue *vq, int slot)
{
	struct virtio_blk_req *vr = &sc->sc_reqs[slot];
	struct buf *bp = vr->vr_bp;
	const uint32_t rt = virtio_rw32(vsc, vr->vr_hdr.type);

	vr->vr_bp = NULL;

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			sizeof(struct virtio_blk_req_hdr), sizeof(uint8_t),
			BUS_DMASYNC_POSTREAD);
	if (bp == DUMMY_VR_BP) {
		mutex_enter(&sc->sc_sync_wait_lock);
		sc->sc_sync_status = vr->vr_status;
		sc->sc_sync_use = SYNC_DONE;
		cv_broadcast(&sc->sc_sync_wait);
		mutex_exit(&sc->sc_sync_wait_lock);
		virtio_dequeue_commit(vsc, vq, slot);
		return;
	}
	switch (rt) {
	case VIRTIO_BLK_T_OUT:
	case VIRTIO_BLK_T_IN:
		bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
				0, bp->b_bcount,
				(bp->b_flags & B_READ)?BUS_DMASYNC_POSTREAD
						      :BUS_DMASYNC_POSTWRITE);
		break;
	default:
		if (vr->vr_datap == NULL)
			break;
		bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
				0, vr->vr_datas, BUS_DMASYNC_POSTREAD |
				BUS_DMASYNC_POSTWRITE);
		break;
	}
	bus_dmamap_unload(virtio_dmat(vsc), vr->vr_payload);

	if (vr->vr_status != VIRTIO_BLK_S_OK) {
		bp->b_error = EIO;
		bp->b_resid = bp->b_bcount;
	} else {
		bp->b_error = 0;
		bp->b_resid = 0;
	}

	if (vr->vr_datap != NULL) {
		kmem_free(vr->vr_datap, vr->vr_datas);
		vr->vr_datap = NULL;
		vr->vr_datas = 0;
	}

	virtio_dequeue_commit(vsc, vq, slot);

	switch (rt) {
	case VIRTIO_BLK_T_OUT:
	case VIRTIO_BLK_T_IN:
		lddone(&sc->sc_ld, bp);
		break;
	case VIRTIO_BLK_T_DISCARD:
		lddiscardend(&sc->sc_ld, bp);
		break;
	}
}

static int
ld_virtio_vq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct ld_virtio_softc *sc = device_private(virtio_child(vsc));
	int r = 0;
	int slot;

again:
	if (virtio_dequeue(vsc, vq, &slot, NULL))
		return r;
	r = 1;

	ld_virtio_vq_done1(sc, vsc, vq, slot);
	goto again;
}

static int
ld_virtio_dump(struct ld_softc *ld, void *data, daddr_t blkno, int blkcnt)
{
	struct ld_virtio_softc *sc = device_private(ld->sc_dv);
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq;
	struct virtio_blk_req *vr;
	int slot, r;

	if (sc->sc_readonly)
		return EIO;

	r = virtio_enqueue_prep(vsc, vq, &slot);
	if (r != 0) {
		if (r == EAGAIN) { /* no free slot; dequeue first */
			delay(100);
			ld_virtio_vq_done(vq);
			r = virtio_enqueue_prep(vsc, vq, &slot);
			if (r != 0)
				return r;
		}
		return r;
	}
	vr = &sc->sc_reqs[slot];
	r = bus_dmamap_load(virtio_dmat(vsc), vr->vr_payload,
			    data, blkcnt*ld->sc_secsize, NULL,
			    BUS_DMA_WRITE|BUS_DMA_NOWAIT);
	if (r != 0)
		return r;

	r = virtio_enqueue_reserve(vsc, vq, slot, vr->vr_payload->dm_nsegs +
	    VIRTIO_BLK_CTRL_SEGMENTS);
	if (r != 0) {
		bus_dmamap_unload(virtio_dmat(vsc), vr->vr_payload);
		return r;
	}

	vr->vr_bp = (void*)0xdeadbeef;
	vr->vr_hdr.type   = virtio_rw32(vsc, VIRTIO_BLK_T_OUT);
	vr->vr_hdr.ioprio = virtio_rw32(vsc, 0);
	vr->vr_hdr.sector = virtio_rw64(vsc,
			blkno * ld->sc_secsize /
			VIRTIO_BLK_BSIZE);

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
			0, blkcnt*ld->sc_secsize,
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			offsetof(struct virtio_blk_req, vr_status),
			sizeof(uint8_t),
			BUS_DMASYNC_PREREAD);

	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 0, sizeof(struct virtio_blk_req_hdr),
			 true);
	virtio_enqueue(vsc, vq, slot, vr->vr_payload, true);
	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 offsetof(struct virtio_blk_req, vr_status),
			 sizeof(uint8_t),
			 false);
	virtio_enqueue_commit(vsc, vq, slot, true);

	for ( ; ; ) {
		int dslot;

		r = virtio_dequeue(vsc, vq, &dslot, NULL);
		if (r != 0)
			continue;
		if (dslot != slot) {
			ld_virtio_vq_done1(sc, vsc, vq, dslot);
			continue;
		} else
			break;
	}

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
			0, blkcnt*ld->sc_secsize,
			BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			offsetof(struct virtio_blk_req, vr_status),
			sizeof(uint8_t),
			BUS_DMASYNC_POSTREAD);
	if (vr->vr_status == VIRTIO_BLK_S_OK)
		r = 0;
	else
		r = EIO;
	virtio_dequeue_commit(vsc, vq, slot);

	return r;
}

static int
ld_virtio_detach(device_t self, int flags)
{
	struct ld_virtio_softc *sc = device_private(self);
	struct ld_softc *ld = &sc->sc_ld;
	bus_dma_tag_t dmat = virtio_dmat(sc->sc_virtio);
	int r, i, qsize;

	qsize = sc->sc_vq.vq_num;
	r = ldbegindetach(ld, flags);
	if (r != 0)
		return r;
	virtio_reset(sc->sc_virtio);
	virtio_free_vq(sc->sc_virtio, &sc->sc_vq);

	for (i = 0; i < qsize; i++) {
		bus_dmamap_destroy(dmat,
				   sc->sc_reqs[i].vr_cmdsts);
		bus_dmamap_destroy(dmat,
				   sc->sc_reqs[i].vr_payload);
	}
	bus_dmamem_unmap(dmat, sc->sc_reqs,
			 sizeof(struct virtio_blk_req) * qsize);
	bus_dmamem_free(dmat, &sc->sc_reqs_seg, 1);

	ldenddetach(ld);

	if (sc->sc_typename != NULL)
		kmem_strfree(sc->sc_typename);

	cv_destroy(&sc->sc_sync_wait);
	mutex_destroy(&sc->sc_sync_wait_lock);

	virtio_child_detach(sc->sc_virtio);

	return 0;
}

static int
ld_virtio_flush(struct ld_softc *ld, bool poll)
{
	struct ld_virtio_softc * const sc = device_private(ld->sc_dv);
	struct virtio_softc * const vsc = sc->sc_virtio;
	const uint64_t features = virtio_features(vsc);
	struct virtqueue *vq = &sc->sc_vq;
	struct virtio_blk_req *vr;
	int slot;
	int r;

	if ((features & VIRTIO_BLK_F_FLUSH) == 0)
		return 0;

	mutex_enter(&sc->sc_sync_wait_lock);
	while (sc->sc_sync_use != SYNC_FREE) {
		if (poll) {
			mutex_exit(&sc->sc_sync_wait_lock);
			ld_virtio_vq_done(vq);
			mutex_enter(&sc->sc_sync_wait_lock);
			continue;
		}
		cv_wait(&sc->sc_sync_wait, &sc->sc_sync_wait_lock);
	}
	sc->sc_sync_use = SYNC_BUSY;
	mutex_exit(&sc->sc_sync_wait_lock);

	r = virtio_enqueue_prep(vsc, vq, &slot);
	if (r != 0) {
		return r;
	}

	vr = &sc->sc_reqs[slot];
	KASSERT(vr->vr_bp == NULL);

	r = virtio_enqueue_reserve(vsc, vq, slot, VIRTIO_BLK_CTRL_SEGMENTS);
	if (r != 0) {
		return r;
	}

	vr->vr_bp = DUMMY_VR_BP;
	vr->vr_hdr.type   = virtio_rw32(vsc, VIRTIO_BLK_T_FLUSH);
	vr->vr_hdr.ioprio = virtio_rw32(vsc, 0);
	vr->vr_hdr.sector = virtio_rw64(vsc, 0);

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			offsetof(struct virtio_blk_req, vr_status),
			sizeof(uint8_t),
			BUS_DMASYNC_PREREAD);

	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 0, sizeof(struct virtio_blk_req_hdr),
			 true);
	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 offsetof(struct virtio_blk_req, vr_status),
			 sizeof(uint8_t),
			 false);
	virtio_enqueue_commit(vsc, vq, slot, true);

	mutex_enter(&sc->sc_sync_wait_lock);
	while (sc->sc_sync_use != SYNC_DONE) {
		if (poll) {
			mutex_exit(&sc->sc_sync_wait_lock);
			ld_virtio_vq_done(vq);
			mutex_enter(&sc->sc_sync_wait_lock);
			continue;
		}
		cv_wait(&sc->sc_sync_wait, &sc->sc_sync_wait_lock);
	}

	if (sc->sc_sync_status == VIRTIO_BLK_S_OK)
		r = 0;
	else
		r = EIO;

	sc->sc_sync_use = SYNC_FREE;
	cv_broadcast(&sc->sc_sync_wait);
	mutex_exit(&sc->sc_sync_wait_lock);

	return r;
}

static int
ld_virtio_getcache(struct ld_softc *ld, int *bitsp)
{
	struct ld_virtio_softc * const sc = device_private(ld->sc_dv);
	struct virtio_softc * const vsc = sc->sc_virtio;
	const uint64_t features = virtio_features(vsc);

	*bitsp = DKCACHE_READ;
	if ((features & VIRTIO_BLK_F_CONFIG_WCE) != 0)
		*bitsp |= DKCACHE_WCHANGE;
	if (virtio_read_device_config_1(vsc,
	    VIRTIO_BLK_CONFIG_WRITEBACK) != 0x00)
		*bitsp |= DKCACHE_WRITE;

	return 0;
}

static int
ld_virtio_setcache(struct ld_softc *ld, int bits)
{
	struct ld_virtio_softc * const sc = device_private(ld->sc_dv);
	struct virtio_softc * const vsc = sc->sc_virtio;
	const uint8_t wce = (bits & DKCACHE_WRITE) ? 0x01 : 0x00;

	virtio_write_device_config_1(vsc,
	    VIRTIO_BLK_CONFIG_WRITEBACK, wce);
	if (virtio_read_device_config_1(vsc,
	    VIRTIO_BLK_CONFIG_WRITEBACK) != wce)
		return EIO;

	return 0;
}

static int
ld_virtio_ioctl(struct ld_softc *ld, u_long cmd, void *addr, int32_t flag, bool poll)
{
	int error;

	switch (cmd) {
	case DIOCCACHESYNC:
		error = ld_virtio_flush(ld, poll);
		break;

	case DIOCGCACHE:
		error = ld_virtio_getcache(ld, (int *)addr);
		break;

	case DIOCSCACHE:
		error = ld_virtio_setcache(ld, *(int *)addr);
		break;

	default:
		error = EPASSTHROUGH;
		break;
	}

	return error;
}

static int
ld_virtio_discard(struct ld_softc *ld, struct buf *bp)
{
	struct ld_virtio_softc * const sc = device_private(ld->sc_dv);
	struct virtio_softc * const vsc = sc->sc_virtio;
	struct virtqueue * const vq = &sc->sc_vq;
	struct virtio_blk_req *vr;
	const uint64_t features = virtio_features(vsc);
	int r;
	int slot;
	uint64_t blkno;
	uint32_t nblks;
	struct virtio_blk_discard_write_zeroes * dwz;

	if ((features & VIRTIO_BLK_F_DISCARD) == 0 ||
	    sc->sc_max_discard_seg < 1)
		return EINVAL;

	if (sc->sc_readonly)
		return EIO;

	blkno = bp->b_rawblkno * sc->sc_ld.sc_secsize / VIRTIO_BLK_BSIZE;
	nblks = bp->b_bcount / VIRTIO_BLK_BSIZE;

	if (nblks > sc->sc_max_discard_sectors)
		return ERANGE;

	r = virtio_enqueue_prep(vsc, vq, &slot);
	if (r != 0) {
		return r;
	}

	vr = &sc->sc_reqs[slot];
	KASSERT(vr->vr_bp == NULL);

	dwz = kmem_alloc(sizeof(*dwz), KM_SLEEP);

	r = bus_dmamap_load(virtio_dmat(vsc), vr->vr_payload,
	    dwz, sizeof(*dwz), NULL, BUS_DMA_WRITE | BUS_DMA_NOWAIT);
	if (r != 0) {
		device_printf(sc->sc_dev,
		    "discard payload dmamap failed, error code %d\n", r);
		virtio_enqueue_abort(vsc, vq, slot);
		kmem_free(dwz, sizeof(*dwz));
		return r;
	}

	KASSERT(vr->vr_payload->dm_nsegs <= sc->sc_seg_max);
	r = virtio_enqueue_reserve(vsc, vq, slot, vr->vr_payload->dm_nsegs +
	    VIRTIO_BLK_CTRL_SEGMENTS);
	if (r != 0) {
		bus_dmamap_unload(virtio_dmat(vsc), vr->vr_payload);
		kmem_free(dwz, sizeof(*dwz));
		return r;
	}

	vr->vr_hdr.type = virtio_rw32(vsc, VIRTIO_BLK_T_DISCARD);
	vr->vr_hdr.ioprio = virtio_rw32(vsc, 0);
	vr->vr_hdr.sector = virtio_rw64(vsc, 0);
	vr->vr_bp = bp;

	KASSERT(vr->vr_datap == NULL);
	vr->vr_datap = dwz;
	vr->vr_datas = sizeof(*dwz);

	dwz->sector = virtio_rw64(vsc, blkno);
	dwz->num_sectors = virtio_rw32(vsc, nblks);
	dwz->flags = virtio_rw32(vsc, 0);

	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_payload,
			0, vr->vr_datas, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(virtio_dmat(vsc), vr->vr_cmdsts,
			offsetof(struct virtio_blk_req, vr_status),
			sizeof(uint8_t),
			BUS_DMASYNC_PREREAD);

	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 0, sizeof(struct virtio_blk_req_hdr),
			 true);
	virtio_enqueue(vsc, vq, slot, vr->vr_payload, true);
	virtio_enqueue_p(vsc, vq, slot, vr->vr_cmdsts,
			 offsetof(struct virtio_blk_req, vr_status),
			 sizeof(uint8_t),
			 false);
	virtio_enqueue_commit(vsc, vq, slot, true);

	return 0;
}

MODULE(MODULE_CLASS_DRIVER, ld_virtio, "ld,virtio");

static int
ld_virtio_modcmd(modcmd_t cmd, void *opaque)
{
	int error = 0;

	switch (cmd) {
	case MODULE_CMD_INIT:
#ifdef _MODULE
		error = config_init_component(cfdriver_ioconf_ld_virtio,
		    cfattach_ioconf_ld_virtio, cfdata_ioconf_ld_virtio);
#endif
		break;
	case MODULE_CMD_FINI:
#ifdef _MODULE
		error = config_fini_component(cfdriver_ioconf_ld_virtio,
		    cfattach_ioconf_ld_virtio, cfdata_ioconf_ld_virtio);
#endif
		break;
	default:
		error = ENOTTY;
		break;
	}

	return error;
}
