# HelixScreen Development Roadmap

**Last Updated:** 2026-02-07 | **Status:** Beta - Seeking Testers

---

## Project Status

| Area | Status |
|------|--------|
| **Production Panels** | 31 panels + 17 overlays |
| **First-Run Wizard** | 8-step guided setup (Input Shaper optional) |
| **Moonraker API** | 40+ methods |
| **Multi-Material (AMS)** | Core complete (Happy Hare, AFC, ValgACE, Toolchanger) |
| **Plugin System** | Core infrastructure complete |
| **Test Suite** | 203 test files, 6700+ test cases |
| **Platforms** | Pi, AD5M, macOS, Linux |
| **Printer Database** | 59 printer models with auto-detection |
| **Filament Database** | 48 materials with temp/drying/compatibility data |
| **Theme System** | Dynamic JSON themes with live preview |

---

## Recently Completed

### Comprehensive AFC Support ✅
**Completed:** 2026-02-07

Full parsing and command support for AFC (Box Turtle) multi-filament systems:
- **Bug fixes:** `current_state` field priority, tool mapping from `map` field, endless spool from `runout_lane`, AFC message parsing with error dedup
- **Full data parsing:** Hub bowden length, stepper extended fields (`buffer_status`, `filament_status`, `dist_hub`), buffer objects, global state (`quiet_mode`, `led_state`)
- **11 device actions:** Calibration wizard, bowden length, test lanes, change blade, park, brush, reset motor, LED toggle, quiet mode, forward/reverse speed
- **Error recovery:** Differentiated `reset()` (AFC_HOME) vs `recover()` (AFC_RESET), per-lane reset (`AFC_LANE_RESET`)
- **Mock AFC mode:** Realistic Box Turtle simulation via `HELIX_MOCK_AMS_TYPE=afc`
- **Live smoke test:** `scripts/afc-test.sh` validates all AFC objects and detects field drift

**Files:** `ams_backend_afc.cpp`, `ams_backend_afc.h`, `ams_backend_mock.cpp`, 8 fixture files
**Tests:** 92 test cases, 173 assertions (phases tagged `[phase1]`-`[phase4]`)

### Input Shaper Calibration ✅
**Completed:** 2026-01-22

Full resonance compensation workflow:
- Calibration trigger for X/Y axes + accelerometer noise check
- Frequency response chart (hardware-adaptive: full/simplified/table based on platform)
- Results display with recommended shaper + 5 alternatives (frequency, vibrations, smoothing)
- Apply (session) / Save Config (persistent) / Test Print (TUNING_TOWER) workflow
- 30-day persistent cache with printer ID matching
- First-run wizard integration (optional step, auto-skipped if no ADXL)
- Platform tier detection (EMBEDDED/BASIC/STANDARD) for UI adaptation

**Files:** `input_shaper_calibrator.cpp`, `ui_panel_input_shaper.cpp`, `ui_frequency_response_chart.cpp`
**Tests:** 535+ assertions across 6 test files

### Dynamic Theming System ✅
**Completed:** 2026-01-21

Full JSON-based theming with live preview:
- 16-color palette system (replaces hardcoded Nord)
- Theme editor overlay with color picker
- Live preview without restart
- Property sliders (border radius, opacity, shadows)
- Save/Save As New/Revert workflow
- Theme discovery and directory management

**Files:** `theme_loader.h`, `ui_theme_editor_overlay.cpp`, `helix_theme.c`

### AMS Settings Redesign ✅
**Completed:** 2026-01-22

Visual slot-based configuration replacing 7 sparse panels:
- Tool badges on AMS slots (T0, T1, etc.)
- Endless spool arrow visualization (routed backup chains)
- Tap-to-edit popup for quick tool/backup configuration
- Device Operations overlay (consolidated Home/Recover/Abort + dynamic calibration/speed)
- iOS-style settings navigation with 8 sub-panels
- Material compatibility validation with toast warnings

