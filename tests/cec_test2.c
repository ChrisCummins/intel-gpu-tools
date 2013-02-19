#include "rendercopy.h"

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)
#define COUNT 16

static unsigned char *testdata;
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
		uint32_t *row;

		row = (uint32_t *)(testdata + j * stride);
		for (i = 0; i < WIDTH; i++)
			testbuffer[j * HEIGHT + i] = *(row + i);
	}
}

/* Verify that the contents of testbuffer match the value of testcolour. */
static void testbuffer_check(cairo_surface_t *surface)
{
	int stride, j, i;

	stride = cairo_image_surface_get_stride(surface);

	for (j = 0; j < HEIGHT; j++) {
		uint32_t *row;

		row = (uint32_t *)(testdata + j * stride);
		for (i = 0; i < WIDTH; i++) {
			uint32_t data_pixel, buffer_pixel;

			data_pixel = *(row + i);
			buffer_pixel = testbuffer[j * HEIGHT + i];
			if (buffer_pixel != data_pixel) {
				fprintf(stderr, "Expected 0x%08x, "
					"found 0x%08x at 0x%08x\n", data_pixel,
					buffer_pixel, j*WIDTH+i);
				abort();
			}
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
	char *input_file = "rendercopy-input.png";
	render_copyfunc_t render_copy;
	drm_intel_bufmgr *bufmgr;
	drm_intel_bo *src_bo, *dst_bo;
	struct scratch_buf src, dst;
	struct intel_batchbuffer *batchbuffer;
	cairo_surface_t *surface;
	int file_descriptor;

	printf("Executing rendercopy_gen7 test\n");

	if (argc > 1)
		input_file = argv[1];

	file_descriptor = drm_open_any();

	render_copy = get_render_copyfunc(intel_get_drm_devid(file_descriptor));
	if (render_copy == NULL) {
		fprintf(stderr, "No render-copy function, doing nothing\n");
		return 77;
	}

	/* Get our input data. We attempt to open the file 'input_file', and
	 * load the a WIDTHxHEIGHT square from the top left of it.
	 */
	surface = cairo_image_surface_create_from_png(input_file);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "Unable to open PNG '%s'\n", input_file);
		return 1;
	}

	if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_RGB24) {
		fprintf(stderr, "Incorrect image format'\n");
		return 2;
	}

	if (cairo_image_surface_get_width(surface) < WIDTH ||
	    cairo_image_surface_get_height(surface) < HEIGHT) {
		fprintf(stderr, "Input image must be at least 512x512!\n");
		return 3;
	}

	testdata = cairo_image_surface_get_data(surface);

	bufmgr = drm_intel_bufmgr_gem_init(file_descriptor, 4096);
	batchbuffer = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(file_descriptor));

	/* Initialise our buffer objects. */
	src_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);
	dst_bo = drm_intel_bo_alloc(bufmgr, "", SIZE, 4096);

	/* Populate our buffer with data. */
	write_data_to_buffer(surface);

	/* Write testbuffer to source bufferobject. */
	write_testbuffer(file_descriptor, src_bo->handle);

	/* Check that our testbuffer write worked. */
	/* read_testbuffer(file_descriptor, src_bo->handle); */
	testbuffer_check(surface);

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
	/* testbuffer_check(surface); */
	/* snprintf(filename, sizeof(filename), "%d-dst.png", i/2+1); */
	testbuffer_to_png("rendercopy-output.png");

	return 0;
}
