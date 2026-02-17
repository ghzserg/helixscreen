# Multi-Unit AMS Branch Summary

**Branch**: `feature/multi-unit-ams`
**Date**: 2026-02-15
**Status**: Ready for merge to main
**Stats**: 76 commits, 146 files changed, +34,843 / -13,806 lines

## What Shipped

### 1. Multi-Unit AMS Overview Panel (M1-M3)

The headline feature: a new overview panel for multi-unit filament systems (e.g., AFC printers with multiple Box Turtle units). When a backend reports 2+ units, the filament panel navigates to an overview grid instead of the single-unit detail view.

**Key files:**
- `include/ui_panel_ams_overview.h` -- `AmsOverviewPanel` class
- `src/ui/ui_panel_ams_overview.cpp` -- Card grid, inline detail, zoom animations, context menu
- `ui_xml/ams_overview_panel.xml` -- Two-column layout (cards/detail left, loaded info right)
- `ui_xml/ams_unit_card.xml` -- Mini unit card with slot color bars, hub sensor dot, error badge

**How it works:**
- `navigate_to_ams_panel()` checks `AmsSystemInfo::is_multi_unit()` to decide overview vs. detail
- Overview shows a card per unit with mini color bars (height proportional to remaining weight)
- System path canvas (`ui_system_path_canvas`) draws unit-to-hub-to-nozzle routing below the cards
- Clicking a card triggers an inline zoom transition (scale+fade animation, 200ms) to a detail view of that unit's slots
- Detail view uses the shared `ams_unit_detail` component with hub-only path canvas
- Slot context menu (load/unload) works in detail mode
- Back button returns to overview with reverse zoom-out animation

**Key files (system path):**
- `include/ui_system_path_canvas.h` / `src/ui/ui_system_path_canvas.cpp` -- Custom LVGL widget: draws unit outputs converging through hub to nozzle, per-unit hub sensors, bypass path, active filament color

### 2. Shared AMS Detail Components (DRY Refactor)

Extracted shared rendering code so both AmsPanel and AmsOverviewPanel use identical slot grid, tray, labels, and path canvas setup.

**Key files:**
- `include/ui_ams_detail.h` / `src/ui/ui_ams_detail.cpp` -- Free functions: `ams_detail_find_widgets()`, `ams_detail_create_slots()`, `ams_detail_destroy_slots()`, `ams_detail_update_tray()`, `ams_detail_update_labels()`, `ams_detail_setup_path_canvas()`
- `include/ui_ams_slot_layout.h` -- `AmsSlotLayout` struct: slot width, overlap, positioning math
- `ui_xml/components/ams_unit_detail.xml` -- Shared XML component (slot grid + tray + labels layer)
- `ui_xml/components/ams_loaded_card.xml` -- Shared "currently loaded" filament info card

### 3. Error State Visualization

Per-slot error indicators and per-unit error badges, driven by new `SlotError` and `BufferHealth` data on `SlotInfo`.

**Data model** (`include/ams_types.h`):
- `SlotError` -- message + severity (INFO/WARNING/ERROR), `std::optional` on `SlotInfo`
- `BufferHealth` -- AFC buffer fault proximity data, `std::optional` on `SlotInfo`
- `AmsUnit::has_any_error()` -- rolls up per-slot errors for overview badge

**Detail view** (`src/ui/ui_ams_slot.cpp`):
- 14px error badge at top-right of spool (red for ERROR, yellow for WARNING)
- 8px buffer health dot at bottom-center (green/yellow/red based on fault proximity)

**Overview view** (`src/ui/ui_panel_ams_overview.cpp`):
- 12px error badge at top-right of unit card (worst severity across slots)
- Mini-bar status lines colored by error severity

**Backend integration:**
- AFC: per-lane error from `status` field + buffer health from `AFC_buffer` objects
- Happy Hare: system-level error mapped to `current_slot` via `reason_for_pause`, cleared on IDLE
- Mock: `set_slot_error()` / `set_slot_buffer_health()` + pre-populated errors in AFC multi-unit mode

