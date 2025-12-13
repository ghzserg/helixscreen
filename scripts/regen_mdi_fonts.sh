#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regenerate Material Design Icons fonts for LVGL
#
# This script generates LVGL font files using MDI (Pictogrammers) icons.
# MDI provides 7200+ icons in a single unified font, eliminating the need
# for FontAwesome's split Solid/Brands/Regular fonts.
#
# MDI uses Unicode Plane 15 Private Use Area (0xF0000+)
# UTF-8 encoding: 4 bytes per codepoint (vs FontAwesome's 3-byte 0xF000 range)
#
# Source: https://pictogrammers.com/library/mdi/
# Font:   https://github.com/Templarian/MaterialDesign-Webfont

set -e
cd "$(dirname "$0")/.."

# Add node_modules/.bin to PATH so lv_font_conv is available
# (same as how npm scripts work)
export PATH="$PWD/node_modules/.bin:$PATH"

FONT=assets/fonts/materialdesignicons-webfont.ttf

# Check font exists
if [ ! -f "$FONT" ]; then
    echo "ERROR: MDI font not found at $FONT"
    echo "Download from: https://github.com/Templarian/MaterialDesign-Webfont/raw/master/fonts/materialdesignicons-webfont.ttf"
    exit 1
fi

# Icon mapping: FA name -> MDI codepoint
# All codepoints verified from https://pictogrammers.com/library/mdi/
#
# Navigation & Structure
# ----------------------
# home           F02DC  (was FA F015)
# cog            F0493  (was FA F013) - settings
# tune-variant   F08E9  (was FA F1DE) - controls/sliders
# dots-vertical  F01D9  (was FA F142) - advanced/more
# folder-open    F0770  (was FA F07C)
#
# Status & Network
# ----------------
# thermometer    F050F  (was FA F2C7)
# wifi           F05A9  (was FA F1EB)
# wifi-strength-1 F091F (was FA F6AC)
# wifi-strength-2 F0922 (was FA F6AA)
# wifi-strength-3 F0925 (was FA F6AB)
# wifi-off       F05AA  (was FA F127) - wifi-slash
# ethernet       F0200  (was FA F796)
# lightbulb      F0335  (was FA F0EB)
# usb            F05E0  (was missing in FA Solid!)
# bluetooth      F00AF  (was missing in FA Solid!)
#
# Media Controls
# --------------
# play           F040A  (was FA F04B)
# pause          F03E4  (was FA F04C)
# stop           F04DB  (was FA F04D)
#
# UI Elements
# -----------
# check          F012C  (was FA F00C)
# close          F0156  (was FA F00D) - xmark
# check-circle   F05E0  (was FA F058) -- NOTE: using check-circle-outline F05E1 instead
# close-circle   F0159  (was FA F057) - xmark-circle
# information    F02FC  (was FA F05A) - info-circle
# help-circle    F02D7  (was FA F059) - question-circle
# alert          F0026  (was FA F071) - triangle-exclamation
#
# Arrows & Chevrons
# -----------------
# chevron-left   F0141  (was FA F053)
# chevron-right  F0142  (was FA F054)
# chevron-up     F0143  (was FA F077)
# chevron-down   F0140  (was FA F078)
# arrow-all      F0026  (was FA F0B2) - arrows-all
# arrow-up-bold  F0737  (alternative to arrow-up-from-line)
# arrow-down-bold F072E (alternative to arrow-down-to-line)
#
# Actions
# -------
# delete         F01B4  (was FA F1F8) - trash
# pencil         F03EB  (was FA F044) - edit
# backspace      F00AC  (was FA F55A)
# lock           F033E  (was FA F023)
#
# Objects
# -------
# clock-outline  F0150  (was FA F017)
# cube-outline   F1A2F  (was FA F1B2) - cube
# water          F058C  (was FA F043) - droplet
# fire           F0238  (was FA F06D)
# leaf           F0436  (was FA F06C)
# fan            F0210  (was FA F863)
# power          F0425  (was FA F011) - power-off
# bell           F009A  (was FA F0F3) - notifications
#
# List/Grid
# ---------
# format-list-bulleted F0279 (was FA F03A) - list
# view-grid      F0588  (was FA F009) - th-large
# film           F097A  (was FA F008) - filament (spool icon doesn't exist, use film/reel)

