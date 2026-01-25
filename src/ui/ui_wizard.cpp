// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard.h"

#include "ui_error_reporting.h"
#include "ui_panel_home.h"
#include "ui_subject_registry.h"
#include "ui_wizard_ams_identify.h"
#include "ui_wizard_connection.h"
#include "ui_wizard_fan_select.h"
#include "ui_wizard_filament_sensor_select.h"
#include "ui_wizard_heater_select.h"
#include "ui_wizard_input_shaper.h"
#include "ui_wizard_led_select.h"
#include "ui_wizard_printer_identify.h"
#include "ui_wizard_summary.h"
#include "ui_wizard_touch_calibration.h"
#include "ui_wizard_wifi.h"

#include "ams_state.h"
#include "app_globals.h"
#include "config.h"
#include "filament_sensor_manager.h"
#include "hardware_validator.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "runtime_config.h"
#include "subject_managed_panel.h"
#include "theme_manager.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstdio>

// Subject declarations (static/global scope required)
static lv_subject_t current_step;
static lv_subject_t total_steps;
static lv_subject_t wizard_title;
static lv_subject_t wizard_progress;
static lv_subject_t wizard_back_visible;

// Non-static: accessible from other wizard step files
lv_subject_t connection_test_passed;  // Global: 0=connection not validated, 1=validated or N/A
lv_subject_t wizard_next_button_text; // Global: accessible from touch calibration step
lv_subject_t wizard_subtitle;         // Global: accessible for dynamic subtitle updates

// SubjectManager for RAII cleanup of wizard subjects
static SubjectManager wizard_subjects_;

// String buffers (must be persistent)
static char wizard_title_buffer[64];
static char wizard_progress_buffer[32];
static char wizard_next_button_text_buffer[16];
static char wizard_subtitle_buffer[128];

// Wizard container instance
static lv_obj_t* wizard_container = nullptr;

// Track current screen for proper cleanup (-1 = no screen loaded yet)
static int current_screen_step = -1;

// Track if touch calibration step (0) is being skipped - not fbdev or already calibrated
static bool touch_cal_step_skipped = false;

// Track if AMS step (6) is being skipped - no AMS detected
static bool ams_step_skipped = false;

// Track if LED step (7) is being skipped - no LEDs discovered
static bool led_step_skipped = false;

// Track if filament sensor step (8) is being skipped - <2 standalone sensors
static bool filament_step_skipped = false;

// Track if input shaper step (9) is being skipped - no accelerometer
static bool input_shaper_step_skipped = false;

// Forward declarations
static void on_back_clicked(lv_event_t* e);
static void on_next_clicked(lv_event_t* e);
static void ui_wizard_load_screen(int step);
static void ui_wizard_cleanup_current_screen();
static const char* get_step_title_from_xml(int step);
static const char* get_step_subtitle_from_xml(int step);

// ============================================================================
// Step Metadata (read from XML <consts>)
// ============================================================================

/**
 * Map step number to XML component name
 * Each component defines its own step_title in its <consts> block
 */
static const char* const STEP_COMPONENT_NAMES[] = {
    "wizard_touch_calibration",      // 0 (may be skipped on non-fbdev)
    "wizard_wifi_setup",             // 1
    "wizard_connection",             // 2
    "wizard_printer_identify",       // 3
    "wizard_heater_select",          // 4
    "wizard_fan_select",             // 5
    "wizard_ams_identify",           // 6 (may be skipped if no AMS)
    "wizard_led_select",             // 7 (may be skipped if no LEDs)
    "wizard_filament_sensor_select", // 8 (may be skipped if <2 sensors)
    "wizard_input_shaper",           // 9 (may be skipped if no accelerometer)
    "wizard_summary"                 // 10
};
static constexpr int STEP_COMPONENT_COUNT = 10; // Last step number (summary at step 10)

/**
 * Get step title from XML component's <consts> block
 *
 * Each wizard step XML file defines:
 *   <consts>
 *     <str name="step_title" value="WiFi Setup"/>
 *     <int name="step_order" value="1"/>
 *   </consts>
 *
 * This function reads step_title from the component's scope at runtime,
 * eliminating hardcoded title strings in C++.
 */
