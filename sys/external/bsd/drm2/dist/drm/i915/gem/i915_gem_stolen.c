/*	$NetBSD: i915_gem_stolen.c,v 1.5.4.1 2024/02/03 11:15:12 martin Exp $	*/

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008-2012 Intel Corporation
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: i915_gem_stolen.c,v 1.5.4.1 2024/02/03 11:15:12 martin Exp $");

#include <linux/errno.h>
#include <linux/mutex.h>

#include <drm/drm_mm.h>
#include <drm/i915_drm.h>

#include "gem/i915_gem_region.h"
#include "i915_drv.h"
#include "i915_gem_stolen.h"

#include <linux/nbsd-namespace.h>

/*
 * The BIOS typically reserves some of the system's memory for the exclusive
 * use of the integrated graphics. This memory is no longer available for
 * use by the OS and so the user finds that his system has less memory
 * available than he put in. We refer to this memory as stolen.
 *
 * The BIOS will allocate its framebuffer from the stolen memory. Our
 * goal is try to reuse that object for our own fbcon which must always
 * be available for panics. Anything else we can reuse the stolen memory
 * for is a boon.
 */

int i915_gem_stolen_insert_node_in_range(struct drm_i915_private *i915,
					 struct drm_mm_node *node, u64 size,
					 unsigned alignment, u64 start, u64 end)
{
	int ret;

	if (!drm_mm_initialized(&i915->mm.stolen))
		return -ENODEV;

	/* WaSkipStolenMemoryFirstPage:bdw+ */
	if (INTEL_GEN(i915) >= 8 && start < 4096)
		start = 4096;

	mutex_lock(&i915->mm.stolen_lock);
	ret = drm_mm_insert_node_in_range(&i915->mm.stolen, node,
					  size, alignment, 0,
					  start, end, DRM_MM_INSERT_BEST);
	mutex_unlock(&i915->mm.stolen_lock);

	return ret;
}

int i915_gem_stolen_insert_node(struct drm_i915_private *i915,
				struct drm_mm_node *node, u64 size,
				unsigned alignment)
{
	return i915_gem_stolen_insert_node_in_range(i915, node, size,
						    alignment, 0, U64_MAX);
}

void i915_gem_stolen_remove_node(struct drm_i915_private *i915,
				 struct drm_mm_node *node)
{
	mutex_lock(&i915->mm.stolen_lock);
	drm_mm_remove_node(node);
	mutex_unlock(&i915->mm.stolen_lock);
}

static int i915_adjust_stolen(struct drm_i915_private *i915,
			      struct resource *dsm)
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;
	struct resource *r;

	if (dsm->start == 0 || dsm->end <= dsm->start)
		return -EINVAL;

	/*
	 * TODO: We have yet too encounter the case where the GTT wasn't at the
	 * end of stolen. With that assumption we could simplify this.
	 */

	/* Make sure we don't clobber the GTT if it's within stolen memory */
	if (INTEL_GEN(i915) <= 4 &&
	    !IS_G33(i915) && !IS_PINEVIEW(i915) && !IS_G4X(i915)) {
		struct resource stolen[2] = {*dsm, *dsm};
		struct resource ggtt_res;
		resource_size_t ggtt_start;

		ggtt_start = intel_uncore_read(uncore, PGTBL_CTL);
		if (IS_GEN(i915, 4))
			ggtt_start = (ggtt_start & PGTBL_ADDRESS_LO_MASK) |
				     (ggtt_start & PGTBL_ADDRESS_HI_MASK) << 28;
		else
			ggtt_start &= PGTBL_ADDRESS_LO_MASK;

		ggtt_res =
			(struct resource) DEFINE_RES_MEM(ggtt_start,
							 ggtt_total_entries(ggtt) * 4);

		if (ggtt_res.start >= stolen[0].start && ggtt_res.start < stolen[0].end)
			stolen[0].end = ggtt_res.start;
		if (ggtt_res.end > stolen[1].start && ggtt_res.end <= stolen[1].end)
			stolen[1].start = ggtt_res.end;

		/* Pick the larger of the two chunks */
		if (resource_size(&stolen[0]) > resource_size(&stolen[1]))
			*dsm = stolen[0];
		else
			*dsm = stolen[1];

		if (stolen[0].start != stolen[1].start ||
		    stolen[0].end != stolen[1].end) {
			DRM_DEBUG_DRIVER("GTT within stolen memory at %pR\n", &ggtt_res);
			DRM_DEBUG_DRIVER("Stolen memory adjusted to %pR\n", dsm);
		}
	}

