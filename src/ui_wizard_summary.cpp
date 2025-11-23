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

#include "ui_wizard_summary.h"

#include "ui_subject_registry.h"
#include "ui_wizard.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <sstream>
#include <string>

// ============================================================================
// Static Data & Subjects
// ============================================================================

// Subject declarations (module scope)
static lv_subject_t summary_printer_name;
static lv_subject_t summary_printer_type;
static lv_subject_t summary_wifi_ssid;
static lv_subject_t summary_moonraker_connection;
static lv_subject_t summary_bed;
static lv_subject_t summary_hotend;
static lv_subject_t summary_part_fan;
static lv_subject_t summary_hotend_fan;
static lv_subject_t summary_led_strip;

// Visibility control subjects (int: 1=visible, 0=hidden)
static lv_subject_t summary_part_fan_visible;
static lv_subject_t summary_hotend_fan_visible;
static lv_subject_t summary_led_strip_visible;

// String buffers (must be persistent)
static char printer_name_buffer[128];
static char printer_type_buffer[128];
static char wifi_ssid_buffer[128];
static char moonraker_connection_buffer[128];
static char bed_buffer[256];
static char hotend_buffer[256];
static char part_fan_buffer[128];
static char hotend_fan_buffer[128];
static char led_strip_buffer[128];

// Screen instance
static lv_obj_t* summary_screen_root = nullptr;

// ============================================================================
// Helper Functions
// ============================================================================

static std::string format_bed_summary(Config* config) {
    std::stringstream ss;

    std::string heater = config->get<std::string>(WizardConfigPaths::BED_HEATER, "None");
    std::string sensor = config->get<std::string>(WizardConfigPaths::BED_SENSOR, "None");

    ss << "Heater: " << (heater == "None" ? "None" : heater);
    ss << ", Sensor: " << (sensor == "None" ? "None" : sensor);

    return ss.str();
}

static std::string format_hotend_summary(Config* config) {
    std::stringstream ss;

    std::string heater = config->get<std::string>(WizardConfigPaths::HOTEND_HEATER, "None");
    std::string sensor = config->get<std::string>(WizardConfigPaths::HOTEND_SENSOR, "None");

    ss << "Heater: " << (heater == "None" ? "None" : heater);
    ss << ", Sensor: " << (sensor == "None" ? "None" : sensor);

    return ss.str();
}

// ============================================================================
// Subject Initialization
// ============================================================================

