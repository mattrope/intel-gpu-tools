/*
 * Copyright © 2015 Intel Corporation
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
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"

IGT_TEST_DESCRIPTION("Simple test of nuclear pageflip functionality");

typedef struct {
	int drm_fd;
	igt_display_t display;
} data_t;

typedef struct {
	data_t *data;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t crc_1, crc_2, crc_3, crc_4, crc_5, crc_6, crc_7, crc_8,
		  crc_9, crc_10;
	struct igt_fb red_fb, blue_fb, black_fb, yellow_fb;
	drmModeModeInfo *mode;
} functional_test_t;

typedef struct {
	data_t *data;
	drmModeResPtr moderes;
	struct igt_fb blue_fb, oversized_fb, undersized_fb;
} sanity_test_t;

typedef struct {
	data_t *data;
	struct igt_fb red_fb, blue_fb;
} pageflip_test_t;

static void
functional_test_init(functional_test_t *test, igt_output_t *output, enum pipe pipe)
{
	data_t *data = test->data;
	drmModeModeInfo *mode;

	test->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				0.0, 0.0, 0.0,
				&test->black_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				0.0, 0.0, 1.0,
				&test->blue_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				1.0, 1.0, 0.0,
				&test->yellow_fb);
	igt_create_color_fb(data->drm_fd, 100, 100,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				1.0, 0.0, 0.0,
				&test->red_fb);

	test->mode = mode;
}

static void
functional_test_fini(functional_test_t *test, igt_output_t *output)
{
	igt_pipe_crc_free(test->pipe_crc);

	igt_remove_fb(test->data->drm_fd, &test->black_fb);
	igt_remove_fb(test->data->drm_fd, &test->blue_fb);
	igt_remove_fb(test->data->drm_fd, &test->red_fb);
	igt_remove_fb(test->data->drm_fd, &test->yellow_fb);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit2(&test->data->display, COMMIT_LEGACY);
}

/*
 * Nuclear pageflip functional testing.
 *   - Black primary plane via traditional interfaces, red sprite, grab CRC:1.
 *   - Blue primary plane via traditional interfaces, red sprite, grab CRC:2.
 *   - Yellow primary via traditional interfaces
 *   - Blue primary plane, red sprite via universal planes, grab CRC:3 and compare
 *     with CRC:2 (should be the same)
 *   - Disable primary plane, grab CRC:4 (should be same as CRC:1)
 *   - Reenable primary, grab CRC:5 (should be same as CRC:2 and CRC:3)
 *   - Yellow primary, no sprite
 *   - Disable CRTC
 *   - Program red sprite (while CRTC off)
 *   - Program blue primary (while CRTC off)
 *   - Enable CRTC, grab CRC:6 (should be same as CRC:2)
 */
static void
functional_test_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	functional_test_t test = { .data = data };
	igt_display_t *display = &data->display;
	igt_plane_t *primary, *sprite;

	igt_skip_on(pipe >= display->n_pipes);

	igt_info("Testing connector %s using pipe %s\n", igt_output_name(output),
		 kmstest_pipe_name(pipe));

	functional_test_init(&test, output, pipe);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	sprite = igt_output_get_plane(output, IGT_PLANE_2);
	if (!sprite) {
		functional_test_fini(&test, output);
		igt_skip("No sprite plane available\n");
	}

	igt_plane_set_position(sprite, 100, 100);

	/* Step 1: Legacy API's, black primary, red sprite (CRC 1) */
	igt_plane_set_fb(primary, &test.black_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_1);

	/* Step 2: Legacy API', blue primary, red sprite (CRC 2) */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_2);

	/* Step 3: Legacy API's, yellow primary (CRC 3) */
	igt_plane_set_fb(primary, &test.yellow_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_3);

	/* Step 4: Nuclear pageflip, blue primary, red sprite (CRC 4) */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_NUCLEAR);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_4);

	/* Step 5: Nuclear pageflip, disable primary plane (CRC 5) */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_NUCLEAR);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_5);

	/* Step 6: Nuclear pageflip, re-enable primary with blue (CRC 6) */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(display, COMMIT_NUCLEAR);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_6);

	/* Step 7: Disable CRTC */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_LEGACY);

	/* Blue bg + red sprite should be same under both types of API's */
	igt_assert(igt_crc_equal(&test.crc_2, &test.crc_4));

	/* Disabling primary plane should be same as black primary */
	igt_assert(igt_crc_equal(&test.crc_1, &test.crc_5));

	/* Re-enabling primary should return to blue properly */
	igt_assert(igt_crc_equal(&test.crc_2, &test.crc_6));

	igt_plane_set_fb(primary, NULL);
	igt_plane_set_fb(sprite, NULL);

	functional_test_fini(&test, output);
}

static void
sanity_test_init(sanity_test_t *test, igt_output_t *output, enum pipe pipe)
{
	data_t *data = test->data;
	drmModeModeInfo *mode;

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->blue_fb);
	igt_create_color_fb(data->drm_fd,
			    mode->hdisplay + 100, mode->vdisplay + 100,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->oversized_fb);
	igt_create_color_fb(data->drm_fd,
			    mode->hdisplay - 100, mode->vdisplay - 100,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->undersized_fb);

	test->moderes = drmModeGetResources(data->drm_fd);
}

static void
sanity_test_fini(sanity_test_t *test, igt_output_t *output)
{
	drmModeFreeResources(test->moderes);

	igt_remove_fb(test->data->drm_fd, &test->oversized_fb);
	igt_remove_fb(test->data->drm_fd, &test->undersized_fb);
	igt_remove_fb(test->data->drm_fd, &test->blue_fb);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit2(&test->data->display, COMMIT_LEGACY);
}

static void
sanity_test_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	sanity_test_t test = { .data = data };
	igt_plane_t *primary;
	int ret = 0;

	igt_skip_on(pipe >= data->display.n_pipes);

	igt_output_set_pipe(output, pipe);

	sanity_test_init(&test, output, pipe);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

	/* Use legacy API to set a mode with a blue FB */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(&data->display, COMMIT_LEGACY);

	/*
	 * Try to use nuclear pageflip API to set primary plane that
	 * doesn't cover CRTC (should fail).
	 */
	igt_plane_set_fb(primary, &test.undersized_fb);
	ret = igt_display_try_commit2(&data->display, COMMIT_NUCLEAR);
	igt_assert(ret == -EINVAL);

	/* Same as above, but different plane positioning. */
	igt_plane_set_position(primary, 100, 100);
	ret = igt_display_try_commit2(&data->display, COMMIT_NUCLEAR);
	igt_assert(ret == -EINVAL);

	igt_plane_set_position(primary, 0, 0);
	igt_plane_set_fb(primary, NULL);
	sanity_test_fini(&test, output);
}

static void
run_tests_for_pipe(data_t *data, enum pipe pipe)
{
	igt_output_t *output;

	igt_subtest_f("nuclear-pageflip-pipe-%s-functional",
		      kmstest_pipe_name(pipe))
		for_each_connected_output(&data->display, output)
			functional_test_pipe(data, pipe, output);

	igt_subtest_f("nuclear-pageflip-pipe-%s-sanity",
		      kmstest_pipe_name(pipe))
		for_each_connected_output(&data->display, output)
			sanity_test_pipe(data, pipe, output);
}

static data_t data;

igt_main
{

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any_master();

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();
		igt_display_init(&data.display, data.drm_fd);

		igt_require(data.display.has_universal_planes);
		igt_require(data.display.has_atomic_props);
	}

	for (int pipe = 0; pipe < 3; pipe++)
		run_tests_for_pipe(&data, pipe);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
