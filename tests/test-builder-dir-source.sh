#!/bin/bash
#
# Copyright (C) 2026 Patrick Griffis <pgriffis@igalia.com>
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

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd "$TEST_DATA_DIR"

mkdir -p dir-source/lib
echo "hello from dir source" > dir-source/lib/libfoo.txt

# Archives are extracted with strip-components=1 by default, so everything
# lives under a single toplevel directory
mkdir -p symlink-merge/toplevel/lib64
ln -s lib64 symlink-merge/toplevel/lib
tar -C symlink-merge -cf symlink-merge.tar toplevel
merge_sha256=$(sha256sum symlink-merge.tar | cut -d' ' -f1)

cat > test-dir-source-symlink-merge.json <<EOF
{
  "app-id": "org.test.DirSourceSymlinkMerge",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "modules": [{
    "name": "test",
    "buildsystem": "simple",
    "sources": [
      {
        "type": "archive",
        "path": "symlink-merge.tar",
        "sha256": "${merge_sha256}"
      },
      {
        "type": "dir",
        "path": "dir-source"
      }
    ],
    "build-commands": [
      "test -L lib",
      "cp lib64/libfoo.txt /app/libfoo.txt"
    ]
  }]
}
EOF

run_build test-dir-source-symlink-merge.json

assert_file_has_content appdir/files/libfoo.txt "hello from dir source"

ok "dir source merges into existing in-tree symlinked directory"

mkdir -p outside
mkdir -p symlink-escape/toplevel
ln -s "${TEST_DATA_DIR}/outside" symlink-escape/toplevel/lib
tar -C symlink-escape -cf symlink-escape.tar toplevel
escape_sha256=$(sha256sum symlink-escape.tar | cut -d' ' -f1)

cat > test-dir-source-symlink-escape.json <<EOF
{
  "app-id": "org.test.DirSourceSymlinkEscape",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "modules": [{
    "name": "test",
    "buildsystem": "simple",
    "sources": [
      {
        "type": "archive",
        "path": "symlink-escape.tar",
        "sha256": "${escape_sha256}"
      },
      {
        "type": "dir",
        "path": "dir-source"
      }
    ],
    "build-commands": [
      "true"
    ]
  }]
}
EOF

run_build_fail test-dir-source-symlink-escape.json

assert_not_has_file outside/libfoo.txt

ok "dir source does not write through symlink escaping the build dir"

done_testing
