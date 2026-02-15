# HelixScreen Development Roadmap

**Last Updated:** 2026-02-12 | **Status:** Beta - Seeking Testers

---

## Project Status

| Area | Status |
|------|--------|
| **Production Panels** | 30 panels + 48 overlays/modals |
| **First-Run Wizard** | 10-step guided setup (Input Shaper, Telemetry opt-in) |
| **Moonraker API** | 40+ methods, abstraction boundary enforced |
| **Multi-Material (AMS)** | Core complete (Happy Hare, AFC, ValgACE, Toolchanger) |
| **Plugin System** | Core infrastructure complete |
| **Test Suite** | 275 test files, 9400+ test cases |
| **Platforms** | Pi, AD5M, K1, QIDI, Snapmaker U1, macOS, Linux |
| **Printer Database** | 60 printer models with auto-detection |
| **Filament Database** | 48 materials with temp/drying/compatibility data |
| **Theme System** | Dynamic JSON themes with live preview |
| **Layout System** | Auto-detection for ultrawide (1920x480) and small (480x320) displays |
| **Sound System** | Multi-backend synthesizer (SDL, PWM, M300), JSON themes |
| **Telemetry** | Opt-in crash reporting + session analytics |

---

## Recently Completed

### Custom Printer Image & Inline Name Editing ✅
**Completed:** 2026-02-12

Custom printer image selection with inline name editing in the printer manager overlay:
- **Image picker** with list+preview layout using declarative XML card component
- **Inline name editing** for printer display name
- **Responsive UI** with centering, image abstraction, and clickable capability chips

**Files:** `ui_panel_settings.cpp`, `ui_xml/printer_image_overlay.xml`

### Theme-Aware Markdown Viewer ✅
**Completed:** 2026-02-12

Custom `<ui_markdown>` XML widget wrapping the lv_markdown library:
- Subject binding via `bind_text` for reactive content updates
- Theme-aware rendering using design token colors
- Used for release notes display in update notification modal

**Files:** `ui_markdown.h`, `ui_markdown.cpp`

### Modal System Standardization ✅
**Completed:** 2026-02-12

All modals standardized to use `ui_dialog` component with `modal_button_row`:
- Consistent theming via LVGL button grey background (auto light/dark)
- Reusable button row component across all modal XML files
- AMS modals migrated to use `modal_button_row`

**Files:** `ui_dialog.h`, `ui_xml/modal_button_row.xml`, `ui_xml/*_modal.xml`

---

## Current Priorities

### 1. Plugin Ecosystem

**Status:** Core infrastructure complete, expanding ecosystem

The plugin system launched with version checking, UI injection points, and async execution.

**Next steps:**
- [ ] LED Effects plugin → production quality
- [ ] Additional plugin examples for community
- [ ] Plugin documentation refinement

**Files:** `src/plugin_manager.cpp`, `docs/PLUGIN_DEVELOPMENT.md`

### 2. Production Hardening

Remaining items for production readiness:
- [ ] Structured logging with log rotation
- [ ] Edge case testing (print failures, filesystem errors)
- [ ] Streaming file operations verified on AD5M with 50MB+ G-code files

---

## What's Complete

### Core Architecture
- LVGL 9.4 with declarative XML layouts
- Reactive Subject-Observer data binding
- Design token system (no hardcoded colors/spacing)
- RAII lifecycle management (PanelBase, ObserverGuard, SubscriptionGuard)
- **Dynamic theme system** with JSON themes, live preview, and theme editor
- **Layout system** with auto-detection for ultrawide and small displays
- Responsive breakpoints (small/medium/large displays)
- Observer factory pattern (`observe_int_sync`, `observe_string_async`, etc.)
- **Versioned config migration** for seamless upgrades between releases
- **Moonraker API abstraction boundary** — UI decoupled from WebSocket layer
- **Modal system** standardized via `ui_dialog` + `modal_button_row` components
- **God class decomposition** — PrinterState into 13 domain classes, SettingsPanel into 5 overlays, PrintStatusPanel into 8 components

### Panels & Features
- **31 Production Panels:** Home, Controls, Motion, Print Status, Print Select, Settings, Advanced, Macros, Console, Power, Print History, Spoolman, AMS, Bed Mesh, PID Calibration, Z-Offset, Screws Tilt, Input Shaper, Extrusion, Filament, Temperature panels, and more
- **17+ Overlays:** WiFi, Timelapse Settings, Firmware Retraction, Machine Limits, Fan Control, Exclude Object, Print Tune, Theme Editor, AMS Device Operations, Network Settings, Touch Calibration, Printer Manager, and more
- **First-Run Wizard:** WiFi -> Moonraker -> Printer ID -> Heaters -> Fans -> LEDs -> Input Shaper -> Telemetry Opt-in -> Summary
- **Calibration Workflows:** PID tuning (live graph, fan control, material presets), Z-offset with live adjust, Screws Tilt, Input Shaper (frequency response charts, CSV parser, per-axis results)
- **Bed Mesh:** 3D visualization with touch rotation, profile switching, 38 FPS optimized rendering
- **Sound system:** Multi-backend audio (SDL, PWM, M300) with JSON themes and volume control
- **Timelapse:** Plugin detection, install wizard, settings UI, real-time event handling, render progress, video management
- **Filament tracking:** Live consumption during printing, slicer estimate on completion
- **Display rotation:** Support for 0/90/180/270 across all binaries
- **Telemetry:** Opt-in crash reporting and session analytics with Cloudflare Worker backend
- **Pre-print ETA prediction** using weighted-average historical timing data
- **Exclude objects** with object list overlay, thumbnails, and confirmation flow
- **Markdown viewer** (`ui_markdown`) with theme-aware rendering and subject binding
- **Custom printer images** with inline name editing in printer manager overlay

