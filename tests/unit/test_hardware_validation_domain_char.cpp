// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_hardware_validation_domain_char.cpp
 * @brief Characterization tests for PrinterState hardware validation domain
 *
 * These tests capture the CURRENT behavior of hardware validation subjects
 * in PrinterState before extraction to a dedicated state class.
 *
 * Hardware validation subjects (11 total):
 * - hardware_has_issues_ (int) - 0=no issues, 1=has issues
 * - hardware_issue_count_ (int) - total count of all issues
 * - hardware_max_severity_ (int) - 0=INFO, 1=WARNING, 2=CRITICAL
 * - hardware_critical_count_ (int) - count of critical issues
 * - hardware_warning_count_ (int) - count of warning (expected_missing) issues
 * - hardware_info_count_ (int) - count of info (newly_discovered) issues
 * - hardware_session_count_ (int) - count of session change issues
 * - hardware_status_title_ (string) - "All Healthy" or "X Issues Detected"
 * - hardware_status_detail_ (string) - e.g., "1 critical, 2 missing, 1 new"
 * - hardware_issues_label_ (string) - "1 Hardware Issue" or "5 Hardware Issues"
 * - hardware_validation_version_ (int) - increments on validation change
 *
 * Update mechanism:
 * - set_hardware_validation_result(HardwareValidationResult) - synchronous
 * - remove_hardware_issue(string) - removes issue and re-applies result
 *
 * Key behaviors:
 * - All subjects initialize to 0/"" or default strings
 * - Version increments on every set_hardware_validation_result call
 * - String formatting respects pluralization
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "hardware_validator.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

// Helper to get subject by XML name (requires init_subjects(true))
static lv_subject_t* get_subject_by_name(const char* name) {
    return lv_xml_get_subject(NULL, name);
}

// Helper to create a critical issue
static HardwareIssue make_critical(const std::string& name, const std::string& msg = "Missing") {
    return HardwareIssue::critical(name, HardwareType::HEATER, msg);
}

// Helper to create a warning issue
static HardwareIssue make_warning(const std::string& name, const std::string& msg = "Missing") {
    return HardwareIssue::warning(name, HardwareType::SENSOR, msg);
}

// Helper to create an info issue
static HardwareIssue make_info(const std::string& name, const std::string& msg = "New") {
    return HardwareIssue::info(name, HardwareType::FAN, msg);
}

// ============================================================================
// Initial Value Tests - Document default initialization behavior
// ============================================================================

TEST_CASE("Hardware validation characterization: initial values after init",
          "[characterization][hardware-validation][init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true); // Need XML registration to lookup by name

    SECTION("hardware_status_title initializes to 'Healthy'") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_title");
        REQUIRE(subject != nullptr);
        REQUIRE(std::string(lv_subject_get_string(subject)) == "Healthy");
    }

    SECTION("hardware_issues_label initializes to 'No Hardware Issues'") {
        lv_subject_t* subject = get_subject_by_name("hardware_issues_label");
        REQUIRE(subject != nullptr);
        REQUIRE(std::string(lv_subject_get_string(subject)) == "No Hardware Issues");
    }
}

// ============================================================================
// Empty Result Tests - Verify behavior with no issues
// ============================================================================

TEST_CASE("Hardware validation characterization: empty result (no issues)",
          "[characterization][hardware-validation][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    HardwareValidationResult empty_result;
    state.set_hardware_validation_result(empty_result);

    SECTION("has_issues is 0 for empty result") {
        REQUIRE(lv_subject_get_int(state.get_hardware_has_issues_subject()) == 0);
        REQUIRE(state.has_hardware_issues() == false);
    }

    SECTION("issue_count is 0 for empty result") {
        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 0);
    }

    SECTION("max_severity is 0 (INFO) for empty result") {
        REQUIRE(lv_subject_get_int(state.get_hardware_max_severity_subject()) == 0);
    }

    SECTION("all category counts are 0 for empty result") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_critical_count")) == 0);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_warning_count")) == 0);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_info_count")) == 0);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_session_count")) == 0);
    }

    SECTION("status_title is 'All Healthy' for empty result") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_title");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "All Healthy");
    }

    SECTION("status_detail is 'All configured hardware detected' for empty result") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_detail");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "All configured hardware detected");
    }

    SECTION("issues_label is 'No Hardware Issues' for empty result") {
        REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
                "No Hardware Issues");
    }

    SECTION("version increments on set_hardware_validation_result") {
        int version_before = lv_subject_get_int(state.get_hardware_validation_version_subject());
        HardwareValidationResult another_empty;
        state.set_hardware_validation_result(another_empty);
        int version_after = lv_subject_get_int(state.get_hardware_validation_version_subject());
        REQUIRE(version_after == version_before + 1);
    }
}

