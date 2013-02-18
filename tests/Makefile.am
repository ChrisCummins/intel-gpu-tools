if BUILD_TESTS
noinst_PROGRAMS = \
	$(TESTS_progs) \
	$(TESTS_progs_M) \
	$(HANG) \
	$(NULL)

if HAVE_NOUVEAU
NOUVEAU_TESTS_M = $(NULL)
NOUVEAU_TESTS = $(NULL)
endif

TESTS_progs_M = $(NULL)

TESTS_progs = \
	cec_test \
	cec_test2 \
	$(NULL)

# IMPORTANT: The ZZ_ tests need to be run last!
# ... and make can't deal with inlined comments ...
TESTS_scripts_M = $(NULL)

TESTS_scripts = $(NULL)

# This target contains testcases which support automagic subtest enumeration
# from the piglit testrunner with --list-subtests and running individual
# subtests with --run-subtest <testname>
multi_kernel_tests = \
	$(TESTS_progs_M) \
	$(TESTS_scripts_M) \
	$(NULL)

single_kernel_tests = \
	$(TESTS_progs) \
	$(TESTS_scripts) \
	$(NULL)

kernel_tests = \
	$(single_kernel_tests) \
	$(multi_kernel_tests) \
	$(NULL)

TESTS = \
	$(NULL)

test:
	@whoami | grep root || ( echo ERROR: not running as root; exit 1 )
	@./check_drm_clients
	@make TESTS="${kernel_tests}" check

list-single-tests:
	@echo TESTLIST
	@echo ${single_kernel_tests}
	@echo END TESTLIST

list-multi-tests:
	@echo TESTLIST
	@echo ${multi_kernel_tests}
	@echo END TESTLIST

HANG = $(NULL)

EXTRA_PROGRAMS = $(TESTS_progs) $(TESTS_progs_M) $(HANG)
EXTRA_DIST = $(TESTS_scripts) $(TESTS_scripts_M) drm_lib.sh check_drm_clients debugfs_wedged
CLEANFILES = $(EXTRA_PROGRAMS)

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS)

testdisplay_SOURCES = \
	testdisplay.c \
	testdisplay.h \
	testdisplay_hotplug.c \
	$(NULL)

TESTS_progs += testdisplay
LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)

gem_fence_thrash_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_fence_thrash_LDADD = $(LDADD) -lpthread
gem_threaded_access_tiled_LDADD = $(LDADD) -lpthread

gem_wait_render_timeout_LDADD = $(LDADD) -lrt
kms_flip_LDADD = $(LDADD) -lrt

gem_ctx_basic_LDADD = $(LDADD) -lpthread

prime_nv_test_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_test_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_api_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_api_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_pcopy_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_pcopy_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
endif
