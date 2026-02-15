<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Creality K2 Series Support

HelixScreen has a cross-compilation target for the Creality K2 series of enclosed CoreXY printers. The K2 series runs Klipper with stock Moonraker, making it a natural fit for HelixScreen. However, the build target is **completely untested** on real hardware and needs community validation.

## Supported Models

All K2 models use Allwinner ARM Cortex-A53 (likely A133/T800) processors running Tina Linux (OpenWrt-based).

| Model | Build Volume | Display | Chamber Heater | CFS | Status |
|-------|-------------|---------|----------------|-----|--------|
| K2 | 260 mm cubed | 4.3" 480x800 | No | Optional | Untested |
| K2 Pro | 300 mm cubed | 4.3" 480x800 | Yes (60C) | Optional | Untested |
| K2 Plus | 350 mm cubed | 4.3" 480x800 | Yes (60C) | Optional | Untested |
| K2 SE | 220x215x245 mm | Unknown | No | Unknown | Untested |

## Hardware

| Spec | Value |
|------|-------|
| SoC | Allwinner A133/T800 (ARM Cortex-A53, quad-core) |
| Display | 4.3" touch, 480x800 (portrait panel, likely 800x480 after driver rotation) |
| Touch | Capacitive (Goodix GT9xx or TLSC6x controllers) |
| RAM | Unconfirmed, likely 512MB-1GB |
| Storage | 32 GB |
| OS | Tina Linux 21.02-SNAPSHOT (OpenWrt/Buildroot-based) |
| Init System | procd (OpenWrt-style, NOT systemd) |
| Moonraker | Stock, port 4408 (Fluidd) / 4409 (Mainsail) |
| Multi-Material | CFS (Creality Filament System), RS-485, up to 16 colors |
| SSH | `root` / `creality_2024` (via settings menu) |

### CFS (Creality Filament System)

The K2 series supports the CFS multi-material system:
- 4 spools per unit, up to 4 daisy-chained units (16 colors)
- RS-485 serial communication
- RFID filament identification, humidity/temperature monitoring
- G-code macros: `T0`-`T3` (tool change), `BOX_GO_TO_EXTRUDE_POS`, `BOX_NOZZLE_CLEAN`, `BOX_CUT_MATERIAL`
- **Caveat**: CFS communication relies on closed-source Python `.so` blobs (`filament_rack_wrapper`, `serial_485_wrapper`, `box_wrapper`)

## Cross-Compilation

The K2 target uses Bootlin's armv7-eabihf musl toolchain with fully static linking. We target armv7 (32-bit) despite the A53 being aarch64-capable because Tina Linux commonly uses 32-bit userland and entware packages for K2 are armv7sf.

### Build via Docker (Recommended)

```bash
# Build the Docker toolchain and cross-compile (first time only — cached after)
make k2-docker
```

