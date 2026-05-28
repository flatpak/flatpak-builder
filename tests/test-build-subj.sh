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

# Flatpak only exposes subject but not body
#
# ostree show format:
#
# commit 5f7703dc87237cfd81ace54e660cc4308c63e0ff59e546332202d0743a0c2f93
# ContentChecksum: c283ccf614d1e5a07b2e9cb4e3eae3ff5373d1cad8a377b7d8fa553271ac2440
# Date: 2026-05-28 05:50:51 +0000
#
#     A subject
#
#     A body
#

ostree_get_subject () {
    local repo=$1
    local ref=$2
    ostree show --repo="$repo" "$ref" | awk '/^$/{found=1; next} found{sub(/^    /, ""); print; exit}'
}

ostree_get_body () {
    local repo=$1
    local ref=$2
    ostree show --repo="$repo" "$ref" | awk '/^$/{found=1; next} found{sub(/^    /, ""); print}' | tail -n +2
}

TEST_GIT_DIR="$(mktemp -d)"

cd "$TEST_GIT_DIR"

git init -q
git config --local user.email "test@flatpak.org"
git config --local user.name "test"
git commit -q --allow-empty -m "Init"

APP_ID="org.test.git_subj"
MANIFEST_NAME="$APP_ID.json"
REPO="$TEST_DATA_DIR/repo"
COMMIT_SUBJ="Add manifest foo bar"

cat > "$MANIFEST_NAME" <<'EOF'
{
    "app-id": "org.test.git_subj",
    "runtime": "org.test.Platform",
    "sdk": "org.test.Sdk",
    "modules": [{
        "name": "test",
        "buildsystem": "simple",
        "build-commands": ["mkdir -p /app"]
    }]
}
EOF

git add "$MANIFEST_NAME"
git commit -q -m "$COMMIT_SUBJ"

COMMIT_HASH="$(git rev-parse --short=12 HEAD)"
MANIFEST_SHA256="$(sha256sum "$MANIFEST_NAME" | awk '{print $1}')"
EXPECTED_SUBJECT="$COMMIT_SUBJ ($COMMIT_HASH)"
EXPECTED_BODY="Manifest checksum: $MANIFEST_SHA256"

${FLATPAK_BUILDER} --force-clean --repo="$REPO"_1 appdir "$MANIFEST_NAME" >&2

REF="$(ostree refs --repo="$REPO"_1 | grep "^app/$APP_ID")"
SUBJECT="$(ostree_get_subject "$REPO"_1 "$REF")"
BODY="$(ostree_get_body "$REPO"_1 "$REF")"

assert_streq "$SUBJECT" "$EXPECTED_SUBJECT"
assert_streq "$BODY" "$EXPECTED_BODY"

echo "ok default subject and body are set from git"

${FLATPAK_BUILDER} --force-clean --repo="$REPO"_2 \
    --subject="Custom subject" --body="Custom body" appdir "$MANIFEST_NAME" >&2

REF="$(ostree refs --repo="$REPO"_2 | grep "^app/$APP_ID")"
SUBJECT="$(ostree_get_subject "$REPO"_2 "$REF")"
BODY="$(ostree_get_body "$REPO"_2 "$REF")"

assert_streq "$SUBJECT" "Custom subject"
assert_streq "$BODY" "Custom body"

echo "ok explicit subject and body override defaults"

rm -rf "$TEST_GIT_DIR/.git"

${FLATPAK_BUILDER} --force-clean --repo="$REPO"_3 appdir "$MANIFEST_NAME" >&2

REF="$(ostree refs --repo="$REPO"_3 | grep "^app/$APP_ID")"
SUBJECT="$(ostree_get_subject "$REPO"_3 "$REF")"
BODY="$(ostree_get_body "$REPO"_3 "$REF")"

assert_streq "$SUBJECT" "Export $APP_ID"
assert_streq "$BODY" "$EXPECTED_BODY"

echo "ok default export subject is used outside git dir"
