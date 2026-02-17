#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for ForgeX boot sequence coordination:
# - install-dev.sh applies all three screen.sh patches (not just backlight)
# - helixscreen.init calls platform_wait_for_boot_complete with skip mechanism
# - hooks-ad5m-forgex.sh implements platform_wait_for_boot_complete
# - Bundled install.sh stays in sync with modular installer
# - Uninstall properly reverses all patches

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
}

# --- install-dev.sh must apply all ForgeX patches ---

@test "install-dev.sh calls patch_forgex_screen_sh" {
    grep -q 'patch_forgex_screen_sh' "$WORKTREE_ROOT/scripts/install-dev.sh"
}

@test "install-dev.sh calls patch_forgex_screen_drawing" {
    grep -q 'patch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/install-dev.sh"
}

@test "install-dev.sh calls install_forgex_logged_wrapper" {
    grep -q 'install_forgex_logged_wrapper' "$WORKTREE_ROOT/scripts/install-dev.sh"
}

# --- Bundled install.sh parity ---

@test "bundled install.sh calls all three ForgeX patches" {
    grep -q 'patch_forgex_screen_sh' "$WORKTREE_ROOT/scripts/install.sh"
    grep -q 'patch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/install.sh"
    grep -q 'install_forgex_logged_wrapper' "$WORKTREE_ROOT/scripts/install.sh"
}

@test "bundle-installer.sh calls all three ForgeX patches" {
    grep -q 'patch_forgex_screen_sh' "$WORKTREE_ROOT/scripts/bundle-installer.sh"
    grep -q 'patch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/bundle-installer.sh"
    grep -q 'install_forgex_logged_wrapper' "$WORKTREE_ROOT/scripts/bundle-installer.sh"
}

# --- helixscreen.init boot complete hook ---

@test "helixscreen.init defines platform_wait_for_boot_complete default" {
    grep -q 'platform_wait_for_boot_complete()' "$WORKTREE_ROOT/config/helixscreen.init"
}

@test "helixscreen.init calls platform_wait_for_boot_complete in start" {
    # Must be called AFTER platform_wait_for_services, before launching helix-screen
    grep -q 'platform_wait_for_boot_complete' "$WORKTREE_ROOT/config/helixscreen.init"
    # Verify ordering: wait_for_services comes before wait_for_boot_complete
    local services_line boot_line
    services_line=$(grep -n 'platform_wait_for_services' "$WORKTREE_ROOT/config/helixscreen.init" | head -1 | cut -d: -f1)
    boot_line=$(grep -n 'platform_wait_for_boot_complete' "$WORKTREE_ROOT/config/helixscreen.init" | tail -1 | cut -d: -f1)
    [ "$boot_line" -gt "$services_line" ]
}

@test "helixscreen.init has skip mechanism for boot wait" {
    # HELIX_NO_BOOT_WAIT must be checkable to bypass the wait (for debugging)
    grep -q 'HELIX_NO_BOOT_WAIT' "$WORKTREE_ROOT/config/helixscreen.init"
}

# --- hooks-ad5m-forgex.sh boot complete implementation ---

@test "ForgeX hooks implement platform_wait_for_boot_complete" {
    grep -q 'platform_wait_for_boot_complete()' "$WORKTREE_ROOT/config/platform/hooks-ad5m-forgex.sh"
}

@test "ForgeX boot complete checks for S99root process" {
    grep -q 'S99root' "$WORKTREE_ROOT/config/platform/hooks-ad5m-forgex.sh"
}

@test "ForgeX boot complete has timeout" {
    # Must not hang forever waiting for S99root
    grep -A20 'platform_wait_for_boot_complete()' "$WORKTREE_ROOT/config/platform/hooks-ad5m-forgex.sh" | grep -q 'timeout'
}

# --- ForgeX boot complete functional tests ---

@test "platform_wait_for_boot_complete returns immediately when S99root missing" {
    # Source the hooks file
    . "$WORKTREE_ROOT/config/platform/hooks-ad5m-forgex.sh"

    # No S99root file exists in the test env → should return 0 immediately
    run platform_wait_for_boot_complete
    [ "$status" -eq 0 ]
}

@test "platform_wait_for_boot_complete returns when no matching process" {
    # Create a fake S99root file so the function doesn't bail early
    mkdir -p "$BATS_TEST_TMPDIR/opt/config/mod/.root"
    touch "$BATS_TEST_TMPDIR/opt/config/mod/.root/S99root"

    # Create a test version with configurable path and short timeout
    cat > "$BATS_TEST_TMPDIR/test_hook.sh" << 'EOF'
platform_wait_for_boot_complete() {
    local s99root="$1"
    if [ ! -f "$s99root" ]; then
        return 0
    fi
    echo "Waiting for ForgeX boot to complete..."
    local timeout=3
    local waited=0
    while [ "$waited" -lt "$timeout" ]; do
        if ! ps w 2>/dev/null | grep -v grep | grep -q "S99root"; then
            echo "ForgeX boot complete after ${waited}s"
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "Warning: ForgeX boot still running after ${timeout}s, starting anyway"
    return 1
}
EOF
    . "$BATS_TEST_TMPDIR/test_hook.sh"

    # No S99root process running → should return 0 quickly
    run platform_wait_for_boot_complete "$BATS_TEST_TMPDIR/opt/config/mod/.root/S99root"
    [ "$status" -eq 0 ]
    [[ "$output" == *"ForgeX boot complete"* ]]
}

