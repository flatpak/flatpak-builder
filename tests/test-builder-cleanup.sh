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

run_build () {
    local manifest=$1
    ${FLATPAK_BUILDER} --force-clean appdir "$manifest" >&2
}

cat > test-cleanup-toplevel.json <<'EOF'
{
  "app-id": "org.test.CleanupToplevel",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "cleanup": ["/share/doc"],
  "modules": [
    {
      "name": "mod1",
      "buildsystem": "simple",
      "build-commands": [
        "mkdir -p /app/share/doc",
        "echo test > /app/share/doc/file1"
      ]
    },
    {
      "name": "mod2",
      "buildsystem": "simple",
      "build-commands": [
        "mkdir -p /app/share/doc",
        "echo test > /app/share/doc/file2"
      ]
    }
  ]
}
EOF

run_build test-cleanup-toplevel.json

assert_not_has_file appdir/files/share/doc/file1
assert_not_has_file appdir/files/share/doc/file2
assert_not_has_dir  appdir/files/share/doc

echo "ok toplevel cleanup applies to all modules"

cat > test-cleanup-module-scope.json <<'EOF'
{
  "app-id": "org.test.CleanupModuleScope",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "modules": [
    {
      "name": "mod1",
      "buildsystem": "simple",
      "cleanup": ["/share/doc"],
      "build-commands": [
        "mkdir -p /app/share/doc",
        "echo test > /app/share/doc/file1"
      ]
    },
    {
      "name": "mod2",
      "buildsystem": "simple",
      "build-commands": [
        "mkdir -p /app/share/doc",
        "echo test > /app/share/doc/file2"
      ]
    }
  ]
}
EOF

run_build test-cleanup-module-scope.json

assert_not_has_file appdir/files/share/doc/file1
assert_has_file appdir/files/share/doc/file2

echo "ok module cleanup does not affect other modules"

cat > test-cleanup-basename.json <<'EOF'
{
  "app-id": "org.test.CleanupBasename",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "cleanup": ["*.txt"],
  "modules": [{
    "name": "test",
    "buildsystem": "simple",
    "build-commands": [
      "mkdir -p /app/share",
      "echo a > /app/share/a.txt",
      "echo b > /app/share/b.log"
    ]
  }]
}
EOF

run_build test-cleanup-basename.json

assert_not_has_file appdir/files/share/a.txt
assert_has_file appdir/files/share/b.log

echo "ok basename pattern works"

cat > test-cleanup-star.json <<'EOF'
{
  "app-id": "org.test.CleanupStar",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "cleanup": ["*.txt"],
  "modules": [{
    "name": "test",
    "buildsystem": "simple",
    "build-commands": [
      "mkdir -p /app/a",
      "echo x > /app/a/file.txt",
      "mkdir -p /app/b",
      "echo y > /app/b/file.log"
    ]
  }]
}
EOF

run_build test-cleanup-star.json

assert_not_has_file appdir/files/a/file.txt
assert_has_file appdir/files/b/file.log

echo "ok wildcard matching works"

cat > test-cleanup-module-star-scope.json <<'EOF'
{
  "app-id": "org.test.CleanupModuleStarScope",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "modules": [
    {
      "name": "mod1",
      "buildsystem": "simple",
      "build-commands": [
        "mkdir -p /app/share",
        "echo a > /app/share/file1.txt"
      ]
    },
    {
      "name": "mod2",
      "buildsystem": "simple",
      "cleanup": ["*.txt"],
      "build-commands": [
        "mkdir -p /app/share",
        "echo b > /app/share/file2.txt",
        "echo c > /app/share/file2.log"
      ]
    }
  ]
}
EOF

run_build test-cleanup-module-star-scope.json

assert_has_file appdir/files/share/file1.txt
assert_not_has_file appdir/files/share/file2.txt
assert_has_file appdir/files/share/file2.log

echo "ok wildcard matching works and is scoped to module"

cat > test-cleanup-question.json <<'EOF'
{
  "app-id": "org.test.CleanupQuestion",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "modules": [
    {
      "name": "mod1",
      "buildsystem": "simple",
      "build-commands": [
        "mkdir -p /app/share",
        "echo a > /app/share/a1.txt"
      ]
    },
    {
      "name": "mod2",
      "buildsystem": "simple",
      "cleanup": ["a?.txt"],
      "build-commands": [
        "mkdir -p /app/share",
        "echo b > /app/share/a2.txt",
        "echo c > /app/share/a22.txt",
        "echo d > /app/share/ab.txt"
      ]
    }
  ]
}
EOF

run_build test-cleanup-question.json

assert_has_file appdir/files/share/a1.txt
assert_not_has_file appdir/files/share/a2.txt
assert_has_file appdir/files/share/a22.txt
assert_not_has_file appdir/files/share/ab.txt

echo "ok '?' matches exactly one char and is module-scoped"

cat > test-cleanup-question-slash.json <<'EOF'
{
  "app-id": "org.test.CleanupQuestionSlash",
  "runtime": "org.test.Platform",
  "sdk": "org.test.Sdk",
  "cleanup": ["/a?/file.txt"],
  "modules": [{
    "name": "test",
    "buildsystem": "simple",
    "build-commands": [
      "mkdir -p /app/a1",
      "echo x > /app/a1/file.txt",
      "mkdir -p /app/a/b",
      "echo y > /app/a/b/file.txt"
    ]
  }]
}
EOF

run_build test-cleanup-question-slash.json

assert_not_has_file appdir/files/a1/file.txt
assert_has_file appdir/files/a/b/file.txt

echo "ok '?' does not match '/'"
