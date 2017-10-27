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
#include "i915_aubmemtrace.h"
#include "i915_aubmemtrace_format.h"

/**
 * DOC: AUB Memtrace
 *
 * The "AUB" memtrace file format provides a way to log GPU workloads in the
 * same (or a very similar) form as they would be sent to the Intel Graphics
 * Hardware. These logs are then provided to the user, who can use them for
 * multiple purposes. For example: to easily browse the workload in order to
 * find HW programming errors or to replay the workload using a GPU simulator or
 * emulator.
 *
 * Technically, the format is the same used by intel_aubdump (a userspace tool
 * that you can find in intel-gpu-tools) but by writing AUB files from the KMD
 * we can log information that a userspace tool by itself cannot. E.g.: real GPU
 * virtual addresses, pagetables, GPU contexts, workaround batchbuffers, etc...
 *
 * Trivia:
 * In case the reader was wondering, AUB is a shorthand for "Auburn", the
 * code name of the Intel740™ Graphics Accelerator (also known as the i740).
 * We maintain the name of the file format for historical reasons.
 *
 */

#define AUB_TOOL_VERSION_MAJOR 0
#define AUB_TOOL_VERSION_MINOR 1

#define AUB_WRITE(data, len) do { \
	aub->write(aub->priv, data, len); \
} while (0)

#define PADDING(x) ((4 - ((x) & 3)) & 3)

static inline void aub_write_padding(struct intel_aub *aub, u32 bytes)
{
	u32 zero = 0;

	if (GEM_WARN_ON(bytes > 3))
		return;

	AUB_WRITE(&zero, bytes);
}

static inline void aub_header_fill(struct aub_cmd_hdr *header, u32 type,
				   u32 opcode, u32 sub_opcode,
				   u32 dword_count)
{
	header->type = type;
	header->opcode = opcode;
	header->sub_opcode = sub_opcode;
	header->dword_count = dword_count;
}

struct aub_chip_revision {
	uint rev_id;
	uint stepping;
	uint metal;
};

static const struct aub_chip_revision bdw_revs[] = {
	{ 0, STEP_A, 0 },
};

static const struct aub_chip_revision chv_revs[] = {
	{ 0, STEP_A, 0 },
};

static const struct aub_chip_revision skl_revs[] = {
	{ SKL_REVID_A0, STEP_A, 0 },
	{ SKL_REVID_B0, STEP_B, 0 },
	{ SKL_REVID_C0, STEP_C, 0 },
	{ SKL_REVID_D0, STEP_D, 0 },
	{ SKL_REVID_E0, STEP_E, 0 },
	{ SKL_REVID_F0, STEP_E, 0 },
	{ SKL_REVID_G0, STEP_G, 0 },
	{ SKL_REVID_H0, STEP_H, 0 },
};

static const struct aub_chip_revision bxt_revs[] = {
	{ BXT_REVID_A0,     STEP_A, 0 },
	{ BXT_REVID_A1,     STEP_A, 1 },
	{ BXT_REVID_B0,     STEP_B, 0 },
	{ BXT_REVID_B_LAST, STEP_B, 1 },
	{ BXT_REVID_C0,     STEP_C, 0 },
};

static const struct aub_chip_revision kbl_revs[] = {
	{ KBL_REVID_A0, STEP_A, 0 },
	{ KBL_REVID_B0, STEP_B, 0 },
	{ KBL_REVID_C0, STEP_C, 0 },
	{ KBL_REVID_D0, STEP_D, 0 },
	{ KBL_REVID_E0, STEP_E, 0 },
};

static const struct aub_chip_revision glk_revs[] = {
	{ GLK_REVID_A0, STEP_A, 0 },
	{ GLK_REVID_A1, STEP_A, 1 },
};

static const struct aub_chip_revision cnl_revs[] = {
	{ CNL_REVID_A0, STEP_A, 0 },
	{ CNL_REVID_B0, STEP_B, 0 },
	{ CNL_REVID_C0, STEP_C, 0 },
};

struct aub_platforms_table {
	uint platform_id;
	uint device;
	const struct aub_chip_revision *table;
	uint count;
};

