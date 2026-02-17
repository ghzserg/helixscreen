// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_color_picker.h"

#include "ui_hsv_picker.h"
#include "ui_modal.h"

#include "color_utils.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

namespace helix {

// Special preset names that don't follow standard color naming
static const struct {
    uint32_t hex;
    const char* name;
} kSpecialColorNames[] = {
    {0xD4AF37, "Gold"},  {0xCD7F32, "Bronze"}, {0x8B4513, "Wood"},
    {0xE8E8FF, "Clear"}, {0xC0C0C0, "Silver"}, {0xE0D5C7, "Marble"},
    {0xFF7043, "Coral"}, {0x1A237E, "Navy"},   {0xBCAAA4, "Taupe"},
};

std::string get_color_name_from_hex(uint32_t rgb) {
    // Check for special preset names first
    for (const auto& entry : kSpecialColorNames) {
        if (entry.hex == rgb) {
            return entry.name;
        }
    }
    // Use algorithmic color description
    return helix::describe_color(rgb);
}

} // namespace helix

namespace helix::ui {

// Static member initialization
bool ColorPicker::callbacks_registered_ = false;
ColorPicker* ColorPicker::active_instance_ = nullptr;

// ============================================================================
// Construction / Destruction
// ============================================================================

ColorPicker::ColorPicker() {
    spdlog::debug("[ColorPicker] Constructed");
}

ColorPicker::~ColorPicker() {
    // Modal destructor will call hide() if visible
    deinit_subjects();
    spdlog::trace("[ColorPicker] Destroyed");
}

ColorPicker::ColorPicker(ColorPicker&& other) noexcept
    : Modal(std::move(other)), selected_color_(other.selected_color_),
      color_callback_(std::move(other.color_callback_)),
      dismiss_callback_(std::move(other.dismiss_callback_)),
      subjects_initialized_(other.subjects_initialized_) {
    // Copy buffers
    std::memcpy(hex_buf_, other.hex_buf_, sizeof(hex_buf_));
    std::memcpy(name_buf_, other.name_buf_, sizeof(name_buf_));

    // Subjects are not movable - they stay with original
    other.subjects_initialized_ = false;
}

ColorPicker& ColorPicker::operator=(ColorPicker&& other) noexcept {
    if (this != &other) {
        Modal::operator=(std::move(other));
        selected_color_ = other.selected_color_;
        color_callback_ = std::move(other.color_callback_);
        dismiss_callback_ = std::move(other.dismiss_callback_);
        subjects_initialized_ = other.subjects_initialized_;
        std::memcpy(hex_buf_, other.hex_buf_, sizeof(hex_buf_));
        std::memcpy(name_buf_, other.name_buf_, sizeof(name_buf_));
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void ColorPicker::set_color_callback(ColorCallback callback) {
    color_callback_ = std::move(callback);
}

void ColorPicker::set_dismiss_callback(std::function<void()> callback) {
    dismiss_callback_ = std::move(callback);
}

bool ColorPicker::show_with_color(lv_obj_t* parent, uint32_t initial_color) {
    // Register callbacks once (idempotent)
    register_callbacks();

    // Initialize subjects if needed
    init_subjects();

    // Set initial color before showing
    selected_color_ = initial_color;

    // Show the modal via Modal
    if (!Modal::show(parent)) {
        return false;
    }

    // Track active instance for static callbacks
    active_instance_ = this;

    spdlog::info("[ColorPicker] Shown with initial color #{:06X}", initial_color);
    return true;
}

// ============================================================================
// Modal Hooks
// ============================================================================

void ColorPicker::on_show() {
    // Get hex input field
    hex_input_ = find_widget("hex_input");

    // Register keyboard for hex input so software keyboard appears on touch
    if (hex_input_ && dialog_) {
        helix::ui::modal_register_keyboard(dialog_, hex_input_);
    }

    // Bind name label to subject (save observer for cleanup)
    lv_obj_t* name_label = find_widget("selected_name_label");
    if (name_label) {
        name_label_observer_ = lv_label_bind_text(name_label, &name_subject_, nullptr);
    }

    // Initialize preview with current color
    update_preview(selected_color_);

    // Initialize HSV picker with current color and set callback
    lv_obj_t* hsv_picker = find_widget("hsv_picker");
    if (hsv_picker) {
        ui_hsv_picker_set_color_rgb(hsv_picker, selected_color_);
        ui_hsv_picker_set_callback(
            hsv_picker,
            [](uint32_t rgb, void* user_data) {
                auto* self = static_cast<ColorPicker*>(user_data);
                self->update_preview(rgb, true); // from HSV picker
            },
            this);
        spdlog::debug("[ColorPicker] HSV picker initialized with color #{:06X}", selected_color_);
    }
}

void ColorPicker::on_hide() {
    // Clear active instance
    if (active_instance_ == this) {
        active_instance_ = nullptr;
    }

    spdlog::debug("[ColorPicker] on_hide()");

    // Call dismiss callback if set (fires on any close - select, cancel, or backdrop)
    if (dismiss_callback_) {
        dismiss_callback_();
    }
}

void ColorPicker::on_cancel() {
    spdlog::debug("[ColorPicker] Cancelled");
    Modal::on_cancel(); // Calls hide()
}

// ============================================================================
// Subject Management
// ============================================================================

void ColorPicker::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize string subjects with empty buffers (local binding only, not XML registered)
    hex_buf_[0] = '\0';
    name_buf_[0] = '\0';

    lv_subject_init_string(&hex_subject_, hex_buf_, nullptr, sizeof(hex_buf_), "");
    subjects_.register_subject(&hex_subject_);

    lv_subject_init_string(&name_subject_, name_buf_, nullptr, sizeof(name_buf_), "");
    subjects_.register_subject(&name_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[ColorPicker] Subjects initialized");
}

void ColorPicker::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    // SubjectManager handles all lv_subject_deinit() calls via RAII
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[ColorPicker] Subjects deinitialized");
}

// ============================================================================
// Internal Methods
// ============================================================================

void ColorPicker::update_preview(uint32_t color_rgb, bool from_hsv_picker, bool from_hex_input) {
    if (!dialog_) {
        return;
    }

    selected_color_ = color_rgb;

    // Update the preview swatch
    lv_obj_t* preview = find_widget("selected_color_preview");
    if (preview) {
        lv_obj_set_style_bg_color(preview, lv_color_hex(color_rgb), 0);
    }

    // Update the hex input (unless change came from hex input itself)
    if (!from_hex_input && hex_input_) {
        hex_input_updating_ = true;
        snprintf(hex_buf_, sizeof(hex_buf_), "#%06X", color_rgb);
        lv_textarea_set_text(hex_input_, hex_buf_);
        lv_obj_set_style_text_color(hex_input_, theme_manager_get_color("text"), LV_PART_MAIN);
        hex_input_updating_ = false;
    }

    // Update the color name label via subject
    std::string name = helix::get_color_name_from_hex(color_rgb);
    snprintf(name_buf_, sizeof(name_buf_), "%s", name.c_str());
    lv_subject_copy_string(&name_subject_, name_buf_);

    // Sync HSV picker if change came from preset swatch (not from HSV picker itself)
    if (!from_hsv_picker) {
        lv_obj_t* hsv_picker = find_widget("hsv_picker");
        if (hsv_picker) {
            ui_hsv_picker_set_color_rgb(hsv_picker, color_rgb);
        }
    }
}

void ColorPicker::handle_swatch_clicked(lv_obj_t* swatch) {
    if (!swatch || !dialog_) {
        return;
    }

    // Get the background color from the clicked swatch
    lv_color_t color = lv_obj_get_style_bg_color(swatch, LV_PART_MAIN);
    uint32_t rgb = lv_color_to_u32(color) & 0xFFFFFF;

    update_preview(rgb);
}

void ColorPicker::handle_select() {
    std::string color_name = helix::get_color_name_from_hex(selected_color_);
    spdlog::info("[ColorPicker] Color selected: #{:06X} ({})", selected_color_, color_name);

    // Invoke callback before hiding
    if (color_callback_) {
        color_callback_(selected_color_, color_name);
    }

    // Hide the picker
    hide();
}

void ColorPicker::handle_hex_input_changed() {
    if (hex_input_updating_ || !hex_input_) {
        return;
    }

    const char* text = lv_textarea_get_text(hex_input_);
    uint32_t parsed_color;

    if (helix::parse_hex_color(text, parsed_color)) {
        // Valid - normal text color, update preview
        lv_obj_set_style_text_color(hex_input_, theme_manager_get_color("text"), LV_PART_MAIN);
        update_preview(parsed_color, false, true); // from_hex_input=true
    } else {
        // Invalid - show error color
        lv_obj_set_style_text_color(hex_input_, theme_manager_get_color("danger"), LV_PART_MAIN);
    }
}

void ColorPicker::handle_hex_input_defocused() {
    if (!hex_input_) {
        return;
    }

    const char* text = lv_textarea_get_text(hex_input_);
    uint32_t parsed_color;

    if (!helix::parse_hex_color(text, parsed_color)) {
        // Invalid on defocus - revert to current selected color
        hex_input_updating_ = true;
        snprintf(hex_buf_, sizeof(hex_buf_), "#%06X", selected_color_);
        lv_textarea_set_text(hex_input_, hex_buf_);
        lv_obj_set_style_text_color(hex_input_, theme_manager_get_color("text"), LV_PART_MAIN);
        hex_input_updating_ = false;
    }
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void ColorPicker::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "color_picker_close_cb", on_close_cb);
    lv_xml_register_event_cb(nullptr, "color_swatch_clicked_cb", on_swatch_cb);
    lv_xml_register_event_cb(nullptr, "color_picker_cancel_cb", on_cancel_cb);
    lv_xml_register_event_cb(nullptr, "color_picker_select_cb", on_select_cb);
    lv_xml_register_event_cb(nullptr, "hex_input_changed_cb", on_hex_input_changed_cb);
    lv_xml_register_event_cb(nullptr, "hex_input_defocused_cb", on_hex_input_defocused_cb);

    callbacks_registered_ = true;
    spdlog::debug("[ColorPicker] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

ColorPicker* ColorPicker::get_instance_from_event(lv_event_t* e) {
    (void)e; // Not needed - we use static instance tracking
    return active_instance_;
}

void ColorPicker::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->hide();
    }
}

void ColorPicker::on_swatch_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* swatch = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->handle_swatch_clicked(swatch);
    }
}

void ColorPicker::on_cancel_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->on_cancel();
    }
}

void ColorPicker::on_select_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_select();
    }
}

void ColorPicker::on_hex_input_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_hex_input_changed();
    }
}

void ColorPicker::on_hex_input_defocused_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_hex_input_defocused();
    }
}

} // namespace helix::ui
