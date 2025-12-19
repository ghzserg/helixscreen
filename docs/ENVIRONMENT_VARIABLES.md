# Environment Variables Reference

This document provides a comprehensive reference for all environment variables used in HelixScreen.

## Quick Reference

| Category | Count | Prefix |
|----------|-------|--------|
| [Display & Backend](#display--backend-configuration) | 7 | `HELIX_` |
| [G-Code Viewer](#g-code-viewer) | 3 | `HELIX_` |
| [Bed Mesh](#bed-mesh) | 1 | `HELIX_` |
| [Mock & Testing](#mock--testing) | 9 | `HELIX_MOCK_*` |
| [UI Automation](#ui-automation) | 3 | `HELIX_AUTO_*` |
| [Calibration](#calibration-auto-start) | 2 | `*_AUTO_START` |
| [Logging & Paths](#logging--data-paths) | 2 | Standard Unix |

---

## Display & Backend Configuration

These variables control how HelixScreen connects to displays and input devices.

### `HELIX_DISPLAY_BACKEND`

Override the automatic display backend detection.

| Property | Value |
|----------|-------|
| **Values** | `sdl`, `drm`, `fbdev` |
| **Default** | Auto-detect based on platform |
| **File** | `src/display_backend.cpp` |

```bash
# Force SDL backend (useful for debugging on embedded systems)
HELIX_DISPLAY_BACKEND=sdl ./build/bin/helix-screen

# Force DRM backend
HELIX_DISPLAY_BACKEND=drm ./build/bin/helix-screen
```

### `HELIX_DRM_DEVICE`

Specify which DRM device to use when multiple GPUs are present.

| Property | Value |
|----------|-------|
| **Values** | Device path (e.g., `/dev/dri/card0`, `/dev/dri/card1`) |
| **Default** | Auto-detect (first available) |
| **File** | `src/display_backend_drm.cpp` |

```bash
# Use secondary GPU
HELIX_DRM_DEVICE=/dev/dri/card1 ./build/bin/helix-screen
```

### `HELIX_TOUCH_DEVICE`

Override automatic touch input device detection.

| Property | Value |
|----------|-------|
| **Values** | Device path (e.g., `/dev/input/event0`) |
| **Default** | Auto-detect |
| **Files** | `src/display_backend_fbdev.cpp`, `src/display_backend_drm.cpp` |

```bash
# Specify touch device explicitly
HELIX_TOUCH_DEVICE=/dev/input/event2 ./build/bin/helix-screen
```

### `HELIX_BACKLIGHT_DEVICE`

Control backlight device path or disable backlight control entirely.

| Property | Value |
|----------|-------|
| **Values** | Device path, or `"none"` to disable |
| **Default** | Auto-detect |
| **File** | `src/backlight_backend.cpp` |

```bash
# Disable backlight control (e.g., for external displays)
HELIX_BACKLIGHT_DEVICE=none ./build/bin/helix-screen

# Use specific backlight device
HELIX_BACKLIGHT_DEVICE=/sys/class/backlight/backlight-lvds ./build/bin/helix-screen
```

### `HELIX_SDL_DISPLAY`

Select which monitor to use when running with SDL backend on multi-monitor systems.

| Property | Value |
|----------|-------|
| **Values** | Display index (`0`, `1`, `2`, ...) |
| **Default** | `0` (primary display) |
| **File** | `src/main.cpp` |

```bash
# Run on second monitor
HELIX_SDL_DISPLAY=1 ./build/bin/helix-screen
```

### `HELIX_SDL_XPOS` / `HELIX_SDL_YPOS`

Position the SDL window at exact screen coordinates.

| Property | Value |
|----------|-------|
| **Values** | Pixel coordinates (integers) |
| **Default** | Centered on selected display |
| **File** | `src/main.cpp` |

```bash
# Position window at specific coordinates
HELIX_SDL_XPOS=100 HELIX_SDL_YPOS=200 ./build/bin/helix-screen
```

---

## G-Code Viewer

### `HELIX_GCODE_MODE`

Force the G-code preview rendering mode.

| Property | Value |
|----------|-------|
| **Values** | `2D`, `3D` |
| **Default** | `2D` (TinyGL 3D renderer disabled by default) |
| **Files** | `src/ui_gcode_viewer.cpp`, `src/ui_panel_print_status.cpp`, `src/ui_panel_gcode_test.cpp` |

```bash
# Enable 3D G-code preview (requires TinyGL/OpenGL ES)
HELIX_GCODE_MODE=3D ./build/bin/helix-screen

# Force 2D layer view
HELIX_GCODE_MODE=2D ./build/bin/helix-screen
```

**Note:** 3D mode requires the build to include TinyGL or OpenGL ES support. On platforms without GPU acceleration, 2D mode is recommended for performance.

### `HELIX_FORCE_GCODE_MEMORY_FAIL`

Force the G-code memory safety check to fail, simulating a memory-constrained device like AD5M. Useful for testing thumbnail fallback behavior without deploying to embedded hardware.

| Property | Value |
|----------|-------|
| **Values** | `1` (force failure), unset (normal behavior) |
| **Default** | Unset (normal memory checking) |
| **File** | `src/memory_utils.cpp` |

```bash
# Force memory check to fail - viewer falls back to thumbnail mode
HELIX_FORCE_GCODE_MEMORY_FAIL=1 ./build/bin/helix-screen --test -p print-status -vv
```

**Use case:** Testing that the thumbnail displays immediately when G-code rendering is unavailable, without needing to deploy to memory-constrained hardware.

### `HELIX_GCODE_STREAMING`

Control G-code streaming mode for memory-efficient loading of large files. Streaming loads layers on-demand instead of the entire file at once, enabling 10MB+ G-code files on memory-constrained devices like AD5M.

| Property | Value |
|----------|-------|
| **Values** | `on` (always stream), `off` (always full load), `auto` (calculate based on RAM) |
| **Default** | `auto` |
| **Config** | `gcode_viewer.streaming_mode` in `helixconfig.json` |
| **File** | `src/gcode_streaming_config.cpp` |

**Priority order:**
1. Environment variable (highest) - for testing/debugging
2. Config file setting - for user preference
3. Auto-detection based on available RAM

```bash
# Force streaming mode (useful for testing streaming behavior)
HELIX_GCODE_STREAMING=on ./build/bin/helix-screen --test -p print-status -vv

# Force full load mode (may crash on large files with low RAM!)
HELIX_GCODE_STREAMING=off ./build/bin/helix-screen --test --gcode-file large.gcode -vv

# Use auto-detection (default)
HELIX_GCODE_STREAMING=auto ./build/bin/helix-screen --test -p print-status -vv
```

**Auto-detection thresholds** (at 40% RAM threshold, 15x expansion factor):
| Available RAM | Streaming kicks in at |
|---------------|----------------------|
| 47 MB (AD5M)  | ~1.25 MB |
| 256 MB        | ~6.8 MB |
| 1 GB          | ~27 MB |
| 4 GB          | ~107 MB |

**Related config options:**
- `gcode_viewer.streaming_mode`: `"auto"`, `"on"`, or `"off"`
- `gcode_viewer.streaming_threshold_percent`: 1-90 (default 40)

---

## Bed Mesh

### `HELIX_BED_MESH_2D`

Force the bed mesh visualization to use 2D heatmap mode instead of 3D surface rendering.

| Property | Value |
|----------|-------|
| **Values** | `1` (enable), unset (disable) |
| **Default** | Off (3D surface when available) |
| **File** | `src/ui_bed_mesh.cpp` |

```bash
# Force 2D heatmap visualization
HELIX_BED_MESH_2D=1 ./build/bin/helix-screen
```

---

## Mock & Testing

These variables control the mock printer simulation, useful for development and testing without a real printer.

### `HELIX_AMS_GATES`

Set the number of filament gates in the mock AMS (Automatic Material System).

| Property | Value |
|----------|-------|
| **Values** | `1` to `16` |
| **Default** | `4` |
| **File** | `src/main.cpp` |

```bash
# Simulate 8-slot AMS
HELIX_AMS_GATES=8 ./build/bin/helix-screen --test

# Simulate 16-slot MMU
HELIX_AMS_GATES=16 ./build/bin/helix-screen --test
```

### `HELIX_MOCK_AMS_TYPE`

Set the type of AMS simulation (standard multi-slot vs toolchanger).

| Property | Value |
|----------|-------|
| **Values** | `"toolchanger"`, `"tool_changer"`, or `"tc"` |
| **Default** | Standard AMS (multi-slot) |
| **File** | `src/ams_backend.cpp` |

```bash
# Simulate a toolchanger printer
HELIX_MOCK_AMS_TYPE=toolchanger ./build/bin/helix-screen --test
```

### `HELIX_MOCK_DRYER`

Enable filament dryer simulation in mock mode.

| Property | Value |
|----------|-------|
| **Values** | `1` or `true` |
| **Default** | Disabled |
| **File** | `src/ams_backend.cpp` |

```bash
# Enable mock dryer
HELIX_MOCK_DRYER=1 ./build/bin/helix-screen --test
```

### `HELIX_MOCK_DRYER_SPEED`

Speed multiplier for dryer simulation (for faster testing).

| Property | Value |
|----------|-------|
| **Values** | Integer multiplier (e.g., `2` = 2x speed) |
| **Default** | `1` (real-time) |
| **File** | `src/ams_backend_mock.cpp` |

```bash
# Run dryer simulation at 10x speed
HELIX_MOCK_DRYER=1 HELIX_MOCK_DRYER_SPEED=10 ./build/bin/helix-screen --test
```

### `HELIX_MOCK_SPOOLMAN`

Enable or disable mock Spoolman integration. When disabled, `get_spoolman_status()` reports as disconnected.

| Property | Value |
|----------|-------|
| **Values** | `0` or `off` to disable; any other value keeps enabled |
| **Default** | Enabled (mock Spoolman always connected in test mode) |
| **File** | `src/main.cpp` |

```bash
# Disable mock Spoolman to test "no Spoolman" scenarios
HELIX_MOCK_SPOOLMAN=0 ./build/bin/helix-screen --test
```

### `HELIX_MOCK_AMS_REALISTIC`

Enable realistic multi-phase AMS operations (load/unload with tip forming, purging, etc.).

| Property | Value |
|----------|-------|
| **Values** | `1` or `true` |
| **Default** | Disabled (instant operations) |
| **File** | `src/ams_backend.cpp` |

```bash
# Enable realistic AMS timing and phases
HELIX_MOCK_AMS_REALISTIC=1 ./build/bin/helix-screen --test
```

### `HELIX_MOCK_FILAMENT_SENSORS`

Configure custom filament sensor configurations for testing.

| Property | Value |
|----------|-------|
| **Values** | Comma-separated `type:name` pairs, or `"none"` |
| **Default** | Single runout switch sensor |
| **File** | `src/moonraker_client_mock.cpp` |

**Sensor Types:**
- `switch` - Simple on/off runout switch
- `motion` - Motion-based encoder sensor

```bash
# Multiple sensors
HELIX_MOCK_FILAMENT_SENSORS="switch:fsensor,motion:encoder" ./build/bin/helix-screen --test

# No sensors
HELIX_MOCK_FILAMENT_SENSORS=none ./build/bin/helix-screen --test
```

### `HELIX_MOCK_FILAMENT_STATE`

Set the initial state of filament sensors.

| Property | Value |
|----------|-------|
| **Values** | `sensor_name:state` (e.g., `fsensor:empty`, `fsensor:detected`) |
| **Default** | Detected |
| **File** | `src/moonraker_client_mock.cpp` |

```bash
# Start with empty filament sensor
HELIX_MOCK_FILAMENT_STATE="fsensor:empty" ./build/bin/helix-screen --test
```

### `MOCK_EMPTY_POWER`

Return an empty power devices list from mock Moonraker API.

| Property | Value |
|----------|-------|
| **Values** | Any value (presence enables) |
| **Default** | Populated power device list |
| **File** | `src/moonraker_api_mock.cpp` |

```bash
# Simulate printer with no controllable power devices
MOCK_EMPTY_POWER=1 ./build/bin/helix-screen --test
```

---

## UI Automation

These variables enable automated testing and CI/CD workflows.

### `HELIX_AUTO_QUIT_MS`

Automatically quit the application after a specified duration.

| Property | Value |
|----------|-------|
| **Values** | `100` to `3600000` (milliseconds) |
| **Default** | No timeout (run indefinitely) |
| **File** | `src/main.cpp` |

```bash
# Quit after 5 seconds
HELIX_AUTO_QUIT_MS=5000 ./build/bin/helix-screen --test

# CI test run (3 second timeout)
HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p home
```

### `HELIX_AUTO_SCREENSHOT`

Automatically capture a screenshot before quitting (use with `HELIX_AUTO_QUIT_MS`).

| Property | Value |
|----------|-------|
| **Values** | `1` (enable) |
| **Default** | Disabled |
| **File** | `src/main.cpp` |

```bash
# Automated screenshot capture
HELIX_AUTO_SCREENSHOT=1 HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p motion
```

### `HELIX_BENCHMARK`

Enable frame counting and FPS reporting for performance testing.

| Property | Value |
|----------|-------|
| **Values** | Any value (presence enables) |
| **Default** | Disabled |
| **File** | `src/main.cpp` |

```bash
# Run performance benchmark
HELIX_BENCHMARK=1 HELIX_AUTO_QUIT_MS=10000 ./build/bin/helix-screen --test
```

---

## Calibration Auto-Start

These variables auto-start calibration procedures for testing purposes.

### `INPUT_SHAPER_AUTO_START`

Auto-start X-axis input shaper calibration when the panel loads.

| Property | Value |
|----------|-------|
| **Values** | Any value (presence enables) |
| **Default** | Disabled |
| **File** | `src/ui_panel_input_shaper.cpp` |

```bash
# Test input shaper panel with auto-start
INPUT_SHAPER_AUTO_START=1 ./build/bin/helix-screen --test -p input-shaper
```

### `SCREWS_AUTO_START`

Auto-start bed screw probing when the screws tilt panel loads.

| Property | Value |
|----------|-------|
| **Values** | Any value (presence enables) |
| **Default** | Disabled |
| **File** | `src/ui_panel_screws_tilt.cpp` |

```bash
# Test screws panel with auto-start
SCREWS_AUTO_START=1 ./build/bin/helix-screen --test -p screws-tilt
```

---

## Logging & Data Paths

### `XDG_DATA_HOME`

XDG base directory specification for application data storage.

| Property | Value |
|----------|-------|
| **Values** | Directory path |
| **Default** | `~/.local/share` |
| **File** | `src/logging_init.cpp` |

HelixScreen stores logs and data in `$XDG_DATA_HOME/helixscreen/`.

### `HOME`

User home directory (standard Unix variable).

| Property | Value |
|----------|-------|
| **Values** | Directory path |
| **Default** | (from system) |
| **File** | `src/logging_init.cpp` |

Used as fallback when `XDG_DATA_HOME` is not set.

---

## Build-Time Variables

These are set during compilation via the Makefile system.

| Variable | Purpose | Source |
|----------|---------|--------|
| `HELIX_VERSION` | Version string | `VERSION.txt` |
| `HELIX_VERSION_MAJOR` | Major version number | Parsed from version |
| `HELIX_VERSION_MINOR` | Minor version number | Parsed from version |
| `HELIX_VERSION_PATCH` | Patch version number | Parsed from version |
| `HELIX_GIT_HASH` | Git commit hash (short) | `git describe` |

---

## Preprocessor Flags

These are set at compile time to enable/disable features:

| Flag | Purpose |
|------|---------|
| `HELIX_DISPLAY_SDL` | Enable SDL2 display backend |
| `HELIX_DISPLAY_DRM` | Enable DRM display backend |
| `HELIX_DISPLAY_FBDEV` | Enable framebuffer display backend |
| `HELIX_ENABLE_OPENGLES` | Enable OpenGL ES support for 3D rendering |
| `HELIX_HAS_SYSTEMD` | Enable systemd integration |

---

## Shell Script Variables

### Screenshot Script (`scripts/screenshot.sh`)

| Variable | Purpose | Default |
|----------|---------|---------|
| `HELIX_SCREENSHOT_DISPLAY` | Display number for multi-monitor | `1` |
| `HELIX_SCREENSHOT_OPEN` | Auto-open in Preview (macOS) | Disabled |

```bash
# Take screenshot on display 0 and open it
HELIX_SCREENSHOT_DISPLAY=0 HELIX_SCREENSHOT_OPEN=1 ./scripts/screenshot.sh helix-screen test-output
```

---

## Systemd Service Configuration

When running as a systemd service, environment variables are set in the service file:

```ini
# /etc/systemd/system/helixscreen.service
[Service]
Environment="HELIX_SCREEN_WIDTH=800"
Environment="HELIX_SCREEN_HEIGHT=480"
Environment="HELIX_DISPLAY_BACKEND=drm"
```

See `docs/user/CONFIGURATION.md` for systemd deployment details.

---

## Common Usage Patterns

### Development Testing

```bash
# Basic mock testing
./build/bin/helix-screen --test -vv

# Test specific panel with verbose logging
./build/bin/helix-screen --test -p motion -vv

# Multi-slot AMS testing
HELIX_AMS_GATES=8 ./build/bin/helix-screen --test -p filament
```

### CI/CD Screenshots

```bash
# Automated panel screenshots
HELIX_AUTO_SCREENSHOT=1 HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p home
HELIX_AUTO_SCREENSHOT=1 HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p motion
HELIX_AUTO_SCREENSHOT=1 HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p settings
```

### Performance Benchmarking

```bash
# Run 10-second benchmark
HELIX_BENCHMARK=1 HELIX_AUTO_QUIT_MS=10000 ./build/bin/helix-screen --test -p home -v
```

### Hardware Override (Embedded)

```bash
# Override all device paths
HELIX_DISPLAY_BACKEND=drm \
HELIX_DRM_DEVICE=/dev/dri/card1 \
HELIX_TOUCH_DEVICE=/dev/input/event2 \
HELIX_BACKLIGHT_DEVICE=/sys/class/backlight/lcd \
./build/bin/helix-screen
```

---

## See Also

- [Development Guide](DEVELOPMENT.md) - Daily development workflow
- [Build System](BUILD_SYSTEM.md) - Build configuration
- [Testing Guide](TESTING.md) - Test infrastructure
- [User Configuration](user/CONFIGURATION.md) - End-user setup
