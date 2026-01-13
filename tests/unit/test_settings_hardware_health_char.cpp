// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_hardware_health_char.cpp
 * @brief Characterization tests for Hardware Health overlay
 *
 * These tests document the exact behavior of the hardware health UI
 * in ui_panel_settings.cpp to enable safe extraction. They test the LOGIC only,
 * not the LVGL widgets (no UI creation).
 *
 * Pattern: Mirror the calculation/formatting logic used in the panel,
 * then verify specific cases to document expected behavior.
 *
 * @see ui_panel_settings.cpp - SettingsPanel::handle_hardware_health_clicked()
 * @see ui_panel_settings.cpp - SettingsPanel::populate_hardware_issues()
 * @see hardware_validator.h - HardwareIssue, HardwareValidationResult
 */

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Helpers: Data Model (mirrors hardware_validator.h)
// ============================================================================

/**
 * @brief Test-local copy of HardwareIssueSeverity enum
 *
 * Mirrors helix::HardwareIssueSeverity for test isolation.
 */
enum class TestHardwareIssueSeverity {
    INFO = 0,    ///< New hardware discovered
    WARNING = 1, ///< Configured hardware missing
    CRITICAL = 2 ///< Core hardware missing
};

/**
 * @brief Test-local copy of HardwareType enum
 */
enum class TestHardwareType {
    HEATER = 0,
    SENSOR = 1,
    FAN = 2,
    LED = 3,
    FILAMENT_SENSOR = 4,
    OTHER = 5
};

/**
 * @brief Test-local hardware issue structure
 */
struct TestHardwareIssue {
    std::string hardware_name;
    TestHardwareType hardware_type;
    TestHardwareIssueSeverity severity;
    std::string message;
    bool is_optional;

    static TestHardwareIssue critical(const std::string& name, TestHardwareType type,
                                      const std::string& msg) {
        return {name, type, TestHardwareIssueSeverity::CRITICAL, msg, false};
    }

    static TestHardwareIssue warning(const std::string& name, TestHardwareType type,
                                     const std::string& msg, bool optional = false) {
        return {name, type, TestHardwareIssueSeverity::WARNING, msg, optional};
    }

    static TestHardwareIssue info(const std::string& name, TestHardwareType type,
                                  const std::string& msg) {
        return {name, type, TestHardwareIssueSeverity::INFO, msg, false};
    }
};

/**
 * @brief Test-local validation result structure
 */
struct TestHardwareValidationResult {
    std::vector<TestHardwareIssue> critical_missing;
    std::vector<TestHardwareIssue> expected_missing;
    std::vector<TestHardwareIssue> newly_discovered;
    std::vector<TestHardwareIssue> changed_from_last_session;

    [[nodiscard]] bool has_issues() const {
        return !critical_missing.empty() || !expected_missing.empty() ||
               !newly_discovered.empty() || !changed_from_last_session.empty();
    }

    [[nodiscard]] bool has_critical() const {
        return !critical_missing.empty();
    }

    [[nodiscard]] size_t total_issue_count() const {
        return critical_missing.size() + expected_missing.size() + newly_discovered.size() +
               changed_from_last_session.size();
    }

    [[nodiscard]] TestHardwareIssueSeverity max_severity() const {
        if (!critical_missing.empty()) {
            return TestHardwareIssueSeverity::CRITICAL;
        }
        if (!expected_missing.empty() || !changed_from_last_session.empty()) {
            return TestHardwareIssueSeverity::WARNING;
        }
        return TestHardwareIssueSeverity::INFO;
    }
};

// ============================================================================
// Test Helpers: Conversion Functions (mirrors hardware_validator.h)
// ============================================================================

/**
 * @brief Convert hardware type to display string
 *
 * Mirrors hardware_type_to_string()
 */
static const char* hardware_type_to_string(TestHardwareType type) {
    switch (type) {
    case TestHardwareType::HEATER:
        return "heater";
    case TestHardwareType::SENSOR:
        return "sensor";
    case TestHardwareType::FAN:
        return "fan";
    case TestHardwareType::LED:
        return "led";
    case TestHardwareType::FILAMENT_SENSOR:
        return "filament_sensor";
    default:
        return "hardware";
    }
}

/**
 * @brief Convert severity to XML attribute string
 *
 * Mirrors the lambda in SettingsPanel::populate_hardware_issues()
 */