static const struct aub_platforms_table platforms[] = {
	{ INTEL_BROADWELL,  DEV_BDW, bdw_revs, ARRAY_SIZE(bdw_revs) },
	{ INTEL_CHERRYVIEW, DEV_CHV, chv_revs, ARRAY_SIZE(chv_revs) },
	{ INTEL_SKYLAKE,    DEV_SKL, skl_revs, ARRAY_SIZE(skl_revs) },
	{ INTEL_BROXTON,    DEV_BXT, bxt_revs, ARRAY_SIZE(bxt_revs) },
	{ INTEL_KABYLAKE,   DEV_KBL, kbl_revs, ARRAY_SIZE(kbl_revs) },
	{ INTEL_GEMINILAKE, DEV_GLK, glk_revs, ARRAY_SIZE(glk_revs) },
	{ INTEL_CANNONLAKE, DEV_CNL, cnl_revs, ARRAY_SIZE(cnl_revs) },
};

static int aub_write_version_packet(struct intel_aub *aub,
				    enum intel_platform platform,
				    u8 revision, const char *message)
{
	u32 length, padding;
	struct cmd_memtrace_version cmd;
	char *buf = (char *)aub->scratch;
	bool rev_warning = false;
	int i, j;

	memset(&cmd, 0, sizeof(cmd));
	aub_header_fill(&cmd.header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_VERSION, sizeof(cmd) / 4 - 2);

	cmd.memtrace_file_version = AUB_FILE_FORMAT_VERSION;
	cmd.swizzling = SWIZZLING_DISABLED;
	cmd.recording_method = METHOD_PHY;
	cmd.pch = PCH_DEFAULT;
	cmd.capture_tool = CAPTURE_TOOL_KMD;

	for (i = 0; i < ARRAY_SIZE(platforms); i++) {
		if (platform == platforms[i].platform_id) {
			const struct aub_chip_revision *table =
				platforms[i].table;
			uint count = platforms[i].count;
			cmd.device = platforms[i].device;

			for (j = 0; j < count; j++) {
				if (revision == table[j].rev_id) {
					cmd.stepping = table[j].stepping;
					cmd.metal = table[j].metal;
				}
			}

			if (j == count) {
				rev_warning = true;
				cmd.stepping = table[count - 1].stepping;
				cmd.metal = table[count - 1].metal;
			}

			break;
		}

		if (i == ARRAY_SIZE(platforms)) {
			DRM_ERROR("Unsupported platform 0x%x\n", platform);
			return -ENODEV;
		}
	}

	cmd.tool_primary_version = AUB_TOOL_VERSION_MAJOR;
	cmd.tool_secondary_version = AUB_TOOL_VERSION_MINOR;

	snprintf(buf, AUB_COMMENT_MAX_LENGTH, message);
	length = strlen(buf);
	padding = PADDING(length);
	cmd.header.dword_count += (length + padding) / 4;

	AUB_WRITE(&cmd, sizeof(cmd) - 4);
	AUB_WRITE(buf, length);
	aub_write_padding(aub, padding);

	if (rev_warning)
		i915_aub_comment(aub,
				 "Unknown revid 0x%x. Using last known step/metal",
				 revision);

	return 0;
}

static void aub_write_comment_packet(struct intel_aub *aub, const char *comment)
{
	struct cmd_memtrace_comment cmd;
	const char preface[] = "AUB: ";
	uint preface_len = strlen(preface);
	uint comment_len = strlen(comment) + 1;
	uint padding = PADDING(comment_len + preface_len);

	memset(&cmd, 0, sizeof(cmd));
	aub_header_fill(&cmd.header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_COMMENT, sizeof(cmd) / 4 - 2);
	cmd.header.dword_count += (preface_len + comment_len + padding) / 4;

	AUB_WRITE(&cmd, sizeof(cmd) - 4);
	AUB_WRITE(preface, preface_len);
	AUB_WRITE(comment, comment_len);
	aub_write_padding(aub, padding);
}

static int aub_write_mem_packet(struct intel_aub *aub,
				enum tiling_values tiling,
				enum data_type_values type,
				enum address_space_values space,
				u64 address,
				const void *data,
				u32 bytes)
{
	struct cmd_memtrace_memwrite cmd;
	uint max_bytes = 4 * (0xffff - (sizeof(cmd) / 4 - 2));
	uint padding = PADDING(bytes);
	uint num_dwords = (bytes + padding) / 4;

	if (bytes > max_bytes)
		return -E2BIG;

	memset(&cmd, 0, sizeof(cmd));
	aub_header_fill(&cmd.header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_MEMORY_WRITE, sizeof(cmd) / 4 - 2);
	cmd.header.dword_count += num_dwords;

	cmd.address = address;
	cmd.tiling = tiling;
	cmd.data_type_hint = type;
	cmd.address_space = space;
	cmd.data_size = bytes;

	AUB_WRITE(&cmd, sizeof(cmd) - 4);
	AUB_WRITE(data, bytes);
	aub_write_padding(aub, padding);

	return bytes;
}

