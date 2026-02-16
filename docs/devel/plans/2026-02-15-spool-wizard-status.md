# Spool Wizard — Implementation Status

**Date**: 2026-02-15
**Branch**: `feature/spool-wizard-v2` (worktree: `.worktrees/spool-wizard-v2`)
**Based on**: `feature/multi-unit-ams` (76 commits ahead of `main`)
**Phase**: Phase 3 of Spoolman Management design (`2026-02-09-spoolman-management-design.md`)

---

## Branch History

The wizard was originally built on `feature/spool-wizard` branched from `main` (10 commits). This was a mistake — it missed all the Spoolman infrastructure built in `feature/multi-unit-ams` (VendorInfo, FilamentInfo, SpoolInfo types, callback types, existing CRUD methods, SpoolmanPanel overlay, context menu, edit modal, etc.).

**`feature/spool-wizard-v2`** was created from `feature/multi-unit-ams` HEAD and the wizard-specific files were ported over, adapted to use the v2 types (`FilamentInfo.display_name()` instead of `.name`, `float` types instead of `double`, etc.).

**The old `feature/spool-wizard` branch can be deleted.** All its work is now in `feature/spool-wizard-v2`.

---

## What's Built (Committed: `29231b75`)

### Files Added (6 new)
| File | Lines | Purpose |
|------|-------|---------|
| `include/ui_spool_wizard.h` | 352 | SpoolWizardOverlay class — 3-step wizard state machine |
| `src/ui/ui_spool_wizard.cpp` | ~1500 | Full wizard implementation: vendor/filament steps, creation chain |
| `tests/unit/test_spool_wizard.cpp` | ~780 | 46 test cases, 144 assertions (all passing) |
| `ui_xml/spool_wizard.xml` | 356 | Full 3-step wizard XML layout with subject bindings |
| `ui_xml/wizard_vendor_row.xml` | 18 | Vendor list row template (name + source badge area) |
| `ui_xml/wizard_filament_row.xml` | 28 | Filament list row (color swatch + material + name + temps) |

### Files Modified (8)
| File | Changes |
|------|---------|
| `include/moonraker_api.h` | +5 new virtual methods (external vendors/filaments, vendor-filtered filaments, delete vendor/filament) |
| `include/moonraker_api_mock.h` | +5 mock overrides |
| `src/api/moonraker_api_advanced.cpp` | +146 lines: implementations via `server.spoolman.proxy` RPC |
| `src/api/moonraker_api_mock.cpp` | +147 lines: mock data for Hatchbox/Polymaker/eSUN/Prusament vendors + PLA/PETG filaments |
| `include/ui_panel_spoolman.h` | +SpoolWizardOverlay include, `wizard_panel_` member, `on_add_spool_clicked` callback |
| `src/ui/ui_panel_spoolman.cpp` | +`on_add_spool_clicked` → lazy_create_and_push_overlay with completion callback for refresh |
| `ui_xml/spoolman_panel.xml` | +action_button_2 ("+" button with primary bg) alongside refresh button |
| `src/xml_registration.cpp` | +3 component registrations (wizard_vendor_row, wizard_filament_row, spool_wizard) |

### Architecture
- **SpoolWizardOverlay** extends `OverlayBase`, uses `DEFINE_GLOBAL_PANEL` singleton pattern
- **3-step flow**: Vendor (Step 0) → Filament (Step 1) → Spool Details (Step 2)
- **14 LVGL subjects** drive all UI state (step visibility, button states, labels, counts, loading states)
- **Declarative UI**: Step visibility via `bind_flag_if_not_eq`, button gating via subjects, text via `bind_text`
- **Imperative exceptions**: Dynamic list row population, color swatch styling (both accepted exceptions per CLAUDE.md)
- **Async data loading**: Dual-source merge (Spoolman server + SpoolmanDB external API) with `std::atomic<int>` coordination
- **Atomic creation chain**: vendor → filament → spool with best-effort rollback on failure
- **Thread safety**: All async callbacks wrapped in `ui_queue_update()` for LVGL thread marshaling

### New API Methods
| Method | Endpoint | Purpose |
|--------|----------|---------|
| `get_spoolman_external_vendors()` | GET `/v1/external/vendor` | SpoolmanDB vendor catalog |
| `get_spoolman_external_filaments(vendor_name)` | GET `/v1/external/filament?vendor_name=...` | SpoolmanDB filaments by vendor |
| `get_spoolman_filaments(vendor_id)` | GET `/v1/filament?vendor_id=...` | Server filaments filtered by vendor |
| `delete_spoolman_vendor(id)` | DELETE `/v1/vendor/{id}` | Rollback support |
| `delete_spoolman_filament(id)` | DELETE `/v1/filament/{id}` | Rollback support |

