// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_panel_bindings.cpp
 * @brief TDD tests for XML component subject-to-UI bindings
 *
 * These tests verify that LVGL subjects correctly update UI widgets through
 * declarative XML bindings.
 *
 * Test Categories:
 * - [ui][home_panel] - Home panel bindings
 * - [ui][controls_panel] - Controls panel bindings
 * - [ui][print_status_panel] - Print status panel bindings
 * - [ui][temp_panel] - Temperature panel bindings (nozzle + bed)
 * - [bind_text] - Text binding tests
 * - [bind_value] - Value binding tests (bars, sliders)
 * - [bind_flag] - Flag binding tests (visibility)
 * - [bind_style] - Style binding tests (colors, appearance)
 *
 * The XMLTestFixture provides:
 * - LVGL display with fonts and theme initialized
 * - Custom widgets registered (icon, text_*, ui_card, temp_display)
 * - PrinterState subjects registered for XML bindings
 */

#include "ui_temp_display.h"

#include "../test_fixtures.h"
#include "../ui_test_utils.h"
#include "printer_state.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;
// Helper to set values on the XML-registered subject (what temp_display actually reads)
// This is critical for test isolation - other tests may have registered their own
// subjects with the same names, so we must use lv_xml_get_subject to get the
// subject that's ACTUALLY in the registry, not state().get_*_subject().
static void set_xml_subject(const char* name, int value) {
    lv_subject_t* subject = lv_xml_get_subject(NULL, name);
    REQUIRE(subject != nullptr); // Fail fast if subject not registered
    lv_subject_set_int(subject, value);
}

// =============================================================================
// TEMPERATURE PANEL BINDING TESTS (NOZZLE + BED) - READY FOR IMPLEMENTATION
// =============================================================================
// XMLTestFixture initialization now works - the theme init hang was fixed by
// deleting the test screen before theme initialization, then recreating it after.
//
// Remaining issue: lv_timer_handler() hangs when there are async subject updates
// scheduled. This prevents using process_lvgl() in tests that use XMLTestFixture.

TEST_CASE_METHOD(XMLTestFixture, "temp_display: binds to extruder temperature subjects",
                 "[ui][temp_display][bind_current][bind_target]") {
    // Test verifies the temp_display widget correctly binds to temperature subjects
    // and displays the expected values.

    // 1. Set temperature values BEFORE creating component using XML-registered subjects
    // Temperature is in centidegrees (200.0°C = 2000, 210.0°C = 2100)
    set_xml_subject("extruder_temp", 2000);   // 200.0°C
    set_xml_subject("extruder_target", 2100); // 210.0°C

    // 2. temp_display is already registered by XMLTestFixture (ui_temp_display_init)
    // Just need to create an instance with binding attributes
    const char* attrs[] = {"bind_current", "extruder_temp", "bind_target", "extruder_target",
                           "show_target",  "true",          nullptr};
    lv_obj_t* temp = create_component("temp_display", attrs);
    REQUIRE(temp != nullptr);
    REQUIRE(ui_temp_display_is_valid(temp));

    // 3. Verify initial values are bound correctly
    // temp_display converts centidegrees to degrees (2000 -> 200)
    int displayed_current = ui_temp_display_get_current(temp);
    int displayed_target = ui_temp_display_get_target(temp);

    REQUIRE(displayed_current == 200);
    REQUIRE(displayed_target == 210);
}

TEST_CASE_METHOD(XMLTestFixture, "temp_display: reactive update when subject changes",
                 "[ui][temp_display][reactive]") {
    // Test verifies the temp_display widget updates reactively when subjects change

    // 1. Set initial temperatures using XML-registered subjects
    set_xml_subject("extruder_temp", 1500);   // 150.0°C
    set_xml_subject("extruder_target", 2000); // 200.0°C

    // 2. Create temp_display with bindings
    const char* attrs[] = {"bind_current", "extruder_temp", "bind_target", "extruder_target",
                           "show_target",  "true",          nullptr};
    lv_obj_t* temp = create_component("temp_display", attrs);
    REQUIRE(temp != nullptr);

    // 3. Verify initial values
    REQUIRE(ui_temp_display_get_current(temp) == 150);
    REQUIRE(ui_temp_display_get_target(temp) == 200);

    // 4. Update subjects - this should trigger reactive update
    set_xml_subject("extruder_temp", 1800);   // 180.0°C
    set_xml_subject("extruder_target", 2200); // 220.0°C

    // 5. Verify values updated reactively
    REQUIRE(ui_temp_display_get_current(temp) == 180);
    REQUIRE(ui_temp_display_get_target(temp) == 220);
}

