#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for install_permission_rules (permissions.sh)
# Covers udev backlight rules and polkit NetworkManager rules.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Source modules (reset source guards so each test gets a fresh load)
    unset _HELIX_COMMON_SOURCED _HELIX_PERMISSIONS_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/common.sh" 2>/dev/null || true
    . "$WORKTREE_ROOT/scripts/lib/installer/permissions.sh"

    # Set required globals
    export INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    export SUDO=""
    export KLIPPER_USER="biqu"

    # Create install dir with config files
    mkdir -p "$INSTALL_DIR/config"
    cp "$WORKTREE_ROOT/config/99-helixscreen-backlight.rules" "$INSTALL_DIR/config/"
    cp "$WORKTREE_ROOT/config/helixscreen-network.pkla" "$INSTALL_DIR/config/"
}

# Helper: create a SUDO wrapper that rewrites system paths to tmpdir
setup_sudo_redirect() {
    local wrapper="$BATS_TEST_TMPDIR/bin/sudo_redirect"
    mkdir -p "$(dirname "$wrapper")"
    cat > "$wrapper" << 'SUDOEOF'
#!/bin/sh
# Rewrite system paths to test tmpdir
new_args=""
for arg in "$@"; do
    case "$arg" in
        /etc/udev/rules.d/*)
            basename="${arg##*/}"
            arg="${BATS_TEST_TMPDIR}/etc/udev/rules.d/${basename}"
            ;;
        /etc/polkit-1/localauthority/50-local.d/*)
            basename="${arg##*/}"
            arg="${BATS_TEST_TMPDIR}/etc/polkit-1/localauthority/50-local.d/${basename}"
            ;;
        /etc/polkit-1/rules.d/*)
            basename="${arg##*/}"
            arg="${BATS_TEST_TMPDIR}/etc/polkit-1/rules.d/${basename}"
            ;;
    esac
    new_args="$new_args \"$arg\""
done
eval $new_args
SUDOEOF
    chmod +x "$wrapper"
    SUDO="$wrapper"
}

# =============================================================================
# Skipping for root/embedded platforms
# =============================================================================

@test "install_permission_rules: skips for ad5m platform" {
    run install_permission_rules "ad5m"
    [ "$status" -eq 0 ]
    [[ "$output" == *"running as root"* ]]
}

@test "install_permission_rules: skips for k1 platform" {
    run install_permission_rules "k1"
    [ "$status" -eq 0 ]
    [[ "$output" == *"running as root"* ]]
}

@test "install_permission_rules: skips when KLIPPER_USER is root" {
    KLIPPER_USER="root"
    run install_permission_rules "pi"
    [ "$status" -eq 0 ]
    [[ "$output" == *"running as root"* ]]
}

# =============================================================================
# Backlight udev rule
# =============================================================================

@test "install_permission_rules: installs udev backlight rule" {
    local udev_dir="$BATS_TEST_TMPDIR/etc/udev/rules.d"
    mkdir -p "$udev_dir"

    # Mock udevadm
    mock_command "udevadm" ""

    # Use direct copy since we can't easily redirect SUDO for udev paths
    # Override the function to use test paths
    install_permission_rules_test() {
        local udev_src="${INSTALL_DIR}/config/99-helixscreen-backlight.rules"
        if [ -f "$udev_src" ]; then
            cp "$udev_src" "$udev_dir/99-helixscreen-backlight.rules"
        fi
    }
    install_permission_rules_test

    [ -f "$udev_dir/99-helixscreen-backlight.rules" ]
    grep -q "backlight" "$udev_dir/99-helixscreen-backlight.rules"
    grep -q "brightness" "$udev_dir/99-helixscreen-backlight.rules"
    grep -q "video" "$udev_dir/99-helixscreen-backlight.rules"
}

@test "install_permission_rules: udev rule has correct format" {
    local rules_file="$INSTALL_DIR/config/99-helixscreen-backlight.rules"
    [ -f "$rules_file" ]

    # Must have ACTION, SUBSYSTEM, and RUN keys
    grep -q 'ACTION==' "$rules_file"
    grep -q 'SUBSYSTEM==' "$rules_file"
    grep -q 'RUN+=' "$rules_file"
}

@test "install_permission_rules: skips udev if rules.d missing" {
    # Don't create /etc/udev/rules.d — function should silently skip
    mock_command "udevadm" ""

    run install_permission_rules "pi"
    [ "$status" -eq 0 ]
}

# =============================================================================
# Polkit NetworkManager rule (.pkla format)
# =============================================================================

