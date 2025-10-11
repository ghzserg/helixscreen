#!/usr/bin/env python3
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
    'icon_home': (0xF015, 'house'),
    'icon_controls': (0xF1DE, 'sliders'),
    'icon_filament': (0xF576, 'fill-drip'),
    'icon_settings': (0xF013, 'gear'),
    'icon_advanced': (0xF142, 'ellipsis-vertical'),
    'icon_temperature': (0xF2C7, 'thermometer-half'),
    'icon_wifi': (0xF1EB, 'wifi'),
    'icon_ethernet': (0xF796, 'ethernet'),
    'icon_wifi_slash': (0xF127, 'wifi-slash'),
    'icon_lightbulb': (0xF0EB, 'lightbulb'),
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
    pattern = r'(\t\t<!-- ={69} -->.*?<!-- ={69} -->.*?(?=\t</consts>))'

    new_section = generate_icon_section()

    if re.search(pattern, content, re.DOTALL):
        # Replace existing section
        content = re.sub(pattern, new_section, content, flags=re.DOTALL)
        print(f"Updated icon constants in {GLOBALS_FILE}")
    else:
        # Add new section before </consts>
        content = content.replace('\t</consts>', f'\n{new_section}\n\t</consts>')
        print(f"Added icon constants to {GLOBALS_FILE}")

    with open(GLOBALS_FILE, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f"Generated {len(ICONS)} icon constants:")
    for name, (codepoint, desc) in sorted(ICONS.items()):
        print(f"  #{name:<20} U+{codepoint:04X}  {desc}")

if __name__ == '__main__':
    update_globals_xml()
