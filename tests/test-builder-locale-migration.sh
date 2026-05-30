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

echo "1..4"

setup_repo
install_repo
setup_sdk_repo
install_sdk_repo

cd "$TEST_DATA_DIR"

cat > test-locale-migration.json <<'EOF'
{
    "app-id": "org.test.locale_migration",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "mkdir -p /app/share/locale/de/LC_MESSAGES",
            "echo 'de translation' > /app/share/locale/de/LC_MESSAGES/test.mo",

            "mkdir -p /app/share/locale/C/LC_MESSAGES",
            "echo 'C locale' > /app/share/locale/C/LC_MESSAGES/test.mo",
            "mkdir -p /app/share/locale/en/LC_MESSAGES",
            "echo 'en locale' > /app/share/locale/en/LC_MESSAGES/test.mo",

            "mkdir -p /app/lib/locale/ru/LC_MESSAGES",
            "echo 'ru translation' > /app/lib/locale/ru/LC_MESSAGES/test.mo",

            "mkdir -p /app/share/locale/sr_RS@latin.UTF-8/LC_MESSAGES",
            "echo 'sr combined' > /app/share/locale/sr_RS@latin.UTF-8/LC_MESSAGES/test.mo",

            "mkdir -p /app/share/locale/ko/LC_MESSAGES",
            "echo 'app1' > /app/share/locale/ko/LC_MESSAGES/app1.mo",
            "echo 'app2' > /app/share/locale/ko/LC_MESSAGES/app2.mo",

            "mkdir -p /app/share/locale/es/LC_MESSAGES",
            "echo 'es share' > /app/share/locale/es/LC_MESSAGES/test.mo",
            "mkdir -p /app/lib/locale/es/LC_CTYPE",
            "echo 'es lib' > /app/lib/locale/es/LC_CTYPE/test.mo",

            "mkdir -p /app/share/locale/de_DE/LC_MESSAGES",
            "echo 'de_DE translation' > /app/share/locale/de_DE/LC_MESSAGES/test.mo",
            "mkdir -p /app/share/locale/de_AT/LC_MESSAGES",
            "echo 'de_AT translation' > /app/share/locale/de_AT/LC_MESSAGES/test.mo",

            "touch /app/share/locale/plain-file",

            "mkdir -p /app/share/locale/sv"
        ]
    }]
}
EOF

run_build test-locale-migration.json

# share/locale is migrated to share/runtime/locale/$lang
assert_has_symlink appdir/files/share/locale/de
assert_symlink_has_content appdir/files/share/locale/de 'share/runtime/locale/de'
assert_has_file appdir/files/share/runtime/locale/de/share/de/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/runtime/locale/de/share/de/LC_MESSAGES/test.mo 'de translation'

# lib/locale is migrated to share/runtime/locale/$lang
assert_has_symlink appdir/files/lib/locale/ru
assert_symlink_has_content appdir/files/lib/locale/ru 'share/runtime/locale/ru'
assert_has_file appdir/files/share/runtime/locale/ru/lib/ru/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/runtime/locale/ru/lib/ru/LC_MESSAGES/test.mo 'ru translation'

# C and en locales are not migrated
assert_has_dir appdir/files/share/locale/C
assert_has_file appdir/files/share/locale/C/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/locale/C/LC_MESSAGES/test.mo 'C locale'

assert_has_dir appdir/files/share/locale/en
assert_has_file appdir/files/share/locale/en/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/locale/en/LC_MESSAGES/test.mo 'en locale'

# sr_RS@latin.UTF-8 -> sr
assert_has_symlink appdir/files/share/locale/sr_RS@latin.UTF-8
assert_has_file appdir/files/share/runtime/locale/sr/share/sr_RS@latin.UTF-8/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/runtime/locale/sr/share/sr_RS@latin.UTF-8/LC_MESSAGES/test.mo 'sr combined'

