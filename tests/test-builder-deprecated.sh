#!/bin/bash
#
# Copyright © 2022 Red Hat, Inc.
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# This file is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

set -euo pipefail

srcdir=$(dirname "$0")
. "$srcdir"/libtest.sh

skip_without_fuse

echo "1..8"

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd "$TEST_DATA_DIR"

cp "$srcdir"/org.test.Deprecated.MD5.archive.json .
cp "$srcdir"/org.test.Deprecated.MD5.archive.yaml .
cp "$srcdir"/org.test.Deprecated.MD5.file.json .
cp "$srcdir"/org.test.Deprecated.MD5.file.yaml .
cp "$srcdir"/org.test.Deprecated.SHA1.archive.json .
cp "$srcdir"/org.test.Deprecated.SHA1.archive.yaml .
cp "$srcdir"/org.test.Deprecated.SHA1.file.json .
cp "$srcdir"/org.test.Deprecated.SHA1.file.yaml .
cp "$srcdir"/hello.sh .
cp "$srcdir"/hello.tar.xz .

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.MD5.archive.json 2> build-error-log
assert_file_has_content build-error-log 'The "md5" source property is deprecated due to the weakness of MD5 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated MD5 hash for archive in JSON"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.MD5.archive.yaml 2> build-error-log
assert_file_has_content build-error-log 'The "md5" source property is deprecated due to the weakness of MD5 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated MD5 hash for archive in YAML"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.MD5.file.json 2> build-error-log
assert_file_has_content build-error-log 'The "md5" source property is deprecated due to the weakness of MD5 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated MD5 hash for file in JSON"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.MD5.file.yaml 2> build-error-log
assert_file_has_content build-error-log 'The "md5" source property is deprecated due to the weakness of MD5 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated MD5 hash for file in YAML"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.SHA1.archive.json 2> build-error-log
assert_file_has_content build-error-log 'The "sha1" source property is deprecated due to the weakness of SHA1 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated SHA1 hash for archive in JSON"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.SHA1.archive.yaml 2> build-error-log
assert_file_has_content build-error-log 'The "sha1" source property is deprecated due to the weakness of SHA1 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated SHA1 hash for archive in YAML"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.SHA1.file.json 2> build-error-log
assert_file_has_content build-error-log 'The "sha1" source property is deprecated due to the weakness of SHA1 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated SHA1 hash for file in JSON"

${FLATPAK_BUILDER} --force-clean appdir org.test.Deprecated.SHA1.file.yaml 2> build-error-log
assert_file_has_content build-error-log 'The "sha1" source property is deprecated due to the weakness of SHA1 hashes.'
assert_file_has_content build-error-log 'Use the "sha256" property for the more secure SHA256 hash.'

echo "ok deprecated SHA1 hash for file in YAML"
