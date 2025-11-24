#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

BINARY="./build/bin/helix-ui-proto"

echo "Testing print select view toggle..."
echo ""
echo "1. Initial card view (should show list icon)..."
timeout 3 $BINARY -p print-select > /dev/null 2>&1 || true
./scripts/screenshot.sh helix-ui-proto test-card-view print-select

echo ""
echo "2. Manually verify:"
echo "   - Card view shows list icon (≡) in top right"
echo "   - List view shows grid icon (⊞) in top right"
echo ""
echo "To manually test toggle:"
echo "   ./build/bin/helix-ui-proto -p print-select"
echo "   Click the icon button in top right to toggle views"
echo ""
echo "Screenshots saved to:"
echo "   /tmp/ui-screenshot-test-card-view.png"