#ifdef __NetBSD__		/* XXX */
	__USE(r);
#else
	/*
	 * Verify that nothing else uses this physical address. Stolen
	 * memory should be reserved by the BIOS and hidden from the
	 * kernel. So if the region is already marked as busy, something
	 * is seriously wrong.
	 */
	r = devm_request_mem_region(i915->drm.dev, dsm->start,
				    resource_size(dsm),
				    "Graphics Stolen Memory");
	if (r == NULL) {
		/*
		 * One more attempt but this time requesting region from
		 * start + 1, as we have seen that this resolves the region
		 * conflict with the PCI Bus.
		 * This is a BIOS w/a: Some BIOS wrap stolen in the root
		 * PCI bus, but have an off-by-one error. Hence retry the
		 * reservation starting from 1 instead of 0.
		 * There's also BIOS with off-by-one on the other end.
		 */
		r = devm_request_mem_region(i915->drm.dev, dsm->start + 1,
					    resource_size(dsm) - 2,
					    "Graphics Stolen Memory");
		/*
		 * GEN3 firmware likes to smash pci bridges into the stolen
		 * range. Apparently this works.
		 */
		if (!r && !IS_GEN(i915, 3)) {
			DRM_ERROR("conflict detected with stolen region: %pR\n",
				  dsm);

			return -EBUSY;
		}
	}
#endif

	return 0;
}

static void i915_gem_cleanup_stolen(struct drm_i915_private *i915)
{
	if (!drm_mm_initialized(&i915->mm.stolen))
		return;

	mutex_destroy(&i915->mm.stolen_lock);
	drm_mm_takedown(&i915->mm.stolen);
}

static void g4x_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore,
					IS_GM45(i915) ?
					CTG_STOLEN_RESERVED :
					ELK_STOLEN_RESERVED);
	resource_size_t stolen_top = i915->dsm.end + 1;

	DRM_DEBUG_DRIVER("%s_STOLEN_RESERVED = %08x\n",
			 IS_GM45(i915) ? "CTG" : "ELK", reg_val);

	if ((reg_val & G4X_STOLEN_RESERVED_ENABLE) == 0)
		return;

	/*
	 * Whether ILK really reuses the ELK register for this is unclear.
	 * Let's see if we catch anyone with this supposedly enabled on ILK.
	 */
	WARN(IS_GEN(i915, 5), "ILK stolen reserved found? 0x%08x\n",
	     reg_val);

	if (!(reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK))
		return;

	if (!(reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK))
		return;

	*base = (reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK) << 16;
	WARN_ON((reg_val & G4X_STOLEN_RESERVED_ADDR1_MASK) < *base);

	*size = stolen_top - *base;
}

static void gen6_get_stolen_reserved(struct drm_i915_private *i915,
				     struct intel_uncore *uncore,
				     resource_size_t *base,
				     resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN6_STOLEN_RESERVED_SIZE_MASK) {
	case GEN6_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN6_STOLEN_RESERVED_512K:
		*size = 512 * 1024;
		break;
	case GEN6_STOLEN_RESERVED_256K:
		*size = 256 * 1024;
		break;
	case GEN6_STOLEN_RESERVED_128K:
		*size = 128 * 1024;
		break;
	default:
		*size = 1024 * 1024;
		MISSING_CASE(reg_val & GEN6_STOLEN_RESERVED_SIZE_MASK);
	}
}

static void vlv_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);
	resource_size_t stolen_top = i915->dsm.end + 1;

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	switch (reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK) {
	default:
		MISSING_CASE(reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK);
		/* fall through */
	case GEN7_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	}

	/*
	 * On vlv, the ADDR_MASK portion is left as 0 and HW deduces the
	 * reserved location as (top - size).
	 */
	*base = stolen_top - *size;
}

