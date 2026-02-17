#!/bin/bash
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bidirectional icon validation:
#   1. Forward:  codepoints.h → fonts (are defined icons compiled?)
#   2. Reverse:  XML icon="" → codepoints.h (are used icons defined?)
#
# This script prevents two common bugs:
#   - Icons added to codepoints.h but fonts not regenerated
#   - Icons used in XML but never added to codepoints.h
#
# Usage:
#   ./scripts/validate_icon_fonts.sh         # Check if fonts are up to date
#   ./scripts/validate_icon_fonts.sh --fix   # Regenerate fonts if needed
#
# Exit codes:
#   0 - All icons valid
#   1 - Missing icons (fonts need regeneration or codepoints need adding)
#   2 - Script error

set -e
cd "$(dirname "$0")/.."

CODEPOINTS_FILE="include/ui_icon_codepoints.h"
FONT_FILE="assets/fonts/mdi_icons_32.c"  # Check one size, all have same glyphs

# Parse command line
FIX_MODE=false
if [[ "$1" == "--fix" ]]; then
    FIX_MODE=true
fi

# Extract codepoints from ui_icon_codepoints.h
# Format: {"name", "\xF3\xB0\x80\xA6"},  // F0026 alert
# We extract the hex codepoint from the comment (e.g., F0026)
extract_required_codepoints() {
    # Match lines with icon definitions and extract the codepoint from comment
    grep -E '\{"[^"]+",\s*"\\x[^"]+"\}.*//\s*[Ff][0-9A-Fa-f]+' "$CODEPOINTS_FILE" |
    grep -oE '//\s*[Ff][0-9A-Fa-f]+' |
    sed 's|// *||' |
    tr '[:lower:]' '[:upper:]' |
    sort -u
}

# Extract codepoints from compiled font file
# Format: --range 0xF0026,0xF0029,...
extract_font_codepoints() {
    if [[ ! -f "$FONT_FILE" ]]; then
        echo "ERROR: Font file not found: $FONT_FILE" >&2
        echo "Run './scripts/regen_mdi_fonts.sh' to generate fonts" >&2
        exit 2
    fi

    head -5 "$FONT_FILE" |
    grep -oE '0x[Ff][0-9A-Fa-f]+' |
    sed 's/0x//' |
    tr '[:lower:]' '[:upper:]' |
    sort -u
}

echo "Validating icon fonts..."
echo "  Codepoints file: $CODEPOINTS_FILE"
echo "  Font file: $FONT_FILE"
echo ""

# Get codepoints from both sources
REQUIRED=$(extract_required_codepoints)
IN_FONT=$(extract_font_codepoints)

# Find missing codepoints using comm (much faster than loop + grep)
MISSING=$(comm -23 <(echo "$REQUIRED") <(echo "$IN_FONT"))
if [[ -z "$MISSING" ]]; then
    MISSING_COUNT=0
else
    MISSING_COUNT=$(echo "$MISSING" | wc -l | tr -d ' ')
fi

# Report results
REQUIRED_COUNT=$(echo "$REQUIRED" | wc -l | tr -d ' ')
IN_FONT_COUNT=$(echo "$IN_FONT" | wc -l | tr -d ' ')

echo "  Required icons: $REQUIRED_COUNT"
echo "  Icons in font: $IN_FONT_COUNT"
echo ""

if [[ $MISSING_COUNT -gt 0 ]]; then
    echo "❌ MISSING FROM FONTS ($MISSING_COUNT):"
    echo ""
    echo "$MISSING" | while IFS= read -r cp; do
        if [[ -n "$cp" ]]; then
            # Find the icon name for this codepoint
            NAME=$(grep -E "//\s*$cp" "$CODEPOINTS_FILE" | head -1 | sed -E 's/.*\{"([^"]+)".*/\1/')
            echo "   0x$cp ($NAME)"
        fi
    done
    echo ""

    if $FIX_MODE; then
        echo "Regenerating fonts..."
        ./scripts/regen_mdi_fonts.sh
        echo ""
        echo "✓ Fonts regenerated. Re-run validation to confirm."
        exit 0
    else
        echo "Run './scripts/validate_icon_fonts.sh --fix' to regenerate fonts"
        echo "Or run './scripts/regen_mdi_fonts.sh' directly"
        exit 1
    fi
else
    echo "✓ All defined icons are present in compiled fonts"
fi