TEST_CASE_METHOD(XMLTestFixture, "temp_display: target shows -- when heater off (target=0)",
                 "[ui][temp_display][heater_off]") {
    // Test verifies target displays "--" when heater is off (target=0)

    // 1. Set current temp but target=0 (heater off) using XML-registered subjects
    set_xml_subject("extruder_temp", 250); // 25.0°C (ambient)
    set_xml_subject("extruder_target", 0); // Off

    // 2. Create temp_display with bindings
    const char* attrs[] = {"bind_current", "extruder_temp", "bind_target", "extruder_target",
                           "show_target",  "true",          nullptr};
    lv_obj_t* temp = create_component("temp_display", attrs);
    REQUIRE(temp != nullptr);

    // 3. Verify current shows actual value
    REQUIRE(ui_temp_display_get_current(temp) == 25);

    // 4. Verify target is 0 (the display shows "--" but getter returns 0)
    REQUIRE(ui_temp_display_get_target(temp) == 0);
}

TEST_CASE_METHOD(XMLTestFixture, "nozzle_temp_panel: temp_display shows current temperature",
                 "[ui][temp_panel][bind_current][.xml_required]") {
    SKIP("Requires nozzle_status subject registration - implement when subject is available");

    // Test implementation ready - uncomment when all subjects are registered:
    // REQUIRE(register_component("temp_display"));
    // REQUIRE(register_component("header_bar"));
    // REQUIRE(register_component("overlay_panel"));
    // REQUIRE(register_component("nozzle_temp_panel"));
    // lv_subject_set_int(state().get_active_extruder_temp_subject(), 20000);
    // lv_obj_t* panel = create_component("nozzle_temp_panel");
    // REQUIRE(panel != nullptr);
    // process_lvgl(100);
    // lv_obj_t* temp_display = UITest::find_by_name(panel, "nozzle_temp_display");
    // REQUIRE(temp_display != nullptr);
    // int displayed_current = ui_temp_display_get_current(temp_display);
    // REQUIRE(displayed_current == 200); // 20000 centidegrees = 200C
}

TEST_CASE_METHOD(XMLTestFixture, "temp_display: binds to bed temperature subjects",
                 "[ui][temp_display][bind_current][bind_target]") {
    // Test verifies the temp_display widget works with bed temperature subjects

    // 1. Set bed temperature values using XML-registered subjects
    set_xml_subject("bed_temp", 600);   // 60.0°C
    set_xml_subject("bed_target", 700); // 70.0°C

    // 2. Create temp_display with bed bindings
    const char* attrs[] = {"bind_current", "bed_temp", "bind_target", "bed_target",
                           "show_target",  "true",     nullptr};
    lv_obj_t* temp = create_component("temp_display", attrs);
    REQUIRE(temp != nullptr);
    REQUIRE(ui_temp_display_is_valid(temp));

    // 3. Verify bed values are bound correctly
    REQUIRE(ui_temp_display_get_current(temp) == 60);
    REQUIRE(ui_temp_display_get_target(temp) == 70);
}

// =============================================================================
// HOME PANEL BINDING TESTS (SKIP - complex dependencies)
// =============================================================================

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: status_text binding updates label",
                 "[ui][home_panel][bind_text][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: printer_type_text binding updates label",
                 "[ui][home_panel][bind_text][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: print_display_filename binding updates label",
                 "[ui][home_panel][bind_text][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: print_progress_text binding updates label",
                 "[ui][home_panel][bind_text][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: print_progress_bar binding updates bar value",
                 "[ui][home_panel][bind_value][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: disconnected_overlay hidden when connected",
                 "[ui][home_panel][bind_flag][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: notification_badge hidden when count is zero",
                 "[ui][home_panel][bind_flag][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: extruder_temp binding updates temp_display",
                 "[ui][home_panel][bind_current][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "home_panel: extruder_target binding updates temp_display target",
                 "[ui][home_panel][bind_target][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: network_label binding updates text",
                 "[ui][home_panel][bind_text][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