static void gen7_get_stolen_reserved(struct drm_i915_private *i915,
				     struct intel_uncore *uncore,
				     resource_size_t *base,
				     resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	*base = reg_val & GEN7_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK) {
	case GEN7_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN7_STOLEN_RESERVED_256K:
		*size = 256 * 1024;
		break;
	default:
		*size = 1024 * 1024;
		MISSING_CASE(reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK);
	}
}

static void chv_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN8_STOLEN_RESERVED_SIZE_MASK) {
	case GEN8_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_2M:
		*size = 2 * 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_4M:
		*size = 4 * 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_8M:
		*size = 8 * 1024 * 1024;
		break;
	default:
		*size = 8 * 1024 * 1024;
		MISSING_CASE(reg_val & GEN8_STOLEN_RESERVED_SIZE_MASK);
	}
}

static void bdw_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);
	resource_size_t stolen_top = i915->dsm.end + 1;

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	if (!(reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK))
		return;

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;
	*size = stolen_top - *base;
}

static void icl_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u64 reg_val = intel_uncore_read64(uncore, GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = 0x%016"PRIx64"\n", reg_val);

	*base = reg_val & GEN11_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN8_STOLEN_RESERVED_SIZE_MASK) {
	case GEN8_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_2M:
		*size = 2 * 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_4M:
		*size = 4 * 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_8M:
		*size = 8 * 1024 * 1024;
		break;
	default:
		*size = 8 * 1024 * 1024;
		MISSING_CASE(reg_val & GEN8_STOLEN_RESERVED_SIZE_MASK);
	}
}

static int i915_gem_init_stolen(struct drm_i915_private *i915)
{
	struct intel_uncore *uncore = &i915->uncore;
	resource_size_t reserved_base, stolen_top;
	resource_size_t reserved_total, reserved_size;

	mutex_init(&i915->mm.stolen_lock);

	if (intel_vgpu_active(i915)) {
		dev_notice(i915->drm.dev,
			   "%s, disabling use of stolen memory\n",
			   "iGVT-g active");
		return 0;
	}

	if (intel_vtd_active() && INTEL_GEN(i915) < 8) {
		dev_notice(i915->drm.dev,
			   "%s, disabling use of stolen memory\n",
			   "DMAR active");
		return 0;
	}

	if (resource_size(&intel_graphics_stolen_res) == 0)
		return 0;

	i915->dsm = intel_graphics_stolen_res;

	if (i915_adjust_stolen(i915, &i915->dsm))
		return 0;

	GEM_BUG_ON(i915->dsm.start == 0);
	GEM_BUG_ON(i915->dsm.end <= i915->dsm.start);

	stolen_top = i915->dsm.end + 1;
	reserved_base = stolen_top;
	reserved_size = 0;

	switch (INTEL_GEN(i915)) {
	case 2:
	case 3:
		break;
	case 4:
		if (!IS_G4X(i915))
			break;
		/* fall through */
	case 5:
		g4x_get_stolen_reserved(i915, uncore,
					&reserved_base, &reserved_size);
		break;
	case 6:
		gen6_get_stolen_reserved(i915, uncore,
					 &reserved_base, &reserved_size);
		break;
	case 7:
		if (IS_VALLEYVIEW(i915))
			vlv_get_stolen_reserved(i915, uncore,
						&reserved_base, &reserved_size);
		else
			gen7_get_stolen_reserved(i915, uncore,
						 &reserved_base, &reserved_size);
		break;
	case 8:
	case 9:
	case 10:
		if (IS_LP(i915))
			chv_get_stolen_reserved(i915, uncore,
						&reserved_base, &reserved_size);
		else
			bdw_get_stolen_reserved(i915, uncore,
						&reserved_base, &reserved_size);
		break;
	default:
		MISSING_CASE(INTEL_GEN(i915));
		/* fall-through */
	case 11:
	case 12:
		icl_get_stolen_reserved(i915, uncore,
					&reserved_base,
					&reserved_size);
		break;
	}

	/*
	 * Our expectation is that the reserved space is at the top of the
	 * stolen region and *never* at the bottom. If we see !reserved_base,
	 * it likely means we failed to read the registers correctly.
	 */
	if (!reserved_base) {
		DRM_ERROR("inconsistent reservation %pa + %pa; ignoring\n",
			  &reserved_base, &reserved_size);
		reserved_base = stolen_top;
		reserved_size = 0;
	}

	i915->dsm_reserved =
		(struct resource)DEFINE_RES_MEM(reserved_base, reserved_size);

	if (!resource_contains(&i915->dsm, &i915->dsm_reserved)) {
		DRM_ERROR("Stolen reserved area %pR outside stolen memory %pR\n",
			  &i915->dsm_reserved, &i915->dsm);
		return 0;
	}

	/* It is possible for the reserved area to end before the end of stolen
	 * memory, so just consider the start. */
	reserved_total = stolen_top - reserved_base;

	DRM_DEBUG_DRIVER("Memory reserved for graphics device: %"PRIu64"K, usable: %"PRIu64"K\n",
			 (u64)resource_size(&i915->dsm) >> 10,
			 ((u64)resource_size(&i915->dsm) - reserved_total) >> 10);

	i915->stolen_usable_size =
		resource_size(&i915->dsm) - reserved_total;

	/* Basic memrange allocator for stolen space. */
	drm_mm_init(&i915->mm.stolen, 0, i915->stolen_usable_size);

	return 0;
}