# MDI icon codepoints (0xF0000 range)
# All icons we need, sorted by codepoint for lv_font_conv efficiency
# Format: 0xFxxxx (MDI uses 5 hex digits in the Private Use Area Plane 15)
#
# Note: MDI is a pure icon font - no ASCII characters included.
# lv_font_conv handles sparse high codepoints fine without ASCII workaround.
# =============================================================================
# Icon codepoints organized by category
# All icons needed for the font-based icon system
# =============================================================================

MDI_ICONS="0xF0026"      # alert (triangle-exclamation)
MDI_ICONS+=",0xF0029"    # alert-octagon (emergency)
MDI_ICONS+=",0xF05D8"    # animation (stacked rectangles - for settings)
MDI_ICONS+=",0xF009A"    # bell (notifications)
MDI_ICONS+=",0xF00AD"    # block-helper (prohibited)
MDI_ICONS+=",0xF00AF"    # bluetooth
MDI_ICONS+=",0xF0B5C"    # backspace-outline

# Check/Close/UI
MDI_ICONS+=",0xF012A"    # chart-line
MDI_ICONS+=",0xF012C"    # check
MDI_ICONS+=",0xF0140"    # chevron-down
MDI_ICONS+=",0xF0141"    # chevron-left
MDI_ICONS+=",0xF0142"    # chevron-right
MDI_ICONS+=",0xF0143"    # chevron-up
MDI_ICONS+=",0xF0150"    # clock-outline
MDI_ICONS+=",0xF0156"    # close (xmark)
MDI_ICONS+=",0xF0159"    # close-circle (xmark-circle)
MDI_ICONS+=",0xF0174"    # code-tags
MDI_ICONS+=",0xF018D"    # console/terminal
MDI_ICONS+=",0xF01B4"    # delete (trash)
MDI_ICONS+=",0xF01D9"    # dots-vertical (advanced)
MDI_ICONS+=",0xF01FA"    # engine (motor)

# Ethernet/Network
MDI_ICONS+=",0xF0200"    # ethernet
MDI_ICONS+=",0xF0210"    # fan
MDI_ICONS+=",0xF0238"    # fire
MDI_ICONS+=",0xF024B"    # folder
MDI_ICONS+=",0xF0259"    # folder-upload
MDI_ICONS+=",0xF0279"    # format-list-bulleted (list)
MDI_ICONS+=",0xF02D7"    # help-circle (question-circle)
MDI_ICONS+=",0xF02DC"    # home
MDI_ICONS+=",0xF02E3"    # bed
MDI_ICONS+=",0xF02FC"    # information (info-circle)
MDI_ICONS+=",0xF02FD"    # information-outline

# Layers/Light
MDI_ICONS+=",0xF0317"    # lan
MDI_ICONS+=",0xF0328"    # layers
MDI_ICONS+=",0xF0335"    # lightbulb
MDI_ICONS+=",0xF033E"    # lock
MDI_ICONS+=",0xF03D6"    # package-variant (inventory)
MDI_ICONS+=",0xF03E4"    # pause
MDI_ICONS+=",0xF03EB"    # pencil (edit)
MDI_ICONS+=",0xF040A"    # play
MDI_ICONS+=",0xF040C"    # play-circle (resume)
MDI_ICONS+=",0xF0425"    # power (power-off)
MDI_ICONS+=",0xF042A"    # printer
MDI_ICONS+=",0xF042B"    # printer-3d
MDI_ICONS+=",0xF0436"    # leaf
MDI_ICONS+=",0xF0438"    # radiator (heater alternative)
MDI_ICONS+=",0xF044E"    # redo (clockwise arrow - for tighten)
MDI_ICONS+=",0xF0450"    # refresh
MDI_ICONS+=",0xF0465"    # rotate-left (CCW rotation)
MDI_ICONS+=",0xF0467"    # rotate-right (CW rotation)
MDI_ICONS+=",0xF0469"    # router-wireless
MDI_ICONS+=",0xF0479"    # sd (SD card)
MDI_ICONS+=",0xF048A"    # send (for console input)
MDI_ICONS+=",0xF0493"    # cog (settings)
MDI_ICONS+=",0xF04C5"    # speedometer
MDI_ICONS+=",0xF04DB"    # stop
MDI_ICONS+=",0xF050F"    # thermometer
MDI_ICONS+=",0xF054C"    # undo (counter-clockwise arrow - for loosen)
MDI_ICONS+=",0xF0E04"    # thermometer-minus
MDI_ICONS+=",0xF0E05"    # thermometer-plus

