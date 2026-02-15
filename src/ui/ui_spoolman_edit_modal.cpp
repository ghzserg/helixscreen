// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_edit_modal.h"

#include "ui_spool_canvas.h"
#include "ui_toast.h"
#include "ui_update_queue.h"

#include "moonraker_api.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace helix::ui {

// Static member initialization
bool SpoolEditModal::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

SpoolEditModal::SpoolEditModal() {
    spdlog::debug("[SpoolEditModal] Constructed");
}

SpoolEditModal::~SpoolEditModal() {
    spdlog::trace("[SpoolEditModal] Destroyed");
}

// ============================================================================
// Public API
// ============================================================================

void SpoolEditModal::set_completion_callback(CompletionCallback callback) {
    completion_callback_ = std::move(callback);
}

bool SpoolEditModal::show_for_spool(lv_obj_t* parent, const SpoolInfo& spool, MoonrakerAPI* api) {
    register_callbacks();

    original_spool_ = spool;
    working_spool_ = spool;
    api_ = api;

    if (!Modal::show(parent)) {
        return false;
    }

    lv_obj_set_user_data(dialog_, this);

    spdlog::info("[SpoolEditModal] Shown for spool {} ({})", spool.id, spool.display_name());
    return true;
}

// ============================================================================
// Modal Hooks
// ============================================================================

void SpoolEditModal::on_show() {
    callback_guard_ = std::make_shared<bool>(true);
    populate_fields();
    update_spool_preview();
    update_save_button_text();
}

void SpoolEditModal::on_hide() {
    callback_guard_.reset();
    spdlog::debug("[SpoolEditModal] on_hide()");
}

// ============================================================================
// Internal Methods
// ============================================================================

void SpoolEditModal::populate_fields() {
    if (!dialog_) {
        return;
    }

    // Title
    lv_obj_t* title = find_widget("spool_title");
    if (title) {
        std::string title_text = "Edit Spool #" + std::to_string(working_spool_.id);
        lv_label_set_text(title, title_text.c_str());
    }

    // Read-only info labels
    lv_obj_t* material_label = find_widget("material_label");
    if (material_label) {
        const char* material =
            working_spool_.material.empty() ? "Unknown" : working_spool_.material.c_str();
        lv_label_set_text(material_label, material);
    }

    lv_obj_t* color_label = find_widget("color_label");
    if (color_label) {
        std::string color;
        if (!working_spool_.color_name.empty()) {
            color = working_spool_.color_name;
        } else if (!working_spool_.color_hex.empty()) {
            color = working_spool_.color_hex;
        } else {
            color = "No color";
        }
        lv_label_set_text(color_label, color.c_str());
    }

    lv_obj_t* vendor_label = find_widget("vendor_label");
    if (vendor_label) {
        const char* vendor =
            working_spool_.vendor.empty() ? "Unknown" : working_spool_.vendor.c_str();
        lv_label_set_text(vendor_label, vendor);
    }

    // Editable fields
    lv_obj_t* remaining_field = find_widget("field_remaining");
    if (remaining_field) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", working_spool_.remaining_weight_g);
        lv_textarea_set_text(remaining_field, buf);
    }

    lv_obj_t* spool_weight_field = find_widget("field_spool_weight");
    if (spool_weight_field) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", working_spool_.spool_weight_g);
        lv_textarea_set_text(spool_weight_field, buf);
    }

    lv_obj_t* price_field = find_widget("field_price");
    if (price_field) {
        if (working_spool_.price > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", working_spool_.price);
            lv_textarea_set_text(price_field, buf);
        } else {
            lv_textarea_set_text(price_field, "");
        }
    }

    lv_obj_t* lot_field = find_widget("field_lot_nr");
    if (lot_field) {
        lv_textarea_set_text(lot_field, working_spool_.lot_nr.c_str());
    }

    lv_obj_t* comment_field = find_widget("field_comment");
    if (comment_field) {
        lv_textarea_set_text(comment_field, working_spool_.comment.c_str());
    }
}

void SpoolEditModal::update_spool_preview() {
    if (!dialog_) {
        return;
    }

    lv_obj_t* canvas = find_widget("spool_preview");
    if (!canvas) {
        return;
    }

    // Set color from spool's hex color
    lv_color_t color = theme_manager_get_color("text_muted"); // Default gray
    if (!working_spool_.color_hex.empty()) {
        std::string hex = working_spool_.color_hex;
        if (!hex.empty() && hex[0] == '#') {
            hex = hex.substr(1);
        }
        unsigned int color_val = 0;
        if (sscanf(hex.c_str(), "%x", &color_val) == 1) {
            color = lv_color_hex(color_val); // parse spool color_hex
        }
    }
    ui_spool_canvas_set_color(canvas, color);

    // Set fill level from remaining weight
    float fill_level = 0.5f;
    if (working_spool_.initial_weight_g > 0) {
        fill_level = static_cast<float>(working_spool_.remaining_weight_g) /
                     static_cast<float>(working_spool_.initial_weight_g);
        fill_level = std::max(0.0f, std::min(1.0f, fill_level));
    }
    ui_spool_canvas_set_fill_level(canvas, fill_level);
    ui_spool_canvas_redraw(canvas);
}

bool SpoolEditModal::is_dirty() const {
    return std::abs(working_spool_.remaining_weight_g - original_spool_.remaining_weight_g) > 0.1 ||
           std::abs(working_spool_.spool_weight_g - original_spool_.spool_weight_g) > 0.1 ||
           std::abs(working_spool_.price - original_spool_.price) > 0.001 ||
           working_spool_.lot_nr != original_spool_.lot_nr ||
           working_spool_.comment != original_spool_.comment;
}

