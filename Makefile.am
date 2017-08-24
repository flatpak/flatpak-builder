NULL =

bin_PROGRAMS = $(NULL)
dist_installed_test_extra_scripts = $(NULL)
noinst_PROGRAMS = $(NULL)
noinst_LTLIBRARIES = $(NULL)
libexec_PROGRAMS = $(NULL)
CLEANFILES = $(NULL)
MAINTAINERCLEANFILES = $(NULL)
DISTCLEANFILES= $(NULL)
BUILT_SOURCES = $(NULL)
EXTRA_DIST =

include $(top_srcdir)/buildutil/glib-tap.mk

SUBDIRS =


if BUILD_DOCUMENTATION
SUBDIRS += . doc
endif

FLATPAK_BINDIR=$(bindir)

AM_CPPFLAGS =							\
	-DFLATPAK_BINDIR=\"$(FLATPAK_BINDIR)\"			\
	-DFLATPAK_BASEDIR=\"$(pkgdatadir)\"			\
	-DG_LOG_DOMAIN=\"flatpak-builder\"			\
	-I$(srcdir)/libglnx					\
	-I$(srcdir)/common					\
	-I$(builddir)/common					\
	-include "config.h"					\
	$(NULL)

AM_CFLAGS = $(WARN_CFLAGS)

lib_LTLIBRARIES =
noinst_LTLIBRARIES += libglnx.la
libglnx_srcpath := $(srcdir)/libglnx
libglnx_cflags := \
	$(BASE_CFLAGS) \
	"-I$(libglnx_srcpath)" \
	-std=gnu99 \
	$(HIDDEN_VISIBILITY_CFLAGS) \
	$(NULL)
libglnx_libs := $(BASE_LIBS)
include libglnx/Makefile-libglnx.am.inc

include common/Makefile.am.inc
include builder/Makefile.am.inc
include tests/Makefile.am.inc

EXTRA_DIST += README.md

AM_DISTCHECK_CONFIGURE_FLAGS =		\
	--enable-documentation		\
	--disable-maintainer-mode	\
	--enable-introspection