### Multi-Material (AMS)
- 5 backend implementations: Happy Hare, AFC-Klipper, ValgACE, Toolchanger, Mock
- Spool visualization with 3D-style gradients and animations
- **Visual slot configuration:** Tool badges, endless spool arrows, tap-to-edit popup
- **Material compatibility validation** with toast warnings for incompatible endless spool
- Spoolman integration (6 API methods: list spools, assign, consume, etc.)
- Print color requirements display from G-code metadata
- External spool bypass support
- **AFC comprehensive support:** Full data parsing (hub/extruder/buffer/stepper), 11 device actions, per-lane reset, live smoke test, mock AFC mode

### Filament Database
- **48 materials** with temperature ranges, drying parameters, density, compatibility groups
- 7 compatibility groups (PLA, PETG, ABS_ASA, PA, TPU, PC, HIGH_TEMP)
- Material alias resolution ("Nylon" -> "PA", "Polycarbonate" -> "PC")
- Dryer presets dropdown populated from database
- Endless spool material validation

### Plugin System
- Dynamic plugin loading with version compatibility checking
- UI injection points for extensibility
- Thread-safe async plugin execution
- Settings UI for plugin discovery and management
- LED Effects proof-of-concept plugin

### Moonraker Integration
- WebSocket client with auto-reconnection
- JSON-RPC protocol with timeout management
- 40+ API methods: print control, motion, heaters, fans, LEDs, power devices, print history, timelapse, screws tilt, firmware retraction, machine limits, Spoolman, input shaper
- Full mock backend for development without real printer

### Installer & Deployment
- **KIAUH extension** for one-click install
- **Bundled uninstaller** (`install.sh --uninstall`) with previous UI restoration (151 shell tests)
- **Installer pre-flight checks** for Klipper/Moonraker on AD5M and K1
- **QIDI & Snapmaker U1** platform support

### Build System
- Parallel builds (`make -j`)
- Docker cross-compilation for Pi (aarch64) and AD5M (armv7-a)
- Pre-commit hooks (clang-format, quality checks)
- CI/CD with GitHub Actions
- Icon font generation with validation
- Incremental compile_commands.json generation for LSP

---

## Backlog (Lower Priority)

| Feature | Effort | Notes |
|---------|--------|-------|
| **Lazy panel initialization** | Medium | Defer `init_subjects()` until first nav; blocked on LVGL XML subject timing |
| **Camera/Webcam** | Low | Lower priority for local touchscreen use case |
| **Belt tension visualization** | Future | Accelerometer-based CoreXY belt comparison; reuses frequency chart |
| **OTA updates** | Future | UpdateChecker downloads + installs; needs auto-apply without user interaction |

See `docs/IDEAS.md` for additional ideas and design rationale.

---

## Known Technical Debt

See `docs/ARCHITECTURAL_DEBT.md` for the full register.

**Resolved (2026-01):**
- ~~PrinterState god class~~ → Decomposed into 13 domain classes
- ~~PrintStatusPanel~~ → Extracted 8 focused components
- ~~SettingsPanel~~ → Extracted 5 overlay components

**Remaining:**
- **Application class** (1249 lines) → Extract bootstrapper and runtime
- **Singleton cascade pattern** → UIPanelContext value object
- **Code duplication** → SubjectManagedPanel base class (in progress)
- **NavigationManager intimacy** → Extract INavigable interface

---

## Design Philosophy

HelixScreen is a **local touchscreen** UI - users are physically present at the printer. This fundamentally differs from web UIs (Mainsail/Fluidd) designed for remote monitoring.

**We prioritize:**
- Tactile controls optimized for touch
- At-a-glance information for the user standing at the machine
- Calibration workflows (PID, Z-offset, screws tilt, input shaper)
- Real-time tuning (speed, flow, firmware retraction)

**Lower priority for this form factor:**
- Camera (you can see the printer with your eyes)
- Job queue (requires manual print removal between jobs)
- System stats (CPU/memory) — not diagnosing remote issues
- Remote access/monitoring features

Don't copy features from web UIs just because "competitors have it" — evaluate whether it makes sense for a local touchscreen.

---

## Target Platforms

| Platform | Architecture | Status |
|----------|--------------|--------|
| **Raspberry Pi 4/5 (64-bit)** | aarch64 | Docker cross-compile |
| **Raspberry Pi (32-bit)** | armv7-a (armhf) | Docker cross-compile |
| **BTT Pad** | aarch64 | Same as Pi |
| **Adventurer 5M** | armv7-a | Static linking (glibc 2.25) |
| **Creality K1** | MIPS32 | Static linking |
| **QIDI** | aarch64 | Detection heuristics + print profile |
| **Snapmaker U1** | armv7-a | 480x320 display support |
| **Creality K2** | ARM | Static musl (untested) |
| **macOS** | x86_64/ARM64 | SDL2 development |
| **Linux** | x86_64 | SDL2, CI/CD tested |

---

## Contributing

See `docs/DEVELOPMENT.md#contributing` for code standards and git workflow.

**Key references:**
- `CLAUDE.md` - Project patterns and critical rules
- `docs/ARCHITECTURE.md` - System design and principles
- `docs/LVGL9_XML_GUIDE.md` - XML layout reference
- `docs/DEVELOPMENT.md` - Build and workflow guide
