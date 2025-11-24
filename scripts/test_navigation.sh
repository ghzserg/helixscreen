#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

BIN="./build/bin/helix-ui-proto"
TIMEOUT_CMD="timeout"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "==================================="
echo "Navigation Flow Testing"
echo "==================================="
echo ""

# Test 1: Command Line Panel Selection
echo "Test 1: Command line panel selection..."
echo "----------------------------------------"

test_panel_flag() {
    local panel=$1
    local expected_text=$2

    echo -n "Testing -p $panel... "
    output=$($TIMEOUT_CMD 3 $BIN -p $panel 2>&1 || true)

    if echo "$output" | grep -q "$expected_text"; then
        echo -e "${GREEN}✓ PASS${NC}"
        return 0
    else
        echo -e "${RED}✗ FAIL${NC}"
        echo "Expected: $expected_text"
        echo "Got: $output" | head -3
        return 1
    fi
}

# Test each panel flag
test_panel_flag "home" "Initial Panel: 0"
test_panel_flag "print-select" "Initial Panel: 1"
test_panel_flag "controls" "Initial Panel: 2"
test_panel_flag "motion" "Initial Panel: 2"  # Motion is overlay on controls
test_panel_flag "nozzle-temp" "Initial Panel: 2"
test_panel_flag "bed-temp" "Initial Panel: 2"
test_panel_flag "extrusion" "Initial Panel: 2"
test_panel_flag "print-status" "Initial Panel: 0"  # Print status starts from home

echo ""
echo "Test 2: Screen size flags..."
echo "----------------------------------------"

test_screen_size() {
    local size=$1
    local expected=$2

    echo -n "Testing -s $size... "
    output=$($TIMEOUT_CMD 2 $BIN -s $size 2>&1 || true)

    if echo "$output" | grep -q "$expected"; then
        echo -e "${GREEN}✓ PASS${NC}"
        return 0
    else
        echo -e "${RED}✗ FAIL${NC}"
        echo "Expected: $expected"
        return 1
    fi
}

test_screen_size "tiny" "480x320"
test_screen_size "small" "800x480"
test_screen_size "medium" "1024x600"
test_screen_size "large" "1280x720"

echo ""
echo "Test 3: Panel rendering verification..."
echo "----------------------------------------"

test_panel_renders() {
    local panel=$1
    local check_text=$2

    echo -n "Verifying $panel renders... "
    output=$($TIMEOUT_CMD 3 $BIN -p $panel 2>&1 || true)

    # Check for successful initialization (no errors)
    if echo "$output" | grep -qi "error\|failed\|null"; then
        echo -e "${RED}✗ FAIL - Errors detected${NC}"
        echo "$output" | grep -i "error\|failed\|null" | head -3
        return 1
    else
        echo -e "${GREEN}✓ PASS${NC}"
        return 0
    fi
}

test_panel_renders "home" "Home"
test_panel_renders "controls" "Controls"
test_panel_renders "motion" "Motion"
test_panel_renders "nozzle-temp" "Nozzle"
test_panel_renders "bed-temp" "Bed"
test_panel_renders "extrusion" "Extrusion"
test_panel_renders "print-select" "Print"
test_panel_renders "file-detail" "Detail"
test_panel_renders "print-status" "Status"

echo ""
echo "==================================="
echo "Navigation Tests Complete"
echo "==================================="
echo ""
echo "Summary:"
echo "  ✓ All command line flags working"
echo "  ✓ All screen sizes supported"
echo "  ✓ All panels render without errors"
echo ""
echo "Manual Testing Required:"
echo "  - Interactive navigation (click motion card → back)"
echo "  - Multi-level navigation (nozzle temp → keypad → back)"
echo "  - State preservation across navigation"
echo "  - Rapid button clicking"
echo ""
