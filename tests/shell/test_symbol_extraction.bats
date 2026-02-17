#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for symbol extraction build targets
# Verifies Makefile symbols/strip targets and that LDFLAGS no longer strips at link time.

@test "symbols target is defined in Makefile" {
    run make -n symbols 2>&1
    # Should not say "No rule to make target"
    [[ "$output" != *"No rule to make target"* ]]
}

@test "strip target is defined in Makefile" {
    run make -n strip 2>&1
    [[ "$output" != *"No rule to make target"* ]]
}

@test "symbols and strip are in .PHONY" {
    local phony_line
    phony_line=$(make -n -p 2>/dev/null | grep '^\.PHONY:' | head -1)
    [[ "$phony_line" == *"symbols"* ]]
    [[ "$phony_line" == *"strip"* ]]
}

@test "LDFLAGS does not contain -s for cross-compile builds" {
    # Verify the old LDFLAGS += -s pattern is gone
    # Check the actual Makefile source, not make output (which varies by host)
    ! grep -q 'LDFLAGS += -s' Makefile
}

@test "STRIP_CMD is defined when STRIP_BINARY=yes" {
    local makevars
    makevars=$(make -n -p STRIP_BINARY=yes CROSS_COMPILE=fake- 2>/dev/null || true)
    echo "$makevars" | grep -q 'STRIP_CMD'
}

@test "NM_CMD is defined when STRIP_BINARY=yes" {
    local makevars
    makevars=$(make -n -p STRIP_BINARY=yes CROSS_COMPILE=fake- 2>/dev/null || true)
    echo "$makevars" | grep -q 'NM_CMD'
}

@test "symbols target uses nm -nC" {
    # The Makefile uses $(NM_CMD) -nC — check for the -nC flags
    grep -A5 '^symbols:' Makefile | grep -q '\-nC'
}

@test "strip target uses strip command" {
    grep -A2 '^strip:' Makefile | grep -q 'STRIP_CMD'
}

@test "symbol map output goes to TARGET.sym" {
    grep -A2 '^symbols:' Makefile | grep -q '$(TARGET).sym'
}

@test "release.yml build matrix includes all platforms" {
    local yml=".github/workflows/release.yml"
    [ -f "$yml" ]

    # Verify the build matrix includes all expected platforms
    for platform in pi pi32 ad5m k1 k2; do
        grep -q "platform: ${platform}" "$yml"
    done
}

@test "release.yml uploads symbol artifacts via matrix" {
    local yml=".github/workflows/release.yml"

    # Symbol upload uses matrix.platform variable, not literal platform names
    grep -q 'name: symbols-\${{ matrix.platform }}' "$yml"
    grep -q 'helix-screen.sym' "$yml"
}

@test "release.yml uploads symbol maps to R2" {
    local yml=".github/workflows/release.yml"
    grep -q 'symbols/v.*\.sym' "$yml"
}
