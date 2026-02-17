#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for systemd service file templating
# Verifies placeholders exist in template and substitution works correctly

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
SERVICE_TEMPLATE="$WORKTREE_ROOT/config/helixscreen.service"

setup() {
    load helpers
}

# --- Template placeholder tests ---

@test "service template has @@HELIX_USER@@ placeholder" {
    grep -q '@@HELIX_USER@@' "$SERVICE_TEMPLATE"
}

@test "service template has @@HELIX_GROUP@@ placeholder" {
    grep -q '@@HELIX_GROUP@@' "$SERVICE_TEMPLATE"
}

@test "service template has @@INSTALL_DIR@@ in WorkingDirectory" {
    grep -q 'WorkingDirectory=@@INSTALL_DIR@@' "$SERVICE_TEMPLATE"
}

@test "service template has @@INSTALL_DIR@@ in ExecStart" {
    grep -q 'ExecStart=@@INSTALL_DIR@@' "$SERVICE_TEMPLATE"
}

@test "service template does NOT contain hardcoded User=root" {
    ! grep -q '^User=root' "$SERVICE_TEMPLATE"
}

@test "service template does NOT contain ProtectHome=read-only" {
    ! grep -q 'ProtectHome=read-only' "$SERVICE_TEMPLATE"
}

@test "service template HAS ReadWritePaths directive" {
    grep -q 'ReadWritePaths=' "$SERVICE_TEMPLATE"
}

@test "service template HAS ProtectSystem=strict" {
    grep -q 'ProtectSystem=strict' "$SERVICE_TEMPLATE"
}

@test "service template HAS SupplementaryGroups=video input render tty" {
    grep -q 'SupplementaryGroups=video input render tty' "$SERVICE_TEMPLATE"
}

# --- Substitution tests (copy to tmpdir, run sed, verify) ---

@test "substitution replaces all @@HELIX_USER@@ with biqu" {
    cp "$SERVICE_TEMPLATE" "$BATS_TEST_TMPDIR/test.service"
    sed -i '' "s|@@HELIX_USER@@|biqu|g" "$BATS_TEST_TMPDIR/test.service" 2>/dev/null || \
    sed -i "s|@@HELIX_USER@@|biqu|g" "$BATS_TEST_TMPDIR/test.service"
    grep -q 'User=biqu' "$BATS_TEST_TMPDIR/test.service"
}

@test "substitution replaces all @@INSTALL_DIR@@ with /opt/helixscreen" {
    cp "$SERVICE_TEMPLATE" "$BATS_TEST_TMPDIR/test.service"
    sed -i '' "s|@@INSTALL_DIR@@|/opt/helixscreen|g" "$BATS_TEST_TMPDIR/test.service" 2>/dev/null || \
    sed -i "s|@@INSTALL_DIR@@|/opt/helixscreen|g" "$BATS_TEST_TMPDIR/test.service"
    grep -q 'WorkingDirectory=/opt/helixscreen' "$BATS_TEST_TMPDIR/test.service"
    grep -q 'ExecStart=/opt/helixscreen/bin/helix-launcher.sh' "$BATS_TEST_TMPDIR/test.service"
}

# --- SupplementaryGroups filtering tests ---
# These simulate the installer's group filtering logic

@test "SupplementaryGroups filtered to only existing groups" {
    # This test requires Linux groups (video/input/render) — skip on macOS
    getent group video >/dev/null 2>&1 || getent group input >/dev/null 2>&1 || skip "no video/input groups (not Linux)"
    cp "$SERVICE_TEMPLATE" "$BATS_TEST_TMPDIR/test.service"
    # Simulate filtering: keep only groups that exist on this system
    desired_groups=$(grep '^SupplementaryGroups=' "$BATS_TEST_TMPDIR/test.service" | sed 's/^SupplementaryGroups=//')
    existing_groups=""
    for grp in $desired_groups; do
        if getent group "$grp" >/dev/null 2>&1; then
            existing_groups="${existing_groups:+$existing_groups }$grp"
        fi
    done
    [ -n "$existing_groups" ]
    sed -i '' "s|^SupplementaryGroups=.*|SupplementaryGroups=$existing_groups|" "$BATS_TEST_TMPDIR/test.service" 2>/dev/null || \
    sed -i "s|^SupplementaryGroups=.*|SupplementaryGroups=$existing_groups|" "$BATS_TEST_TMPDIR/test.service"
    grep -q "^SupplementaryGroups=$existing_groups" "$BATS_TEST_TMPDIR/test.service"
}

@test "SupplementaryGroups line removed when no groups exist" {
    cp "$SERVICE_TEMPLATE" "$BATS_TEST_TMPDIR/test.service"
    # Replace with groups that definitely don't exist
    sed -i '' 's|^SupplementaryGroups=.*|SupplementaryGroups=fakegrp1 fakegrp2|' "$BATS_TEST_TMPDIR/test.service" 2>/dev/null || \
    sed -i 's|^SupplementaryGroups=.*|SupplementaryGroups=fakegrp1 fakegrp2|' "$BATS_TEST_TMPDIR/test.service"
    # Now simulate the filtering logic
    desired_groups=$(grep '^SupplementaryGroups=' "$BATS_TEST_TMPDIR/test.service" | sed 's/^SupplementaryGroups=//')
    existing_groups=""
    for grp in $desired_groups; do
        if getent group "$grp" >/dev/null 2>&1; then
            existing_groups="${existing_groups:+$existing_groups }$grp"
        fi
    done
    # No groups matched — remove the directive
    [ -z "$existing_groups" ]
    sed -i '' '/^SupplementaryGroups=/d' "$BATS_TEST_TMPDIR/test.service" 2>/dev/null || \
    sed -i '/^SupplementaryGroups=/d' "$BATS_TEST_TMPDIR/test.service"
    ! grep -q '^SupplementaryGroups=' "$BATS_TEST_TMPDIR/test.service"
}

@test "no @@ markers remain after full substitution" {
    cp "$SERVICE_TEMPLATE" "$BATS_TEST_TMPDIR/test.service"
    sed -i '' "s|@@HELIX_USER@@|biqu|g;s|@@HELIX_GROUP@@|biqu|g;s|@@INSTALL_DIR@@|/opt/helixscreen|g" "$BATS_TEST_TMPDIR/test.service" 2>/dev/null || \
    sed -i "s|@@HELIX_USER@@|biqu|g;s|@@HELIX_GROUP@@|biqu|g;s|@@INSTALL_DIR@@|/opt/helixscreen|g" "$BATS_TEST_TMPDIR/test.service"
    ! grep -q '@@' "$BATS_TEST_TMPDIR/test.service"
}
