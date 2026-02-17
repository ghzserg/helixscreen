// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_component_keypad.cpp
 * @brief Numeric keypad overlay with reactive Subject-Observer pattern
 *
 * Uses standard overlay navigation (NavigationManager push_overlay/go_back) and reactive
 * bindings for the display. The XML binds to the keypad_display subject,
 * so updating the subject automatically updates the UI.
 */

#include "ui_component_keypad.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Reactive State (Subject for XML binding)
// ============================================================================
static lv_subject_t keypad_display_subject;
static char keypad_display_buf[16] = "";
static bool subjects_initialized = false;
static SubjectManager subjects_;

// Widget reference (for showing/hiding via nav system)
static lv_obj_t* keypad_widget = nullptr;

// Current config and input state
static ui_keypad_config_t current_config;
static char input_buffer[16] = "";

// ============================================================================
// Forward declarations
// ============================================================================
static void update_display();
static void append_digit(int digit);
static void handle_backspace();
static void handle_confirm();
static void wire_button_events();

// ============================================================================
// Subject Initialization (call BEFORE XML creation)
// ============================================================================
void ui_keypad_init_subjects() {
    if (subjects_initialized) {
        return;
    }

    // Initialize display subject for reactive binding (starts empty)
    UI_MANAGED_SUBJECT_STRING(keypad_display_subject, keypad_display_buf, "", "keypad_display",
                              subjects_);

    subjects_initialized = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticPanelRegistry::instance().register_destroy("KeypadSubjects", ui_keypad_deinit_subjects);

    spdlog::debug("[Keypad] Subjects initialized");
}

void ui_keypad_deinit_subjects() {
    if (!subjects_initialized) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized = false;
    spdlog::debug("[Keypad] Subjects deinitialized");
}

// ============================================================================
// Widget Initialization (call AFTER XML creation)
// ============================================================================
void ui_keypad_init(lv_obj_t* parent) {
    if (!parent) {
        spdlog::error("[Keypad] Cannot init keypad: parent is null");
        return;
    }

    if (keypad_widget) {
        spdlog::warn("[Keypad] Already initialized");
        return;
    }

    // Ensure subjects are initialized first
    ui_keypad_init_subjects();

    // Create keypad from XML component
    keypad_widget = (lv_obj_t*)lv_xml_create(parent, "numeric_keypad_panel", nullptr);
    if (!keypad_widget) {
        spdlog::error("[Keypad] Failed to create keypad from XML");
        return;
    }

    // Wire button events
    wire_button_events();

    spdlog::debug("[Keypad] Numeric keypad initialized");
}

// ============================================================================
// Public API
// ============================================================================
void ui_keypad_show(const ui_keypad_config_t* config) {
    if (!keypad_widget || !config) {
        spdlog::error("[Keypad] Cannot show keypad: not initialized or invalid config");
        return;
    }

    // Store config
    current_config = *config;

    // Start with empty display (user enters fresh value)
    input_buffer[0] = '\0';

    // Update display via subject (reactive binding updates XML automatically)
    update_display();

    // Update unit label (set dynamically since XML prop is only evaluated at creation)
    lv_obj_t* unit_label = lv_obj_find_by_name(keypad_widget, "input_unit");
    if (unit_label) {
        lv_label_set_text(unit_label, config->unit_label ? config->unit_label : "");
    }

    // Show via overlay navigation, but keep previous panel visible (transparent overlay)
    NavigationManager::instance().push_overlay(keypad_widget, false /* hide_previous */);

    spdlog::info("[Keypad] Showing (initial={:.1f}, range={:.0f}-{:.0f})", config->initial_value,
                 config->min_value, config->max_value);
}

void ui_keypad_hide() {
    if (keypad_widget && ui_keypad_is_visible()) {
        NavigationManager::instance().go_back();
    }
}

bool ui_keypad_is_visible() {
    if (!keypad_widget)
        return false;
    return !lv_obj_has_flag(keypad_widget, LV_OBJ_FLAG_HIDDEN);
}

lv_subject_t* ui_keypad_get_display_subject() {
    return &keypad_display_subject;
}

// ============================================================================
// Input Logic
// ============================================================================
static void update_display() {
    lv_subject_copy_string(&keypad_display_subject, input_buffer);
}

static void append_digit(int digit) {
    size_t len = strlen(input_buffer);

    // Count digits (ignore decimal/minus)
    int digit_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (input_buffer[i] >= '0' && input_buffer[i] <= '9') {
            digit_count++;
        }
    }

    // Max 3 digits
    if (digit_count >= 3) {
        return;
    }

    // Append digit
    if (len < sizeof(input_buffer) - 1) {
        input_buffer[len] = '0' + digit;
        input_buffer[len + 1] = '\0';
        update_display();
    }
}

static void handle_backspace() {
    size_t len = strlen(input_buffer);
    if (len > 0) {
        input_buffer[len - 1] = '\0';
    }
    // Stay empty if all digits deleted (don't reset to "0")
    update_display();
}

static void handle_confirm() {
    // Parse value (empty = 0)
    float value = (input_buffer[0] == '\0') ? 0.0f : static_cast<float>(atof(input_buffer));

    // Validate range - show error if out of bounds
    if (value < current_config.min_value || value > current_config.max_value) {
        NOTIFY_ERROR("Value must be between {:.0f} and {:.0f}", current_config.min_value,
                     current_config.max_value);
        return; // Don't close keypad, let user correct the value
    }

    // Hide first (before callback, in case callback shows something else)
    ui_keypad_hide();

    // Invoke callback
    if (current_config.callback) {
        current_config.callback(value, current_config.user_data);
        spdlog::info("[Keypad] Confirmed value={:.1f}", value);
    }
}

// ============================================================================
// Event Wiring
// ============================================================================
static void wire_button_events() {
    if (!keypad_widget)
        return;

    // Number buttons 0-9
    const char* btn_names[] = {"btn_0", "btn_1", "btn_2", "btn_3", "btn_4",
                               "btn_5", "btn_6", "btn_7", "btn_8", "btn_9"};

    for (int i = 0; i < 10; i++) {
        lv_obj_t* btn = lv_obj_find_by_name(keypad_widget, btn_names[i]);
        if (btn) {
            lv_obj_add_event_cb(
                btn,
                [](lv_event_t* e) {
                    helix::ui::event_safe_call("keypad_digit", [e]() {
                        int digit = (int)(intptr_t)lv_event_get_user_data(e);
                        append_digit(digit);
                    });
                },
                LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
    }

    // Backspace button
    lv_obj_t* btn_back = lv_obj_find_by_name(keypad_widget, "btn_backspace");
    if (btn_back) {
        lv_obj_add_event_cb(
            btn_back,
            [](lv_event_t*) {
                helix::ui::event_safe_call("keypad_backspace", []() { handle_backspace(); });
            },
            LV_EVENT_CLICKED, nullptr);
    }

    // NOTE: Back button is handled by header_bar's default on_header_back_clicked callback
    // Do NOT add a second handler here - it would cause double navigation!

    // Action button (OK in header_bar) → confirm
    lv_obj_t* ok_btn = lv_obj_find_by_name(keypad_widget, "action_button");
    if (ok_btn) {
        lv_obj_add_event_cb(
            ok_btn,
            [](lv_event_t*) {
                helix::ui::event_safe_call("keypad_confirm", []() { handle_confirm(); });
            },
            LV_EVENT_CLICKED, nullptr);
    }

    spdlog::debug("[Keypad] Events wired");
}