static const char* get_step_title_from_xml(int step) {
    if (step < 0 || step > STEP_COMPONENT_COUNT) {
        spdlog::warn("[Wizard] Invalid step {} for title lookup", step);
        return "Unknown Step";
    }

    const char* comp_name = STEP_COMPONENT_NAMES[step];
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope(comp_name);
    if (!scope) {
        spdlog::warn("[Wizard] Component scope not found for '{}'", comp_name);
        return "Unknown Step";
    }

    const char* title = lv_xml_get_const(scope, "step_title");
    if (!title) {
        spdlog::warn("[Wizard] step_title not found in '{}' consts", comp_name);
        return "Unknown Step";
    }

    return title;
}

/**
 * Get step subtitle from XML component's <consts> block
 *
 * Subtitles provide contextual hints (e.g., "Skip if using Ethernet")
 * that appear below the title in the wizard header.
 */
static const char* get_step_subtitle_from_xml(int step) {
    if (step < 0 || step > STEP_COMPONENT_COUNT) {
        return "";
    }

    const char* comp_name = STEP_COMPONENT_NAMES[step];
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope(comp_name);
    if (!scope) {
        return "";
    }

    const char* subtitle = lv_xml_get_const(scope, "step_subtitle");
    return subtitle ? subtitle : "";
}

// Track if subjects have been initialized (to avoid double-deinit)
static bool wizard_subjects_initialized = false;

void ui_wizard_init_subjects() {
    spdlog::debug("[Wizard] Initializing subjects");

    // Initialize subjects with defaults using managed macros for RAII cleanup
    UI_MANAGED_SUBJECT_INT(current_step, 1, "current_step", wizard_subjects_);
    UI_MANAGED_SUBJECT_INT(total_steps, 10, "total_steps",
                           wizard_subjects_); // 10 steps: WiFi, Connection, Printer, Heater,
                                              // Fan, AMS, LED, Filament, Input Shaper, Summary

    UI_MANAGED_SUBJECT_STRING(wizard_title, wizard_title_buffer, "Welcome", "wizard_title",
                              wizard_subjects_);
    UI_MANAGED_SUBJECT_STRING(wizard_progress, wizard_progress_buffer, "Step 1 of 10",
                              "wizard_progress", wizard_subjects_);
    UI_MANAGED_SUBJECT_STRING(wizard_next_button_text, wizard_next_button_text_buffer, "Next",
                              "wizard_next_button_text", wizard_subjects_);
    UI_MANAGED_SUBJECT_STRING(wizard_subtitle, wizard_subtitle_buffer, "", "wizard_subtitle",
                              wizard_subjects_);

    // Initialize connection_test_passed to 1 (enabled by default for all steps)
    // Step 2 (connection) will set it to 0 until test passes
    UI_MANAGED_SUBJECT_INT(connection_test_passed, 1, "connection_test_passed", wizard_subjects_);

    // Initialize wizard_back_visible to 1 (visible by default)
    // Step navigation will hide it when at first visible step
    UI_MANAGED_SUBJECT_INT(wizard_back_visible, 1, "wizard_back_visible", wizard_subjects_);

    wizard_subjects_initialized = true;
    spdlog::debug("[Wizard] Subjects initialized ({} subjects registered)",
                  wizard_subjects_.count());
}

void ui_wizard_deinit_subjects() {
    if (!wizard_subjects_initialized) {
        return;
    }
    // Use SubjectManager for RAII cleanup - handles all registered subjects
    wizard_subjects_.deinit_all();
    wizard_subjects_initialized = false;
    spdlog::debug("[Wizard] Subjects deinitialized");
}

// Helper type for constant name/value pairs
struct WizardConstant {
    const char* name;
    const char* value;
};

// Helper: Register array of constants to a scope
static void register_constants_to_scope(lv_xml_component_scope_t* scope,
                                        const WizardConstant* constants) {
    if (!scope)
        return;
    for (int i = 0; constants[i].name != NULL; i++) {
        lv_xml_register_const(scope, constants[i].name, constants[i].value);
    }
}

