# Log Level Refactor Plan

**Status**: Phase 4 - Prefix Standardization
**Created**: 2025-12-13
**Reference**: `docs/LOGGING.md` (the authoritative logging guidelines)

## Overview

This plan outlines the work to standardize logging across the entire HelixScreen codebase. The goal is to make each verbosity level (`-v`, `-vv`, `-vvv`) useful and consistent.

---

## ✅ Completed Work

### Phase 1: DEBUG → TRACE (High-Volume Noise) ✅

Moved per-item loop logging to TRACE to clean up `-vv` output.

| File | Status | Change |
|------|--------|--------|
| `src/ui_theme.cpp` | ✅ Done | Per-color/spacing/font registration |
| `src/moonraker_client.cpp` | ✅ Done | Wire protocol (send_jsonrpc, request registration) |
| `src/moonraker_api_files.cpp` | ✅ Done | Per-file metadata requests, thumbnail downloads |
| `src/ui_panel_print_select.cpp` | ✅ Done | Per-file metadata/thumbnail logs |
| `src/ui_status_bar_manager.cpp` | ✅ Done | Observer registration, state machine |
| `src/printer_state.cpp` | ✅ Done | Subject value plumbing |
| `src/main.cpp` | ✅ Done | State change callback queueing |
| `src/ui_icon.cpp` | ✅ Done | Icon source/variant changes |
| `src/ui_temp_display.cpp` | ✅ Done | Widget creation, binding |
| `src/ui_nav_manager.cpp` | ✅ Done | Per-button/icon wiring |
| `src/ams_state.cpp` | ✅ Done | Per-gate updates |
| `src/ui_ams_slot.cpp` | ✅ Done | Per-slot observer creation |
| `src/ui_gcode_viewer.cpp` | ✅ Done | SIZE_CHANGED events |
| `src/ui_bed_mesh.cpp` | ✅ Done | SIZE_CHANGED events |
| `src/thumbnail_cache.cpp` | ✅ Done | Per-download logs |

### Phase 2: INFO → DEBUG (Demote) ✅

Moved internal details from INFO to DEBUG.

| File | Status | Message Pattern |
|------|--------|-----------------|
| `src/ui_modal_manager.cpp` | ✅ Done | "Initializing/registered modal dialog subjects" |
| `src/settings_manager.cpp` | ✅ Done | "Initializing subjects" |
| `src/usb_backend.cpp` | ✅ Done | Platform detection, mock creation |
| `src/ui_gcode_viewer.cpp` | ✅ Done | Renderer selection |
| `src/wifi_backend_macos.mm` | ✅ Done | "Starting CoreWLAN backend" |
| `src/ams_backend.cpp` | ✅ Done | Backend type selection |

### Phase 3: DEBUG → INFO (Promote) ✅

Promoted important milestones to INFO.

| File | Status | Message |
|------|--------|---------|
| `src/main.cpp` | ✅ Done | "XML UI created successfully" |
| `src/main.cpp` | ✅ Done | "Moonraker client initialized" |
| `src/moonraker_client.cpp` | ✅ Done | "Subscription complete: N objects subscribed" |

---

## 🔄 Current Work: Phase 4 - Full Codebase Audit

### Problem

Found **435+ non-standard logs** across the codebase needing:
1. **Prefix standardization** - Add `[ComponentName]` format
2. **Log level audit** - Many INFO logs are internal details (should be DEBUG)

Current inconsistencies:
| Pattern | Example | Should Be |
|---------|---------|-----------|
| `ComponentName:` | `AmsBackendMock: Started` | `[AmsBackendMock] Started` |
| No prefix | `Loaded 50 tips` | `[TipsManager] Loaded 50 tips` |
| Sentence start | `Printer state subjects...` | `[PrinterState] Subjects...` |

### Files by Non-Standard Count

| File | Count | Priority |
|------|-------|----------|
| `src/main.cpp` | 76 | High |
| `src/ui_gcode_viewer.cpp` | 40 | High |
| `src/gcode_parser.cpp` | 37 | Medium |
| `src/gcode_tinygl_renderer.cpp` | 35 | Medium |
| `src/display_backend_drm.cpp` | 27 | Medium |
| `src/bed_mesh_renderer.cpp` | 21 | Medium |
| `src/display_backend.cpp` | 17 | Medium |
| `src/ui_notification.cpp` | 16 | Medium |
| `src/app_globals.cpp` | 16 | Medium |
| `src/ams_backend_mock.cpp` | 15 | Low (mock) |
| Others | ~135 | Low |

### Standard Format

All logs MUST use: `spdlog::level("[ComponentName] Message", args...)`

- Component name in square brackets
- Space after closing bracket
- PascalCase for single words: `[PrinterState]`
- Space-separated for multi-word: `[AMS State]`, `[Moonraker Client]`

---

## Progress Tracking

- [x] Create `docs/LOGGING.md` with guidelines
- [x] Create this refactor plan
- [x] Phase 1: DEBUG → TRACE (15 files)
- [x] Phase 2: INFO → DEBUG (6 files)
- [x] Phase 3: DEBUG → INFO (3 promotions)
- [x] Verification testing
- [ ] Phase 4: Prefix standardization (~435 logs)
  - [ ] `src/main.cpp` (76 logs)
  - [ ] `src/ui_gcode_viewer.cpp` (40 logs)
  - [ ] `src/gcode_parser.cpp` (37 logs)
  - [ ] `src/display_backend*.cpp` (57 logs)
  - [ ] Remaining files (~225 logs)

---

## Testing Strategy

After each batch of changes:

1. **Build**: `make -j`
2. **Verify prefixes**: `grep -rn 'spdlog::' src/main.cpp | grep -v '\[' | wc -l` (should decrease)
3. **Test runtime**: `./build/bin/helix-screen --test -vv 2>&1 | head -50`

### Success Criteria

- All prefixes follow `[ComponentName]` pattern
- No `printf`, `std::cout`, or `LV_LOG_*` usage
- Build succeeds
- Runtime output is readable and consistent
