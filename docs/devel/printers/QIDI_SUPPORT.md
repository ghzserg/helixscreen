<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# QIDI Printer Support

HelixScreen supports QIDI's enclosed CoreXY printers as an open-source alternative to QIDI's proprietary touchscreen UI and FreeDi's closed-source LCD firmware. If your QIDI is running standard Moonraker -- whether through stock firmware, FreeDi, OpenQ1, or another community project -- HelixScreen can replace the built-in display interface.

## Supported Models

All supported QIDI models use MKSPI boards with ARM Cortex-A53 (aarch64) processors and 1GB RAM.

| Model | Display | Resolution | Status |
|-------|---------|------------|--------|
| X-Max 3 | 7" touch | 800x480 | Untested |
| X-Plus 3 | 5" touch | ~800x480 | Untested |
| Q1 Pro | 5" touch | ~800x480 | Untested |
| Plus 4 | 7" touch | 800x480 | Untested |
| Q2 | ? | ? | Untested |
| Q2 Pro | ? | ? | Untested |

## Installation

### Prerequisites

- A QIDI printer running Klipper with Moonraker accessible over the network
- SSH access to the printer
- FreeDi (recommended) or stock firmware with Moonraker enabled

### Using the Pi/aarch64 Binary

QIDI's Cortex-A53 processor is the same aarch64 architecture as the Raspberry Pi 4 and Pi 5. The standard Pi build of HelixScreen runs natively on QIDI hardware with no modifications.

```bash
# Build on a build server (or use a pre-built release)
make pi-docker

# Copy the binary to your QIDI printer
scp build-pi/bin/helix-screen root@<qidi-ip>:/usr/local/bin/

# SSH into the printer and run
ssh root@<qidi-ip>
helix-screen
```

For verbose output during first-time setup, add `-vv` for DEBUG-level logging:

```bash
helix-screen -vv
```

### Display Backend

HelixScreen auto-detects the best available display backend in this order: DRM, fbdev, SDL. QIDI hardware should work with either DRM or fbdev depending on the OS setup. No display configuration is needed -- HelixScreen picks the right backend automatically.

### Touch Input

HelixScreen uses libinput for touch input and should auto-detect `/dev/input/eventX` devices on QIDI hardware. If touch input doesn't work, check that input devices are present and accessible:

```bash
ls /dev/input/event*
```

Ensure the user running HelixScreen has read permissions on the event device. Running as root (common on QIDI printers) avoids permission issues.

## Auto-Detection

HelixScreen auto-detects QIDI printers using several heuristics:

- Hostname patterns
- Chamber heater presence
- MCU identification patterns
- Build volume dimensions
- QIDI-specific G-code macros (`M141`, `M191`, `CLEAR_NOZZLE`)

No manual printer configuration is needed in most cases. HelixScreen identifies your QIDI model and applies the correct settings automatically.

## Print Start Tracking

HelixScreen uses the `qidi` print start profile to track progress through your printer's start sequence. The profile recognizes QIDI's typical startup phases:

1. Homing
2. Bed heating
3. Nozzle cleaning (`CLEAR_NOZZLE`)
4. Z tilt adjust
5. Bed mesh calibration
6. Nozzle heating
7. Chamber heating
8. Print begins

The progress bar updates as each phase completes, so you can see exactly where your printer is in its startup routine.

## Known Limitations

- **Untested on real hardware** -- Detection heuristics and display rendering are based on documentation and community reports. Community testers are very welcome.
- **No chamber heater control UI** -- QIDI printers have heated chambers, but HelixScreen doesn't yet have a dedicated chamber temperature control panel.
- **FreeDi integration not automated** -- Manual binary deployment is required. There is no KIAUH or package manager integration for QIDI yet.
- **Stock QIDI firmware may have limited Moonraker** -- QIDI's modified Klipper and Moonraker builds may not expose all standard endpoints. FreeDi or mainline Klipper is recommended for the best experience.

## Community Testing

We need testers with QIDI hardware. If you can help:

1. Build or download the aarch64 binary
2. Deploy it to your QIDI printer
3. Report back: Does it start? Does the display render? Does touch work? Is your printer detected correctly?
4. File issues at the HelixScreen GitHub repository

Even a quick "it boots and shows the home screen" report is valuable. Testing on any of the supported models helps the whole community.

## Related Projects

- **[FreeDi](https://github.com/Phil1988/FreeDi)** -- Replaces QIDI's stock OS with Armbian and mainline Klipper. Recommended base OS for running HelixScreen on QIDI hardware.
- **[GuppyScreen](https://github.com/ballaswag/guppyscreen)** -- Another LVGL-based touchscreen display for Klipper printers.
- **[KlipperScreen](https://github.com/KlipperScreen/KlipperScreen)** -- Python/GTK-based display interface (typically requires an external monitor).