**Status:** Code-complete with unit tests. Visual verification checklist in `docs/devel/plans/2026-02-15-error-state-visualization-design.md` has NOT been run yet.

### 4. AFC Device Management (Progressive Disclosure)

Full device management overlay for AFC backends with progressive disclosure (sections -> detail -> actions).

**Key files:**
- `include/ui_ams_device_operations_overlay.h` / `src/ui/ui_ams_device_operations_overlay.cpp` -- Section list with icons, descriptions, and drill-down
- `include/ui_ams_device_section_detail_overlay.h` / `src/ui/ui_ams_device_section_detail_overlay.cpp` -- New overlay: renders actions for a single section (buttons, toggles, sliders, dropdowns)
- `include/afc_config_manager.h` / `src/printer/afc_config_manager.cpp` -- Fetches/parses AFC Klipper config files for dynamic action values
- `include/klipper_config_parser.h` / `src/system/klipper_config_parser.cpp` -- Generic Klipper INI-like config parser (colon/equals separators, multi-line gcode)
- `ui_xml/ams_device_operations.xml` -- Section list UI
- `ui_xml/ams_device_section_detail.xml` -- Action detail UI

**AFC sections (7 total):** setup, speed, hub, tip_forming, purge, config, maintenance

### 5. Happy Hare Device Management

Device management sections and actions for Happy Hare MMU backends, mirroring the AFC pattern.

**Key files:**
- `include/hh_defaults.h` / `src/printer/hh_defaults.cpp` -- Default sections (setup, speed, maintenance) and actions
- `src/printer/ams_backend_happy_hare.cpp` -- Wired `get_device_sections()` and `get_device_actions()` to defaults + dynamic overlays

### 6. Mock Backend Deduplication (AFC + HH Shared Defaults)

Eliminated 150+ lines of duplicated action/section/capability data between mock and real backends.

**Key files:**
- `include/afc_defaults.h` / `src/printer/afc_defaults.cpp` -- Single source of truth for AFC sections (7), actions (~11), and capabilities
- `include/hh_defaults.h` / `src/printer/hh_defaults.cpp` -- Single source of truth for HH sections (3) and actions
- Mock backend now calls `afc_default_*()` / `hh_default_*()` instead of hardcoding

### 7. AFC Error Handling & Toast Notifications

Error messages from AFC backends are surfaced as toast notifications with dedup and action_prompt suppression.

**Key files:**
- `src/printer/ams_backend_afc.cpp` -- Error state tracking, buffer fault warnings, `reason_for_pause` clearing on idle
- `include/action_prompt_manager.h` / `src/ui/action_prompt_manager.cpp` -- Action prompt dialog generation from mock calibration wizard

### 8. Spoolman Panel Enhancements

Major rework of the Spoolman panel with virtualized list, CRUD operations, and context menus.

**Key files:**
- `include/ui_spoolman_list_view.h` / `src/ui/ui_spoolman_list_view.cpp` -- Virtualized list with fixed widget pool (same pattern as PrintSelectListView)
- `include/ui_spoolman_context_menu.h` / `src/ui/ui_spoolman_context_menu.cpp` -- Context menu for spool actions (edit, delete, assign)
- `include/ui_spoolman_edit_modal.h` / `src/ui/ui_spoolman_edit_modal.cpp` -- Full edit modal with dirty tracking, formatted values, centered spool preview
- `include/spoolman_types.h` / `src/printer/spoolman_types.cpp` -- Spoolman data types extracted to standalone module
- `ui_xml/spoolman_context_menu.xml`, `ui_xml/spoolman_edit_modal.xml` -- New XML components
- `src/api/moonraker_api_advanced.cpp` -- Split filament/spool PATCH APIs, Spoolman weight refresh

### 9. Reusable UI Components

Several new shared components built for this branch:

**status_pill** -- Variant-based colored pill widget (success/warning/danger/info/muted):
- `include/ui_status_pill.h` / `src/ui/ui_status_pill.cpp` / `ui_xml/status_pill.xml`
- Used for AFC bypass sensor display (hardware=status_pill, virtual=toggle)

**ui_variant** -- Shared variant module extracted from icon widget:
- `include/ui_variant.h` / `src/ui/ui_variant.cpp`
- Maps variant strings to theme color tokens, used by status_pill and icon

**ui_context_menu** -- Generic context menu base class:
- `include/ui_context_menu.h` / `src/ui/ui_context_menu.cpp`
- Full-screen backdrop, smart positioning, async dismiss
- Subclassed by `AmsContextMenu` and `SpoolmanContextMenu`

**text_input clear button** -- Android-style X button built into text_input widget:
- `include/ui_text_input.h` / `src/ui/ui_text_input.cpp`
- Used in Spoolman search and history list search

**ui_button bind_text** -- Smart subject resolution with `@` prefix convention:
- `include/ui_button.h` / `src/ui/ui_button.cpp`
- `@subject_name` resolves to subject value, plain text used as literal

### 10. Keyboard Improvements

- Enter-to-next-field: pressing Enter advances focus to the next input field
- Multiline mode: newlines supported in multiline text areas
- `src/ui/ui_keyboard_manager.cpp`

### 11. i18n Sync

All user-facing strings wrapped with `lv_tr()`, including device management UI strings. Translation XML regenerated.

**Key files:**
- `ui_xml/translations/translations.xml` -- +4916 lines of translation entries
- `src/generated/lv_i18n_translations.c` -- Regenerated

## Architecture Changes

### New Patterns Introduced

1. **Shared detail helpers** (`ui_ams_detail.h`): Free functions that operate on `AmsDetailWidgets` struct, eliminating duplication between panels that embed the same slot grid component.

2. **Shared defaults modules** (`afc_defaults.h`, `hh_defaults.h`): Static data modules consumed by both real and mock backends. Real backends overlay dynamic config values; mock uses defaults directly.

3. **Generic context menu base** (`ui_context_menu.h`): Reusable backdrop + positioning + async-dismiss for popup menus. AMS and Spoolman context menus subclass this.

4. **Progressive disclosure overlay** (`ui_ams_device_section_detail_overlay.h`): Section list -> section detail -> individual actions. Renders buttons/toggles/sliders/dropdowns from `DeviceAction` metadata.

5. **Klipper config parser** (`klipper_config_parser.h`): Generic parser for Klipper INI-like config. Used by `AfcConfigManager` to read AFC config files for dynamic slider values.

6. **Variant module** (`ui_variant.h`): Maps variant strings (success/warning/danger/info/muted) to theme color tokens. Shared between status_pill and icon widgets.

### New Files (46 new source/header files)

| Category | New Files |
|----------|-----------|
| AMS UI | `ui_panel_ams_overview.{h,cpp}`, `ui_ams_detail.{h,cpp}`, `ui_ams_slot_layout.h`, `ui_system_path_canvas.{h,cpp}`, `ui_ams_device_section_detail_overlay.{h,cpp}` |
| AMS Backend | `afc_defaults.{h,cpp}`, `hh_defaults.{h,cpp}`, `afc_config_manager.{h,cpp}`, `klipper_config_parser.{h,cpp}`, `action_prompt_manager.{h,cpp}` |
| Spoolman | `ui_spoolman_list_view.{h,cpp}`, `ui_spoolman_context_menu.{h,cpp}`, `ui_spoolman_edit_modal.{h,cpp}`, `spoolman_types.{h,cpp}`, `moonraker_api_advanced.cpp`, `moonraker_api_mock.cpp` |
| Shared UI | `ui_context_menu.{h,cpp}`, `ui_status_pill.{h,cpp}`, `ui_variant.{h,cpp}` |
| XML | `ams_overview_panel.xml`, `ams_unit_card.xml`, `ams_unit_detail.xml`, `ams_loaded_card.xml`, `ams_device_section_detail.xml`, `status_pill.xml`, `spoolman_context_menu.xml`, `spoolman_edit_modal.xml` |

