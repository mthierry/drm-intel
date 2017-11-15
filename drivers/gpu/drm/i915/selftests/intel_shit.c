/*
 * Copyright © 2016 Intel Corporation
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

#include <linux/kthread.h>

#include "../i915_selftest.h"

#include "mock_context.h"
#include "mock_drm.h"


static int shit_active_engine(void *data)
{
	struct intel_engine_cs *engine = data;
	struct i915_request *rq[2] = {};
	struct i915_gem_context *ctx[2];
	struct drm_file *file;
	unsigned long count = 0;
	int err = 0;

	file = mock_file(engine->i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&engine->i915->drm.struct_mutex);
	ctx[0] = live_context(engine->i915, file);
	mutex_unlock(&engine->i915->drm.struct_mutex);
	if (IS_ERR(ctx[0])) {
		err = PTR_ERR(ctx[0]);
		goto err_file;
	}

	mutex_lock(&engine->i915->drm.struct_mutex);
	ctx[1] = live_context(engine->i915, file);
	mutex_unlock(&engine->i915->drm.struct_mutex);
	if (IS_ERR(ctx[1])) {
		err = PTR_ERR(ctx[1]);
		i915_gem_context_put(ctx[0]);
		goto err_file;
	}

	while (!kthread_should_stop()) {
		unsigned int idx = count++ & 1;
		struct i915_request *old = rq[idx];
		struct i915_request *new;

		mutex_lock(&engine->i915->drm.struct_mutex);
		new = i915_request_alloc(engine, ctx[idx]);
		if (IS_ERR(new)) {
			mutex_unlock(&engine->i915->drm.struct_mutex);
			err = PTR_ERR(new);
			break;
		}

		rq[idx] = i915_request_get(new);
		i915_request_add(new);
		mutex_unlock(&engine->i915->drm.struct_mutex);

		if (old) {
			i915_request_wait(old, 0, MAX_SCHEDULE_TIMEOUT);
			i915_request_put(old);
		}
	}

	for (count = 0; count < ARRAY_SIZE(rq); count++)
		i915_request_put(rq[count]);

err_file:
	mock_file_free(engine->i915, file);
	return err;
}

static int igt_active_engines(void *arg, bool do_reset)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine, *active;
	enum intel_engine_id id, tmp;
	int err = 0;

	/* Check that issuing a reset on one engine does not interfere
	 * with any other engine.
	 */

	if (!intel_has_reset_engine(i915))
		return 0;

	pr_info("do_reset = %s\n", yesno(do_reset));
	for_each_engine(engine, i915, id) {
		struct task_struct *threads[I915_NUM_ENGINES];
		unsigned long resets[I915_NUM_ENGINES];
		unsigned long global = i915_reset_count(&i915->gpu_error);
		IGT_TIMEOUT(end_time);

		memset(threads, 0, sizeof(threads));
		for_each_engine(active, i915, tmp) {
			struct task_struct *tsk;

			if (active == engine)
				continue;

			resets[tmp] = i915_reset_engine_count(&i915->gpu_error,
							      active);

			tsk = kthread_run(shit_active_engine, active,
					  "igt/%s", active->name);
			if (IS_ERR(tsk)) {
				err = PTR_ERR(tsk);
				goto unwind;
			}

			threads[tmp] = tsk;
			get_task_struct(tsk);
		}

		set_bit(I915_RESET_ENGINE + engine->id, &i915->gpu_error.flags);
		do {
			if (do_reset) {
				err = i915_reset_engine(engine, NULL);
				if (err) {
					pr_err("i915_reset_engine(%s) failed, err=%d\n",
				 	      engine->name, err);
					break;
				}
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + engine->id,
			  &i915->gpu_error.flags);

unwind:
		for_each_engine(active, i915, tmp) {
			int ret;

			if (!threads[tmp])
				continue;

			ret = kthread_stop(threads[tmp]);
			if (ret) {
				pr_err("kthread for active engine %s failed, err=%d\n",
				       active->name, ret);
				if (!err)
					err = ret;
			}
			put_task_struct(threads[tmp]);

			if (resets[tmp] != i915_reset_engine_count(&i915->gpu_error,
								   active)) {
				pr_err("Innocent engine %s was reset (count=%ld)\n",
				       active->name,
				       i915_reset_engine_count(&i915->gpu_error,
							       active) - resets[tmp]);
				err = -EIO;
			}
		}

		if (global != i915_reset_count(&i915->gpu_error)) {
			pr_err("Global reset (count=%ld)!\n",
			       i915_reset_count(&i915->gpu_error) - global);
			err = -EIO;
		}

		if (err)
			break;

		cond_resched();
	}

	if (i915_terminally_wedged(&i915->gpu_error))
		err = -EIO;

	return err;
}


static int igt_active_engines_reset(void *arg)
{
	return igt_active_engines(arg, true);
}

static int igt_active_engines_no_reset(void *arg)
{
	return igt_active_engines(arg, false);
}


int intel_shit_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_active_engines_reset),
		SUBTEST(igt_active_engines_no_reset),
	};
	int err;

	if (!intel_has_gpu_reset(i915))
		return 0;

	intel_runtime_pm_get(i915);

	err = i915_subtests(tests, i915);

	intel_runtime_pm_put(i915);

	return err;
}