static int aub_write_mem_discon_packet(struct intel_aub *aub,
				       enum tiling_values tiling,
				       enum data_type_values type,
				       enum address_space_values space,
				       const struct memwrite_element *elements,
				       const void **data,
				       uint count)
{
	struct aub_cmd_hdr header;
	struct aub_cmd_memwrite_discon_opts cmd_opts;
	uint cmd_size = sizeof(struct cmd_memtrace_memwrite_discon);
	uint max_bytes = 4 * (0xffff - (cmd_size / 4 - 2));
	uint total_bytes = 0;
	uint padding;
	uint num_dwords;
	int i;

	if (count > DISCONTIGUOUS_WRITE_MAX_ELEMENTS)
		return -E2BIG;

	for (i = 0; i < count; i++)
		total_bytes += elements[i].data_size;

	padding = PADDING(total_bytes);
	num_dwords = (total_bytes + padding) / 4;

	if (total_bytes > max_bytes)
		return -E2BIG;

	memset(&header, 0, sizeof(header));
	aub_header_fill(&header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_MEMORY_WRITE_DISCONTIGUOUS,
			cmd_size / 4 - 2);
	header.dword_count += num_dwords;

	memset(&cmd_opts, 0, sizeof(cmd_opts));
	cmd_opts.tiling = tiling;
	cmd_opts.data_type_hint = type;
	cmd_opts.address_space = space;
	cmd_opts.number_of_elements = count;

	AUB_WRITE(&header, sizeof(header));
	AUB_WRITE(&cmd_opts, sizeof(cmd_opts));

	AUB_WRITE(elements, count * sizeof(*elements));
	for (i = count; i < DISCONTIGUOUS_WRITE_MAX_ELEMENTS; i++) {
		struct memwrite_element zero = {0, 0};
		AUB_WRITE(&zero, sizeof(zero));
	}

	for (i = 0; i < count; i++)
		AUB_WRITE(data[i], elements[i].data_size);

	aub_write_padding(aub, padding);

	return total_bytes;
}

static void aub_write_register_packet(struct intel_aub *aub, i915_reg_t reg,
				      u32 value)
{
	struct cmd_memtrace_register_write cmd;

	memset(&cmd, 0, sizeof(cmd));
	aub_header_fill(&cmd.header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_REGISTER_WRITE, sizeof(cmd) / 4 - 1);
	cmd.message_source = SOURCE_IA;
	cmd.register_size = SIZE_DWORD;
	cmd.register_space = SPACE_MMIO;
	cmd.write_mask_low = 0xffffffff;
	cmd.write_mask_high = 0x0;

	cmd.register_offset = i915_mmio_reg_offset(reg);
	cmd.data[0] = value;

	AUB_WRITE(&cmd, sizeof(cmd));
}

static void aub_write_pci_register_packet(struct intel_aub *aub, u16 bus,
					  u8 device, u8 function,
					  u32 offset, u32 value)
{
	struct cmd_memtrace_register_write cmd;

	memset(&cmd, 0, sizeof(cmd));
	aub_header_fill(&cmd.header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_REGISTER_WRITE, sizeof(cmd) / 4 - 1);
	cmd.message_source = SOURCE_IA;
	cmd.register_size = SIZE_DWORD;
	cmd.register_space = SPACE_PCI;
	cmd.write_mask_low = 0xffffffff;
	cmd.write_mask_high = 0x0;

	cmd.bus = bus;
	cmd.device = device;
	cmd.function = function;
	cmd.offset = offset;
	cmd.data[0] = value;

	AUB_WRITE(&cmd, sizeof(cmd));
}

static void aub_write_regpoll_packet(struct intel_aub *aub, i915_reg_t reg,
				     u32 mask, u32 value)
{
	struct cmd_memtrace_register_poll cmd;

	memset(&cmd, 0, sizeof(cmd));
	aub_header_fill(&cmd.header, CMD_TYPE_AUB, CMD_OPC_MEMTRACE,
			CMD_SUBOPC_MEMTRACE_REGISTER_POLL, sizeof(cmd) / 4 - 1);
	cmd.abort_on_timeout = 1;
	cmd.poll_not_equal = 0;
	cmd.operation_type = OPERATION_TYPE_NORMAL;
	cmd.register_size = SIZE_DWORD;
	cmd.register_space = SPACE_MMIO;

	cmd.poll_mask_low = mask;
	cmd.register_offset = i915_mmio_reg_offset(reg);
	cmd.data[0] = value;

	AUB_WRITE(&cmd, sizeof(cmd));
}