void ui_wizard_container_register_responsive_constants() {
    spdlog::debug("[Wizard] Registering responsive constants to wizard_container scope");

    // Detect screen size using custom breakpoints
    lv_display_t* display = lv_display_get_default();
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    // Determine button width based on breakpoint (only responsive constant remaining)
    const char* button_width;
    const char* size_label;

    if (greater_res <= UI_BREAKPOINT_SMALL_MAX) { // ≤480: 480x320
        button_width = "110";
        size_label = "SMALL";
    } else if (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) { // 481-800: 800x480
        button_width = "140";
        size_label = "MEDIUM";
    } else { // >800: 1024x600+
        button_width = "160";
        size_label = "LARGE";
    }

    spdlog::debug("[Wizard] Screen size: {} (greater_res={}px)", size_label, greater_res);

    // Register button width constant
    WizardConstant constants[] = {
        {"wizard_button_width", button_width}, {NULL, NULL} // Sentinel
    };

    // Register to wizard_container scope (parent)
    lv_xml_component_scope_t* parent_scope = lv_xml_component_get_scope("wizard_container");
    register_constants_to_scope(parent_scope, constants);

    // Define child components that inherit this constant
    const char* children[] = {
        "wizard_touch_calibration",
        "wizard_wifi_setup",
        "wizard_connection",
        "wizard_printer_identify",
        "wizard_heater_select",
        "wizard_fan_select",
        "wizard_ams_identify",
        "wizard_led_select",
        "wizard_filament_sensor_select",
        "wizard_input_shaper",
        "wizard_summary",
        NULL // Sentinel
    };

    // Propagate to all children
    int child_count = 0;
    for (int i = 0; children[i] != NULL; i++) {
        lv_xml_component_scope_t* child_scope = lv_xml_component_get_scope(children[i]);
        if (child_scope) {
            register_constants_to_scope(child_scope, constants);
            child_count++;
        }
    }

    spdlog::debug(
        "[Wizard] Registered wizard_button_width={} to wizard_container and {} child components",
        button_width, child_count);
}

void ui_wizard_register_event_callbacks() {
    spdlog::debug("[Wizard] Registering event callbacks");
    lv_xml_register_event_cb(nullptr, "on_back_clicked", on_back_clicked);
    lv_xml_register_event_cb(nullptr, "on_next_clicked", on_next_clicked);
}

lv_obj_t* ui_wizard_create(lv_obj_t* parent) {
    spdlog::debug("[Wizard] Creating wizard container");

    // Create wizard from XML (constants already registered)
    wizard_container = (lv_obj_t*)lv_xml_create(parent, "wizard_container", nullptr);

    if (!wizard_container) {
        spdlog::error("[Wizard] Failed to create wizard_container from XML");
        return nullptr;
    }

    // Background color applied automatically by LVGL theme (uses theme->color_card)
    // No explicit styling needed - theme patching in ui_theme.cpp handles this

    // Update layout to ensure SIZE_CONTENT calculates correctly
    lv_obj_update_layout(wizard_container);

    spdlog::debug("[Wizard] Wizard container created successfully");
    return wizard_container;
}

