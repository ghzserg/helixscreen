#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

echo "Testing keypad responsiveness at all screen sizes..."
echo

for size in tiny small medium large; do
    echo "========================================="
    echo "Screen size: $size"
    echo "========================================="

    # Run UI and auto-screenshot
    timeout 3 ./build/bin/helix-ui-proto -s $size -k 2>&1 | grep -E "init_lvgl|Auto-opening|Screenshot" || true

    # Small delay between tests
    sleep 0.5
done

echo
echo "Keypad responsiveness test complete!"
echo "Check /tmp/ui-screenshot-*.png for results"
