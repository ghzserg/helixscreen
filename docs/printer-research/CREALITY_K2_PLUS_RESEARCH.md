# Creality K2 Plus (and K2 Series) Research

**Date**: 2026-02-02 (updated 2026-02-07)
**Status**: Comprehensive research complete (architecture corrected)

## Executive Summary

The Creality K2 Plus is Creality's flagship CoreXY enclosed printer with 350mm³ build volume. It runs **Creality OS** (modified Klipper) and supports multi-material via **CFS (Creality Filament System)** - up to 16 colors with 4 daisy-chained units. **Moonraker is included in stock firmware** on port 4408. The K2 Plus likely uses the same Ingenic X2000E MIPS processor as the K1 series.

---

## 1. Hardware Specifications

### K2 Plus

| Specification | Details |
|---------------|---------|
| **Build Volume** | 350 x 350 x 350 mm |
| **Max Print Speed** | 600 mm/s |
| **Max Acceleration** | 30,000 mm/s² |
| **Nozzle Temperature** | Up to 350°C |
| **Bed Temperature** | Up to 120°C |
| **Chamber Temperature** | Up to 60°C (actively heated) |
| **Display** | 4.3" LCD touchscreen, 480 x 800 |
| **Storage** | 32 GB onboard |
| **Connectivity** | Dual-band WiFi, Ethernet |
| **Motion** | Step-servo motors, 32,768 microsteps/rev |
| **Weight** | 35 kg |
| **Price** | $1,499 (Combo with CFS) |