static inline phys_addr_t adjust_gsm_paddr(struct intel_aub *aub,
					   bool global_gtt,
					   phys_addr_t pte_paddr)
{
	if (global_gtt) {
		/*
		 * We already told the other end about the base
		 * of the GGTT stolen memory, so treat it here
		 * as if it was 0x0
		 */
		return (pte_paddr - aub->gsm_paddr);
	} else
		return pte_paddr;
}

static int aub_write_discon_pages(struct intel_aub *aub,
				  bool global_gtt,
				  enum tiling_values tiling,
				  enum data_type_values type,
				  enum address_space_values space,
				  const struct drm_i915_error_page *pages,
				  uint count)
{
	enum address_space_values pte_space;
	uint count_left = count;
	const struct drm_i915_error_page *pages_left = pages;
	struct memwrite_element *elements =
		(struct memwrite_element *)aub->scratch;
	const void **data =
		(const void **)(elements + DISCONTIGUOUS_WRITE_MAX_ELEMENTS);
	int ret;
	int i;

	BUILD_BUG_ON(sizeof(aub->scratch) <
		     DISCONTIGUOUS_WRITE_MAX_ELEMENTS * sizeof(*elements) +
		     DISCONTIGUOUS_WRITE_MAX_ELEMENTS * sizeof(*data));

	pte_space = global_gtt ? ADDRESS_SPACE_GTT_ENTRY :
				 ADDRESS_SPACE_PPGTT_ENTRY;

	while (count_left) {
		uint c = min(count_left, (uint)DISCONTIGUOUS_WRITE_MAX_ELEMENTS);

		if (c == 1) {
			const gen8_pte_t *pte = &pages_left[0].pte;
			phys_addr_t pte_paddr =
				adjust_gsm_paddr(aub, global_gtt,
						 pages_left[0].pte_paddr);

			ret = aub_write_mem_packet(aub, TILING_NONE, TYPE_NOTYPE,
						   pte_space, pte_paddr, pte,
						   sizeof(u64));
		} else {
			for (i = 0; i < c; i++) {
				elements[i].address =
					adjust_gsm_paddr(aub, global_gtt,
							 pages_left[i].pte_paddr);
				elements[i].data_size = sizeof(u64);
				data[i] = &pages_left[i].pte;
			}

			ret = aub_write_mem_discon_packet(aub, TILING_NONE,
							  TYPE_NOTYPE, pte_space,
							  elements,
							  data, c);
		}
		if (ret < 0)
			return ret;

		if (c == 1) {
			ret = aub_write_mem_packet(aub, tiling, type, space,
						   pages_left[0].paddr,
						   pages_left[0].storage,
						   PAGE_SIZE);
		} else {
			for (i = 0; i < c; i++) {
				elements[i].address = pages_left[i].paddr;
				elements[i].data_size = PAGE_SIZE;
				data[i] = pages_left[i].storage;
			}

			ret = aub_write_mem_discon_packet(aub, tiling, type,
							  space, elements,
							  data, c);
		}
		if (ret < 0)
			return ret;

		count_left -= c;
		pages_left += c;
	}

	return 0;
}

struct intel_aub *i915_aub_start(struct drm_i915_private *i915,
				 write_aub_fn write_function,
				 void *private_data,
				 const char *message,
				 bool verbose)
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct intel_aub *aub;
	int ret;

	aub = kmalloc(sizeof(*aub), GFP_KERNEL);
	if (!aub)
		return ERR_PTR(-ENOMEM);

	aub->write = write_function;
	aub->priv = private_data;
	aub->platform = i915->info.platform;
	aub->revision = INTEL_REVID(i915);
	aub->gsm_paddr = ggtt->gsm_paddr;
	aub->verbose = verbose;

	ret = aub_write_version_packet(aub, aub->platform,
				       aub->revision, message);
	if (ret < 0) {
		kfree(aub);
		return ERR_PTR(ret);
	}

	/* Tell the other end about the physical GGTT location */
	GEM_BUG_ON(upper_32_bits(aub->gsm_paddr));
	aub_write_pci_register_packet(aub, 0, 0, 0, 0xb4,
				      lower_32_bits(aub->gsm_paddr));

	return aub;
}

void i915_aub_comment(struct intel_aub *aub, const char *format, ...)
{
	va_list args;
	char *buf = (char *)aub->scratch;
	BUILD_BUG_ON(sizeof(aub->scratch) < AUB_COMMENT_MAX_LENGTH);

	if (!aub->verbose)
		return;

	va_start(args, format);
	vsnprintf(buf, AUB_COMMENT_MAX_LENGTH, format, args);
	va_end(args);

	aub_write_comment_packet(aub, buf);
}