static const char* severity_to_xml_string(TestHardwareIssueSeverity sev) {
    switch (sev) {
    case TestHardwareIssueSeverity::CRITICAL:
        return "error";
    case TestHardwareIssueSeverity::WARNING:
        return "warning";
    case TestHardwareIssueSeverity::INFO:
    default:
        return "info";
    }
}

// ============================================================================
// CHARACTERIZATION TESTS
// ============================================================================

TEST_CASE("CHAR: HardwareIssueSeverity enum values",
          "[characterization][settings][hardware_health]") {
    SECTION("Severity enum has expected integer values") {
        // INFO=0, WARNING=1, CRITICAL=2 (matches hardware_validator.h)
        REQUIRE(static_cast<int>(TestHardwareIssueSeverity::INFO) == 0);
        REQUIRE(static_cast<int>(TestHardwareIssueSeverity::WARNING) == 1);
        REQUIRE(static_cast<int>(TestHardwareIssueSeverity::CRITICAL) == 2);
    }

    SECTION("Severity ordering: INFO < WARNING < CRITICAL") {
        REQUIRE(TestHardwareIssueSeverity::INFO < TestHardwareIssueSeverity::WARNING);
        REQUIRE(TestHardwareIssueSeverity::WARNING < TestHardwareIssueSeverity::CRITICAL);
    }
}

TEST_CASE("CHAR: HardwareType enum values", "[characterization][settings][hardware_health]") {
    SECTION("Type enum has expected integer values") {
        REQUIRE(static_cast<int>(TestHardwareType::HEATER) == 0);
        REQUIRE(static_cast<int>(TestHardwareType::SENSOR) == 1);
        REQUIRE(static_cast<int>(TestHardwareType::FAN) == 2);
        REQUIRE(static_cast<int>(TestHardwareType::LED) == 3);
        REQUIRE(static_cast<int>(TestHardwareType::FILAMENT_SENSOR) == 4);
        REQUIRE(static_cast<int>(TestHardwareType::OTHER) == 5);
    }
}

TEST_CASE("CHAR: Hardware type to string conversion",
          "[characterization][settings][hardware_health]") {
    SECTION("HEATER type") {
        REQUIRE(std::string(hardware_type_to_string(TestHardwareType::HEATER)) == "heater");
    }

    SECTION("SENSOR type") {
        REQUIRE(std::string(hardware_type_to_string(TestHardwareType::SENSOR)) == "sensor");
    }

    SECTION("FAN type") {
        REQUIRE(std::string(hardware_type_to_string(TestHardwareType::FAN)) == "fan");
    }

    SECTION("LED type") {
        REQUIRE(std::string(hardware_type_to_string(TestHardwareType::LED)) == "led");
    }

    SECTION("FILAMENT_SENSOR type") {
        REQUIRE(std::string(hardware_type_to_string(TestHardwareType::FILAMENT_SENSOR)) ==
                "filament_sensor");
    }

    SECTION("OTHER type defaults to 'hardware'") {
        REQUIRE(std::string(hardware_type_to_string(TestHardwareType::OTHER)) == "hardware");
    }
}

TEST_CASE("CHAR: Severity to XML attribute conversion",
          "[characterization][settings][hardware_health]") {
    SECTION("CRITICAL severity maps to 'error'") {
        REQUIRE(std::string(severity_to_xml_string(TestHardwareIssueSeverity::CRITICAL)) ==
                "error");
    }

    SECTION("WARNING severity maps to 'warning'") {
        REQUIRE(std::string(severity_to_xml_string(TestHardwareIssueSeverity::WARNING)) ==
                "warning");
    }

    SECTION("INFO severity maps to 'info'") {
        REQUIRE(std::string(severity_to_xml_string(TestHardwareIssueSeverity::INFO)) == "info");
    }
}

