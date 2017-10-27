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

#ifndef _INTEL_AUBCAPTURE_FORMAT_H_
#define _INTEL_AUBCAPTURE_FORMAT_H_

#pragma pack(push, 4)

#define AUB_FILE_FORMAT_VERSION 0

#define CMD_TYPE_AUB	0x7

#define CMD_OPC_MEMTRACE	0x2e

#define CMD_SUBOPC_MEMTRACE_VERSION			0xe
#define CMD_SUBOPC_MEMTRACE_COMMENT			0x8
#define CMD_SUBOPC_MEMTRACE_REGISTER_POLL		0x2
#define CMD_SUBOPC_MEMTRACE_REGISTER_WRITE		0x3
#define CMD_SUBOPC_MEMTRACE_MEMORY_WRITE		0x6
#define CMD_SUBOPC_MEMTRACE_MEMORY_WRITE_DISCONTIGUOUS	0xb

/**
 * struct aub_cmd_hdr - AUB command header
 */
struct aub_cmd_hdr {
	/** @dword_count: The number of dwords in the command not including the
	 * first dword */
	uint32_t dword_count : 16;
	uint32_t sub_opcode  : 7;
	uint32_t opcode      : 6;
	uint32_t type        : 3;
};

enum stepping_values {
	STEP_A = 0, STEP_B, STEP_C, STEP_D, STEP_E, STEP_F, STEP_G, STEP_H,
	STEP_I, STEP_J, STEP_K, STEP_L, STEP_M, STEP_N, STEP_O, STEP_P, STEP_Q,
	STEP_R, STEP_S, STEP_T, STEP_U, STEP_V, STEP_W, STEP_X, STEP_Y, STEP_Z
};

enum device_values {
	DEV_BDW = 11,
	DEV_CHV = 13,
	DEV_SKL = 12,
	DEV_BXT = 14,
	DEV_KBL = 16,
	DEV_GLK = 17,
	DEV_CNL = 15,
};

enum swizzling_values {
	SWIZZLING_ENABLED = 1,
	SWIZZLING_DISABLED = 0
};

enum recording_method_values {
	METHOD_PHY = 1,
	METHOD_GFX = 0
};

enum pch_values {
	PCH_DEFAULT = 0
};

enum capture_tool_values {
	CAPTURE_TOOL_KMD = 1
};

/**
 * struct cmd_memtrace_version - first packet to appear on the AUB file (kind of
 * a file header).
 *
 * Includes version information about the memtrace file that contains it.
 */
struct cmd_memtrace_version {
	struct aub_cmd_hdr header;

	/** @memtrace_file_version: memtrace file format version. */
	uint32_t memtrace_file_version;

	struct {
		/** @metal: Which HW metal the memtrace file was generated on */
		uint32_t metal            : 3;
		/** @stepping: Which HW stepping the memtrace file was generated
		 * on. One of  enum stepping_values */
		uint32_t stepping         : 5;
		/** @device: Which device the memtrace file was generated on.
		 * One of enum device_values */
		uint32_t device           : 8;
		/** @swizzling: Which swizzling the data is in. One of enum
		 * swizzling_values */
		uint32_t swizzling        : 2;
		/** @recording_method: Which recording method was used.
		 * One of enum recording_method_values */
		uint32_t recording_method : 2;
		/** @pch: Which PCH was used. One of enum pch_values */
		uint32_t pch              : 8;
		/** @capture_tool: Which tool generated the memtrace file. One
		 * of enum capture_tool_values */
		uint32_t capture_tool     : 4;
	};

	/** @tool_primary_version: The primary version number for the capture
	 * tool used. */
	uint32_t tool_primary_version;

	/** @tool_secondary_version: The secondary version number for the
	 * capture tool used. */
	uint32_t tool_secondary_version;

	/**
	 * @command_line: Command line used to generate the memtrace file (N
	 * dwords). If this string is not 4 byte aligned it has to be padded
	 * with 0s at the end.
	 */
	char command_line[4];
};

/**
 * struct cmd_memtrace_comment - A comment in the AUB file.
 *
 * Free-style text, can be used for a number of reasons.
 */
struct cmd_memtrace_comment {
	struct aub_cmd_hdr header;

	uint32_t reserved;

	/**
	 * @comment: A comment that should be printed to console (N dwords).
	 * If this string is not 4 byte aligned it has to be padded with 0s
	 * at the end.
	 */
	char comment[4];
};

enum message_source_values {
	SOURCE_IA = 0
};

enum register_size_values {
	SIZE_BYTE =  0,
	SIZE_WORD =  1,
	SIZE_DWORD = 2,
	SIZE_QWORD = 3,
};

enum register_space_values {
	SPACE_MMIO = 0,
	SPACE_PCI = 2,
};

struct cmd_memtrace_register_write {
	struct aub_cmd_hdr header;

	/** @register_offset: The offset in the selected register space. For
	 * PCI configuration registers this offset field is split into four
	 * sub-fields: [31:16] is the bus number, [15:11] is the device number,
	 * [10:8] is the function number, and [7:0] is the register offset. */
	union {
		uint32_t register_offset;
		struct {
			uint32_t bus      : 16;
			uint32_t device   : 5;
			uint32_t function : 3;
			uint32_t offset   : 8;
		};
	};

	struct {
		uint32_t                  : 4;
		/** @message_source: Origin of the register write. One of enum
		 * message_source_values */
		uint32_t message_source   : 4;
		uint32_t                  : 8;
		/** @register_size: Size of the data. One of enum
		 * register_size_values */
		uint32_t register_size    : 4;
		uint32_t                  : 8;
		/** @register_space: Which register space to use. One of enum
		 * register_space_values */
		uint32_t register_space   : 4;
	};

