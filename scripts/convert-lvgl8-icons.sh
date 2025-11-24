#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

ICON_DIR="assets/images/material"

echo "Converting Material icons from LVGL 8 to LVGL 9 format..."

for file in "$ICON_DIR"/*.c; do
    echo "Converting: $file"

    # Replace LVGL 8 structure name with LVGL 9 name
    sed -i '' 's/lv_img_dsc_t/lv_image_dsc_t/g' "$file"

    # Replace LV_IMG_CF_TRUE_COLOR_ALPHA with LV_COLOR_FORMAT_ARGB8888
    sed -i '' 's/LV_IMG_CF_TRUE_COLOR_ALPHA/LV_COLOR_FORMAT_ARGB8888/g' "$file"

    # Replace data_size calculation: remove LV_IMG_PX_SIZE_ALPHA_BYTE multiplication
    sed -i '' 's/\.data_size = \([0-9]*\) \* LV_IMG_PX_SIZE_ALPHA_BYTE,/.data_size = \1,/g' "$file"

    # Remove .header.always_zero and .header.reserved lines (LVGL 9 doesn't have these)
    sed -i '' '/\.header\.always_zero/d' "$file"
    sed -i '' '/\.header\.reserved = 0,/d' "$file"

done

echo "Conversion complete! Converted $(ls "$ICON_DIR"/*.c | wc -l) files."
