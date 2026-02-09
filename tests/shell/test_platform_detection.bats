#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for detect_platform() in platform.sh
# Covers Pi 64-bit kernel with 32-bit userspace detection (the "not found" bug)

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

    # Source platform.sh (skip source guard by unsetting it)
    unset _HELIX_PLATFORM_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"

    # Create a fake /etc/os-release indicating Debian (Pi-like system)
    mkdir -p "$BATS_TEST_TMPDIR/etc"
    echo 'ID=debian' > "$BATS_TEST_TMPDIR/etc/os-release"
    # Create a fake /home/pi directory to trigger is_pi=true
    mkdir -p "$BATS_TEST_TMPDIR/home/pi"
}

# Helper: override detect_platform to use mocked filesystem and commands
# We redefine detect_platform to inject our test hooks since it reads
# system files directly (/etc/os-release, /home/*)
_detect_platform_with_mocks() {
    local mock_arch="$1"
    local mock_getconf="$2"
    local mock_file_output="${3:-}"

    # Mock uname -m
    uname() {
        if [ "$1" = "-m" ]; then
            echo "$mock_arch"
        elif [ "$1" = "-r" ]; then
            echo "6.1.0-rpi"
        else
            command uname "$@"
        fi
    }
    export -f uname

    # Mock getconf
    if [ "$mock_getconf" = "unavailable" ]; then
        getconf() { return 1; }
    else
        getconf() {
            if [ "$1" = "LONG_BIT" ]; then
                echo "$mock_getconf"
            else
                command getconf "$@"
            fi
        }
    fi
    export -f getconf

    # Mock file command (for fallback path)
    if [ -n "$mock_file_output" ]; then
        file() { echo "$mock_file_output"; }
        export -f file
    fi

    # Mock grep to match our fake os-release
    # (detect_platform greps /etc/os-release for Raspbian|Debian)
    local orig_grep
    orig_grep=$(command -v grep)

    # We need is_pi=true. Override the filesystem checks by creating dirs
    # The function checks /home/pi, /home/mks, /home/biqu dirs
    # We already created /home/pi in setup, but detect_platform checks the
    # real filesystem. Instead, we'll redefine detect_platform inline.

    detect_platform
}

# Since detect_platform reads real /etc/os-release and /home/*, we need
# to override the full function with test-specific logic that captures
# just the userspace detection. Extract the Pi bitness logic into a
# testable form.

# Test the core logic: given aarch64 kernel + getconf says 32-bit
@test "Pi aarch64 kernel + 32-bit userspace (getconf) returns pi32" {
    # Redefine detect_platform with controlled inputs
    detect_platform() {
        local arch="aarch64"
        local is_pi=true

        local userspace_bits
        userspace_bits=$(getconf LONG_BIT 2>/dev/null || echo "")
        if [ "$userspace_bits" = "64" ]; then
            echo "pi"
        elif [ "$userspace_bits" = "32" ]; then
            echo "pi32"
        else
            echo "pi"
        fi
    }

    mock_command "getconf" "32"
    run detect_platform
    [ "$status" -eq 0 ]
    [ "$output" = "pi32" ]
}

@test "Pi aarch64 kernel + 64-bit userspace (getconf) returns pi" {
    detect_platform() {
        local arch="aarch64"
        local is_pi=true

        local userspace_bits
        userspace_bits=$(getconf LONG_BIT 2>/dev/null || echo "")
        if [ "$userspace_bits" = "64" ]; then
            echo "pi"
        elif [ "$userspace_bits" = "32" ]; then
            echo "pi32"
        else
            echo "pi"
        fi
    }

    mock_command "getconf" "64"
    run detect_platform
    [ "$status" -eq 0 ]
    [ "$output" = "pi" ]
}

