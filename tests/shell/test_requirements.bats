#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for check_requirements(), check_disk_space(), detect_init_system(),
# and install_runtime_deps() in scripts/lib/installer/requirements.sh

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Override the no-op log stubs so we can assert on output
    log_error()   { echo "ERROR: $*"; }
    log_warn()    { echo "WARN: $*"; }
    log_info()    { echo "INFO: $*"; }
    log_success() { echo "OK: $*"; }
    export -f log_error log_warn log_info log_success

    # Reset source guard so we can re-source per test
    unset _HELIX_REQUIREMENTS_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/requirements.sh"

    export INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    export SUDO=""
    export INIT_SYSTEM=""
}

# ---------------------------------------------------------------------------
# Helper: create a restricted PATH with only the named commands available.
# This is the cleanest way to hide real commands from check_requirements(),
# which relies on `command -v`.
# ---------------------------------------------------------------------------
make_restricted_path() {
    local bin="$BATS_TEST_TMPDIR/restricted_bin"
    mkdir -p "$bin"
    for cmd in "$@"; do
        local real
        real="$(command -v "$cmd" 2>/dev/null)" || continue
        ln -sf "$real" "$bin/$cmd"
    done
    echo "$bin"
}

# Helper: run check_requirements in a subshell with a restricted PATH.
# log_* functions are redefined to produce visible output since helpers.bash
# stubs them as no-ops.
run_check_requirements_with_path() {
    local restricted_path="$1"
    run bash -c "
        log_error()   { echo \"ERROR: \$*\"; }
        log_info()    { echo \"INFO: \$*\"; }
        log_success() { echo \"OK: \$*\"; }
        export -f log_error log_info log_success
        export PATH='$restricted_path'
        unset _HELIX_REQUIREMENTS_SOURCED
        . '$WORKTREE_ROOT/scripts/lib/installer/requirements.sh'
        check_requirements
    "
}

# ===========================================================================
# check_requirements
# ===========================================================================

@test "check_requirements: succeeds when all tools present" {
    run check_requirements
    [ "$status" -eq 0 ]
    [[ "$output" == *"All required commands available"* ]]
}

@test "check_requirements: fails when curl AND wget both missing" {
    local rbin
    rbin=$(make_restricted_path tar gunzip)

    run_check_requirements_with_path "$rbin"
    [ "$status" -ne 0 ]
    [[ "$output" == *"curl or wget"* ]]
}

@test "check_requirements: fails when tar missing" {
    local rbin
    rbin=$(make_restricted_path curl gunzip)

    run_check_requirements_with_path "$rbin"
    [ "$status" -ne 0 ]
    [[ "$output" == *"tar"* ]]
}

@test "check_requirements: fails when gunzip missing" {
    local rbin
    rbin=$(make_restricted_path curl tar)

    run_check_requirements_with_path "$rbin"
    [ "$status" -ne 0 ]
    [[ "$output" == *"gunzip"* ]]
}

@test "check_requirements: succeeds with curl but no wget" {
    local rbin
    rbin=$(make_restricted_path curl tar gunzip)

    run_check_requirements_with_path "$rbin"
    [ "$status" -eq 0 ]
}

@test "check_requirements: succeeds with wget but no curl" {
    local rbin
    rbin=$(make_restricted_path tar gunzip)
    # wget may not exist on macOS; create a stub
    printf '#!/bin/sh\nexit 0\n' > "$rbin/wget"
    chmod +x "$rbin/wget"

    run_check_requirements_with_path "$rbin"
    [ "$status" -eq 0 ]
}

@test "check_requirements: error lists all missing tools" {
    local rbin
    rbin=$(make_restricted_path)  # nothing available

    run_check_requirements_with_path "$rbin"
    [ "$status" -ne 0 ]
    [[ "$output" == *"curl or wget"* ]]
    [[ "$output" == *"tar"* ]]
    [[ "$output" == *"gunzip"* ]]
}

@test "check_requirements: all tools missing exits non-zero" {
    local rbin
    rbin=$(make_restricted_path)

    run_check_requirements_with_path "$rbin"
    [ "$status" -ne 0 ]
}

# ===========================================================================
# check_disk_space
# ===========================================================================

@test "check_disk_space: succeeds with adequate space (GNU df)" {
    mkdir -p "$BATS_TEST_TMPDIR/opt"

    mock_command_script "df" '
echo "Filesystem     1M-blocks  Used Available Use% Mounted on"
echo "/dev/sda1          1000   900       100  90% /"
'

    run check_disk_space "pi"
    [ "$status" -eq 0 ]
    [[ "$output" == *"100MB available"* ]]
}

@test "check_disk_space: exits when space insufficient (GNU df)" {
    mkdir -p "$BATS_TEST_TMPDIR/opt"

    mock_command_script "df" '
echo "Filesystem     1M-blocks  Used Available Use% Mounted on"
echo "/dev/sda1          1000   990        10  99% /"
'

    run check_disk_space "pi"
    [ "$status" -ne 0 ]
    [[ "$output" == *"Insufficient disk space"* ]]
}

@test "check_disk_space: walks up to parent when INSTALL_DIR does not exist" {
    # INSTALL_DIR doesn't exist, but its parent does
    mkdir -p "$BATS_TEST_TMPDIR/opt"

    mock_command_script "df" '
echo "Filesystem     1M-blocks  Used Available Use% Mounted on"
echo "/dev/sda1          1000   800       200  80% /"
'

    run check_disk_space "pi"
    [ "$status" -eq 0 ]
}

@test "check_disk_space: walks up deeply nested non-existent path" {
    # No intermediate directories created
    export INSTALL_DIR="$BATS_TEST_TMPDIR/a/b/c/d/helixscreen"

    mock_command_script "df" '
echo "Filesystem     1M-blocks  Used Available Use% Mounted on"
echo "/dev/sda1          1000   800       200  80% /"
'

    run check_disk_space "pi"
    [ "$status" -eq 0 ]
}

@test "check_disk_space: graceful when df fails entirely" {
    mkdir -p "$BATS_TEST_TMPDIR/opt"

    # df produces no output and exits non-zero
    mock_command_script "df" 'exit 1'

    # available_mb will be empty, so the integer comparison is skipped
    run check_disk_space "pi"
    [ "$status" -eq 0 ]
}

@test "check_disk_space: BusyBox df format with ad5m platform" {
    mkdir -p "$BATS_TEST_TMPDIR/opt"

    # BusyBox df: KB blocks. 102400 KB = 100MB available
    mock_command_script "df" '
echo "Filesystem           1K-blocks      Used Available Use% Mounted on"
echo "/dev/mmcblk0p1         1048576    945152    102400  90% /"
'

    run check_disk_space "ad5m"
    [ "$status" -eq 0 ]
    [[ "$output" == *"100MB available"* ]]
}

@test "check_disk_space: BusyBox df with insufficient space" {
    mkdir -p "$BATS_TEST_TMPDIR/opt"

    # BusyBox df: 10240 KB = 10MB available (below 50MB minimum)
    mock_command_script "df" '
echo "Filesystem           1K-blocks      Used Available Use% Mounted on"
echo "/dev/mmcblk0p1         1048576   1038336     10240  99% /"
'

    run check_disk_space "ad5m"
    [ "$status" -ne 0 ]
    [[ "$output" == *"Insufficient disk space"* ]]
}

@test "check_disk_space: uses default /opt/helixscreen when INSTALL_DIR unset" {
    unset INSTALL_DIR

    mock_command_script "df" '
echo "Filesystem     1M-blocks  Used Available Use% Mounted on"
echo "/dev/sda1          1000   800       200  80% /"
'

    run check_disk_space "pi"
    [ "$status" -eq 0 ]
}

# ===========================================================================
# detect_init_system
# ===========================================================================

@test "detect_init_system: detects systemd when both indicators present" {
    mock_command "systemctl" ""
    if [ ! -d /run/systemd/system ]; then
        skip "Requires /run/systemd/system (Linux with systemd)"
    fi

    detect_init_system
    [ "$INIT_SYSTEM" = "systemd" ]
}

@test "detect_init_system: systemctl without /run/systemd/system falls through" {
    mock_command "systemctl" ""
    # On macOS, /run/systemd/system does not exist
    if [ -d /run/systemd/system ]; then
        skip "Test requires /run/systemd/system to NOT exist"
    fi

    if [ -d /etc/init.d ]; then
        detect_init_system
        [ "$INIT_SYSTEM" = "sysv" ]
    else
        # Neither systemd dir nor init.d -- expect failure
        run detect_init_system
        [ "$status" -ne 0 ]
    fi
}

@test "detect_init_system: no systemctl, /etc/init.d exists -> sysv" {
    mock_command_fail "systemctl"
    if [ ! -d /etc/init.d ]; then
        skip "/etc/init.d not present on this system"
    fi
    # detect_init_system checks 'command -v systemctl' (which finds the mock)
    # AND '[ -d /run/systemd/system ]'. If the latter exists, it picks systemd
    # regardless of mock. Skip on systems with real systemd runtime dir.
    if [ -d /run/systemd/system ]; then
        skip "Cannot hide /run/systemd/system on this system (systemd runner)"
    fi

    detect_init_system
    [ "$INIT_SYSTEM" = "sysv" ]
}

@test "detect_init_system: neither systemctl nor /etc/init.d -> error" {
    mock_command_fail "systemctl"
    if [ -d /etc/init.d ]; then
        skip "/etc/init.d exists on this system"
    fi

    run detect_init_system
    [ "$status" -ne 0 ]
    [[ "$output" == *"Could not detect init system"* ]]
}

@test "detect_init_system: systemd wins over sysv when both present" {
    mock_command "systemctl" ""
    if [ ! -d /run/systemd/system ]; then
        skip "Requires /run/systemd/system (Linux with systemd)"
    fi
    # /etc/init.d may also exist, but systemd should win

    detect_init_system
    [ "$INIT_SYSTEM" = "systemd" ]
}

# ===========================================================================
# install_runtime_deps
# ===========================================================================

@test "install_runtime_deps: returns immediately for ad5m platform" {
    run install_runtime_deps "ad5m"
    [ "$status" -eq 0 ]
    # Should not attempt to check or install packages
    [[ "$output" != *"Checking runtime dependencies"* ]]
}

@test "install_runtime_deps: returns immediately for k1 platform" {
    run install_runtime_deps "k1"
    [ "$status" -eq 0 ]
    [[ "$output" != *"Checking runtime dependencies"* ]]
}

@test "install_runtime_deps: checks deps for pi platform" {
    # Mock dpkg-query to report all packages as installed
    mock_command_script "dpkg-query" 'echo "install ok installed"'

    run install_runtime_deps "pi"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Checking runtime dependencies"* ]]
    [[ "$output" == *"already installed"* ]]
}

# ===========================================================================
# verify_binary_deps
# ===========================================================================

# Helper: set up a fake binary and mock ldd for verify_binary_deps tests
setup_verify_binary() {
    mkdir -p "$INSTALL_DIR/bin"
    printf '#!/bin/sh\nexit 0\n' > "$INSTALL_DIR/bin/helix-screen"
    chmod +x "$INSTALL_DIR/bin/helix-screen"
}

@test "verify_binary_deps: succeeds when all libs found" {
    setup_verify_binary
    mock_command_script "ldd" '
echo "	linux-vdso.so.1 (0x7fff12345000)"
echo "	libdrm.so.2 => /usr/lib/aarch64-linux-gnu/libdrm.so.2 (0x7f1234000000)"
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x7f1230000000)"
'

    run verify_binary_deps "pi"
    [ "$status" -eq 0 ]
    [[ "$output" == *"All shared library dependencies satisfied"* ]]
}

@test "verify_binary_deps: skips when ldd not available" {
    setup_verify_binary
    mock_command_fail "ldd"

    run verify_binary_deps "pi"
    [ "$status" -eq 0 ]
}

@test "verify_binary_deps: skips when binary not found" {
    # Provide ldd but don't create the binary
    mock_command "ldd" ""

    run verify_binary_deps "pi"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Binary not found"* ]]
}

@test "verify_binary_deps: detects missing libssl.so.1.1 and installs compat" {
    setup_verify_binary

    # First ldd call: libssl missing. Second call (after install): all good.
    local call_count_file="$BATS_TEST_TMPDIR/ldd_calls"
    echo "0" > "$call_count_file"

    mock_command_script "ldd" "
count=\$(cat '$call_count_file')
count=\$((count + 1))
echo \$count > '$call_count_file'
if [ \$count -eq 1 ]; then
    echo '	libssl.so.1.1 => not found'
    echo '	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x7f1230000000)'
else
    echo '	libssl.so.1.1 => /usr/lib/aarch64-linux-gnu/libssl.so.1.1 (0x7f1234000000)'
    echo '	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x7f1230000000)'
fi
"

    # apt-cache says libssl1.1 is available
    mock_command_script "apt-cache" 'echo "Package: libssl1.1"'
    # apt-get install succeeds
    mock_command "apt-get" ""

    run verify_binary_deps "pi"
    [ "$status" -eq 0 ]
    [[ "$output" == *"libssl.so.1.1 not found"* ]]
    [[ "$output" == *"Installing libssl1.1"* ]]
    [[ "$output" == *"resolved"* ]]
}

@test "verify_binary_deps: fails when libssl1.1 package not available" {
    setup_verify_binary
    mock_command_script "ldd" '
echo "	libssl.so.1.1 => not found"
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x7f1230000000)"
'
    # apt-cache says no such package
    mock_command_fail "apt-cache"

    run verify_binary_deps "pi"
    [ "$status" -ne 0 ]
    [[ "$output" == *"libssl1.1 package not available"* ]]
    [[ "$output" == *"OpenSSL 3"* ]]
}

@test "verify_binary_deps: fails when non-ssl library missing on pi" {
    setup_verify_binary
    mock_command_script "ldd" '
echo "	libfoo.so.42 => not found"
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x7f1230000000)"
'

    run verify_binary_deps "pi"
    [ "$status" -ne 0 ]
    [[ "$output" == *"Missing shared libraries"* ]]
    [[ "$output" == *"libfoo.so.42"* ]]
    [[ "$output" == *"Could not resolve"* ]]
}