// ============================================================================
// Critical Issues Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: critical issues only",
          "[characterization][hardware-validation][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    HardwareValidationResult result;
    result.critical_missing.push_back(make_critical("extruder", "Extruder not responding"));
    result.critical_missing.push_back(make_critical("heater_bed", "Bed heater missing"));
    state.set_hardware_validation_result(result);

    SECTION("has_issues is 1 for critical issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_has_issues_subject()) == 1);
        REQUIRE(state.has_hardware_issues() == true);
    }

    SECTION("issue_count equals number of critical issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 2);
    }

    SECTION("max_severity is 2 (CRITICAL) for critical issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_max_severity_subject()) == 2);
    }

    SECTION("critical_count matches number of critical issues") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_critical_count")) == 2);
    }

    SECTION("other category counts remain 0") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_warning_count")) == 0);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_info_count")) == 0);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_session_count")) == 0);
    }

    SECTION("status_title shows '2 Issues Detected'") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_title");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "2 Issues Detected");
    }

    SECTION("status_detail shows '2 critical'") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_detail");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "2 critical");
    }

    SECTION("issues_label shows '2 Hardware Issues'") {
        REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
                "2 Hardware Issues");
    }
}

// ============================================================================
// Warning Issues Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: warning issues only",
          "[characterization][hardware-validation][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    HardwareValidationResult result;
    result.expected_missing.push_back(make_warning("temperature_sensor chamber"));
    state.set_hardware_validation_result(result);

    SECTION("has_issues is 1 for warning issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_has_issues_subject()) == 1);
    }

    SECTION("issue_count equals number of warning issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 1);
    }

    SECTION("max_severity is 1 (WARNING) for warning-only issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_max_severity_subject()) == 1);
    }

    SECTION("warning_count matches number of expected_missing issues") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_warning_count")) == 1);
    }

    SECTION("status_title shows '1 Issue Detected' (singular)") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_title");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "1 Issue Detected");
    }

    SECTION("status_detail shows '1 missing'") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_detail");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "1 missing");
    }

    SECTION("issues_label shows '1 Hardware Issue' (singular)") {
        REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
                "1 Hardware Issue");
    }
}

// ============================================================================
// Info Issues Tests (newly discovered)
// ============================================================================

TEST_CASE("Hardware validation characterization: info issues only (newly discovered)",
          "[characterization][hardware-validation][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    HardwareValidationResult result;
    result.newly_discovered.push_back(make_info("neopixel toolhead_lights"));
    result.newly_discovered.push_back(make_info("fan_generic exhaust_fan"));
    result.newly_discovered.push_back(make_info("filament_switch_sensor runout"));
    state.set_hardware_validation_result(result);

    SECTION("has_issues is 1 for info issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_has_issues_subject()) == 1);
    }

    SECTION("issue_count equals number of info issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 3);
    }

    SECTION("max_severity is 0 (INFO) for info-only issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_max_severity_subject()) == 0);
    }

    SECTION("info_count matches number of newly_discovered issues") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_info_count")) == 3);
    }

    SECTION("status_detail shows '3 new'") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_detail");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "3 new");
    }

    SECTION("issues_label shows '3 Hardware Issues'") {
        REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
                "3 Hardware Issues");
    }
}

