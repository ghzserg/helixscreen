#!/usr/bin/env python3

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Update globals.xml with FontAwesome UTF-8 icon constants.

This keeps the icon section clearly marked as auto-generated.
The UTF-8 characters are embedded directly in globals.xml since LVGL XML
doesn't support cross-component constant sharing.

Usage:
    python3 scripts/generate-icon-consts.py

To add new icons:
    1. Add to the ICONS dict below with Unicode codepoint
    2. Run this script to regenerate the icon section in globals.xml
    3. Use #icon_name in any XML file
"""

import re

# FontAwesome 6 Free Solid icon mappings
# Format: 'constant_name': (unicode_codepoint, 'description')
ICONS = {
    # Navigation icons
    'icon_home': (0xF015, 'house'),
    'icon_controls': (0xF1DE, 'sliders'),
    'icon_filament': (0xF008, 'film'),
    'icon_settings': (0xF013, 'gear'),
    'icon_advanced': (0xF142, 'ellipsis-vertical'),
    'icon_folder': (0xF07C, 'folder-open'),

    # Status icons
    'icon_temperature': (0xF2C7, 'thermometer-half'),
    'icon_wifi': (0xF1EB, 'wifi'),
    'icon_ethernet': (0xF796, 'ethernet'),
    'icon_wifi_slash': (0xF127, 'wifi-slash'),
    'icon_lightbulb': (0xF0EB, 'lightbulb'),
    'icon_question_circle': (0xF059, 'circle-question'),
    'icon_check_circle': (0xF058, 'circle-check (connected)'),
    'icon_xmark_circle': (0xF057, 'circle-xmark (error/failed)'),
    'icon_lock': (0xF023, 'lock (encrypted)'),

    # WiFi signal strength icons
    'icon_signal_wifi_1': (0xF6AC, 'wifi-1 (0-25% signal)'),
    'icon_signal_wifi_2': (0xF6AA, 'wifi-2 (26-50% signal)'),
    'icon_signal_wifi_3': (0xF6AB, 'wifi-3 (51-75% signal)'),
    'icon_signal_wifi_4': (0xF1EB, 'wifi (76-100% signal)'),

    # Metadata icons
    'icon_clock': (0xF017, 'clock-o'),
    'icon_leaf': (0xF06C, 'leaf'),

    # Action icons
    'icon_chevron_left': (0xF053, 'chevron-left'),
    'icon_trash': (0xF1F8, 'trash'),
    'icon_list': (0xF03A, 'list'),
    'icon_th_large': (0xF009, 'th-large'),

    # Controls panel launcher icons
    'icon_arrows_all': (0xF0B2, 'arrows-up-down-left-right'),
    'icon_fire': (0xF06D, 'fire'),
    'icon_arrow_up_line': (0xF342, 'arrow-up-from-line'),
    'icon_fan': (0xF863, 'fan'),
    'icon_power_off': (0xF011, 'power-off'),

    # Motion control icons - Unicode arrows from diagonal_arrows_40 font
    'icon_arrow_up': (0x2191, 'upwards arrow'),
    'icon_arrow_down': (0x2193, 'downwards arrow'),
    'icon_arrow_left': (0x2190, 'leftwards arrow'),
    'icon_arrow_right': (0x2192, 'rightwards arrow'),
    'icon_arrow_up_left': (0x2196, 'diagonal up-left'),
    'icon_arrow_up_right': (0x2197, 'diagonal up-right'),
    'icon_arrow_down_left': (0x2199, 'diagonal down-left'),
    'icon_arrow_down_right': (0x2198, 'diagonal down-right'),

    # FontAwesome chevrons for Z-axis
    'icon_chevron_up': (0xF077, 'chevron-up'),
    'icon_chevron_down': (0xF078, 'chevron-down'),

    # Keypad icons
    'icon_backspace': (0xF55A, 'delete-left'),

    # Filament operations icons
    'icon_arrow_down_to_line': (0xF33D, 'arrow-down-to-line (load)'),
    'icon_droplet': (0xF043, 'droplet (purge)'),
    'icon_cube': (0xF1B2, 'cube (PETG)'),
    'icon_edit': (0xF044, 'edit (custom)'),
    'icon_triangle_exclamation': (0xF071, 'triangle-exclamation (warning)'),
}

GLOBALS_FILE = 'ui_xml/globals.xml'

def generate_icon_section():
    """Generate the icon constants section."""
    lines = []
    lines.append('\t\t<!-- ===================================================================== -->')
    lines.append('\t\t<!-- FontAwesome icon constants - AUTO-GENERATED - DO NOT EDIT MANUALLY   -->')
    lines.append('\t\t<!-- Run: python3 scripts/generate-icon-consts.py                         -->')
    lines.append('\t\t<!-- ===================================================================== -->')

    for name, (codepoint, description) in sorted(ICONS.items()):
        char = chr(codepoint)
        unicode_val = f"U+{codepoint:04X}"
        lines.append(f'\t\t<str name="{name}" value="{char}"/>  <!-- {unicode_val} {description} -->')

    return '\n'.join(lines)

def update_globals_xml():
    """Update globals.xml with icon constants."""

    with open(GLOBALS_FILE, 'r', encoding='utf-8') as f:
        content = f.read()

    # Pattern to match the auto-generated icon section
    pattern = r'(\t<!-- ={69} -->.*?FontAwesome icon constants.*?<!-- ={69} -->.*?)(?=\n\t<!-- ={69} -->|\n</consts>)'

    new_section = generate_icon_section()

    if re.search(pattern, content, re.DOTALL):
        # Replace existing section
        content = re.sub(pattern, new_section, content, flags=re.DOTALL)
        print(f"Updated icon constants in {GLOBALS_FILE}")
    else:
        # Add new section before </consts>
        content = content.replace('</consts>', f'\n{new_section}\n</consts>')
        print(f"Added icon constants to {GLOBALS_FILE}")

    with open(GLOBALS_FILE, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f"Generated {len(ICONS)} icon constants:")
    for name, (codepoint, desc) in sorted(ICONS.items()):
        print(f"  #{name:<20} U+{codepoint:04X}  {desc}")

if __name__ == '__main__':
    update_globals_xml()