	uint32_t write_mask_low;

	/** @write_mask_high: ignored if register_size is not QWORD. */
	uint32_t write_mask_high;

	/** @data: The data that is expected from the register write. */
	uint32_t data[1];
};

enum operation_type_values {
	OPERATION_TYPE_NORMAL =         0,
	OPERATION_TYPE_INTERLACED_CRC = 1,
};

struct cmd_memtrace_register_poll {
	struct aub_cmd_hdr header;

	/** @register_offset: The offset in the selected register space. For
	 * PCI configuration registers this offset field is split into four
	 * sub-fields: [31:16] is the bus number, [15:11] is the device number,
	 * [10:8] is the function number, and [7:0] is the register offset. */
	union {
		uint32_t register_offset;
		struct {
			uint32_t bus      : 16;
			uint32_t device   : 5;
			uint32_t function : 3;
			uint32_t offset   : 8;
		};
	};

	struct {
		uint32_t                  : 1;
		/** @timeout_action: Abort if the timeout expires? */
		uint32_t abort_on_timeout : 1;
		/** @poll_not_equal: Poll until value != target */
		uint32_t poll_not_equal   : 1;
		uint32_t                  : 1;
		/** @operation_type: One of operation_type_values */
		uint32_t operation_type   : 4;
		uint32_t                  : 8;
		/** @register_size: Size of the data. One of enum
		 * register_size_values */
		uint32_t register_size    : 4;
		uint32_t                  : 8;
		/** @register_space: Which register space to use. One of enum
		 * register_space_values */
		uint32_t register_space   : 4;
	};

	uint32_t poll_mask_low;

	/** @write_mask_high: ignored if register_size is not QWORD. */
	uint32_t poll_mask_high;

	/** @data: The data that is expected from the register read. */
	uint32_t data[1];
};

enum tiling_values {
	TILING_NONE = 0,
	TILING_X = 1,
	TILING_Y = 2,
};

enum data_type_values {
	TYPE_NOTYPE = 0,
	TYPE_BATCH_BUFFER = 1,
	TYPE_LOGICAL_RING_CONTEXT_RCS = 48,
	TYPE_LOGICAL_RING_CONTEXT_BCS = 49,
	TYPE_LOGICAL_RING_CONTEXT_VCS = 50,
	TYPE_LOGICAL_RING_CONTEXT_VECS = 51,
};

enum address_space_values {
	ADDRESS_SPACE_PHYSICAL = 2,
	ADDRESS_SPACE_GTT_GFX  = 0,
	ADDRESS_SPACE_GTT_ENTRY = 4,
	ADDRESS_SPACE_PPGTT_GFX = 5,
	ADDRESS_SPACE_PPGTT_PML4_ENTRY = 10,
	ADDRESS_SPACE_PPGTT_PDP_ENTRY = 8,
	ADDRESS_SPACE_PPGTT_PD_ENTRY = 9,
	ADDRESS_SPACE_PPGTT_ENTRY = 6,
};

struct cmd_memtrace_memwrite {
	struct aub_cmd_hdr header;

	/** @address: The address of the memory to read. The address space is
	 * determined by the address_space field. */
	uint64_t address;

	struct {
		uint32_t                    : 2;
		/** @tiling: Tiling format. One of enum tiling_values */
		uint32_t tiling             : 2;
		uint32_t                    : 16;
		/** @data_type_hint: This parameter specifies the type of data
		 * block that follows. One of enum data_type_values. If it isn't
		 * known mark it as TYPE_NOTYPE */
		uint32_t data_type_hint     : 8;
		/** @address_space: This parameter specifies the type of memory
		 * corresponding to the data block (GTT-relative, physical
		 * local, physical system, etc...). One of enum
		 * address_space_values */
		uint32_t address_space      : 4;
	};

	/** @data_size: The number of bytes that will be written. The data
	 * elements are packed into dwords in the data parameter, padded with
	 * zeroes */
	uint32_t data_size;

	/** @data: The data that will be written. */
	uint32_t data[1];
};

#define DISCONTIGUOUS_WRITE_MAX_ELEMENTS 63

struct memwrite_element {
	/** @address: The address of the memory to read. */
	uint64_t address;
	/** @data_size: The number of bytes that will be written. */
	uint32_t data_size;
};

struct cmd_memtrace_memwrite_discon {
	struct aub_cmd_hdr header;

	struct aub_cmd_memwrite_discon_opts {
		uint32_t                    : 2;
		/** @tiling: Tiling format. One of enum tiling_values */
		uint32_t tiling             : 2;

		/** @tiling: Number of address and data_size pairs */
		uint32_t number_of_elements : 16;
		/** @data_type_hint: This parameter specifies the type of data
		 * block that follows. One of enum data_type_values. If it isn't
		 * known mark it as TYPE_NOTYPE */
		uint32_t data_type_hint     : 8;
		/** @address_space: This parameter specifies the type of memory
		 * corresponding to the data block (GTT-relative, physical
		 * local, physical system, etc...). One of enum
		 * address_space_values */
		uint32_t address_space      : 4;
	} opts;

	struct memwrite_element elements[DISCONTIGUOUS_WRITE_MAX_ELEMENTS];

	/** @data: The data that will be written. */
	uint32_t data[1];
};

#pragma pack(pop)

#endif
