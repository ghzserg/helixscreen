#!/usr/bin/env python3

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Embed FontAwesome icon UTF-8 characters into LVGL XML files.

Usage:
    python3 embed-icons-in-xml.py <xml_file> <icon_name> <utf8_bytes>

Example:
    python3 embed-icons-in-xml.py nav.xml ICON_HOME "EF 80 95"
"""

import sys

def bytes_to_char(hex_bytes):
    """Convert space-separated hex bytes to UTF-8 character."""
    byte_list = bytes.fromhex(hex_bytes.replace(' ', ''))
    return byte_list.decode('utf-8')

# FontAwesome 6 icon mappings (from ui_fonts.h)
ICONS = {
    'ICON_HOME': 'EF 80 95',           # U+F015 house
    'ICON_CONTROLS': 'EF 87 9E',       # U+F1DE sliders
    'ICON_FILAMENT': 'EF 95 B6',       # U+F576 fill-drip
    'ICON_SETTINGS': 'EF 80 93',       # U+F013 gear
    'ICON_ADVANCED': 'EF 85 82',       # U+F142 ellipsis-vertical
}

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Available icons:")
        for name, hex_bytes in ICONS.items():
            char = bytes_to_char(hex_bytes)
            print(f"  {name}: {hex_bytes} -> '{char}'")
        sys.exit(0)

    if len(sys.argv) == 3:
        # Convert icon name to character
        icon_name = sys.argv[1]
        if icon_name in ICONS:
            char = bytes_to_char(ICONS[icon_name])
            print(char, end='')
        else:
            print(f"Unknown icon: {icon_name}", file=sys.stderr)
            sys.exit(1)
    elif len(sys.argv) == 4:
        # Custom hex bytes
        custom_bytes = sys.argv[2]
        char = bytes_to_char(custom_bytes)
        print(char, end='')
