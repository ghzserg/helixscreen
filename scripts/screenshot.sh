#!/bin/bash
# UI Prototype Screenshot Helper
# Builds, runs, captures, and compresses screenshot in one command
# Usage: ./screenshot.sh [binary_name] [output_name]
#   binary_name: Name of binary to run (default: guppy-ui-proto)
#   output_name: Name for output file (default: timestamp)

set -e

# Get binary name (first arg) or default
BINARY="${1:-guppy-ui-proto}"
BINARY_PATH="./build/bin/${BINARY}"

# Get unique name or use timestamp
NAME="${2:-$(date +%s)}"
BMP_FILE="/tmp/ui-screenshot-${NAME}.bmp"
PNG_FILE="/tmp/ui-screenshot-${NAME}.png"

# Build
echo "Building prototype..."
make -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "(Compiling|Linking|Build complete|error:)" || true

# Clean old screenshots
rm -f /tmp/ui-screenshot.bmp

# Run and auto-capture (app saves with timestamp)
echo "Running ${BINARY} (3 second timeout)..."
TIMESTAMP=$(date +%s)
gtimeout 3 "${BINARY_PATH}" 2>&1 | grep -E "(LVGL initialized|Screenshot saved|Switched to panel|Error|error)" || true

# Find the most recent screenshot (app now saves with timestamps)
LATEST_BMP=$(ls -t /tmp/ui-screenshot-*.bmp 2>/dev/null | head -1)
if [ -n "$LATEST_BMP" ]; then
    # Rename to requested name
    if [ "$LATEST_BMP" != "$BMP_FILE" ]; then
        mv "$LATEST_BMP" "$BMP_FILE"
    fi
else
    echo "ERROR: Screenshot not captured"
    exit 1
fi

# Convert to PNG
echo "Converting to PNG..."
magick "$BMP_FILE" "$PNG_FILE"

# Show result
SIZE=$(ls -lh "$PNG_FILE" | awk '{print $5}')
echo "Screenshot ready: $PNG_FILE ($SIZE)"
echo "Path: $PNG_FILE"
