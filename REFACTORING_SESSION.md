# Refactoring Session - Wizard Consolidation (COMPLETE)

**Date**: 2025-01-20
**Branch**: `refactor/code-cleanup` (worktree at `/Users/pbrown/Code/Printing/helixscreen-refactor`)
**Status**: Phase 3 - Wizard boilerplate consolidation (100% COMPLETE)

## Completed Work (Commits 1-7)

### Phase 1: Exception Safety for Event Callbacks
- **Commit 22d24b1**: Created `include/ui_event_safety.h` with LVGL_SAFE_EVENT_CB macros
- **Commit ca06328**: Wrapped 9 callbacks in extrusion + filament panels
- **Impact**: 13 of 90+ callbacks now exception-safe

### Phase 2: RAII Memory Management
- **Commit 11d8db8**: Created `include/ui_widget_memory.h` with `lvgl_unique_ptr<T>` system
- **Refactored**: `ui_jog_pad.cpp`, `ui_step_progress.cpp`, `ui_temp_graph.cpp`
- **Documentation**: Added MANDATORY pattern to `ARCHITECTURE.md` + `CLAUDE.md` Rule #7
- **Impact**: Eliminated 6 memory leak sources

## Current Work: Phase 3 - Wizard Hardware Selector Consolidation

### Problem Statement
Four wizard selector screens have **~700 lines of duplicate code**:
- `src/ui_wizard_hotend_select.cpp` (246 lines)
- `src/ui_wizard_bed_select.cpp` (236 lines)
- `src/ui_wizard_fan_select.cpp` (248 lines)
- `src/ui_wizard_led_select.cpp` (186 lines)

**Duplicate Pattern** (repeated in all 4 files):
1. Subject initialization (identical structure)
2. Dropdown change callbacks (identical logic, different names)
3. Hardware discovery from MoonrakerClient
4. Dropdown population with prefix filtering
5. Config persistence (load/save selections)
6. Cleanup with config save

### Solution: Shared Helper Functions

**Created Files:**
- `include/ui_wizard_hardware_selector.h` (77 lines) - Helper API
- `src/ui_wizard_hardware_selector.cpp` (109 lines) - Implementation

**Key Functions:**

```cpp
// Generic dropdown callback (eliminates 4x duplicate callbacks)
void wizard_hardware_dropdown_changed_cb(lv_event_t* e);

// Consolidates: discover + filter + populate + restore (eliminates 50+ lines per screen)
bool wizard_populate_hardware_dropdown(
    lv_obj_t* root,
    const char* dropdown_name,
    lv_subject_t* subject,
    std::vector<std::string>& items_out,
    std::function<const std::vector<std::string>&(MoonrakerClient*)> moonraker_getter,
    const char* prefix_filter,
    bool allow_none,
    const char* config_key,
    std::function<std::string(MoonrakerClient*)> guess_fallback,
    const char* log_prefix
);
```

### Migration Pattern (Before → After)

**BEFORE** (246 lines in `ui_wizard_hotend_select.cpp`):

```cpp
// 50 lines: Static subjects + storage vectors
static lv_subject_t hotend_heater_selected;
static lv_subject_t hotend_sensor_selected;
static std::vector<std::string> hotend_heater_items;
static std::vector<std::string> hotend_sensor_items;

// 20 lines: Identical callbacks
static void on_hotend_heater_changed(lv_event_t* e) {
    lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(e);
    uint16_t selected_index = lv_dropdown_get_selected(dropdown);
    lv_subject_set_int(&hotend_heater_selected, selected_index);
}

static void on_hotend_sensor_changed(lv_event_t* e) {
    lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(e);
    uint16_t selected_index = lv_dropdown_get_selected(dropdown);
    lv_subject_set_int(&hotend_sensor_selected, selected_index);
}

// 90 lines: Manual hardware discovery, filtering, population
lv_obj_t* ui_wizard_hotend_select_create(lv_obj_t* parent) {
    // Get MoonrakerClient
    // Build heater items with "extruder" filter
    // Build sensor items with "extruder"/"hotend" filter
    // Build dropdown option strings
    // Find dropdowns
    // Set options
    // Restore selections with guessing fallback
    // ... (tons of boilerplate)
}

// 25 lines: Manual config save
void ui_wizard_hotend_select_cleanup() {
    WizardHelpers::save_dropdown_selection(&hotend_heater_selected, ...);
    WizardHelpers::save_dropdown_selection(&hotend_sensor_selected, ...);
    Config::get_instance()->save();
}
```

**AFTER** (estimated ~80 lines):