void i915_aub_register(struct intel_aub *aub, i915_reg_t reg, u32 value)
{
	aub_write_register_packet(aub, reg, value);
}

void i915_aub_gtt(struct intel_aub *aub, enum pagemap_level lvl,
		  phys_addr_t paddr, const u64 *entries, uint count)
{
	enum address_space_values space;
	uint max_count = PAGE_SIZE / sizeof(*entries);
	uint c = min(count, max_count);

	switch (lvl) {
	default:
		MISSING_CASE(lvl);
	case PPGTT_LEVEL4:
		space = ADDRESS_SPACE_PPGTT_PML4_ENTRY;
		break;
	case PPGTT_LEVEL3:
		space = ADDRESS_SPACE_PPGTT_PDP_ENTRY;
		break;
	case PPGTT_LEVEL2:
		space = ADDRESS_SPACE_PPGTT_PD_ENTRY;
		break;
	case PPGTT_LEVEL1:
		space = ADDRESS_SPACE_PPGTT_ENTRY;
		break;
	case GGTT_LEVEL1:
		space = ADDRESS_SPACE_GTT_ENTRY;
		paddr = adjust_gsm_paddr(aub, true, paddr);
		break;
	}

	aub_write_mem_packet(aub, TILING_NONE, TYPE_NOTYPE, space, paddr,
			     entries, c * sizeof(*entries));
}

void i915_aub_context(struct intel_aub *aub, u8 class,
		      const struct drm_i915_error_page *pages, uint count)
{
	enum data_type_values type;

	switch (class) {
	default:
		MISSING_CASE(class);
	case OTHER_CLASS:
	case RENDER_CLASS:
		type = TYPE_LOGICAL_RING_CONTEXT_RCS;
		break;
	case VIDEO_DECODE_CLASS:
		type = TYPE_LOGICAL_RING_CONTEXT_VCS;
		break;
	case VIDEO_ENHANCEMENT_CLASS:
		type = TYPE_LOGICAL_RING_CONTEXT_VECS;
		break;
	case COPY_ENGINE_CLASS:
		type = TYPE_LOGICAL_RING_CONTEXT_BCS;
		break;
	}

	aub_write_discon_pages(aub, true, TILING_NONE, type,
			       ADDRESS_SPACE_PHYSICAL, pages, count);
}

void i915_aub_batchbuffer(struct intel_aub *aub, bool global_gtt,
			  const struct drm_i915_error_page *pages, uint count)
{
	aub_write_discon_pages(aub, global_gtt, TILING_NONE, TYPE_BATCH_BUFFER,
			       ADDRESS_SPACE_PHYSICAL, pages, count);
}

void i915_aub_buffer(struct intel_aub *aub, bool global_gtt, int tiling_mode,
		     const struct drm_i915_error_page *pages, uint count)
{
	enum tiling_values tiling;

	switch (tiling_mode) {
	default:
		MISSING_CASE(tiling_mode);
	case I915_TILING_NONE:
		tiling = TILING_NONE;
		break;
	case I915_TILING_X:
		tiling = TILING_X;
		break;
	case I915_TILING_Y:
		tiling = TILING_Y;
		break;
	}

	aub_write_discon_pages(aub, global_gtt, tiling, TYPE_NOTYPE,
			       ADDRESS_SPACE_PHYSICAL, pages, count);
}

void i915_aub_elsp_submit(struct intel_aub *aub, struct intel_engine_cs *engine,
			  u64 desc)
{
	i915_reg_t elsp = RING_ELSP(engine);
	i915_reg_t elsp_status = RING_EXECLIST_STATUS_LO(engine);
	u32 value;

	aub_write_register_packet(aub, elsp, 0x0);
	aub_write_register_packet(aub, elsp, 0x0);
	aub_write_register_packet(aub, elsp, upper_32_bits(desc));
	aub_write_register_packet(aub, elsp, lower_32_bits(desc));

	/*
	 * Due to the nature of the AUB file (no timing information), we cannot
	 * use it to model asynchronous things like Lite Restores or Preemption.
	 * This is the reason we use this "fake" ELSP submission with just one
	 * element at a time instead of just capturing the real submission. And
	 * also the reason why here we force the other end to wait until the HW
	 * becomes idle again.
	 */
	value = GEN8_CTX_STATUS_ACTIVE_IDLE << EL_STATUS_LAST_CTX_SWITCH_SHIFT;
	aub_write_regpoll_packet(aub, elsp_status, value, value);
}

void i915_aub_stop(struct intel_aub *aub)
{
	kfree(aub);
}