// =============================================================================
// CONTROLS PANEL BINDING TESTS (SKIP - complex dependencies)
// =============================================================================

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: pos_x binding updates position text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: pos_y binding updates position text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: pos_z binding updates position text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: speed_pct binding updates text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: flow_pct binding updates text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: x_homed indicator style changes when homed",
                 "[ui][controls_panel][bind_style][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: y_homed indicator style changes when homed",
                 "[ui][controls_panel][bind_style][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: z_homed indicator style changes when homed",
                 "[ui][controls_panel][bind_style][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "controls_panel: part_fan_slider binding updates slider value",
                 "[ui][controls_panel][bind_value][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: z_offset_banner hidden when delta is zero",
                 "[ui][controls_panel][bind_flag][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

// =============================================================================
// PRINT STATUS PANEL BINDING TESTS (SKIP - complex dependencies)
// =============================================================================

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: print_display_filename binding updates label",
                 "[ui][print_status_panel][bind_text][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: print_elapsed binding updates time label",
                 "[ui][print_status_panel][bind_text][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: print_remaining binding updates time label",
                 "[ui][print_status_panel][bind_text][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: print_progress bar binding updates value",
                 "[ui][print_status_panel][bind_value][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: print_progress_text binding updates label",
                 "[ui][print_status_panel][bind_text][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "print_status_panel: print_layer_text binding updates label",
                 "[ui][print_status_panel][bind_text][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: preparing_overlay hidden when not preparing",
                 "[ui][print_status_panel][bind_flag][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: print_complete_overlay visibility on outcome",
                 "[ui][print_status_panel][bind_flag][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}

// =============================================================================
// NOZZLE/BED TEMP PANEL STATUS BINDING TESTS (SKIP - needs nozzle_status subject)
// =============================================================================

TEST_CASE_METHOD(MoonrakerTestFixture, "nozzle_temp_panel: status_message binding updates text",
                 "[ui][temp_panel][bind_text][.xml_required]") {
    SKIP("Requires nozzle_status subject registration - implement when subject is available");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "bed_temp_panel: temp_display shows target temperature",
                 "[ui][temp_panel][bind_target][.xml_required]") {
    SKIP("Requires full bed_temp_panel test - similar to nozzle tests above");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "bed_temp_panel: status_message binding updates text",
                 "[ui][temp_panel][bind_text][.xml_required]") {
    SKIP("Requires bed_status subject registration - implement when subject is available");
}

// =============================================================================
// ADDITIONAL BINDING TESTS (MIXED PANELS - SKIP)
// =============================================================================

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "controls_panel: nozzle_temp_display binding (temp_display widget)",
                 "[ui][controls_panel][bind_current][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "controls_panel: bed_temp_display binding (temp_display widget)",
                 "[ui][controls_panel][bind_current][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: nozzle_status binding updates status text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: bed_status binding updates status text",
                 "[ui][controls_panel][bind_text][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "home_panel: print_card_idle visibility bound to print_active",
                 "[ui][home_panel][bind_flag][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "home_panel: print_card_printing visibility bound to print_show_progress",
                 "[ui][home_panel][bind_flag][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "home_panel: printer_image dimmed style when disconnected",
                 "[ui][home_panel][bind_style][.xml_required]") {
    SKIP("Home panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture, "controls_panel: all_homed button style changes when homed",
                 "[ui][controls_panel][bind_style][.xml_required]") {
    SKIP("Controls panel has many component dependencies - implement after simpler panels work");
}

TEST_CASE_METHOD(MoonrakerTestFixture,
                 "print_status_panel: timelapse button visibility bound to capability",
                 "[ui][print_status_panel][bind_flag][.xml_required]") {
    SKIP("Print status panel has many component dependencies - implement after simpler panels "
         "work");
}
