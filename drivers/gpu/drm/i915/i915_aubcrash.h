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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _INTEL_AUBCRASH_H_
#define _INTEL_AUBCRASH_H_

#if IS_ENABLED(CONFIG_DRM_I915_AUB_CRASH_DUMP)

void i915_error_record_ppgtt(struct i915_gpu_state *error,
			     struct i915_address_space *vm,
			     int idx);
void i915_error_free_ppgtt(struct i915_gpu_state *error, int idx);
void i915_error_page_walk(struct i915_address_space *vm,
			  u64 offset,
			  gen8_pte_t *entry,
			  phys_addr_t *paddr);
int i915_error_state_to_aub(struct drm_i915_error_state_buf *m,
                            const struct i915_gpu_state *error);

static inline bool i915_error_state_should_capture(struct i915_vma *vma,
						   struct i915_vma *batch)
{
	return ((INTEL_GEN(vma->vm->i915) >= 8) && (vma != batch));
}

#else

static inline void i915_error_record_ppgtt(struct i915_gpu_state *error,
					   struct i915_address_space *vm,
					   int idx)
{
}

static inline void i915_error_free_ppgtt(struct i915_gpu_state *error, int idx)
{
}

static inline void i915_error_page_walk(struct i915_address_space *vm,
					u64 offset,
					gen8_pte_t *entry,
					phys_addr_t *paddr)
{
}

static inline int i915_error_state_to_aub(struct drm_i915_error_state_buf *m,
					  const struct i915_gpu_state *error)
{
	return 0;
}

static inline bool i915_error_state_should_capture(struct i915_vma *vma,
						   struct i915_vma *batch)
{
	return false;
}

#endif

#endif
