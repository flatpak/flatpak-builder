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

# Default: CGO_CFLAGS=$CFLAGS (inherited from SDK or set explicitly)
# The test SDK does not ship with defaults so it is set here
# via cflags explicitly.
cat > test-cgo-default.json <<'EOF'
{
    "app-id": "org.test.CgoDefault",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "cflags": "-O2 -g"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo $CFLAGS > /app/cflags_out",
            "echo $CGO_CFLAGS > /app/cgo_cflags_out"
        ]
    }]
}
EOF

run_build test-cgo-default.json

assert_file_has_content appdir/files/cflags_out '\-O2 \-g'
assert_file_has_content appdir/files/cgo_cflags_out '\-O2 \-g'

echo "ok cgo-cflags defaults to cflags"

# Explicit cgo-cflags set and no cgo-cflags-override: CGO_CFLAGS=$CFLAGS+explicit
cat > test-cgo-set.json <<'EOF'
{
    "app-id": "org.test.CgoSet",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "cflags": "-O2 -g",
        "cgo-cflags": "-O0"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo $CGO_CFLAGS > /app/cgo_cflags_out"
        ]
    }]
}
EOF

run_build test-cgo-set.json

assert_file_has_content appdir/files/cgo_cflags_out '\-O2 \-g \-O0'

echo "ok explicit cgo-cflags is additive to cflags"

# Explicit cgo-cflags set and `cgo-cflags-override: true`: CGO_CFLAGS=explicit
cat > test-cgo-set-with-override.json <<'EOF'
{
    "app-id": "org.test.CgoSetWithOverride",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "cflags": "-O2 -g",
        "cgo-cflags": "-O0",
        "cgo-cflags-override": true
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo $CFLAGS > /app/cflags_out",
            "echo $CGO_CFLAGS > /app/cgo_cflags_out"
        ]
    }]
}
EOF

run_build test-cgo-set-with-override.json

assert_file_has_content appdir/files/cgo_cflags_out '\-O0'

echo "ok cgo-cflags with override has only explicit flags"

# Only `cgo-cflags-override: true`: CGO_CFLAGS is unset
cat > test-cgo-only-override.json <<'EOF'
{
    "app-id": "org.test.CgoOnlyOverride",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "cflags": "-O2 -g",
        "cgo-cflags-override": true
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo ${CGO_CFLAGS:-unset} > /app/cgo_cflags_out"
        ]
    }]
}
EOF

run_build test-cgo-only-override.json

assert_file_has_content appdir/files/cgo_cflags_out '^unset$'

echo "ok only cgo-cflags-override clears CGO_CFLAGS"

# Default: RUSTFLAGS should be passed. The test SDK does not ship with
# defaults so it is set here via rustflags explicitly.
cat > test-rustflags-set.json <<'EOF'
{
    "app-id": "org.test.RustflagsSet",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "rustflags": "-C opt-level=2"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo ${RUSTFLAGS} > /app/rustflags_out"
        ]
    }]
}
EOF

run_build test-rustflags-set.json

assert_file_has_content appdir/files/rustflags_out '\-C opt\-level=2'

echo "ok rustflags is passed by default"

# rustflags-override at module level clears manifest-level rustflags
# or equivalently clears SDK rustflags
cat > test-rustflags-override.json <<'EOF'
{
    "app-id": "org.test.RustflagsOverride",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "rustflags": "-C debuginfo=0"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-options": {
            "rustflags": "-C opt-level=3",
            "rustflags-override": true
        },
        "build-commands": [
            "echo ${RUSTFLAGS} > /app/rustflags_out"
        ]
    }]
}
EOF

run_build test-rustflags-override.json

assert_file_has_content appdir/files/rustflags_out '\-C opt\-level=3'
assert_not_file_has_content appdir/files/rustflags_out 'debuginfo'

echo "ok rustflags-override at module level clears manifest-level rustflags"

# Only rustflags-override is set, rustflags should be cleared
cat > test-rustflags-only-override.json <<'EOF'
{
    "app-id": "org.test.RustflagsOnlyOverride",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "rustflags": "-C debuginfo=0"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-options": {
            "rustflags-override": true
        },
        "build-commands": [
            "echo ${RUSTFLAGS:-unset} > /app/rustflags_out"
        ]
    }]
}
EOF

run_build test-rustflags-only-override.json

assert_file_has_content appdir/files/rustflags_out '^unset$'

echo "ok only rustflags-override clears rustflags"
