/*
 * Copyright 2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This program is intended for testing of display functionality.  It should
 * allow for testing of
 *   - hotplug
 *   - mode setting
 *   - clone & twin modes
 *   - panel fitting
 *   - test patterns & pixel generators
 * Additional programs can test the detected outputs against VBT provided
 * device lists (both docked & undocked).
 *
 * TODO:
 * - pixel generator in transcoder
 * - test pattern reg in pipe
 * - test patterns on outputs (e.g. TV)
 * - handle hotplug (leaks crtcs, can't handle clones)
 * - allow mode force
 * - expose output specific controls
 *  - e.g. DDC-CI brightness
 *  - HDMI controls
 *  - panel brightness
 *  - DP commands (e.g. poweroff)
 * - verify outputs against VBT/physical connectors
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "i915_drm.h"
#include "drmtest.h"
#include "testdisplay.h"

#include <stdlib.h>
#include <signal.h>

/* Time before quitting */
#define DISPLAY_TIME 3000

drmModeRes *resources;
int drm_fd, modes;
int test_all_modes =0, test_preferred_mode = 0, force_mode = 0,
	test_plane, enable_tiling;
int sleep_between_modes = 5;
uint32_t depth = 24, stride, bpp;
int qr_code = 0;
int specified_mode_num = -1, specified_disp_id = -1;

drmModeModeInfo force_timing;

int crtc_x, crtc_y, crtc_w, crtc_h, width, height;
unsigned int plane_fb_id;
unsigned int plane_crtc_id;
unsigned int plane_id;
int plane_width, plane_height;
static const uint32_t SPRITE_COLOR_KEY = 0x00aaaaaa;
uint32_t *fb_ptr;
char *input_file;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name {
	int type;
	const char *name;
};

#define type_name_fn(res) \
static const char * res##_str(int type) {			\
	unsigned int i;					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DisplayPort" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "Embedded DisplayPort" },
};

/*
 * Mode setting with the kernel interfaces is a bit of a chore.
 * First you have to find the connector in question and make sure the
 * requested mode is available.
 * Then you need to find the encoder attached to that connector so you
 * can bind it with a free crtc.
 */
struct connector {
	uint32_t id;
	int mode_valid;
	drmModeModeInfo mode;
	drmModeEncoder *encoder;
	drmModeConnector *connector;
	int crtc;
	int pipe;
};

static void end_test(void)
{
  exit (0);
}

static void connector_find_preferred_mode(struct connector *c)
{
	drmModeConnector *connector;
	drmModeEncoder *encoder = NULL;
	int i, j;

	/* First, find the connector & mode */
	c->mode_valid = 0;
	connector = drmModeGetConnector(drm_fd, c->id);
	if (!connector) {
		fprintf(stderr, "could not get connector %d: %s\n",
			c->id, strerror(errno));
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connection != DRM_MODE_CONNECTED) {
		drmModeFreeConnector(connector);
		return;
	}

	if (!connector->count_modes) {
		fprintf(stderr, "connector %d has no modes\n", c->id);
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connector_id != c->id) {
		fprintf(stderr, "connector id doesn't match (%d != %d)\n",
			connector->connector_id, c->id);
		drmModeFreeConnector(connector);
		return;
	}

	for (j = 0; j < connector->count_modes; j++) {
		c->mode = connector->modes[j];
		if (c->mode.type & DRM_MODE_TYPE_PREFERRED) {
			c->mode_valid = 1;
			break;
		}
	}

	if ( specified_mode_num != -1 ){
		c->mode = connector->modes[specified_mode_num];
		if (c->mode.type & DRM_MODE_TYPE_PREFERRED)
			c->mode_valid = 1;
	}

	if (!c->mode_valid) {
		if (connector->count_modes > 0) {
			/* use the first mode as test mode */
			c->mode = connector->modes[0];
			c->mode_valid = 1;
		}
		else {
			fprintf(stderr, "failed to find any modes on connector %d\n",
				c->id);
			return;
		}
	}

	/* Now get the encoder */
	for (i = 0; i < connector->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm_fd, connector->encoders[i]);

		if (!encoder) {
			fprintf(stderr, "could not get encoder %i: %s\n",
				resources->encoders[i], strerror(errno));
			drmModeFreeEncoder(encoder);
			continue;
		}

		break;
	}

	c->encoder = encoder;

	if (i == resources->count_encoders) {
		fprintf(stderr, "failed to find encoder\n");
		c->mode_valid = 0;
		return;
	}

	/* Find first CRTC not in use */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] && (c->encoder->possible_crtcs & (1<<i)))
			break;
	}
	c->crtc = resources->crtcs[i];
	c->pipe = i;

	if(test_preferred_mode || force_mode || specified_mode_num != -1)
		resources->crtcs[i] = 0;

	c->connector = connector;
}