// ============================================================================
// Session Changed Issues Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: session changed issues only",
          "[characterization][hardware-validation][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    HardwareValidationResult result;
    // Session changes are warnings that hardware was present last session but is now missing
    HardwareIssue session_issue;
    session_issue.hardware_name = "temperature_sensor enclosure";
    session_issue.hardware_type = HardwareType::SENSOR;
    session_issue.severity = HardwareIssueSeverity::WARNING;
    session_issue.message = "Was present last session";
    result.changed_from_last_session.push_back(session_issue);
    state.set_hardware_validation_result(result);

    SECTION("has_issues is 1 for session changed issues") {
        REQUIRE(lv_subject_get_int(state.get_hardware_has_issues_subject()) == 1);
    }

    SECTION("session_count matches number of changed_from_last_session issues") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_session_count")) == 1);
    }

    SECTION("max_severity is 1 (WARNING) for session-only issues") {
        // Session changes are treated as warnings in max_severity calculation
        REQUIRE(lv_subject_get_int(state.get_hardware_max_severity_subject()) == 1);
    }

    SECTION("status_detail shows '1 changed'") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_detail");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "1 changed");
    }
}

// ============================================================================
// Mixed Issues Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: mixed issues",
          "[characterization][hardware-validation][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    HardwareValidationResult result;
    result.critical_missing.push_back(make_critical("extruder"));
    result.expected_missing.push_back(make_warning("probe"));
    result.expected_missing.push_back(make_warning("bltouch"));
    result.newly_discovered.push_back(make_info("neopixel case_lights"));
    state.set_hardware_validation_result(result);

    SECTION("issue_count is sum of all categories") {
        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 4);
    }

    SECTION("max_severity is highest severity (CRITICAL=2)") {
        REQUIRE(lv_subject_get_int(state.get_hardware_max_severity_subject()) == 2);
    }

    SECTION("each category count is correct") {
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_critical_count")) == 1);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_warning_count")) == 2);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_info_count")) == 1);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_session_count")) == 0);
    }

    SECTION("status_title shows total count") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_title");
        REQUIRE(std::string(lv_subject_get_string(subject)) == "4 Issues Detected");
    }

    SECTION("status_detail lists all non-empty categories with comma separation") {
        lv_subject_t* subject = get_subject_by_name("hardware_status_detail");
        std::string detail = lv_subject_get_string(subject);
        // Should contain: "1 critical, 2 missing, 1 new"
        REQUIRE(detail.find("1 critical") != std::string::npos);
        REQUIRE(detail.find("2 missing") != std::string::npos);
        REQUIRE(detail.find("1 new") != std::string::npos);
        REQUIRE(detail.find(", ") != std::string::npos);
    }
}

// ============================================================================
// Version Increment Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: version increments on each call",
          "[characterization][hardware-validation][version]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    int initial_version = lv_subject_get_int(state.get_hardware_validation_version_subject());

    SECTION("version increments by 1 on each set_hardware_validation_result call") {
        HardwareValidationResult empty_result;
        state.set_hardware_validation_result(empty_result);
        REQUIRE(lv_subject_get_int(state.get_hardware_validation_version_subject()) ==
                initial_version + 1);

        state.set_hardware_validation_result(empty_result);
        REQUIRE(lv_subject_get_int(state.get_hardware_validation_version_subject()) ==
                initial_version + 2);

        state.set_hardware_validation_result(empty_result);
        REQUIRE(lv_subject_get_int(state.get_hardware_validation_version_subject()) ==
                initial_version + 3);
    }

    SECTION("version increments even when content unchanged") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));

        state.set_hardware_validation_result(result);
        int v1 = lv_subject_get_int(state.get_hardware_validation_version_subject());

        // Same result again
        state.set_hardware_validation_result(result);
        int v2 = lv_subject_get_int(state.get_hardware_validation_version_subject());

        REQUIRE(v2 == v1 + 1);
    }
}

// ============================================================================
// get_hardware_validation_result Tests
// ============================================================================