static struct sg_table *
i915_pages_create_for_stolen(struct drm_device *dev,
			     resource_size_t offset, resource_size_t size)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct sg_table *st;
	struct scatterlist *sg;
#ifdef __NetBSD__
	bus_dma_tag_t dmat = i915->drm.dmat;
	bus_dma_segment_t *seg = NULL;
	int nseg = 0, i;
	bool loaded = false;
	int ret;
#endif

	GEM_BUG_ON(range_overflows(offset, size, resource_size(&i915->dsm)));

	/* We hide that we have no struct page backing our stolen object
	 * by wrapping the contiguous physical allocation with a fake
	 * dma mapping in a single scatterlist.
	 */

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return ERR_PTR(-ENOMEM);

#ifdef __NetBSD__
	KASSERT((size % PAGE_SIZE) == 0);
	nseg = size / PAGE_SIZE;
	seg = kmem_alloc(nseg * sizeof(seg[0]), KM_SLEEP);

	/*
	 * XXX x86 bus_dmamap_load_raw fails to respect the maxsegsz we
	 * pass to bus_dmamap_create, so we have to create page-sized
	 * segments to begin with.
	 */
	for (i = 0; i < nseg; i++) {
		seg[i].ds_addr = (bus_addr_t)i915->dsm.start + offset +
		    i*PAGE_SIZE;
		seg[i].ds_len = PAGE_SIZE;
	}

	sg = NULL;

	ret = sg_alloc_table_from_bus_dmamem(st, dmat, seg, nseg, GFP_KERNEL);
	if (ret) {
		DRM_ERROR("failed to alloc sg table for stolen object: %d\n",
		    ret);
		ret = -ENOMEM;
		goto out;
	}
	sg = st->sgl;

	/* XXX errno NetBSD->Linux */
	ret = -bus_dmamap_create(dmat, size, nseg, PAGE_SIZE, 0,
	    BUS_DMA_WAITOK, &st->sgl->sg_dmamap);
	if (ret) {
		DRM_ERROR("failed to create DMA map for stolen object: %d\n",
		    ret);
		st->sgl->sg_dmamap = NULL;
		goto out;
	}
	st->sgl->sg_dmat = dmat;

	/* XXX errno NetBSD->Liux */
	ret = -bus_dmamap_load_raw(dmat, st->sgl->sg_dmamap, seg, nseg, size,
	    BUS_DMA_WAITOK);
	if (ret) {
		DRM_ERROR("failed to load DMA map for stolen object: %d\n",
		    ret);
		goto out;
	}
	loaded = true;

out:	kmem_free(seg, nseg * sizeof(seg[0]));
	if (ret) {
		if (loaded)
			bus_dmamap_unload(dmat, st->sgl->sg_dmamap);
		if (sg && sg->sg_dmamap)
			bus_dmamap_destroy(dmat, sg->sg_dmamap);
		if (sg)
			sg_free_table(st);
		kfree(st);
		return ERR_PTR(ret);
	}
