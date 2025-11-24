#!/usr/bin/env python3

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Material Design Icon Manager for HelixScreen
Downloads, converts, and registers Material Design icons for LVGL 9.

Usage:
    python3 scripts/material_icons.py add wifi-strength-1 wifi-strength-2 ...
    python3 scripts/material_icons.py list
    python3 scripts/material_icons.py convert wifi_strength_1.svg

Requirements:
    - curl (for downloading)
    - imagemagick (magick command for PNG conversion)
    - pypng, lz4 (for LVGL conversion via LVGLImage.py)
"""

import os
import sys
import argparse
import subprocess
import json
from pathlib import Path
from typing import List, Dict, Optional
import re

# Project paths
PROJECT_ROOT = Path(__file__).parent.parent
ASSETS_DIR = PROJECT_ROOT / "assets" / "images" / "material"
SCRIPTS_DIR = PROJECT_ROOT / "scripts"
INCLUDE_DIR = PROJECT_ROOT / "include"
SRC_DIR = PROJECT_ROOT / "src"
LVGLIMAGE_PY = SCRIPTS_DIR / "LVGLImage.py"

# Material Design Icons GitHub
MD_ICONS_BASE = "https://raw.githubusercontent.com/google/material-design-icons/master/src"

# Icon categories in Material Design
MD_CATEGORIES = [
    "action", "alert", "av", "communication", "content", "device", "editor",
    "file", "hardware", "home", "image", "maps", "navigation", "notification",
    "places", "social", "toggle"
]

# Color codes for output
class Colors:
    BLUE = '\033[34m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    RED = '\033[31m'
    BOLD = '\033[1m'
    RESET = '\033[0m'

def log_info(msg: str):
    print(f"{Colors.BLUE}[INFO]{Colors.RESET} {msg}")

def log_success(msg: str):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.RESET} {msg}")

def log_warning(msg: str):
    print(f"{Colors.YELLOW}[WARNING]{Colors.RESET} {msg}")

def log_error(msg: str):
    print(f"{Colors.RED}[ERROR]{Colors.RESET} {msg}", file=sys.stderr)

def check_dependencies() -> bool:
    """Check if required tools are available."""
    deps = {
        "curl": "Download SVG files",
        "magick": "Convert SVG to PNG (ImageMagick)",
        "python3": "Run LVGLImage.py conversion"
    }

    missing = []
    for cmd, desc in deps.items():
        if subprocess.run(["which", cmd], capture_output=True).returncode != 0:
            missing.append(f"{cmd} ({desc})")

    if missing:
        log_error("Missing dependencies:")
        for dep in missing:
            log_error(f"  - {dep}")
        log_error("\nInstall with:")
        log_error("  macOS: brew install imagemagick")
        log_error("  Debian/Ubuntu: sudo apt install imagemagick curl python3")
        return False

    # Check Python dependencies
    try:
        import png
        import lz4.block
    except ImportError as e:
        log_error(f"Missing Python dependency: {e}")
        log_error("Install with: pip3 install pypng lz4")
        return False

    return True

def sanitize_icon_name(name: str) -> str:
    """Convert icon name to valid C identifier (wifi-strength-1 -> wifi_strength_1)."""
    return name.replace("-", "_")

def download_svg(icon_name: str, category: Optional[str] = None) -> Optional[Path]:
    """
    Download SVG from Material Design Icons GitHub.

    Args:
        icon_name: Icon name (e.g., 'wifi-strength-1', 'lock')
        category: Optional category hint (e.g., 'device', 'action')

    Returns:
        Path to downloaded SVG or None if failed
    """
    ASSETS_DIR.mkdir(parents=True, exist_ok=True)

    # Try specified category first
    categories_to_try = [category] if category else MD_CATEGORIES

    for cat in categories_to_try:
        if cat is None:
            continue

        # Material Design uses 'materialicons' and 'materialiconsoutlined' variants
        # Try regular first, then outlined
        for variant in ["materialicons", "materialiconsoutlined"]:
            # URL format: src/{category}/{icon_name}/{variant}/24px.svg
            url = f"{MD_ICONS_BASE}/{cat}/{icon_name}/{variant}/24px.svg"
            output_path = ASSETS_DIR / f"{sanitize_icon_name(icon_name)}.svg"

            log_info(f"Trying {cat}/{icon_name}/{variant}...")

            result = subprocess.run(
                ["curl", "-f", "-s", "-L", "-o", str(output_path), url],
                capture_output=True
            )

            if result.returncode == 0:
                # Verify it's actually an SVG
                with open(output_path, 'r') as f:
                    content = f.read(100)
                    if '<svg' in content:
                        log_success(f"Downloaded: {icon_name} ({cat}/{variant})")
                        return output_path

            # Clean up failed attempt
            if output_path.exists():
                output_path.unlink()

    log_error(f"Could not find icon '{icon_name}' in Material Design Icons")
    log_info(f"Search URL pattern: {MD_ICONS_BASE}/{{category}}/{icon_name}/{{variant}}/24px.svg")
    return None

def convert_svg_to_png(svg_path: Path, size: int = 64) -> Optional[Path]:
    """Convert SVG to PNG using ImageMagick with proper alpha transparency.

    Preserves the original SVG's anti-aliased alpha channel while converting all
    RGB values to black. This produces smooth icon edges required for high-quality
    rendering with LVGL's image recoloring system.

    The approach:
    1. Render SVG at high density (-density 300) to avoid blurry upscaling
       Material Design icons are 24x24 native; upscaling to 64x64 needs high DPI
    2. Use transparent background (-background none) to preserve original alpha
    3. Resize to target dimensions
    4. Convert to grayscale color space
    5. Set all RGB channels to 0 (black) while preserving alpha channel

    This results in 8-bit gray+alpha PNG with black RGB and smooth alpha gradients
    for anti-aliased edges (e.g., graya(0,255) for solid, graya(0,1-254) for edges).

    The -density 300 flag is CRITICAL: without it, icons appear fuzzy/blurred.
    """
    png_path = svg_path.with_suffix('.png')

    result = subprocess.run([
        "magick",
        "-density", "300",
        "-background", "none",
        str(svg_path),
        "-resize", f"{size}x{size}",
        "-colorspace", "gray",
        "-channel", "RGB",
        "-evaluate", "set", "0",
        "+channel",
        str(png_path)
    ], capture_output=True, text=True)

    if result.returncode != 0:
        log_error(f"Failed to convert {svg_path.name}: {result.stderr}")
        return None

    log_success(f"Converted: {svg_path.name} → {png_path.name} ({size}x{size} gray+alpha)")
    return png_path

def convert_png_to_lvgl(png_path: Path) -> Optional[Path]:
    """Convert PNG to LVGL C array using LVGLImage.py."""
    c_path = png_path.with_suffix('.c')
    output_dir = png_path.parent

    # Use venv python if available, otherwise system python3
    python_bin = PROJECT_ROOT / ".venv" / "bin" / "python3"
    if not python_bin.exists():
        python_bin = "python3"

    result = subprocess.run([
        str(python_bin),
        str(LVGLIMAGE_PY),
        str(png_path),
        "--ofmt", "C",
        "--cf", "RGB565A8",
        "-o", str(output_dir),
        "--name", png_path.stem
    ], capture_output=True, text=True)

    if result.returncode != 0:
        log_error(f"Failed to convert {png_path.name} to LVGL: {result.stderr}")
        return None

    log_success(f"Generated: {c_path.name}")
    return c_path

def get_existing_icons() -> Dict[str, str]:
    """Parse material_icons.h to get currently registered icons."""
    icons = {}
    header_path = INCLUDE_DIR / "material_icons.h"

    if not header_path.exists():
        return icons

    with open(header_path, 'r') as f:
        for line in f:
            # Match: LV_IMG_DECLARE(icon_name);
            match = re.search(r'LV_IMG_DECLARE\((\w+)\);', line)
            if match:
                c_name = match.group(1)
                icons[c_name] = c_name

    return icons

def add_icon_to_header(icon_c_name: str, category: str = "WiFi & Network") -> bool:
    """Add LV_IMG_DECLARE to material_icons.h."""
    header_path = INCLUDE_DIR / "material_icons.h"

    if not header_path.exists():
        log_error(f"Header file not found: {header_path}")
        return False

    # Check if already exists
    with open(header_path, 'r') as f:
        content = f.read()
        if f"LV_IMG_DECLARE({icon_c_name})" in content:
            log_warning(f"Icon '{icon_c_name}' already declared in header")
            return True

    # Find the category section or create it
    lines = content.split('\n')
    insert_idx = None

    # Look for category comment
    for i, line in enumerate(lines):
        if f"// {category}" in line:
            # Insert after this comment
            insert_idx = i + 1
            break

    # If category not found, insert before material_icons_register() function
    if insert_idx is None:
        for i, line in enumerate(lines):
            if "void material_icons_register()" in line:
                # Add new category section
                insert_idx = i
                lines.insert(insert_idx, "")
                lines.insert(insert_idx + 1, f"// {category}")
                insert_idx += 2
                break

    if insert_idx is None:
        log_error("Could not find insertion point in header")
        return False

    # Insert declaration
    lines.insert(insert_idx, f"LV_IMG_DECLARE({icon_c_name});")

    # Write back
    with open(header_path, 'w') as f:
        f.write('\n'.join(lines))

    log_success(f"Added declaration to material_icons.h: {icon_c_name}")
    return True

def add_icon_to_cpp(icon_c_name: str, xml_name: str) -> bool:
    """Add lv_xml_register_image() call to material_icons.cpp."""
    cpp_path = SRC_DIR / "material_icons.cpp"

    if not cpp_path.exists():
        log_error(f"Source file not found: {cpp_path}")
        return False

    # Check if already registered
    with open(cpp_path, 'r') as f:
        content = f.read()
        if f'lv_xml_register_image(NULL, "{xml_name}", &{icon_c_name})' in content:
            log_warning(f"Icon '{xml_name}' already registered in cpp")
            return True

    # Find the material_icons_register() function and add before the closing brace
    lines = content.split('\n')
    insert_idx = None

    for i in range(len(lines) - 1, -1, -1):
        if lines[i].strip() == '}' and 'material_icons_register' in ''.join(lines[max(0, i-20):i]):
            insert_idx = i
            break

    if insert_idx is None:
        log_error("Could not find material_icons_register() function closing brace")
        return False

    # Add registration line
    indent = "    "
    reg_line = f'{indent}lv_xml_register_image(NULL, "{xml_name}", &{icon_c_name});'
    lines.insert(insert_idx, reg_line)

    # Update count in log message
    for i, line in enumerate(lines):
        if 'spdlog::info("Registering Material Design icons' in line:
            # Extract current count
            match = re.search(r'\((\d+) total\)', line)
            if match:
                old_count = int(match.group(1))
                new_count = old_count + 1
                lines[i] = line.replace(f"({old_count} total)", f"({new_count} total)")
            break

    # Write back
    with open(cpp_path, 'w') as f:
        f.write('\n'.join(lines))

    log_success(f"Added registration to material_icons.cpp: {xml_name} -> {icon_c_name}")
    return True

def add_icon(icon_name: str, category: Optional[str] = None) -> bool:
    """
    Complete workflow: download SVG, convert to PNG, convert to LVGL C array,
    and register in material_icons.h/.cpp.
    """
    log_info(f"Adding icon: {icon_name}")

    # Sanitize name for C identifier
    c_name = sanitize_icon_name(icon_name)
    xml_name = f"mat_{c_name}"

    # Check if SVG already exists
    svg_path = ASSETS_DIR / f"{c_name}.svg"
    if not svg_path.exists():
        svg_path = download_svg(icon_name, category)
        if not svg_path:
            return False
    else:
        log_info(f"Using existing SVG: {svg_path.name}")

    # Convert SVG to PNG
    png_path = ASSETS_DIR / f"{c_name}.png"
    if not png_path.exists():
        png_path = convert_svg_to_png(svg_path)
        if not png_path:
            return False
    else:
        log_info(f"Using existing PNG: {png_path.name}")

    # Convert PNG to LVGL C array
    c_path = ASSETS_DIR / f"{c_name}.c"
    if not c_path.exists():
        c_path = convert_png_to_lvgl(png_path)
        if not c_path:
            return False
    else:
        log_info(f"Using existing C array: {c_path.name}")

    # Add to header and cpp
    if not add_icon_to_header(c_name, "WiFi & Network"):
        return False

    if not add_icon_to_cpp(c_name, xml_name):
        return False

    log_success(f"✓ Icon '{icon_name}' fully integrated as '{xml_name}'")
    return True

def list_icons():
    """List all currently registered Material Design icons."""
    icons = get_existing_icons()

    if not icons:
        log_warning("No icons found in material_icons.h")
        return

    log_info(f"Registered Material Design icons ({len(icons)} total):")
    for c_name in sorted(icons.keys()):
        xml_name = f"mat_{c_name}"
        c_file = ASSETS_DIR / f"{c_name}.c"
        status = "✓" if c_file.exists() else "✗"
        print(f"  {status} {xml_name:<30} ({c_name}.c)")

def convert_existing(svg_file: str) -> bool:
    """Convert existing SVG file to PNG and LVGL C array."""
    svg_path = ASSETS_DIR / svg_file

    if not svg_path.exists():
        log_error(f"SVG file not found: {svg_path}")
        return False

    # Extract icon name
    icon_name = svg_path.stem
    c_name = sanitize_icon_name(icon_name)

    log_info(f"Converting existing SVG: {svg_file}")

    # Convert to PNG
    png_path = convert_svg_to_png(svg_path)
    if not png_path:
        return False

    # Convert to LVGL
    c_path = convert_png_to_lvgl(png_path)
    if not c_path:
        return False

    log_success(f"✓ Converted {svg_file} → {c_name}.c")
    return True

def main():
    parser = argparse.ArgumentParser(
        description="Material Design Icon Manager for HelixScreen",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Add wifi signal strength icons
  python3 scripts/material_icons.py add wifi-strength-1 wifi-strength-2 wifi-strength-3 wifi-strength-4

  # Add lock variants
  python3 scripts/material_icons.py add wifi-strength-1-lock wifi-strength-2-lock

  # List registered icons
  python3 scripts/material_icons.py list

  # Convert existing SVGs
  python3 scripts/material_icons.py convert wifi_strength_1.svg wifi_strength_2.svg
"""
    )

    subparsers = parser.add_subparsers(dest='command', help='Command to run')

    # Add command
    add_parser = subparsers.add_parser('add', help='Download and add new icons')
    add_parser.add_argument('icons', nargs='+', help='Icon names to add (e.g., wifi-strength-1)')
    add_parser.add_argument('--category', '-c', help='Material Design category hint (e.g., device, action)')

    # List command
    subparsers.add_parser('list', help='List all registered icons')

    # Convert command
    convert_parser = subparsers.add_parser('convert', help='Convert existing SVG files')
    convert_parser.add_argument('svgs', nargs='+', help='SVG filenames to convert')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Check dependencies
    if not check_dependencies():
        return 1

    # Execute command
    if args.command == 'add':
        success_count = 0
        for icon_name in args.icons:
            if add_icon(icon_name, args.category):
                success_count += 1

        log_info(f"\nAdded {success_count}/{len(args.icons)} icons successfully")

        if success_count > 0:
            log_info("\nNext steps:")
            log_info("  1. Rebuild: make")
            log_info("  2. Use in XML: <icon src='mat_icon_name' scale='200'/>")

        return 0 if success_count == len(args.icons) else 1

    elif args.command == 'list':
        list_icons()
        return 0

    elif args.command == 'convert':
        success_count = 0
        for svg_file in args.svgs:
            if convert_existing(svg_file):
                success_count += 1

        log_info(f"\nConverted {success_count}/{len(args.svgs)} files successfully")
        return 0 if success_count == len(args.svgs) else 1

    return 1

if __name__ == "__main__":
    sys.exit(main())
