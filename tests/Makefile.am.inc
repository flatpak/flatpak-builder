AM_TESTS_ENVIRONMENT = FLATPAK_TESTS_DEBUG=1 \
	GI_TYPELIB_PATH=$$(cd $(top_builddir) && pwd)$${GI_TYPELIB_PATH:+:$$GI_TYPELIB_PATH} \
	LD_LIBRARY_PATH=$$(cd $(top_builddir)/.libs && pwd)$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH} \
	PATH=$$(cd $(top_builddir) && pwd):$${PATH} \
	$(NULL)

if !WITH_SYSTEM_DEBUGEDIT
AM_TESTS_ENVIRONMENT += FLATPAK_BUILDER_DEBUGEDIT=$$(cd $(top_builddir) && pwd)/flatpak-builder-debugedit
endif

dist_installed_test_extra_scripts += \
	tests/empty-configure \
	tests/test-configure \
	tests/make-test-app.sh \
	tests/make-test-runtime.sh \
	tests/make-test-bundles.sh \
	tests/testpython.py \
	$(NULL)

dist_installed_test_data = \
	tests/libtest.sh \
	tests/org.test.Hello.png \
	tests/source1.json \
	tests/source2.json \
	tests/test.json \
	tests/test.yaml \
	tests/test.eson \
	tests/test-runtime.json \
	tests/module1.json \
	tests/module1.yaml \
	tests/module1.eson \
	tests/data1 \
	tests/data1.patch \
	tests/module2.json \
	tests/module2.yaml \
	tests/module2.eson \
	tests/data2 \
	tests/data2.patch \
	tests/session.conf.in \
	tests/0001-Add-test-logo.patch \
	tests/org.test.Python.json \
	tests/org.test.Python2.json \
	tests/importme.py \
	tests/importme2.py \
	tests/org.test.Deprecated.MD5.archive.json \
	tests/org.test.Deprecated.MD5.archive.yaml \
	tests/org.test.Deprecated.MD5.file.json \
	tests/org.test.Deprecated.MD5.file.yaml \
	tests/org.test.Deprecated.SHA1.archive.json \
	tests/org.test.Deprecated.SHA1.archive.yaml \
	tests/org.test.Deprecated.SHA1.file.json \
	tests/org.test.Deprecated.SHA1.file.yaml \
	tests/hello.sh \
	tests/hello.tar.xz \
	$(NULL)

installed_test_keyringdir = $(installed_testdir)/test-keyring
installed_test_keyring2dir = $(installed_testdir)/test-keyring2

if ENABLE_INSTALLED_TESTS
dist_installed_test_keyring_DATA = \
	tests/test-keyring/README \
	tests/test-keyring/pubring.gpg \
	tests/test-keyring/secring.gpg \
	$(NULL)
dist_installed_test_keyring2_DATA = \
	tests/test-keyring2/README \
	tests/test-keyring2/pubring.gpg \
	tests/test-keyring2/secring.gpg \
	$(NULL)
endif

dist_test_scripts = \
	tests/test-builder.sh \
	tests/test-builder-deprecated.sh \
	tests/test-builder-python.sh \
	$(NULL)

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=tests/flatpak.supp tests/glib.supp
EXTRA_DIST += tests/flatpak.supp tests/glib.supp
DISTCLEANFILES += \
	$(NULL)