void ui_wizard_navigate_to_step(int step) {
    spdlog::debug("[Wizard] Navigating to step {}", step);

    // Clamp step to valid range (internal steps are 0-9)
    if (step < 0)
        step = 0;
    if (step > STEP_COMPONENT_COUNT)
        step = STEP_COMPONENT_COUNT;

    // Reset skip flags when starting wizard from the beginning
    // This ensures correct behavior if wizard is restarted after hardware changes
    if (step == 0) {
        touch_cal_step_skipped = false;
        ams_step_skipped = false;
        led_step_skipped = false;
        filament_step_skipped = false;
        input_shaper_step_skipped = false;

        // Auto-skip touch calibration step if not needed
        if (get_wizard_touch_calibration_step()->should_skip()) {
            touch_cal_step_skipped = true;
            step = 1;
            spdlog::debug("[Wizard] Skipping touch calibration step");
        }
    }

    // Calculate display step and total for progress indicator
    // When steps are skipped, we adjust the display numbers:
    // - Touch cal skip (0): step 1+ displays as-is (touch cal was step 1 when shown)
    // - AMS skip (6): steps 7+ display one lower
    // - LED skip (7): steps 8+ display one lower
    // - Filament skip (8): steps 9+ display one lower
    // - Input shaper skip (9): steps 10+ display one lower
    // Total = 11 - skipped_count (steps 0-10 = 11 total)
    int display_step = step + 1; // Convert internal step (0-based) to 1-based display
    if (touch_cal_step_skipped)
        display_step--;
    if (ams_step_skipped && step > 6)
        display_step--;
    if (led_step_skipped && step > 7)
        display_step--;
    if (filament_step_skipped && step > 8)
        display_step--;
    if (input_shaper_step_skipped && step > 9)
        display_step--;

    int display_total = 11; // Steps 0-10 = 11 total
    if (touch_cal_step_skipped)
        display_total--;
    if (ams_step_skipped)
        display_total--;
    if (led_step_skipped)
        display_total--;
    if (filament_step_skipped)
        display_total--;
    if (input_shaper_step_skipped)
        display_total--;

    // Update current_step subject (internal step number for UI bindings)
    lv_subject_set_int(&current_step, step);

    // Update Back button visibility based on whether we can go back
    int min_step = touch_cal_step_skipped ? 1 : 0;
    lv_subject_set_int(&wizard_back_visible, (step > min_step) ? 1 : 0);

    // Determine if this is the last step (summary is always step 10 internally)
    bool is_last_step = (step == 10);

    // Update next button text based on step
    if (is_last_step) {
        lv_subject_copy_string(&wizard_next_button_text, "Finish");
    } else {
        lv_subject_copy_string(&wizard_next_button_text, "Next");
    }

    // Update progress text with display values
    char progress_buf[32];
    snprintf(progress_buf, sizeof(progress_buf), "Step %d of %d", display_step, display_total);
    lv_subject_copy_string(&wizard_progress, progress_buf);

    // Load screen content (uses internal step number)
    ui_wizard_load_screen(step);

    // Force layout update on entire wizard after screen is loaded
    if (wizard_container) {
        lv_obj_update_layout(wizard_container);
    }

    spdlog::debug("[Wizard] Updated to step {}/{} (internal: {}), button: {}", display_step,
                  display_total, step, is_last_step ? "Finish" : "Next");
}

void ui_wizard_set_title(const char* title) {
    if (!title) {
        spdlog::warn("[Wizard] set_title called with nullptr, ignoring");
        return;
    }

    spdlog::debug("[Wizard] Setting title: {}", title);
    lv_subject_copy_string(&wizard_title, title);
}

// ============================================================================
// Screen Cleanup
// ============================================================================

/**
 * Cleanup the current wizard screen before navigating to a new one
 *
 * Calls the appropriate cleanup function based on current_screen_step.
 * This ensures resources are properly released and screen pointers are reset.
 */
static void ui_wizard_cleanup_current_screen() {
    if (current_screen_step < 0) {
        return; // No screen loaded yet
    }

    spdlog::debug("[Wizard] Cleaning up screen for step {}", current_screen_step);

    switch (current_screen_step) {
    case 0: // Touch Calibration
        get_wizard_touch_calibration_step()->cleanup();
        break;
    case 1: // WiFi Setup
        get_wizard_wifi_step()->cleanup();
        break;
    case 2: // Moonraker Connection
        get_wizard_connection_step()->cleanup();
        break;
    case 3: // Printer Identification
        get_wizard_printer_identify_step()->cleanup();
        break;
    case 4: // Heater Select (combined bed + hotend)
        get_wizard_heater_select_step()->cleanup();
        break;
    case 5: // Fan Select
        get_wizard_fan_select_step()->cleanup();
        break;
    case 6: // AMS Identify
        get_wizard_ams_identify_step()->cleanup();
        break;
    case 7: // LED Select
        get_wizard_led_select_step()->cleanup();
        break;
    case 8: // Filament Sensor Select
        get_wizard_filament_sensor_select_step()->cleanup();
        break;
    case 9: // Input Shaper
        get_wizard_input_shaper_step()->cleanup();
        break;
    case 10: // Summary
        get_wizard_summary_step()->cleanup();
        break;
    default:
        spdlog::warn("[Wizard] Unknown screen step {} during cleanup", current_screen_step);
        break;
    }
}