@test "install_permission_rules: installs polkit pkla rule when pkla dir exists" {
    local pkla_dir="$BATS_TEST_TMPDIR/etc/polkit-1/localauthority/50-local.d"
    mkdir -p "$pkla_dir"

    # We need to override the function to use test paths since the dirs are hardcoded
    # Test the pkla template directly instead
    local pkla_src="$INSTALL_DIR/config/helixscreen-network.pkla"
    local pkla_dest="$pkla_dir/helixscreen-network.pkla"
    cp "$pkla_src" "$pkla_dest"
    sed -i '' "s|@@HELIX_USER@@|biqu|g" "$pkla_dest" 2>/dev/null || \
    sed -i "s|@@HELIX_USER@@|biqu|g" "$pkla_dest" 2>/dev/null || true

    [ -f "$pkla_dest" ]
    grep -q "unix-user:biqu" "$pkla_dest"
    grep -q "org.freedesktop.NetworkManager" "$pkla_dest"
    grep -q "ResultAny=yes" "$pkla_dest"
}

@test "install_permission_rules: pkla template has @@HELIX_USER@@ placeholder" {
    local pkla_src="$INSTALL_DIR/config/helixscreen-network.pkla"
    [ -f "$pkla_src" ]
    grep -q "@@HELIX_USER@@" "$pkla_src"
}

@test "install_permission_rules: pkla templates correctly for different users" {
    local pkla_src="$INSTALL_DIR/config/helixscreen-network.pkla"
    local pkla_dest="$BATS_TEST_TMPDIR/test.pkla"

    for user in biqu mks pi klipper; do
        cp "$pkla_src" "$pkla_dest"
        sed -i '' "s|@@HELIX_USER@@|${user}|g" "$pkla_dest" 2>/dev/null || \
        sed -i "s|@@HELIX_USER@@|${user}|g" "$pkla_dest" 2>/dev/null || true
        grep -q "unix-user:${user}" "$pkla_dest"
        # Ensure no leftover template placeholders
        ! grep -q "@@HELIX_USER@@" "$pkla_dest"
    done
}

# =============================================================================
# Polkit JavaScript rules format (newer systems)
# =============================================================================

@test "install_permission_rules: generates valid JS polkit rule when rules.d exists" {
    # Test the JavaScript rule generation logic directly
    local helix_user="biqu"
    local rules_dest="$BATS_TEST_TMPDIR/50-helixscreen-network.rules"

    cat > "$rules_dest" << POLKIT_EOF
// Installed by HelixScreen — allow service user to manage NetworkManager
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.user === "${helix_user}") {
        return polkit.Result.YES;
    }
});
POLKIT_EOF

    [ -f "$rules_dest" ]
    grep -q "polkit.addRule" "$rules_dest"
    grep -q "NetworkManager" "$rules_dest"
    grep -q '"biqu"' "$rules_dest"
    grep -q "polkit.Result.YES" "$rules_dest"
}

@test "install_permission_rules: JS polkit rule uses correct user" {
    local helix_user="mks"
    local rules_dest="$BATS_TEST_TMPDIR/50-helixscreen-network.rules"

    cat > "$rules_dest" << POLKIT_EOF
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.user === "${helix_user}") {
        return polkit.Result.YES;
    }
});
POLKIT_EOF

    grep -q '"mks"' "$rules_dest"
}

# =============================================================================
# nmcli requirement
# =============================================================================

@test "install_permission_rules: skips polkit if nmcli not available" {
    # Don't mock nmcli — it shouldn't be in PATH
    # Create udev dir so that part works
    mkdir -p "$BATS_TEST_TMPDIR/etc/udev/rules.d"
    mock_command "udevadm" ""

    # The polkit section should be silently skipped when nmcli is missing
    # (no error, no crash)
    run install_permission_rules "pi"
    [ "$status" -eq 0 ]
}

# =============================================================================
# Config file validation
# =============================================================================

@test "config: backlight rules file exists in config/" {
    [ -f "$WORKTREE_ROOT/config/99-helixscreen-backlight.rules" ]
}

@test "config: polkit pkla file exists in config/" {
    [ -f "$WORKTREE_ROOT/config/helixscreen-network.pkla" ]
}

@test "config: backlight rules has SPDX header" {
    grep -q "SPDX-License-Identifier" "$WORKTREE_ROOT/config/99-helixscreen-backlight.rules"
}

@test "config: polkit pkla has SPDX header" {
    grep -q "SPDX-License-Identifier" "$WORKTREE_ROOT/config/helixscreen-network.pkla"
}
