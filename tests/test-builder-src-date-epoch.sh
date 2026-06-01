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

cd "$TEST_DATA_DIR"

cat > test-src-date-epoch-default.json <<'EOF'
{
    "app-id": "org.test.src_date_epoch_default",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo ${SOURCE_DATE_EPOCH} > /app/sde_out"
        ]
    }]
}
EOF

run_build test-src-date-epoch-default.json

assert_file_has_content appdir/files/sde_out '^1321009871$'

echo "ok source-date-epoch default fixed epoch is set"

cat > test-src-date-epoch-override.json <<'EOF'
{
    "app-id": "org.test.src_date_epoch_override",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo ${SOURCE_DATE_EPOCH} > /app/sde_out"
        ]
    }]
}
EOF

run_build --override-source-date-epoch=1234567890 test-src-date-epoch-override.json

assert_file_has_content appdir/files/sde_out '^1234567890$'

echo "ok source-date-epoch override value is used"

cat > test-src-date-epoch-disable.json <<'EOF'
{
    "app-id": "org.test.src_date_epoch_disable",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "echo ${SOURCE_DATE_EPOCH:-unset} > /app/sde_out"
        ]
    }]
}
EOF

run_build --override-source-date-epoch=0 test-src-date-epoch-disable.json

assert_file_has_content appdir/files/sde_out '^unset$'

echo "ok source-date-epoch is unset when disabled with 0"
