#!/bin/bash

set -euo pipefail

. $(dirname $0)/libtest.sh

skip_without_fuse

echo "1..5"

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd "$TEST_DATA_DIR"

run_build () {
    local manifest=$1
    ${FLATPAK_BUILDER} --force-clean appdir "$manifest" >&2
}

# No cgo-cflags: CGO_CFLAGS should equal CFLAGS
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

# cgo-cflags set: CGO_CFLAGS should use that value
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
            "echo $CFLAGS > /app/cflags_out",
            "echo $CGO_CFLAGS > /app/cgo_cflags_out"
        ]
    }]
}
EOF
run_build test-cgo-set.json
assert_file_has_content appdir/files/cflags_out '\-O2 \-g'
assert_file_has_content appdir/files/cgo_cflags_out '\-O0'
assert_not_file_has_content appdir/files/cgo_cflags_out '\-O2'
echo "ok cgo-cflags overrides cflags fallback"

# cgo-cflags-override without cgo-cflags: CGO_CFLAGS should not be set
cat > test-cgo-override-empty.json <<'EOF'
{
    "app-id": "org.test.CgoOverrideEmpty",
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
run_build test-cgo-override-empty.json
assert_file_has_content appdir/files/cgo_cflags_out '^unset$'
echo "ok cgo-cflags-override without cgo-cflags suppresses fallback"

# No cgo-cxxflags set: CGO_CXXFLAGS should equal CXXFLAGS
cat > test-cgo-cxx-default.json <<'EOF'
{
    "app-id": "org.test.CgoCxxDefault",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "cxxflags": "-O2 -g"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo $CXXFLAGS > /app/cxxflags_out",
            "echo $CGO_CXXFLAGS > /app/cgo_cxxflags_out"
        ]
    }]
}
EOF
run_build test-cgo-cxx-default.json
assert_file_has_content appdir/files/cxxflags_out '\-O2 \-g'
assert_file_has_content appdir/files/cgo_cxxflags_out '\-O2 \-g'
echo "ok cgo-cxxflags defaults to cxxflags"

# cgo-cxxflags set: CGO_CXXFLAGS should use that value
cat > test-cgo-cxx-set.json <<'EOF'
{
    "app-id": "org.test.CgoCxxSet",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-options": {
        "cxxflags": "-O2 -g",
        "cgo-cxxflags": "-O0"
    },
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo $CXXFLAGS > /app/cxxflags_out",
            "echo $CGO_CXXFLAGS > /app/cgo_cxxflags_out"
        ]
    }]
}
EOF
run_build test-cgo-cxx-set.json
assert_file_has_content appdir/files/cxxflags_out '\-O2 \-g'
assert_file_has_content appdir/files/cgo_cxxflags_out '\-O0'
assert_not_file_has_content appdir/files/cgo_cxxflags_out '\-O2'
echo "ok cgo-cxxflags overrides cxxflags fallback"
