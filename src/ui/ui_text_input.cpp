// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_text_input.cpp
 * @brief Custom <text_input> XML widget with reactive data binding.
 *
 * Provides a semantic wrapper around lv_textarea that adds bind_text support,
 * similar to how lv_label has lv_label_bind_text(). LVGL's native textarea
 * doesn't support XML binding, so we implement it here using the observer pattern.
 *
 * Also supports keyboard_hint attribute to specify initial keyboard mode.
 */

#include "ui_text_input.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/core/lv_observer_private.h" // For lv_observer_t internals
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_textarea_parser.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>

/**
 * Magic value to identify text_input widgets and store keyboard hint.
 * The user_data is structured as: (MAGIC | hint_value)
 * This allows us to both identify text_input widgets and store their hint.
 */
static constexpr uintptr_t TEXT_INPUT_MAGIC = 0xBADC0DE0;
static constexpr uintptr_t TEXT_INPUT_MAGIC_MASK = 0xFFFFFFF0;
static constexpr uintptr_t TEXT_INPUT_HINT_MASK = 0x0000000F;

/**
 * Observer callback - updates textarea when subject changes.
 *
 * This is invoked automatically by LVGL's observer system whenever
 * lv_subject_copy_string() or lv_subject_set_pointer() is called.
 */
static void textarea_text_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    if (subject->type == LV_SUBJECT_TYPE_STRING || subject->type == LV_SUBJECT_TYPE_POINTER) {
        lv_textarea_set_text(static_cast<lv_obj_t*>(observer->target),
                             static_cast<const char*>(subject->value.pointer));
    }
}

/**
 * Thread-local flag to prevent reentrancy during two-way binding updates.
 * This is needed because lv_textarea_set_text fires VALUE_CHANGED, which
 * would call us again before we've finished updating the subject.
 */
static thread_local bool g_updating_from_textarea = false;

/**
 * Event callback - updates subject when textarea text changes.
 *
 * This provides the reverse binding: when user types in the textarea,
 * we update the bound subject so other code sees the new value.
 * The subject pointer is passed via event user_data.
 */
static void textarea_value_changed_cb(lv_event_t* e) {
    // Prevent reentrancy - if we're already handling an update, skip
    if (g_updating_from_textarea) {
        return;
    }

    lv_obj_t* textarea = lv_event_get_target_obj(e);
    lv_subject_t* subject = static_cast<lv_subject_t*>(lv_event_get_user_data(e));

    if (subject == nullptr) {
        return;
    }

    // Validate subject type before using it
    // This prevents crashes if user_data is not actually a subject pointer
    if (subject->type != LV_SUBJECT_TYPE_STRING && subject->type != LV_SUBJECT_TYPE_POINTER) {
        return;
    }

    // Get current text from textarea
    const char* new_text = lv_textarea_get_text(textarea);
    if (new_text == nullptr) {
        return;
    }

    // Get current subject value
    const char* subject_text = lv_subject_get_string(subject);

    // Only update if text actually changed
    if (subject_text == nullptr || std::strcmp(new_text, subject_text) != 0) {
        g_updating_from_textarea = true;
        lv_subject_copy_string(subject, new_text);
        g_updating_from_textarea = false;
    }
}

/**
 * XML create callback for <text_input>.
 *
 * Creates a textarea with sensible defaults for form inputs:
 * - Responsive vertical padding from theme
 * - One-line mode enabled
 * - Default keyboard hint (TEXT)
 */
static void* ui_text_input_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));
    lv_obj_t* textarea = lv_textarea_create(parent);

    // Apply responsive padding for consistent height across screen sizes
    const char* pad_ver = lv_xml_get_const(nullptr, "space_md");
    if (pad_ver) {
        int32_t padding = std::atoi(pad_ver);
        lv_obj_set_style_pad_ver(textarea, padding, 0);
    }

    // Note: Border and background styling is handled by theme_core's apply_cb
    // which adds input_bg_style (elevated_bg color) to all textareas.
    // We don't set inline styles here as they would override the theme.

    // One-line mode by default for form inputs
    lv_textarea_set_one_line(textarea, true);

    // Set default keyboard hint (TEXT) via user_data magic value
    lv_obj_set_user_data(
        textarea,
        reinterpret_cast<void*>(TEXT_INPUT_MAGIC | static_cast<uintptr_t>(KeyboardHint::TEXT)));

    return textarea;
}