# View/Dashboard
MDI_ICONS+=",0xF0566"    # vibrate (input shaper)
MDI_ICONS+=",0xF0567"    # video (timelapse)
MDI_ICONS+=",0xF0568"    # video-off (timelapse disabled)
MDI_ICONS+=",0xF056E"    # view-dashboard
MDI_ICONS+=",0xF0570"    # view-grid
MDI_ICONS+=",0xF0572"    # view-list
MDI_ICONS+=",0xF058C"    # water (droplet)
MDI_ICONS+=",0xF05A1"    # weight
MDI_ICONS+=",0xF05A9"    # wifi
MDI_ICONS+=",0xF05AA"    # wifi-off (wifi-slash)
MDI_ICONS+=",0xF05E0"    # usb
MDI_ICONS+=",0xF05E1"    # check-circle-outline (check-circle)

# Tune/Update
MDI_ICONS+=",0xF062E"    # tune
MDI_ICONS+=",0xF06A5"    # power-plug
MDI_ICONS+=",0xF06B0"    # update
MDI_ICONS+=",0xF0717"    # snowflake (cooldown)
MDI_ICONS+=",0xF072E"    # arrow-down-bold (z-closer)
MDI_ICONS+=",0xF0731"    # arrow-left-bold
MDI_ICONS+=",0xF0734"    # arrow-right-bold
MDI_ICONS+=",0xF0737"    # arrow-up-bold (z-farther)
MDI_ICONS+=",0xF073A"    # cancel
MDI_ICONS+=",0xF0758"    # grid-large
MDI_ICONS+=",0xF0770"    # folder-open
MDI_ICONS+=",0xF0792"    # arrow-collapse-down (flow-down)
MDI_ICONS+=",0xF0793"    # arrow-expand-down (bed drops - CoreXY Z closer)
MDI_ICONS+=",0xF0795"    # arrow-collapse-up (flow-up)
MDI_ICONS+=",0xF0796"    # arrow-expand-up (bed rises - CoreXY Z farther)
MDI_ICONS+=",0xF081D"    # fan-off
MDI_ICONS+=",0xF08E9"    # tune-variant (controls)

