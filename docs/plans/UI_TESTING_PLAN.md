# UI Testing Plan

## Overview

This document captures the comprehensive plan for UI component testing in HelixScreen, including infrastructure built, tests identified, and technical knowledge gained.

**Status**: Phase 2 Complete - Infrastructure Ready
**Last Updated**: 2026-01-07
**Tests Implemented**: 5 (temp_display binding tests)
**Tests Scaffolded**: 38 (skipped, awaiting component registration)
**Tests Identified**: 200+ (documented below)

---

## Infrastructure Built

### XMLTestFixture

A test fixture that enables real XML component testing with proper theme initialization.

**Location**: `tests/test_fixtures.h`, `tests/test_fixtures.cpp`

**Capabilities**:
- Full theme initialization (light mode for consistent screenshots)
- Font registration via `AssetManager::register_all()`
- Subject registration for XML bindings
- Custom widget registration (temp_display, ui_card, ui_icon, ui_text)
- Component loading via `register_component()` and `create_component()`

**Usage**:
```cpp
TEST_CASE_METHOD(XMLTestFixture, "my test", "[ui][component]") {
    // Set state BEFORE creating component
    state().set_extruder_temp(20000);  // 200°C in centidegrees

    // Register and create component
    REQUIRE(register_component("temp_display"));

    const char* attrs[] = {"bind_current", "extruder_temp", nullptr};
    lv_obj_t* widget = create_component("temp_display", attrs);
    REQUIRE(widget != nullptr);

    // Verify binding
    REQUIRE(ui_temp_display_get_current(widget) == 200);
}
```

### Key Technical Discoveries

1. **Theme Initialization Order**
   `ui_theme_init()` hangs if called when screens exist. Solution: delete test screen → init theme → recreate screen.

2. **Subject Binding is Synchronous**
   When subjects change via `lv_subject_set_int()`, observer callbacks fire immediately. No `process_lvgl()` needed for simple binding tests.

3. **Temperature Units**
   All temperatures in centidegrees: `20000` = 200.00°C

4. **Widget Props via Attrs**
   XML component creation accepts `const char** attrs` as key-value pairs terminated by nullptr.

---

## Component Inventory

### Panels (25 full-screen views)

| Panel | Lines | Priority | Key Bindings |
|-------|-------|----------|--------------|
| `print_status_panel` | 2,913 | P1 | print_progress, temps, layers, elapsed/remaining |
| `print_select_panel` | 2,067 | P1 | view_mode, file list |
| `settings_panel` | 1,996 | P2 | toggles, dropdowns |
| `controls_panel` | 1,654 | P1 | positions, homing, speed/flow |
| `home_panel` | 1,326 | P1 | status, temps, network, notifications |
| `ams_panel` | 1,400 | P2 | slots, tool, filament path |
| `history_list_panel` | 1,400 | P3 | print history |
| `bed_mesh_panel` | 1,228 | P3 | mesh visualization |
| `filament_panel` | 1,159 | P2 | filament sensors, presets |
| `calibration_pid_panel` | 1,092 | P3 | PID tuning state machine |
| `history_dashboard_panel` | 887 | P3 | stats, charts |
| `gcode_test_panel` | 816 | P4 | debug only |
| `screws_tilt_panel` | 743 | P3 | calibration |
| Other panels | <700 | P3-P4 | Various |

### Modals & Dialogs (25+)

| Modal | Purpose | State Complexity |
|-------|---------|------------------|
| `abort_progress_modal` | Print cancellation | 7-state machine |
| `ams_edit_modal` | AMS slot config | Multi-field form |
| `wifi_password_modal` | Network auth | Text input |
| `print_cancel_confirm` | Cancel confirmation | Simple Y/N |
| `save_z_offset_modal` | Z-offset save | Value display |
| `exclude_object_modal` | Object exclusion | Undo timer |
| `runout_guidance_modal` | Filament runout | Action buttons |
| `bed_mesh_calibrate_modal` | Mesh cal progress | State tracking |

### Wizard Steps (9)

