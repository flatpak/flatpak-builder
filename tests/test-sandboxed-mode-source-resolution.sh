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

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd "$TEST_DATA_DIR"


mkdir file-workdir
cd file-workdir

CURDIR="$(pwd)"

echo "test" > test.txt

SHA=$(sha256sum test.txt | awk '{print $1}')

cat > test-path-inside.json <<'EOF'
{
    "app-id": "org.test.sandbox_path_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp test.txt /app/test.txt"
        ],
        "sources": [{
            "type": "file",
            "path": "test.txt"
        }]
    }]
}
EOF

run_build --sandbox test-path-inside.json
assert_has_file appdir/files/test.txt
assert_file_has_content appdir/files/test.txt 'test'

ok "path source inside manifest tree works for file type in sandboxed mode"

cat > test-file-url-inside.json <<EOF
{
    "app-id": "org.test.sandbox_file_url_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp test.txt /app/test.txt"
        ],
        "sources": [{
            "type": "file",
            "url": "file://$CURDIR/test.txt",
            "sha256": "$SHA"
        }]
    }]
}
EOF

run_build --sandbox test-file-url-inside.json
assert_has_file appdir/files/test.txt
assert_file_has_content appdir/files/test.txt 'test'

ok "file URI source inside manifest tree works for file type in sandboxed mode"

mkdir tmp_1
cd tmp_1

cat > test-path-outside.json <<'EOF'
{
    "app-id": "org.test.sandbox_path_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "file",
            "path": "../test.txt"
        }]
    }]
}
EOF

BUILD_LOG=build-log-file-path-outside run_build_fail --sandbox \
    test-path-outside.json
assert_file_has_content build-log-file-path-outside 'not inside manifest directory'

ok "path source outside manifest tree is rejected for file type in sandboxed mode"

cat > test-file-url-outside.json <<EOF
{
    "app-id": "org.test.sandbox_file_url_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "file",
            "url": "file://$CURDIR/test.txt",
            "sha256": "$SHA"
        }]
    }]
}
EOF

BUILD_LOG=build-log-file-url-outside run_build_fail --sandbox \
    test-file-url-outside.json
assert_file_has_content build-log-file-url-outside 'not inside manifest directory'

ok "file URI source outside manifest tree is rejected for file type in sandboxed mode"

cd "$TEST_DATA_DIR"
mkdir archive-workdir
cd archive-workdir

CURDIR="$(pwd)"

mkdir -p archive-src
echo "archived content" > archive-src/file.txt
tar -czf archive-src.tar.gz -C archive-src .

SHA=$(sha256sum archive-src.tar.gz | awk '{print $1}')

cat > test-archive-path-inside.json <<'EOF'
{
    "app-id": "org.test.sandbox_archive_path_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp file.txt /app/file.txt"
        ],
        "sources": [{
            "type": "archive",
            "path": "archive-src.tar.gz"
        }]
    }]
}
EOF

run_build --sandbox test-archive-path-inside.json
assert_has_file appdir/files/file.txt
assert_file_has_content appdir/files/file.txt 'archived content'

ok "path source inside manifest tree works for archive type in sandboxed mode"

cat > test-archive-file_uri-inside.json <<EOF
{
    "app-id": "org.test.sandbox_archive_file_uri_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp file.txt /app/file.txt"
        ],
        "sources": [{
            "type": "archive",
            "url": "file://$CURDIR/archive-src.tar.gz",
            "sha256": "$SHA"
        }]
    }]
}
EOF

run_build --sandbox test-archive-file_uri-inside.json
assert_has_file appdir/files/file.txt
assert_file_has_content appdir/files/file.txt 'archived content'

ok "file URI source inside manifest tree works for archive type in sandboxed mode"

mkdir tmp_1
cd tmp_1

cat > test-archive-path-outside.json <<'EOF'
{
    "app-id": "org.test.sandbox_archive_path_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "path": "../archive-src.tar.gz"
        }]
    }]
}
EOF

BUILD_LOG=build-log-archive-path-outside run_build_fail --sandbox \
    test-archive-path-outside.json
assert_file_has_content build-log-archive-path-outside 'not inside manifest directory'

ok "path source outside manifest tree is rejected for archive type in sandboxed mode"

cat > test-archive-file_uri-outside.json <<EOF
{
    "app-id": "org.test.sandbox_archive_file_uri_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "url": "file://$CURDIR/archive-src.tar.gz",
            "sha256": "$SHA"
        }]
    }]
}
EOF

BUILD_LOG=build-log-archive-file_uri-outside run_build_fail --sandbox \
     test-archive-file_uri-outside.json
assert_file_has_content build-log-archive-file_uri-outside 'not inside manifest directory'

ok "file URI source outside manifest tree is rejected for archive type in sandboxed mode"

cp ../archive-src.tar.gz .

