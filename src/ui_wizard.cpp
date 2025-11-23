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

#include "ui_wizard.h"

#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_wizard_bed_select.h"
#include "ui_wizard_connection.h"
#include "ui_wizard_fan_select.h"
#include "ui_wizard_hotend_select.h"
#include "ui_wizard_led_select.h"
#include "ui_wizard_printer_identify.h"
#include "ui_wizard_summary.h"
#include "ui_wizard_wifi.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_client.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstdio>

// Subject declarations (static/global scope required)
static lv_subject_t current_step;
static lv_subject_t total_steps;
static lv_subject_t wizard_title;
static lv_subject_t wizard_progress;
static lv_subject_t wizard_next_button_text;

// Non-static: accessible from ui_wizard_connection.cpp
lv_subject_t connection_test_passed; // Global: 0=connection not validated, 1=validated or N/A

// String buffers (must be persistent)
static char wizard_title_buffer[64];
static char wizard_progress_buffer[32];
static char wizard_next_button_text_buffer[16];

// Wizard container instance
static lv_obj_t* wizard_container = nullptr;

// Track current screen for proper cleanup
static int current_screen_step = 0;

// Forward declarations
static void on_back_clicked(lv_event_t* e);
static void on_next_clicked(lv_event_t* e);
static void ui_wizard_load_screen(int step);
static void ui_wizard_cleanup_current_screen();

void ui_wizard_init_subjects() {
    spdlog::debug("[Wizard] Initializing subjects");

    // Initialize subjects with defaults
    UI_SUBJECT_INIT_AND_REGISTER_INT(current_step, 1, "current_step");
    UI_SUBJECT_INIT_AND_REGISTER_INT(total_steps, 8, "total_steps"); // 8 steps: WiFi, Connection, Printer, Bed, Hotend, Fan, LED, Summary

    UI_SUBJECT_INIT_AND_REGISTER_STRING(wizard_title, wizard_title_buffer, "Welcome", "wizard_title");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(wizard_progress, wizard_progress_buffer, "Step 1 of 8", "wizard_progress");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(wizard_next_button_text, wizard_next_button_text_buffer, "Next", "wizard_next_button_text");

    // Initialize connection_test_passed to 1 (enabled by default for all steps)
    // Step 2 (connection) will set it to 0 until test passes
    UI_SUBJECT_INIT_AND_REGISTER_INT(connection_test_passed, 1, "connection_test_passed");

    spdlog::info("[Wizard] Subjects initialized");
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

    // 1. Detect screen size using custom breakpoints
    lv_display_t* display = lv_display_get_default();
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    // 2. Determine responsive values based on breakpoint
    const char* header_height;
    const char* footer_height;
    const char* button_width;
    const char* header_font;
    const char* title_font;
    const char* wifi_card_height;
    const char* wifi_ethernet_height;
    const char* wifi_toggle_height;
    const char* network_icon_size;
    const char* size_label;

    if (greater_res <= UI_BREAKPOINT_SMALL_MAX) { // ≤480: 480x320
        header_height = "32";
        footer_height = "72"; // header + 40
        button_width = "110";
        header_font = "montserrat_14";
        title_font = "montserrat_16";
        wifi_card_height = "80";
        wifi_ethernet_height = "70";
        wifi_toggle_height = "32";
        network_icon_size = "20";
        size_label = "SMALL";
    } else if (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) { // 481-800: 800x480
        header_height = "42";
        footer_height = "82"; // header + 40
        button_width = "140";
        header_font = "montserrat_16";
        title_font = "montserrat_20";
        wifi_card_height = "120";
        wifi_ethernet_height = "100";
        wifi_toggle_height = "48";
        network_icon_size = "24";
        size_label = "MEDIUM";
    } else { // >800: 1024x600+
        header_height = "48";
        footer_height = "88"; // header + 40
        button_width = "160";
        header_font = "montserrat_20";
        title_font = lv_xml_get_const(NULL, "font_heading");
        wifi_card_height = "140";
        wifi_ethernet_height = "120";
        wifi_toggle_height = "64";
        network_icon_size = "32";
        size_label = "LARGE";
    }

    spdlog::info("[Wizard] Screen size: {} (greater_res={}px)", size_label, greater_res);

    // 3. Read padding/gap from globals (centralized responsive values)
    const char* padding_value = lv_xml_get_const(NULL, "padding_normal");
    const char* gap_value = lv_xml_get_const(NULL, "gap_normal");

    // 4. Define all wizard constants in array
    WizardConstant constants[] = {
        // Layout dimensions
        {"wizard_padding", padding_value},
        {"wizard_gap", gap_value},
        {"wizard_header_height", header_height},
        {"wizard_footer_height", footer_height},
        {"wizard_button_width", button_width},
        // Typography
        {"wizard_header_font", header_font},
        {"wizard_title_font", title_font},
        // WiFi screen specific
        {"wifi_toggle_height", wifi_toggle_height},
        {"wifi_card_height", wifi_card_height},
        {"wifi_ethernet_height", wifi_ethernet_height},
        {"network_icon_size", network_icon_size},
        {NULL, NULL} // Sentinel
    };

    // 5. Register to wizard_container scope (parent)
    lv_xml_component_scope_t* parent_scope = lv_xml_component_get_scope("wizard_container");
    register_constants_to_scope(parent_scope, constants);

    // 6. Define child components that inherit these constants
    // Note: WiFi network list constants (list_item_padding, list_item_height, list_item_font)
    //       are registered separately by ui_wizard_wifi_register_responsive_constants()
    const char* children[] = {
        "wizard_wifi_setup",
        "wizard_connection",
        "wizard_printer_identify",
        "wizard_bed_select",
        "wizard_hotend_select",
        "wizard_fan_select",
        "wizard_led_select",
        "wizard_summary",
        NULL // Sentinel
    };

    // 7. Propagate to all children
    int child_count = 0;
    for (int i = 0; children[i] != NULL; i++) {
        lv_xml_component_scope_t* child_scope = lv_xml_component_get_scope(children[i]);
        if (child_scope) {
            register_constants_to_scope(child_scope, constants);
            child_count++;
        }
    }

    spdlog::info("[Wizard] Registered 11 constants to wizard_container and propagated to {} child "
                 "components",
                 child_count);
    spdlog::debug("[Wizard] Values: padding={}, gap={}, header_h={}, footer_h={}, button_w={}",
                  padding_value, gap_value, header_height, footer_height, button_width);
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

    spdlog::info("[Wizard] Wizard container created successfully");
    return wizard_container;
}