| Step | Order | Conditional |
|------|-------|-------------|
| `wizard_wifi` | 1 | No |
| `wizard_connection` | 2 | No |
| `wizard_printer_id` | 3 | No |
| `wizard_heater_select` | 4 | No |
| `wizard_fan_select` | 5 | Yes (if fans) |
| `wizard_led_select` | 7 | Yes (if LEDs) |
| `wizard_filament_sensor` | 8 | Yes (if sensors) |
| `wizard_ams_identify` | 6 | Yes (if AMS) |
| `wizard_summary` | 9 | No |

### Custom C++ Widgets (16)

| Widget | Lines | Testable API |
|--------|-------|--------------|
| `GcodeViewer` | 2,079 | Rendering, object detection |
| `FilamentPathCanvas` | 1,395 | Path visualization |
| `AmsSlot` | 1,092 | Slot state display |
| `TempGraph` | 1,041 | Temperature history |
| `TempDisplay` | ~300 | `get_current()`, `get_target()` |
| `TextInput` | ~500 | Value, validation |
| `Switch` | ~200 | Checked state |
| `Icon` | ~150 | Symbol display |

---

## Test Categories

### 1. Binding Tests (144 identified)

Tests that verify subject → UI binding works correctly.

**Pattern**:
```cpp
TEST_CASE_METHOD(XMLTestFixture, "widget: subject binding", "[ui][bind_text]") {
    state().set_some_value(expected);
    auto* widget = create_component("widget_name");
    REQUIRE(get_displayed_value(widget) == expected);
}
```

**By Panel**:

| Panel | Bindings | Status |
|-------|----------|--------|
| `home_panel` | 58 | 13 scaffolded |
| `controls_panel` | 49 | 14 scaffolded |
| `print_status_panel` | 29 | 9 scaffolded |
| `nozzle_temp_panel` | 3 | 2 implemented |
| `bed_temp_panel` | 3 | 2 implemented |
| `print_select_panel` | 2 | 0 scaffolded |
| **Total** | **144** | **5 implemented, 38 scaffolded** |

### 2. Interaction Tests

Tests that simulate user actions (clicks, typing, navigation).

**Priority Interactions**:
- [ ] Button clicks → state changes
- [ ] Dropdown selection → subject updates
- [ ] Slider drag → value changes
- [ ] Modal show/dismiss flows
- [ ] Panel navigation (tabs, overlays)
- [ ] Long-press actions (exclude object)

### 3. Workflow Integration Tests

End-to-end user journey tests.

| Workflow | Steps | Priority |
|----------|-------|----------|
| Print start | Select file → confirm → monitor | P1 |
| Print cancel | Click cancel → confirm → abort states | P1 |
| Temperature set | Open panel → select preset → monitor heating | P2 |
| Z-offset cal | Start → adjust → accept → save | P2 |
| Wizard flow | 9 steps with conditional skipping | P2 |
| Connection | Discover → select → test → connect | P2 |
| Filament runout | Pause → modal → load → resume | P3 |
| Exclude object | Long-press → confirm → undo window | P3 |

### 4. State Machine Tests

Tests for components with complex state transitions.

| Component | States | Transitions |
|-----------|--------|-------------|
| `AbortManager` | 7 | IDLE → TRY_HEATER → PROBE_QUEUE → SENT_CANCEL → COMPLETE |
| `ZOffsetPanel` | 7 | IDLE → PROBING → ADJUSTING → SAVING → COMPLETE |
| `WizardContainer` | 9 | Step 1 → ... → Step 9 with conditional skips |
| `ConnectionState` | 3 | DISCONNECTED → CONNECTING → CONNECTED |

### 5. Error Handling Tests

| Scenario | Expected Behavior |
|----------|-------------------|
| Connection lost during print | Show reconnecting, retry |
| Klippy shutdown | Show restart button |
| Print failed | Show error message, options |
| Filament runout | Show guidance modal |
| AMS jam | Show error with slot info |
| Probe failed | Show retry option |

---

## Implementation Phases

### Phase 1: Infrastructure ✅ COMPLETE
- [x] Create XMLTestFixture
- [x] Fix theme initialization hang
- [x] Implement temp_display binding tests
- [x] Document test patterns

