# KlipperScreen Competitive Research

> Last updated: 2026-02-11 | Source: GitHub `KlipperScreen/KlipperScreen`

## Overview

KlipperScreen is a Python/GTK3 touchscreen interface for Klipper 3D printers. It communicates with Klipper via Moonraker's WebSocket and REST APIs.

| Attribute | Detail |
|-----------|--------|
| **Language** | Python 3 |
| **GUI Framework** | GTK3 (via PyGObject) |
| **Communication** | Moonraker WebSocket (JSON-RPC) + REST API |
| **Display Servers** | X11, Wayland (Cage), Android (adb) |
| **Threading** | WebSocket on separate thread, `GLib.idle_add()` for thread-safe UI updates |
| **Config System** | ConfigParser-based, hierarchical merging (defaults → user → auto-generated) |
| **Theming** | CSS-based, multiple themes, runtime switching, font scaling |
| **Navigation** | Stack-based panel system with action bar (back/home/estop) |
| **Panels** | 35 total |
| **Custom Widgets** | 11 (heatergraph, bedmap, objectmap, keyboard, keypad, lockscreen, etc.) |
| **Languages** | 25+ via Weblate |
| **License** | GPL-3.0 |

### Architecture

```
KlipperScreen (Gtk.Window)
├── KlippyWebsocket ─── Moonraker (JSON-RPC, separate thread)
├── KlippyRest ───────── Moonraker REST API (SSL/TLS, API key auth)
├── KlippyGcodes ─────── G-code command builder
├── Printer ──────────── State model (devices, 1200-sample temp history)
├── KlipperScreenConfig ─ Hierarchical config (ConfigParser)
├── Panel Stack ──────── 35 lazy-loaded panels
│   └── base_panel ───── Action bar, titlebar (time/battery/temp)
├── Widgets ──────────── 11 custom (keyboard, heatergraph, bedmap, etc.)
└── Theme Engine ─────── CSS provider, font scaling, color classes
```

**State Machine:** disconnected → startup → ready/printing/paused → shutdown/error

---

## Complete Feature Inventory

### Core Operations (6 panels)

| Panel | Purpose | Key Features |
|-------|---------|--------------|
| **job_status** | Print monitoring | Position (X/Y/Z), filament usage, speed/flow factors, Z-offset, layer info, acceleration, multiple time estimations |
| **move** | Head control | XYZ movement (0.1–50mm steps), home all, disable motors |
| **temperature** | Thermal management | All heaters/sensors, preheat profiles (PLA/ABS/PETG/FLEX), temperature graph with 1200-sample history |
| **extrude** | Filament control | Extrude/retract (5–25mm, 1–25mm/s), multi-extruder, load/unload macros |
| **fine_tune** | Live print adjustments | Z-babystep (0.01–5mm), speed factor %, extrusion factor % |
| **console** | G-code terminal | Command input, color-coded output, auto-scroll, history, temp message filtering |

### Calibration & Leveling (6 panels)

| Panel | Purpose | Key Features |
|-------|---------|--------------|
| **bed_level** | Manual leveling | Screw tilt adjust with visual feedback (CW/CCW turn indicators), clickable screw positions |
| **bed_mesh** | Mesh calibration | Create/load/clear profiles, 2D mesh visualization, auto-calibrate trigger |
| **zcalibrate** | Z-offset | Interactive Z adjustment, nozzle probing, offset storage |
| **pressure_advance** | PA tuning | Coefficient (0–1mm), smooth time (0–0.2s) |
| **retraction** | Firmware retraction | Length, speed, unretract extra length/speed |
| **input_shaper** | Resonance comp | ADXL345 auto-cal, manual X/Y frequency (0–133Hz), shaper type selection (zv/mzv/zvd/ei/2hump/3hump) |

### Hardware Control (5 panels)

