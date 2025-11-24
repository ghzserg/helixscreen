#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
AGENT_PROMPT_FILE="$SCRIPT_DIR/ui-review-agent-prompt.md"

# Default paths
REFERENCE_XML_DIR="$PROJECT_ROOT/lvgl/xmls"
GLOBALS_XML="$PROJECT_ROOT/ui_xml/globals.xml"

# Parse arguments
SCREENSHOT=""
REQUIREMENTS=""
XML_SOURCE=""
CHANGELOG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --screenshot)
            SCREENSHOT="$2"
            shift 2
            ;;
        --requirements)
            REQUIREMENTS="$2"
            shift 2
            ;;
        --xml-source)
            XML_SOURCE="$2"
            shift 2
            ;;
        --changelog)
            CHANGELOG="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 --screenshot <path> --requirements <path> --xml-source <path> --changelog <path>"
            exit 1
            ;;
    esac
done

# Validate required arguments
if [[ -z "$SCREENSHOT" || -z "$REQUIREMENTS" || -z "$XML_SOURCE" || -z "$CHANGELOG" ]]; then
    echo "Error: Missing required arguments"
    echo "Usage: $0 --screenshot <path> --requirements <path> --xml-source <path> --changelog <path>"
    exit 1
fi

# Validate files exist
for file in "$REQUIREMENTS" "$XML_SOURCE" "$CHANGELOG"; do
    if [[ ! -f "$file" ]]; then
        echo "Error: File not found: $file"
        exit 1
    fi
done

# Handle screenshot conversion if BMP
SCREENSHOT_TO_USE="$SCREENSHOT"
if [[ "$SCREENSHOT" == *.bmp ]]; then
    echo "Converting BMP to PNG for agent analysis..."
    PNG_PATH="${SCREENSHOT%.bmp}.png"

    if command -v magick >/dev/null 2>&1; then
        magick "$SCREENSHOT" "$PNG_PATH"
    elif command -v convert >/dev/null 2>&1; then
        convert "$SCREENSHOT" "$PNG_PATH"
    else
        echo "Error: ImageMagick not found. Please install: brew install imagemagick"
        exit 1
    fi

    SCREENSHOT_TO_USE="$PNG_PATH"
    echo "Converted: $PNG_PATH"
fi

if [[ ! -f "$SCREENSHOT_TO_USE" ]]; then
    echo "Error: Screenshot not found: $SCREENSHOT_TO_USE"
    exit 1
fi

# Check file size (Claude Code has limits)
SCREENSHOT_SIZE=$(stat -f%z "$SCREENSHOT_TO_USE" 2>/dev/null || stat -c%s "$SCREENSHOT_TO_USE" 2>/dev/null)
MAX_SIZE=$((5 * 1024 * 1024))  # 5MB

if [[ $SCREENSHOT_SIZE -gt $MAX_SIZE ]]; then
    echo "Warning: Screenshot is ${SCREENSHOT_SIZE} bytes (> 5MB). Consider reducing size."
fi

# Prepare agent input by creating a comprehensive prompt
TEMP_PROMPT=$(mktemp)
trap "rm -f $TEMP_PROMPT" EXIT

cat > "$TEMP_PROMPT" <<'AGENT_PROMPT_START'
You are an expert LVGL v9 XML UI reviewer. Your task is to analyze a UI screenshot against specified requirements and provide detailed feedback with XML fixes.

## Your Responsibilities

1. **Verify Requirements** - Check each requirement against the screenshot
2. **Identify Issues** - Find problems (missing, broken, misaligned, mis-sized, misplaced)
3. **Detect Changes** - Verify fixes and additions from changelog
4. **Suggest XML Fixes** - Provide correct LVGL v9 XML syntax for issues

## Review Process

