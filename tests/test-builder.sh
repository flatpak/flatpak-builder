#!/bin/bash
#
# Copyright (C) 2011 Colin Walters <walters@verbum.org>
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

echo "1..9"

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

# Need /var/tmp cwd for xattrs
REPO=`pwd`/repos/test
cd $TEST_DATA_DIR/

cp -a $(dirname $0)/test-configure .
echo "version1" > app-data
cp $(dirname $0)/test-rename.json .
cp $(dirname $0)/test-rename-appdata.json .
cp $(dirname $0)/test.json .
cp $(dirname $0)/test.yaml .
cp $(dirname $0)/test-runtime.json .
cp $(dirname $0)/0001-Add-test-logo.patch .
cp $(dirname $0)/Hello.desktop .
cp $(dirname $0)/Hello.xml .
cp $(dirname $0)/Hello.appdata.xml .
cp $(dirname $0)/Hello-desktop.appdata.xml .
cp $(dirname $0)/org.test.Hello.desktop .
cp $(dirname $0)/org.test.Hello.xml .
cp $(dirname $0)/org.test.Hello.appdata.xml .
cp $(dirname $0)/org.flatpak_builder.gui.desktop .
cp $(dirname $0)/org.flatpak_builder.gui.json .
cp $(dirname $0)/org.flatpak_builder.gui.metainfo.xml .
cp $(dirname $0)/org.test.Hello.png .
mkdir include1
cp $(dirname $0)/module1.json include1/
cp $(dirname $0)/module1.yaml include1/
cp $(dirname $0)/source1.json include1/
cp $(dirname $0)/data1 include1/
cp $(dirname $0)/data1.patch include1/
mkdir include1/include2
cp $(dirname $0)/module2.json include1/include2/
cp $(dirname $0)/module2.yaml include1/include2/
cp $(dirname $0)/source2.json include1/include2/
cp $(dirname $0)/data2 include1/include2/
cp $(dirname $0)/data2.patch include1/include2/
echo "MY LICENSE" > ./LICENSE

for MANIFEST in test.json test.yaml test-rename.json test-rename-appdata.json ; do
    echo "building manifest $MANIFEST" >&2
    ${FLATPAK_BUILDER} --repo=$REPO $FL_GPGARGS --force-clean appdir $MANIFEST >&2

    assert_file_has_content appdir/files/share/app-data version1
    assert_file_has_content appdir/metadata shared=network;
    assert_file_has_content appdir/metadata tags=test;
    assert_file_has_content appdir/files/ran_module1 module1
    assert_file_has_content appdir/files/ran_module2 module2

    assert_not_has_file appdir/files/cleanup/a_filee
    assert_not_has_file appdir/files/bin/file.cleanup

    assert_has_file appdir/files/cleaned_up > out
    assert_has_file appdir/files/share/icons/hicolor/64x64/apps/org.test.Hello2.png
    assert_has_file appdir/files/share/icons/hicolor/64x64/mimetypes/org.test.Hello2.application-x-hello.png
    assert_has_file appdir/files/share/icons/hicolor/64x64/mimetypes/org.test.Hello2.application-x-goodbye.png
    assert_has_file appdir/files/share/applications/org.test.Hello2.desktop
    assert_has_file appdir/files/share/metainfo/org.test.Hello2.metainfo.xml
    xmllint --noout appdir/files/share/metainfo/org.test.Hello2.metainfo.xml >&2
    grep -qs "<id>org.test.Hello2</id>" appdir/files/share/metainfo/org.test.Hello2.metainfo.xml

    assert_has_file appdir/files/share/mime/packages/org.test.Hello2.xml
    xmllint --noout appdir/files/share/mime/packages/org.test.Hello2.xml >&2

    assert_file_has_content appdir/files/out '^foo$'
    assert_file_has_content appdir/files/out2 '^foo2$'

    assert_file_has_content appdir/files/source1 'Hello, from source 1'
    assert_file_has_content appdir/files/source2 'Hello, from source 2'

    ${FLATPAK} build appdir /app/bin/hello2.sh > hello_out2
    assert_file_has_content hello_out2 '^Hello world2, from a sandbox$'

    assert_file_has_content appdir/files/share/licenses/org.test.Hello2/test/LICENSE '^MY LICENSE$'

    echo "ok build"
done

${FLATPAK} ${U} install -y test-repo org.test.Hello2 master >&2
run org.test.Hello2 > hello_out3
assert_file_has_content hello_out3 '^Hello world2, from a sandbox$'

run --command=cat org.test.Hello2 /app/share/app-data > app_data_1
assert_file_has_content app_data_1 version1

echo "ok install+run"

echo "version2" > app-data
${FLATPAK_BUILDER} $FL_GPGARGS --repo=$REPO --force-clean appdir test.json >&2
assert_file_has_content appdir/files/share/app-data version2
${FLATPAK_BUILDER} $FL_GPGARGS --repo=$REPO --force-clean appdir test.yaml >&2
assert_file_has_content appdir/files/share/app-data version2

${FLATPAK} ${U} update -y org.test.Hello2 master >&2

run --command=cat org.test.Hello2 /app/share/app-data > app_data_2
assert_file_has_content app_data_2 version2

echo "ok update"

# The build-args of --help should prevent the faulty cleanup and
# platform-cleanup commands from executing
${FLATPAK_BUILDER} $FL_GPGARGS --repo=$REPO --force-clean runtimedir \
    test-runtime.json >&2

echo "ok runtime build cleanup with build-args"

# test screenshot ref commit
${FLATPAK_BUILDER} --repo=$REPO/repo_sc --force-clean builddir_sc \
    --mirror-screenshots-url=https://example.org/media \
    org.flatpak_builder.gui.json >&2
ostree --repo=$REPO/repo_sc refs|grep -Eq "^screenshots/$(flatpak --default-arch)$"
ostree checkout --repo=$REPO/repo_sc -U screenshots/$(flatpak --default-arch) outdir_sc
find outdir_sc -path "*/screenshots/image-1_orig.png" -type f | grep -q .

echo "ok screenshot ref commit"

# test compose partial url
${FLATPAK_BUILDER} --force-clean builddir_sc \
    --mirror-screenshots-url=https://example.org/media \
    --state-dir .fp-compose-url-policy \
    --compose-url-policy=partial \
    org.flatpak_builder.gui.json >&2
gzip -cdq builddir_sc/files/share/app-info/xmls/org.flatpak_builder.gui.xml.gz|grep -Eq '>org/flatpak_builder/gui/[^/]+/screenshots/image-1_orig\.png</image>'

echo "ok compose partial url"
