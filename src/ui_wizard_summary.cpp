// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_summary.h"

#include "ui_subject_registry.h"
#include "ui_wizard.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <sstream>
#include <string>

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardSummaryStep> g_wizard_summary_step;

WizardSummaryStep* get_wizard_summary_step() {
    if (!g_wizard_summary_step) {
        g_wizard_summary_step = std::make_unique<WizardSummaryStep>();
    }
    return g_wizard_summary_step.get();
}

void destroy_wizard_summary_step() {
    g_wizard_summary_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardSummaryStep::WizardSummaryStep() {
    // Zero-initialize buffers
    std::memset(printer_name_buffer_, 0, sizeof(printer_name_buffer_));
    std::memset(printer_type_buffer_, 0, sizeof(printer_type_buffer_));
    std::memset(wifi_ssid_buffer_, 0, sizeof(wifi_ssid_buffer_));
    std::memset(moonraker_connection_buffer_, 0, sizeof(moonraker_connection_buffer_));
    std::memset(bed_buffer_, 0, sizeof(bed_buffer_));
    std::memset(hotend_buffer_, 0, sizeof(hotend_buffer_));
    std::memset(part_fan_buffer_, 0, sizeof(part_fan_buffer_));
    std::memset(hotend_fan_buffer_, 0, sizeof(hotend_fan_buffer_));
    std::memset(led_strip_buffer_, 0, sizeof(led_strip_buffer_));

    spdlog::debug("[{}] Instance created", get_name());
}

WizardSummaryStep::~WizardSummaryStep() {
    // NOTE: Do NOT call LVGL functions here - LVGL may be destroyed first
    // NOTE: Do NOT log here - spdlog may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Move Semantics
// ============================================================================

WizardSummaryStep::WizardSummaryStep(WizardSummaryStep&& other) noexcept
    : screen_root_(other.screen_root_),
      printer_name_(other.printer_name_),
      printer_type_(other.printer_type_),
      wifi_ssid_(other.wifi_ssid_),
      moonraker_connection_(other.moonraker_connection_),
      bed_(other.bed_),
      hotend_(other.hotend_),
      part_fan_(other.part_fan_),
      part_fan_visible_(other.part_fan_visible_),
      hotend_fan_(other.hotend_fan_),
      hotend_fan_visible_(other.hotend_fan_visible_),
      led_strip_(other.led_strip_),
      led_strip_visible_(other.led_strip_visible_),
      subjects_initialized_(other.subjects_initialized_) {
    // Move buffers
    std::memcpy(printer_name_buffer_, other.printer_name_buffer_, sizeof(printer_name_buffer_));
    std::memcpy(printer_type_buffer_, other.printer_type_buffer_, sizeof(printer_type_buffer_));
    std::memcpy(wifi_ssid_buffer_, other.wifi_ssid_buffer_, sizeof(wifi_ssid_buffer_));
    std::memcpy(moonraker_connection_buffer_, other.moonraker_connection_buffer_, sizeof(moonraker_connection_buffer_));
    std::memcpy(bed_buffer_, other.bed_buffer_, sizeof(bed_buffer_));
    std::memcpy(hotend_buffer_, other.hotend_buffer_, sizeof(hotend_buffer_));
    std::memcpy(part_fan_buffer_, other.part_fan_buffer_, sizeof(part_fan_buffer_));
    std::memcpy(hotend_fan_buffer_, other.hotend_fan_buffer_, sizeof(hotend_fan_buffer_));
    std::memcpy(led_strip_buffer_, other.led_strip_buffer_, sizeof(led_strip_buffer_));

    // Null out other
    other.screen_root_ = nullptr;
    other.subjects_initialized_ = false;
}

WizardSummaryStep& WizardSummaryStep::operator=(WizardSummaryStep&& other) noexcept {
    if (this != &other) {
        screen_root_ = other.screen_root_;
        printer_name_ = other.printer_name_;
        printer_type_ = other.printer_type_;
        wifi_ssid_ = other.wifi_ssid_;
        moonraker_connection_ = other.moonraker_connection_;
        bed_ = other.bed_;
        hotend_ = other.hotend_;
        part_fan_ = other.part_fan_;
        part_fan_visible_ = other.part_fan_visible_;
        hotend_fan_ = other.hotend_fan_;
        hotend_fan_visible_ = other.hotend_fan_visible_;
        led_strip_ = other.led_strip_;
        led_strip_visible_ = other.led_strip_visible_;
        subjects_initialized_ = other.subjects_initialized_;

        // Move buffers
        std::memcpy(printer_name_buffer_, other.printer_name_buffer_, sizeof(printer_name_buffer_));
        std::memcpy(printer_type_buffer_, other.printer_type_buffer_, sizeof(printer_type_buffer_));
        std::memcpy(wifi_ssid_buffer_, other.wifi_ssid_buffer_, sizeof(wifi_ssid_buffer_));
        std::memcpy(moonraker_connection_buffer_, other.moonraker_connection_buffer_, sizeof(moonraker_connection_buffer_));
        std::memcpy(bed_buffer_, other.bed_buffer_, sizeof(bed_buffer_));
        std::memcpy(hotend_buffer_, other.hotend_buffer_, sizeof(hotend_buffer_));
        std::memcpy(part_fan_buffer_, other.part_fan_buffer_, sizeof(part_fan_buffer_));
        std::memcpy(hotend_fan_buffer_, other.hotend_fan_buffer_, sizeof(hotend_fan_buffer_));
        std::memcpy(led_strip_buffer_, other.led_strip_buffer_, sizeof(led_strip_buffer_));

        // Null out other
        other.screen_root_ = nullptr;
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Helper Functions
// ============================================================================

std::string WizardSummaryStep::format_bed_summary() {
    Config* config = Config::get_instance();
    std::stringstream ss;

    std::string heater = config->get<std::string>(WizardConfigPaths::BED_HEATER, "None");
    std::string sensor = config->get<std::string>(WizardConfigPaths::BED_SENSOR, "None");

    ss << "Heater: " << (heater == "None" ? "None" : heater);
    ss << ", Sensor: " << (sensor == "None" ? "None" : sensor);

    return ss.str();
}

std::string WizardSummaryStep::format_hotend_summary() {
    Config* config = Config::get_instance();
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

void WizardSummaryStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Load all values from config
    Config* config = Config::get_instance();

    // Printer name
    std::string printer_name =
        config ? config->get<std::string>(WizardConfigPaths::PRINTER_NAME, "Unnamed Printer")
               : "Unnamed Printer";
    spdlog::debug("[{}] Printer name from config: '{}'", get_name(), printer_name);
    strncpy(printer_name_buffer_, printer_name.c_str(), sizeof(printer_name_buffer_) - 1);
    printer_name_buffer_[sizeof(printer_name_buffer_) - 1] = '\0';

    // Printer type
    std::string printer_type =
        config ? config->get<std::string>(WizardConfigPaths::PRINTER_TYPE, "Unknown") : "Unknown";
    spdlog::debug("[{}] Printer type from config: '{}'", get_name(), printer_type);
    strncpy(printer_type_buffer_, printer_type.c_str(), sizeof(printer_type_buffer_) - 1);
    printer_type_buffer_[sizeof(printer_type_buffer_) - 1] = '\0';

    // WiFi SSID
    std::string wifi_ssid =
        config ? config->get<std::string>(WizardConfigPaths::WIFI_SSID, "Not configured")
               : "Not configured";
    spdlog::debug("[{}] WiFi SSID from config: '{}'", get_name(), wifi_ssid);
    strncpy(wifi_ssid_buffer_, wifi_ssid.c_str(), sizeof(wifi_ssid_buffer_) - 1);
    wifi_ssid_buffer_[sizeof(wifi_ssid_buffer_) - 1] = '\0';

    // Moonraker connection (host:port)
    std::string moonraker_host =
        config ? config->get<std::string>(WizardConfigPaths::MOONRAKER_HOST, "Not configured")
               : "Not configured";
    int moonraker_port = config ? config->get<int>(WizardConfigPaths::MOONRAKER_PORT, 7125) : 7125;
    spdlog::debug("[{}] Moonraker host from config: '{}', port: {}", get_name(), moonraker_host,
                  moonraker_port);
    std::stringstream moonraker_ss;
    if (moonraker_host != "Not configured") {
        moonraker_ss << moonraker_host << ":" << moonraker_port;
    } else {
        moonraker_ss << "Not configured";
    }
    strncpy(moonraker_connection_buffer_, moonraker_ss.str().c_str(),
            sizeof(moonraker_connection_buffer_) - 1);
    moonraker_connection_buffer_[sizeof(moonraker_connection_buffer_) - 1] = '\0';
    spdlog::debug("[{}] Moonraker connection buffer: '{}'", get_name(),
                  moonraker_connection_buffer_);

    // Bed configuration
    std::string bed_summary = config ? format_bed_summary() : "Not configured";
    strncpy(bed_buffer_, bed_summary.c_str(), sizeof(bed_buffer_) - 1);
    bed_buffer_[sizeof(bed_buffer_) - 1] = '\0';

    // Hotend configuration
    std::string hotend_summary = config ? format_hotend_summary() : "Not configured";
    strncpy(hotend_buffer_, hotend_summary.c_str(), sizeof(hotend_buffer_) - 1);
    hotend_buffer_[sizeof(hotend_buffer_) - 1] = '\0';

    // Part cooling fan
    std::string part_fan =
        config ? config->get<std::string>(WizardConfigPaths::PART_FAN, "None") : "None";
    strncpy(part_fan_buffer_, part_fan.c_str(), sizeof(part_fan_buffer_) - 1);
    part_fan_buffer_[sizeof(part_fan_buffer_) - 1] = '\0';
    int part_fan_visible = (part_fan != "None") ? 1 : 0;

    // Hotend cooling fan
    std::string hotend_fan =
        config ? config->get<std::string>(WizardConfigPaths::HOTEND_FAN, "None") : "None";
    strncpy(hotend_fan_buffer_, hotend_fan.c_str(), sizeof(hotend_fan_buffer_) - 1);
    hotend_fan_buffer_[sizeof(hotend_fan_buffer_) - 1] = '\0';
    int hotend_fan_visible = (hotend_fan != "None") ? 1 : 0;

    // LED strip
    std::string led_strip =
        config ? config->get<std::string>(WizardConfigPaths::LED_STRIP, "None") : "None";
    strncpy(led_strip_buffer_, led_strip.c_str(), sizeof(led_strip_buffer_) - 1);
    led_strip_buffer_[sizeof(led_strip_buffer_) - 1] = '\0';
    int led_strip_visible = (led_strip != "None") ? 1 : 0;

    // Initialize and register all subjects
    UI_SUBJECT_INIT_AND_REGISTER_STRING(printer_name_, printer_name_buffer_, printer_name_buffer_, "summary_printer_name");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(printer_type_, printer_type_buffer_, printer_type_buffer_, "summary_printer_type");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(wifi_ssid_, wifi_ssid_buffer_, wifi_ssid_buffer_, "summary_wifi_ssid");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(moonraker_connection_, moonraker_connection_buffer_, moonraker_connection_buffer_, "summary_moonraker_connection");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_, bed_buffer_, bed_buffer_, "summary_bed");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(hotend_, hotend_buffer_, hotend_buffer_, "summary_hotend");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(part_fan_, part_fan_buffer_, part_fan_buffer_, "summary_part_fan");
    UI_SUBJECT_INIT_AND_REGISTER_INT(part_fan_visible_, part_fan_visible, "summary_part_fan_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(hotend_fan_, hotend_fan_buffer_, hotend_fan_buffer_, "summary_hotend_fan");
    UI_SUBJECT_INIT_AND_REGISTER_INT(hotend_fan_visible_, hotend_fan_visible, "summary_hotend_fan_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(led_strip_, led_strip_buffer_, led_strip_buffer_, "summary_led_strip");
    UI_SUBJECT_INIT_AND_REGISTER_INT(led_strip_visible_, led_strip_visible, "summary_led_strip_visible");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized with config values", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardSummaryStep::register_callbacks() {
    spdlog::debug("[{}] No callbacks to register (read-only screen)", get_name());
    // No interactive callbacks for summary screen
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardSummaryStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating summary screen", get_name());

    // Safety check: cleanup should have been called by wizard navigation
    if (screen_root_) {
        spdlog::warn(
            "[{}] Screen pointer not null - cleanup may not have been called properly", get_name());
        screen_root_ = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Refresh subjects with latest config values before creating UI
    init_subjects();

    // Create screen from XML
    screen_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_summary", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        return nullptr;
    }

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardSummaryStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    // NOTE: Wizard framework handles object deletion - we only null the pointer
    // See HANDOFF.md Pattern #9: Wizard Screen Lifecycle
    screen_root_ = nullptr;
}

// ============================================================================
// Validation
// ============================================================================

bool WizardSummaryStep::is_validated() const {
    // Summary screen is always validated (no user input required)
    return true;
}