TEST_CASE(
    "Hardware validation characterization: get_hardware_validation_result returns stored result",
    "[characterization][hardware-validation][getter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("after setting empty result, returns empty result") {
        // Note: The stored HardwareValidationResult is NOT cleared by reset_for_testing()
        // (unlike subjects which are reset to defaults). To ensure empty state, we must
        // explicitly set an empty result.
        HardwareValidationResult empty_result;
        state.set_hardware_validation_result(empty_result);

        const HardwareValidationResult& result = state.get_hardware_validation_result();
        REQUIRE(result.critical_missing.empty());
        REQUIRE(result.expected_missing.empty());
        REQUIRE(result.newly_discovered.empty());
        REQUIRE(result.changed_from_last_session.empty());
        REQUIRE(result.has_issues() == false);
    }

    SECTION("returns stored result after set") {
        HardwareValidationResult input;
        input.critical_missing.push_back(make_critical("extruder"));
        input.expected_missing.push_back(make_warning("probe"));
        state.set_hardware_validation_result(input);

        const HardwareValidationResult& stored = state.get_hardware_validation_result();
        REQUIRE(stored.critical_missing.size() == 1);
        REQUIRE(stored.critical_missing[0].hardware_name == "extruder");
        REQUIRE(stored.expected_missing.size() == 1);
        REQUIRE(stored.expected_missing[0].hardware_name == "probe");
    }
}

// ============================================================================
// remove_hardware_issue Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: remove_hardware_issue",
          "[characterization][hardware-validation][remove]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("removes issue from critical_missing and updates counts") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        result.critical_missing.push_back(make_critical("heater_bed"));
        state.set_hardware_validation_result(result);

        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 2);

        state.remove_hardware_issue("extruder");

        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 1);
        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_critical_count")) == 1);

        // Verify stored result is updated
        const HardwareValidationResult& stored = state.get_hardware_validation_result();
        REQUIRE(stored.critical_missing.size() == 1);
        REQUIRE(stored.critical_missing[0].hardware_name == "heater_bed");
    }

    SECTION("removes issue from expected_missing") {
        HardwareValidationResult result;
        result.expected_missing.push_back(make_warning("probe"));
        result.expected_missing.push_back(make_warning("bltouch"));
        state.set_hardware_validation_result(result);

        state.remove_hardware_issue("probe");

        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_warning_count")) == 1);
        REQUIRE(state.get_hardware_validation_result().expected_missing.size() == 1);
    }

    SECTION("removes issue from newly_discovered") {
        HardwareValidationResult result;
        result.newly_discovered.push_back(make_info("neopixel led"));
        state.set_hardware_validation_result(result);

        state.remove_hardware_issue("neopixel led");

        REQUIRE(lv_subject_get_int(get_subject_by_name("hardware_info_count")) == 0);
        REQUIRE(lv_subject_get_int(state.get_hardware_has_issues_subject()) == 0);
    }

    SECTION("removing last issue sets has_issues to 0") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        REQUIRE(state.has_hardware_issues() == true);

        state.remove_hardware_issue("extruder");

        REQUIRE(state.has_hardware_issues() == false);
        REQUIRE(std::string(lv_subject_get_string(get_subject_by_name("hardware_status_title"))) ==
                "All Healthy");
    }

    SECTION("removing non-existent issue does not crash") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        // Should not crash, just no-op
        state.remove_hardware_issue("nonexistent_hardware");

        REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 1);
    }

    SECTION("remove_hardware_issue increments version") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        int version_before = lv_subject_get_int(state.get_hardware_validation_version_subject());
        state.remove_hardware_issue("extruder");
        int version_after = lv_subject_get_int(state.get_hardware_validation_version_subject());

        REQUIRE(version_after == version_before + 1);
    }
}