### Test Coverage
- Step navigation (forward, back, boundaries, labels)
- Vendor merge/dedup (server priority, DB-only, case-insensitive)
- Vendor filtering (substring, empty query, no matches)
- Vendor selection (existing + new vendor creation)
- Filament merge/dedup (material+color key, server priority)
- Filament selection (existing + new with material auto-fill)
- Material auto-fill from compiled-in filament_database.h
- Spool details pre-fill and validation
- Create request lifecycle (no-API path, completion callback)
- Edge cases (empty inputs, invalid indices, default state)

---

## Known Bugs (from Code Review — P0/P1)

These were identified during review but **not yet fixed**:

| Priority | Issue | Description |
|----------|-------|-------------|
| **P0** | No error toast | `on_creation_error()` logs but never shows the user why creation failed |
| **P0** | Wizard stays open on success | `on_creation_success()` fires completion callback but doesn't `ui_nav_go_back()` |
| **P0** | Silent API response swallow | `create_spoolman_vendor/filament/spool` success callbacks have no else-branch — if response format is unexpected, neither `on_success` nor `on_error` fires; wizard hangs in "creating" state |
| **P1** | Incomplete state reset | `on_activate()` resets vendor state but not filament/spool state — reopening wizard leaks prior session's filament selection, spool weight, etc. |
| **P1** | Creation callbacks on deactivated overlay | If user navigates away mid-creation, `ui_queue_update` callbacks still fire and mutate state on a deactivated overlay |

### Code Quality Notes (from review, non-blocking)
- Hardcoded colors in 3 places (color swatches — acceptable exception)
- `lv_obj_add_event_cb` for scroll (pre-existing in SpoolmanPanel)
- No test coverage for creation chain (would need mock API injection)
- Inconsistent log tag format (static callbacks use `[SpoolWizard]`, instance methods use `get_name()`)
- No input length validation on vendor name/URL

---

## What's NOT Built Yet

### Remaining Plan Steps

| Step | Status | Description |
|------|--------|-------------|
| P0 Bug Fixes | **NOT STARTED** | Fix 3 P0 bugs + 2 P1 issues from review above |
| Step 9: filamentcolors.xyz | **NOT STARTED** | Optional network color enhancement — query `filamentcolors.xyz/api/swatch/` for better color names, in-memory cache, graceful degradation |
| Creation chain tests | **NOT STARTED** | Mock API injection to test vendor→filament→spool chain, partial failure rollback |

### Visual Test Plan (NOT STARTED)

All 10 visual tests require running `./build/bin/helix-screen --test -vv` and manually verifying UI behavior.

#### VT-1: Launch Wizard from SpoolmanPanel
1. Navigate to Spoolman panel overlay
2. Verify "+" button visible in header bar (alongside refresh button)
3. Tap "+" button
4. Verify wizard overlay slides in showing Step 1 (Vendor)
5. Verify step indicator shows "Step 1 of 3"
6. Verify Next button is disabled (no vendor selected)
7. Verify Back button is visible

#### VT-2: Vendor Search and Selection
1. On Step 1, verify vendor list populates (mock: Hatchbox, Polymaker, eSUN, Prusament from external + server vendors merged)
2. Type "hatch" in search box — verify list filters to matching vendors
3. Tap a vendor row — verify highlight/selection, Next button enables
4. Clear search — verify full list returns
5. Try "Create New Vendor" toggle — verify form expands with name and URL fields

#### VT-3: Create New Vendor Flow
1. Toggle "Create New Vendor"
2. Fill in vendor name (e.g., "Test Vendor Co")
3. Verify "Use" button enables when name non-empty
4. Tap "Use" — verify form collapses, Next enables, selected vendor name displayed
5. Tap Next to proceed to Step 2

#### VT-4: Filament Step with Existing Vendor
1. After selecting existing vendor with filaments
2. Step 2 shows: selected vendor name at top, filament list with color swatches + material + temp range
3. Tap a filament row — verify selection, Next button enables
4. Verify Back goes to Step 1 (vendor selection preserved)

#### VT-5: Create New Filament with Material Auto-Fill
1. On Step 2, toggle "Create New Filament"
2. Select material from dropdown (e.g., PLA)
3. Verify temperature fields auto-fill (190-220 nozzle, 50-60 bed)
4. Tap color picker button — verify ColorPicker modal opens
5. Pick a color — verify swatch updates
6. Verify "Confirm" enables when material + color set

