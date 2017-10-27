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

int i915_error_state_to_aub(struct drm_i915_error_state_buf *m,
			    const struct i915_gpu_state *error)
{
	return 0;
}