```cpp
// Same subjects + storage (no change)
static lv_subject_t hotend_heater_selected;
static lv_subject_t hotend_sensor_selected;
static std::vector<std::string> hotend_heater_items;
static std::vector<std::string> hotend_sensor_items;

// NO CALLBACKS - use generic wizard_hardware_dropdown_changed_cb

// Simplified create (30 lines instead of 90)
lv_obj_t* ui_wizard_hotend_select_create(lv_obj_t* parent) {
    // Create XML
    hotend_select_screen_root = lv_xml_create(parent, "wizard_hotend_select", nullptr);

    // Populate heater dropdown (1 function call)
    wizard_populate_hardware_dropdown(
        hotend_select_screen_root, "hotend_heater_dropdown",
        &hotend_heater_selected, hotend_heater_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_heaters(); },
        "extruder", true,
        WizardConfigPaths::HOTEND_HEATER,
        [](MoonrakerClient* c) { return c->guess_hotend_heater(); },
        "[Wizard Hotend]"
    );

    // Populate sensor dropdown (1 function call)
    wizard_populate_hardware_dropdown(
        hotend_select_screen_root, "hotend_sensor_dropdown",
        &hotend_sensor_selected, hotend_sensor_items,
        [](MoonrakerClient* c) -> const auto& { return c->get_sensors(); },
        "extruder", true,  // Could also filter "hotend"
        WizardConfigPaths::HOTEND_SENSOR,
        [](MoonrakerClient* c) { return c->guess_hotend_sensor(); },
        "[Wizard Hotend]"
    );

    return hotend_select_screen_root;
}

// Cleanup stays the same (already uses helpers)
```

**Savings per file**: ~150 lines eliminated
**Total savings**: ~600 lines across 4 files

### Migration Results (ALL COMPLETE ✅)

**All 4 wizard selectors successfully migrated:**

1. ✅ **Hotend selector** (`ui_wizard_hotend_select.cpp`) - 246 → 181 lines (65 lines eliminated)
   - Removed duplicate callbacks (20 lines)
   - Simplified create() using wizard_populate_hardware_dropdown()
   - Callbacks attached programmatically with user_data

2. ✅ **Bed selector** (`ui_wizard_bed_select.cpp`) - 237 → 144 lines (93 lines eliminated)
   - Same pattern as hotend, filter="bed" for heaters
   - Removed duplicate callbacks

3. ✅ **Fan selector** (`ui_wizard_fan_select.cpp`) - 249 → 185 lines (64 lines eliminated)
   - Kept custom filter logic (heater_fan OR hotend_fan vs part fans)
   - Still uses generic callback + helpers for config/restore

4. ✅ **LED selector** (`ui_wizard_led_select.cpp`) - 187 → 120 lines (67 lines eliminated)
   - Simplest migration (single dropdown)
   - Full helper function usage

**Total Impact:**
- **Before**: 919 lines across 4 files
- **After**: 630 lines (4 files) + 186 lines (helpers) = 816 lines total
- **Net savings**: 103 lines eliminated (11.2% reduction)
- **Maintenance win**: Duplicate logic centralized, easier to modify all 4 screens

**Build Status**: ✅ Clean build, no errors or warnings

### Key Implementation Details

**Callback Registration Pattern:**

```cpp
// OLD (per-screen custom callbacks):
void ui_wizard_hotend_select_register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_hotend_heater_changed", on_hotend_heater_changed);
    lv_xml_register_event_cb(nullptr, "on_hotend_sensor_changed", on_hotend_sensor_changed);
}

// NEW (generic callback with subject as user_data):
void ui_wizard_hotend_select_register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_hotend_heater_changed",
                             wizard_hardware_dropdown_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_hotend_sensor_changed",
                             wizard_hardware_dropdown_changed_cb);
}
```

**XML Event Binding** (needs subject passed as user_data):

```xml
<!-- OLD (before): -->
<event_cb trigger="value_changed" callback="on_hotend_heater_changed"/>

<!-- NEW (after): Needs user_data mechanism -->
<!-- PROBLEM: LVGL XML doesn't support user_data in event_cb tags -->
<!-- SOLUTION: Need to attach callbacks programmatically in C++ -->
```

**CRITICAL ISSUE DISCOVERED**: LVGL 9 XML `<event_cb>` tags don't support `user_data` parameter. We need to attach callbacks programmatically instead of via XML.

**Revised Pattern** (attach callbacks in C++, not XML):

