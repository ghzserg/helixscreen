// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_wizard_printer_identify.h"

#include "ui_event_safety.h"
#include "ui_keyboard.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_wizard.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "printer_detector.h"
#include "printer_types.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

// ============================================================================
// Static Data & Subjects
// ============================================================================

// Extern declaration for global connection_test_passed subject (defined in ui_wizard.cpp)
extern lv_subject_t connection_test_passed;

// Subject declarations (module scope)
static lv_subject_t printer_name;
static lv_subject_t printer_type_selected;
static lv_subject_t printer_detection_status;

// String buffers (must be persistent)
static char printer_name_buffer[128];
static char printer_detection_status_buffer[256];

// Screen instance
static lv_obj_t* printer_identify_screen_root = nullptr;

// Validation state
static bool printer_identify_validated = false;

// ============================================================================
// Auto-Detection Infrastructure (Placeholder for Phase 3)
// ============================================================================

/**
 * @brief Printer auto-detection hint (confidence + reasoning)
 *
 * Future integration point for printer auto-detection heuristics.
 * Phase 3 will query MoonrakerClient for discovered hardware and
 * use pattern matching to suggest printer type.
 */
struct PrinterDetectionHint {
    int type_index;     // Index into PrinterTypes::PRINTER_TYPES_ROLLER
    int confidence;     // 0-100 (≥70 = auto-select, <70 = suggest)
    std::string reason; // Human-readable detection reasoning
};

/**
 * @brief Find index of printer name in PRINTER_TYPES_ROLLER
 * @param printer_name Printer type name to search for
 * @return Index in roller (0-32), or DEFAULT_PRINTER_TYPE_INDEX if not found
 */
static int find_printer_type_index(const std::string& printer_name) {
    std::istringstream stream(PrinterTypes::PRINTER_TYPES_ROLLER);
    std::string line;
    int index = 0;

    while (std::getline(stream, line)) {
        if (line == printer_name) {
            return index;
        }
        index++;
    }

    return PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX; // "Unknown"
}

/**
 * @brief Detect printer type from hardware discovery data
 *
 * Integrates with PrinterDetector to analyze discovered hardware and suggest
 * printer type based on fingerprinting heuristics (sensors, fans, hostname).
 *
 * @return Detection hint with confidence and reasoning
 */
static PrinterDetectionHint detect_printer_type() {
    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        spdlog::debug("[Wizard Printer] No MoonrakerClient available for auto-detection");
        return {PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX, 0, "No printer connection available"};
    }

    // Build hardware data from MoonrakerClient discovery
    PrinterHardwareData hardware;
    hardware.heaters = client->get_heaters();
    hardware.sensors = client->get_sensors();
    hardware.fans = client->get_fans();
    hardware.leds = client->get_leds();
    hardware.hostname = client->get_hostname();

    // Run detection engine
    PrinterDetectionResult result = PrinterDetector::detect(hardware);

    if (result.confidence == 0) {
        // No match found
        return {PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX, 0, result.reason};
    }

    // Map detected type_name to roller index
    int type_index = find_printer_type_index(result.type_name);

    if (type_index == PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX && result.confidence > 0) {
        // Detected a printer but it's not in our roller list
        spdlog::warn(
            "[Wizard Printer] Detected '{}' ({}% confident) but not found in PRINTER_TYPES_ROLLER",
            result.type_name, result.confidence);
        return {PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX, result.confidence,
                "Detected: " + result.type_name + " (" + result.reason +
                    ") - not in dropdown list"};
    }

    spdlog::info("[Wizard Printer] Auto-detected: {} (confidence: {}, reason: {})",
                 result.type_name, result.confidence, result.reason);

    return {type_index, result.confidence, result.reason};
}

// ============================================================================
// Subject Initialization
// ============================================================================