# WiFi strength icons
MDI_ICONS+=",0xF091F"    # wifi-strength-1
MDI_ICONS+=",0xF0920"    # wifi-strength-1-alert
MDI_ICONS+=",0xF0921"    # wifi-strength-1-lock
MDI_ICONS+=",0xF0922"    # wifi-strength-2
MDI_ICONS+=",0xF0924"    # wifi-strength-2-lock
MDI_ICONS+=",0xF0925"    # wifi-strength-3
MDI_ICONS+=",0xF0927"    # wifi-strength-3-lock
MDI_ICONS+=",0xF0928"    # wifi-strength-4
MDI_ICONS+=",0xF092A"    # wifi-strength-4-lock
MDI_ICONS+=",0xF095B"    # sine-wave (input shaper)
MDI_ICONS+=",0xF022F"    # film (filament - film reel icon)
MDI_ICONS+=",0xF0A46"    # engine-off (motor-off)
MDI_ICONS+=",0xF0D49"    # axis-arrow (all 3 axes)
MDI_ICONS+=",0xF0D4C"    # axis-x-arrow
MDI_ICONS+=",0xF0D51"    # axis-y-arrow
MDI_ICONS+=",0xF0D55"    # axis-z-arrow
MDI_ICONS+=",0xF0E4F"    # lightbulb-off
MDI_ICONS+=",0xF0E5B"    # printer-3d-nozzle (extruder)
MDI_ICONS+=",0xF0EC7"    # rotate-3d (orbit/3D view rotation)
MDI_ICONS+=",0xF0F85"    # speedometer-medium (speed-up)
MDI_ICONS+=",0xF0F86"    # speedometer-slow (speed-down)
MDI_ICONS+=",0xF0F9C"    # home-import-outline (home-z)
MDI_ICONS+=",0xF1323"    # hammer-wrench (tools/build)
MDI_ICONS+=",0xF16B5"    # wifi-alert
MDI_ICONS+=",0xF16BD"    # wifi-check
MDI_ICONS+=",0xF16BF"    # wifi-lock
MDI_ICONS+=",0xF18B8"    # printer-3d-nozzle-heat (heater)
MDI_ICONS+=",0xF1A2F"    # cube-outline
MDI_ICONS+=",0xF1A45"    # heat-wave (for heated bed icon)
MDI_ICONS+=",0xF1B35"    # train-car-flatbed (print bed base)

# Custom icons
MDI_ICONS+=",0xF01BE"    # cursor-move (4-way movement cross)
MDI_ICONS+=",0xF004C"    # arrow-expand-all (expand_all/move)
MDI_ICONS+=",0xF03D4"    # hourglass (timer/loading)

# Arrow icons for motion control (using MDI arrows)
MDI_ICONS+=",0xF005D"    # arrow-up
MDI_ICONS+=",0xF0045"    # arrow-down
MDI_ICONS+=",0xF004D"    # arrow-left
MDI_ICONS+=",0xF0054"    # arrow-right
MDI_ICONS+=",0xF0041"    # arrow-bottom-left
MDI_ICONS+=",0xF0043"    # arrow-bottom-right
MDI_ICONS+=",0xF005C"    # arrow-top-left
MDI_ICONS+=",0xF005E"    # arrow-top-right
MDI_ICONS+=",0xF004E"    # arrow-left-right (arrows-all horizontal)
MDI_ICONS+=",0xF005F"    # arrow-up-down (arrows-all vertical)

# Keyboard icons (for on-screen keyboard special keys)
MDI_ICONS+=",0xF030F"    # keyboard-close (dismiss keyboard)
MDI_ICONS+=",0xF0311"    # keyboard-return (enter key)
MDI_ICONS+=",0xF0632"    # apple-keyboard-caps (caps lock indicator)
MDI_ICONS+=",0xF0636"    # apple-keyboard-shift (shift key outline)

# Sizes to generate - matching text font sizes for consistent UI
SIZES="16 24 32 48 64"

echo "Generating Material Design Icons fonts for LVGL..."
echo "  Font: $FONT"
echo ""

for SIZE in $SIZES; do
    OUTPUT="assets/fonts/mdi_icons_${SIZE}.c"
    echo "  Generating mdi_icons_${SIZE} -> $OUTPUT"

    lv_font_conv \
        --font "$FONT" --size "$SIZE" --bpp 4 --format lvgl \
        --range "$MDI_ICONS" \
        --no-compress \
        -o "$OUTPUT"
done

echo ""
echo "Done! Generated fonts for sizes: $SIZES"
echo ""
echo "Next steps:"
echo "  1. Update include/ui_fonts.h - replace FA ICON_* macros with MDI codepoints"
echo "  2. Update src/main.cpp - register mdi_icons_* instead of fa_icons_*"
echo "  3. Update ui_xml/globals.xml - change icon values to MDI codepoints"
echo "  4. Update ui_xml/*.xml - change fa_icons_* to mdi_icons_*"
echo "  5. Update Makefile - change FONT_SRCS from fa_icons to mdi_icons"
echo "  6. Delete old fa_icons_*.c and fa-solid-900.ttf"
