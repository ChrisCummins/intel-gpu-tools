bin_PROGRAMS = $(NULL)

noinst_PROGRAMS = $(NULL)

dist_bin_SCRIPTS = intel_gpu_abrt

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS)

intel_dump_decode_SOURCES = 	\
	intel_dump_decode.c

intel_error_decode_SOURCES =	\
	intel_error_decode.c

intel_bios_reader_SOURCES =	\
	intel_bios_reader.c	\
	intel_bios.h
