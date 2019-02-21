/*
 * Copyright © 2013,2014 Intel Corporation
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

#include "igt.h"
#include <math.h>

IGT_TEST_DESCRIPTION("Test crtc background color feature");

/*
 * Paints a desired color into a full-screen primary plane and then compares
 * that CRC with turning off all planes and setting the CRTC background to the
 * same color.
 *
 * At least on current Intel platforms, the CRC tests appear to always fail,
 * even though the resulting pipe output seems to be the same.  The bspec tells
 * us that we must have at least one plane turned on when taking a pipe-level
 * CRC, so the fact that we're violating that may explain the failures.  If
 * your platform gives failures for all tests, you may want to run with
 * --interactive-debug=bgcolor --skip-crc-compare to visually inspect that the
 * background color matches the equivalent solid plane.
 */

typedef struct {
	igt_display_t display;
	int gfx_fd;
	igt_output_t *output;
	igt_pipe_crc_t *pipe_crc;
	drmModeModeInfo *mode;
} data_t;

/*
 * Local copy of kernel uapi
 */
static inline __u64
local_argb(__u8 bpc, __u16 alpha, __u16 red, __u16 green, __u16 blue)
{
       int msb_shift = 16 - bpc;

       return (__u64)alpha << msb_shift << 48 |
              (__u64)red   << msb_shift << 32 |
              (__u64)green << msb_shift << 16 |
              (__u64)blue  << msb_shift;
}

static void test_background(data_t *data, enum pipe pipe, int w, int h,
			    __u64 r, __u64 g, __u64 b)
{
	igt_crc_t plane_crc, bg_crc;
	struct igt_fb colorfb;
	igt_plane_t *plane = igt_output_get_plane_type(data->output,
						       DRM_PLANE_TYPE_PRIMARY);


	/* Fill the primary plane and set the background to the same color */
	igt_create_color_fb(data->gfx_fd, w, h, DRM_FORMAT_XRGB2101010,
			    LOCAL_DRM_FORMAT_MOD_NONE,
			    (double)r / 0xFFFF,
			    (double)g / 0xFFFF,
			    (double)b / 0xFFFF,
			    &colorfb);
	igt_plane_set_fb(plane, &colorfb);
	igt_pipe_set_prop_value(&data->display, pipe, IGT_CRTC_BACKGROUND,
				local_argb(16, 0xffff, r, g, b));
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
	igt_pipe_crc_collect_crc(data->pipe_crc, &plane_crc);
	igt_debug_wait_for_keypress("bgcolor");

	/* Turn off the primary plane so only bg shows */
	igt_plane_set_fb(plane, NULL);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
	igt_pipe_crc_collect_crc(data->pipe_crc, &bg_crc);
	igt_debug_wait_for_keypress("bgcolor");

	/*
	 * The following test relies on hardware that generates valid CRCs
	 * even when no planes are on.  Sadly, this doesn't appear to be the
	 * case for current Intel platforms; pipe CRC's never match bgcolor
	 * CRC's, likely because we're violating the bspec's guidance that there
	 * must always be at least one real plane turned on when taking the
	 * pipe-level ("dmux") CRC.
	 */
	igt_assert_crc_equal(&plane_crc, &bg_crc);
}

igt_main
{
	data_t data = {};
	igt_output_t *output;
	drmModeModeInfo *mode;
	int w, h;
	enum pipe pipe;

	igt_fixture {
		data.gfx_fd = drm_open_driver_master(DRIVER_ANY);
		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc(data.gfx_fd);
		igt_display_require(&data.display, data.gfx_fd);
	}

	for_each_pipe_static(pipe) igt_subtest_group {
		igt_fixture {
			igt_display_require_output_on_pipe(&data.display, pipe);
			igt_require(igt_pipe_has_prop(&data.display, pipe,
						      IGT_CRTC_BACKGROUND));

			output = igt_get_single_output_for_pipe(&data.display,
								pipe);
			igt_output_set_pipe(output, pipe);
			data.output = output;

			mode = igt_output_get_mode(output);
			w = mode->hdisplay;
			h = mode->vdisplay;

			data.pipe_crc = igt_pipe_crc_new(data.gfx_fd, pipe,
							  INTEL_PIPE_CRC_SOURCE_AUTO);
		}

		igt_subtest_f("background-color-pipe-%s-black",
			      kmstest_pipe_name(pipe))
			test_background(&data, pipe, w, h,
					0, 0, 0);

		igt_subtest_f("background-color-pipe-%s-white",
			      kmstest_pipe_name(pipe))
			test_background(&data, pipe, w, h,
					0xFFFF, 0xFFFF, 0xFFFF);

		igt_subtest_f("background-color-pipe-%s-red",
			      kmstest_pipe_name(pipe))
			test_background(&data, pipe, w, h,
					0xFFFF, 0, 0);

		igt_subtest_f("background-color-pipe-%s-green",
			      kmstest_pipe_name(pipe))

			test_background(&data, pipe, w, h,
					0, 0xFFFF, 0);

		igt_subtest_f("background-color-pipe-%s-blue",
			      kmstest_pipe_name(pipe))
			test_background(&data, pipe, w, h,
					0, 0, 0xFFFF);

		igt_fixture {
			igt_display_reset(&data.display);
			igt_pipe_crc_free(data.pipe_crc);
			data.pipe_crc = NULL;
		}
	}

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