cat > test-archive-mirror_uri-outside.json <<EOF
{
    "app-id": "org.test.sandbox_archive_mirror_uri_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "archive",
            "url": "https://example.org/archive-src.tar.gz",
            "mirror-urls": ["file://$CURDIR/archive-src.tar.gz"],
            "sha256": "$SHA"
        }]
    }]
}
EOF

BUILD_LOG=build-log-archive-mirror_uri-outside run_build_fail --sandbox \
     test-archive-mirror_uri-outside.json
assert_file_has_content build-log-archive-mirror_uri-outside 'not inside manifest directory'

ok "mirror URI source outside manifest tree is rejected for archive type in sandboxed mode"

cd "$TEST_DATA_DIR"
mkdir patch-workdir
cd patch-workdir

mkdir -p archive-src
echo "archived content" > archive-src/file.txt
tar -czf archive-src.tar.gz -C archive-src .

cat > fix.patch <<'EOF'
--- a/file.txt
+++ b/file.txt
@@ -1 +1 @@
-archived content
+patched content
EOF

cat > test-patch-path-inside.json <<'EOF'
{
    "app-id": "org.test.sandbox_patch_path_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp file.txt /app"
        ],
        "sources": [
            {
                "type": "archive",
                "path": "archive-src.tar.gz"
            },
            {
                "type": "patch",
                "path": "fix.patch"
            }
        ]
    }]
}
EOF

run_build --sandbox test-patch-path-inside.json
assert_has_file appdir/files/file.txt
assert_file_has_content appdir/files/file.txt 'patched content'

ok "path source inside manifest tree works for patch type in sandboxed mode"

mkdir tmp_1
cp archive-src.tar.gz tmp_1/
cd tmp_1

cat > test-patch-path-outside.json <<'EOF'
{
    "app-id": "org.test.sandbox_patch_path_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [
            {
                "type": "archive",
                "path": "archive-src.tar.gz"
            },
            {
                "type": "patch",
                "path": "../fix.patch"
            }
        ]
    }]
}
EOF

BUILD_LOG=build-log-patch-path-outside run_build_fail --sandbox \
    test-patch-path-outside.json
assert_file_has_content build-log-patch-path-outside 'not inside manifest directory'

ok "path source outside manifest tree is rejected for patch type in sandboxed mode"

cd "$TEST_DATA_DIR"
mkdir git-workdir
cd git-workdir

CURDIR="$(pwd)"

mkdir git-repo
( cd git-repo \
  && git -c init.defaultBranch=test init -q \
  && git config --local user.email "test@flatpak.org" \
  && git config --local user.name "test" \
  && echo "git content" > file.txt \
  && git add file.txt \
  && git commit -q -m "Add file" )


cat > test-git-path-inside.json <<'EOF'
{
    "app-id": "org.test.sandbox_git_path_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp file.txt /app/file.txt"
        ],
        "sources": [{
            "type": "git",
            "path": "git-repo",
            "branch": "test"
        }]
    }]
}
EOF

run_build --sandbox test-git-path-inside.json
assert_has_file appdir/files/file.txt
assert_file_has_content appdir/files/file.txt 'git content'

ok "path source inside manifest tree works for git type in sandboxed mode"

cat > test-git-file_uri-inside.json <<EOF
{
    "app-id": "org.test.sandbox_git_file_uri_inside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "cp file.txt /app/file.txt"
        ],
        "sources": [{
            "type": "git",
            "url": "file://$CURDIR/git-repo",
            "branch": "test"
        }]
    }]
}
EOF

run_build --sandbox test-git-file_uri-inside.json
assert_has_file appdir/files/file.txt
assert_file_has_content appdir/files/file.txt 'git content'

ok "file URI source inside manifest tree works for git type in sandboxed mode"

mkdir tmp_1
cd tmp_1

cat > test-git-path-outside.json <<'EOF'
{
    "app-id": "org.test.sandbox_git_path_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "git",
            "path": "../git-repo",
            "branch": "test"
        }]
    }]
}
EOF

BUILD_LOG=build-log-git-path-outside run_build_fail --sandbox \
    test-git-path-outside.json
assert_file_has_content build-log-git-path-outside 'not inside manifest directory'

ok "path source outside manifest tree is rejected for git type in sandboxed mode"

cat > test-git-file_uri-outside.json <<EOF
{
    "app-id": "org.test.sandbox_git_file_uri_outside",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [],
        "sources": [{
            "type": "git",
            "url": "file://$CURDIR/git-repo",
            "branch": "test"
        }]
    }]
}
EOF

BUILD_LOG=build-log-git-file_uri-outside run_build_fail --sandbox \
    test-git-file_uri-outside.json
assert_file_has_content build-log-git-file_uri-outside 'not inside manifest directory'

ok "file URI source outside manifest tree is rejected for git type in sandboxed mode"

done_testing