**Files:** `ui_panel_ams.cpp`, `ui_ams_device_operations_overlay.cpp`, `ui_endless_spool_arrows.cpp`

### Material Database Consolidation ✅
**Completed:** 2026-01-22

Single source of truth for filament data:
- 48 materials with temp ranges, drying params, density, compatibility groups
- Material alias resolution ("Nylon" → "PA")
- Endless spool compatibility validation with toast warnings
- Dryer presets dropdown (replaced hardcoded buttons)
- All UI components now use database lookups

**Files:** `filament_database.h`, `ui_panel_filament.cpp`, `ui_ams_edit_modal.cpp`

### God Class Decomposition ✅
**Completed:** 2026-01-12

Major architectural improvements:
- **PrinterState:** Decomposed into 13 focused domain classes (was 1514 lines)
- **SettingsPanel:** Extracted 5 overlay components (1976→935 lines, 53% reduction)
- **PrintStatusPanel:** Extracted 8 components (2983→1700 lines)

---

## Current Priorities

### 1. Pre-Print ETA Prediction

**Status:** In progress

Predict how long the preparation phase (heating, homing, leveling) will take based on historical timing data:
- **PreprintPredictor**: Pure-logic weighted-average predictor using last 3 print start timings
- Phase-level timing history stored in config (`/print_start_history/entries`)
- Real-time "time remaining" display during PRINT_START with per-phase weighting
- FIFO entry management with 15-minute anomaly rejection
- Integration with PrintStartCollector for automatic timing capture

**Files:** `preprint_predictor.h`, `print_start_collector.h`, `printer_print_state.h`
**Tests:** 18 test cases covering weighting, FIFO, edge cases

### 2. Plugin Ecosystem

**Status:** Core infrastructure complete, expanding ecosystem

The plugin system launched with version checking, UI injection points, and async execution.

**Next steps:**
- [ ] LED Effects plugin → production quality
- [ ] Additional plugin examples for community
- [ ] Plugin documentation refinement

**Files:** `src/plugin_manager.cpp`, `docs/PLUGIN_DEVELOPMENT.md`

### 3. Production Hardening

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
- Responsive breakpoints (small/medium/large displays)
- Observer factory pattern (`observe_int_sync`, `observe_string_async`, etc.)

### Panels & Features
- **31 Production Panels:** Home, Controls, Motion, Print Status, Print Select, Settings, Advanced, Macros, Console, Power, Print History, Spoolman, AMS, Bed Mesh, PID Calibration, Z-Offset, Screws Tilt, Input Shaper, Extrusion, Filament, Temperature panels, and more
- **17+ Overlays:** WiFi, Timelapse Settings, Firmware Retraction, Machine Limits, Fan Control, Exclude Object, Print Tune, Theme Editor, AMS Device Operations, Network Settings, Touch Calibration, and more
- **First-Run Wizard:** WiFi → Moonraker → Printer ID → Heaters → Fans → LEDs → Input Shaper → Summary
- **Calibration Workflows:** PID tuning, Z-offset with live adjust, Screws Tilt, Input Shaper (full ADXL integration)
- **Bed Mesh:** 3D visualization with touch rotation, profile switching, 38 FPS optimized rendering

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
- 40+ API methods: print control, motion, heaters, fans, LEDs, power devices, print history, timelapse, screws tilt, firmware retraction, machine limits, Spoolman, input shaper
- Full mock backend for development without real printer

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
| **OTA updates** | Future | Currently requires manual binary update |

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
- Remote monitoring features

See `docs/IDEAS.md` § "Design Philosophy: Local vs Remote UI" for full rationale.

---

## Target Platforms

| Platform | Architecture | Status |
|----------|--------------|--------|
| **Raspberry Pi 4/5 (64-bit)** | aarch64 | Docker cross-compile |
| **Raspberry Pi (32-bit)** | armv7-a (armhf) | Docker cross-compile |
| **BTT Pad** | aarch64 | Same as Pi |
| **Adventurer 5M** | armv7-a | Static linking (glibc 2.25) |
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