#else
	if (sg_alloc_table(st, 1, GFP_KERNEL)) {
		kfree(st);
		return ERR_PTR(-ENOMEM);
	}

	sg = st->sgl;
	sg->offset = 0;
	sg->length = size;

	sg_dma_address(sg) = (dma_addr_t)i915->dsm.start + offset;
	sg_dma_len(sg) = size;
#endif

	return st;
}

static int i915_gem_object_get_pages_stolen(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages =
		i915_pages_create_for_stolen(obj->base.dev,
					     obj->stolen->start,
					     obj->stolen->size);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	__i915_gem_object_set_pages(obj, pages, obj->stolen->size);

	return 0;
}

static void i915_gem_object_put_pages_stolen(struct drm_i915_gem_object *obj,
					     struct sg_table *pages)
{
	/* Should only be called from i915_gem_object_release_stolen() */
#ifdef __NetBSD__
	bus_dmamap_unload(obj->base.dev->dmat, pages->sgl->sg_dmamap);
#endif
	sg_free_table(pages);
	kfree(pages);
}

static void
i915_gem_object_release_stolen(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct drm_mm_node *stolen = fetch_and_zero(&obj->stolen);

	GEM_BUG_ON(!stolen);

	i915_gem_object_release_memory_region(obj);

	i915_gem_stolen_remove_node(i915, stolen);
	kfree(stolen);
}

static const struct drm_i915_gem_object_ops i915_gem_object_stolen_ops = {
	.get_pages = i915_gem_object_get_pages_stolen,
	.put_pages = i915_gem_object_put_pages_stolen,
	.release = i915_gem_object_release_stolen,
};

static struct drm_i915_gem_object *
__i915_gem_object_create_stolen(struct intel_memory_region *mem,
				struct drm_mm_node *stolen)
{
	static struct lock_class_key lock_class;
	struct drm_i915_gem_object *obj;
	unsigned int cache_level;
	int err = -ENOMEM;

	obj = i915_gem_object_alloc();
	if (!obj)
		goto err;

	drm_gem_private_object_init(&mem->i915->drm, &obj->base, stolen->size);
	i915_gem_object_init(obj, &i915_gem_object_stolen_ops, &lock_class);

	obj->stolen = stolen;
	obj->read_domains = I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT;
	cache_level = HAS_LLC(mem->i915) ? I915_CACHE_LLC : I915_CACHE_NONE;
	i915_gem_object_set_cache_coherency(obj, cache_level);

	err = i915_gem_object_pin_pages(obj);
	if (err)
		goto cleanup;

	i915_gem_object_init_memory_region(obj, mem, 0);

	return obj;

cleanup:
	i915_gem_object_free(obj);
err:
	return ERR_PTR(err);
}

static struct drm_i915_gem_object *
_i915_gem_object_create_stolen(struct intel_memory_region *mem,
			       resource_size_t size,
			       unsigned int flags)
{
	struct drm_i915_private *i915 = mem->i915;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;
	int ret;

	if (!drm_mm_initialized(&i915->mm.stolen))
		return ERR_PTR(-ENODEV);

	if (size == 0)
		return ERR_PTR(-EINVAL);

	stolen = kzalloc(sizeof(*stolen), GFP_KERNEL);
	if (!stolen)
		return ERR_PTR(-ENOMEM);

	ret = i915_gem_stolen_insert_node(i915, stolen, size, 4096);
	if (ret) {
		obj = ERR_PTR(ret);
		goto err_free;
	}

	obj = __i915_gem_object_create_stolen(mem, stolen);
	if (IS_ERR(obj))
		goto err_remove;

	return obj;

err_remove:
	i915_gem_stolen_remove_node(i915, stolen);
err_free:
	kfree(stolen);
	return obj;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *i915,
			      resource_size_t size)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_STOLEN],
					     size, I915_BO_ALLOC_CONTIGUOUS);
}