// ============================================================================
// Screen Loading
// ============================================================================

static void ui_wizard_load_screen(int step) {
    spdlog::debug("[Wizard] Loading screen for step {}", step);

    // Find wizard_content container
    lv_obj_t* content = lv_obj_find_by_name(wizard_container, "wizard_content");
    if (!content) {
        spdlog::error("[Wizard] wizard_content container not found");
        return;
    }

    // Cleanup previous screen resources BEFORE clearing widgets
    ui_wizard_cleanup_current_screen();

    // Clear existing content (widgets)
    lv_obj_clean(content);
    spdlog::debug("[Wizard] Cleared wizard_content container");

    // Set title and subtitle from XML metadata (no more hardcoded strings!)
    const char* title = get_step_title_from_xml(step);
    ui_wizard_set_title(title);
    const char* subtitle = get_step_subtitle_from_xml(step);
    lv_subject_copy_string(&wizard_subtitle, subtitle);

    // Create appropriate screen based on step
    // Note: Step-specific initialization remains in switch because each step
    // has unique logic (WiFi needs init_wifi_manager, etc.)
    switch (step) {
    case 0: // Touch Calibration
        spdlog::debug("[Wizard] Creating touch calibration screen");
        // Reset Next button to enabled - touch cal uses its own completion logic
        lv_subject_set_int(&connection_test_passed, 1);
        get_wizard_touch_calibration_step()->init_subjects();
        get_wizard_touch_calibration_step()->register_callbacks();
        get_wizard_touch_calibration_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 1: // WiFi Setup
        spdlog::debug("[Wizard] Creating WiFi setup screen");
        // Reset Next button to enabled - WiFi step has no connection test requirement
        lv_subject_set_int(&connection_test_passed, 1);
        get_wizard_wifi_step()->init_subjects();
        get_wizard_wifi_step()->register_callbacks();
        get_wizard_wifi_step()->create(content);
        lv_obj_update_layout(content);
        get_wizard_wifi_step()->init_wifi_manager();
        break;

    case 2: // Moonraker Connection
        spdlog::debug("[Wizard] Creating Moonraker connection screen");
        get_wizard_connection_step()->init_subjects();
        get_wizard_connection_step()->register_callbacks();
        get_wizard_connection_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 3: // Printer Identification
        spdlog::debug("[Wizard] Creating printer identification screen");
        get_wizard_printer_identify_step()->init_subjects();
        get_wizard_printer_identify_step()->register_callbacks();
        get_wizard_printer_identify_step()->create(content);
        lv_obj_update_layout(content);
        // Override subtitle with dynamic detection status
        lv_subject_copy_string(&wizard_subtitle,
                               get_wizard_printer_identify_step()->get_detection_status());
        break;

    case 4: // Heater Select (combined bed + hotend)
        spdlog::debug("[Wizard] Creating heater select screen");
        get_wizard_heater_select_step()->init_subjects();
        get_wizard_heater_select_step()->register_callbacks();
        get_wizard_heater_select_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 5: // Fan Select
        spdlog::debug("[Wizard] Creating fan select screen");
        get_wizard_fan_select_step()->init_subjects();
        get_wizard_fan_select_step()->register_callbacks();
        get_wizard_fan_select_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 6: // AMS Identify
        spdlog::debug("[Wizard] Creating AMS identify screen");
        get_wizard_ams_identify_step()->init_subjects();
        get_wizard_ams_identify_step()->register_callbacks();
        (void)get_wizard_ams_identify_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 7: // LED Select
        spdlog::debug("[Wizard] Creating LED select screen");
        get_wizard_led_select_step()->init_subjects();
        get_wizard_led_select_step()->register_callbacks();
        get_wizard_led_select_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 8: // Filament Sensor Select
        spdlog::debug("[Wizard] Creating filament sensor select screen");
        get_wizard_filament_sensor_select_step()->init_subjects();
        get_wizard_filament_sensor_select_step()->register_callbacks();
        get_wizard_filament_sensor_select_step()->create(content);
        lv_obj_update_layout(content);
        // Schedule refresh in case sensors are discovered after screen creation
        // (handles race condition when jumping directly to step 8)
        lv_timer_create(
            [](lv_timer_t* timer) {
                get_wizard_filament_sensor_select_step()->refresh();
                lv_timer_delete(timer);
            },
            1500, nullptr); // Refresh after 1.5 seconds
        break;

    case 9: // Input Shaper Calibration
        spdlog::debug("[Wizard] Creating input shaper calibration screen");
        get_wizard_input_shaper_step()->init_subjects();
        get_wizard_input_shaper_step()->register_callbacks();
        get_wizard_input_shaper_step()->create(content);
        lv_obj_update_layout(content);
        break;

    case 10: // Summary
        spdlog::debug("[Wizard] Creating summary screen");
        get_wizard_summary_step()->init_subjects();
        get_wizard_summary_step()->register_callbacks();
        get_wizard_summary_step()->create(content);
        lv_obj_update_layout(content);
        break;

    default:
        spdlog::warn("[Wizard] Invalid step {}, ignoring", step);
        break;
    }

    // Update current screen step tracking
    current_screen_step = step;
}