void ui_wizard_printer_identify_init_subjects() {
    spdlog::debug("[Wizard Printer] Initializing subjects");

    // Load existing values from config if available
    Config* config = Config::get_instance();
    std::string default_name = "";
    std::string saved_type = "";
    int default_type = PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX;

    try {
        default_name = config->get<std::string>("/printer/name", "");
        saved_type = config->get<std::string>("/printer/type", "");

        // Dynamic lookup: find index by type name
        if (!saved_type.empty()) {
            default_type = find_printer_type_index(saved_type);
            spdlog::debug(
                "[Wizard Printer] Loaded from config: name='{}', type='{}', resolved index={}",
                default_name, saved_type, default_type);
        } else {
            spdlog::debug("[Wizard Printer] Loaded from config: name='{}', no type saved",
                          default_name);
        }
    } catch (const std::exception& e) {
        spdlog::debug("[Wizard Printer] No existing config, using defaults");
    }

    // Initialize with values from config or defaults
    strncpy(printer_name_buffer, default_name.c_str(), sizeof(printer_name_buffer) - 1);
    printer_name_buffer[sizeof(printer_name_buffer) - 1] = '\0';

    UI_SUBJECT_INIT_AND_REGISTER_STRING(printer_name, printer_name_buffer, printer_name_buffer, "printer_name");

    // Run auto-detection if no saved type
    PrinterDetectionHint hint{PrinterTypes::DEFAULT_PRINTER_TYPE_INDEX, 0, ""};
    if (saved_type.empty()) {
        hint = detect_printer_type();
        if (hint.confidence >= 70) {
            // High-confidence detection overrides default
            default_type = hint.type_index;
            spdlog::info("[Wizard Printer] Auto-detection: {} (confidence: {}%)", hint.reason,
                         hint.confidence);
        } else if (hint.confidence > 0) {
            spdlog::info("[Wizard Printer] Auto-detection suggestion: {} (confidence: {}%)",
                         hint.reason, hint.confidence);
        } else {
            spdlog::debug("[Wizard Printer] Auto-detection: {}", hint.reason);
        }
    }

    UI_SUBJECT_INIT_AND_REGISTER_INT(printer_type_selected, default_type, "printer_type_selected");

    // Initialize detection status message (auto-detection results only, not validation)
    const char* status_msg;
    if (!saved_type.empty()) {
        status_msg = "Loaded from configuration";
    } else if (hint.confidence >= 70) {
        // High-confidence detection: show what was detected
        snprintf(printer_detection_status_buffer, sizeof(printer_detection_status_buffer),
                 "Detected: %s", hint.reason.c_str());
        status_msg = printer_detection_status_buffer;
    } else if (hint.confidence > 0) {
        // Low-confidence suggestion
        snprintf(printer_detection_status_buffer, sizeof(printer_detection_status_buffer),
                 "Possible: %s (low confidence)", hint.reason.c_str());
        status_msg = printer_detection_status_buffer;
    } else {
        // No detection results
        status_msg = "No printer detected - please confirm type";
    }

    UI_SUBJECT_INIT_AND_REGISTER_STRING(printer_detection_status, printer_detection_status_buffer, status_msg, "printer_detection_status");

    // Initialize validation state and connection_test_passed based on loaded name
    printer_identify_validated = (default_name.length() > 0);

    // Control Next button reactively: enable if name exists, disable if empty
    int button_state = printer_identify_validated ? 1 : 0;
    lv_subject_set_int(&connection_test_passed, button_state);

    spdlog::info("[Wizard Printer] Subjects initialized (validation: {}, button_state: {})",
                 printer_identify_validated ? "valid" : "invalid", button_state);
}

// ============================================================================
// Event Handlers
// ============================================================================

/**
 * @brief Handle printer name textarea changes with enhanced validation
 *
 * Validates input, trims whitespace, updates reactive button control.
 * Validation feedback is shown via textarea error state (red border).
 * Config is persisted during cleanup, not on each keystroke.
 */
LVGL_SAFE_EVENT_CB_WITH_EVENT(on_printer_name_changed, event, {
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(event);
    const char* text = lv_textarea_get_text(ta);

    // Trim leading/trailing whitespace for validation
    std::string trimmed(text);
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r\f\v"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r\f\v") + 1);

    // Log if trimming made a difference
    if (trimmed != text) {
        spdlog::debug("[Wizard Printer] Name changed (trimmed): '{}' -> '{}'", text, trimmed);
    } else {
        spdlog::debug("[Wizard Printer] Name changed: '{}'", text);
    }

    // Update subject with raw text (let user keep their spaces if they want)
    lv_subject_copy_string(&printer_name, text);

    // Validate trimmed length and check max size
    const size_t max_length = sizeof(printer_name_buffer) - 1; // 127
    bool is_empty = (trimmed.length() == 0);
    bool is_too_long = (trimmed.length() > max_length);
    bool is_valid = !is_empty && !is_too_long;

    // Update validation state
    printer_identify_validated = is_valid;

    // Update connection_test_passed reactively (controls Next button)
    lv_subject_set_int(&connection_test_passed, printer_identify_validated ? 1 : 0);

    // Apply error state to textarea for validation feedback
    if (is_too_long) {
        // Show error state: red border for "too long" error
        lv_color_t error_color = ui_theme_get_color("error_color");
        lv_obj_set_style_border_color(ta, error_color, LV_PART_MAIN);
        lv_obj_set_style_border_width(ta, 2, LV_PART_MAIN);
        spdlog::debug("[Wizard Printer] Validation: name too long ({} > {})", trimmed.length(),
                      max_length);
    } else if (!is_empty) {
        // Valid input: use secondary color border
        lv_color_t valid_color = ui_theme_get_color("secondary_color");
        lv_obj_set_style_border_color(ta, valid_color, LV_PART_MAIN);
        lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    } else {
        // Empty but not an error: neutral state (default border)
        lv_obj_remove_style(ta, nullptr, LV_PART_MAIN | LV_STATE_ANY);
    }

    // Detection status label remains unchanged (shows auto-detection results only)
})

/**
 * @brief Handle printer type roller changes
 */
LVGL_SAFE_EVENT_CB_WITH_EVENT(on_printer_type_changed, event, {
    lv_obj_t* roller = (lv_obj_t*)lv_event_get_target(event);
    uint16_t selected = lv_roller_get_selected(roller);

    char buf[64];
    lv_roller_get_selected_str(roller, buf, sizeof(buf));

    spdlog::debug("[Wizard Printer] Type changed: index {} ({})", selected, buf);

    // Update subject
    lv_subject_set_int(&printer_type_selected, selected);

    // Config will be persisted on cleanup (saves type name, not index)
})

