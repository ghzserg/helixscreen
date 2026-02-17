# HelixScreen Development Roadmap

**Last Updated:** 2026-02-16 | **Status:** Beta - Seeking Testers

---

## Project Status

| Area | Status |
|------|--------|
| **Production UI** | 30 panels, 19 overlays, 13 modals, 187 XML layouts |
| **First-Run Wizard** | 13-step guided setup (touch cal, WiFi, probe, input shaper, telemetry) |
| **Moonraker API** | 116 methods, abstraction boundary enforced |
| **Multi-Material (AMS)** | 5 backends, multi-unit, multi-backend, error visualization |
| **Tool Abstraction** | ToolState with tool-backend mapping, multi-extruder temps |
| **Spoolman** | 23 API methods, full CRUD, Spool Wizard, virtualized list with search |
| **Plugin System** | Core infrastructure complete |
| **Test Suite** | 131 test files (99 C++, 32 shell), ~3,400 test cases, 17,600+ assertions |
| **Platforms** | Pi, AD5M, K1, QIDI, Snapmaker U1, macOS, Linux |
| **Printer Database** | 63 printer models with auto-detection |
| **Filament Database** | 48 materials with temp/drying/compatibility data |
| **Theme System** | Dynamic JSON themes with live preview |
| **Layout System** | Auto-detection for ultrawide (1920x480) and small (480x320) displays |
| **Sound System** | Multi-backend synthesizer (SDL, PWM, M300), JSON themes |
| **Telemetry** | Opt-in crash reporting + session analytics + debug bundle upload |

---

## Recently Completed

### Multi-Unit AMS ✅
**Completed:** 2026-02-17

Multi-unit AMS overview panel, shared detail components (DRY refactor), per-slot error visualization with buffer health, AFC/Happy Hare device management overlays, mock backend deduplication via shared defaults modules. Visual verification complete.

**Branch:** `feature/multi-unit-ams` (76 commits, 146 files, +34k/-14k lines)

### Debug Bundle Upload ✅
**Completed:** 2026-02-16

Support diagnostics system for remote troubleshooting:
- **Debug bundle collector** gathers logs, crash data, config, and system info
- **Upload to Cloudflare Worker** endpoint for support team access
- **Modal UI** with progress feedback in Settings panel
- 193 unit tests

**Files:** `debug_bundle_collector.cpp`, `ui_debug_bundle_modal.cpp`

### Probe Management Overlay ✅
**Completed:** 2026-02-15

Probe configuration and management overlay in Advanced panel:
- **Auto-detection** of probe types: Cartographer, Beacon, Tap, Klicky, BLTouch, eddy current
- **BLTouch panel** with live Z-offset adjustment
- **First-run wizard step** for probe sensor selection
- Hidden when no probe detected

**Files:** `ui_probe_overlay.cpp`, `ui_xml/probe_overlay.xml`, `ui_xml/probe_bltouch_panel.xml`

### Active Extruder Temperature Tracking ✅
**Completed:** 2026-02-16

Unified temperature tracking for multi-extruder/multi-tool printers:
- Dynamic nozzle label showing active tool number
- `PrinterTemperatureState` tracks active extruder across tool changes
- Integrated with LED auto-state and telemetry

### Z-Axis Direction Flip Toggle ✅
**Completed:** 2026-02-15

Settings option to invert Z movement buttons for printers where the auto-detection heuristic gets it wrong.

### Hardware Discovery Improvements ✅
**Completed:** 2026-02-16

Skip expected hardware in new discovery check — reduces false-positive "new hardware detected" prompts after initial wizard setup.

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
- LVGL 9.4 with declarative XML layouts (187 XML files)
- Reactive Subject-Observer data binding
- Design token system (no hardcoded colors/spacing)
- RAII lifecycle management (PanelBase, ObserverGuard, SubscriptionGuard)
- **Dynamic theme system** with JSON themes, live preview, and theme editor
- **Layout system** with auto-detection for ultrawide and small displays
- Responsive breakpoints (small/medium/large displays)
- Observer factory pattern (`observe_int_sync`, `observe_string_async`, etc.)
- **Versioned config migration** for seamless upgrades between releases
- **Moonraker API abstraction boundary** — 116 methods, UI decoupled from WebSocket layer
- **Modal system** standardized via `ui_dialog` + `modal_button_row` components
- **God class decomposition** — PrinterState into 13 domain classes, SettingsPanel into 5 overlays, PrintStatusPanel into 8 components

