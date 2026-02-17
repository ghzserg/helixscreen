// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_context_menu.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
bool SpoolmanContextMenu::callbacks_registered_ = false;
SpoolmanContextMenu* SpoolmanContextMenu::s_active_instance_ = nullptr;

// ============================================================================
// Construction / Destruction
// ============================================================================

SpoolmanContextMenu::SpoolmanContextMenu() {
    spdlog::debug("[SpoolmanContextMenu] Constructed");
}

SpoolmanContextMenu::~SpoolmanContextMenu() {
    if (s_active_instance_ == this) {
        s_active_instance_ = nullptr;
    }
    spdlog::trace("[SpoolmanContextMenu] Destroyed");
}

SpoolmanContextMenu::SpoolmanContextMenu(SpoolmanContextMenu&& other) noexcept
    : ContextMenu(std::move(other)), action_callback_(std::move(other.action_callback_)),
      pending_spool_(std::move(other.pending_spool_)) {
    if (s_active_instance_ == &other) {
        s_active_instance_ = this;
    }
}

SpoolmanContextMenu& SpoolmanContextMenu::operator=(SpoolmanContextMenu&& other) noexcept {
    if (this != &other) {
        if (s_active_instance_ == this) {
            s_active_instance_ = nullptr;
        }

        ContextMenu::operator=(std::move(other));
        action_callback_ = std::move(other.action_callback_);
        pending_spool_ = std::move(other.pending_spool_);

        if (s_active_instance_ == &other) {
            s_active_instance_ = this;
        }
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void SpoolmanContextMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

bool SpoolmanContextMenu::show_for_spool(lv_obj_t* parent, const SpoolInfo& spool,
                                         lv_obj_t* near_widget) {
    register_callbacks();

    // Store spool info before base class calls on_created
    pending_spool_ = spool;

    // Set as active instance for static callbacks
    s_active_instance_ = this;

    // Base class handles: XML creation, on_created callback, positioning
    // item_index = spool.id for action dispatch
    bool result = ContextMenu::show_near_widget(parent, spool.id, near_widget);
    if (!result) {
        s_active_instance_ = nullptr;
    }

    return result;
}

// ============================================================================
// ContextMenu override
// ============================================================================

void SpoolmanContextMenu::on_created(lv_obj_t* menu_obj) {
    // Header: "Vendor Material" (e.g., "Polymaker PLA")
    lv_obj_t* header = lv_obj_find_by_name(menu_obj, "spool_header");
    if (header) {
        std::string name;
        if (!pending_spool_.vendor.empty()) {
            name = pending_spool_.vendor;
        }
        if (!pending_spool_.material.empty()) {
            if (!name.empty()) {
                name += " ";
            }
            name += pending_spool_.material;
        }
        if (name.empty()) {
            name = "Spool #" + std::to_string(pending_spool_.id);
        }
        lv_label_set_text(header, name.c_str());
    }

    // Color subtitle (e.g., "Jet Black") -- hidden when no color name
    lv_obj_t* color_label = lv_obj_find_by_name(menu_obj, "spool_color_label");
    if (color_label) {
        if (pending_spool_.color_name.empty()) {
            lv_obj_add_flag(color_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(color_label, pending_spool_.color_name.c_str());
        }
    }

    // Vendor subtitle is unused (vendor already in header); hide the XML element
    lv_obj_t* vendor_label = lv_obj_find_by_name(menu_obj, "spool_vendor_label");
    if (vendor_label) {
        lv_obj_add_flag(vendor_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Prevent context menu buttons from triggering scroll on the underlying list
    lv_obj_remove_flag(menu_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_t* card = lv_obj_find_by_name(menu_obj, "context_menu");
    if (card) {
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        uint32_t card_children = lv_obj_get_child_count(card);
        for (uint32_t i = 0; i < card_children; i++) {
            lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
            if (child) {
                lv_obj_remove_flag(child, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            }
        }
    }

    spdlog::debug("[SpoolmanContextMenu] Shown for spool {} ({})", pending_spool_.id,
                  pending_spool_.display_name());
}

// ============================================================================
// Event Handlers
// ============================================================================

void SpoolmanContextMenu::dispatch_spoolman_action(MenuAction action) {
    int spool_id = get_item_index();
    ActionCallback callback_copy = action_callback_;

    if (s_active_instance_ == this) {
        s_active_instance_ = nullptr;
    }
    hide();

    if (callback_copy) {
        callback_copy(action, spool_id);
    }
}

void SpoolmanContextMenu::handle_backdrop_clicked() {
    spdlog::debug("[SpoolmanContextMenu] Backdrop clicked");
    dispatch_spoolman_action(MenuAction::CANCELLED);
}

void SpoolmanContextMenu::handle_set_active() {
    spdlog::info("[SpoolmanContextMenu] Set active requested for spool {}", get_item_index());
    dispatch_spoolman_action(MenuAction::SET_ACTIVE);
}

void SpoolmanContextMenu::handle_edit() {
    spdlog::info("[SpoolmanContextMenu] Edit requested for spool {}", get_item_index());
    dispatch_spoolman_action(MenuAction::EDIT);
}

void SpoolmanContextMenu::handle_print_label() {
    spdlog::info("[SpoolmanContextMenu] Print label requested for spool {}", get_item_index());
    dispatch_spoolman_action(MenuAction::PRINT_LABEL);
}

void SpoolmanContextMenu::handle_delete() {
    spdlog::info("[SpoolmanContextMenu] Delete requested for spool {}", get_item_index());
    dispatch_spoolman_action(MenuAction::DELETE);
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void SpoolmanContextMenu::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "spoolman_context_backdrop_cb", on_backdrop_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_context_set_active_cb", on_set_active_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_context_edit_cb", on_edit_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_context_print_label_cb", on_print_label_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_context_delete_cb", on_delete_cb);

    callbacks_registered_ = true;
    spdlog::debug("[SpoolmanContextMenu] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via Static Pointer)
// ============================================================================

SpoolmanContextMenu* SpoolmanContextMenu::get_active_instance() {
    if (!s_active_instance_) {
        spdlog::warn("[SpoolmanContextMenu] No active instance for event");
    }
    return s_active_instance_;
}

void SpoolmanContextMenu::on_backdrop_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_backdrop_clicked();
    }
}

void SpoolmanContextMenu::on_set_active_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_set_active();
    }
}

void SpoolmanContextMenu::on_edit_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_edit();
    }
}

void SpoolmanContextMenu::on_print_label_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_print_label();
    }
}

void SpoolmanContextMenu::on_delete_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_delete();
    }
}

} // namespace helix::ui