### Phase 2: Core Panel Bindings (Next)
- [ ] Implement home_panel binding tests
- [ ] Implement controls_panel binding tests
- [ ] Implement print_status_panel binding tests
- [ ] Register all panel dependencies

### Phase 3: Interaction Tests
- [ ] Add click simulation tests
- [ ] Add navigation tests
- [ ] Add modal show/dismiss tests

### Phase 4: Workflow Tests
- [ ] Print workflow (select → start → monitor)
- [ ] Cancel workflow (abort state machine)
- [ ] Temperature workflow

### Phase 5: Error Handling
- [ ] Connection loss scenarios
- [ ] Klippy state changes
- [ ] Runout handling

---

## Technical Notes

### Component Registration Order

Components must be registered in dependency order:

```cpp
// 1. Fonts (required before theme)
AssetManager::register_all();

// 2. globals.xml (required for constants)
lv_xml_register_component_from_file("A:ui_xml/globals.xml");

// 3. Theme (uses globals constants)
ui_theme_init(display, false);

// 4. Base widgets
ui_icon_register_widget();
ui_text_init();
ui_card_register();
ui_temp_display_init();

// 5. Components that use base widgets
register_component("header_bar");
register_component("temp_display");
// etc.

// 6. Subjects (before component creation)
state().init_subjects(true);  // true = register with XML

// 7. Create components
lv_xml_create(parent, "component_name", attrs);
```

### No-op Callback Registration

XML components with optional callbacks need no-op handlers:

```cpp
lv_xml_register_event_cb(nullptr, "", xml_test_noop_event_callback);
lv_xml_register_event_cb(nullptr, "on_nozzle_preset_pla_clicked", xml_test_noop_event_callback);
// ... register all optional callbacks
```

### Subject Access Patterns

```cpp
// Get subject pointer for direct manipulation
lv_subject_t* subj = state().get_extruder_temp_subject();
lv_subject_set_int(subj, 20000);

// Use setter methods (preferred - handles thread safety)
state().set_extruder_temp(20000);

// Read current value
int32_t temp = lv_subject_get_int(subj);
```

### Widget Value Verification

```cpp
// Custom widgets expose getter functions
int current = ui_temp_display_get_current(widget);
int target = ui_temp_display_get_target(widget);

// For labels, use UITest helpers
std::string text = UITest::get_text(label);

// For visibility
bool visible = UITest::is_visible(widget);

// Find by name
lv_obj_t* child = UITest::find_by_name(parent, "child_name");
```

---

## Run Commands

```bash
# Build tests
make test

# Run all UI tests
./build/bin/helix-tests "[ui]"

# Run specific component tests
./build/bin/helix-tests "[temp_display]"
./build/bin/helix-tests "[home_panel]"

# Run binding tests by type
./build/bin/helix-tests "[bind_text]"
./build/bin/helix-tests "[bind_value]"
./build/bin/helix-tests "[bind_flag]"

# Run XMLTestFixture tests
./build/bin/helix-tests "[fixture][xml]"

# List all UI tests
./build/bin/helix-tests "[ui]" --list-tests

# Show skipped tests
./build/bin/helix-tests "[.xml_required]" --list-tests
```

---

## Known Limitations

1. **process_lvgl() may hang** with async subject updates scheduled. For simple binding tests, it's not needed since observers fire synchronously.

2. **Complex panels need many component registrations**. home_panel, controls_panel, etc. use many sub-components that must all be registered.

3. **Some widgets require Moonraker connection**. File list, network discovery, etc. need mock infrastructure.

4. **Screenshot comparison not implemented**. Visual regression testing would need additional infrastructure.

---

## Files Reference

| File | Purpose |
|------|---------|
| `tests/test_fixtures.h` | XMLTestFixture declaration |
| `tests/test_fixtures.cpp` | XMLTestFixture implementation |
| `tests/lvgl_test_fixture.h` | Base LVGL fixture |
| `tests/ui_test_utils.h` | UITest namespace helpers |
| `tests/unit/test_ui_panel_bindings.cpp` | Panel binding tests |
| `docs/UI_TESTING_PLAN.md` | This document |

---

## Appendix: All Identified Binding Tests

### home_panel (58 bindings)

