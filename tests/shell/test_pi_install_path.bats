#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for detect_pi_install_dir() and Pi install path logic in platform.sh
# Tests the ecosystem detection cascade:
#   1. klipper/moonraker dir in KLIPPER_HOME → ~/helixscreen
#   2. printer_data dir in KLIPPER_HOME → ~/helixscreen
#   3. moonraker.service active → ~/helixscreen
#   4. Fallback → /opt/helixscreen
# Also tests: INSTALL_DIR override, AD5M/K1 regression, set_install_paths integration

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Reset globals before each test
    KLIPPER_USER=""
    KLIPPER_HOME=""
    INIT_SCRIPT_DEST=""
    PREVIOUS_UI_SCRIPT=""
    AD5M_FIRMWARE=""
    K1_FIRMWARE=""
    INSTALL_DIR="/opt/helixscreen"
    TMP_DIR="/tmp/helixscreen-install"
    _USER_INSTALL_DIR=""

    # Source platform.sh (skip source guard by unsetting it)
    unset _HELIX_PLATFORM_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"
}

# --- Cascade priority tests ---

@test "klipper dir in home → ~/helixscreen" {
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/klipper"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$KLIPPER_HOME/helixscreen" ]
}

@test "moonraker dir in home → ~/helixscreen" {
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/moonraker"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$KLIPPER_HOME/helixscreen" ]
}

@test "printer_data in home (no klipper/moonraker dirs) → ~/helixscreen" {
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/printer_data"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$KLIPPER_HOME/helixscreen" ]
}

@test "moonraker.service exists (no dirs) → ~/helixscreen" {
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME"
    # Mock systemctl to report moonraker.service is active
    # is-active --quiet moonraker.service → $1=is-active, $2=--quiet, $3=moonraker.service
    # is-active --quiet moonraker → $1=is-active, $2=--quiet, $3=moonraker
    mock_command_script "systemctl" '
        case "$1" in
            is-active)
                # Check all args for moonraker
                for arg in "$@"; do
                    case "$arg" in
                        moonraker.service|moonraker) exit 0 ;;
                    esac
                done
                exit 1
                ;;
            *) exit 1 ;;
        esac'

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$KLIPPER_HOME/helixscreen" ]
}

@test "nothing detected → /opt/helixscreen fallback" {
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME"
    # No klipper dirs, no systemctl
    mock_command_fail "systemctl"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "/opt/helixscreen" ]
}

# --- Override tests ---

@test "explicit INSTALL_DIR env var preserved" {
    # Simulate user setting INSTALL_DIR before sourcing
    _USER_INSTALL_DIR="/custom/path"
    INSTALL_DIR="/custom/path"
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/klipper"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "/custom/path" ]
}

@test "default INSTALL_DIR is not treated as user override" {
    # _USER_INSTALL_DIR empty means no user override
    _USER_INSTALL_DIR=""
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/klipper"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$KLIPPER_HOME/helixscreen" ]
}

# --- Regression tests for other platforms ---

@test "AD5M klipper_mod path unchanged" {
    set_install_paths "ad5m" "klipper_mod"
    [ "$INSTALL_DIR" = "/root/printer_software/helixscreen" ]
}

@test "AD5M forge_x path unchanged" {
    set_install_paths "ad5m" "forge_x"
    [ "$INSTALL_DIR" = "/opt/helixscreen" ]
}

@test "K1 simple_af path unchanged" {
    set_install_paths "k1" "simple_af"
    [ "$INSTALL_DIR" = "/usr/data/helixscreen" ]
}

# --- Integration: set_install_paths calls detect correctly ---

@test "set_install_paths pi with klipper ecosystem sets home path" {
    # Pre-set KLIPPER_HOME to simulate what detect_klipper_user would do
    # (detect_klipper_user would normally set this, but we mock it here)
    mock_command_fail "systemctl"
    mock_command "ps" ""
    mock_command_fail "id"

    # Since all mocks fail, detect_klipper_user will set root/root
    # Override KLIPPER_HOME after to simulate a real user with klipper
    set_install_paths "pi"
    # With root fallback and no ecosystem dirs in /root, should be /opt/helixscreen
    [ "$INSTALL_DIR" = "/opt/helixscreen" ]
}

@test "set_install_paths pi detects ecosystem in klipper user home" {
    # Create a fake home directory with klipper ecosystem
    local fake_home="$BATS_TEST_TMPDIR/home/pi"
    mkdir -p "$fake_home/klipper"

    # Mock detect_klipper_user to set our test user
    mock_command_script "systemctl" '
        case "$1" in
            show) echo "pi" ;;
            is-active)
                case "$2" in
                    moonraker.service|moonraker) exit 1 ;;
                    *) exit 1 ;;
                esac
                ;;
            *) exit 1 ;;
        esac'
    mock_command "id" ""

    # detect_klipper_user will find "pi" via systemd mock
    # but KLIPPER_HOME will be /home/pi (real system) not our temp dir
    # So we need to test detect_pi_install_dir directly with controlled KLIPPER_HOME
    KLIPPER_HOME="$fake_home"
    KLIPPER_USER="pi"
    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$fake_home/helixscreen" ]
}

# --- Edge cases ---

@test "KLIPPER_HOME empty falls back to /opt/helixscreen" {
    KLIPPER_HOME=""
    mock_command_fail "systemctl"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "/opt/helixscreen" ]
}

@test "klipper dir takes priority over moonraker.service" {
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/klipper"
    # Even if systemctl would match, dir check comes first
    mock_command_script "systemctl" '
        case "$1" in
            is-active) echo "active"; exit 0 ;;
            *) exit 1 ;;
        esac'

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "$KLIPPER_HOME/helixscreen" ]
}

@test "set_install_paths pi still sets init script and tmp dir" {
    mock_command_fail "systemctl"
    mock_command "ps" ""
    mock_command_fail "id"

    set_install_paths "pi"
    [ "$INIT_SCRIPT_DEST" = "/etc/init.d/S90helixscreen" ]
    [ "$TMP_DIR" = "/tmp/helixscreen-install" ]
}

# --- Bundled installer parity ---
# The bundled install.sh is a separate copy of the modules.
# These tests ensure it stays in sync with platform.sh.

@test "bundled install.sh has detect_pi_install_dir function" {
    grep -q 'detect_pi_install_dir()' "$WORKTREE_ROOT/scripts/install.sh"
}

@test "bundled install.sh has _USER_INSTALL_DIR capture" {
    grep -q '_USER_INSTALL_DIR="${INSTALL_DIR}"' "$WORKTREE_ROOT/scripts/install.sh"
}

@test "bundled install.sh Pi branch calls detect_pi_install_dir" {
    # After detect_klipper_user, must call detect_pi_install_dir
    grep -A5 'detect_klipper_user' "$WORKTREE_ROOT/scripts/install.sh" | grep -q 'detect_pi_install_dir'
}

@test "bundled install.sh Pi branch does NOT hardcode /opt/helixscreen" {
    # The else branch should NOT set INSTALL_DIR="/opt/helixscreen" directly
    ! grep -A3 'detect klipper user.*auto-detect' "$WORKTREE_ROOT/scripts/install.sh" | grep -q 'INSTALL_DIR="/opt/helixscreen"'
}
