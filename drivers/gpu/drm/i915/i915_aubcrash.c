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

void i915_error_page_walk(struct i915_address_space *vm,
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

int i915_error_state_to_aub(struct drm_i915_error_state_buf *m,
			    const struct i915_gpu_state *error)
{
	return 0;
}
