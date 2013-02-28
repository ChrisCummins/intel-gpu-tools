/** @file gem_render_png_blits.c
 *
 * This test performs multiple blits, dumping the output into PNG files. It does
 * perform any consitency checking.
 *
 * The goal is to provide visual feedback for rendercopy testing.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendercopy.h"

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)
#define COUNT 8

static uint32_t testcolour;
static uint32_t testbuffer[WIDTH*HEIGHT];

/* Map GTT data into testbuffer. */
static void read_testbuffer(int file_descriptor, uint32_t handle)
{
	int ret;
	struct drm_i915_gem_pread gem_pread;

	gem_pread.handle = handle;
	gem_pread.offset = 0;
	gem_pread.size = sizeof(testbuffer);
	gem_pread.data_ptr = (uintptr_t)testbuffer;
	ret = drmIoctl(file_descriptor, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
	if (ret != 0) {
		fprintf(stderr, "DRM_IOCTL_I915_GEM_PREAD failed, ret=%d\n", ret);
		abort();
	}
}

/* Map the contents of testbuffer in GTT. */
static void write_testbuffer(int file_descriptor, uint32_t handle)
{
	int ret;
	struct drm_i915_gem_pwrite gem_pwrite;

	gem_pwrite.handle = handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = sizeof(testbuffer);
	gem_pwrite.data_ptr = (uintptr_t)testbuffer;
	ret = drmIoctl(file_descriptor, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
	if (ret != 0) {
		fprintf(stderr, "DRM_IOCTL_I915_GEM_PWRITE failed, ret=%d\n", ret);
		abort();
	}
}

/* Write the contents of testbuffer to a PNG file. */
static void testbuffer_to_png(const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t ret;

	surface = cairo_image_surface_create_for_data((void *)testbuffer,
						      CAIRO_FORMAT_ARGB32,
						      WIDTH, HEIGHT, STRIDE);
	ret = cairo_surface_write_to_png(surface, filename);
	if (ret != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "Unable to render testbuffer '%s'", filename);
		abort ();
	}
}

int main(int argc, char **argv)
{
	render_copyfunc_t render_copy;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batchbuffer;
	int file_descriptor, i, j, testcount = 0;

	/* Try and get the testcount from argument. */
	if (argc > 1)
		testcount = atoi(argv[1]);

	if (testcount < 1)
		testcount = COUNT;

	printf("Executing %d render_copy tests\n", testcount);

	file_descriptor = drm_open_any();

	render_copy = get_render_copyfunc(intel_get_drm_devid(file_descriptor));
	if (render_copy == NULL) {
		printf("No render-copy function, doing nothing\n");
		return 77;
	}

	bufmgr = drm_intel_bufmgr_gem_init(file_descriptor, 4096);
	batchbuffer = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(file_descriptor));

#ifdef ENABLE_AUB_DUMP
	if (drmtest_dump_aub()) {
		drm_intel_bufmgr_gem_set_aub_filename(bufmgr, "rendercopy.aub");
		drm_intel_bufmgr_gem_set_aub_dump(bufmgr, true);
	}
#endif

	for (i = 0; i < testcount; i++) {
		drm_intel_bo *src_bo, *dst_bo;
		struct scratch_buf src, dst;
		char filename[55];

		/* Initialise our buffer objects. */
		src_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);
		dst_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);

		/* Write a single colour to the testbuffer. We're using ARGB32
		 * format, so the 32 bit pixel values are: 0xAARRGGBB. */
		testcolour = 0xFFFF0000 - (i * 0x04110000);

		for (j = 0; j < WIDTH*HEIGHT; j++)
			testbuffer[j] = testcolour;

		write_testbuffer(file_descriptor, src_bo->handle);

		src.bo = src_bo;
		src.stride = STRIDE;
		src.tiling = I915_TILING_NONE;
		src.size = SIZE;

		dst.bo = dst_bo;
		dst.stride = STRIDE;
		dst.tiling = I915_TILING_NONE;
		dst.size = SIZE;

		render_copy(batchbuffer, &src, 0, 0, WIDTH, HEIGHT, &dst, 0, 0);

#ifdef ENABLE_AUB_DUMP
		if (drmtest_dump_aub()) {
			/* If we're going to be dumping an AUB file, then we can
			 * stop here. */
			drm_intel_gem_bo_aub_dump_bmp(dst.bo,
				0, 0, WIDTH, HEIGHT,
				AUB_DUMP_BMP_FORMAT_ARGB_8888,
				STRIDE, 0);
			drm_intel_bufmgr_gem_set_aub_dump(bufmgr, false);
			printf("Generated AUB file: rendercopy.aub\n");
			return 0;
		}
#endif

		read_testbuffer(file_descriptor, dst_bo->handle);

		snprintf(filename, sizeof(filename), "blit-%d.png", i + 1);
		testbuffer_to_png(filename);
		printf("Created: %s\n", filename);
	}

	return 0;
}
