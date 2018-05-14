#!/usr/bin/bash
# Install build dependencies, run unit tests and installed tests.

set -xeuo pipefail

dn=$(dirname $0)
. ${dn}/libbuild.sh

pkg_install sudo which attr fuse \
    libubsan libasan libtsan elfutils-libelf-devel libdwarf-devel \
    elfutils git gettext-devel libappstream-glib-devel bison \
    libcurl-devel \
    /usr/bin/{update-mime-database,update-desktop-database,gtk-update-icon-cache}
pkg_install_testing ostree-devel ostree libyaml-devel
pkg_install_if_os fedora gjs parallel clang
pkg_install_builddeps flatpak

(git clone --depth=1 https://github.com/flatpak/flatpak/
 cd flatpak
 unset CFLAGS # the sanitizers require calling apps be linked too
 build
 make install
 flatpak --version
)

build --enable-gtk-doc ${CONFIGOPTS:-}