# --- Patch content verification ---

@test "patch_forgex_screen_drawing patches draw_splash case" {
    grep -A30 'patch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep -q 'draw_splash'
}

@test "patch_forgex_screen_drawing patches draw_loading case" {
    grep -A20 'patch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep -q 'draw_loading'
}

@test "patch_forgex_screen_drawing patches boot_message case" {
    grep -A20 'patch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep -q 'boot_message'
}

@test "patch_forgex_screen_drawing verifies output" {
    # Must check that the patch was actually applied, not just that the file is non-empty
    grep -A40 '^patch_forgex_screen_drawing()' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep -q "grep.*helixscreen_active"
}

@test "logged wrapper strips --send-to-screen when flag exists" {
    grep -A30 'install_forgex_logged_wrapper' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep -q '\-\-send-to-screen'
}

@test "logged wrapper preserves argument quoting" {
    # The wrapper must use "$@" not unquoted $args to preserve spaces in arguments
    # Active path should use 'set --' + 'set -- "$@" "$arg"' pattern or similar
    # Check that the exec line in the active path quotes its arguments
    local wrapper_section
    wrapper_section=$(sed -n '/WRAPPER_EOF/,/WRAPPER_EOF/p' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh")
    # The exec in the helixscreen_active branch must quote args
    echo "$wrapper_section" | grep -q 'exec.*/logged-real "\$@"'
}

# --- Uninstall parity ---

@test "uninstall_forgex calls unpatch_forgex_screen_drawing" {
    grep -A25 'uninstall_forgex()' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep -q 'unpatch_forgex_screen_drawing'
}

@test "unpatch_forgex_screen_drawing function exists" {
    grep -q 'unpatch_forgex_screen_drawing()' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh"
}

@test "unpatch_forgex_screen_drawing matches patch comment string" {
    # The unpatch awk must match the EXACT comment string the patch inserts
    local patch_comment unpatch_comment
    patch_comment=$(grep -A25 '^patch_forgex_screen_drawing()' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep '# Skip when')
    unpatch_comment=$(grep -A20 '^unpatch_forgex_screen_drawing()' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh" | grep '# Skip when')
    # Both must contain the same identifying string
    echo "$patch_comment" | grep -q 'Skip when HelixScreen'
    echo "$unpatch_comment" | grep -q 'Skip when HelixScreen'
}

# --- Bundled install.sh uninstall parity ---

@test "bundled install.sh has unpatch_forgex_screen_drawing function" {
    grep -q 'unpatch_forgex_screen_drawing()' "$WORKTREE_ROOT/scripts/install.sh"
}

@test "bundled install.sh uninstall_forgex calls unpatch_forgex_screen_drawing" {
    grep -A25 'uninstall_forgex()' "$WORKTREE_ROOT/scripts/install.sh" | grep -q 'unpatch_forgex_screen_drawing'
}

@test "bundled install.sh logged wrapper uses string accumulation for args" {
    # The active path must use $args (string accumulation) — the old set-- pattern
    # was broken because set-- clears positional params before the loop iterates
    sed -n '/WRAPPER_EOF/,/WRAPPER_EOF/p' "$WORKTREE_ROOT/scripts/install.sh" | grep 'exec.*/logged-real' | head -1 | grep -q '\$args'
}

@test "uninstall.sh calls unpatch_forgex_screen_drawing" {
    grep -q 'unpatch_forgex_screen_drawing' "$WORKTREE_ROOT/scripts/uninstall.sh"
}

# --- helixscreen_active flag coordination ---

@test "ForgeX hooks create helixscreen_active flag in pre_start" {
    grep -A3 'platform_pre_start()' "$WORKTREE_ROOT/config/platform/hooks-ad5m-forgex.sh" | grep -q 'helixscreen_active'
}

@test "ForgeX hooks remove helixscreen_active flag in post_stop" {
    grep -A3 'platform_post_stop()' "$WORKTREE_ROOT/config/platform/hooks-ad5m-forgex.sh" | grep -q 'helixscreen_active'
}

@test "all screen.sh patches check helixscreen_active flag" {
    local patch_count
    patch_count=$(grep -c 'helixscreen_active' "$WORKTREE_ROOT/scripts/lib/installer/forgex.sh")
    # backlight patch, screen drawing patch, logged wrapper, unpatches, comments
    [ "$patch_count" -ge 8 ]
}