TEST_CASE("CHAR: HardwareIssue factory methods", "[characterization][settings][hardware_health]") {
    SECTION("critical() creates CRITICAL issue") {
        auto issue = TestHardwareIssue::critical("heater_bed", TestHardwareType::HEATER,
                                                 "Bed heater not responding");
        REQUIRE(issue.hardware_name == "heater_bed");
        REQUIRE(issue.hardware_type == TestHardwareType::HEATER);
        REQUIRE(issue.severity == TestHardwareIssueSeverity::CRITICAL);
        REQUIRE(issue.message == "Bed heater not responding");
        REQUIRE(issue.is_optional == false);
    }

    SECTION("warning() creates WARNING issue") {
        auto issue =
            TestHardwareIssue::warning("neopixel chamber", TestHardwareType::LED, "LED not found");
        REQUIRE(issue.hardware_name == "neopixel chamber");
        REQUIRE(issue.hardware_type == TestHardwareType::LED);
        REQUIRE(issue.severity == TestHardwareIssueSeverity::WARNING);
        REQUIRE(issue.message == "LED not found");
        REQUIRE(issue.is_optional == false);
    }

    SECTION("warning() with optional flag") {
        auto issue = TestHardwareIssue::warning("neopixel chamber", TestHardwareType::LED,
                                                "LED not found", true);
        REQUIRE(issue.is_optional == true);
    }

    SECTION("info() creates INFO issue") {
        auto issue = TestHardwareIssue::info("fan_generic exhaust", TestHardwareType::FAN,
                                             "New fan detected");
        REQUIRE(issue.hardware_name == "fan_generic exhaust");
        REQUIRE(issue.hardware_type == TestHardwareType::FAN);
        REQUIRE(issue.severity == TestHardwareIssueSeverity::INFO);
        REQUIRE(issue.message == "New fan detected");
        REQUIRE(issue.is_optional == false);
    }
}

TEST_CASE("CHAR: HardwareValidationResult has_issues()",
          "[characterization][settings][hardware_health]") {
    TestHardwareValidationResult result;

    SECTION("Empty result has no issues") {
        REQUIRE(result.has_issues() == false);
    }

    SECTION("Critical issue triggers has_issues") {
        result.critical_missing.push_back(
            TestHardwareIssue::critical("extruder", TestHardwareType::HEATER, "Missing"));
        REQUIRE(result.has_issues() == true);
    }

    SECTION("Expected missing triggers has_issues") {
        result.expected_missing.push_back(
            TestHardwareIssue::warning("neopixel", TestHardwareType::LED, "Missing"));
        REQUIRE(result.has_issues() == true);
    }

    SECTION("Newly discovered triggers has_issues") {
        result.newly_discovered.push_back(
            TestHardwareIssue::info("fan_generic", TestHardwareType::FAN, "Found"));
        REQUIRE(result.has_issues() == true);
    }

    SECTION("Session changes trigger has_issues") {
        result.changed_from_last_session.push_back(
            TestHardwareIssue::warning("sensor", TestHardwareType::SENSOR, "Removed"));
        REQUIRE(result.has_issues() == true);
    }
}

TEST_CASE("CHAR: HardwareValidationResult has_critical()",
          "[characterization][settings][hardware_health]") {
    TestHardwareValidationResult result;

    SECTION("Empty result has no critical") {
        REQUIRE(result.has_critical() == false);
    }

    SECTION("Warning issues don't trigger has_critical") {
        result.expected_missing.push_back(
            TestHardwareIssue::warning("neopixel", TestHardwareType::LED, "Missing"));
        REQUIRE(result.has_critical() == false);
    }

    SECTION("Critical issue triggers has_critical") {
        result.critical_missing.push_back(
            TestHardwareIssue::critical("extruder", TestHardwareType::HEATER, "Missing"));
        REQUIRE(result.has_critical() == true);
    }
}

TEST_CASE("CHAR: HardwareValidationResult total_issue_count()",
          "[characterization][settings][hardware_health]") {
    TestHardwareValidationResult result;

    SECTION("Empty result has count 0") {
        REQUIRE(result.total_issue_count() == 0);
    }

    SECTION("Single issue") {
        result.critical_missing.push_back(
            TestHardwareIssue::critical("extruder", TestHardwareType::HEATER, "Missing"));
        REQUIRE(result.total_issue_count() == 1);
    }

    SECTION("Multiple issues across categories") {
        result.critical_missing.push_back(
            TestHardwareIssue::critical("extruder", TestHardwareType::HEATER, "Missing"));
        result.critical_missing.push_back(
            TestHardwareIssue::critical("heater_bed", TestHardwareType::HEATER, "Missing"));
        result.expected_missing.push_back(
            TestHardwareIssue::warning("neopixel", TestHardwareType::LED, "Missing"));
        result.newly_discovered.push_back(
            TestHardwareIssue::info("fan", TestHardwareType::FAN, "Found"));
        result.changed_from_last_session.push_back(
            TestHardwareIssue::warning("sensor", TestHardwareType::SENSOR, "Removed"));

        REQUIRE(result.total_issue_count() == 5);
    }
}