void SpoolEditModal::update_save_button_text() {
    if (!dialog_) {
        return;
    }

    // Find the primary button label in the modal_button_row
    lv_obj_t* btn = find_widget("btn_primary");
    if (btn) {
        lv_obj_t* label = lv_obj_find_by_name(btn, "label");
        if (label) {
            lv_label_set_text(label, is_dirty() ? "Save" : "Close");
        }
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void SpoolEditModal::handle_close() {
    spdlog::debug("[SpoolEditModal] Close requested");

    if (completion_callback_) {
        completion_callback_(false);
    }
    hide();
}

void SpoolEditModal::handle_field_changed() {
    if (!dialog_) {
        return;
    }

    // Read current field values into working_spool_
    lv_obj_t* field = find_widget("field_remaining");
    if (field) {
        const char* text = lv_textarea_get_text(field);
        working_spool_.remaining_weight_g = text ? std::atof(text) : 0;
    }

    field = find_widget("field_spool_weight");
    if (field) {
        const char* text = lv_textarea_get_text(field);
        working_spool_.spool_weight_g = text ? std::atof(text) : 0;
    }

    field = find_widget("field_price");
    if (field) {
        const char* text = lv_textarea_get_text(field);
        working_spool_.price = text ? std::atof(text) : 0;
    }

    field = find_widget("field_lot_nr");
    if (field) {
        const char* text = lv_textarea_get_text(field);
        working_spool_.lot_nr = text ? text : "";
    }

    field = find_widget("field_comment");
    if (field) {
        const char* text = lv_textarea_get_text(field);
        working_spool_.comment = text ? text : "";
    }

    update_spool_preview();
    update_save_button_text();
}

void SpoolEditModal::handle_reset() {
    spdlog::debug("[SpoolEditModal] Resetting to original values");

    working_spool_ = original_spool_;

    populate_fields();
    update_spool_preview();
    update_save_button_text();

    ui_toast_show(ToastSeverity::INFO, "Reset to original values", 2000);
}

void SpoolEditModal::handle_save() {
    if (!is_dirty()) {
        // Nothing changed — just close
        handle_close();
        return;
    }

    if (!api_) {
        spdlog::warn("[SpoolEditModal] No API, cannot save");
        ui_toast_show(ToastSeverity::ERROR, "API not available", 3000);
        return;
    }

    spdlog::info("[SpoolEditModal] Saving spool {} edits", working_spool_.id);

    // Build PATCH body with changed fields only
    nlohmann::json patch;

    if (std::abs(working_spool_.remaining_weight_g - original_spool_.remaining_weight_g) > 0.1) {
        patch["remaining_weight"] = working_spool_.remaining_weight_g;
    }
    if (std::abs(working_spool_.spool_weight_g - original_spool_.spool_weight_g) > 0.1) {
        patch["spool_weight"] = working_spool_.spool_weight_g;
    }
    if (std::abs(working_spool_.price - original_spool_.price) > 0.001) {
        patch["price"] = working_spool_.price;
    }
    if (working_spool_.lot_nr != original_spool_.lot_nr) {
        patch["lot_nr"] = working_spool_.lot_nr;
    }
    if (working_spool_.comment != original_spool_.comment) {
        patch["comment"] = working_spool_.comment;
    }

    int spool_id = working_spool_.id;
    std::weak_ptr<bool> guard = callback_guard_;

    api_->update_spoolman_spool(
        spool_id, patch,
        [this, guard, spool_id]() {
            if (guard.expired()) {
                return;
            }
            spdlog::info("[SpoolEditModal] Spool {} saved successfully", spool_id);
            // Schedule UI work on LVGL thread (API callbacks run on background thread)
            ui_async_call(
                [](void* ud) {
                    auto* self = static_cast<SpoolEditModal*>(ud);
                    // Re-check guard — modal may have been hidden since scheduling
                    if (!self->callback_guard_) {
                        return;
                    }
                    ui_toast_show(ToastSeverity::SUCCESS, "Spool saved", 2000);
                    if (self->completion_callback_) {
                        self->completion_callback_(true);
                    }
                    self->hide();
                },
                this);
        },
        [spool_id](const MoonrakerError& err) {
            spdlog::error("[SpoolEditModal] Failed to save spool {}: {}", spool_id, err.message);
            ui_async_call(
                [](void*) { ui_toast_show(ToastSeverity::ERROR, "Failed to save spool", 3000); },
                nullptr);
        });
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void SpoolEditModal::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "spoolman_edit_close_cb", on_close_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_edit_field_changed_cb", on_field_changed_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_edit_reset_cb", on_reset_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_edit_save_cb", on_save_cb);

    callbacks_registered_ = true;
    spdlog::debug("[SpoolEditModal] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

SpoolEditModal* SpoolEditModal::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Traverse parent chain to find modal root with user_data
    lv_obj_t* obj = target;
    while (obj) {
        void* user_data = lv_obj_get_user_data(obj);
        if (user_data) {
            return static_cast<SpoolEditModal*>(user_data);
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[SpoolEditModal] Could not find instance from event target");
    return nullptr;
}

void SpoolEditModal::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_close();
    }
}

void SpoolEditModal::on_field_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_field_changed();
    }
}

void SpoolEditModal::on_reset_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_reset();
    }
}

void SpoolEditModal::on_save_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_save();
    }
}

} // namespace helix::ui
