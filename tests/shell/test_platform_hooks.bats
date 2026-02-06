#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for platform hook files.
# Verifies the hook contract (all required functions defined),
# shellcheck compliance, and basic syntax validity.

HOOKS_DIR="config/platform"
HOOK_FILES="hooks-ad5m-forgex.sh hooks-ad5m-kmod.sh hooks-pi.sh hooks-k1.sh"
REQUIRED_FUNCTIONS="platform_stop_competing_uis platform_enable_backlight platform_wait_for_services platform_pre_start platform_post_stop"

# --- Hook contract tests: every hook file must define all 5 functions ---

@test "forgex hooks define all required functions" {
    ( . "$HOOKS_DIR/hooks-ad5m-forgex.sh"
      for func in $REQUIRED_FUNCTIONS; do
          type "$func" >/dev/null 2>&1
      done )
}

@test "kmod hooks define all required functions" {
    ( . "$HOOKS_DIR/hooks-ad5m-kmod.sh"
      for func in $REQUIRED_FUNCTIONS; do
          type "$func" >/dev/null 2>&1
      done )
}

@test "pi hooks define all required functions" {
    ( . "$HOOKS_DIR/hooks-pi.sh"
      for func in $REQUIRED_FUNCTIONS; do
          type "$func" >/dev/null 2>&1
      done )
}

@test "k1 hooks define all required functions" {
    ( . "$HOOKS_DIR/hooks-k1.sh"
      for func in $REQUIRED_FUNCTIONS; do
          type "$func" >/dev/null 2>&1
      done )
}

# --- Shellcheck compliance ---

@test "forgex hooks pass shellcheck" {
    shellcheck -s sh "$HOOKS_DIR/hooks-ad5m-forgex.sh"
}

@test "kmod hooks pass shellcheck" {
    shellcheck -s sh "$HOOKS_DIR/hooks-ad5m-kmod.sh"
}

@test "pi hooks pass shellcheck" {
    shellcheck -s sh "$HOOKS_DIR/hooks-pi.sh"
}

@test "k1 hooks pass shellcheck" {
    shellcheck -s sh "$HOOKS_DIR/hooks-k1.sh"
}

# --- Syntax validity ---

@test "forgex hooks have valid sh syntax" {
    sh -n "$HOOKS_DIR/hooks-ad5m-forgex.sh"
}

@test "kmod hooks have valid sh syntax" {
    sh -n "$HOOKS_DIR/hooks-ad5m-kmod.sh"
}

@test "pi hooks have valid sh syntax" {
    sh -n "$HOOKS_DIR/hooks-pi.sh"
}

@test "k1 hooks have valid sh syntax" {
    sh -n "$HOOKS_DIR/hooks-k1.sh"
}

# --- Init script integration tests ---

INIT_SCRIPT="config/helixscreen.init"

@test "init script defines no-op defaults" {
    # The no-op defaults should be defined even without a hook file present
    grep -q 'platform_stop_competing_uis().*:' "$INIT_SCRIPT"
    grep -q 'platform_enable_backlight().*:' "$INIT_SCRIPT"
    grep -q 'platform_wait_for_services().*:' "$INIT_SCRIPT"
    grep -q 'platform_pre_start().*:' "$INIT_SCRIPT"
    grep -q 'platform_post_stop().*:' "$INIT_SCRIPT"
}

@test "init script sources platform hooks" {
    grep -q 'PLATFORM_HOOKS' "$INIT_SCRIPT"
    grep -q '\. "\$PLATFORM_HOOKS"' "$INIT_SCRIPT"
}

@test "init script has no inline backlight function" {
    # The enable_backlight function should no longer exist
    ! grep -q '^enable_backlight()' "$INIT_SCRIPT"
}

@test "init script has no inline Moonraker wait function" {
    # The wait_for_moonraker function should no longer exist
    ! grep -q '^wait_for_moonraker()' "$INIT_SCRIPT"
}

@test "init script has no inline competing UI function" {
    # The stop_competing_uis function should no longer exist
    ! grep -q '^stop_competing_uis()' "$INIT_SCRIPT"
}

@test "init script calls platform hooks in start" {
    grep -q 'platform_pre_start' "$INIT_SCRIPT"
    grep -q 'platform_stop_competing_uis' "$INIT_SCRIPT"
    grep -q 'platform_enable_backlight' "$INIT_SCRIPT"
    grep -q 'platform_wait_for_services' "$INIT_SCRIPT"
}

@test "init script calls platform hooks in stop" {
    grep -q 'platform_post_stop' "$INIT_SCRIPT"
}

@test "init script passes sh syntax check" {
    sh -n "$INIT_SCRIPT"
}

@test "init script has start/stop/restart/status cases" {
    grep -q 'start)' "$INIT_SCRIPT"
    grep -q 'stop)' "$INIT_SCRIPT"
    grep -q 'restart' "$INIT_SCRIPT"
    grep -q 'status)' "$INIT_SCRIPT"
}

# --- Bundle integrity tests ---

@test "bundled installer passes syntax check" {
    sh -n scripts/install.sh
}

@test "bundled installer contains configure_platform" {
    grep -q 'configure_platform' scripts/install.sh
}

@test "bundled installer contains deploy_platform_hooks" {
    grep -q 'deploy_platform_hooks' scripts/install.sh
}

@test "bundled installer contains detect_klipper_user" {
    grep -q 'detect_klipper_user' scripts/install.sh
}

@test "bundled installer contains record_disabled_service" {
    grep -q 'record_disabled_service' scripts/install.sh
}

@test "bundled installer contains fix_install_ownership" {
    grep -q 'fix_install_ownership' scripts/install.sh
}

@test "bundled installer contains detect_pi_install_dir function" {
    grep -q 'detect_pi_install_dir()' scripts/install.sh
}

@test "bundled installer calls detect_pi_install_dir in Pi branch" {
    # The Pi branch of set_install_paths must call detect_pi_install_dir
    grep -A5 'detect_klipper_user' scripts/install.sh | grep -q 'detect_pi_install_dir'
}

@test "bundled installer captures _USER_INSTALL_DIR" {
    grep -q '_USER_INSTALL_DIR=' scripts/install.sh
}

# --- Parity tests: platform.sh functions must exist in bundled install.sh ---
# These catch the case where a function is added to platform.sh but not
# to the bundled installer (which is a separate copy of the modules).

@test "parity: all platform.sh functions exist in bundled installer" {
    # Extract function names from platform.sh
    local funcs
    funcs=$(grep -E '^[a-z_]+\(\)' scripts/lib/installer/platform.sh | sed 's/().*//')
    for func in $funcs; do
        if ! grep -q "${func}()" scripts/install.sh; then
            echo "MISSING in install.sh: ${func}()"
            return 1
        fi
    done
}

@test "parity: Pi branch in install.sh matches platform.sh" {
    # Both files should call detect_pi_install_dir (not hardcode /opt/helixscreen)
    # in their Pi/else branch of set_install_paths
    ! grep -A3 'Pi and other platforms' scripts/install.sh | grep -q 'INSTALL_DIR="/opt/helixscreen"'
    ! grep -A3 'Pi and other platforms' scripts/lib/installer/platform.sh | grep -q 'INSTALL_DIR="/opt/helixscreen"'
}