### Panels & Features
- **30 Production Panels:** Home, Controls, Motion, Print Status, Print Select, Settings, Advanced, Macros, Console, Power, Print History, Spoolman, AMS, AMS Overview, Bed Mesh, PID Calibration, Z-Offset, Screws Tilt, Input Shaper, Extrusion, Filament, Temperature, and more
- **19 Overlays:** WiFi, Timelapse Settings, Firmware Retraction, Machine Limits, Fan Control, Exclude Object, Print Tune, Theme Editor, AMS Device Operations, AMS Section Detail, AMS Spoolman, Network Settings, Touch Calibration, Printer Manager, Printer Image, LED Control, Probe Management, and more
- **13 Modals:** AMS Edit, AMS Loading Error, Change Host, Crash Report, Debug Bundle, Exclude Object, Plugin Install, Print Cancel, Runout Guidance, Save Z-Offset, Spoolman Edit, Action Prompt
- **First-Run Wizard:** Touch Cal → Language → WiFi → Moonraker → Printer ID → Heaters → Fans → AMS → LEDs → Filament Sensors → Probe Sensors → Input Shaper → Summary (13 steps, conditional skipping)
- **Calibration Workflows:** PID tuning (live graph, fan control, material presets), Z-offset with live adjust, Screws Tilt, Input Shaper (frequency response charts, CSV parser, per-axis results)
- **Bed Mesh:** 3D visualization with touch rotation, profile switching, 38 FPS optimized rendering
- **Sound system:** Multi-backend audio (SDL, PWM, M300) with JSON themes and volume control
- **Timelapse:** Plugin detection, install wizard, settings UI, real-time event handling, render progress, video management
- **Filament tracking:** Live consumption during printing, slicer estimate on completion
- **Display rotation:** Support for 0/90/180/270 across all binaries
- **Telemetry:** Opt-in crash reporting, session analytics, and debug bundle upload via Cloudflare Worker backend
- **Pre-print ETA prediction** using weighted-average historical timing data
- **Exclude objects** with object list overlay, thumbnails, and confirmation flow
- **Markdown viewer** (`ui_markdown`) with theme-aware rendering and subject binding
- **Custom printer images** with inline name editing in printer manager overlay
- **Probe management** with auto-detection for Cartographer, Beacon, Tap, Klicky, BLTouch, eddy current
- **Active extruder tracking** with dynamic nozzle labels for multi-tool printers
- **Z-axis direction flip** toggle for inverted motion printers

### Multi-Material (AMS) & Tool Abstraction

**5 backend implementations** supporting diverse hardware:

| Backend | Hardware | Topology | Slots | Key Capabilities |
|---------|----------|----------|-------|-----------------|
| **Happy Hare** | ERCF, Tradrack, 3MS, Night Owl | Linear | 1-16 | Tool mapping, bypass, endless spool (read-only) |
| **AFC** | Box Turtle (AFC-Klipper-Add-On) | Hub | 1-16 | Editable endless spool, auto-heat on load, buffer health, 12+ device actions |
| **Tool Changer** | viesturz/klipper-toolchanger | Parallel | 1-16 | Mounted/detect state, per-tool filament systems |
| **ValgACE** | AnyCubic ACE Pro | Hub | 4 | Integrated dryer control (temp/duration/fan), REST polling |
| **Mock** | Development simulation | All | Config | Simulates all 4 types with realistic multi-phase operations |

**Multi-backend architecture:**
- Multiple concurrent filament systems per printer (e.g., tool changer + AFC)
- Per-backend event routing and subject storage
- Tool-to-backend mapping via `ToolInfo.backend_index`

**Multi-unit AMS:**
- Overview panel with grid of unit cards (mini slot bars, hub sensor dots, error badges)
- Inline detail view with zoom transition
- Per-slot error indicators with severity (info/warning/error)
- Buffer health monitoring with fault proximity visualization (AFC)

**Tool abstraction layer (ToolState):**
- `ToolInfo` struct: offsets, extruder/heater/fan names, backend mapping, detect state
- Active tool tracking with dynamic nozzle labels for multi-tool printers
- Multi-extruder temperature with per-extruder subjects and selector UI

**UI components (16 XML files):**
- **AMS Panel** — Slot grid, path canvas (spool → hub → toolhead → nozzle), backend selector
- **AMS Overview Panel** — Multi-unit grid with inline detail
- **Device Operations Overlay** — Dynamic backend-specific sections (AFC: 4 sections, HH: 3 sections)
- **Context Menu** — Per-slot actions: load, unload, edit, Spoolman assign, tool mapping, endless spool backup
- **Spool Wizard** — 3-step creation: Vendor → Filament → Spool Details with modal forms for new vendors/filaments
- **Spoolman Panel** — Browse, search, edit, delete with virtualized list and context menus
- **AMS Mini Status** — Home panel widget, click to open
- Modals: AMS Edit (color/material/brand), Loading Error, Dryer (temp/duration presets)

**Spoolman integration:**
- 23 API methods: full CRUD for spools, filaments, vendors + external catalog
- Auto-active spool sync on slot load
- Weight polling with reference counting
- Material compatibility validation with toast warnings

**Also:**
- Spool visualization with 3D-style gradients and animations
- Print color requirements display from G-code metadata
- External spool bypass support
- 15 dedicated test files covering all backends, multi-unit, tool mapping, endless spool, device actions

### Filament Database
- **48 materials** with temperature ranges, drying parameters, density, compatibility groups
- 7 compatibility groups (PLA, PETG, ABS_ASA, PA, TPU, PC, HIGH_TEMP)
- Material alias resolution ("Nylon" → "PA", "Polycarbonate" → "PC")
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
- 116 API methods: print control, motion, heaters, fans, LEDs, power devices, print history, timelapse, screws tilt, firmware retraction, machine limits, Spoolman (full CRUD), input shaper, probe, database, and more
- Full mock backend for development without real printer

### Installer & Deployment
- **KIAUH extension** for one-click install
- **Bundled uninstaller** (`install.sh --uninstall`) with previous UI restoration
- **572 shell tests** across 32 BATS test files
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

See `docs/devel/IDEAS.md` for additional ideas and design rationale.

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
- **Code duplication** → PanelBase/OverlayBase with RAII subjects (complete)
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