| Widget | Subject | Type |
|--------|---------|------|
| `status_text_label` | status_text | bind_text |
| `printer_type_text` | printer_type_text | bind_text |
| `print_display_filename` | print_display_filename | bind_text |
| `print_progress_text` | print_progress_text | bind_text |
| `print_progress_bar` | print_progress | bind_value |
| `disconnected_overlay` | printer_connection_state | bind_flag_if_eq |
| `shutdown_overlay` | klippy_state | bind_flag_if_not_eq |
| `notification_badge` | notification_count | bind_flag_if_eq |
| `notification_badge_count` | notification_count_text | bind_text |
| `extruder_temp` | extruder_temp | bind_current |
| `extruder_target` | extruder_target | bind_target |
| `network_label` | network_label | bind_text |
| `printer_icon_ready` | printer_icon_state | bind_flag_if_not_eq |
| `printer_icon_warning` | printer_icon_state | bind_flag_if_not_eq |
| `printer_icon_error` | printer_icon_state | bind_flag_if_not_eq |
| `net_disconnected` | home_network_icon_state | bind_flag_if_not_eq |
| `net_wifi_1-4` | home_network_icon_state | bind_flag_if_not_eq |
| `net_ethernet` | home_network_icon_state | bind_flag_if_not_eq |
| `badge_bg` | notification_severity | bind_style |
| `estop_button_container` | estop_visible | bind_flag_if_eq |
| `light_button` | printer_has_led | bind_flag_if_eq |
| `ams_button` | ams_slot_count | bind_flag_if_eq |
| `filament_status_container` | show_filament_status | bind_flag_if_eq |
| `filament_loaded` | filament_runout_detected | bind_flag_if_not_eq |
| ... | ... | ... |

### controls_panel (49 bindings)

| Widget | Subject | Type |
|--------|---------|------|
| `pos_x` | controls_pos_x | bind_text |
| `pos_y` | controls_pos_y | bind_text |
| `pos_z` | controls_pos_z | bind_text |
| `speed_pct` | controls_speed_pct | bind_text |
| `flow_pct` | controls_flow_pct | bind_text |
| `x_homed_indicator` | x_homed | bind_style |
| `y_homed_indicator` | y_homed | bind_style |
| `z_homed_indicator` | z_homed | bind_style |
| `part_fan_slider` | controls_fan_pct | bind_value |
| `z_offset_banner` | pending_z_offset_delta | bind_flag_if_eq |
| `nozzle_temp_display` | extruder_temp | bind_current |
| `bed_temp_display` | bed_temp | bind_current |
| `btn_macro_1-4` | macro_N_visible | bind_flag_if_eq |
| `macro_1-4_label` | macro_N_name | bind_text |
| `btn_home_all` | all_homed | bind_style |
| `motors_icon` | motors_enabled | bind_style |
| ... | ... | ... |

### print_status_panel (29 bindings)

| Widget | Subject | Type |
|--------|---------|------|
| `print_display_filename` | print_display_filename | bind_text |
| `print_elapsed` | print_elapsed | bind_text |
| `print_remaining` | print_remaining | bind_text |
| `print_progress` | print_progress | bind_value |
| `print_progress_text` | print_progress_text | bind_text |
| `print_layer_text` | print_layer_text | bind_text |
| `preparing_overlay` | preparing_visible | bind_flag_if_eq |
| `print_complete_overlay` | print_outcome | bind_flag_if_not_eq |
| `print_cancelled_overlay` | print_outcome | bind_flag_if_not_eq |
| `extruder_temp` | extruder_temp | bind_current |
| `bed_temp` | bed_temp | bind_current |
| `btn_timelapse` | printer_has_timelapse | bind_flag_if_eq |
| `pause_button_icon` | pause_button_icon | bind_text |
| `pause_button_label` | pause_button_label | bind_text |
| ... | ... | ... |

---

## Next Session Checklist

When resuming work:

1. Read this document for context
2. Run `make test-run` to verify tests still pass
3. Check `tests/unit/test_ui_panel_bindings.cpp` for current state
4. Pick next component from Phase 2 list
5. Register any missing component dependencies
6. Implement tests following the established patterns
