/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Author:
 *    Oscar Mateo <oscar.mateo@intel.com>
 *
 */

#include "intel_drv.h"
#include "i915_aubcrash.h"
#include "i915_aubmemtrace.h"

/**
 * DOC: AubCrash
 *
 * This code is a companion to i915_gpu_error. The idea is that, on a GPU crash,
 * we can dump an AUB file that describes the state of the system at the point
 * of the crash (GTTs, contexts, BBs, BOs, etc...). While i915_gpu_error kind of
 * already does that, it uses a text format that is not specially human-friendly.
 * An AUB file, on the other hand, can be used by a number of tools (graphical
 * AUB file browsers, simulators, emulators, etc...) that facilitate debugging.
 *
 */

#define COPY_PX_ENTRIES(px, storage) do { \
	u64 *vaddr; \
	if (!storage) \
		return -ENOMEM; \
	vaddr = kmap_atomic(px_base(px)->page); \
	memcpy(storage, vaddr, PAGE_SIZE); \
	kunmap_atomic(vaddr); \
} while (0)

int record_pml4(struct drm_i915_error_pagemap_lvl *e_pml4,
		struct i915_pml4 *pml4,
		struct i915_page_directory_pointer *scratch_pdp,
		bool is_48bit)
{
	int l3;

	if (is_48bit) {
		e_pml4->paddr = px_dma(pml4);
		e_pml4->storage = (u64 *)__get_free_page(GFP_ATOMIC | __GFP_NOWARN);
		COPY_PX_ENTRIES(pml4, e_pml4->storage);
		for (l3 = 0; l3 < GEN8_PML4ES_PER_PML4; l3++)
			if (pml4->pdps[l3] != scratch_pdp)
				e_pml4->nxt_lvl_count++;
	} else
		e_pml4->nxt_lvl_count = 1;

	e_pml4->nxt_lvl = kcalloc(e_pml4->nxt_lvl_count,
				  sizeof(*e_pml4->nxt_lvl), GFP_ATOMIC);
	if (!e_pml4->nxt_lvl) {
		e_pml4->nxt_lvl_count = 0;
		return -ENOMEM;
	}

	return 0;
}

int record_pdp(struct drm_i915_error_pagemap_lvl *e_pdp,
	       struct i915_page_directory_pointer *pdp,
	       bool is_48bit)
{
	if (is_48bit) {
		e_pdp->paddr = px_dma(pdp);
		e_pdp->storage = (u64 *)__get_free_page(GFP_ATOMIC | __GFP_NOWARN);
		COPY_PX_ENTRIES(pdp, e_pdp->storage);
	}

	e_pdp->nxt_lvl_count = pdp->used_pdpes;
	e_pdp->nxt_lvl = kcalloc(e_pdp->nxt_lvl_count,
				  sizeof(*e_pdp->nxt_lvl), GFP_ATOMIC);
	if (!e_pdp->nxt_lvl) {
		e_pdp->nxt_lvl_count = 0;
		return -ENOMEM;
	}

	return 0;
}

int record_pd(struct drm_i915_error_pagemap_lvl *e_pd,
	      struct i915_page_directory *pd)
{
	e_pd->paddr = px_dma(pd);
	e_pd->storage = (u64 *)__get_free_page(GFP_ATOMIC | __GFP_NOWARN);
	COPY_PX_ENTRIES(pd, e_pd->storage);

	e_pd->nxt_lvl_count = pd->used_pdes;
	e_pd->nxt_lvl = kcalloc(e_pd->nxt_lvl_count,
				sizeof(*e_pd->nxt_lvl), GFP_ATOMIC);
	if (!e_pd->nxt_lvl) {
		e_pd->nxt_lvl_count = 0;
		return -ENOMEM;
	}

	return 0;
}

void i915_error_record_ppgtt(struct i915_gpu_state *error,
			     struct i915_address_space *vm,
			     int idx)
{
	struct i915_hw_ppgtt *ppgtt;
	struct drm_i915_error_pagemap_lvl *e_pml4;
	struct i915_pml4 *pml4;
	int l3, l2, max_lvl3, max_lvl2, i, j;
	bool is_48bit;
	int ret;

	if (i915_is_ggtt(vm))
		return;

	ppgtt = i915_vm_to_ppgtt(vm);
	is_48bit = i915_vm_is_48bit(&ppgtt->vm);
	if (is_48bit) {
		max_lvl3 = GEN8_PML4ES_PER_PML4;
		max_lvl2 = GEN8_4LVL_PDPES;
	} else {
		max_lvl3 = 1;
		max_lvl2 = GEN8_3LVL_PDPES;
	}

	/* PML4 */
	pml4 = is_48bit? &ppgtt->pml4 : NULL;
	e_pml4 = &error->ppgtt_pml4[idx];
	ret = record_pml4(e_pml4, pml4, vm->scratch_pdp, is_48bit);
	if (ret < 0)
		return;

	/* PDP */
	for (l3 = 0, i = 0; l3 < max_lvl3; l3++) {
		struct drm_i915_error_pagemap_lvl *e_pdp;
		struct i915_page_directory_pointer *pdp;

		pdp = is_48bit? pml4->pdps[l3] : &ppgtt->pdp;
		if (pdp == vm->scratch_pdp)
			continue;

		e_pdp = &e_pml4->nxt_lvl[i];
		ret = record_pdp(e_pdp, pdp, is_48bit);
		if (ret < 0)
			return;

		/* PD */
		for (l2 = 0, j = 0; l2 < max_lvl2; l2++) {
			struct drm_i915_error_pagemap_lvl *e_pd;
			struct i915_page_directory *pd;

			pd = pdp->page_directory[l2];
			if (pd == vm->scratch_pd)
				continue;

			e_pd = &e_pdp->nxt_lvl[j];
			ret = record_pd(e_pd, pd);
			if (ret < 0)
				return;

			if (++j == e_pdp->nxt_lvl_count)
				break;
		}

		if (++i == e_pml4->nxt_lvl_count)
			break;

	}

	/* XXX: Do I want to dump the scratch pdp/pd/pt/page? */
	/* TODO: Support huge pages */
}

