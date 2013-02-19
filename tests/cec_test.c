#include "rendercopy.h"

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)
#define COUNT 16

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

/* Verify that the contents of testbuffer match the value of testcolour. */
static void testbuffer_check(void)
{
	int i;

	for (i = 0; i < WIDTH*HEIGHT; i++) {
		if (testbuffer[i] != testcolour) {
			fprintf(stderr, "Expected 0x%08x, found 0x%08x "
				"at offset 0x%08x\n",
				testcolour, testbuffer[i], i * 4);
			abort();
		}
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

	if (argc > 1)
		testcount = atoi(argv[1]);

	if (testcount < 1)
		testcount = COUNT;

	printf("%s: Executing %d render_copy tests\n", argv[0], testcount);

	file_descriptor = drm_open_any();

	render_copy = get_render_copyfunc(intel_get_drm_devid(file_descriptor));
	if (render_copy == NULL) {
		printf("No render-copy function, doing nothing\n");
		return 77;
	}

	bufmgr = drm_intel_bufmgr_gem_init(file_descriptor, 4096);
	batchbuffer = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(file_descriptor));

	for (i = 0; i < testcount; i++) {
		drm_intel_bo *src_bo, *dst_bo;
		struct scratch_buf src, dst;
		char filename[20];

		/* Initialise our buffer objects. */
		src_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);
		dst_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);

		/* Write a single colour to the testbuffer. We're using RGB24
		 * format, so the 32 bit pixel values are: 0xRRGGBB (upper eight
		 * bits are unused). */
		testcolour = 0xFFFF0000 - (i * 0x04110000);

		for (j = 0; j < WIDTH*HEIGHT; j++)
			testbuffer[j] = testcolour;

		/* Write testbuffer to source bufferobject. */
		write_testbuffer(file_descriptor, src_bo->handle);

		/* Check that our testbuffer write worked. */
		/* read_testbuffer(file_descriptor, src_bo->handle); */
		/* testbuffer_check(); */

		/* Set up our scratch buffers. */
		src.bo = src_bo;
		src.stride = STRIDE;
		src.tiling = I915_TILING_NONE;
		src.size = SIZE;

		dst.bo = dst_bo;
		dst.stride = STRIDE;
		dst.tiling = I915_TILING_NONE;
		dst.size = SIZE;

		/* We now have two identical bufferobjects, 'src' and
		 * 'dest'. src is populated with value testcolour and dst is
		 * uninitialised. Now we copy the contents of src into dst so
		 * that their contents match. */
		render_copy(batchbuffer, &src, 0, 0, WIDTH, HEIGHT, &dst, 0, 0);

		/* Verify that the render_copy worked and that both buffers
		 * contain the correct pixel values, and then render PNGs. */
		/* read_testbuffer(file_descriptor, src_bo->handle); */
		/* testbuffer_check(); */
		/* snprintf(filename, sizeof(filename), "%d-src.png", i/2+1); */
		/* testbuffer_to_png(filename); */

		read_testbuffer(file_descriptor, dst_bo->handle);
		/* testbuffer_check(); */
		snprintf(filename, sizeof(filename), "%d-dst.png", i/2+1);
		testbuffer_to_png(filename);
	}

	return 0;
}
