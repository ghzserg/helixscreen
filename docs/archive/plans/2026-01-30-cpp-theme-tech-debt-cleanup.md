# C++ Theme Tech Debt Cleanup Plan

## Status: PENDING

**Created:** 2026-01-30
**Context:** Audit from Section 4.8 of reactive-theme-system-implementation.md

---

## Overview

This plan addresses hardcoded colors and imperative styling calls in C++ that should use the theme system for consistency and future customization.

### Scope

| Type | Total | Tech Debt | Acceptable |
|------|-------|-----------|------------|
| `lv_color_hex()` | 59 | 18 (31%) | 41 (dynamic data + fallbacks) |
| `lv_obj_set_style_*()` | ~340 | ~80 (24%) | ~260 (runtime + widget internals) |

---

## Phase 1: High Priority - Watchdog Crash Dialog

**File:** `src/helix_watchdog.cpp`
**Issue:** 9 `lv_color_hex()` + 25 `lv_obj_set_style_*()` calls
**Why:** Runs AFTER theme init, so can use theme tokens. Crash dialog should match app theme.

### Tasks
- [ ] Add crash dialog color tokens to theme_core (crash_bg, crash_text, crash_accent)
- [ ] Refactor `show_crash_dialog()` to use `theme_manager_get_color()` instead of hardcoded hex
- [ ] Replace imperative styling with theme-aware approach
- [ ] Test crash dialog in both light and dark themes

### Verification
- Trigger watchdog crash in mock mode
- Verify colors match current theme

---

## Phase 2: Medium Priority - Nozzle Renderer Colors

**File:** `src/rendering/nozzle_renderer_faceted.cpp`
**Issue:** 6 hardcoded colors (Voron red, housing, motor recess, fan area)
**Why:** Users may want to customize toolhead appearance to match their printer

### Tasks
- [ ] Add toolhead color tokens to globals.xml:
  - `toolhead_accent` (default: Voron red #D11D1D)
  - `toolhead_housing` (#121212)
  - `toolhead_detail` (#100C0B)
  - `toolhead_highlight` (#FFFFFF)
- [ ] Refactor nozzle_renderer_faceted.cpp to use `theme_manager_get_color()`
- [ ] Consider adding to theme editor for user customization

### Verification
- Visual check of nozzle rendering
- Test with custom colors via theme editor (if implemented)

---

## Phase 3: Medium Priority - Splash Screen

**File:** `src/helix_splash.cpp`
**Issue:** 2 `lv_color_hex()` calls using BG_COLOR_DARK constant
**Why:** Splash could respect user's theme preference

### Tasks
- [ ] Investigate if theme is available at splash time
- [ ] If yes: use theme token for splash background
- [ ] If no: document as acceptable early-boot exception

### Verification
- Test app startup in both themes

---

## Phase 4: Lower Priority - AMS Overlay Styling

**Files:**
- `src/ui/ui_ams_device_actions_overlay.cpp` (~25 imperative calls)
- `src/ui/ui_ams_tool_mapping_overlay.cpp` (~20 imperative calls)

**Issue:** Overlay card and row styling uses imperative C++ instead of XML
**Why:** Consistency with declarative UI pattern

### Tasks
- [ ] Create XML components for AMS overlay cards
- [ ] Migrate ui_ams_device_actions_overlay.cpp to use XML components
- [ ] Migrate ui_ams_tool_mapping_overlay.cpp to use XML components
- [ ] Remove imperative styling calls

### Verification
- Visual comparison before/after
- Test all AMS overlay interactions

---

## Phase 5: Lower Priority - Debug Panel

**File:** `src/ui/ui_panel_glyphs.cpp`
**Issue:** 12 imperative styling calls
**Why:** Debug panel could use standard XML components

### Tasks
- [ ] Create glyphs_panel.xml component
- [ ] Migrate styling to XML
- [ ] Simplify C++ to just data binding

### Verification
- Visual check of glyphs panel

---

## Phase 6: AMS Default Slot Color

**File:** `src/ui/ui_ams_slot.cpp`
**Issue:** 3 uses of `AMS_DEFAULT_SLOT_COLOR` constant with `lv_color_hex()`
**Why:** Default empty slot color should be themeable

### Tasks
- [ ] Add `ams_slot_empty` color token to theme
- [ ] Replace `lv_color_hex(AMS_DEFAULT_SLOT_COLOR)` with theme lookup
- [ ] Update constant usage in other AMS files

### Verification
- Test AMS panel with empty slots in both themes

---

## DO NOT CHANGE (Documented Exceptions)

### ui_fatal_error.cpp
**Reason:** Bootstrap error screen runs BEFORE theme system loads. Hardcoded colors are correct and documented.

### Dynamic Filament Colors
**Files:** ui_ams_*.cpp, ui_filament_path_canvas.cpp, ui_gcode_viewer.cpp, ui_panel_spoolman.cpp
**Reason:** Filament colors come from API/user data at runtime. Must use `lv_color_hex()`.

### Animation Code
**Files:** ui_nav_manager.cpp, ui_toast_manager.cpp, ui_panel_print_select.cpp
**Reason:** `translate_x`, `translate_y`, `opa` changes for animations are necessary runtime calls.

### Custom Widget Internals
**Files:** ui_ams_mini_status.cpp, ui_switch.cpp, ui_spool_canvas.cpp, ui_icon.cpp, ui_dialog.cpp
**Reason:** Custom widget create handlers need programmatic styling. This is acceptable.

### Theme System Internals
**Files:** theme_manager.cpp, theme_core.c
**Reason:** Parse implementation and fallback returns are correct use of `lv_color_hex()`.

---

## Completion Criteria

- [ ] All Phase 1-2 tasks complete (high/medium priority)
- [ ] Phase 3-6 tasks complete (lower priority) OR documented as deferred
- [ ] Build passes
- [ ] Visual verification in light and dark themes
- [ ] No regressions in themed components

---

## Notes

- This is cleanup work, not blocking functionality
- Phases can be tackled independently
- Phase 1 (watchdog) gives highest user-visible benefit
- Phase 4-5 are refactoring for code quality, not user-visible changes