static void paint_function(cairo_t *cr, int l_width, int l_height, void *priv)
{
	int image_x, image_y, image_width, image_height, text_x, text_y;
	char text_buffer[128];
	cairo_surface_t *surface;
	cairo_text_extents_t text_extents;

	/* Create surface */
	surface = cairo_image_surface_create_from_png(input_file);

	/* Center the image on screen */
	image_width = cairo_image_surface_get_width(surface);
	image_height = cairo_image_surface_get_height(surface);
	image_x = (width - image_width) / 2;
	image_y = (height - image_height) / 2;
	cairo_translate(cr, image_x, image_y);

	/* Paint the image */
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);
	cairo_surface_destroy(surface);

	/* Paint the screen and image resolutions */
	snprintf(text_buffer, sizeof(text_buffer), "%dx%d (%dx%d)",
		 image_width, image_height, width, height);
	cairo_set_font_size(cr, 30);
	cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	cairo_text_extents(cr, text_buffer, &text_extents);
	text_x = width - image_x - text_extents.width - 10;
	text_y = height - image_y - 10;

	cairo_translate(cr, text_x, text_y);
	cairo_text_path(cr, text_buffer);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_fill(cr);
}

static void sighandler(int signo)
{
	return;
}

static void set_single(void)
{
	int sigs[] = { SIGUSR1 };
	struct sigaction sa;
	sa.sa_handler = sighandler;

	sigemptyset(&sa.sa_mask);

	if (sigaction(sigs[0], &sa, NULL) == -1)
		perror("Could not set signal handler");
}

static void set_mode(struct connector *c)
{
	unsigned int fb_id = 0;
	int j, test_mode_num;

	if (depth <= 8)
		bpp = 8;
	else if (depth > 8 && depth <= 16)
		bpp = 16;
	else if (depth > 16 && depth <= 32)
		bpp = 32;

	connector_find_preferred_mode(c);
	if (!c->mode_valid)
		return;

	test_mode_num = 1;
	if (force_mode) {
		memcpy( &c->mode, &force_timing, sizeof(force_timing));
		c->mode.vrefresh =(force_timing.clock*1e3)/(force_timing.htotal*force_timing.vtotal);
		c->mode_valid = 1;
		sprintf(c->mode.name, "%dx%d", force_timing.hdisplay, force_timing.vdisplay);
	} else if (test_all_modes)
		test_mode_num = c->connector->count_modes;

	for (j = 0; j < test_mode_num; j++) {
		struct kmstest_fb fb_info;
		kmstest_paint_func paint_func = NULL;

		if (test_all_modes)
			c->mode = c->connector->modes[j];

		if (!c->mode_valid)
			continue;

		width = c->mode.hdisplay;
		height = c->mode.vdisplay;

		if (input_file)
			paint_func = paint_function;

		/* This is responsible for actually creating the framebuffer
		 * using the paint function. */
		fb_id = kmstest_create_fb(drm_fd, width, height, bpp, depth,
					  enable_tiling, &fb_info,
					  paint_func, c);

		fb_ptr = gem_mmap(drm_fd, fb_info.gem_handle,
				  fb_info.size, PROT_READ | PROT_WRITE);
		assert(fb_ptr);
		gem_close(drm_fd, fb_info.gem_handle);

		/* This is where we actually output the framebuffer. */
		if (drmModeSetCrtc(drm_fd, c->crtc, fb_id, 0, 0,
				   &c->id, 1, &c->mode)) {
			fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
				width, height, c->mode.vrefresh,
				strerror(errno));
			continue;
		}

		if (sleep_between_modes && test_all_modes && !qr_code)
			sleep(sleep_between_modes);

		if (qr_code){
			set_single();
			pause();
		}

	}

	if(test_all_modes){
		drmModeRmFB(drm_fd,fb_id);
		drmModeSetCrtc(drm_fd, c->crtc, fb_id, 0, 0,  &c->id, 1, 0);
	}

	drmModeFreeEncoder(c->encoder);
	drmModeFreeConnector(c->connector);
}

