#!/bin/bash
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pre-render PLACEHOLDER THUMBNAILS to LVGL binary format
#
# Converts placeholder images used when files have no thumbnails to
# pre-rendered .bin files for instant display on embedded devices.
#
# Output:
#   build/assets/images/prerendered/thumbnail-placeholder-160.bin
#   build/assets/images/prerendered/benchy_thumbnail_white.bin
#
# Usage:
#   ./scripts/regen_placeholder_images.sh              # Generate all
#   ./scripts/regen_placeholder_images.sh --clean      # Remove generated
#   ./scripts/regen_placeholder_images.sh --list       # List targets

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source shared library
source "$SCRIPT_DIR/lib/lvgl_image_lib.sh"

# Configuration
OUTPUT_DIR="${OUTPUT_DIR:-$LVGL_PROJECT_DIR/build/assets/images/prerendered}"

# Colors from library
RED="$LVGL_RED"
GREEN="$LVGL_GREEN"
YELLOW="$LVGL_YELLOW"
CYAN="$LVGL_CYAN"
NC="$LVGL_NC"

# Placeholder images to pre-render
# Format: "source_path:output_name:size"
# Note: size of 0 means "keep original size" (no resize)
PLACEHOLDER_IMAGES=(
    "assets/images/thumbnail-placeholder-160.png:thumbnail-placeholder-160:160"
    "assets/images/benchy_thumbnail_white.png:benchy_thumbnail_white:0"
)

print_header() {
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║      HelixScreen Placeholder Thumbnail Pre-Rendering       ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
}

ensure_output_dir() {
    mkdir -p "$OUTPUT_DIR"
}

clean_placeholders() {
    echo -e "${YELLOW}Cleaning pre-rendered placeholder images...${NC}"
    for spec in "${PLACEHOLDER_IMAGES[@]}"; do
        IFS=':' read -r _ output_name _ <<< "$spec"
        rm -f "$OUTPUT_DIR/${output_name}.bin" 2>/dev/null || true
    done
    echo -e "${GREEN}✓ Cleaned placeholder images${NC}"
}

list_targets() {
    echo -e "${CYAN}Placeholder pre-render targets:${NC}"
    echo ""
    for spec in "${PLACEHOLDER_IMAGES[@]}"; do
        IFS=':' read -r source_path output_name size <<< "$spec"
        if [ "$size" = "0" ]; then
            echo -e "  ${GREEN}$output_name${NC} (original size)"
        else
            echo -e "  ${GREEN}$output_name${NC} (${size}x${size})"
        fi
        echo "    Source: $source_path"
        echo "    Output: $OUTPUT_DIR/${output_name}.bin"
        echo ""
    done
}

render_placeholder() {
    local source_path="$1"
    local output_name="$2"
    local size="$3"

    local full_source="$LVGL_PROJECT_DIR/$source_path"
    local output_file="$OUTPUT_DIR/${output_name}.bin"

    if [ ! -f "$full_source" ]; then
        echo -e "${RED}    ✗ Source not found: $source_path${NC}"
        return 1
    fi

    if [ "$size" = "0" ]; then
        echo -ne "  $output_name (original size)... "
        # Get original size
        local orig_size
        orig_size=$($LVGL_PYTHON -c "from PIL import Image; img=Image.open('$full_source'); print(max(img.width, img.height))")
        size="$orig_size"
    else
        echo -ne "  $output_name (${size}x${size})... "
    fi

    if lvgl_render_image "$full_source" "$OUTPUT_DIR" "$output_name" "$size"; then
        local file_size
        file_size=$(lvgl_file_size "$output_file")
        echo -e "${GREEN}✓${NC} ($file_size)"
        return 0
    else
        echo -e "${RED}✗ Failed${NC}"
        return 1
    fi
}

render_all() {
    local success=0
    local failed=0

    echo -e "\n${CYAN}Rendering placeholder thumbnails...${NC}"
    echo ""

    for spec in "${PLACEHOLDER_IMAGES[@]}"; do
        IFS=':' read -r source_path output_name size <<< "$spec"
        if render_placeholder "$source_path" "$output_name" "$size"; then
            ((success++)) || true
        else
            ((failed++)) || true
        fi
    done

    echo ""
    echo -e "${CYAN}════════════════════════════════════════════════════════════${NC}"
    echo -e "  ${GREEN}✓ Generated: $success files${NC}"
    if [ $failed -gt 0 ]; then
        echo -e "  ${RED}✗ Failed: $failed files${NC}"
    fi
    echo -e "${CYAN}════════════════════════════════════════════════════════════${NC}"

    if [ $failed -gt 0 ]; then
        return 1
    fi
    return 0
}

# Main
case "${1:-}" in
    --clean|-c)
        clean_placeholders
        ;;
    --list|-l)
        print_header
        list_targets
        ;;
    --help|-h)
        print_header
        echo ""
        echo "Usage: $0 [OPTIONS]"
        echo ""
        echo "Options:"
        echo "  (none)     Generate pre-rendered placeholder images"
        echo "  --clean    Remove generated .bin files"
        echo "  --list     List what would be generated"
        echo "  --help     Show this help message"
        echo ""
        echo "Environment Variables:"
        echo "  OUTPUT_DIR  Output directory (default: build/assets/images/prerendered)"
        ;;
    *)
        print_header
        lvgl_check_deps
        ensure_output_dir
        render_all
        ;;
esac
