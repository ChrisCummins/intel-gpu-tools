/** @file gem_render_png_blits.c
 *
 * This test duplicates an input .png image from <src> to <dest> by performing a
 * render_copy blit.
 *
 *   Usage:  gem_render_png_dup <src> <dest>
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

static uint8_t *testdata;
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

/* Copy the contents of input file into testbuffer. */
static void write_data_to_buffer(cairo_surface_t *surface)
{
	int stride, j, i;

	stride = cairo_image_surface_get_stride(surface);

	for (j = 0; j < HEIGHT; j++)
	{
		uint32_t *row = (uint32_t *)(testdata + j * stride);
		for (i = 0; i < WIDTH; i++)
			testbuffer[j * HEIGHT + i] = *(row + i);
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
	char *src_png, *dst_png;
	render_copyfunc_t render_copy;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batchbuffer;
	cairo_surface_t *surface;
	drm_intel_bo *src_bo, *dst_bo;
	struct scratch_buf src, dst;
	int file_descriptor;

	/* Try and get the testcount from argument. */
	if (argc != 3) {
		fprintf(stderr, "Usage: gem_render_png_dup <src> <dst>\n");
		return 1;
	}

	src_png = argv[1];
	dst_png = argv[2];

	file_descriptor = drm_open_any();

	render_copy = get_render_copyfunc(intel_get_drm_devid(file_descriptor));
	if (render_copy == NULL) {
		printf("No render-copy function, doing nothing\n");
		return 77;
	}

	surface = cairo_image_surface_create_from_png(src_png);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "Unable to open PNG '%s'\n", src_png);
		return 1;
	}
	if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_RGB24) {
		fprintf(stderr, "Incorrect image format, expected: CAIRO_FORMAT_RGB24\n");
		return 1;
	}
	if (cairo_image_surface_get_width(surface) < WIDTH ||
	    cairo_image_surface_get_height(surface) < HEIGHT) {
		fprintf(stderr, "Input image must be at least 512x512!\n");
		return 1;
	}

	testdata = cairo_image_surface_get_data(surface);

	bufmgr = drm_intel_bufmgr_gem_init(file_descriptor, 4096);
	batchbuffer = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(file_descriptor));

#ifdef ENABLE_AUB_DUMP
	if (drmtest_dump_aub()) {
		drm_intel_bufmgr_gem_set_aub_filename(bufmgr, "rendercopy.aub");
		drm_intel_bufmgr_gem_set_aub_dump(bufmgr, true);
	}
#endif

	src_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);
	dst_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);

	write_data_to_buffer(surface);

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

	testbuffer_to_png(dst_png);
	printf("Created: '%s'\n", dst_png);

	return 0;
}
