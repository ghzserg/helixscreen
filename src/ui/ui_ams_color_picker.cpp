// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_color_picker.h"

#include "ui_hsv_picker.h"

#include "color_utils.h"

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
bool AmsColorPicker::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsColorPicker::AmsColorPicker() {
    spdlog::debug("[AmsColorPicker] Constructed");
}

AmsColorPicker::~AmsColorPicker() {
    // Modal destructor will call hide() if visible
    deinit_subjects();
    spdlog::debug("[AmsColorPicker] Destroyed");
}

AmsColorPicker::AmsColorPicker(AmsColorPicker&& other) noexcept
    : Modal(std::move(other)), selected_color_(other.selected_color_),
      color_callback_(std::move(other.color_callback_)),
      subjects_initialized_(other.subjects_initialized_) {
    // Copy buffers
    std::memcpy(hex_buf_, other.hex_buf_, sizeof(hex_buf_));
    std::memcpy(name_buf_, other.name_buf_, sizeof(name_buf_));

    // Subjects are not movable - they stay with original
    other.subjects_initialized_ = false;
}

AmsColorPicker& AmsColorPicker::operator=(AmsColorPicker&& other) noexcept {
    if (this != &other) {
        Modal::operator=(std::move(other));
        selected_color_ = other.selected_color_;
        color_callback_ = std::move(other.color_callback_);
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

void AmsColorPicker::set_color_callback(ColorCallback callback) {
    color_callback_ = std::move(callback);
}

bool AmsColorPicker::show_with_color(lv_obj_t* parent, uint32_t initial_color) {
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

    // Store 'this' in modal's user_data for callback traversal
    lv_obj_set_user_data(dialog_, this);

    spdlog::info("[AmsColorPicker] Shown with initial color #{:06X}", initial_color);
    return true;
}

// ============================================================================
// Modal Hooks
// ============================================================================

void AmsColorPicker::on_show() {
    // Bind hex and name labels to subjects (save observers for cleanup)
    lv_obj_t* hex_label = find_widget("selected_hex_label");
    if (hex_label) {
        hex_label_observer_ = lv_label_bind_text(hex_label, &hex_subject_, nullptr);
    }

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
                auto* self = static_cast<AmsColorPicker*>(user_data);
                self->update_preview(rgb, true); // from HSV picker
            },
            this);
        spdlog::debug("[AmsColorPicker] HSV picker initialized with color #{:06X}",
                      selected_color_);
    }
}

void AmsColorPicker::on_hide() {
    // Observer cleanup is handled by SubjectManager::deinit_all() in deinit_subjects()
    // which calls lv_subject_deinit() on each subject. We avoid manual lv_observer_remove()
    // because the destructor calls deinit_subjects() before the Modal base destructor
    // calls on_hide(), which would leave us with stale observer pointers.
    spdlog::debug("[AmsColorPicker] on_hide()");
}

void AmsColorPicker::on_cancel() {
    spdlog::debug("[AmsColorPicker] Cancelled");
    Modal::on_cancel(); // Calls hide()
}

// ============================================================================
// Subject Management
// ============================================================================

void AmsColorPicker::init_subjects() {
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
    spdlog::debug("[AmsColorPicker] Subjects initialized");
}

void AmsColorPicker::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    // SubjectManager handles all lv_subject_deinit() calls via RAII
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[AmsColorPicker] Subjects deinitialized");
}

// ============================================================================
// Internal Methods
// ============================================================================

void AmsColorPicker::update_preview(uint32_t color_rgb, bool from_hsv_picker) {
    if (!dialog_) {
        return;
    }

    selected_color_ = color_rgb;

    // Update the preview swatch
    lv_obj_t* preview = find_widget("selected_color_preview");
    if (preview) {
        lv_obj_set_style_bg_color(preview, lv_color_hex(color_rgb), 0);
    }

    // Update the hex label via subject
    snprintf(hex_buf_, sizeof(hex_buf_), "#%06X", color_rgb);
    lv_subject_copy_string(&hex_subject_, hex_buf_);

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

void AmsColorPicker::handle_swatch_clicked(lv_obj_t* swatch) {
    if (!swatch || !dialog_) {
        return;
    }

    // Get the background color from the clicked swatch
    lv_color_t color = lv_obj_get_style_bg_color(swatch, LV_PART_MAIN);
    uint32_t rgb = lv_color_to_u32(color) & 0xFFFFFF;

    update_preview(rgb);
}

void AmsColorPicker::handle_select() {
    std::string color_name = helix::get_color_name_from_hex(selected_color_);
    spdlog::info("[AmsColorPicker] Color selected: #{:06X} ({})", selected_color_, color_name);

    // Invoke callback before hiding
    if (color_callback_) {
        color_callback_(selected_color_, color_name);
    }

    // Hide the picker
    hide();
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsColorPicker::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "color_picker_close_cb", on_close_cb);
    lv_xml_register_event_cb(nullptr, "color_swatch_clicked_cb", on_swatch_cb);
    lv_xml_register_event_cb(nullptr, "color_picker_cancel_cb", on_cancel_cb);
    lv_xml_register_event_cb(nullptr, "color_picker_select_cb", on_select_cb);

    callbacks_registered_ = true;
    spdlog::debug("[AmsColorPicker] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

AmsColorPicker* AmsColorPicker::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Traverse parent chain to find modal root with user_data
    lv_obj_t* obj = target;
    while (obj) {
        void* user_data = lv_obj_get_user_data(obj);
        if (user_data) {
            return static_cast<AmsColorPicker*>(user_data);
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[AmsColorPicker] Could not find instance from event target");
    return nullptr;
}

void AmsColorPicker::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->hide();
    }
}

void AmsColorPicker::on_swatch_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* swatch = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->handle_swatch_clicked(swatch);
    }
}

void AmsColorPicker::on_cancel_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->on_cancel();
    }
}

void AmsColorPicker::on_select_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_select();
    }
}

} // namespace helix::ui
