#!/usr/bin/env python3

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Download printer images using headless browser
This bypasses anti-hotlinking by actually rendering the page
"""

import subprocess
import sys
from pathlib import Path
import time

# Target dimensions (matching existing images)
TARGET_WIDTH = 750
TARGET_HEIGHT = 930

# Image URLs and filenames (from user-provided Brave search results)
PRINTER_IMAGES = [
    # Voron family
    ("https://imgs.search.brave.com/P0wo4bnoXhVOuj0LOqgPcRjb52LdyeCBEMws2DqfZeY/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly92b3Jv/bmRlc2lnbi5jb20v/d3AtY29udGVudC91/cGxvYWRzLzIwMjEv/MDMvdm9yb24xLWhl/cm8tMS5qcGc", "voron-v1-legacy.png"),
    ("https://imgs.search.brave.com/Tc0CST0yBnIoD9QbunxHx-HICeUT4k7LEbkKjCcCwQc/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly92b3Jv/bmRlc2lnbi5jb20v/d3AtY29udGVudC91/cGxvYWRzLzIwMjEv/MDMvc3cxLmpwZw", "voron-switchwire.png"),

    # Creality Ender series
    ("https://imgs.search.brave.com/v-sbIc7FDG6hP7BL1_hkvOlepDjcplMQfPEtP60MQAU/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9tLm1l/ZGlhLWFtYXpvbi5j/b20vaW1hZ2VzL0kv/NzFrT3l5ZUNCN0wu/anBn", "creality-ender3.png"),
    ("https://imgs.search.brave.com/26N3bXGvOprV7kJkgDSKfULn0Kj_VVLY-oru7d18yW0/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9tLm1l/ZGlhLWFtYXpvbi5j/b20vaW1hZ2VzL0kv/NzFOdGlGcE9DeEwu/anBn", "creality-ender5.png"),

    # Creality CR series
    ("https://imgs.search.brave.com/9Cq2T88a3KQKeyzGWGUEGuk9qZHCwZRm6VgvNQNJUCo/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9tLm1l/ZGlhLWFtYXpvbi5j/b20vaW1hZ2VzL0kv/NzFjSlBJYnU4M0wu/anBn", "creality-cr10.png"),
    ("https://imgs.search.brave.com/6fF3ZEzItmGbeYATh9JqbvC8h0muth-NPT94_rpxtfw/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9tLm1l/ZGlhLWFtYXpvbi5j/b20vaW1hZ2VzL0kv/NjFYTVdWVGVjVkwu/anBn", "creality-cr6se.png"),

    # Prusa family
    ("https://imgs.search.brave.com/2wLWlgaJ-s6lbcxLMUzLJHl_BuBmrh0qdbVJ13FxxTo/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9jZG4u/bXlzaG9wLmNvbS90/aHVtYi81MTJ4NTEy/L2Fzc2V0cy9tZWRp/YS9pbWFnZS83NC83/Mi80YTU5ZmFiM2Zk/ZGRfUFJVU0EtbWsx/LmpwZw", "prusa-mk3.png"),
    ("https://imgs.search.brave.com/dK4KwZ-0lOD_Z0bDVEDfmmSljG9FY1eSl24tQ2aloh0/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9jZG4u/bXlzaG9wLmNvbS90/aHVtYi81MTJ4NTEy/L2Fzc2V0cy9tZWRp/YS9pbWFnZS8wZi9k/Yy9kZTUyNTJkMmNl/NmJfUHJ1c2EtTUs0/LWZyb250LmpwZw", "prusa-mk4.png"),
    ("https://imgs.search.brave.com/IM4fpxRn6QVrHeZ2ETT1_0EwjVOwAjd0hd_jUPpp1Wo/rs:fit:500:0:0:0/g:ce/aHR0cHM6Ly93d3cu/Y29tcHV0ZXJhY3Rp/dmUuY28udWsvaW1h/Z2VzL3Byb2R1Y3Rz/L2xhcmdlL1BSVVNB/LU1JTkktMDA5LTEu/anBn", "prusa-mini.png"),
    ("https://imgs.search.brave.com/lAQvhainG_oDHsEhPtRmfMkX2FGbaE3XyJ0jY5TTNH4/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9jZG4u/bXlzaG9wLmNvbS90/aHVtYi81MTJ4NTEy/L2Fzc2V0cy9tZWRp/YS9pbWFnZS82Mi8w/Ny84MTI4MzM5YjU4/NjNfUHJ1c2EteGwu/anBn", "prusa-xl.png"),
]

def check_browser():
    """Check for Brave, Chrome, or Chromium"""
    browsers = [
        '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '/Applications/Chromium.app/Contents/MacOS/Chromium',
    ]

    for browser in browsers:
        if Path(browser).exists():
            return browser

    # Try command-line versions
    for cmd in ['brave', 'google-chrome', 'chromium']:
        try:
            subprocess.run(['which', cmd], check=True, capture_output=True)
            return cmd
        except:
            continue

    return None

def download_with_browser(url, output_path, browser_path):
    """Download image using headless browser screenshot"""
    print(f"Downloading: {output_path.name}")

    cmd = [
        browser_path,
        '--headless',
        '--disable-gpu',
        '--screenshot=' + str(output_path),
        '--window-size=1920,1080',
        '--default-background-color=0',
        url
    ]

    try:
        # Don't capture output - let it write to console
        result = subprocess.run(cmd, check=True, timeout=15,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Wait a moment for file write to complete
        time.sleep(1)
        # Check if file was created
        if output_path.exists() and output_path.stat().st_size > 0:
            return True
        else:
            print(f"  ERROR: Screenshot file not created or empty")
            return False
    except Exception as e:
        print(f"  ERROR: {e}")
        return False

def resize_image(input_path, output_path):
    """Resize image using ImageMagick to target dimensions"""
    print(f"Resizing: {input_path.name} -> {output_path.name}")
    try:
        cmd = [
            "magick", str(input_path),
            "-resize", f"{TARGET_WIDTH}x{TARGET_HEIGHT}",
            "-gravity", "center",
            "-extent", f"{TARGET_WIDTH}x{TARGET_HEIGHT}",
            "-background", "white",
            str(output_path)
        ]
        subprocess.run(cmd, check=True, capture_output=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"  ERROR resizing: {e.stderr.decode()}")
        return False

def main():
    # Check for browser
    browser_path = check_browser()
    if not browser_path:
        print("ERROR: No browser found. Install Brave, Chrome, or Chromium.")
        print("On macOS: brew install --cask brave-browser")
        sys.exit(1)

    print(f"Using browser: {browser_path}\n")

    # Create output directory
    output_dir = Path("assets/images/printers")
    output_dir.mkdir(parents=True, exist_ok=True)

    temp_dir = Path("/tmp/printer_images_browser")
    temp_dir.mkdir(exist_ok=True)

    success_count = 0
    failed = []

    for url, filename in PRINTER_IMAGES:
        temp_file = temp_dir / f"temp_{filename}"
        final_file = output_dir / filename

        # Skip if already exists
        if final_file.exists():
            print(f"SKIP: {filename} already exists")
            success_count += 1
            continue

        # Download with headless browser
        if not download_with_browser(url, temp_file, browser_path):
            failed.append(filename)
            continue

        # Small delay to let file write complete
        time.sleep(0.5)

        # Resize
        if not resize_image(temp_file, final_file):
            failed.append(filename)
            temp_file.unlink(missing_ok=True)
            continue

        # Clean up temp file
        temp_file.unlink(missing_ok=True)
        success_count += 1
        print(f"  ✓ Success: {filename}\n")

    print(f"\n{'='*60}")
    print(f"Processed {success_count}/{len(PRINTER_IMAGES)} images successfully")

    if failed:
        print(f"\nFailed images ({len(failed)}):")
        for f in failed:
            print(f"  - {f}")
        sys.exit(1)
    else:
        print("\n✓ All images processed successfully!")
        sys.exit(0)

if __name__ == "__main__":
    main()
