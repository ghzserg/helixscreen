// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_led_select.h"

#include "ui_wizard.h"
#include "ui_wizard_hardware_selector.h"
#include "ui_wizard_helpers.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "ui_error_reporting.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardLedSelectStep> g_wizard_led_select_step;

WizardLedSelectStep* get_wizard_led_select_step() {
    if (!g_wizard_led_select_step) {
        g_wizard_led_select_step = std::make_unique<WizardLedSelectStep>();
    }
    return g_wizard_led_select_step.get();
}

void destroy_wizard_led_select_step() {
    g_wizard_led_select_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardLedSelectStep::WizardLedSelectStep() {
    spdlog::debug("[{}] Instance created", get_name());
}

WizardLedSelectStep::~WizardLedSelectStep() {
    // NOTE: Do NOT call LVGL functions here - LVGL may be destroyed first
    // NOTE: Do NOT log here - spdlog may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Move Semantics
// ============================================================================

WizardLedSelectStep::WizardLedSelectStep(WizardLedSelectStep&& other) noexcept
    : screen_root_(other.screen_root_),
      led_strip_selected_(other.led_strip_selected_),
      led_strip_items_(std::move(other.led_strip_items_)),
      subjects_initialized_(other.subjects_initialized_) {
    other.screen_root_ = nullptr;
    other.subjects_initialized_ = false;
}

WizardLedSelectStep& WizardLedSelectStep::operator=(WizardLedSelectStep&& other) noexcept {
    if (this != &other) {
        screen_root_ = other.screen_root_;
        led_strip_selected_ = other.led_strip_selected_;
        led_strip_items_ = std::move(other.led_strip_items_);
        subjects_initialized_ = other.subjects_initialized_;

        other.screen_root_ = nullptr;
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardLedSelectStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize subject with default index 0
    // Actual selection will be restored from config during create() after hardware is discovered
    WizardHelpers::init_int_subject(&led_strip_selected_, 0, "led_strip_selected");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardLedSelectStep::register_callbacks() {
    // No XML callbacks needed - dropdowns attached programmatically in create()
    spdlog::debug("[{}] Callback registration (none needed for hardware selectors)", get_name());
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardLedSelectStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating LED select screen", get_name());

    // Safety check: cleanup should have been called by wizard navigation
    if (screen_root_) {
        spdlog::warn(
            "[{}] Screen pointer not null - cleanup may not have been called properly", get_name());
        screen_root_ = nullptr; // Reset pointer, wizard framework handles deletion
    }

    // Create screen from XML
    screen_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_led_select", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        return nullptr;
    }

    // Populate LED dropdown (discover + filter + populate + restore)
    wizard_populate_hardware_dropdown(
        screen_root_, "led_main_dropdown", &led_strip_selected_, led_strip_items_,
        [](MoonrakerClient* c) -> const auto& { return c->get_leds(); },
        nullptr, // No filter - include all LEDs
        true,    // Allow "None" option
        WizardConfigPaths::LED_STRIP,
        nullptr, // No guessing method for LED strips
        "[Wizard LED]");

    // Attach LED dropdown callback programmatically
    lv_obj_t* led_dropdown = lv_obj_find_by_name(screen_root_, "led_main_dropdown");
    if (led_dropdown) {
        lv_obj_add_event_cb(led_dropdown, wizard_hardware_dropdown_changed_cb,
                            LV_EVENT_VALUE_CHANGED, &led_strip_selected_);
    }

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardLedSelectStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    // Save current selection to config before cleanup (deferred save pattern)
    WizardHelpers::save_dropdown_selection(&led_strip_selected_, led_strip_items_,
                                           WizardConfigPaths::LED_STRIP, "[Wizard LED]");

    // Persist to disk
    Config* config = Config::get_instance();
    if (config) {
        if (!config->save()) {
            NOTIFY_ERROR("Failed to save LED configuration");
        }
    }

    // Reset UI references
    // Note: Do NOT call lv_obj_del() here - the wizard framework handles
    // object deletion when clearing wizard_content container
    screen_root_ = nullptr;

    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Validation
// ============================================================================

bool WizardLedSelectStep::is_validated() const {
    // Always return true for baseline implementation
    return true;
}