/**
 * XML apply callback for <text_input>.
 *
 * First applies standard textarea properties (via lv_xml_textarea_apply),
 * then handles our custom attributes:
 * - bind_text: reactive data binding
 * - keyboard_hint: initial keyboard mode hint
 */
static void ui_text_input_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* textarea = static_cast<lv_obj_t*>(lv_xml_state_get_item(state));

    // Pre-scan for multiline BEFORE standard apply, so height/size attrs are applied
    // after one_line mode is already correct (prevents auto-sizing from overriding height)
    for (int i = 0; attrs[i]; i += 2) {
        if (lv_streq("multiline", attrs[i]) && lv_streq("true", attrs[i + 1])) {
            lv_textarea_set_one_line(textarea, false);
            break;
        }
    }

    // Apply standard textarea properties (handles height, width, etc.)
    lv_xml_textarea_apply(state, attrs);

    // Then handle our custom attributes
    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (lv_streq("placeholder", name)) {
            // Shorthand for placeholder_text (more intuitive attribute name)
            lv_textarea_set_placeholder_text(textarea, value);
        } else if (lv_streq("max_length", name)) {
            // Set maximum character length
            lv_textarea_set_max_length(textarea, lv_xml_atoi(value));
        } else if (lv_streq("bind_text", name)) {
            lv_subject_t* subject = lv_xml_get_subject(&state->scope, value);
            if (subject == nullptr) {
                spdlog::warn("[text_input] Subject '{}' not found for bind_text", value);
                continue;
            }

            // Verify subject type
            if (subject->type != LV_SUBJECT_TYPE_STRING &&
                subject->type != LV_SUBJECT_TYPE_POINTER) {
                spdlog::warn("[text_input] Subject '{}' has incompatible type {}", value,
                             static_cast<int>(subject->type));
                continue;
            }

            // Create observer to update textarea when subject changes (subject -> textarea)
            lv_subject_add_observer_obj(subject, textarea_text_observer_cb, textarea, nullptr);

            // Add event handler to update subject when user types (textarea -> subject)
            // Pass subject pointer via user_data
            lv_obj_add_event_cb(textarea, textarea_value_changed_cb, LV_EVENT_VALUE_CHANGED,
                                subject);

            spdlog::trace("[text_input] Bound subject '{}' to textarea (two-way)", value);
        } else if (lv_streq("multiline", name)) {
            // Handled in pre-scan above (before standard apply)
        } else if (lv_streq("keyboard_hint", name)) {
            // Parse keyboard hint and store in user_data
            KeyboardHint hint = KeyboardHint::TEXT;

            if (std::strcmp(value, "numeric") == 0) {
                hint = KeyboardHint::NUMERIC;
                spdlog::trace("[text_input] Set keyboard_hint to NUMERIC");
            } else if (std::strcmp(value, "text") == 0) {
                hint = KeyboardHint::TEXT;
                spdlog::trace("[text_input] Set keyboard_hint to TEXT");
            } else {
                spdlog::warn("[text_input] Unknown keyboard_hint '{}', using TEXT", value);
            }

            // Update user_data with the hint (preserving magic value)
            lv_obj_set_user_data(
                textarea, reinterpret_cast<void*>(TEXT_INPUT_MAGIC | static_cast<uintptr_t>(hint)));
        }
    }
}

void ui_text_input_init() {
    lv_xml_register_widget("text_input", ui_text_input_create, ui_text_input_apply);
    spdlog::debug("[ui_text_input] Registered <text_input> widget");
}

KeyboardHint ui_text_input_get_keyboard_hint(lv_obj_t* textarea) {
    if (textarea == nullptr) {
        return KeyboardHint::TEXT;
    }

    // Get user_data and check magic value
    auto user_data = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(textarea));

    // Verify this is a text_input widget by checking magic
    if ((user_data & TEXT_INPUT_MAGIC_MASK) != TEXT_INPUT_MAGIC) {
        // Not a text_input widget, return default
        return KeyboardHint::TEXT;
    }

    // Extract hint from lower bits
    return static_cast<KeyboardHint>(user_data & TEXT_INPUT_HINT_MASK);
}