# multiple files in a single locale dir are migrated
assert_has_symlink appdir/files/share/locale/ko
assert_has_file appdir/files/share/runtime/locale/ko/share/ko/LC_MESSAGES/app1.mo
assert_file_has_content appdir/files/share/runtime/locale/ko/share/ko/LC_MESSAGES/app1.mo 'app1'
assert_has_file appdir/files/share/runtime/locale/ko/share/ko/LC_MESSAGES/app2.mo
assert_file_has_content appdir/files/share/runtime/locale/ko/share/ko/LC_MESSAGES/app2.mo 'app2'

# same language in share/locale and lib/locale is merged to same target
assert_has_symlink appdir/files/share/locale/es
assert_has_file appdir/files/share/runtime/locale/es/share/es/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/runtime/locale/es/share/es/LC_MESSAGES/test.mo 'es share'
assert_has_symlink appdir/files/lib/locale/es
assert_has_file appdir/files/share/runtime/locale/es/lib/es/LC_CTYPE/test.mo
assert_file_has_content appdir/files/share/runtime/locale/es/lib/es/LC_CTYPE/test.mo 'es lib'

# language variants are merged
assert_has_symlink appdir/files/share/locale/de_DE
assert_has_symlink appdir/files/share/locale/de_AT
assert_has_file appdir/files/share/runtime/locale/de/share/de_DE/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/runtime/locale/de/share/de_DE/LC_MESSAGES/test.mo 'de_DE translation'
assert_has_file appdir/files/share/runtime/locale/de/share/de_AT/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/runtime/locale/de/share/de_AT/LC_MESSAGES/test.mo 'de_AT translation'

# empty locale dir creates empty migration dir
assert_has_dir appdir/files/share/locale/sv
assert_has_dir appdir/files/share/runtime/locale/sv

# non-directory entries are ignored
assert_has_file appdir/files/share/locale/plain-file
assert_not_has_symlink appdir/files/share/locale/plain-file

echo "ok locale dirs migrated"

cat > test-locale-disabled.json <<'EOF'
{
    "app-id": "org.test.locale_migration_disabled",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "separate-locales": false,
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "mkdir -p /app/share/locale/de/LC_MESSAGES",
            "echo 'de translation' > /app/share/locale/de/LC_MESSAGES/test.mo"
        ]
    }]
}
EOF

run_build test-locale-disabled.json

assert_has_dir appdir/files/share/locale/de
assert_has_file appdir/files/share/locale/de/LC_MESSAGES/test.mo
assert_file_has_content appdir/files/share/locale/de/LC_MESSAGES/test.mo 'de translation'
assert_not_has_dir appdir/files/share/runtime/locale

echo "ok locale dirs migration is disabled on separate-locales false"

cat > test-locale-migration-runtime.json <<'EOF'
{
    "app-id": "org.test.locale_migration_runtime",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "build-runtime": true,
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "mkdir -p /usr/share/locale/pl/LC_MESSAGES",
            "echo 'pl translation' > /usr/share/locale/pl/LC_MESSAGES/test.mo",

            "mkdir -p /usr/lib/locale/cs/LC_MESSAGES",
            "echo 'cs translation' > /usr/lib/locale/cs/LC_MESSAGES/test.mo"
        ]
    }]
}
EOF

run_build test-locale-migration-runtime.json

assert_has_symlink appdir/usr/share/locale/pl
assert_has_file appdir/usr/share/runtime/locale/pl/share/pl/LC_MESSAGES/test.mo
assert_file_has_content appdir/usr/share/runtime/locale/pl/share/pl/LC_MESSAGES/test.mo 'pl translation'

assert_has_symlink appdir/usr/lib/locale/cs
assert_has_file appdir/usr/share/runtime/locale/cs/lib/cs/LC_MESSAGES/test.mo
assert_file_has_content appdir/usr/share/runtime/locale/cs/lib/cs/LC_MESSAGES/test.mo 'cs translation'

echo "ok locale dirs migration works with runtime"

cat > test-locale-no-dirs.json <<'EOF'
{
    "app-id": "org.test.locale_no_dirs",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": [
            "mkdir -p /app/bin",
            "echo test > /app/bin/test"
        ]
    }]
}
EOF

run_build test-locale-no-dirs.json

assert_has_file appdir/files/bin/test
assert_not_has_dir appdir/files/share/runtime/locale

echo "ok locale migration handles missing locale dirs"