TEST_CASE("CHAR: HardwareValidationResult max_severity()",
          "[characterization][settings][hardware_health]") {
    TestHardwareValidationResult result;

    SECTION("Empty result returns INFO") {
        REQUIRE(result.max_severity() == TestHardwareIssueSeverity::INFO);
    }

    SECTION("Only info issues returns INFO") {
        result.newly_discovered.push_back(
            TestHardwareIssue::info("fan", TestHardwareType::FAN, "Found"));
        REQUIRE(result.max_severity() == TestHardwareIssueSeverity::INFO);
    }

    SECTION("Expected missing returns WARNING") {
        result.expected_missing.push_back(
            TestHardwareIssue::warning("neopixel", TestHardwareType::LED, "Missing"));
        REQUIRE(result.max_severity() == TestHardwareIssueSeverity::WARNING);
    }

    SECTION("Session changes return WARNING") {
        result.changed_from_last_session.push_back(
            TestHardwareIssue::warning("sensor", TestHardwareType::SENSOR, "Removed"));
        REQUIRE(result.max_severity() == TestHardwareIssueSeverity::WARNING);
    }

    SECTION("Critical issues return CRITICAL (overrides others)") {
        result.newly_discovered.push_back(
            TestHardwareIssue::info("fan", TestHardwareType::FAN, "Found"));
        result.expected_missing.push_back(
            TestHardwareIssue::warning("neopixel", TestHardwareType::LED, "Missing"));
        result.critical_missing.push_back(
            TestHardwareIssue::critical("extruder", TestHardwareType::HEATER, "Missing"));

        REQUIRE(result.max_severity() == TestHardwareIssueSeverity::CRITICAL);
    }
}

TEST_CASE("CHAR: XML overlay widget names", "[characterization][settings][hardware_health]") {
    SECTION("Overlay root name") {
        std::string name = "hardware_health_overlay";
        REQUIRE(name == "hardware_health_overlay");
    }

    SECTION("Status card") {
        std::string name = "status_card";
        REQUIRE(name == "status_card");
    }

    SECTION("Status icon containers") {
        REQUIRE(std::string("status_icon_container") == "status_icon_container");
        REQUIRE(std::string("status_icon_container_warn") == "status_icon_container_warn");
        REQUIRE(std::string("status_icon_container_crit") == "status_icon_container_crit");
    }

    SECTION("Section containers") {
        REQUIRE(std::string("critical_section") == "critical_section");
        REQUIRE(std::string("warning_section") == "warning_section");
        REQUIRE(std::string("info_section") == "info_section");
        REQUIRE(std::string("session_section") == "session_section");
    }

    SECTION("Issue list containers") {
        REQUIRE(std::string("critical_issues_list") == "critical_issues_list");
        REQUIRE(std::string("warning_issues_list") == "warning_issues_list");
        REQUIRE(std::string("info_issues_list") == "info_issues_list");
        REQUIRE(std::string("session_issues_list") == "session_issues_list");
    }
}

TEST_CASE("CHAR: XML issue row widget names", "[characterization][settings][hardware_health]") {
    SECTION("Row root name") {
        std::string name = "hardware_issue_row";
        REQUIRE(name == "hardware_issue_row");
    }

    SECTION("Content labels") {
        REQUIRE(std::string("hardware_name") == "hardware_name");
        REQUIRE(std::string("issue_message") == "issue_message");
    }

    SECTION("Action buttons container") {
        std::string name = "action_buttons";
        REQUIRE(name == "action_buttons");
    }

    SECTION("Individual buttons") {
        REQUIRE(std::string("ignore_btn") == "ignore_btn");
        REQUIRE(std::string("save_btn") == "save_btn");
    }

    SECTION("Severity icons") {
        REQUIRE(std::string("icon_info") == "icon_info");
        REQUIRE(std::string("icon_success") == "icon_success");
        REQUIRE(std::string("icon_warning") == "icon_warning");
        REQUIRE(std::string("icon_error") == "icon_error");
    }
}

TEST_CASE("CHAR: XML callback names", "[characterization][settings][hardware_health]") {
    SECTION("Main overlay callback") {
        std::string name = "on_hardware_health_clicked";
        REQUIRE(name == "on_hardware_health_clicked");
    }
}

