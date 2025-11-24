#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

SVG_SOURCE="/Users/pbrown/Code/Printing/helixscreen/assets/material_svg"
OUTPUT_DIR="assets/images/material"

echo "Converting Material Design SVGs to LVGL 9 C arrays..."
echo "Source: $SVG_SOURCE"
echo "Output: $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"

count=0
for svg_file in "$SVG_SOURCE"/*.svg; do
    filename=$(basename "$svg_file" .svg)
    output_file="$OUTPUT_DIR/${filename}.c"

    echo "Converting: $filename.svg → ${filename}.c"

    npx lv_img_conv \
        --color-format ARGB8888 \
        --output-format c_array \
        "$svg_file" \
        -o "$output_file"

    ((count++))
done

echo ""
echo "Conversion complete! Converted $count SVG files to LVGL 9 C arrays."