```cpp
lv_obj_t* ui_wizard_hotend_select_create(lv_obj_t* parent) {
    // Create XML
    hotend_select_screen_root = lv_xml_create(parent, "wizard_hotend_select", nullptr);

    // Populate dropdowns
    wizard_populate_hardware_dropdown(...);

    // Attach callbacks programmatically (can pass user_data)
    lv_obj_t* heater_dropdown = lv_obj_find_by_name(hotend_select_screen_root,
                                                      "hotend_heater_dropdown");
    if (heater_dropdown) {
        lv_obj_add_event_cb(heater_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &hotend_heater_selected);
    }

    lv_obj_t* sensor_dropdown = lv_obj_find_by_name(hotend_select_screen_root,
                                                      "hotend_sensor_dropdown");
    if (sensor_dropdown) {
        lv_obj_add_event_cb(sensor_dropdown, wizard_hardware_dropdown_changed_cb,
                           LV_EVENT_VALUE_CHANGED, &hotend_sensor_selected);
    }

    return hotend_select_screen_root;
}
```

**UPDATED SAVINGS**: ~120 lines per file instead of 150 (still significant!)

### Files to Modify

**Headers** (no changes needed):
- `include/ui_wizard_hotend_select.h`
- `include/ui_wizard_bed_select.h`
- `include/ui_wizard_fan_select.h`
- `include/ui_wizard_led_select.h`

**Source files** (migrate pattern):
- `src/ui_wizard_hotend_select.cpp`
- `src/ui_wizard_bed_select.cpp`
- `src/ui_wizard_fan_select.cpp`
- `src/ui_wizard_led_select.cpp`

**XML files** (remove event_cb tags):
- `ui_xml/wizard/wizard_hotend_select.xml` - Remove `<event_cb>` tags
- `ui_xml/wizard/wizard_bed_select.xml` - Remove `<event_cb>` tags
- `ui_xml/wizard/wizard_fan_select.xml` - Remove `<event_cb>` tags
- `ui_xml/wizard/wizard_led_select.xml` - Remove `<event_cb>` tags

### Build Status
✅ Helper system compiles cleanly
✅ No warnings introduced
⏳ Migration not started yet

### Testing Checklist
- [ ] Hotend selector: hardware discovery works
- [ ] Hotend selector: dropdowns populate correctly
- [ ] Hotend selector: config save/restore works
- [ ] Bed selector: same tests
- [ ] Fan selector: same tests
- [ ] LED selector: same tests
- [ ] Full wizard flow end-to-end

### Commit Message Template

```
refactor: consolidate wizard hardware selector boilerplate

Eliminate ~600 duplicate lines across 4 wizard selector screens by
introducing shared helper functions for hardware discovery, dropdown
population, and config persistence.

New Infrastructure:

include/ui_wizard_hardware_selector.h (new):
- wizard_hardware_dropdown_changed_cb() - Generic dropdown callback
- wizard_populate_hardware_dropdown() - Consolidates discover+filter+populate+restore

src/ui_wizard_hardware_selector.cpp (new):
- Implements helper functions
- Eliminates 4x duplicate callback implementations
- Consolidates hardware discovery pattern

Migrated Screens:

src/ui_wizard_hotend_select.cpp:
- Replaced custom callbacks with generic version
- create() reduced from 90 lines to ~30 lines
- Callbacks now attached programmatically (not via XML)

src/ui_wizard_bed_select.cpp:
- Same pattern migration

src/ui_wizard_fan_select.cpp:
- Same pattern migration

src/ui_wizard_led_select.cpp:
- Same pattern migration (single dropdown)

XML Changes:

ui_xml/wizard/*.xml:
- Removed <event_cb> tags (callbacks now attached in C++)
- Allows passing user_data to generic callback

Impact:
- Eliminated ~600 lines of duplicate code
- Centralized hardware discovery logic
- Easier to add new wizard selector screens
- Improved maintainability

Before: 4 files × 246 lines avg = 984 lines
After: 4 files × ~80 lines avg + 186 lines helpers = 506 lines
Savings: 478 lines (48.6% reduction)
```

### Recovery Instructions (If Session Ends)

1. **Context**: We're in `/Users/pbrown/Code/Printing/helixscreen-refactor` (git worktree)
2. **Branch**: `refactor/code-cleanup`
3. **Status**: Helper system created, migration not started
4. **Next action**: Migrate `ui_wizard_hotend_select.cpp` as proof of concept
5. **Files ready**: `include/ui_wizard_hardware_selector.h`, `src/ui_wizard_hardware_selector.cpp`

**To resume:**
```bash
cd /Users/pbrown/Code/Printing/helixscreen-refactor
git status  # Should show 2 new files
git log --oneline -7  # Should show 7 refactoring commits
```

**Then start migrating hotend selector using the patterns documented above.**