// ============================================================================
// String Formatting Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: string formatting and pluralization",
          "[characterization][hardware-validation][format]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("singular issue label: '1 Hardware Issue'") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
                "1 Hardware Issue");
    }

    SECTION("plural issue label: '5 Hardware Issues'") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        result.expected_missing.push_back(make_warning("probe"));
        result.expected_missing.push_back(make_warning("bltouch"));
        result.newly_discovered.push_back(make_info("neopixel led"));
        result.newly_discovered.push_back(make_info("fan_generic exhaust"));
        state.set_hardware_validation_result(result);

        REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
                "5 Hardware Issues");
    }

    SECTION("singular title: '1 Issue Detected'") {
        HardwareValidationResult result;
        result.expected_missing.push_back(make_warning("probe"));
        state.set_hardware_validation_result(result);

        REQUIRE(std::string(lv_subject_get_string(get_subject_by_name("hardware_status_title"))) ==
                "1 Issue Detected");
    }

    SECTION("plural title: '3 Issues Detected'") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        result.expected_missing.push_back(make_warning("probe"));
        result.newly_discovered.push_back(make_info("neopixel led"));
        state.set_hardware_validation_result(result);

        REQUIRE(std::string(lv_subject_get_string(get_subject_by_name("hardware_status_title"))) ==
                "3 Issues Detected");
    }
}

// ============================================================================
// has_hardware_issues() Convenience Method Test
// ============================================================================

TEST_CASE("Hardware validation characterization: has_hardware_issues() method",
          "[characterization][hardware-validation][convenience]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("has_hardware_issues() returns false initially") {
        REQUIRE(state.has_hardware_issues() == false);
    }

    SECTION("has_hardware_issues() returns true when issues present") {
        HardwareValidationResult result;
        result.newly_discovered.push_back(make_info("neopixel led"));
        state.set_hardware_validation_result(result);

        REQUIRE(state.has_hardware_issues() == true);
    }

    SECTION("has_hardware_issues() matches subject value") {
        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        bool method_result = state.has_hardware_issues();
        int subject_value = lv_subject_get_int(state.get_hardware_has_issues_subject());

        REQUIRE(method_result == (subject_value != 0));
    }
}

// ============================================================================
// Observer Notification Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: observer fires when validation changes",
          "[characterization][hardware-validation][observer]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    auto observer_cb = [](lv_observer_t* observer, lv_subject_t* subject) {
        int* count_ptr = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count_ptr)++;
    };

    SECTION("observer fires on hardware_validation_version changes") {
        int notify_count = 0;
        lv_subject_t* version_subject = state.get_hardware_validation_version_subject();

        lv_observer_t* observer =
            lv_subject_add_observer(version_subject, observer_cb, &notify_count);

        // LVGL notifies once on add
        REQUIRE(notify_count == 1);

        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        REQUIRE(notify_count == 2);

        lv_observer_remove(observer);
    }

    SECTION("observer fires on hardware_has_issues changes") {
        int notify_count = 0;
        lv_subject_t* has_issues_subject = state.get_hardware_has_issues_subject();

        lv_observer_t* observer =
            lv_subject_add_observer(has_issues_subject, observer_cb, &notify_count);

        // LVGL notifies once on add
        REQUIRE(notify_count == 1);

        HardwareValidationResult result;
        result.critical_missing.push_back(make_critical("extruder"));
        state.set_hardware_validation_result(result);

        // Should fire because value changed from 0 to 1
        REQUIRE(notify_count >= 2);

        lv_observer_remove(observer);
    }
}

// ============================================================================
// Reset Cycle Tests
// ============================================================================

TEST_CASE("Hardware validation characterization: subjects survive reset_for_testing cycle",
          "[characterization][hardware-validation][reset]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    // Set validation result
    HardwareValidationResult result;
    result.critical_missing.push_back(make_critical("extruder"));
    state.set_hardware_validation_result(result);

    REQUIRE(state.has_hardware_issues() == true);
    REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 1);

    // Reset and reinitialize
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    // After reset, values should be back to defaults
    REQUIRE(state.has_hardware_issues() == false);
    REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_hardware_validation_version_subject()) == 0);
    REQUIRE(std::string(lv_subject_get_string(state.get_hardware_issues_label_subject())) ==
            "No Hardware Issues");

    // Subjects should still be functional after reset
    HardwareValidationResult new_result;
    new_result.newly_discovered.push_back(make_info("neopixel led"));
    state.set_hardware_validation_result(new_result);

    REQUIRE(state.has_hardware_issues() == true);
    REQUIRE(lv_subject_get_int(state.get_hardware_issue_count_subject()) == 1);
}
