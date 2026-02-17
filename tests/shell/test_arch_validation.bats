#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for validate_binary_architecture() in scripts/lib/installer/release.sh

RELEASE_SH="scripts/lib/installer/release.sh"

setup() {
    source tests/shell/helpers.bash
    export GITHUB_REPO="prestonbrown/helixscreen"
    source "$RELEASE_SH"
}

# --- ARM 32-bit binary tests ---

@test "validate_binary_architecture: ARM32 binary passes for ad5m" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_arm32_elf "$binary"
    run validate_binary_architecture "$binary" "ad5m"
    [ "$status" -eq 0 ]
}

@test "validate_binary_architecture: MIPS binary passes for k1" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_mips_elf "$binary"
    run validate_binary_architecture "$binary" "k1"
    [ "$status" -eq 0 ]
}

@test "validate_binary_architecture: ARM32 binary passes for pi32" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_arm32_elf "$binary"
    run validate_binary_architecture "$binary" "pi32"
    [ "$status" -eq 0 ]
}

# --- AARCH64 binary tests ---

@test "validate_binary_architecture: AARCH64 binary passes for pi" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_aarch64_elf "$binary"
    run validate_binary_architecture "$binary" "pi"
    [ "$status" -eq 0 ]
}

# --- Cross-architecture mismatch tests ---

@test "validate_binary_architecture: AARCH64 binary FAILS for ad5m" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_aarch64_elf "$binary"
    run validate_binary_architecture "$binary" "ad5m"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: AARCH64 binary FAILS for k1" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_aarch64_elf "$binary"
    run validate_binary_architecture "$binary" "k1"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: ARM32 binary FAILS for k1" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_arm32_elf "$binary"
    run validate_binary_architecture "$binary" "k1"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: AARCH64 binary FAILS for pi32" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_aarch64_elf "$binary"
    run validate_binary_architecture "$binary" "pi32"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: ARM32 binary FAILS for pi" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_arm32_elf "$binary"
    run validate_binary_architecture "$binary" "pi"
    [ "$status" -eq 1 ]
}

# --- Error message content ---

@test "validate_binary_architecture: mismatch reports expected vs actual" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_aarch64_elf "$binary"
    # Override log_error stub to capture output
    log_error() { echo "$@"; }
    export -f log_error
    run validate_binary_architecture "$binary" "ad5m"
    [ "$status" -eq 1 ]
    [[ "${output}" == *"Architecture mismatch"* ]]
    [[ "${output}" == *"ARM 32-bit"* ]]
    [[ "${output}" == *"AARCH64"* ]]
}

# --- Edge cases ---

@test "validate_binary_architecture: missing file returns error" {
    run validate_binary_architecture "/nonexistent/binary" "ad5m"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: non-ELF file returns error" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    echo "not an ELF file at all" > "$binary"
    run validate_binary_architecture "$binary" "ad5m"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: truncated file (less than 20 bytes) is handled" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    printf '\x7fELF\x01' > "$binary"  # Only 5 bytes
    run validate_binary_architecture "$binary" "ad5m"
    # Should fail: hexdump output will have fewer fields, awk fields 19/20 empty
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: empty file returns error" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    touch "$binary"
    # Empty file produces no hexdump output, so header is empty.
    # Empty/unreadable binaries should fail validation (not silently pass).
    run validate_binary_architecture "$binary" "ad5m"
    [ "$status" -eq 1 ]
}

@test "validate_binary_architecture: unknown platform skips validation" {
    local binary="$BATS_TEST_TMPDIR/helix-screen"
    create_fake_arm32_elf "$binary"
    run validate_binary_architecture "$binary" "windows"
    [ "$status" -eq 0 ]
}