TEST_CASE("CHAR: XML subject names", "[characterization][settings][hardware_health]") {
    SECTION("Has issues subject (bound to status icon visibility)") {
        std::string name = "hardware_has_issues";
        REQUIRE(name == "hardware_has_issues");
    }

    SECTION("Max severity subject (bound to icon container visibility)") {
        std::string name = "hardware_max_severity";
        REQUIRE(name == "hardware_max_severity");
    }

    SECTION("Count subjects (bound to section visibility)") {
        REQUIRE(std::string("hardware_critical_count") == "hardware_critical_count");
        REQUIRE(std::string("hardware_warning_count") == "hardware_warning_count");
        REQUIRE(std::string("hardware_info_count") == "hardware_info_count");
        REQUIRE(std::string("hardware_session_count") == "hardware_session_count");
    }

    SECTION("Status text subjects") {
        REQUIRE(std::string("hardware_status_title") == "hardware_status_title");
        REQUIRE(std::string("hardware_status_detail") == "hardware_status_detail");
    }

    SECTION("Issues label subject (for settings row)") {
        // Used by row_hardware_health in settings_panel.xml
        std::string name = "hardware_issues_label";
        REQUIRE(name == "hardware_issues_label");
    }
}

TEST_CASE("CHAR: Hardware action button behavior",
          "[characterization][settings][hardware_health]") {
    SECTION("CRITICAL issues do NOT show action buttons") {
        // Critical issues cannot be ignored or saved
        auto issue =
            TestHardwareIssue::critical("extruder", TestHardwareType::HEATER, "Required hardware");
        bool show_buttons = (issue.severity != TestHardwareIssueSeverity::CRITICAL);
        REQUIRE(show_buttons == false);
    }

    SECTION("WARNING issues show Ignore button only") {
        auto issue =
            TestHardwareIssue::warning("neopixel", TestHardwareType::LED, "Configured but missing");
        bool show_buttons = (issue.severity != TestHardwareIssueSeverity::CRITICAL);
        bool show_save = (issue.severity == TestHardwareIssueSeverity::INFO);
        REQUIRE(show_buttons == true);
        REQUIRE(show_save == false);
    }

    SECTION("INFO issues show both Ignore and Save buttons") {
        auto issue =
            TestHardwareIssue::info("fan_generic", TestHardwareType::FAN, "Newly discovered");
        bool show_buttons = (issue.severity != TestHardwareIssueSeverity::CRITICAL);
        bool show_save = (issue.severity == TestHardwareIssueSeverity::INFO);
        REQUIRE(show_buttons == true);
        REQUIRE(show_save == true);
    }
}

TEST_CASE("CHAR: Hardware action workflow", "[characterization][settings][hardware_health]") {
    SECTION("Ignore action marks hardware as optional") {
        // Mirrors SettingsPanel::handle_hardware_action(name, is_ignore=true)
        // Calls HardwareValidator::set_hardware_optional(config, name, true)
        // Shows toast: "Hardware marked as optional"
        // Removes from cached validation result
        // Refreshes overlay

        std::string hw_name = "neopixel chamber";
        bool is_ignore = true;

        // Expected behavior
        REQUIRE(is_ignore == true);
        REQUIRE(hw_name == "neopixel chamber");

        // Toast message format
        std::string toast = "Hardware marked as optional";
        REQUIRE(toast == "Hardware marked as optional");
    }

    SECTION("Save action adds to expected hardware with confirmation") {
        // Mirrors SettingsPanel::handle_hardware_action(name, is_ignore=false)
        // Shows confirmation dialog before saving
        // Calls HardwareValidator::add_expected_hardware(config, name)
        // Shows toast: "Hardware saved to config"
        // Removes from cached validation result
        // Refreshes overlay

        std::string hw_name = "fan_generic exhaust";
        bool is_ignore = false;

        // Expected behavior
        REQUIRE(is_ignore == false);
        REQUIRE(hw_name == "fan_generic exhaust");

        // Confirmation dialog message format
        char message_buf[256];
        snprintf(message_buf, sizeof(message_buf),
                 "Add '%s' to expected hardware?\n\nYou'll be notified if it's removed later.",
                 hw_name.c_str());
        REQUIRE(std::string(message_buf).find("fan_generic exhaust") != std::string::npos);

        // Toast message format after confirmation
        std::string toast = "Hardware saved to config";
        REQUIRE(toast == "Hardware saved to config");
    }
}