void ui_wizard_navigate_to_step(int step) {
    spdlog::debug("[Wizard] Navigating to step {}", step);

    // Clamp step to valid range
    int total = lv_subject_get_int(&total_steps);
    if (step < 1)
        step = 1;
    if (step > total)
        step = total;

    // Update current_step subject
    lv_subject_set_int(&current_step, step);

    // Update next button text based on step
    if (step == total) {
        lv_subject_copy_string(&wizard_next_button_text, "Finish");
    } else {
        lv_subject_copy_string(&wizard_next_button_text, "Next");
    }

    // Update progress text
    char progress_buf[32];
    snprintf(progress_buf, sizeof(progress_buf), "Step %d of %d", step, total);
    lv_subject_copy_string(&wizard_progress, progress_buf);

    // Load screen content
    ui_wizard_load_screen(step);

    // Force layout update on entire wizard after screen is loaded
    if (wizard_container) {
        lv_obj_update_layout(wizard_container);
    }

    spdlog::debug("[Wizard] Updated to step {}/{}, button: {}", step, total,
                  (step == total) ? "Finish" : "Next");
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
    if (current_screen_step == 0) {
        return; // No screen loaded yet
    }

    spdlog::debug("[Wizard] Cleaning up screen for step {}", current_screen_step);

    switch (current_screen_step) {
    case 1: // WiFi Setup
        ui_wizard_wifi_cleanup();
        break;
    case 2: // Moonraker Connection
        ui_wizard_connection_cleanup();
        break;
    case 3: // Printer Identification
        ui_wizard_printer_identify_cleanup();
        break;
    case 4: // Bed Select
        ui_wizard_bed_select_cleanup();
        break;
    case 5: // Hotend Select
        ui_wizard_hotend_select_cleanup();
        break;
    case 6: // Fan Select
        ui_wizard_fan_select_cleanup();
        break;
    case 7: // LED Select
        ui_wizard_led_select_cleanup();
        break;
    case 8: // Summary
        ui_wizard_summary_cleanup();
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

    // Create appropriate screen based on step
    switch (step) {
    case 1: // WiFi Setup
        spdlog::info("[Wizard] Creating WiFi setup screen");
        ui_wizard_wifi_init_subjects();
        ui_wizard_wifi_register_callbacks();
        // Note: WiFi constants now registered by
        // ui_wizard_container_register_responsive_constants()
        ui_wizard_wifi_create(content);
        lv_obj_update_layout(content);
        ui_wizard_wifi_init_wifi_manager();
        ui_wizard_set_title("WiFi Setup");
        break;

    case 2: // Moonraker Connection
        spdlog::info("[Wizard] Creating Moonraker connection screen");
        ui_wizard_connection_init_subjects();
        ui_wizard_connection_register_callbacks();
        ui_wizard_connection_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("Moonraker Connection");
        break;

    case 3: // Printer Identification
        spdlog::info("[Wizard] Creating printer identification screen");
        ui_wizard_printer_identify_init_subjects();
        ui_wizard_printer_identify_register_callbacks();
        ui_wizard_printer_identify_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("Printer Identification");
        break;

    case 4: // Bed Select
        spdlog::info("[Wizard] Creating bed select screen");
        ui_wizard_bed_select_init_subjects();
        ui_wizard_bed_select_register_callbacks();
        ui_wizard_bed_select_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("Bed Configuration");
        break;

    case 5: // Hotend Select
        spdlog::info("[Wizard] Creating hotend select screen");
        ui_wizard_hotend_select_init_subjects();
        ui_wizard_hotend_select_register_callbacks();
        ui_wizard_hotend_select_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("Hotend Configuration");
        break;

    case 6: // Fan Select
        spdlog::info("[Wizard] Creating fan select screen");
        ui_wizard_fan_select_init_subjects();
        ui_wizard_fan_select_register_callbacks();
        ui_wizard_fan_select_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("Fan Configuration");
        break;

    case 7: // LED Select
        spdlog::info("[Wizard] Creating LED select screen");
        ui_wizard_led_select_init_subjects();
        ui_wizard_led_select_register_callbacks();
        ui_wizard_led_select_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("LED Configuration");
        break;

    case 8: // Summary
        spdlog::info("[Wizard] Creating summary screen");
        ui_wizard_summary_init_subjects();
        ui_wizard_summary_register_callbacks();
        ui_wizard_summary_create(content);
        lv_obj_update_layout(content);
        ui_wizard_set_title("Configuration Summary");
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
        spdlog::info("[Wizard] Setting wizard_completed flag");
        config->set<bool>("/wizard_completed", true);
        if (!config->save()) {
            spdlog::error("[Wizard] Failed to save wizard_completed flag!");
        }
    } else {
        spdlog::error("[Wizard] Failed to get config instance to mark wizard complete");
    }

    // 2. Cleanup current wizard screen
    ui_wizard_cleanup_current_screen();

    // 3. Delete wizard container (main UI is already created underneath)
    if (wizard_container) {
        spdlog::debug("[Wizard] Deleting wizard container");
        lv_obj_del(wizard_container);
        wizard_container = nullptr;
    }

    // 4. Connect to Moonraker using saved configuration
    if (!config) {
        config = Config::get_instance();
    }
    MoonrakerClient* client = get_moonraker_client();

    if (!config || !client) {
        spdlog::error("[Wizard] Failed to get config or moonraker client");
        return;
    }

    std::string moonraker_host = config->get<std::string>(WizardConfigPaths::MOONRAKER_HOST, "");
    int moonraker_port = config->get<int>(WizardConfigPaths::MOONRAKER_PORT, 7125);

    if (moonraker_host.empty()) {
        spdlog::warn("[Wizard] No Moonraker host configured, skipping connection");
        return;
    }

    // Build WebSocket URL
    std::string moonraker_url =
        "ws://" + moonraker_host + ":" + std::to_string(moonraker_port) + "/websocket";

    // Connect to Moonraker
    spdlog::info("[Wizard] Connecting to Moonraker at {}", moonraker_url);
    int connect_result = client->connect(
        moonraker_url.c_str(),
        []() {
            spdlog::info("✓ Connected to Moonraker");
            // Start auto-discovery (must be called AFTER connection is established)
            MoonrakerClient* client = get_moonraker_client();
            if (client) {
                client->discover_printer(
                    []() { spdlog::info("✓ Printer auto-discovery complete"); });
            }
        },
        []() { spdlog::warn("✗ Disconnected from Moonraker"); });

    if (connect_result != 0) {
        spdlog::error("[Wizard] Failed to initiate Moonraker connection (code {})", connect_result);
    }

    spdlog::info("[Wizard] Wizard complete, transitioned to main UI");
}

// ============================================================================
// Event Handlers
// ============================================================================

static void on_back_clicked(lv_event_t* e) {
    (void)e;
    int current = lv_subject_get_int(&current_step);
    if (current > 1) {
        ui_wizard_navigate_to_step(current - 1);
    }
    spdlog::debug("[Wizard] Back button clicked, step: {}", current - 1);
}

static void on_next_clicked(lv_event_t* e) {
    (void)e;
    int current = lv_subject_get_int(&current_step);
    int total = lv_subject_get_int(&total_steps);

    if (current < total) {
        ui_wizard_navigate_to_step(current + 1);
        spdlog::debug("[Wizard] Next button clicked, step: {}", current + 1);
    } else {
        spdlog::info("[Wizard] Finish button clicked, completing wizard");
        ui_wizard_complete();
    }
}