#### VT-6: Spool Details Step and Pre-Fill
1. After selecting filament, tap Next to Step 3
2. Verify summary card shows vendor name + filament info
3. Verify summary color swatch shows selected filament color
4. Verify remaining weight pre-filled from filament net weight
5. Verify price, lot, notes fields are editable
6. Verify "Create Spool" button is enabled (weight > 0)

#### VT-7: Full Creation Flow (Existing Vendor + Existing Filament)
1. Select existing vendor → select existing filament → fill spool details → "Create Spool"
2. Verify creating spinner appears
3. Verify wizard closes on success (**currently broken — P0 bug**)
4. Verify SpoolmanPanel refreshes spool list
5. Verify new spool appears in list

#### VT-8: Full Creation Flow (New Vendor + New Filament)
1. Create new vendor → create new filament (custom material/color/temps) → fill spool details → "Create Spool"
2. Verify atomic creation chain: vendor created, then filament, then spool
3. Verify spinner shown during creation
4. Verify success closes wizard (**currently broken — P0 bug**)

#### VT-9: Back Navigation Preserves State
1. Fill Step 1 (select vendor), go to Step 2, Back to Step 1 — vendor still selected
2. From Step 2, go to Step 3, Back — filament selection preserved
3. From Step 1, Back should close/pop the wizard overlay

#### VT-10: Error Handling and Edge Cases
1. Empty vendor list (no Spoolman configured) — verify empty state or helpful message
2. Vendor with no filaments — verify empty filament list with create option visible
3. Clear remaining weight to 0 — verify Create button disables
4. Simulate network error mid-creation — verify spinner stops (**currently broken — P0: no error toast**)

---

## Design Decisions Made

| Decision | Rationale |
|----------|-----------|
| Panel-width overlay (not modal) | Consistent with SpoolmanPanel pattern; more room for lists |
| 3-step wizard (not single form) | Reduces cognitive load; allows vendor/filament reuse |
| SpoolmanDB via external API | Spoolman server already has `/v1/external/vendor` and `/v1/external/filament` endpoints — no need to bundle the 4MB JSON |
| Dual-source merge | Shows both what's already on server AND what's available in the database |
| Atomic creation with rollback | If filament creation fails after vendor was created, attempt to delete the vendor |
| `DEFINE_GLOBAL_PANEL` singleton | Consistent with all other overlays; static callbacks access global instance |
| Subject-driven step visibility | XML `bind_flag_if_not_eq` on step index — no imperative `lv_obj_add_flag(HIDDEN)` |

---

## Future Work (Beyond Current Scope)

From the original Spoolman Management design (`2026-02-09`):

| Feature | Status |
|---------|--------|
| Spool CRUD (browse, edit, delete) | **Done** (in multi-unit-ams, SpoolmanPanel) |
| New Spool wizard | **In progress** (this branch) |
| QR code label printing | **Not started** |
| USB barcode scanner input | **Not started** |
| Shared reusable components | **Done** (ams_unit_detail, ams_loaded_card, etc.) |

---

## How to Test

```bash
cd .worktrees/spool-wizard-v2

# Build
make -j

# Run wizard unit tests
./build/bin/helix-tests "[spool_wizard]"
# Expected: 144 assertions in 46 test cases — all pass

# Visual testing (mock printer mode)
./build/bin/helix-screen --test -vv
# Navigate: Home → Filament → Spoolman → tap "+" button
```

---

## File Map

```
include/
  ui_spool_wizard.h          ← Wizard class (339 lines)
  moonraker_api.h            ← +5 methods
  moonraker_api_mock.h       ← +5 mock overrides
  ui_panel_spoolman.h        ← +wizard_panel_ member, +on_add_spool_clicked

src/
  ui/ui_spool_wizard.cpp     ← Wizard implementation (~1500 lines)
  ui/ui_panel_spoolman.cpp   ← +on_add_spool_clicked callback
  api/moonraker_api_advanced.cpp  ← +5 API implementations
  api/moonraker_api_mock.cpp      ← +5 mock implementations
  xml_registration.cpp       ← +3 component registrations

ui_xml/
  spool_wizard.xml           ← 3-step wizard layout (356 lines)
  wizard_vendor_row.xml      ← Vendor row template
  wizard_filament_row.xml    ← Filament row template
  spoolman_panel.xml         ← +action_button_2 ("+" button)

tests/
  unit/test_spool_wizard.cpp ← 46 tests, 144 assertions
```