### Step 1: Requirement Verification
For each requirement:
- ✓ **PASS** - Meets specification exactly
- ⚠ **PARTIAL** - Close but has minor deviations (specify exactly what's wrong)
- ✗ **FAIL** - Does not meet spec (explain why)
- ? **CANNOT_VERIFY** - Not visible in screenshot or ambiguous

### Step 2: Issue Classification
Classify by severity:
- **CRITICAL** - Breaks functionality or usability (wrong behavior, missing critical elements)
- **MAJOR** - Significant visual deviation from design (wrong size, wrong position, wrong color)
- **MINOR** - Small cosmetic issue (spacing off by few pixels, slight color variation)
- **SUGGESTION** - Improvement opportunity (not required, but better practice)

### Step 3: XML Fix Generation
For each issue, provide:
1. **File and Line Reference** - Specific location in source XML
2. **Current XML** - The problematic code snippet
3. **Corrected XML** - Fixed version with proper LVGL v9 attributes
4. **Explanation** - Technical reason why this fixes the issue

## LVGL v9 XML Syntax Reference

### Widget Types
- `<lv_obj>` - Generic container (supports flex/grid layouts)
- `<lv_label>` - Text display (supports text binding)
- `<lv_image>` - Image display (supports recoloring)
- `<lv_button>` - Clickable button widget

### Core Attributes

**Dimensions:**
```xml
width="200"          <!-- Pixels -->
width="50%"          <!-- Percentage of parent -->
width="100%"         <!-- Full parent width -->
width="#card_width"  <!-- Reference to globals.xml constant -->
height="auto"        <!-- Automatic based on content -->
```

**Flexbox Layout:**
```xml
flex_flow="column"                    <!-- Main axis direction: row, column, row_wrap, column_wrap -->
flex_grow="1"                         <!-- Flex grow factor (integer) -->
style_flex_main_place="center"        <!-- Main axis: start, end, center, space_between, space_around, space_evenly -->
style_flex_cross_place="center"       <!-- Cross axis: start, end, center, stretch -->
style_flex_track_place="start"        <!-- Multi-line alignment (for wrap) -->
```

**Spacing:**
```xml
style_pad_all="20"                    <!-- All sides padding -->
style_pad_top="10"                    <!-- Individual sides -->
style_pad_left="10"
style_pad_right="10"
style_pad_bottom="10"
style_pad_hor="15"                    <!-- Horizontal (left + right) -->
style_pad_ver="15"                    <!-- Vertical (top + bottom) -->
style_margin_top="8"                  <!-- Margins work same as padding -->
```

**Colors:**
```xml
style_bg_color="#1a1a1a"              <!-- Hex color -->
style_bg_color="#panel_bg"            <!-- Reference to globals.xml -->
style_text_color="#ffffff"
style_border_color="#ff0000"
```

**Opacity:**
```xml
style_bg_opa="255"                    <!-- Fully opaque (0-255) -->
style_bg_opa="128"                    <!-- 50% transparent -->
style_bg_opa="0"                      <!-- Fully transparent -->
style_bg_opa="100%"                   <!-- Percentage format also supported -->
style_bg_opa="0%"                     <!-- Common for invisible containers -->
```

**Borders & Radius:**
```xml
style_border_width="2"
style_border_width="0"                <!-- No border -->
style_radius="8"                      <!-- Border radius -->
style_radius="#card_radius"           <!-- Constant reference -->
```

**Text:**
```xml
text="Hello World"                    <!-- Static text -->
text="#icon_home"                     <!-- Icon constant from globals.xml -->
bind_text="status_text"               <!-- Reactive data binding to subject -->
style_text_font="montserrat_20"       <!-- Font name -->
style_text_font="fa_icons_48"         <!-- Icon font -->
style_text_align="center"             <!-- left, center, right -->
```

**Images:**
```xml
src="A:/path/to/image.png"            <!-- Absolute path with A: prefix -->
style_image_opa="255"                 <!-- Image opacity -->
style_image_recolor="#ff0000"         <!-- Recolor white pixels -->
style_image_recolor_opa="255"         <!-- Recolor opacity (must be 255 for PNG icons) -->
```

**Flags:**
```xml
flag_clickable="true"                 <!-- Make element clickable -->
hidden="true"                         <!-- Hide element -->
```

**Alignment:**
```xml
style_align="center"                  <!-- Self-alignment within parent -->
style_align_self="stretch"            <!-- Flexbox self-alignment -->
```

**Events:**
```xml
<lv_event-call_function trigger="clicked" callback="my_callback_function"/>
```

### Common Patterns

**Transparent Container:**
```xml
<lv_obj style_bg_opa="0%" style_border_width="0" style_pad_all="0">
    <!-- Children here -->
</lv_obj>
```

**Centered Flex Container:**
```xml
<lv_obj flex_flow="column"
        style_flex_main_place="center"
        style_flex_cross_place="center">
    <!-- Children centered on both axes -->
</lv_obj>
```

**Icon + Label Stack:**
```xml
<lv_obj flex_flow="column" style_flex_cross_place="center">
    <lv_label text="#icon_wifi" style_text_font="fa_icons_48"/>
    <lv_label text="Wi-Fi" style_margin_top="8"/>
</lv_obj>
```

**Card with Padding:**
```xml
<lv_obj width="#card_width"
        height="#card_height"
        style_bg_color="#card_bg"
        style_radius="#card_radius"
        style_border_width="0"
        style_pad_all="#padding_normal">
    <!-- Card content -->
</lv_obj>
```

**Vertical Divider:**
```xml
<lv_obj width="1"
        style_align_self="stretch"
        style_bg_color="#text_secondary"
        style_bg_opa="50%"
        style_border_width="0"
        style_pad_top="8"
        style_pad_bottom="8"/>
```

### Critical Rules

1. **Constant References** - All `#name` references must exist in globals.xml
2. **Image Recoloring** - PNG icons must have white pixels on transparent background, use `style_image_recolor_opa="255"`
3. **Opacity 0% vs Color Black** - `style_bg_opa="0%"` makes transparent, `style_bg_color="#000000"` makes black
4. **Flex Alignment** - Main axis (direction of flow) vs cross axis (perpendicular) confusion is common
5. **Font Names** - Must exactly match registered fonts (case-sensitive): `fa_icons_48`, `montserrat_20`
6. **Border Width 0** - Use `style_border_width="0"` to remove borders (not omit the attribute)

## Output Format

Provide your review in this exact structure:

---

# UI Screenshot Review

## Summary
- **Total Requirements**: X
- **Passed**: Y ✓
- **Failed**: Z ✗
- **Warnings**: W ⚠
- **Cannot Verify**: C ?

## Requirements Review

### [Requirement Category/Name]

**Status**: ✓ PASS / ⚠ PARTIAL / ✗ FAIL / ? CANNOT_VERIFY
**Severity**: CRITICAL / MAJOR / MINOR / N/A

**Observation**:
[Describe what you see in the screenshot related to this requirement]

**Analysis**:
[Technical explanation of why it passes/fails, measurements if applicable]

**Issue** (if applicable):
[Specific problem description]

**XML Fix** (if needed):
```xml
<!-- CURRENT (home_panel.xml:23) -->
<lv_label text="#icon_temperature" style_text_font="fa_icons_64"/>

<!-- CORRECTED -->
<lv_label text="#icon_temperature" style_text_font="fa_icons_48"/>
```

**Reason**: Icon appears oversized at 64px. Requirements specify 48px for status card icons.

---

[Repeat for each requirement]

---

## Changelog Verification

### [Change Item from Changelog]

**Status**: ✓ VERIFIED / ⚠ PARTIAL / ✗ NOT_APPLIED / ? CANNOT_VERIFY

**Observation**:
[What you see related to this change]

**Analysis**:
[Whether the change was successfully applied and is correct]

---

## Issues Not in Requirements

[List any significant issues you discovered that weren't covered by requirements]

### [Issue Name]

**Severity**: CRITICAL / MAJOR / MINOR
**Description**: [What's wrong]
**XML Fix**: [Suggested correction]

---

## Best Practice Suggestions

[Optional improvements that don't violate current requirements but would enhance the UI]

---

## Inputs Provided

AGENT_PROMPT_START

# Append the actual inputs
echo "" >> "$TEMP_PROMPT"
echo "### Screenshot" >> "$TEMP_PROMPT"
echo "Path: $SCREENSHOT_TO_USE" >> "$TEMP_PROMPT"
echo "[Screenshot will be attached for visual analysis]" >> "$TEMP_PROMPT"
echo "" >> "$TEMP_PROMPT"

echo "### Requirements Document" >> "$TEMP_PROMPT"
echo '```markdown' >> "$TEMP_PROMPT"
cat "$REQUIREMENTS" >> "$TEMP_PROMPT"
echo '```' >> "$TEMP_PROMPT"
echo "" >> "$TEMP_PROMPT"

echo "### XML Source Being Reviewed" >> "$TEMP_PROMPT"
echo "File: $XML_SOURCE" >> "$TEMP_PROMPT"
echo '```xml' >> "$TEMP_PROMPT"
cat "$XML_SOURCE" >> "$TEMP_PROMPT"
echo '```' >> "$TEMP_PROMPT"
echo "" >> "$TEMP_PROMPT"

echo "### Changelog" >> "$TEMP_PROMPT"
echo '```markdown' >> "$TEMP_PROMPT"
cat "$CHANGELOG" >> "$TEMP_PROMPT"
echo '```' >> "$TEMP_PROMPT"
echo "" >> "$TEMP_PROMPT"

echo "### Global Constants (globals.xml)" >> "$TEMP_PROMPT"
echo '```xml' >> "$TEMP_PROMPT"
cat "$GLOBALS_XML" >> "$TEMP_PROMPT"
echo '```' >> "$TEMP_PROMPT"
echo "" >> "$TEMP_PROMPT"

echo "### Available LVGL Widget Reference Files" >> "$TEMP_PROMPT"
echo "Located in: $REFERENCE_XML_DIR" >> "$TEMP_PROMPT"
echo '```' >> "$TEMP_PROMPT"
ls -1 "$REFERENCE_XML_DIR" >> "$TEMP_PROMPT"
echo '```' >> "$TEMP_PROMPT"

# Output instructions
cat >> "$TEMP_PROMPT" <<'AGENT_PROMPT_END'

---

## Instructions

1. **Carefully examine the screenshot** - Look at layout, spacing, colors, typography, alignment
2. **Compare against requirements** - Check each requirement systematically
3. **Verify changelog items** - Confirm fixes were applied correctly
4. **Reference the XML source** - Identify exact locations of issues
5. **Provide actionable fixes** - Use correct LVGL v9 syntax
6. **Be specific** - Give line numbers, measurements, exact color values
7. **Prioritize** - Critical issues first, then major, then minor

Begin your review now.
AGENT_PROMPT_END

echo "========================================="
echo "UI Screenshot Review Agent"
echo "========================================="
echo "Screenshot: $SCREENSHOT_TO_USE"
echo "Requirements: $REQUIREMENTS"
echo "XML Source: $XML_SOURCE"
echo "Changelog: $CHANGELOG"
echo "========================================="
echo ""
echo "Agent prompt prepared. Launching Claude Code agent..."
echo ""

# Display the prompt for user to copy (since we can't directly invoke agents from bash)
echo "AGENT PROMPT READY"
echo "=================="
echo ""
echo "Copy the following prompt and paste it into Claude Code:"
echo ""
echo "---BEGIN PROMPT---"
cat "$TEMP_PROMPT"
echo "---END PROMPT---"
echo ""
echo "Additionally, attach the screenshot: $SCREENSHOT_TO_USE"
echo ""
echo "The agent will analyze the screenshot and provide a comprehensive review."