# =============================================================================
# REVERSE CHECK: XML icon="" → codepoints.h
# =============================================================================
echo ""
echo "Validating XML icon references..."

XML_DIR="ui_xml"

# Extract all icon names from XML files (icon="name" attributes)
# - Exclude XML comments (lines containing <!--)
# - Exclude *_icon="..." patterns (like hide_action_button_icon="true")
# - Exclude $var patterns (component props like icon="$action_button_icon")
extract_xml_icons() {
    grep -h 'icon="' "$XML_DIR"/*.xml 2>/dev/null |
    grep -v '<!--' |
    grep -v '_icon=' |
    grep -oE 'icon="[^"]+"' |
    sed -E 's/icon="([^"]+)"/\1/' |
    grep -v '^\$' |
    sort -u
}

# Extract all defined icon names from codepoints.h
extract_defined_icons() {
    grep -oE '\{"[^"]+",\s*"\\x' "$CODEPOINTS_FILE" |
    sed -E 's/\{"([^"]+)".*/\1/' |
    sort -u
}

XML_ICONS=$(extract_xml_icons)
DEFINED_ICONS=$(extract_defined_icons)

# Find icons used in XML but not defined in codepoints.h (using comm, much faster)
UNDEFINED=$(comm -23 <(echo "$XML_ICONS") <(echo "$DEFINED_ICONS"))
if [[ -z "$UNDEFINED" ]]; then
    UNDEFINED_COUNT=0
else
    UNDEFINED_COUNT=$(echo "$UNDEFINED" | wc -l | tr -d ' ')
fi

XML_COUNT=$(echo "$XML_ICONS" | grep -c . || echo 0)
DEFINED_COUNT=$(echo "$DEFINED_ICONS" | grep -c . || echo 0)

echo "  Icons used in XML: $XML_COUNT"
echo "  Icons defined: $DEFINED_COUNT"
echo ""

if [[ $UNDEFINED_COUNT -gt 0 ]]; then
    echo "❌ UNDEFINED ICONS ($UNDEFINED_COUNT):"
    echo "   These icons are used in XML but not defined in $CODEPOINTS_FILE"
    echo ""
    echo "$UNDEFINED" | while IFS= read -r icon; do
        if [[ -n "$icon" ]]; then
            # Find which XML file uses this icon
            FILES=$(grep -rl "icon=\"$icon\"" "$XML_DIR" 2>/dev/null | sed 's|.*/||' | tr '\n' ', ' | sed 's/, $//')
            echo "   $icon (used in: $FILES)"
        fi
    done
    echo ""
    echo "To fix: Add the missing icon to include/ui_icon_codepoints.h"
    echo "        and scripts/regen_mdi_fonts.sh, then run 'make regen-fonts'"
    echo ""
    echo "Find codepoints at: https://pictogrammers.com/library/mdi/"
    exit 1
else
    echo "✓ All XML icon references are defined"
fi

# =============================================================================
# SORTED CHECK: ICON_MAP must be alphabetically sorted for binary search
# =============================================================================
echo ""
echo "Validating ICON_MAP sort order..."

# Extract icon names in order (as they appear in the file)
ICON_NAMES_ACTUAL=$(grep -E '\{"[^"]+",\s*"\\x' "$CODEPOINTS_FILE" | sed -E 's/.*\{"([^"]+)".*/\1/')
ICON_NAMES_SORTED=$(echo "$ICON_NAMES_ACTUAL" | env LANG=en_EN.UTF-8 sort)

# Compare actual order with sorted order
if [[ "$ICON_NAMES_ACTUAL" != "$ICON_NAMES_SORTED" ]]; then
    echo "❌ ICON_MAP is not alphabetically sorted!"
    echo ""
    echo "   The binary search in lookup_codepoint() requires alphabetical order."
    echo "   Out-of-order entries will cause icon lookup failures at runtime."
    echo ""

    # Find the first out-of-order entry
    PREV=""
    while IFS= read -r name; do
        if [[ -n "$PREV" && "$name" < "$PREV" ]]; then
            echo "   First error: '$name' should come before '$PREV'"
            break
        fi
        PREV="$name"
    done <<< "$ICON_NAMES_ACTUAL"
    echo ""
    echo "To fix: Reorder entries in $CODEPOINTS_FILE alphabetically"
    exit 1
else
    echo "✓ ICON_MAP is correctly sorted for binary search"
fi
