SUBDIRS = doc test

noinst_LTLIBRARIES = libbrw.la

bin_PROGRAMS = intel-gen4asm intel-gen4disasm

libbrw_la_SOURCES =		\
	brw_compat.h		\
	brw_context.c		\
	brw_context.h		\
	brw_disasm.c		\
	brw_defines.h		\
	brw_eu.h		\
	brw_eu.c		\
	brw_eu_compact.c	\
	brw_eu_debug.c		\
	brw_eu_emit.c		\
	brw_eu_util.c		\
	brw_reg.h		\
	brw_structs.h		\
	ralloc.c		\
	ralloc.h		\
	$(NULL)

AM_YFLAGS = -d --warnings=all
AM_CFLAGS= $(ASSEMBLER_WARN_CFLAGS)

LEX = flex -i
BUILT_SOURCES = gram.h gram.c lex.c
gram.h: gram.c

intel_gen4asm_SOURCES =	\
	gen4asm.h	\
	gram.y		\
	lex.l		\
	main.c		\
	$(NULL)

intel_gen4asm_LDADD = libbrw.la

intel_gen4disasm_SOURCES =  disasm-main.c
intel_gen4disasm_LDADD = libbrw.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = intel-gen4asm.pc

gen_regions_files = 					\
	gen_regions/assets/css/bootstrap-responsive.css \
	gen_regions/assets/css/bootstrap.css		\
	gen_regions/assets/js/bootstrap-alert.js	\
	gen_regions/assets/js/bootstrap-button.js	\
	gen_regions/assets/js/bootstrap-collapse.js	\
	gen_regions/assets/js/bootstrap-dropdown.js	\
	gen_regions/assets/js/bootstrap.js		\
	gen_regions/assets/js/gen_regions.js		\
	gen_regions/assets/js/jquery-1.9.1.js		\
	gen_regions/assets/js/jquery-migrate-1.1.1.js	\
	gen_regions/assets/js/jquery.ba-hashchange.js	\
	gen_regions/assets/js/jquery.hotkeys.js		\
	gen_regions/gen_regions.html			\
	$(NULL)

MAINTAINERCLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = \
	$(gen_regions_files) \
	README \
	TODO \
	intel-gen4asm.pc.in