## Known Limitations & TODOs

### TODOs Left in Code

| File | Line | TODO |
|------|------|------|
| `src/ui/ui_panel_ams_overview.cpp` | 50 | Replace `MINI_BAR_HEIGHT_PX` compile-time constant with `theme_manager_get_spacing("ams_bars_height")` to use the responsive value from `globals.xml` |
| `src/ui/ui_panel_ams_overview.cpp` | 270 | Iterate all backends (0..backend_count) to aggregate units across multiple simultaneous AMS systems. Currently only queries backend 0 |
| `src/printer/ams_backend_happy_hare.cpp` | 34 | Detect from Happy Hare configuration if hardware bypass sensor is present |
| `src/printer/ams_backend_happy_hare.cpp` | 1039 | Map `gear_from_buffer_speed` to correct Happy Hare parameter name |
| `src/printer/ams_backend_afc.cpp` | 44 | Detect from AFC configuration whether bypass sensor is virtual or hardware |

### Features Deferred

1. **Multi-backend aggregation in overview panel**: The data layer supports multiple simultaneous backends (e.g., AFC + Happy Hare on different toolheads), but the overview panel currently only queries `get_backend(0)`. The per-backend slot subject storage and event routing are ready -- the UI aggregation loop is the remaining integration point.

2. **Visual verification of error indicators**: All error visualization code is implemented and unit-tested, but the visual smoke test checklist (in `docs/devel/plans/2026-02-15-error-state-visualization-design.md`) has not been run through manually.

3. **Spoolman edit/assign from overview detail**: The overview detail context menu shows "Use the AMS detail panel" info toasts for Edit and Spoolman actions instead of handling them inline. Full functionality requires navigating to the AMS detail panel.

4. **HH encoder/clog visualization**: Happy Hare has encoder-based clog detection, but it uses a different model than AFC buffer health. Left as `buffer_health = nullopt` for HH backends.

5. **Bypass sensor auto-detection**: Both AFC and Happy Hare backends have TODOs for detecting whether the bypass sensor is hardware or virtual from config. Currently assumes defaults.

## Testing

### New Test Files (11 files)

| Test File | Focus | Key Coverage |
|-----------|-------|-------------|
| `test_ams_multi_unit.cpp` | Multi-unit data model, unit creation, slot indexing, overview navigation | 883 lines |
| `test_error_state_visualization.cpp` | SlotError, BufferHealth, per-backend error population, mock simulation | 723 lines |
| `test_klipper_config_parser.cpp` | Klipper INI parsing: sections, keys, colon/equals, multi-line, roundtrip | 695 lines |
| `test_afc_device_actions_config.cpp` | Config-driven action values, slider ranges, section ordering | 570 lines |
| `test_ams_afc_multi_extruder.cpp` | AFC multi-extruder integration, tool-to-backend mapping | 517 lines |
| `test_afc_defaults.cpp` | Shared AFC sections, actions, capabilities | 258 lines |
| `test_afc_error_handling.cpp` | AFC error states, toast notifications, dedup | 259 lines |
| `test_afc_config_manager.cpp` | AFC config file fetching and parsing | 253 lines |
| `test_ams_hub_sensor.cpp` | Per-unit hub sensors, system path state | 277 lines |
| `test_hh_defaults.cpp` | Shared Happy Hare sections, actions | 140 lines |
| `test_spoolman_list_view.cpp` | Virtualized list, widget recycling | 190 lines |

### Modified Test Files

| Test File | Changes |
|-----------|---------|
| `test_spoolman.cpp` | +453 lines (context menu, edit modal, CRUD operations) |
| `test_ui_button.cpp` | +128 lines (bind_text, `@` prefix convention) |
| `test_ui_variant.cpp` | +43 lines (variant parsing, opacity) |
| `test_ui_theme.cpp` | +64 lines (breakpoint, header, arc resize tests) |
| `test_action_prompt.cpp` | +71 lines (action prompt dialog generation) |

