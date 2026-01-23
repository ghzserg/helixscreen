# HelixScreen Development Roadmap

**Last Updated:** 2026-01-22 | **Status:** Beta - Seeking Testers

---

## Project Status

| Area | Status |
|------|--------|
| **Production Panels** | 30 panels + 16 overlays |
| **First-Run Wizard** | 7-step guided setup |
| **Moonraker API** | 40+ methods |
| **Multi-Material (AMS)** | Core complete (Happy Hare, AFC, ValgACE, Toolchanger) |
| **Plugin System** | Core infrastructure complete |
| **Test Suite** | 156 test files, 6600+ test cases |
| **Platforms** | Pi, AD5M, macOS, Linux |
| **Printer Database** | 59 printer models with auto-detection |
| **Filament Database** | 48 materials with temp/drying/compatibility data |
| **Theme System** | Dynamic JSON themes with live preview |

---

## Recently Completed

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

### 1. AMS Settings Redesign 🔄
**Status:** Phase 2 in progress

Consolidating 7 sparse AMS settings panels into visual slot-based configuration:

**Completed:**
- [x] Tool badges on AMS slots (T0, T1, etc.)
- [x] Endless spool arrow visualization (backup chains)
- [x] Tap-to-edit popup for quick tool/backup config
- [x] Device Operations overlay (consolidates maintenance/calibration/speed)

**Remaining:**
- [ ] Phase 3: Remove old sub-panels, update navigation

**Files:** `ui_panel_ams.cpp`, `ams_device_operations.xml`
**Plan:** `docs/plans/2026-01-21-ams-settings-redesign.md`

### 2. Input Shaper Implementation

**Status:** Stub panel exists, awaiting implementation

The Input Shaper panel currently shows a "Coming Soon" overlay. Implementation requires:
- Integration with Klipper's `input_shaper` module
- Graphing `SHAPER_CALIBRATE` results (frequency response)
- Recommended shaper selection UI
- Apply/revert workflow

**Files:** `src/ui_panel_input_shaper.cpp`, `ui_xml/input_shaper_panel.xml`

### 3. Plugin Ecosystem

**Status:** Core infrastructure complete, expanding ecosystem

The plugin system launched with version checking, UI injection points, and async execution.

**Next steps:**
- [ ] LED Effects plugin → production quality
- [ ] Additional plugin examples for community
- [ ] Plugin documentation refinement

**Files:** `src/plugin_manager.cpp`, `docs/PLUGIN_DEVELOPMENT.md`

### 4. Production Hardening

Remaining items for production readiness:
- [ ] Structured logging with log rotation
- [ ] Edge case testing (print failures, filesystem errors)
- [ ] Streaming file operations verified on AD5M with 50MB+ G-code files

### 5. Bed Mesh Renderer Refactor

**Status:** Phases 1-4 complete, 5-7 remaining

Decomposing the 2,243-line god-file into modular architecture:
- Rasterizer, Overlays, Geometry, Clipping modules
- Target: 1,450 lines total with single-responsibility modules

See `IMPLEMENTATION_PLAN.md` for detailed phase breakdown.

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
- **30 Production Panels:** Home, Controls, Motion, Print Status, Print Select, Settings, Advanced, Macros, Console, Power, Print History, Spoolman, AMS, Bed Mesh, PID Calibration, Z-Offset, Screws Tilt, Extrusion, Filament, Temperature panels, and more
- **16+ Overlays:** WiFi, Timelapse Settings, Firmware Retraction, Machine Limits, Fan Control, Exclude Object, Print Tune, Theme Editor, AMS Device Operations, Network Settings, Touch Calibration, and more
- **First-Run Wizard:** WiFi → Moonraker → Printer ID → Heaters → Fans → LEDs → Summary

### Multi-Material (AMS)
- 5 backend implementations: Happy Hare, AFC-Klipper, ValgACE, Toolchanger, Mock
- Spool visualization with 3D-style gradients and animations
- **Visual slot configuration:** Tool badges, endless spool arrows, tap-to-edit popup
- **Material compatibility validation** with toast warnings for incompatible endless spool
- Spoolman integration (6 API methods: list spools, assign, consume, etc.)
- Print color requirements display from G-code metadata
- External spool bypass support

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
- 40+ API methods: print control, motion, heaters, fans, LEDs, power devices, print history, timelapse, screws tilt, firmware retraction, machine limits, Spoolman

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
| **Client-side thumbnails** | Low | Fallback when Moonraker doesn't provide (USB symlinked files) |
| **mDNS discovery** | Low | Auto-find Moonraker; manual IP works fine |
| **NULL → nullptr cleanup** | Low | Consistency across C++ codebase |
| **Belt tension visualization** | Future | Controlled excitation + stroboscopic LED feedback |
| **OTA updates** | Future | Currently requires manual binary update |
| **Custom theme creation wizard** | Low | Guide users through creating themes from scratch |

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
| **Raspberry Pi 4/5** | aarch64 | Docker cross-compile |
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