void ui_wizard_summary_init_subjects() {
    spdlog::debug("[Wizard Summary] Initializing subjects");

    // Load all values from config
    Config* config = Config::get_instance();

    // Printer name
    std::string printer_name =
        config ? config->get<std::string>(WizardConfigPaths::PRINTER_NAME, "Unnamed Printer")
               : "Unnamed Printer";
    spdlog::debug("[Wizard Summary] Printer name from config: '{}'", printer_name);
    strncpy(printer_name_buffer, printer_name.c_str(), sizeof(printer_name_buffer) - 1);
    printer_name_buffer[sizeof(printer_name_buffer) - 1] = '\0';

    // Printer type
    std::string printer_type =
        config ? config->get<std::string>(WizardConfigPaths::PRINTER_TYPE, "Unknown") : "Unknown";
    spdlog::debug("[Wizard Summary] Printer type from config: '{}'", printer_type);
    strncpy(printer_type_buffer, printer_type.c_str(), sizeof(printer_type_buffer) - 1);
    printer_type_buffer[sizeof(printer_type_buffer) - 1] = '\0';

    // WiFi SSID
    std::string wifi_ssid =
        config ? config->get<std::string>(WizardConfigPaths::WIFI_SSID, "Not configured")
               : "Not configured";
    spdlog::debug("[Wizard Summary] WiFi SSID from config: '{}'", wifi_ssid);
    strncpy(wifi_ssid_buffer, wifi_ssid.c_str(), sizeof(wifi_ssid_buffer) - 1);
    wifi_ssid_buffer[sizeof(wifi_ssid_buffer) - 1] = '\0';

    // Moonraker connection (host:port)
    std::string moonraker_host =
        config ? config->get<std::string>(WizardConfigPaths::MOONRAKER_HOST, "Not configured")
               : "Not configured";
    int moonraker_port = config ? config->get<int>(WizardConfigPaths::MOONRAKER_PORT, 7125) : 7125;
    spdlog::debug("[Wizard Summary] Moonraker host from config: '{}', port: {}", moonraker_host,
                  moonraker_port);
    std::stringstream moonraker_ss;
    if (moonraker_host != "Not configured") {
        moonraker_ss << moonraker_host << ":" << moonraker_port;
    } else {
        moonraker_ss << "Not configured";
    }
    strncpy(moonraker_connection_buffer, moonraker_ss.str().c_str(),
            sizeof(moonraker_connection_buffer) - 1);
    moonraker_connection_buffer[sizeof(moonraker_connection_buffer) - 1] = '\0';
    spdlog::debug("[Wizard Summary] Moonraker connection buffer: '{}'",
                  moonraker_connection_buffer);

    // Bed configuration
    std::string bed_summary = config ? format_bed_summary(config) : "Not configured";
    strncpy(bed_buffer, bed_summary.c_str(), sizeof(bed_buffer) - 1);
    bed_buffer[sizeof(bed_buffer) - 1] = '\0';

    // Hotend configuration
    std::string hotend_summary = config ? format_hotend_summary(config) : "Not configured";
    strncpy(hotend_buffer, hotend_summary.c_str(), sizeof(hotend_buffer) - 1);
    hotend_buffer[sizeof(hotend_buffer) - 1] = '\0';

    // Part cooling fan
    std::string part_fan =
        config ? config->get<std::string>(WizardConfigPaths::PART_FAN, "None") : "None";
    strncpy(part_fan_buffer, part_fan.c_str(), sizeof(part_fan_buffer) - 1);
    part_fan_buffer[sizeof(part_fan_buffer) - 1] = '\0';
    int part_fan_visible = (part_fan != "None") ? 1 : 0;

    // Hotend cooling fan
    std::string hotend_fan =
        config ? config->get<std::string>(WizardConfigPaths::HOTEND_FAN, "None") : "None";
    strncpy(hotend_fan_buffer, hotend_fan.c_str(), sizeof(hotend_fan_buffer) - 1);
    hotend_fan_buffer[sizeof(hotend_fan_buffer) - 1] = '\0';
    int hotend_fan_visible = (hotend_fan != "None") ? 1 : 0;

    // LED strip
    std::string led_strip =
        config ? config->get<std::string>(WizardConfigPaths::LED_STRIP, "None") : "None";
    strncpy(led_strip_buffer, led_strip.c_str(), sizeof(led_strip_buffer) - 1);
    led_strip_buffer[sizeof(led_strip_buffer) - 1] = '\0';
    int led_strip_visible = (led_strip != "None") ? 1 : 0;

    // Initialize and register all subjects
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_printer_name, printer_name_buffer, printer_name_buffer, "summary_printer_name");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_printer_type, printer_type_buffer, printer_type_buffer, "summary_printer_type");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_wifi_ssid, wifi_ssid_buffer, wifi_ssid_buffer, "summary_wifi_ssid");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_moonraker_connection, moonraker_connection_buffer, moonraker_connection_buffer, "summary_moonraker_connection");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_bed, bed_buffer, bed_buffer, "summary_bed");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_hotend, hotend_buffer, hotend_buffer, "summary_hotend");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_part_fan, part_fan_buffer, part_fan_buffer, "summary_part_fan");
    UI_SUBJECT_INIT_AND_REGISTER_INT(summary_part_fan_visible, part_fan_visible, "summary_part_fan_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_hotend_fan, hotend_fan_buffer, hotend_fan_buffer, "summary_hotend_fan");
    UI_SUBJECT_INIT_AND_REGISTER_INT(summary_hotend_fan_visible, hotend_fan_visible, "summary_hotend_fan_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(summary_led_strip, led_strip_buffer, led_strip_buffer, "summary_led_strip");
    UI_SUBJECT_INIT_AND_REGISTER_INT(summary_led_strip_visible, led_strip_visible, "summary_led_strip_visible");

    spdlog::info("[Wizard Summary] Subjects initialized with config values");
}

// ============================================================================
// Callback Registration
// ============================================================================

void ui_wizard_summary_register_callbacks() {
    spdlog::debug("[Wizard Summary] No callbacks to register (read-only screen)");
    // No interactive callbacks for summary screen
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* ui_wizard_summary_create(lv_obj_t* parent) {
    spdlog::info("[Wizard Summary] Creating summary screen");

    // Safety check: cleanup should have been called by wizard navigation
    if (summary_screen_root) {
        spdlog::warn(
            "[Wizard Summary] Screen pointer not null - cleanup may not have been called properly");
        summary_screen_root = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Refresh subjects with latest config values before creating UI
    ui_wizard_summary_init_subjects();

    // Create screen from XML
    summary_screen_root = (lv_obj_t*)lv_xml_create(parent, "wizard_summary", nullptr);
    if (!summary_screen_root) {
        spdlog::error("[Wizard Summary] Failed to create screen from XML");
        return nullptr;
    }

    spdlog::info("[Wizard Summary] Screen created successfully");
    return summary_screen_root;
}

// ============================================================================
// Cleanup
// ============================================================================

void ui_wizard_summary_cleanup() {
    spdlog::debug("[Wizard Summary] Cleaning up resources");

    // NOTE: Wizard framework handles object deletion - we only null the pointer
    // See HANDOFF.md Pattern #9: Wizard Screen Lifecycle
    summary_screen_root = nullptr;
}

// ============================================================================
// Validation
// ============================================================================

bool ui_wizard_summary_is_validated() {
    // Summary screen is always validated (no user input required)
    return true;
}