### What's NOT Tested

- Visual layout/rendering (no screenshot regression tests yet)
- Error indicator visual appearance (documented in smoke test checklist)
- System path canvas drawing correctness (custom LVGL widget, hard to unit test)
- Zoom animation timing/smoothness

## Post-Merge Cleanup

1. **Archive completed plans**: Move `2026-02-14-mock-backend-dedup.md` and `2026-02-15-error-state-visualization-design.md` to `docs/archive/plans/`
2. **Run error visualization smoke test**: Walk through the visual verification checklist
3. **Update ROADMAP.md**: Mark multi-unit AMS as complete
4. **Consider removing cancel-escalation plans**: `docs/plans/2026-02-14-cancel-escalation-design.md` and `docs/plans/2026-02-14-cancel-escalation-plan.md` were deleted on this branch -- verify they're not needed on main

## Commit Log

Commits grouped by feature area (76 total):

### Multi-Unit AMS Overview (M1-M3 milestones)

| Commit | Description |
|--------|-------------|
| `74c31f6a` | **M1+M2**: Multi-unit data layer, overview panel, mock backend |
| `fb93176c` | **M3**: Per-unit hub sensors, two-column overview layout, system path canvas |
| `34dd8830` | Polish overview panel layout, bypass path, sensor dots, loaded card |
| `261b5f22` | Inline detail view with zoom transition, performance fixes, DRY refactor |
| `2f355788` | Detail view tray z-order, path canvas, zoom animation, hub-only mode |
| `e1c47db9` | Slot context menu in overview detail view |
| `e9690623` | Move tray behind slots for badge/halo visibility, merge slot info into section header |

### Shared Detail Components (DRY refactor)

| Commit | Description |
|--------|-------------|
| `d6c8c98a` | Shared `ams_unit_detail` XML component for spool grid + tray |
| `a77d835b` | Shared helper functions for spool grid rendering |
| `9076f917` | Shared `ams_loaded_card` XML component |
| `500818e1` | Wire AmsPanel to shared ams_unit_detail and ams_loaded_card |
| `3227cfba` | Wire AmsOverviewPanel to shared ams_unit_detail and ams_loaded_card |

### Error State Visualization

| Commit | Description |
|--------|-------------|
| `5e75848d` | Error state visualization design doc |
| `0d693084` | Per-slot error state and buffer health data model |
| `a1b8f8bb` | Error indicator and buffer health visualization in slot and overview views |
| `0f0fbab0` | Mock backend error/buffer health simulation for --test mode |
| `b88fe6fb` | Clear buffer fault warnings on recovery, clear HH reason_for_pause on idle |

### AFC Device Management

| Commit | Description |
|--------|-------------|
| `842c91a9` | Device management system design for AFC + Happy Hare |
| `4ee3d85c` | Device management implementation plan |
| `ca87886e` | Progressive disclosure overlay, AFC config manager, Klipper config parser |
| `a29d1680` | Use setting_action_row for section list, fix icons, add descriptions |
| `886e7d6f` | Rename "Device Operations" to "AMS Management", add system info line |
| `f8593727` | Rename calibration -> setup, fold LED, add hub/tip/purge/config defaults |
| `b20580b7` | AFC backend overlays dynamic values on shared defaults |
| `da54f064` | Wire up Happy Hare device management (sections, actions, G-code execution) |
| `53e48e6a` | Add Happy Hare defaults (setup, speed, maintenance) |

### Mock Backend Deduplication

| Commit | Description |
|--------|-------------|
| `5bd1664a` | Mock backend deduplication plan |
| `ded48b04` | Extract shared AFC defaults module |
| `0a1d2fdf` | Use shared defaults in AFC backend |
| `c9dde27e` | Use shared defaults in mock backend |
| `e88e2401` | Mock uses complete AFC defaults and HH defaults per mode |

### AFC Error Handling

