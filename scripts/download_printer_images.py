#!/usr/bin/env python3

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Download and process printer images for wizard
Resizes all images to consistent dimensions for UI display
"""

import subprocess
import sys
from pathlib import Path

# Target dimensions (matching existing images)
TARGET_WIDTH = 750
TARGET_HEIGHT = 930

# Image URLs and filenames
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

    # Anycubic family
    ("https://imgs.search.brave.com/fc5wSwQyDx_d4m6n7h19JeIHjLzlJJEAKSDCBbMu2BE/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9tLm1l/ZGlhLWFtYXpvbi5j/b20vaW1hZ2VzL0kv/NDF0NTZFNUhyS0wu/anBn", "anycubic-kobra.png"),
    ("https://imgs.search.brave.com/-wLgDW8_t1cqW8Uxn9d8rZiqS9Kr6BveR4zk4kGP_yY/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly8zZHVz/cy5kZS93cC1jb250/ZW50L3VwbG9hZHMv/MjAyMi8wMi9Bbnlj/dWJpYy1WeXBlci0z/RC1EcnVja2VyLWth/dWZlbi1kZXV0c2No/bGFuZC0yLmpwZw", "anycubic-vyper.png"),
    ("https://imgs.search.brave.com/cQK5QQMxkDnjo1-SfZBCNg3S-BNu715Rdx-0tLO0zsc/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly93d3cu/M2RwcmludGVyc2Jh/eS5jb20vaW1hZ2Uv/Y2FjaGUvY2F0YWxv/Zy9hbnljdWJpYy9h/bnljdWJpYy1jaGly/b24tMi02NTB4ODAw/LndlYnA", "anycubic-chiron.png"),

    # Rat Rig family
    ("https://imgs.search.brave.com/sOJ7tSM4ckw6ioEk6_X91i5nGUtg-Qc9PPHyE7MCl_E/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9jZG4u/cHJvZC53ZWJzaXRl/LWZpbGVzLmNvbS82/NDY1ZmE5YjAxN2Fi/OWQ2MGM3Nzk3ZDcv/NjQ5NDAxZWE2M2I1/ZTk4NDQxYWMwMzNm/XzNfMV8wMC5qcGc", "ratrig-vcore3.png"),
    ("https://imgs.search.brave.com/lwdW9t9l6oIPTOjfvoypq6aydqn7YCuPghvsxYMgmVw/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9ibGFj/a2Zyb2cucGwvaW1h/Z2VzL3JhdHJpZy92/bWluaW9uL21pbmkv/NDAwcHhfMDdfMV8x/Mi5wbmc", "ratrig-vminion.png"),

    # FLSUN Delta
    ("https://imgs.search.brave.com/M162aUuhc0xBFMEhFkSLe8FXwlpUON6rWn5quCe1F48/rs:fit:860:0:0:0/g:ce/aHR0cHM6Ly9odHRw/Mi5tbHN0YXRpYy5j/b20vRF9RX05QXzJY/XzY1OTk5NC1NTEE1/MzU2NjcwODk1Ml8w/MjIwMjMtVi53ZWJw", "flsun-delta.png"),
]

def download_image(url, output_path):
    """Download image from URL using curl to bypass anti-hotlinking"""
    print(f"Downloading: {output_path.name}")
    try:
        cmd = [
            "curl", "-L",  # Follow redirects
            "-A", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",  # User agent
            "-o", str(output_path),
            url
        ]
        result = subprocess.run(cmd, check=True, capture_output=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"  ERROR downloading: {e.stderr.decode()}")
        return False

def resize_image(input_path, output_path):
    """Resize image using ImageMagick to target dimensions"""
    print(f"Resizing: {input_path.name} -> {output_path.name}")
    try:
        # Use magick convert with aspect ratio preservation and padding
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
    except FileNotFoundError:
        print("  ERROR: ImageMagick not found. Install with: brew install imagemagick")
        return False

def main():
    # Create output directory
    output_dir = Path("assets/images/printers")
    output_dir.mkdir(parents=True, exist_ok=True)

    temp_dir = Path("/tmp/printer_images")
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

        # Download
        if not download_image(url, temp_file):
            failed.append(filename)
            continue

        # Resize
        if not resize_image(temp_file, final_file):
            failed.append(filename)
            temp_file.unlink(missing_ok=True)
            continue

        # Clean up temp file
        temp_file.unlink(missing_ok=True)
        success_count += 1
        print(f"  ✓ Success: {filename}")

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