| Panel | Purpose | Key Features |
|-------|---------|--------------|
| **fan** | Fan speed | fan, fan_generic, controller_fan, heater_fan; 0–100% |
| **led** | LED control | RGB/RGBW color picker, presets, per-LED addressing, brightness |
| **power** | Power devices | Toggle GPIO/TPLink/Tasmota/Shelly devices, status display, locked_while_printing |
| **limits** | Printer limits | Max accel, max velocity, min cruise ratio |
| **pins** | Output pins | Generic output pins, PWM tools, on/off toggles |

### File & Print Management (2 panels)

| Panel | Purpose | Key Features |
|-------|---------|--------------|
| **gcodes** | File browser | Sort by name/date/size, thumbnails, rename, delete, search |
| **exclude** | Object exclusion | Exclude/include objects mid-print, polygon visualization on object map |

### System & Configuration (8 panels)

| Panel | Purpose | Key Features |
|-------|---------|--------------|
| **gcode_macros** | Macro launcher | List all macros, parameter input, descriptions, sorting |
| **network** | WiFi/Network | NetworkManager (DBus), AP scanning, WPA-EAP enterprise support |
| **camera** | Webcam | Multi-camera, MPV player, MJPEG/WebRTC, flip/rotation |
| **settings** | App settings | Theme, language, display options |
| **system** | System info | CPU/RAM/disk stats, uptime, Klipper/Moonraker versions, service restart |
| **notifications** | Notification log | Timestamped history (info/warning/error), clear log |
| **updater** | Software updates | Per-component updates (Klipper/Moonraker/KlipperScreen), version tracking |
| **spoolman** | Filament tracking | Spoolman API, spool metadata, vendor info, filament type, weight/length |

### Navigation & Utility (8 panels)

| Panel | Purpose |
|-------|---------|
| **main_menu** | Jinja2-templated primary nav, conditional display based on printer config |
| **menu** | Generic menu panel |
| **printer_select** | Multi-printer switching, connect/disconnect |
| **shutdown** | Shutdown/reboot system or printer |
| **splash_screen** | Startup/connection screen |
| **base_panel** | Action bar (back/home/estop/shutdown), titlebar (time/battery/temp) |
| **lockscreen** | Screen lock/authentication |
| **example** | Template for custom panels |

---

## 18-Month Development Analysis (Aug 2024 – Feb 2026)

### Summary

| Metric | Value |
|--------|-------|
| Total commits (non-merge) | 165 |
| Active contributors | 26 |
| Commit frequency | ~9/month |
| Lead maintainer | Alfredo Monclus (109 commits, 66%) |
| Translation bot | Weblate (25 commits, 15%) |

### Work Distribution

| Category | Commits | % | Notes |
|----------|---------|---|-------|
| Bug Fixes | 33 | 20% | UI issues, state management, edge cases |
| Translations | 25 | 15% | Weblate automated, ~25 languages |
| Features | 16 | 10% | New panels and capabilities |
| Refactoring | 14 | 8% | Cleanup and optimization |
| Chores | 14 | 8% | Deps, changelog, housekeeping |
| Documentation | 7 | 4% | Troubleshooting, config guides |
| Other | 41 | 25% | Reverts, type fixes, smaller commits |

### Major Additions (Last 18 Months)