The Docker image (`docker/Dockerfile.k2`) downloads [Bootlin's armv7-eabihf musl toolchain](https://toolchains.bootlin.com/) (stable-2024.02-1).

### Build Directly (Requires Toolchain)

```bash
make PLATFORM_TARGET=k2 -j
```

### Build Configuration

| Setting | Value |
|---------|-------|
| Architecture | armv7-a (hard-float, NEON VFPv4) |
| Toolchain | `arm-buildroot-linux-musleabihf-gcc` (Bootlin musl) |
| Linking | Fully static (musl) |
| Display backend | fbdev (`/dev/fb0`) |
| Input | evdev (auto-detected) |
| SSL | Disabled (Moonraker is local on port 4408) |
| TinyGL 3D | Disabled (untested) |
| Optimization | `-Os` with LTO (size-optimized) |
| Platform define | `HELIX_PLATFORM_K2` |

### CI/Release Status

The K2 target **is included** in the GitHub Actions release pipeline (`.github/workflows/release.yml`). Release artifacts are built automatically:

```bash
# Manual packaging
make package-k2
```

## Installation

### Prerequisites

- A Creality K2, K2 Pro, or K2 Plus printer
- Root access enabled (Settings > "Root account information" > acknowledge disclaimer > wait 30 seconds > press "Ok")
- SSH access: `root@<printer-ip>` (password: `creality_2024`)

### Deploy

```bash
# Full deploy (binary + assets + config)
make deploy-k2 K2_HOST=192.168.1.xxx

# Deploy and run in foreground with debug logging
make deploy-k2-fg K2_HOST=192.168.1.xxx

# Deploy binary only (fast iteration)
make deploy-k2-bin K2_HOST=192.168.1.xxx

# SSH into the printer
make k2-ssh K2_HOST=192.168.1.xxx

# Full build + deploy + run cycle
make k2-test K2_HOST=192.168.1.xxx
```

Default deploy directory is `/opt/helixscreen` (override with `K2_DEPLOY_DIR`). Default SSH credentials are `root`/`creality_2024` (override with `K2_USER`/`K2_PASS`).

**Note**: The K2 uses BusyBox (OpenWrt), so deployment uses tar/ssh transfer instead of rsync.

### Display Backend

HelixScreen renders directly to `/dev/fb0`. The stock display UI (`display-server`) must be stopped to release the framebuffer. The deploy targets handle this automatically.

**Unconfirmed**: Whether the framebuffer reports 800x480 (driver-rotated, ideal) or 480x800 (would need software rotation via `lv_display_set_rotation()` plus touch coordinate transform). Run `cat /sys/class/graphics/fb0/virtual_size` on the printer to check.

### Touch Input

HelixScreen uses evdev and should auto-detect the Goodix GT9xx or TLSC6x capacitive touch controller. Ensure the user running HelixScreen has read permissions on `/dev/input/event*`. Running as root (default) avoids permission issues.

## Auto-Detection

HelixScreen auto-detects K2 printers using heuristics from `config/printer_database.json`:

| Heuristic | Confidence | Description |
|-----------|------------|-------------|
| Hostname `k2` | 85 | Hostname contains "k2" |
| `multi_material` object | 80 | Multi-material system (CFS) |
| `chamber_temp` sensor | 70 | Chamber temperature sensor |
| Hostname `creality` | 60 | Hostname contains "creality" |
| CoreXY kinematics | 40 | CoreXY motion system |

Detection confidence is lower than other platforms because the K2's Moonraker objects are relatively generic. The strongest signal is the hostname, which typically defaults to a Creality-branded string.

## Known Limitations and Open Questions

### Untested -- Needs Hardware Validation

The entire K2 target is built from research, not hardware testing. These assumptions may be wrong:

| Assumption | Our Guess | If Wrong... |
|-----------|-----------|-------------|
| ARM variant | armv7 (32-bit userland) | Switch to aarch64 musl toolchain in Dockerfile |
| C library | musl (OpenWrt default) | Does not matter -- binary is fully static |
| Framebuffer | 800x480 (driver-rotated) | Need `lv_display_set_rotation()` + touch transform |
| Deploy directory | `/opt/helixscreen` | Override `K2_DEPLOY_DIR` at deploy time |
| Stock UI process | `display-server` | Check `ps` output, update deploy targets |
| Log command | `logread -f` (OpenWrt) | Might be `tail -f /var/log/messages` |
| BusyBox (no rsync) | Yes | If rsync available, could simplify deployment |

### Diagnostic Commands

Run these on a K2 with SSH access to validate assumptions:

```bash
# Display orientation -- is it already rotated by the driver?
cat /sys/class/graphics/fb0/virtual_size
# Expected: "800,480" (ideal) or "480,800" (need sw rotation)

# CPU architecture
uname -m
# Expected: "armv7l" or "aarch64"

# SoC confirmation
cat /proc/cpuinfo
# Looking for: Allwinner, Cortex-A53

# Libc -- musl or glibc?
ldd --version 2>&1 || ls /lib/libc.so* /lib/ld-*

# Writable space for installation
df -h

# Framebuffer release -- does killing display-server work?
killall display-server

# Touch device path
ls /dev/input/event*
cat /proc/bus/input/devices | grep -A 4 -i touch

# Moonraker confirmation
curl -s http://localhost:4408/server/info | head
```

**Note:** No K2-specific print start profile exists yet. The default profile will be used.

### Other Limitations

- **No platform hooks script** -- Unlike the Snapmaker U1 and AD5M targets, the K2 does not yet have a `hooks-k2.sh` platform hooks script. The deploy targets handle stock UI shutdown inline.
- **CFS integration incomplete** -- Multi-material works via G-code macros (`T0`-`T3`, `BOX_*`), but HelixScreen does not have dedicated CFS slot visualization or filament mapping UI. The CFS communication blobs are closed-source.
- **Display orientation unknown** -- If the framebuffer is 480x800 (portrait), HelixScreen will need software rotation support, which is not yet implemented for this target.
- **procd init system** -- The K2 uses OpenWrt's procd instead of systemd. No HelixScreen init script exists for procd yet.
- **Declining community ecosystem** -- Key community projects (k2-improvements, Guilouz helper script) have been archived or abandoned. HelixScreen would be the first custom UI deployed on K2 hardware. See the [K1 vs K2 community comparison](printer-research/CREALITY_K1_VS_K2_COMMUNITY.md) for full analysis.
- **No chamber heater control UI** -- K2 Pro and K2 Plus have heated chambers (`M141`, `M191`), but HelixScreen does not yet have a dedicated chamber temperature panel.

## Community Testing

We need testers with K2 hardware. If you can help:

1. Enable root access (Settings > "Root account information")
2. Build the binary: `make k2-docker`
3. Run the diagnostic commands above and share the output
4. Deploy: `make deploy-k2-fg K2_HOST=<ip>` (runs in foreground with debug logging)
5. Report back: Does it start? Does the display render? Does touch work? Is your printer detected? What orientation is the framebuffer?
6. File issues at the HelixScreen GitHub repository

Even running the diagnostic commands without deploying HelixScreen is valuable -- it validates the assumptions the build target is based on.

## Related Resources

- **[CrealityOfficial/K2_Series_Klipper](https://github.com/CrealityOfficial/K2_Series_Klipper)** -- Creality's official (incomplete) Klipper fork
- **[Guilouz/Creality-K2Plus-Extracted-Firmwares](https://github.com/Guilouz/Creality-K2Plus-Extracted-Firmwares)** -- Extracted stock firmware images
- **[K2 Plus Research](printer-research/CREALITY_K2_PLUS_RESEARCH.md)** -- Detailed hardware and software research
- **[K1 vs K2 Community Comparison](printer-research/CREALITY_K1_VS_K2_COMMUNITY.md)** -- Analysis of community ecosystem differences
- **[Creality Wiki](https://wiki.creality.com/en/k2-flagship-series/k2-plus)** -- Official K2 Plus documentation
- **[Creality Forum](https://forum.creality.com/c/flagship-series/creality-flagship-k2-plus/81)** -- Official K2 Plus community forum