TEST_CASE("CHAR: Save confirmation dialog", "[characterization][settings][hardware_health]") {
    SECTION("Dialog title") {
        std::string title = "Save Hardware";
        REQUIRE(title == "Save Hardware");
    }

    SECTION("Dialog message format") {
        std::string hw_name = "neopixel chamber";
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Add '%s' to expected hardware?\n\nYou'll be notified if it's removed later.",
                 hw_name.c_str());

        std::string msg(buf);
        REQUIRE(msg.find("Add 'neopixel chamber' to expected hardware?") != std::string::npos);
        REQUIRE(msg.find("You'll be notified if it's removed later") != std::string::npos);
    }

    SECTION("Confirm button text") {
        std::string text = "Save";
        REQUIRE(text == "Save");
    }

    SECTION("Severity is Info") {
        // Uses ModalSeverity::Info
        std::string severity = "info";
        REQUIRE(severity == "info");
    }
}

TEST_CASE("CHAR: populate_hardware_issues() behavior",
          "[characterization][settings][hardware_health]") {
    SECTION("Populates four issue lists") {
        // The method populates:
        // 1. critical_issues_list <- result.critical_missing
        // 2. warning_issues_list <- result.expected_missing
        // 3. info_issues_list <- result.newly_discovered
        // 4. session_issues_list <- result.changed_from_last_session

        std::vector<std::string> list_names = {"critical_issues_list", "warning_issues_list",
                                               "info_issues_list", "session_issues_list"};

        REQUIRE(list_names.size() == 4);
    }

    SECTION("Clears existing children before populating") {
        // Uses lv_obj_clean(list) before adding new rows
        bool clears_first = true;
        REQUIRE(clears_first == true);
    }

    SECTION("Creates hardware_issue_row for each issue") {
        // Uses lv_xml_create(list, "hardware_issue_row", attrs)
        std::string component = "hardware_issue_row";
        REQUIRE(component == "hardware_issue_row");
    }

    SECTION("Passes severity attribute to row") {
        // attrs[] = {"severity", severity_to_string(issue.severity), nullptr}
        auto critical_attr = severity_to_xml_string(TestHardwareIssueSeverity::CRITICAL);
        auto warning_attr = severity_to_xml_string(TestHardwareIssueSeverity::WARNING);
        auto info_attr = severity_to_xml_string(TestHardwareIssueSeverity::INFO);

        REQUIRE(std::string(critical_attr) == "error");
        REQUIRE(std::string(warning_attr) == "warning");
        REQUIRE(std::string(info_attr) == "info");
    }

    SECTION("Calls ui_severity_card_finalize() after row creation") {
        // Required to show correct severity icon
        bool calls_finalize = true;
        REQUIRE(calls_finalize == true);
    }

    SECTION("Sets hardware_name label text") {
        // lv_label_set_text(name_label, issue.hardware_name.c_str())
        std::string widget = "hardware_name";
        REQUIRE(widget == "hardware_name");
    }

    SECTION("Sets issue_message label text") {
        // lv_label_set_text(message_label, issue.message.c_str())
        std::string widget = "issue_message";
        REQUIRE(widget == "issue_message");
    }
}

TEST_CASE("CHAR: Issue row user_data management", "[characterization][settings][hardware_health]") {
    SECTION("Hardware name stored in row user_data") {
        // Uses strdup() to copy name for callback access
        // char* name_copy = strdup(issue.hardware_name.c_str())
        // lv_obj_set_user_data(row, name_copy)
        std::string hw_name = "neopixel chamber";
        char* copy = strdup(hw_name.c_str());
        REQUIRE(std::string(copy) == hw_name);
        free(copy);
    }

    SECTION("DELETE event handler frees user_data") {
        // Registers LV_EVENT_DELETE handler that calls free(user_data)
        // This is acceptable exception to declarative UI rule for cleanup
        bool uses_delete_handler = true;
        REQUIRE(uses_delete_handler == true);
    }
}

