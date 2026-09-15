#!/bin/bash
#
# Copyright (C) 2026 Boudhayan Bhattacharya <bbhtt@bbhtt.in>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

set -euo pipefail

. $(dirname $0)/libtest.sh

skip_without_fuse

echo "1..3"

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd $TEST_DATA_DIR/

cp $(dirname $0)/org.flatpak_builder.gui.desktop .
cp $(dirname $0)/org.flatpak_builder.gui.metainfo.xml .
cp $(dirname $0)/org.test.Hello-256.png .
cp $(dirname $0)/org.flatpak.appstream_media.json .

# test compose partial url policy
APPDIR=builddir_sc \
run_build \
    --mirror-screenshots-url=https://example.org/media \
    --state-dir .fp-compose-url-policy-partial \
    --compose-url-policy=partial \
    org.flatpak.appstream_media.json
# we test for the icon tag instead of screenshot
# the former works offline the latter does not
gzip -cdq builddir_sc/files/share/app-info/xmls/org.flatpak.appstream_media.xml.gz|grep -Eq '>org/flatpak/appstream_media/[^/]+/icons/128x128/org.flatpak.appstream_media.png</icon>'

echo "ok compose partial url policy"

# test compose full url policy
if appstream_has_version 0 16 3; then
    APPDIR=builddir_sc \
    run_build \
        --mirror-screenshots-url=https://example.org/media \
        --state-dir .fp-compose-url-policy-full \
        --compose-url-policy=full \
        org.flatpak.appstream_media.json

    gzip -cdq builddir_sc/files/share/app-info/xmls/org.flatpak.appstream_media.xml.gz|grep -Eq '>https://example.org/media/org/flatpak/appstream_media/[^/]+/icons/128x128/org.flatpak.appstream_media.png</icon>'

    echo "ok compose full url policy"
else
    echo "ok # Skip AppStream < 0.16.3"
fi

# test compose jxl image format
if appstream_has_version 1 2 0; then
    APPDIR=builddir_jxl \
    run_build \
        --mirror-screenshots-url=https://example.org/media \
        --state-dir .fp-compose-image-format-jxl \
        --compose-url-policy=full \
        --compose-image-format=jxl \
        org.flatpak.appstream_media.json

        gzip -cdq builddir_jxl/files/share/app-info/xmls/org.flatpak.appstream_media.xml.gz|grep -Eq '>https://example.org/media/org/flatpak/appstream_media/[^/]+/icons/128x128/org.flatpak.appstream_media.jxl</icon>'

    echo "ok compose jxl image format"
else
    echo "ok # Skip AppStream < 1.2.0"
fi
