bin_PROGRAMS += \
	flatpak-builder \
	$(NULL)

flatpak_builder_SOURCES = \
	src/builder-main.c \
	src/builder-manifest.c \
	src/builder-manifest.h \
	src/builder-options.c \
	src/builder-options.h \
	src/builder-extension.c \
	src/builder-extension.h \
	src/builder-module.c \
	src/builder-module.h \
	src/builder-post-process.c \
	src/builder-post-process.h \
	src/builder-source.c \
	src/builder-source.h \
	src/builder-source-archive.c \
	src/builder-source-archive.h \
	src/builder-source-git.c \
	src/builder-source-git.h \
	src/builder-source-bzr.c \
	src/builder-source-bzr.h \
	src/builder-source-svn.c \
	src/builder-source-svn.h \
	src/builder-source-file.c \
	src/builder-source-file.h \
	src/builder-source-script.c \
	src/builder-source-script.h \
	src/builder-source-shell.c \
	src/builder-source-shell.h \
	src/builder-source-extra-data.c \
	src/builder-source-extra-data.h \
	src/builder-source-patch.c \
	src/builder-source-patch.h \
	src/builder-source-dir.c \
	src/builder-source-dir.h \
	src/builder-context.c \
	src/builder-context.h \
	src/builder-cache.c \
	src/builder-cache.h \
	src/builder-utils.c \
	src/builder-utils.h \
	src/builder-flatpak-utils.c \
	src/builder-flatpak-utils.h \
	src/builder-git.c \
	src/builder-git.h \
	$(NULL)

flatpak_builder_LDADD = $(AM_LDADD) $(BASE_LIBS) $(LIBELF_LIBS) $(YAML_LIBS) libglnx.la
flatpak_builder_CFLAGS = $(AM_CFLAGS) $(BASE_CFLAGS) $(YAML_CFLAGS)
