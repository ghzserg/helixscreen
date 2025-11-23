#!/bin/bash
# HelixScreen Documentation Screenshot Generator
#
# This script automates the generation of all documentation screenshots.
# Run this after making UI changes to update documentation images.
#
# Usage: ./scripts/generate-screenshots.sh
#
# Output: docs/images/screenshot-*.png

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}ℹ${NC} $1"; }
success() { echo -e "${GREEN}✓${NC} $1"; }

# Detect script directory and change to project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Ensure output directory exists
mkdir -p docs/images

info "Generating documentation screenshots..."
info "This will take ~30 seconds (6 screenshots × 5 seconds each)"
echo

# Define all screenshots to capture
# Format: output_name panel_name additional_flags
SCREENSHOTS=(
    "home-panel:home:--test"
    "print-select-card:print-select:--test"
    "controls-panel:controls:--test"
    "motion-panel:motion:--test"
    "gcode-viewer:gcode-test:--test"
    "wizard-wifi::--wizard --test"
)

# Generate each screenshot
for screenshot in "${SCREENSHOTS[@]}"; do
    IFS=':' read -r output_name panel_name flags <<< "$screenshot"

    info "Capturing: $output_name"

    # Build command
    if [ -z "$panel_name" ]; then
        # No panel name (e.g., wizard)
        ./scripts/screenshot.sh helix-ui-proto "$output_name" $flags
    else
        # With panel name
        ./scripts/screenshot.sh helix-ui-proto "$output_name" "$panel_name" $flags
    fi

    # Move to docs directory
    if [ -f "/tmp/ui-screenshot-${output_name}.png" ]; then
        mv "/tmp/ui-screenshot-${output_name}.png" "docs/images/screenshot-${output_name}.png"
        success "Saved: docs/images/screenshot-${output_name}.png"
    else
        echo "⚠️  Failed to generate: $output_name"
    fi

    echo
done

echo
success "All screenshots generated!"
info "Output directory: docs/images/"
echo
ls -lh docs/images/screenshot-*.png
