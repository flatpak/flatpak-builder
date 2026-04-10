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

echo "1..7"

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd "$TEST_DATA_DIR"

REPO="$(pwd)/repos/test"

run_build () {
    local manifest=$1
    ${FLATPAK_BUILDER} --force-clean appdir "$manifest" >&2
}

run_build_fail () {
    local manifest=$1
    if ${FLATPAK_BUILDER} --force-clean appdir "$manifest" >&2; then
        echo "build of $manifest unexpectedly succeeded" >&2
        exit 1
    fi
}

mkdir -p source_licence_1
ln -s /proc/self source_licence_1/licenses
tar czf source_licence_1.tar.gz source_licence_1/

cat > test-licence_interm_symlink.json <<'EOF'
{
    "app-id": "org.test.licence_interm_symlink",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "license-files": ["licenses/environ"],
        "sources": [{
            "type": "archive",
            "path": "source_licence_1.tar.gz",
            "dest-filename": "source_licence_1.tar.gz"
        }]
    }]
}
EOF

run_build_fail test-licence_interm_symlink.json

assert_not_has_file appdir/files/share/licenses/org.test.licence_interm_symlink/test/environ

echo "ok intermediate path symlink is rejected"

mkdir -p source_licence_2
ln -s /etc/hostname source_licence_2/LICENSE
tar czf source_licence_2.tar.gz source_licence_2/

cat > test-licence_abs_symlink.json <<'EOF'
{
    "app-id": "org.test.licence_abs_symlink",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "license-files": ["LICENSE"],
        "sources": [{
            "type": "archive",
            "path": "source_licence_2.tar.gz",
            "dest-filename": "source_licence_2.tar.gz"
        }]
    }]
}
EOF

run_build_fail test-licence_abs_symlink.json

assert_not_has_file appdir/files/share/licenses/org.test.licence_abs_symlink/test/LICENSE

echo "ok absolute path symlink is rejected"

mkdir -p source_licence_3
echo "MY LICENSE TEXT" > source_licence_3/LICENSE
tar czf source_licence_3.tar.gz source_licence_3/

cat > test-license_working_1.json <<'EOF'
{
    "app-id": "org.test.licence_working_1",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "license-files": ["LICENSE"],
        "sources": [{
            "type": "archive",
            "path": "source_licence_3.tar.gz",
            "dest-filename": "source_licence_3.tar.gz"
        }]
    }]
}
EOF

run_build test-license_working_1.json

assert_has_file appdir/files/share/licenses/org.test.licence_working_1/test/LICENSE
assert_file_has_content \
    appdir/files/share/licenses/org.test.licence_working_1/test/LICENSE \
    '^MY LICENSE TEXT$'

echo "ok license file is recorded with license-files key"

mkdir -p source_licence_4
echo "MY DEFAULT LICENSE TEXT" > source_licence_4/LICENSE
tar czf source_licence_4.tar.gz source_licence_4/

cat > test-license_working_2.json <<'EOF'
{
    "app-id": "org.test.licence_working_2",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "path": "source_licence_4.tar.gz",
            "dest-filename": "source_licence_4.tar.gz"
        }]
    }]
}
EOF

run_build test-license_working_2.json

assert_has_file appdir/files/share/licenses/org.test.licence_working_2/test/LICENSE
assert_file_has_content \
    appdir/files/share/licenses/org.test.licence_working_2/test/LICENSE \
    '^MY DEFAULT LICENSE TEXT$'

echo "ok license file is recorded by default"

mkdir -p source_licence_5/LICENSES mkdir -p source_licence_5/licenses
echo "MIT LICENSE TEXT" > source_licence_5/LICENSES/MIT.txt
echo "APACHE LICENSE TEXT" > source_licence_5/licenses/Apache-2.0.txt
tar czf source_licence_5.tar.gz source_licence_5/

cat > test-license_subdir.json <<'EOF'
{
    "app-id": "org.test.licence_subdir",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "path": "source_licence_5.tar.gz",
            "dest-filename": "source_licence_5.tar.gz"
        }]
    }]
}
EOF

run_build test-license_subdir.json

assert_has_file appdir/files/share/licenses/org.test.licence_subdir/test/LICENSES_MIT.txt
assert_file_has_content \
    appdir/files/share/licenses/org.test.licence_subdir/test/LICENSES_MIT.txt \
    '^MIT LICENSE TEXT$'
assert_has_file appdir/files/share/licenses/org.test.licence_subdir/test/licenses_Apache-2.0.txt
assert_file_has_content \
    appdir/files/share/licenses/org.test.licence_subdir/test/licenses_Apache-2.0.txt \
    '^APACHE LICENSE TEXT$'

echo "ok licence subdir files are collected automatically"

mkdir -p source_licence_6/LICENSES
ln -s /etc/hostname source_licence_6/LICENSES/MIT.txt
tar czf source_licence_6.tar.gz source_licence_6/

cat > test-licence_subdir_symlink.json <<'EOF'
{
    "app-id": "org.test.licence_subdir_symlink",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "path": "source_licence_6.tar.gz",
            "dest-filename": "source_licence_6.tar.gz"
        }]
    }]
}
EOF

run_build test-licence_subdir_symlink.json

assert_not_has_file appdir/files/share/licenses/org.test.licence_subdir_symlink/test/LICENSES_MIT.txt

echo "ok symlink inside licence subdir is skipped"

mkdir -p source_licence_7
ln -s /proc/self source_licence_7/LICENSES
tar czf source_licence_7.tar.gz source_licence_7/

cat > test-licence_subdir_itself_symlink.json <<'EOF'
{
    "app-id": "org.test.licence_subdir_itself_symlink",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "path": "source_licence_7.tar.gz",
            "dest-filename": "source_licence_7.tar.gz"
        }]
    }]
}
EOF

run_build test-licence_subdir_itself_symlink.json

assert_not_has_file appdir/files/share/licenses/org.test.licence_subdir_itself_symlink/test/LICENSES_environ

echo "ok licence dir symlink is skipped"
