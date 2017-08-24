noinst_LTLIBRARIES += libflatpak-common.la

BUILT_SOURCES += $(nodist_libflatpak_common_la_SOURCES)
CLEANFILES += $(nodist_libflatpak_common_la_SOURCES)

libflatpak_common_la_SOURCES = \
	common/flatpak-utils.c \
	common/flatpak-utils.h \
	$(NULL)

libflatpak_common_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(BASE_CFLAGS) \
	$(HIDDEN_VISIBILITY_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(JSON_CFLAGS) \
	$(XAUTH_CFLAGS) \
	$(LIBSECCOMP_CFLAGS) \
	$(INTERNAL_GPGME_CFLAGS) \
	$(NULL)
libflatpak_common_la_LIBADD = $(AM_LIBADD) libglnx.la $(BASE_LIBS) $(OSTREE_LIBS) $(SOUP_LIBS) $(JSON_LIBS) $(XAUTH_LIBS) $(LIBSECCOMP_LIBS) $(INTERNAL_GPGME_LIBS)