// ============================================================================
// Wizard Completion
// ============================================================================

void ui_wizard_complete() {
    spdlog::info("[Wizard] Completing wizard and transitioning to main UI");

    // 1. Mark wizard as completed in config
    Config* config = Config::get_instance();
    if (config) {
        spdlog::debug("[Wizard] Setting wizard_completed flag");
        config->set<bool>("/wizard_completed", true);

        // 1b. Populate expected_hardware from wizard selections
        // This prevents "new hardware detected" warnings on subsequent runs
        std::vector<std::string> hardware_paths = {
            helix::wizard::BED_HEATER,    // "/printer/heaters/bed"
            helix::wizard::HOTEND_HEATER, // "/printer/heaters/hotend"
            helix::wizard::PART_FAN,      // "/printer/fans/part"
            helix::wizard::HOTEND_FAN,    // "/printer/fans/hotend"
            helix::wizard::LED_STRIP      // "/printer/leds/strip"
        };

        for (const auto& path : hardware_paths) {
            std::string hw_name = config->get<std::string>(path, "");
            if (!hw_name.empty() && hw_name != "None") {
                HardwareValidator::add_expected_hardware(config, hw_name);
                spdlog::debug("[Wizard] Added '{}' to expected_hardware", hw_name);
            }
        }

        // 1c. Add user-selected runout sensor to expected hardware
        {
            auto& sensor_mgr = helix::FilamentSensorManager::instance();
            auto sensors = sensor_mgr.get_sensors();
            for (const auto& sensor : sensors) {
                if (sensor.role == helix::FilamentSensorRole::RUNOUT &&
                    !sensor.klipper_name.empty()) {
                    HardwareValidator::add_expected_hardware(config, sensor.klipper_name);
                    spdlog::info("[Wizard] Added runout sensor '{}' to expected_hardware",
                                 sensor.klipper_name);
                    break;
                }
            }
        }

        // 1d. Add AMS to expected hardware if detected (step wasn't skipped)
        // This allows the hardware validator to warn if AMS disappears between sessions
        if (!ams_step_skipped) {
            auto& ams = AmsState::instance();
            AmsBackend* backend = ams.get_backend();
            if (backend) {
                AmsType type = backend->get_type();
                std::string ams_hw_name;
                switch (type) {
                case AmsType::AFC:
                    ams_hw_name = "AFC"; // Matches the Klipper object name (uppercase)
                    break;
                case AmsType::HAPPY_HARE:
                    ams_hw_name = "mmu"; // Matches the Klipper object name
                    break;
                case AmsType::TOOL_CHANGER:
                    ams_hw_name = "toolchanger"; // Marker for tool changer detection
                    break;
                case AmsType::VALGACE:
                    ams_hw_name = "valgace"; // ValgACE marker (REST-based, not a Klipper object)
                    break;
                default:
                    break;
                }
                if (!ams_hw_name.empty()) {
                    HardwareValidator::add_expected_hardware(config, ams_hw_name);
                    spdlog::info("[Wizard] Added '{}' to expected hardware", ams_hw_name);
                }
            }
        }

        if (!config->save()) {
            NOTIFY_ERROR("Failed to save setup completion");
        }
    } else {
        LOG_ERROR_INTERNAL("[Wizard] Failed to get config instance to mark wizard complete");
    }

    // 2. Cleanup current wizard screen
    ui_wizard_cleanup_current_screen();

    // 3. Delete wizard container (main UI is already created underneath)
    if (wizard_container) {
        spdlog::debug("[Wizard] Deleting wizard container");
        lv_obj_del(wizard_container);
        wizard_container = nullptr;
    }

    // 4. Clear global wizard state
    set_wizard_active(false);

    // 5. Schedule deferred runout check - modal may need to show after wizard
    lv_timer_create(
        [](lv_timer_t* timer) {
            auto& fsm = helix::FilamentSensorManager::instance();
            if (fsm.has_any_runout() && get_runtime_config()->should_show_runout_modal()) {
                spdlog::debug("[Wizard] Deferred runout check - triggering modal");
                get_global_home_panel().trigger_idle_runout_check();
            }
            lv_timer_delete(timer);
        },
        500, nullptr); // 500ms delay for UI to stabilize

    // 6. Connect to Moonraker using saved configuration
    if (!config) {
        config = Config::get_instance();
    }
    MoonrakerClient* client = get_moonraker_client();

    if (!config || !client) {
        spdlog::error("[Wizard] Failed to get config or moonraker client");
        return;
    }

    std::string moonraker_host = config->get<std::string>(helix::wizard::MOONRAKER_HOST, "");
    int moonraker_port = config->get<int>(helix::wizard::MOONRAKER_PORT, 7125);

    if (moonraker_host.empty()) {
        spdlog::warn("[Wizard] No Moonraker host configured, skipping connection");
        return;
    }

    // Build WebSocket URL
    std::string moonraker_url =
        "ws://" + moonraker_host + ":" + std::to_string(moonraker_port) + "/websocket";

    // Build HTTP base URL for file transfers (same host:port, http:// scheme)
    std::string http_base_url = "http://" + moonraker_host + ":" + std::to_string(moonraker_port);
    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        api->set_http_base_url(http_base_url);
    }

    // Check if already connected to the same URL
    ConnectionState current_state = client->get_connection_state();
    const std::string& current_url = client->get_last_url();

    if (current_state == ConnectionState::CONNECTED && current_url == moonraker_url) {
        // Already connected - but we still need to re-discover hardware
        // because user may have changed hardware mappings during wizard
        spdlog::info("[Wizard] Already connected to {} - triggering re-discovery", moonraker_url);
        client->discover_printer([]() {
            spdlog::info("✓ Printer re-discovery complete after wizard");
            // Reload home panel config after discovery completes
            get_global_home_panel().reload_from_config();
        });
    } else {
        // Connect to Moonraker
        spdlog::debug("[Wizard] Connecting to Moonraker at {}", moonraker_url);
        int connect_result = client->connect(
            moonraker_url.c_str(),
            []() {
                spdlog::info("✓ Connected to Moonraker");
                // Start auto-discovery (must be called AFTER connection is established)
                MoonrakerClient* client = get_moonraker_client();
                if (client) {
                    client->discover_printer([]() {
                        spdlog::info("✓ Printer auto-discovery complete");
                        // Reload home panel config after discovery completes
                        get_global_home_panel().reload_from_config();
                    });
                }
            },
            []() { spdlog::warn("✗ Disconnected from Moonraker"); });

        if (connect_result != 0) {
            spdlog::error("[Wizard] Failed to initiate Moonraker connection (code {})",
                          connect_result);
        }
    }

    // Tell Home Panel to reload immediately for printer image, type overlay
    // (LED and other hardware will update async when discovery completes)
    get_global_home_panel().reload_from_config();

    spdlog::info("[Wizard] Wizard complete, transitioned to main UI");
}