void i915_error_free_ppgtt(struct i915_gpu_state *error, int idx)
{
	struct drm_i915_error_pagemap_lvl *e_pml4 = &error->ppgtt_pml4[idx];
	int i, j;

	for (i = e_pml4->nxt_lvl_count - 1; i >= 0; i--) {
		struct drm_i915_error_pagemap_lvl *e_pdp =
			&e_pml4->nxt_lvl[i];

		for (j = e_pdp->nxt_lvl_count - 1; j >= 0; j--) {
			struct drm_i915_error_pagemap_lvl *e_pd =
				&e_pdp->nxt_lvl[j];

			free_page((unsigned long)e_pd->storage);
			kfree(e_pd);
		}
		free_page((unsigned long)e_pdp->storage);
		kfree(e_pdp);
	}
	free_page((unsigned long)e_pml4->storage);
}

void i915_error_page_walk(struct drm_i915_private *i915,
			  struct i915_address_space *vm,
			  u64 offset,
			  gen8_pte_t *entry,
			  phys_addr_t *paddr)
{
	if (i915_is_ggtt(vm)) {
		struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
		uint index = offset >> PAGE_SHIFT;

		gen8_pte_t __iomem *pte =
			(gen8_pte_t __iomem *)ggtt->gsm + index;

		*entry = readq(pte);
		*paddr = ggtt->gsm_paddr + index * sizeof(u64);
	} else {
		struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
		struct i915_pml4 *pml4;
		struct i915_page_directory_pointer *pdp;
		struct i915_page_directory *pd;
		struct i915_page_table *pt;
		u32 pml4e, pdpe, pde, pte;
		u64 *vaddr;

		pml4e = gen8_pml4e_index(offset);
		if (i915_vm_is_48bit(&ppgtt->vm)) {
			pml4 = &ppgtt->pml4;
			pdp = pml4->pdps[pml4e];
		} else {
			GEM_BUG_ON(pml4e != 0);
			pdp = &ppgtt->pdp;
		}

		pdpe = gen8_pdpe_index(offset);
		pd = pdp->page_directory[pdpe];

		pde = gen8_pde_index(offset);
		pt = pd->page_table[pde];

		pte = gen8_pte_index(offset);
		vaddr = kmap_atomic(px_base(pt)->page);
		*entry = vaddr[pte];
		kunmap_atomic(vaddr);
		*paddr = px_dma(pt) + pte * sizeof(u64);
	}
}

#ifdef CONFIG_DRM_I915_COMPRESS_ERROR

void write_aub(void *priv, const void *data, size_t len)
{
	struct drm_i915_error_state_buf *e = priv;

	/* TODO: Compress the AUB file on the go */
	i915_error_binary_write(e, data, len);
}

#else

void write_aub(void *priv, const void *data, size_t len)
{
	struct drm_i915_error_state_buf *e = priv;

	i915_error_binary_write(e, data, len);
}

#endif

#define AUB_COMMENT_ERROR_OBJ(name, obj) do { \
	i915_aub_comment(aub, name " (%08x_%08x %8u)", \
			 upper_32_bits((obj)->gtt_offset), \
			 lower_32_bits((obj)->gtt_offset), \
			 (obj)->gtt_size); \
} while (0)