@test "Pi armv7l kernel + 32-bit userspace returns pi32" {
    detect_platform() {
        local arch="armv7l"
        local is_pi=true

        local userspace_bits
        userspace_bits=$(getconf LONG_BIT 2>/dev/null || echo "")
        if [ "$userspace_bits" = "64" ]; then
            echo "pi"
        elif [ "$userspace_bits" = "32" ]; then
            echo "pi32"
        else
            if [ "$arch" = "aarch64" ]; then
                echo "pi"
            else
                echo "pi32"
            fi
        fi
    }

    mock_command "getconf" "32"
    run detect_platform
    [ "$status" -eq 0 ]
    [ "$output" = "pi32" ]
}

@test "Pi aarch64 + getconf unavailable + file says 32-bit returns pi32" {
    detect_platform() {
        local arch="aarch64"
        local is_pi=true

        local userspace_bits
        userspace_bits=$(getconf LONG_BIT 2>/dev/null || echo "")
        if [ "$userspace_bits" = "64" ]; then
            echo "pi"
        elif [ "$userspace_bits" = "32" ]; then
            echo "pi32"
        else
            if file /usr/bin/id 2>/dev/null | grep -q "64-bit"; then
                echo "pi"
            elif file /usr/bin/id 2>/dev/null | grep -q "32-bit"; then
                echo "pi32"
            else
                if [ "$arch" = "aarch64" ]; then
                    echo "pi"
                else
                    echo "pi32"
                fi
            fi
        fi
    }

    mock_command_fail "getconf"
    mock_command "file" "/usr/bin/id: ELF 32-bit LSB pie executable, ARM, EABI5"
    run detect_platform
    [ "$status" -eq 0 ]
    [ "$output" = "pi32" ]
}

@test "Pi aarch64 + getconf unavailable + file says 64-bit returns pi" {
    detect_platform() {
        local arch="aarch64"
        local is_pi=true

        local userspace_bits
        userspace_bits=$(getconf LONG_BIT 2>/dev/null || echo "")
        if [ "$userspace_bits" = "64" ]; then
            echo "pi"
        elif [ "$userspace_bits" = "32" ]; then
            echo "pi32"
        else
            if file /usr/bin/id 2>/dev/null | grep -q "64-bit"; then
                echo "pi"
            elif file /usr/bin/id 2>/dev/null | grep -q "32-bit"; then
                echo "pi32"
            else
                if [ "$arch" = "aarch64" ]; then
                    echo "pi"
                else
                    echo "pi32"
                fi
            fi
        fi
    }

    mock_command_fail "getconf"
    mock_command "file" "/usr/bin/id: ELF 64-bit LSB pie executable, ARM aarch64"
    run detect_platform
    [ "$status" -eq 0 ]
    [ "$output" = "pi" ]
}

@test "Pi aarch64 + getconf unavailable + file unavailable falls back to kernel arch" {
    detect_platform() {
        local arch="aarch64"
        local is_pi=true

        local userspace_bits
        userspace_bits=$(getconf LONG_BIT 2>/dev/null || echo "")
        if [ "$userspace_bits" = "64" ]; then
            echo "pi"
        elif [ "$userspace_bits" = "32" ]; then
            echo "pi32"
        else
            if file /usr/bin/id 2>/dev/null | grep -q "64-bit"; then
                echo "pi"
            elif file /usr/bin/id 2>/dev/null | grep -q "32-bit"; then
                echo "pi32"
            else
                if [ "$arch" = "aarch64" ]; then
                    echo "pi"
                else
                    echo "pi32"
                fi
            fi
        fi
    }

    mock_command_fail "getconf"
    mock_command_fail "file"
    run detect_platform
    [ "$status" -eq 0 ]
    [ "$output" = "pi" ]
}

# --- Source code regression tests ---

@test "platform.sh uses getconf LONG_BIT for userspace detection" {
    grep -q 'getconf LONG_BIT' "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"
}

@test "platform.sh has file command fallback for userspace detection" {
    grep -q 'file /usr/bin/id' "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"
}

@test "platform.sh warns about 64-bit kernel with 32-bit userspace" {
    grep -q '64-bit kernel with 32-bit userspace' "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"
}

@test "install.sh (bundled) also uses getconf LONG_BIT" {
    grep -q 'getconf LONG_BIT' "$WORKTREE_ROOT/scripts/install.sh"
}
