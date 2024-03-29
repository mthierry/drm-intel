/*
 * Copyright © 2014-2017 Intel Corporation
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
 */

#include "intel_guc_ads.h"
#include "intel_uc.h"
#include "i915_drv.h"

/*
 * The Additional Data Struct (ADS) has pointers for different buffers used by
 * the GuC. One single gem object contains the ADS struct itself (guc_ads), the
 * scheduling policies (guc_policies), a structure describing a collection of
 * register sets (guc_mmio_reg_state) and some extra pages for the GuC to save
 * its internal state for sleep.
 */

static void guc_policy_init(struct guc_policy *policy)
{
	policy->execution_quantum = POLICY_DEFAULT_EXECUTION_QUANTUM_US;
	policy->preemption_time = POLICY_DEFAULT_PREEMPTION_TIME_US;
	policy->fault_time = POLICY_DEFAULT_FAULT_TIME_US;
	policy->policy_flags = 0;
}

static void guc_policies_init(struct guc_policies *policies)
{
	struct guc_policy *policy;
	u32 p, i;

	policies->dpc_promote_time = POLICY_DEFAULT_DPC_PROMOTE_TIME_US;
	policies->max_num_work_items = POLICY_MAX_NUM_WI;

	for (p = 0; p < GUC_CLIENT_PRIORITY_NUM; p++) {
		for (i = GUC_RENDER_ENGINE; i < GUC_MAX_ENGINES_NUM; i++) {
			policy = &policies->policy[p][i];

			guc_policy_init(policy);
		}
	}

	policies->is_valid = 1;
}

/*
 * In this macro it is highly unlikely to exceed max value but even if we did
 * it is not an error so just throw a warning and continue. Only side effect
 * in continuing further means some registers won't be added to save/restore
 * list.
 */
#define GUC_ADD_MMIO_REG_ADS(node, reg_addr, _flags, defvalue)		\
	do {								\
		u32 __count = node->number_of_registers;		\
		if (WARN_ON(__count >= GUC_REGSET_MAX_REGISTERS))	\
			continue;					\
		node->registers[__count].offset = reg_addr.reg;		\
		node->registers[__count].flags = (_flags);		\
		if (defvalue)						\
			node->registers[__count].value = (defvalue);	\
		node->number_of_registers++;				\
	} while (0)

/*
 * The first 80 dwords of the register state context, containing the
 * execlists and ppgtt registers.
 */
#define LR_HW_CONTEXT_SIZE	(80 * sizeof(u32))

/**
 * intel_guc_ads_create() - creates GuC ADS
 * @guc: intel_guc struct
 *
 */
