/*	$NetBSD: i915_gem_region.c,v 1.4.4.1 2024/02/03 11:15:12 martin Exp $	*/

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: i915_gem_region.c,v 1.4.4.1 2024/02/03 11:15:12 martin Exp $");

#include "intel_memory_region.h"
#include "i915_gem_region.h"
#include "i915_drv.h"
#include "i915_trace.h"

void
i915_gem_object_put_pages_buddy(struct drm_i915_gem_object *obj,
				struct sg_table *pages)
{
	__intel_memory_region_put_pages_buddy(obj->mm.region, &obj->mm.blocks);

	obj->mm.dirty = false;
#ifdef __NetBSD__
	bus_dmamap_unload(obj->base.dev->dmat, pages->sgl->sg_dmamap);
#endif
	sg_free_table(pages);
	kfree(pages);
}

int
i915_gem_object_get_pages_buddy(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mem = obj->mm.region;
	struct list_head *blocks = &obj->mm.blocks;
	resource_size_t size = obj->base.size;
	resource_size_t prev_end;
	struct i915_buddy_block *block;
	unsigned int flags;
	struct sg_table *st;
	struct scatterlist *sg;
	unsigned int sg_page_sizes;
	int ret;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

#ifndef __NetBSD__
	if (sg_alloc_table(st, size >> ilog2(mem->mm.chunk_size), GFP_KERNEL)) {
		kfree(st);
		return -ENOMEM;
	}
#endif

	flags = I915_ALLOC_MIN_PAGE_SIZE;
	if (obj->flags & I915_BO_ALLOC_CONTIGUOUS)
		flags |= I915_ALLOC_CONTIGUOUS;

	ret = __intel_memory_region_get_pages_buddy(mem, size, flags, blocks);
	if (ret)
		goto err_free_sg;

	GEM_BUG_ON(list_empty(blocks));

#ifdef __NetBSD__
	__USE(prev_end);
	bus_dma_tag_t dmat = obj->base.dev->dmat;
	bus_dma_segment_t *segs = NULL;
	int i = 0, nsegs = 0;
	bool loaded = false;

	sg = NULL;

	list_for_each_entry(block, blocks, link) {
		if (nsegs >= INT_MAX ||
		    nsegs >= SIZE_MAX/sizeof(segs[0]))
			goto err;
		nsegs++;
	}
	segs = kmem_zalloc(nsegs * sizeof(segs[0]), KM_SLEEP);
	list_for_each_entry(block, blocks, link) {
		u64 block_size, offset;

		block_size = min_t(u64, size,
				   i915_buddy_block_size(&mem->mm, block));
		offset = i915_buddy_block_offset(block);

		segs[i].ds_addr = mem->region.start + offset;
		segs[i].ds_len = block_size;
		i++;
	}
	KASSERT(i == nsegs);

	ret = sg_alloc_table_from_bus_dmamem(st, dmat, segs, nsegs,
	    GFP_KERNEL);
	if (ret)
		goto err;
	sg = st->sgl;

	/* XXX errno NetBSD->Linux */
	ret = -bus_dmamap_create(dmat, size, nsegs, size, 0, BUS_DMA_WAITOK,
	    &sg->sg_dmamap);
	if (ret) {
		sg->sg_dmamap = NULL;
		goto err;
	}
	sg->sg_dmat = dmat;

	/* XXX errno NetBSD->Linux */
	ret = -bus_dmamap_load_raw(dmat, sg->sg_dmamap, segs, nsegs, size,
	    BUS_DMA_WAITOK);
	if (ret)
		goto err;
	loaded = true;

	kmem_free(segs, nsegs * sizeof(segs[0]));
	segs = NULL;

	sg_page_sizes = i915_sg_page_sizes(sg);
#else
	sg = st->sgl;
	st->nents = 0;
	sg_page_sizes = 0;
	prev_end = (resource_size_t)-1;

	list_for_each_entry(block, blocks, link) {
		u64 block_size, offset;

		block_size = min_t(u64, size,
				   i915_buddy_block_size(&mem->mm, block));
		offset = i915_buddy_block_offset(block);

		GEM_BUG_ON(overflows_type(block_size, sg->length));

		if (offset != prev_end ||
		    add_overflows_t(typeof(sg->length), sg->length, block_size)) {
			if (st->nents) {
				sg_page_sizes |= sg->length;
				sg = __sg_next(sg);
			}

			sg_dma_address(sg) = mem->region.start + offset;
			sg_dma_len(sg) = block_size;

			sg->length = block_size;

			st->nents++;
		} else {
			sg->length += block_size;
			sg_dma_len(sg) += block_size;
		}

		prev_end = offset + block_size;
	}

	sg_page_sizes |= sg->length;
	sg_mark_end(sg);
	i915_sg_trim(st);
#endif

	__i915_gem_object_set_pages(obj, st, sg_page_sizes);

	return 0;

#ifdef __NetBSD__
err:
	if (loaded)
		bus_dmamap_unload(dmat, st->sgl->sg_dmamap);
	if (sg && sg->sg_dmamap)
		bus_dmamap_destroy(dmat, sg->sg_dmamap);
	if (segs)
		kmem_free(segs, nsegs * sizeof(segs[0]));
	__intel_memory_region_put_pages_buddy(mem, blocks);
#endif
err_free_sg:
	sg_free_table(st);
	kfree(st);
	return ret;
}

void i915_gem_object_init_memory_region(struct drm_i915_gem_object *obj,
					struct intel_memory_region *mem,
					unsigned long flags)
{
	INIT_LIST_HEAD(&obj->mm.blocks);
	obj->mm.region = intel_memory_region_get(mem);

	obj->flags |= flags;
	if (obj->base.size <= mem->min_page_size)
		obj->flags |= I915_BO_ALLOC_CONTIGUOUS;

	mutex_lock(&mem->objects.lock);

	if (obj->flags & I915_BO_ALLOC_VOLATILE)
		list_add(&obj->mm.region_link, &mem->objects.purgeable);
	else
		list_add(&obj->mm.region_link, &mem->objects.list);

	mutex_unlock(&mem->objects.lock);
}

void i915_gem_object_release_memory_region(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mem = obj->mm.region;

	mutex_lock(&mem->objects.lock);
	list_del(&obj->mm.region_link);
	mutex_unlock(&mem->objects.lock);

	intel_memory_region_put(mem);
}

struct drm_i915_gem_object *
i915_gem_object_create_region(struct intel_memory_region *mem,
			      resource_size_t size,
			      unsigned int flags)
{
	struct drm_i915_gem_object *obj;

	/*
	 * NB: Our use of resource_size_t for the size stems from using struct
	 * resource for the mem->region. We might need to revisit this in the
	 * future.
	 */

	GEM_BUG_ON(flags & ~I915_BO_ALLOC_FLAGS);

	if (!mem)
		return ERR_PTR(-ENODEV);

	size = round_up(size, mem->min_page_size);

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_MIN_ALIGNMENT));

	/*
	 * XXX: There is a prevalence of the assumption that we fit the
	 * object's page count inside a 32bit _signed_ variable. Let's document
	 * this and catch if we ever need to fix it. In the meantime, if you do
	 * spot such a local variable, please consider fixing!
	 */

	if (size >> PAGE_SHIFT > INT_MAX)
		return ERR_PTR(-E2BIG);

	if (overflows_type(size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = mem->ops->create_object(mem, size, flags);
	if (!IS_ERR(obj))
		trace_i915_gem_object_create(obj);

	return obj;
}