static int init_stolen(struct intel_memory_region *mem)
{
	intel_memory_region_set_name(mem, "stolen");

	/*
	 * Initialise stolen early so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	return i915_gem_init_stolen(mem->i915);
}

static void release_stolen(struct intel_memory_region *mem)
{
	i915_gem_cleanup_stolen(mem->i915);
}

static const struct intel_memory_region_ops i915_region_stolen_ops = {
	.init = init_stolen,
	.release = release_stolen,
	.create_object = _i915_gem_object_create_stolen,
};

struct intel_memory_region *i915_gem_stolen_setup(struct drm_i915_private *i915)
{
	return intel_memory_region_create(i915,
					  intel_graphics_stolen_res.start,
					  resource_size(&intel_graphics_stolen_res),
					  PAGE_SIZE, 0,
					  &i915_region_stolen_ops);
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen_for_preallocated(struct drm_i915_private *i915,
					       resource_size_t stolen_offset,
					       resource_size_t gtt_offset,
					       resource_size_t size)
{
	struct intel_memory_region *mem = i915->mm.regions[INTEL_REGION_STOLEN];
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;
	struct i915_vma *vma;
	int ret;

	if (!drm_mm_initialized(&i915->mm.stolen))
		return ERR_PTR(-ENODEV);

	DRM_DEBUG_DRIVER("creating preallocated stolen object: stolen_offset=%pa, gtt_offset=%pa, size=%pa\n",
			 &stolen_offset, &gtt_offset, &size);

	/* KISS and expect everything to be page-aligned */
	if (WARN_ON(size == 0) ||
	    WARN_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE)) ||
	    WARN_ON(!IS_ALIGNED(stolen_offset, I915_GTT_MIN_ALIGNMENT)))
		return ERR_PTR(-EINVAL);

	stolen = kzalloc(sizeof(*stolen), GFP_KERNEL);
	if (!stolen)
		return ERR_PTR(-ENOMEM);

	stolen->start = stolen_offset;
	stolen->size = size;
	mutex_lock(&i915->mm.stolen_lock);
	ret = drm_mm_reserve_node(&i915->mm.stolen, stolen);
	mutex_unlock(&i915->mm.stolen_lock);
	if (ret) {
		DRM_DEBUG_DRIVER("failed to allocate stolen space\n");
		kfree(stolen);
		return ERR_PTR(ret);
	}

	obj = __i915_gem_object_create_stolen(mem, stolen);
	if (IS_ERR(obj)) {
		DRM_DEBUG_DRIVER("failed to allocate stolen object\n");
		i915_gem_stolen_remove_node(i915, stolen);
		kfree(stolen);
		return obj;
	}

	/* Some objects just need physical mem from stolen space */
	if (gtt_offset == I915_GTT_OFFSET_NONE)
		return obj;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err;

	vma = i915_vma_instance(obj, &ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_pages;
	}

	/* To simplify the initialisation sequence between KMS and GTT,
	 * we allow construction of the stolen object prior to
	 * setting up the GTT space. The actual reservation will occur
	 * later.
	 */
	mutex_lock(&ggtt->vm.mutex);
	ret = i915_gem_gtt_reserve(&ggtt->vm, &vma->node,
				   size, gtt_offset, obj->cache_level,
				   0);
	if (ret) {
		DRM_DEBUG_DRIVER("failed to allocate stolen GTT space\n");
		mutex_unlock(&ggtt->vm.mutex);
		goto err_pages;
	}

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

	GEM_BUG_ON(vma->pages);
	vma->pages = obj->mm.pages;
	atomic_set(&vma->pages_count, I915_VMA_PAGES_ACTIVE);

	set_bit(I915_VMA_GLOBAL_BIND_BIT, __i915_vma_flags(vma));
	__i915_vma_set_map_and_fenceable(vma);

	list_add_tail(&vma->vm_link, &ggtt->vm.bound_list);
	mutex_unlock(&ggtt->vm.mutex);

	GEM_BUG_ON(i915_gem_object_is_shrinkable(obj));
	atomic_inc(&obj->bind_count);

	return obj;

err_pages:
	i915_gem_object_unpin_pages(obj);
err:
	i915_gem_object_put(obj);
	return ERR_PTR(ret);
}
