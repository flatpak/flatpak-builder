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
ACLOCAL_AMFLAGS = -I m4 -I subprojects/libglnx ${ACLOCAL_FLAGS}
AM_CPPFLAGS =							\
	-DFLATPAK_BINDIR=\"$(FLATPAK_BINDIR)\"			\
	-DFLATPAK_BASEDIR=\"$(pkgdatadir)\"			\
	-DG_LOG_DOMAIN=\"flatpak-builder\"			\
	-I$(srcdir)/subprojects					\
	-I$(srcdir)/subprojects/libglnx				\
	-I$(srcdir)/subprojects/debugedit			\
	-include "config.h"					\
	$(NULL)

AM_CFLAGS = $(WARN_CFLAGS)

if WITH_SYSTEM_DEBUGEDIT
AM_CPPFLAGS += -DDEBUGEDIT=\"$(DEBUGEDIT)\"
else
AM_CPPFLAGS += -DDEBUGEDIT=\"$(libexecdir)/flatpak-builder-debugedit\"
endif

lib_LTLIBRARIES =
noinst_LTLIBRARIES += libglnx.la
libglnx_srcpath := $(srcdir)/subprojects/libglnx
libglnx_cflags := \
	$(BASE_CFLAGS) \
	"-I$(libglnx_srcpath)" \
	-std=gnu99 \
	$(HIDDEN_VISIBILITY_CFLAGS) \
	$(NULL)
libglnx_libs := $(BASE_LIBS)
include subprojects/libglnx/Makefile-libglnx.am.inc

include src/Makefile.am.inc
include tests/Makefile.am.inc

if !WITH_SYSTEM_DEBUGEDIT

libexec_PROGRAMS+=flatpak-builder-debugedit
flatpak_builder_debugedit_SOURCES =				\
	subprojects/debugedit/tools/debugedit.c \
	subprojects/debugedit/tools/hashtab.c	\
	subprojects/debugedit/tools/hashtab.h	\
	subprojects/debugedit/tools/md5.c		\
	subprojects/debugedit/tools/md5.h		\
	subprojects/debugedit/tools/sha1.c		\
	subprojects/debugedit/tools/sha1.h		\
	subprojects/debugedit/tools/ansidecl.h		\
	$(NULL)
flatpak_builder_debugedit_LDADD =	\
	$(AM_LDADD) $(BASE_LIBS)		\
	$(LIBELF_LIBS) $(LIBDW_LIBS)	\
	$(NULL)
flatpak_builder_debugedit_CFLAGS =		\
	$(AM_CFLAGS) $(BASE_CFLAGS)			\
	-Isubprojects/debugedit				\
	-DVERSION="\"@PACKAGE_VERSION@\""	\
	-Wno-format-nonliteral				\
	$(NULL)

endif # !WITH_SYSTEM_DEBUGEDIT

EXTRA_DIST += README.md

AM_DISTCHECK_CONFIGURE_FLAGS =		\
	--enable-documentation		\
	--disable-maintainer-mode	\
	--enable-introspection