/*
 * Re-probe outputs and light up as many as possible.
 *
 * On Intel, we have two CRTCs that we can drive independently with
 * different timings and scanout buffers.
 *
 * Each connector has a corresponding encoder, except in the SDVO case
 * where an encoder may have multiple connectors.
 */
int update_display(void)
{
	struct connector *connectors;
	int c;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		return 0;
	}

	connectors = calloc(resources->count_connectors,
			    sizeof(struct connector));
	if (!connectors)
		return 0;

	if (test_preferred_mode || test_all_modes || force_mode || specified_disp_id != -1) {
		/* Find any connected displays */
		for (c = 0; c < resources->count_connectors; c++) {
			connectors[c].id = resources->connectors[c];
			if ( specified_disp_id != -1 && connectors[c].id != specified_disp_id )
				continue;

			set_mode(&connectors[c]);
		}
	}
	drmModeFreeResources(resources);
	return 1;
}

static char optstr[] = "hiaf:s:d:p:mrto:";

static void __attribute__((noreturn)) usage(char *name)
{
	fprintf(stderr, "usage: %s [-hiasdpmtf]\n", name);
	fprintf(stderr, "\t-a\ttest all modes\n");
	fprintf(stderr, "\t-s\t<duration>\tsleep between each mode test\n");
	fprintf(stderr, "\t-d\t<depth>\tbit depth of scanout buffer\n");
	fprintf(stderr, "\t-p\t<planew,h>,<crtcx,y>,<crtcw,h> test overlay plane\n");
	fprintf(stderr, "\t-m\ttest the preferred mode\n");
	fprintf(stderr, "\t-t\tuse a tiled framebuffer\n");
	fprintf(stderr, "\t-r\tprint a QR code on the screen whose content is \"pass\" for the automatic test\n");
	fprintf(stderr, "\t-o\t<id of the display>,<number of the mode>\tonly test specified mode on the specified display\n");
	fprintf(stderr, "\t-f\t<clock MHz>,<hdisp>,<hsync-start>,<hsync-end>,<htotal>,\n");
	fprintf(stderr, "\t\t<vdisp>,<vsync-start>,<vsync-end>,<vtotal>\n");
	fprintf(stderr, "\t\ttest force mode\n");
	fprintf(stderr, "\tDefault is to test all modes.\n");
	exit(0);
}

#define dump_resource(res) if (res) dump_##res()

static gboolean input_event(GIOChannel *source, GIOCondition condition,
				gpointer data)
{
	gchar buf[2];
	gsize count;

	count = read(g_io_channel_unix_get_fd(source), buf, sizeof(buf));
	if (buf[0] == 'q' && (count == 1 || buf[1] == '\n')) {
		exit(0);
	}

	return TRUE;
}