// ============================================================================
// Event Handlers
// ============================================================================

static void on_back_clicked(lv_event_t* e) {
    (void)e;
    int current = lv_subject_get_int(&current_step);

    // Minimum step depends on whether touch calibration was skipped
    int min_step = touch_cal_step_skipped ? 1 : 0;

    if (current > min_step) {
        int prev_step = current - 1;

        // Skip input shaper step (9) when going back if it was skipped
        if (prev_step == 9 && input_shaper_step_skipped) {
            prev_step = 8;
        }

        // Skip touch calibration step (0) when going back if it was skipped
        if (prev_step == 0 && touch_cal_step_skipped) {
            // Can't go back further - touch cal was skipped
            return;
        }

        // Skip filament sensor step (8) when going back if it was skipped
        if (prev_step == 8 && filament_step_skipped) {
            prev_step = 7;
        }

        // Skip LED step (7) when going back if it was skipped
        if (prev_step == 7 && led_step_skipped) {
            prev_step = 6;
        }

        // Skip AMS step (6) when going back if it was skipped
        if (prev_step == 6 && ams_step_skipped) {
            prev_step = 5;
        }

        ui_wizard_navigate_to_step(prev_step);
        spdlog::debug("[Wizard] Back button clicked, step: {}", prev_step);
    }
}