| Commit | Description |
|--------|-------------|
| `33d930d7` | AFC error handling design |
| `c6ba1016` | AFC error message toast notifications with dedup and prompt suppression |
| `cb8210d4` | Address code review findings for error handling |

### Spoolman Panel

| Commit | Description |
|--------|-------------|
| `b5ab71ec` | Virtualized list view with search/filter |
| `c9c47f65` | Context menu, edit modal integration, and delete confirmation |
| `779f5ef4` | Split filament/spool PATCH APIs, context menu positioning, active indicators |
| `002d2632` | CRUD UI: context menu, edit modal, delete confirmation |
| `f7dc32f9` | Dirty tracking uses formatted values, center spool preview layout |
| `86574bd7` | Skip Spoolman weight refresh in mock mode |

### Status Pill & Variant Module

| Commit | Description |
|--------|-------------|
| `8e8ed26b` | status_pill component design |
| `277f7576` | Update status_pill design with implementation plan |
| `9a76ce56` | Extract shared variant module from icon widget |
| `f98f3eed` | Use shared variant module in icon widget |
| `aba8fe09` | status_pill component with variant-based coloring |
| `b77f6c55` | Variant parsing and opacity tests |
| `94d16615` | Register status_pill XML component |
| `d589c4a4` | Use status_pill for hardware bypass sensor, toggle for virtual |
| `4b3d6440` | Fix bypass row dual-display, section icons and descriptions |
| `4f89d48c` | Move section icons from backends to UI layer |

### UI Button & Text Input

| Commit | Description |
|--------|-------------|
| `ae0ae1b5` | ui_button smart bind_text resolves subjects or falls back to literal text |
| `17c00d17` | `@` prefix convention for subject binding in bind_text |
| `a6bebb5d` | Add `@` prefix to bind_text subject references in modal_dialog |
| `c282cb19` | Timelapse: use ui_button_set_text() instead of child-walking |
| `74a94046` | text_input: built-in Android-style clear button support |
| `7472ef65` | Use text_input clear button in Spoolman and history panels |

### Keyboard Improvements

| Commit | Description |
|--------|-------------|
| `8697a675` | Enter-to-next-field, multiline newlines, and edit modal improvements |

### i18n

| Commit | Description |
|--------|-------------|
| `aefe0a3a` | Wrap remaining user-facing strings with lv_tr() |
| `9ad7a578` | Wrap device management UI strings with lv_tr() |
| `71cdc5e9` | Sync translations, fix escape handling, add new translations |
| `7207990b` | Format translations.xml |

### Toast Notifications

| Commit | Description |
|--------|-------------|
| `592b2fe3` | Toast notifications for all device actions |

### Bug Fixes & Polish

| Commit | Description |
|--------|-------------|
| `dfb00e63` | Slider layout alignment and fire-on-release debouncing |
| `65000fe4` | Align slider labels with fixed width and add column gap in action rows |
| `5359c412` | Disable focus on close buttons in all dialogs to prevent list scroll |
| `00bf19f8` | Disable focus on modal and context menu buttons to prevent list scroll |
| `ae0ae1b5` | Update section icon mapping for setup rename |
| `27663236` | Mock calibration wizard generates action_prompt dialog |
| `d1e1df3e` | Update breakpoint/header/arc tests, fix AFC reset virtual, remove completed plan docs |

### Docs & Housekeeping

| Commit | Description |
|--------|-------------|
| `c1009a31` | Archive completed multi-backend and multi-extruder-temps plans, mark phases complete |
| `deaedd7c` | Add UI panels section to FILAMENT_MANAGEMENT.md, note multi-backend aggregation |
| `68f36a45` | Move plans to docs/devel/plans/, add error visualization section to filament docs |
| `293854f4` | Merge branch 'main' into feature/multi-unit-ams |
| `230e45a2` | Merge branch 'main' into feature/multi-unit-ams |
| `371b2789` | Merge branch 'main' into feature/multi-unit-ams |