static void enter_exec_path( char **argv )
{
	char *exec_path = NULL;
	char *pos = NULL;
	short len_path = 0;
	int ret;

	len_path = strlen( argv[0] );
	exec_path = (char*) malloc(len_path);

	memcpy(exec_path, argv[0], len_path);
	pos = strrchr(exec_path, '/');
	if (pos != NULL)
		*(pos+1) = '\0';

	ret = chdir(exec_path);
	assert(ret == 0);
	free(exec_path);
}

int main(int argc, char **argv)
{
	int c;
	int ret = 0;
	GIOChannel *stdinchannel;
	GMainLoop *mainloop;
	float force_clock;

	if (argc > 1 && argv[1][0] != '-') {
		/* Input image specified at command line */
		input_file = argv[1];
		printf("%s: displaying '%s'\n", argv[0], input_file);
	} else {
		printf("%s: no input image\n", argv[0]);
	}


	enter_exec_path(argv);

	opterr = 0;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'a':
			test_all_modes = 1;
			break;
		case 'f':
			force_mode = 1;
			if(sscanf(optarg,"%f,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu",
				&force_clock,&force_timing.hdisplay, &force_timing.hsync_start,&force_timing.hsync_end,&force_timing.htotal,
				&force_timing.vdisplay, &force_timing.vsync_start, &force_timing.vsync_end, &force_timing.vtotal)!= 9)
				usage(argv[0]);
			force_timing.clock = force_clock*1000;

			break;
		case 's':
			sleep_between_modes = atoi(optarg);
			break;
		case 'd':
			depth = atoi(optarg);
			fprintf(stderr, "using depth %d\n", depth);
			break;
		case 'p':
			if (sscanf(optarg, "%d,%d,%d,%d,%d,%d", &plane_width,
				   &plane_height, &crtc_x, &crtc_y,
				   &crtc_w, &crtc_h) != 6)
				usage(argv[0]);
			test_plane = 1;
			break;
		case 'm':
			test_preferred_mode = 1;
			break;
		case 't':
			enable_tiling = 1;
			break;
		case 'r':
			qr_code = 1;
			break;
		case 'o':
			sscanf(optarg, "%d,%d", &specified_disp_id, &specified_mode_num);
			break;
		default:
			fprintf(stderr, "unknown option %c\n", c);
			/* fall through */
		case 'h':
			usage(argv[0]);
			break;
		}
	}
	if (!test_all_modes && !force_mode &&
	    !test_preferred_mode && specified_mode_num == -1)
		test_all_modes = 1;

	drm_fd = drm_open_any();

	mainloop = g_main_loop_new(NULL, FALSE);
	if (!mainloop) {
		fprintf(stderr, "failed to create glib mainloop\n");
		ret = -1;
		goto out_close;
	}

	if (!testdisplay_setup_hotplug()) {
		fprintf(stderr, "failed to initialize hotplug support\n");
		goto out_mainloop;
	}

	stdinchannel = g_io_channel_unix_new(0);
	if (!stdinchannel) {
		fprintf(stderr, "failed to create stdin GIO channel\n");
		goto out_hotplug;
	}

	ret = g_io_add_watch(stdinchannel, G_IO_IN | G_IO_ERR, input_event,
			     NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to add watch on stdin GIO channel\n");
		goto out_stdio;
	}

	ret = 0;

	if (!update_display()) {
		ret = 1;
		goto out_stdio;
	}

	if (test_all_modes)
		goto out_stdio;

	/* Do not run tests infinitely. */
        g_timeout_add (DISPLAY_TIME, (GSourceFunc)end_test, NULL);

        g_main_loop_run(mainloop);

out_stdio:
	g_io_channel_shutdown(stdinchannel, TRUE, NULL);
out_hotplug:
	testdisplay_cleanup_hotplug();
out_mainloop:
	g_main_loop_unref(mainloop);
out_close:
	close(drm_fd);

	return ret;
}