static void on_next_clicked(lv_event_t* e) {
    (void)e;
    int current = lv_subject_get_int(&current_step);

    // Summary (step 9) is always the last internal step
    if (current >= STEP_COMPONENT_COUNT) {
        spdlog::info("[Wizard] Finish button clicked, completing wizard");
        ui_wizard_complete();
        return;
    }

    // Commit touch calibration when leaving step 0 (only saves if user completed calibration)
    if (current == 0) {
        get_wizard_touch_calibration_step()->commit_calibration();
    }

    int next_step = current + 1;

    // Skip AMS step (6) if no AMS detected
    if (next_step == 6 && get_wizard_ams_identify_step()->should_skip()) {
        ams_step_skipped = true;
        next_step = 7;
        spdlog::debug("[Wizard] Skipping AMS step (no AMS detected)");
    }

    // Skip LED step (7) if no LEDs detected
    if (next_step == 7 && get_wizard_led_select_step()->should_skip()) {
        led_step_skipped = true;
        next_step = 8;
        spdlog::debug("[Wizard] Skipping LED step (no LEDs detected)");
    }

    // Ensure FilamentSensorManager is populated before skip check
    if (next_step == 8) {
        auto& fsm = helix::FilamentSensorManager::instance();
        if (fsm.get_sensors().empty()) {
            MoonrakerAPI* api = get_moonraker_api();
            if (api && api->hardware().has_filament_sensors()) {
                fsm.discover_sensors(api->hardware().filament_sensor_names());
                spdlog::debug("[Wizard] Populated FilamentSensorManager before skip check");
            }
        }
    }

    // Skip filament sensor step (8) if <2 standalone sensors
    if (next_step == 8 && get_wizard_filament_sensor_select_step()->should_skip()) {
        filament_step_skipped = true;

        // Auto-configure single sensor if exactly 1 detected
        auto* step = get_wizard_filament_sensor_select_step();
        if (step->get_standalone_sensor_count() == 1) {
            step->auto_configure_single_sensor();
            spdlog::info("[Wizard] Auto-configured single filament sensor as RUNOUT");
        }
        next_step = 9;
        spdlog::debug("[Wizard] Skipping filament sensor step (<2 sensors)");
    }

    // Skip input shaper step (9) if no accelerometer detected
    if (next_step == 9 && get_wizard_input_shaper_step()->should_skip()) {
        input_shaper_step_skipped = true;
        next_step = 10;
        spdlog::debug("[Wizard] Skipping input shaper step (no accelerometer)");
    }

    ui_wizard_navigate_to_step(next_step);
    spdlog::debug("[Wizard] Next button clicked, step: {}", next_step);
}