**UI/UX:**
- Lockscreen feature (#1490)
- Battery status indicator (#1457)
- Keyboard widget caching (#1610) and icon/pixbuf caching (#1609)
- Enhanced keypad — float support, auto-alpha switching, smaller landscape mode
- Date/time in system panel (#1584)
- Cursor toggle

**Functionality:**
- Moonraker route prefix support (#1464) — better multi-instance setups
- WPA-EAP WiFi support (#1449) — enterprise networks
- Save Z feature (#1545) — print recovery
- Power device association on shutdown
- Bed mesh/leveling automation improvements
- Spoolman comment support

### Bug Fix Focus Areas

| Area | Fixes | Examples |
|------|-------|---------|
| Display/Rendering | ~8 | Vertical mode sizing, heatergraph None data, blanking/DPMS, RTL flip |
| State Management | ~7 | Job status animation timeout, filament sensor toggle persistence, bed level updates |
| Wayland/Platform | ~5 | Keyboard popup in Cage, unsupported commands, mouse click events, focus workaround |
| Network | ~4 | WiFi SSID invalid UTF-8, BSSID handling, security type detection |
| Config/Hardware | ~4 | Screw rotation, DPI loading order, Dotstar LED, move Z speed |

### Most Modified Files

| File | Changes | Role |
|------|---------|------|
| `screen.py` | 32 | Main application logic |
| `panels/network.py` | 9 | Network configuration |
| `panels/job_status.py` | 9 | Print monitoring |
| `panels/gcode_macros.py` | 9 | Macro interface |
| `ks_includes/KlippyGtk.py` | 9 | GTK integration |
| `ks_includes/widgets/keyboard.py` | 8 | Text input |
| `scripts/KlipperScreen-requirements.txt` | 8 | Dependencies |
| `panels/base_panel.py` | 7 | Base panel |

### Development Patterns

1. **Wayland investment** — Heavy compat work throughout 2025–2026, multiple Cage-specific fixes
2. **Performance caching** — Widget, icon, and pixbuf caching added for responsiveness
3. **Power management** — DPMS, power devices, PAM session management
4. **Keyboard/input refinement** — Significant work on keyboard widgets, touch handling, input types
5. **Ecosystem integration** — Moonraker multi-instance, Spoolman features
6. **One-person project** — 66% of commits from lead maintainer; healthy community contributions but centralized

---

## Feature Comparison

### What They Have, We Don't

#### Tier 1 — Critical Gaps

| Feature | Impact | Notes |
|---------|--------|-------|
| **Camera/Webcam** | High | Multi-camera, MJPEG/WebRTC, MPV player. Top user request for remote monitoring |
| **Console/Terminal** | High | G-code input with color-coded output. Power users need this |
| **Screws Tilt Adjust Visual** | High | Their most praised calibration feature — visual screw diagram with CW/CCW indicators |
| **Multi-Printer Support** | Medium | Switch between Klipper instances. Important for farms/multi-printer setups |
| **Power Device Control** | Medium | Toggle GPIO/smart plugs. Useful for enclosure heaters, lights, PSU control |
| **Software Updater** | Medium | Update Klipper/Moonraker/KlipperScreen from the touchscreen |

#### Tier 2 — Nice to Have

| Feature | Notes |
|---------|-------|
| Pressure Advance panel | Dedicated PA coefficient tuning UI |
| Firmware Retraction panel | View/edit retraction settings (we have an overlay but it's less prominent) |
| Limits panel | Real-time velocity/accel limit adjustment |
| Notifications panel | Dedicated notification history viewer |
| Lockscreen | Authentication/screen lock |
| System info panel | CPU/RAM/disk, service restart |
| Pins panel | MCU pin configuration display |

### What We Have, They Don't

| Feature | Significance |
|---------|-------------|
| **PID Tuning UI** | Unique across all Klipper touchscreens — guided PID calibration workflow |
| **3D Bed Mesh** | Interactive 3D visualization vs their 2D grid |
| **AMS/ERCF Support** | Full multi-material panel with slot management — no other touchscreen has this |
| **Print Start Profiles** | JSON-driven, printer-specific phase detection and automation |
| **First-Run Wizard** | Guided setup: WiFi, printer ID, device detection, touch calibration |
| **GCode Viewer** | 3D visualization of print paths with layer inspection |
| **Print History Dashboard** | Job history with analytics and statistics |
| **Input Shaper Visualization** | Frequency response charts with resonance data |
| **Timelapse Integration** | Moonraker timelapse settings, installation wizard, video playback |
| **Theme Editor** | In-UI live theme customization with color picker |
| **Sound System** | Configurable audio feedback with multiple themes/backends |
| **Telemetry** | Privacy-respecting usage analytics infrastructure |
| **Native C++ Performance** | Significantly faster than Python/GTK — no GIL, no interpreter overhead |
| **Macro Enhancement Wizard** | Guided macro creation/editing workflow |
| **Plugin System** | Runtime plugin management |

### Shared Features

| Feature | KlipperScreen | HelixScreen | Edge |
|---------|---------------|-------------|------|
| Temperature Control | Full | Full | Tie |
| Fan Control | Multi-fan | Multi-fan | Tie |
| Motion/Jog | Full | Full | Tie |
| Filament Load/Unload | Yes | Yes | Tie |
| Print Status | Full | Full | Tie |
| File Browser | Full + search | Full + USB | Tie |
| Bed Mesh | 2D viz | 3D viz | **HelixScreen** |
| Z-Offset | Basic | Wizard | **HelixScreen** |
| LED Control | Advanced | Basic | **KlipperScreen** |
| Exclude Object | Visual map | Full | Tie |
| WiFi Config | Yes | Yes | Tie |
| Settings | Yes | Yes | Tie |
| Input Shaper | Auto-cal UI | Frequency viz | **HelixScreen** |
| Spoolman | Full API | AMS-focused | **KlipperScreen** |
| Macros | List + params | List + wizard | Tie |

---

## Strategic Gaps — Prioritized

Features HelixScreen should consider, ordered by user impact:

### Priority 1 — High Impact

1. **Camera/Webcam Panel** — Remote print monitoring is a top user expectation. Support MJPEG streams at minimum, WebRTC for low latency. Multi-camera selection.

2. **Console Panel** — G-code command terminal. Essential for power users, debugging, and running one-off commands. Color-coded output, command history.

3. **Screws Tilt Adjust Visualization** — KlipperScreen's most praised feature. Visual bed diagram showing probe results with turn direction/amount indicators at each screw position.

### Priority 2 — Medium Impact

4. **Multi-Printer Support** — Useful for printer farms and users with multiple machines. Switch between Moonraker instances without restarting.

5. **Power Device Panel** — Toggle smart plugs, GPIO pins, enclosure heaters/lights from the touchscreen. Moonraker already exposes the API.

6. **Software Updater** — Update Klipper/Moonraker from the touchscreen without SSH. Convenient but also risky (bricking potential).

### Priority 3 — Lower Priority

7. **Lockscreen** — Screen lock for shared environments or child safety.
8. **Pressure Advance Panel** — Dedicated PA tuning UI (we may already support this via macros).
9. **System Info Panel** — CPU/RAM/disk stats, service management.
10. **Dedicated Notifications Panel** — Historical notification log with filtering.

### Not Worth Copying

- **Pins panel** — Very niche, most users never need it
- **Wayland/X11 compat work** — We use LVGL directly, not relevant
- **ConfigParser system** — Our JSON/XML approach is already better
- **GTK CSS theming** — Our token-based design system is more structured

---

## Key Takeaways

1. **KlipperScreen is mature but slow-moving** — ~9 commits/month, 66% from one maintainer. Stability-focused, not innovating rapidly.

2. **Our architecture is stronger** — Native C++/LVGL beats Python/GTK on performance, touch reliability, and embedded deployment. Their Wayland struggles don't apply to us.

3. **Their feature breadth is wider** — 35 panels covering edge cases we haven't touched (camera, console, power devices, multi-printer). We're deeper on calibration and visualization.

4. **Camera is the biggest gap** — It's table stakes for a printer touchscreen. Users expect to see their print from the screen.

5. **Console is the second biggest** — Power users reach for SSH when they can't send G-code from the screen. A console panel keeps them in the UI.

6. **We're winning on calibration** — PID tuning, 3D bed mesh, input shaper visualization, print start profiles. Nobody else has these.

7. **We're winning on modern UI** — First-run wizard, theme editor, sound system, telemetry, plugin system. KlipperScreen feels dated in comparison.