### Processor
**CORRECTED**: The K2 uses a DIFFERENT SoC than the K1 series.
- **Linux SoC**: Allwinner (likely A133/T800), ARM Cortex-A53 quad-core (NOT Ingenic MIPS like K1)
- **Evidence**: Tina Linux (Allwinner's distro), entware armv7sf installer, linux-sunxi.org lists "Creality T800" as A133 rebadge
- **RAM**: Unconfirmed, likely 512MB-1GB
- **Storage**: 32 GB
- **Motion MCU**: GD32F303RET6 (ARM Cortex-M3) - same as K1

### K2 Series Variants

| Model | Build Volume | Chamber Heater | Price |
|-------|-------------|----------------|-------|
| **K2** | 260³ mm | No | $549-699 |
| **K2 Pro** | 300³ mm | Yes (60°C) | $849-1,049 |
| **K2 Plus** | 350³ mm | Yes (60°C) | $1,199-1,499 |
| **K2 SE** | 220x215x245 mm | No | Lower cost |

---

## 2. Stock Firmware

### Operating System
- **Tina Linux 21.02-SNAPSHOT** (Buildroot-based)
- **Kernel**: Linux 4.x
- **Klipper**: Custom fork with proprietary extensions
- **Python**: 3.9

### Firmware Distribution
- Updates as `.img` files
- CFS firmware as `.bin` files
- OTA via Creality Cloud
- Extracted at [Guilouz/Creality-K2Plus-Extracted-Firmwares](https://github.com/Guilouz/Creality-K2Plus-Extracted-Firmwares)

---

## 3. Multi-Material System (CFS)

The **Creality Filament System** is Creality's answer to Bambu's AMS.

### CFS Specifications
| Feature | Value |
|---------|-------|
| **Spool Capacity** | 4 per unit |
| **Max Units** | 4 (daisy-chained) |
| **Max Colors** | 16 |
| **Communication** | RS-485 protocol |
| **Features** | Humidity/temp monitoring, RFID, auto-backup |

### CFS Communication
- RS-485 serial via dedicated cables
- Proprietary Python wrappers (compiled `.so` blobs):
  - `filament_rack_wrapper.cpython-39.so`
  - `serial_485_wrapper.cpython-39.so`
  - `box_wrapper.cpython-39.so`

### CFS G-code Macros
```
BOX_GO_TO_EXTRUDE_POS
BOX_NOZZLE_CLEAN
BOX_MOVE_TO_SAFE_POS
BOX_CUT_MATERIAL
T0, T1, T2, T3 (tool change)
```

---

## 4. Moonraker Availability

### Stock: YES (unlike K1 series)

| Service | Port |
|---------|------|
| Fluidd | 4408 |
| Mainsail | 4409 |
| Moonraker API | 4408 |

### Limitations
- Some features restricted in stock
- Full functionality after rooting
- CFS mapping unavailable via native OctoPrint/Klipper file transfer

---

## 5. Custom Firmware Options

### Official Open Source
**Repository**: [CrealityOfficial/K2_Series_Klipper](https://github.com/CrealityOfficial/K2_Series_Klipper)

Community criticism:
- No build documentation
- Outdated Tina Linux base (5 years old)
- CFS modules only as compiled blobs
- No integration instructions

### Community Projects

| Project | Status | Description |
|---------|--------|-------------|
| [k2-improvements](https://github.com/jamincollins/k2-improvements) | **ARCHIVED Aug 2025** | Full Klipper venv, Entware, Cartographer (148 stars, 42 forks) |
| [K2Plus-entware](https://github.com/vsevolod-volkov/K2Plus-entware) | Active? | Basic entware installer |
| [Fluidd-K2](https://github.com/BusPirateV5/Fluidd-K2) | Unknown | Customized Fluidd, WebRTC camera |
| [Mainsail-K2](https://github.com/Guilouz/Mainsail-K2) | Unknown | Lightweight Mainsail build |
| [k2_powerups](https://github.com/minimal3dp/k2_powerups) | Unknown | Improved leveling/start procedures |

**Note**: Guilouz Helper Script will NOT support K2 Plus - developer returned the printer (May 2025). GuppyScreen also does not support K2. See [K1 vs K2 Community Comparison](CREALITY_K1_VS_K2_COMMUNITY.md) for full analysis.

---

## 6. Klipper Configuration Structure

### Location
`/usr/share/klipper/config/`:
- `printer.cfg` - Main config
- `gcode_macro.cfg` - G-code macros

### Key Variables
```ini
z_safe_g28: 0.0
max_x_position: 350
max_y_position: 352
max_z_position: 320
```

### Notable Features
- `Qmode` - Quiet mode (2500mm/s² accel, 150mm/s velocity)
- Chamber heater control (`M141`, `M191`)
- Input shaper calibration
- `BOX_*` macros for CFS

### Closed-Source Components
- `box_wrapper.cpython-39.so`
- `filament_rack_wrapper.cpython-39.so`
- `serial_485_wrapper.cpython-39.so`
- Master server/display application

---

## 7. Display Interface

### Hardware
- **Size**: 4.3 inches
- **Resolution**: 480 x 800 pixels
- **Type**: LCD touchscreen
- **Interface**: `/dev/fb0`

### Software
- Stock UI likely LVGL-based (similar to K1)
- Direct framebuffer rendering
- No X11/Wayland

---

## 8. Root Access

### Enabling Root
1. Settings > "Root account information"
2. Read disclaimer, check acknowledgment
3. Wait 30 seconds, press "Ok"
4. SSH credentials displayed

### Credentials
- **Username**: `root`
- **Password**: `creality_2024`

```bash
ssh root@<printer-ip>
```

---

## 9. HelixScreen Compatibility Assessment

### Favorable Factors
1. **Moonraker included** - Stock firmware has Moonraker on port 4408
2. **Root access available** - SSH with default credentials
3. **Linux framebuffer** - Direct `/dev/fb0` access
4. **LVGL precedent** - Stock UI and GuppyScreen both use LVGL

### Challenges

| Challenge | Severity | Notes |
|-----------|----------|-------|
| Display Resolution | MEDIUM | 480x800 portrait orientation |
| CFS Integration | MEDIUM | Proprietary blobs for filament system |
| No Custom UI Precedent | MEDIUM | No GuppyScreen or other LVGL UI deployed on K2 |
| Declining Community | LOW-MEDIUM | Key maintainers (Guilouz, jamincollins) left |

### Implementation Path
1. **Confirm SoC** - Check `/proc/cpuinfo` (expected: Allwinner ARM)
2. **Build toolchain** - Standard ARM aarch64/armv7 cross-compilation (much easier than K1 MIPS)
3. **Port LVGL drivers** - Framebuffer `/dev/fb0`, evdev touch
4. **Moonraker integration** - Stock on port 4408
5. **CFS support** - Use G-code macros (T0-T3, BOX_*)

---

## 10. Community Resources

### Official
- **Forum**: [forum.creality.com/c/flagship-series/creality-flagship-k2-plus/81](https://forum.creality.com/c/flagship-series/creality-flagship-k2-plus/81)
- **Wiki**: [wiki.creality.com/en/k2-flagship-series/k2-plus](https://wiki.creality.com/en/k2-flagship-series/k2-plus)

### Discord
- [discord.com/invite/creality](https://discord.com/invite/creality)

### GitHub
| Repository | Purpose |
|------------|---------|
| [CrealityOfficial/K2_Series_Klipper](https://github.com/CrealityOfficial/K2_Series_Klipper) | Official Klipper fork |
| [Guilouz/Creality-K2Plus-Extracted-Firmwares](https://github.com/Guilouz/Creality-K2Plus-Extracted-Firmwares) | Extracted firmware |
| [ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen) | GuppyScreen (K2 not yet supported) |

---

## 11. Known Filesystem Paths

| What | Path |
|------|------|
| Klipper | `/usr/share/klipper/` |
| Klipper config | `/usr/share/klipper/config/printer.cfg` |
| G-code macros | `/usr/share/klipper/config/gcode_macro.cfg` |
| Moonraker | Stock, port **4408** (Fluidd) / **4409** (Mainsail) |
| Stock UI | `/usr/bin/display-server` |
| Init system | **procd** (OpenWrt-style, NOT SysV or systemd) |
| Service startup | `/etc/init.d/app` (starts display-server, master-server, app-server, etc.) |
| SSH credentials | `root` / `creality_2024` |

---

## 12. Display Details

| Attribute | Value |
|-----------|-------|
| Size | 4.3 inches |
| Panel resolution | 480 x 800 (native portrait panel) |
| **Displayed orientation** | **Appears landscape from product photos** - likely 800x480 after driver/software rotation |
| Touch type | **Capacitive** (Goodix GT9xx or TLSC6x controllers) |
| Touch modules | `gt9xxnew_ts.ko`, `tlsc6x.ko` (two variants for different HW revisions) |
| Framebuffer | `/dev/fb0` |
| G2D accelerator | `g2d_sunxi` loaded at boot (Allwinner hardware 2D accel, supports rotation) |
| Display control | `/sys/kernel/debug/dispdbg` IOCTLs |

**UNCONFIRMED**: Whether the framebuffer reports 800x480 (driver-rotated, ideal for us) or 480x800 (would need software rotation). Need `cat /sys/class/graphics/fb0/virtual_size` from actual hardware.

---

## 13. Open Questions (Need Hardware Access)

These can only be answered by someone with SSH access to a K2/K2 Plus:

```bash
# 1. Display - is it already rotated by the driver?
cat /sys/class/graphics/fb0/virtual_size
# Expected: "800,480" (great) or "480,800" (need sw rotation)

# 2. CPU architecture - what ARM variant?
uname -m
# Expected: "armv7l" or "aarch64"

# 3. SoC confirmation
cat /proc/cpuinfo
# Looking for: Allwinner, Cortex-A53, etc.

# 4. Libc - musl or glibc?
ldd --version 2>&1 || ls /lib/libc.so* /lib/ld-*
# Determines static linking strategy

# 5. Writable space - where to install HelixScreen?
df -h
mount
# Need a writable partition with space for our binary + assets

# 6. Framebuffer release - does killing display-server work cleanly?
killall display-server
cat /dev/urandom > /dev/fb0  # should see noise on screen

# 7. Touch device path
ls /dev/input/event*
cat /proc/bus/input/devices | grep -A 4 -i touch

# 8. Moonraker confirmation
curl -s http://localhost:4408/server/info | head
```

---

## 14. HelixScreen Build Target

### Status: Build target implemented (UNTESTED)

The K2 cross-compilation target was added as `PLATFORM_TARGET=k2`. It uses Bootlin's armv7-eabihf musl toolchain with fully static linking — same proven strategy as the K1 target but for ARM instead of MIPS.

### What's Done

| Component | Status | Details |
|-----------|--------|---------|
| `PLATFORM_TARGET=k2` in `mk/cross.mk` | **Done** | armv7 hard-float, musl static, fbdev/evdev |
| `docker/Dockerfile.k2` | **Done** | Bootlin armv7-eabihf musl toolchain |
| Deploy targets | **Done** | `deploy-k2`, `deploy-k2-fg`, `deploy-k2-bin`, `k2-test`, `k2-ssh` |
| Release packaging | **Done** | `release-k2`, `package-k2` |
| Update checker platform key | **Done** | `HELIX_PLATFORM_K2` → `"k2"` |
| Framebuffer backend | **Done** | Existing fbdev backend auto-detects resolution |
| Touch input | **Done** | Existing evdev auto-detection handles Goodix/TLSC |

### Build & Deploy

```bash
# Build
make k2-docker

# Deploy (SSH: root / creality_2024)
make deploy-k2 K2_HOST=192.168.1.100
make deploy-k2-fg K2_HOST=192.168.1.100   # foreground with debug output
make k2-ssh K2_HOST=192.168.1.100          # SSH into the printer
```

### Assumptions That May Be Wrong

These are educated guesses. When someone tests on real hardware, expect some of these to break:

| Assumption | Our guess | If wrong... |
|-----------|-----------|-------------|
| **ARM variant** | armv7 (32-bit userland) | Switch to aarch64 musl toolchain in Dockerfile |
| **C library** | musl (OpenWrt default) | Doesn't matter — we're fully static |
| **Framebuffer** | 800x480 (driver-rotated) | Need LVGL `lv_display_set_rotation()` + touch transform |
| **Deploy dir** | `/opt/helixscreen` | Change `K2_DEPLOY_DIR` in cross.mk or override at deploy time |
| **Stock UI process** | `display-server` | Check `ps` output, update the `killall` in deploy target |
| **Log command** | `logread -f` (OpenWrt) | Might be `tail -f /var/log/messages` or journalctl |
| **BusyBox (no rsync)** | Yes (tar/ssh deploy) | If rsync available, could use deploy-common instead |

### What Still Needs Work

| Component | Effort | Notes |
|-----------|--------|-------|
| Display rotation (if fb is 480x800) | Medium | LVGL `lv_display_set_rotation()` + touch coord transform |
| procd init script | Low | Different from K1's SysV init, but simple |
| CFS multi-material support | Medium | Use G-code macros (T0-T3, BOX_*) |
| Hardware validation | **Blocking** | Need someone with SSH access — see Section 13 |

---

## Conclusion

The K2 Plus is architecturally easier to target than K1 (ARM vs MIPS) with stock Moonraker (no community tools needed). The build target is implemented but **completely untested** — it needs someone with a K2 and SSH access to run the diagnostic commands in Section 13 and report back.

Key remaining unknowns:

1. **Framebuffer orientation** - determines if we need rotation support (the big question)
2. **ARM variant** - armv7 vs aarch64 determines if we need to change the toolchain
3. **Install location** - `/opt/helixscreen` is a guess
4. **CFS integration** - multi-material via existing G-code macros (T0-T3, BOX_*)
5. **Community ecosystem** - thin and declining, but stock Moonraker means less dependency on community tools

See also: [K1 vs K2 Community Comparison](CREALITY_K1_VS_K2_COMMUNITY.md)