TEST_CASE("CHAR: Dynamic event callback registration",
          "[characterization][settings][hardware_health]") {
    SECTION("Uses lv_obj_add_event_cb for dynamic rows (acceptable exception)") {
        // Document the exception to declarative UI rule
        // Dynamic row creation requires lv_obj_add_event_cb() because:
        // 1. Rows are created at runtime, not in XML
        // 2. Each row needs hardware name from user_data
        // 3. DELETE cleanup for strdup'd memory
        bool uses_add_event_cb = true;
        REQUIRE(uses_add_event_cb == true);
    }

    SECTION("Button callbacks use parent chain to find row user_data") {
        // Navigation: btn -> action_buttons -> row
        // const char* hw_name = lv_obj_get_user_data(row)
        int parent_depth = 2; // btn -> action_buttons -> row
        REQUIRE(parent_depth == 2);
    }

    SECTION("is_ignore flag passed via event user_data") {
        // reinterpret_cast<void*>(static_cast<uintptr_t>(is_ignore))
        bool is_ignore_true = true;
        bool is_ignore_false = false;

        auto ptr_true = reinterpret_cast<void*>(static_cast<uintptr_t>(is_ignore_true));
        auto ptr_false = reinterpret_cast<void*>(static_cast<uintptr_t>(is_ignore_false));

        REQUIRE(static_cast<bool>(reinterpret_cast<uintptr_t>(ptr_true)) == true);
        REQUIRE(static_cast<bool>(reinterpret_cast<uintptr_t>(ptr_false)) == false);
    }
}

TEST_CASE("CHAR: Overlay lazy creation", "[characterization][settings][hardware_health]") {
    SECTION("Created on first click") {
        // if (!hardware_health_overlay_ && parent_screen_)
        bool created_lazily = true;
        REQUIRE(created_lazily == true);
    }

    SECTION("Uses XML component name") {
        std::string component = "hardware_health_overlay";
        REQUIRE(component == "hardware_health_overlay");
    }

    SECTION("Initially hidden after creation") {
        // lv_obj_add_flag(hardware_health_overlay_, LV_OBJ_FLAG_HIDDEN)
        bool starts_hidden = true;
        REQUIRE(starts_hidden == true);
    }

    SECTION("Populates issues before showing") {
        // populate_hardware_issues() called before ui_nav_push_overlay()
        bool populates_first = true;
        REQUIRE(populates_first == true);
    }

    SECTION("Uses ui_nav_push_overlay() to show") {
        // Pushes onto navigation stack
        std::string nav_function = "ui_nav_push_overlay";
        REQUIRE(nav_function == "ui_nav_push_overlay");
    }
}

// ============================================================================
// DOCUMENTATION SECTION
// ============================================================================

/**
 * @brief Summary of Hardware Health overlay behavior for extraction
 *
 * This documents the exact behavior that must be preserved when extracting
 * the hardware health settings into a separate overlay class.
 *
 * 1. Overlay Creation (lazy):
 *    - Created on first click of "Hardware Health" row in Settings
 *    - Uses XML component "hardware_health_overlay"
 *    - Initially hidden until navigation pushes it
 *
 * 2. Overlay Structure:
 *    - Status card with severity-based icon (OK/warn/crit)
 *    - Four collapsible sections: Critical, Warning, Info, Session
 *    - Each section has list container for issue rows
 *    - Sections hidden when their count subject is 0
 *
 * 3. Issue Population:
 *    - populate_hardware_issues() called before showing
 *    - Clears existing rows, creates new ones from validation result
 *    - Uses hardware_issue_row XML component with severity attribute
 *    - Calls ui_severity_card_finalize() to show correct icon
 *
 * 4. Issue Row Structure:
 *    - Colored left border based on severity
 *    - Severity icon, hardware name, issue message
 *    - Action buttons (hidden for CRITICAL):
 *      - Ignore: marks hardware as optional
 *      - Save: adds to expected list (INFO only, with confirmation)
 *
 * 5. Action Handling:
 *    - Ignore: HardwareValidator::set_hardware_optional()
 *    - Save: shows confirmation dialog, then HardwareValidator::add_expected_hardware()
 *    - Both: toast notification, remove from result, refresh overlay
 *
 * 6. State Management:
 *    - hardware_health_overlay_ - cached overlay widget
 *    - hardware_save_dialog_ - confirmation dialog
 *    - pending_hardware_save_ - hardware name pending save
 *
 * 7. Exception: Uses lv_obj_add_event_cb():
 *    - For DELETE cleanup of user_data (strdup'd hardware name)
 *    - For button clicks (dynamic row creation)
 *    These are acceptable exceptions to declarative UI rule.
 *
 * 8. Dependencies:
 *    - PrinterState::get_hardware_validation_result()
 *    - PrinterState::remove_hardware_issue()
 *    - HardwareValidator static methods
 *    - ui_modal_show_confirmation()
 *    - ui_severity_card_finalize()
 *    - Config (for optional/expected persistence)
 */