int intel_guc_ads_create(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct i915_vma *vma, *kernel_ctx_vma;
	struct page *page;
	/* The ads obj includes the struct itself and buffers passed to GuC */
	struct {
		struct guc_ads ads;
		struct guc_policies policies;
		struct guc_mmio_reg_state reg_state;
		u8 reg_state_buffer[GUC_S3_SAVE_SPACE_PAGES * PAGE_SIZE];
	} __packed *blob;
	struct intel_engine_cs *engine;
	struct i915_workarounds *workarounds = &dev_priv->workarounds;
	enum intel_engine_id id;
	const u32 skipped_offset = LRC_HEADER_PAGES * PAGE_SIZE;
	const u32 skipped_size = LRC_PPHWSP_SZ * PAGE_SIZE + LR_HW_CONTEXT_SIZE;
	u32 base;

	GEM_BUG_ON(guc->ads_vma);

	vma = intel_guc_allocate_vma(guc, PAGE_ALIGN(sizeof(*blob)));
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	guc->ads_vma = vma;

	page = i915_vma_first_page(vma);
	blob = kmap(page);

	/* GuC scheduling policies */
	guc_policies_init(&blob->policies);

	/* MMIO reg state */
	for_each_engine(engine, dev_priv, id) {
		u32 i;
		struct guc_mmio_regset *eng_reg =
			&blob->reg_state.engine_reg[engine->guc_id];

		/*
		 * Provide a list of registers to be saved/restored during gpu
		 * reset. This is mainly required for Media reset (aka watchdog
		 * timeout) which is completely under the control of GuC
		 * (resubmission of hung workload is handled inside GuC).
		 */
		GUC_ADD_MMIO_REG_ADS(eng_reg, RING_HWS_PGA(engine->mmio_base),
				     GUC_REGSET_ENGINERESET |
				     GUC_REGSET_SAVE_CURRENT_VALUE, 0);

		/*
		 * Workaround the guc issue with masked registers, instead of
		 * asking the fw to read the current reg value, we provide the
		 * one we expect.
		 *
		 * GFX_RUN_LIST_ENABLE: lrc mode on, set by engine->init_hw.
		 * GFX_INTERRUPT_STEERING: fwd irqs to GuC (guc_interrupts_capture).
		 */
		GUC_ADD_MMIO_REG_ADS(eng_reg, RING_MODE_GEN7(engine),
				     GUC_REGSET_ENGINERESET |
				     GUC_REGSET_SAVE_DEFAULT_VALUE,
				     I915_READ(RING_MODE_GEN7(engine)) |
				     GFX_RUN_LIST_ENABLE |
				     GFX_INTERRUPT_STEERING | (0xFFFF<<16));

		GUC_ADD_MMIO_REG_ADS(eng_reg, RING_IMR(engine->mmio_base),
				     GUC_REGSET_ENGINERESET |
				     GUC_REGSET_SAVE_CURRENT_VALUE, 0);

		/* ask guc to re-apply workarounds set in *_init_workarounds */
		if (engine->id == RCS) {
			for (i = 0; i < workarounds->guc_count; i++)
				GUC_ADD_MMIO_REG_ADS(eng_reg,
						     _MMIO(workarounds->guc_reg[i].addr),
						     GUC_REGSET_ENGINERESET |
						     GUC_REGSET_SAVE_DEFAULT_VALUE,
						     workarounds->guc_reg[i].value);
		}

		DRM_DEBUG_DRIVER("%s register save/restore count: %u\n",
				 engine->name, eng_reg->number_of_registers);

		blob->reg_state.white_list[engine->guc_id].mmio_start =
			i915_mmio_reg_offset(RING_FORCE_TO_NONPRIV(engine->mmio_base, 0));

		/*
		 * Note: if the GuC whitelist management is enabled, the values
		 * should be filled using the workaround framework to avoid
		 * inconsistencies with the handling of FORCE_TO_NONPRIV
		 * registers.
		 */
		blob->reg_state.white_list[engine->guc_id].count =
					GUC_MMIO_WHITE_LIST_MAX;

		for (i = 0; i < GUC_MMIO_WHITE_LIST_MAX; i++) {
			blob->reg_state.white_list[engine->guc_id].offsets[i] =
				I915_READ(RING_FORCE_TO_NONPRIV(engine->mmio_base, i));
		}
	}

	/*
	 * The GuC requires a "Golden Context" when it reinitialises
	 * engines after a reset. Here we use the Render ring default
	 * context, which must already exist and be pinned in the GGTT,
	 * so its address won't change after we've told the GuC where
	 * to find it. Note that we have to skip our header (1 page),
	 * because our GuC shared data is there.
	 */
	kernel_ctx_vma = to_intel_context(dev_priv->kernel_context,
					  dev_priv->engine[RCS])->state;
	blob->ads.golden_context_lrca =
		intel_guc_ggtt_offset(guc, kernel_ctx_vma) + skipped_offset;

	/*
	 * The GuC expects us to exclude the portion of the context image that
	 * it skips from the size it is to read. It starts reading from after
	 * the execlist context (so skipping the first page [PPHWSP] and 80
	 * dwords). Weird guc is weird.
	 */
	for_each_engine(engine, dev_priv, id)
		blob->ads.eng_state_size[engine->guc_id] =
			engine->context_size - skipped_size;

	base = intel_guc_ggtt_offset(guc, vma);
	blob->ads.scheduler_policies = base + ptr_offset(blob, policies);
	blob->ads.reg_state_buffer = base + ptr_offset(blob, reg_state_buffer);
	blob->ads.reg_state_addr = base + ptr_offset(blob, reg_state);

	kunmap(page);

	return 0;
}

void intel_guc_ads_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->ads_vma, 0);
}