int i915_error_state_to_aub(struct drm_i915_error_state_buf *m,
			    const struct i915_gpu_state *error)
{
	struct drm_i915_private *dev_priv = m->i915;
	struct intel_aub *aub;
	int i;

	aub = i915_aub_start(dev_priv, write_aub, (void *)m, "AubCrash", true);
	if (IS_ERR(aub))
		return PTR_ERR(aub);

	if (!error) {
		i915_aub_comment(aub, "No error state collected\n");
		return 0;
	}

	i915_aub_comment(aub, "Registers");
	i915_aub_register(aub, GAM_ECOCHK, error->gam_ecochk);
	for (i = 0; i < ARRAY_SIZE(error->engine); i++) {
		const struct drm_i915_error_engine *ee = &error->engine[i];
		struct intel_engine_cs *engine = dev_priv->engine[i];

		if (!ee->batchbuffer)
			continue;

		i915_aub_register(aub, RING_MODE_GEN7(engine),
				  _MASKED_BIT_ENABLE(ee->vm_info.gfx_mode));
		i915_aub_register(aub, RING_HWS_PGA(engine->mmio_base),
				  ee->hws);
	}

	i915_aub_comment(aub, "PPGTT PML4/PDP/PD");
	for (i = 0; i < ARRAY_SIZE(error->active_vm); i++) {
		const struct drm_i915_error_pagemap_lvl *pml4 =
			&error->ppgtt_pml4[i];
		int l3, l2;

		if (!error->active_vm[i])
			break;

		if (pml4->storage)
			i915_aub_gtt(aub, PPGTT_LEVEL4, pml4->paddr,
				     pml4->storage, GEN8_PML4ES_PER_PML4);

		for (l3 = 0; l3 < pml4->nxt_lvl_count; l3++) {
			const struct drm_i915_error_pagemap_lvl *pdp =
				&pml4->nxt_lvl[l3];

			if (pdp->storage)
				i915_aub_gtt(aub, PPGTT_LEVEL3, pdp->paddr,
					     pdp->storage, GEN8_4LVL_PDPES);

			for (l2 = 0; l2 < pdp->nxt_lvl_count; l2++) {
				const struct drm_i915_error_pagemap_lvl *pd =
					&pdp->nxt_lvl[l2];

				i915_aub_gtt(aub, PPGTT_LEVEL2, pd->paddr,
					     pd->storage, I915_PDES);
			}
		}
	}

	/* Active request */
	for (i = 0; i < ARRAY_SIZE(error->engine); i++) {
		const struct drm_i915_error_engine *ee = &error->engine[i];
		struct intel_engine_cs *engine = dev_priv->engine[i];
		int j;

		if (!ee->batchbuffer)
			continue;

		i915_aub_comment(aub, "Engine %s", engine->name);

		if (ee->hws_page) {
			AUB_COMMENT_ERROR_OBJ("Hardware Status Page",
					      ee->hws_page);
			i915_aub_buffer(aub, true, ee->hws_page->tiling,
					ee->hws_page->pages,
					ee->hws_page->page_count);
		}

		if (ee->ctx) {
			u64 gtt_offset =
				ee->ctx->gtt_offset + LRC_GUCSHR_SZ * PAGE_SIZE;
			u64 gtt_size =
				ee->ctx->gtt_size - LRC_GUCSHR_SZ * PAGE_SIZE;

			i915_aub_comment(aub,
					 "Logical Ring Context (%08x_%08x %8u)",
					 upper_32_bits(gtt_offset),
					 lower_32_bits(gtt_offset),
					 gtt_size);
			i915_aub_context(aub, engine->class,
					 ee->ctx->pages + LRC_GUCSHR_SZ,
					 ee->ctx->page_count - LRC_GUCSHR_SZ);
		}
#if 0
		if (ee->renderstate) {
			AUB_COMMENT_ERROR_OBJ("Renderstate", ee->renderstate);
			i915_aub_batchbuffer(aub, true, ee->renderstate->pages,
					     ee->renderstate->page_count);
		}
#endif

		if (ee->wa_batchbuffer) {
			AUB_COMMENT_ERROR_OBJ("Scratch", ee->wa_batchbuffer);
			i915_aub_buffer(aub, true, I915_TILING_NONE,
					ee->wa_batchbuffer->pages,
					ee->wa_batchbuffer->page_count);
		}

		if (ee->wa_ctx) {
			AUB_COMMENT_ERROR_OBJ("WA context", ee->wa_ctx);
			i915_aub_batchbuffer(aub, true, ee->wa_ctx->pages,
					     ee->wa_ctx->page_count);
		}

		if (ee->ringbuffer) {
			AUB_COMMENT_ERROR_OBJ("Ringbuffer", ee->ringbuffer);
			i915_aub_batchbuffer(aub, true, ee->ringbuffer->pages,
					     ee->ringbuffer->page_count);
		}

		if (ee->batchbuffer) {
			AUB_COMMENT_ERROR_OBJ("Batchbuffer", ee->batchbuffer);
			i915_aub_batchbuffer(aub, false, ee->batchbuffer->pages,
					     ee->batchbuffer->page_count);
		}

		for (j = 0; j < ee->user_bo_count; j++) {
			struct drm_i915_error_object *obj = ee->user_bo[j];

			AUB_COMMENT_ERROR_OBJ("BO", obj);
			i915_aub_buffer(aub, false, obj->tiling,
					obj->pages, obj->page_count);
		}

		/* XXX: Do I want to overwrite the head/tail inside the lrc? */
		i915_aub_comment(aub, "ELSP submissions");
		for (j = 0; j < ee->num_requests; j++)
			i915_aub_elsp_submit(aub, engine,
					     ee->requests[j].lrc_desc);
	}

	i915_aub_stop(aub);

	if (m->bytes == 0 && m->err)
		return m->err;

	return 0;
}