@test "verify_binary_deps: warns but continues on non-pi platforms" {
    setup_verify_binary
    mock_command_script "ldd" '
echo "	libfoo.so.1 => not found"
'

    run verify_binary_deps "ad5m"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Missing shared libraries"* ]]
    [[ "$output" == *"may not start correctly"* ]]
}

@test "verify_binary_deps: works for pi32 platform same as pi" {
    setup_verify_binary
    mock_command_script "ldd" '
echo "	libssl.so.1.1 => not found"
'
    mock_command_fail "apt-cache"

    run verify_binary_deps "pi32"
    [ "$status" -ne 0 ]
    [[ "$output" == *"libssl1.1 package not available"* ]]
}

@test "verify_binary_deps: multiple missing libs all reported" {
    setup_verify_binary
    mock_command_script "ldd" '
echo "	libssl.so.1.1 => not found"
echo "	libcrypto.so.1.1 => not found"
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x7f1230000000)"
'

    # apt-cache says libssl1.1 is available, but libcrypto stays missing
    mock_command_script "apt-cache" 'echo "Package: libssl1.1"'
    mock_command "apt-get" ""
    # After "install", ldd still shows libcrypto missing
    # (We can't easily change mock mid-test, so the re-check will still show both)

    run verify_binary_deps "pi"
    [[ "$output" == *"Missing shared libraries"* ]]
    [[ "$output" == *"libssl.so.1.1"* ]]
    [[ "$output" == *"libcrypto.so.1.1"* ]]
}
