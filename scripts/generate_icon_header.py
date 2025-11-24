#!/usr/bin/env python3

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Generate C header file with embedded icon data from PNG.
Usage: python3 generate_icon_header.py <input.png> <output.h>
"""

import subprocess
import sys

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input.png> <output.h>", file=sys.stderr)
    sys.exit(1)

input_png = sys.argv[1]
output_h = sys.argv[2]

# Convert PNG to raw RGBA using ImageMagick
result = subprocess.run(
    ["magick", input_png, "-depth", "8", "RGBA:-"],
    capture_output=True
)

if result.returncode != 0:
    print(f"Error converting PNG: {result.stderr.decode()}", file=sys.stderr)
    sys.exit(1)

rgba_data = result.stdout
pixel_count = len(rgba_data) // 4

if pixel_count != 16384:
    print(f"Error: Expected 16384 pixels (128x128), got {pixel_count}", file=sys.stderr)
    sys.exit(1)

# Write header file
with open(output_h, 'w') as f:
    f.write(f"/*\n")
    f.write(f" * Generated from {input_png}\n")
    f.write(f" * DO NOT EDIT - regenerate with: make icon\n")
    f.write(f" */\n\n")
    f.write(f"#ifndef HELIX_ICON_DATA_H\n")
    f.write(f"#define HELIX_ICON_DATA_H\n\n")
    f.write(f"#include <cstdint>\n\n")
    f.write(f"// 128x128 pixels, ARGB8888 format ({pixel_count} pixels, {len(rgba_data)} bytes)\n")
    f.write(f"static const uint32_t helix_icon_128x128[16384] = {{\n")

    # Convert RGBA to ARGB8888 (SDL format) - 8 pixels per line
    for i in range(0, len(rgba_data), 32):
        line_pixels = []
        for j in range(i, min(i + 32, len(rgba_data)), 4):
            r, g, b, a = rgba_data[j:j+4]
            # ARGB8888: (A << 24) | (R << 16) | (G << 8) | B
            argb = (a << 24) | (r << 16) | (g << 8) | b
            line_pixels.append(f"0x{argb:08x}")

        comma = "," if i + 32 < len(rgba_data) else ""
        f.write(f"    {', '.join(line_pixels)}{comma}\n")

    f.write(f"}};\n\n")
    f.write(f"#endif  // HELIX_ICON_DATA_H\n")

print(f"Generated {output_h} with {pixel_count} pixels")