// ============================================================================
// Callback Registration
// ============================================================================

void ui_wizard_printer_identify_register_callbacks() {
    spdlog::debug("[Wizard Printer] Registering event callbacks");

    // Register callbacks with lv_xml system
    lv_xml_register_event_cb(nullptr, "on_printer_name_changed", on_printer_name_changed);
    lv_xml_register_event_cb(nullptr, "on_printer_type_changed", on_printer_type_changed);

    spdlog::info("[Wizard Printer] Event callbacks registered");
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* ui_wizard_printer_identify_create(lv_obj_t* parent) {
    spdlog::debug("[Wizard Printer] Creating printer identification screen");

    if (!parent) {
        spdlog::error("[Wizard Printer] Cannot create: null parent");
        return nullptr;
    }

    // Create from XML
    printer_identify_screen_root =
        (lv_obj_t*)lv_xml_create(parent, "wizard_printer_identify", nullptr);

    if (!printer_identify_screen_root) {
        spdlog::error("[Wizard Printer] Failed to create from XML");
        return nullptr;
    }

    // Find and set up the roller with printer types
    lv_obj_t* roller = lv_obj_find_by_name(printer_identify_screen_root, "printer_type_roller");
    if (roller) {
        lv_roller_set_options(roller, PrinterTypes::PRINTER_TYPES_ROLLER, LV_ROLLER_MODE_NORMAL);

        // Set to the saved selection
        int selected = lv_subject_get_int(&printer_type_selected);
        lv_roller_set_selected(roller, selected, LV_ANIM_OFF);

        // Attach change handler
        lv_obj_add_event_cb(roller, on_printer_type_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        spdlog::debug("[Wizard Printer] Roller configured with {} options",
                      PrinterTypes::PRINTER_TYPE_COUNT);
    } else {
        spdlog::warn("[Wizard Printer] Roller not found in XML");
    }

    // Find and set up the name textarea
    lv_obj_t* name_ta = lv_obj_find_by_name(printer_identify_screen_root, "printer_name_input");
    if (name_ta) {
        // Set initial value from subject (bind_text doesn't set initial value for textareas)
        lv_textarea_set_text(name_ta, printer_name_buffer);

        // Register validation handler for button enable/disable
        lv_obj_add_event_cb(name_ta, on_printer_name_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        ui_keyboard_register_textarea(name_ta);
        spdlog::debug("[Wizard Printer] Name textarea configured with keyboard and validation "
                      "(initial: '{}')",
                      printer_name_buffer);
    }

    // Update layout
    lv_obj_update_layout(printer_identify_screen_root);

    spdlog::info("[Wizard Printer] Screen created successfully");
    return printer_identify_screen_root;
}

// ============================================================================
// Cleanup
// ============================================================================

void ui_wizard_printer_identify_cleanup() {
    spdlog::debug("[Wizard Printer] Cleaning up printer identification screen");

    // Save current subject values to config before persisting
    Config* config = Config::get_instance();
    try {
        // Get current name from subject buffer
        std::string current_name(printer_name_buffer);

        // Trim whitespace
        current_name.erase(0, current_name.find_first_not_of(" \t\n\r\f\v"));
        current_name.erase(current_name.find_last_not_of(" \t\n\r\f\v") + 1);

        // Save printer name if valid
        if (current_name.length() > 0) {
            config->set("/printer/name", current_name);
            spdlog::debug("[Wizard Printer] Saving printer name to config: '{}'", current_name);
        }

        // Get current type index and convert to type name
        int type_index = lv_subject_get_int(&printer_type_selected);

        // Extract type name from PRINTER_TYPES_ROLLER by index
        std::istringstream stream(PrinterTypes::PRINTER_TYPES_ROLLER);
        std::string line;
        int idx = 0;
        std::string type_name = "Unknown"; // Default fallback

        while (std::getline(stream, line)) {
            if (idx == type_index) {
                type_name = line;
                break;
            }
            idx++;
        }

        // Save printer type name (not index)
        config->set("/printer/type", type_name);
        spdlog::debug("[Wizard Printer] Saving printer type to config: '{}' (index {})", type_name,
                      type_index);

        // Persist config changes to disk
        if (config->save()) {
            spdlog::info("[Wizard Printer] Saved printer identification settings to config");
        } else {
            spdlog::error("[Wizard Printer] Failed to save printer configuration to disk!");
        }
    } catch (const std::exception& e) {
        spdlog::error("[Wizard Printer] Failed to save config: {}", e.what());
    }

    // Reset UI references
    printer_identify_screen_root = nullptr;

    // Reset connection_test_passed to enabled (1) for other wizard steps
    lv_subject_set_int(&connection_test_passed, 1);

    spdlog::info("[Wizard Printer] Cleanup complete");
}

// ============================================================================
// Utility Functions
// ============================================================================

bool ui_wizard_printer_identify_is_validated() {
    return printer_identify_validated;
}