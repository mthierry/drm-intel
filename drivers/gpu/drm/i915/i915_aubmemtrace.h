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

#ifndef _INTEL_AUBCAPTURE_H_
#define _INTEL_AUBCAPTURE_H_

#define AUB_COMMENT_MAX_LENGTH	512
#define AUB_SCRATCH_SIZE	1280

typedef void (*write_aub_fn)(void *priv, const void *data, size_t length);

struct intel_aub {
	struct drm_i915_private *i915;

	write_aub_fn write;
	void *priv;

	enum intel_platform platform;
	u8 revision;

	phys_addr_t gsm_paddr;

	bool verbose;

	/* Avoid using the stack */
	u8 scratch[AUB_SCRATCH_SIZE];
};

enum pagemap_level {
	PPGTT_LEVEL4,
	PPGTT_LEVEL3,
	PPGTT_LEVEL2,
	PPGTT_LEVEL1,
	GGTT_LEVEL1,
};

struct intel_aub *i915_aub_start(struct drm_i915_private *i915,
				 write_aub_fn write_function,
				 void *private_data,
				 const char *message,
				 bool verbose);
void i915_aub_comment(struct intel_aub *aub, const char *format, ...);
void i915_aub_register(struct intel_aub *aub, i915_reg_t reg, u32 value);
void i915_aub_gtt(struct intel_aub *aub, enum pagemap_level lvl,
		  phys_addr_t paddr, const u64 *entries, uint count);
void i915_aub_context(struct intel_aub *aub, u8 class,
		      const struct drm_i915_error_page *pages, uint count);
void i915_aub_batchbuffer(struct intel_aub *aub, bool global_gtt,
			  const struct drm_i915_error_page *pages, uint count);
void i915_aub_buffer(struct intel_aub *aub, bool global_gtt, int tiling_mode,
		     const struct drm_i915_error_page *pages, uint count);
void i915_aub_elsp_submit(struct intel_aub *aub, struct intel_engine_cs *engine,
			  u64 desc);
void i915_aub_stop(struct intel_aub *aub);

#endif
