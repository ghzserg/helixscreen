// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_context_menu.h"

#include "ui_toast.h"
#include "ui_utils.h"

#include "ams_backend.h"
#include "filament_database.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
bool AmsContextMenu::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsContextMenu::AmsContextMenu() {
    // Initialize the subject for Unload button enabled state
    lv_subject_init_int(&slot_is_loaded_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_slot_is_loaded", &slot_is_loaded_subject_);
    subject_initialized_ = true;
    spdlog::debug("[AmsContextMenu] Constructed");
}

AmsContextMenu::~AmsContextMenu() {
    hide();

    // Clean up subject
    if (subject_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&slot_is_loaded_subject_);
        subject_initialized_ = false;
    }
    spdlog::debug("[AmsContextMenu] Destroyed");
}

AmsContextMenu::AmsContextMenu(AmsContextMenu&& other) noexcept
    : menu_(other.menu_), parent_(other.parent_), slot_index_(other.slot_index_),
      action_callback_(std::move(other.action_callback_)), backend_(other.backend_),
      total_slots_(other.total_slots_), tool_dropdown_(other.tool_dropdown_),
      backup_dropdown_(other.backup_dropdown_) {
    other.menu_ = nullptr;
    other.parent_ = nullptr;
    other.slot_index_ = -1;
    other.backend_ = nullptr;
    other.total_slots_ = 0;
    other.tool_dropdown_ = nullptr;
    other.backup_dropdown_ = nullptr;
}

AmsContextMenu& AmsContextMenu::operator=(AmsContextMenu&& other) noexcept {
    if (this != &other) {
        hide();

        menu_ = other.menu_;
        parent_ = other.parent_;
        slot_index_ = other.slot_index_;
        action_callback_ = std::move(other.action_callback_);
        backend_ = other.backend_;
        total_slots_ = other.total_slots_;
        tool_dropdown_ = other.tool_dropdown_;
        backup_dropdown_ = other.backup_dropdown_;

        other.menu_ = nullptr;
        other.parent_ = nullptr;
        other.slot_index_ = -1;
        other.backend_ = nullptr;
        other.total_slots_ = 0;
        other.tool_dropdown_ = nullptr;
        other.backup_dropdown_ = nullptr;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void AmsContextMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

bool AmsContextMenu::show_near_widget(lv_obj_t* parent, int slot_index, lv_obj_t* near_widget,
                                      bool is_loaded, AmsBackend* backend) {
    // Hide any existing menu first
    hide();

    if (!parent || !near_widget) {
        spdlog::warn("[AmsContextMenu] Cannot show - missing parent or widget");
        return false;
    }

    // Register callbacks once (idempotent)
    register_callbacks();

    // Store state
    parent_ = parent;
    slot_index_ = slot_index;
    backend_ = backend;

    // Get total slots from backend if available
    if (backend_) {
        total_slots_ = backend_->get_system_info().total_slots;
    } else {
        total_slots_ = 0;
    }

    // Update subject for Unload button state (1=enabled, 0=disabled)
    lv_subject_set_int(&slot_is_loaded_subject_, is_loaded ? 1 : 0);

    // Create context menu from XML
    menu_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "ams_context_menu", nullptr));
    if (!menu_) {
        spdlog::error("[AmsContextMenu] Failed to create menu from XML");
        return false;
    }

    // Store 'this' in menu's user_data for callback traversal
    lv_obj_set_user_data(menu_, this);

    // Configure dropdowns based on backend capabilities
    configure_dropdowns();

    // Find the menu card to position it
    lv_obj_t* menu_card = lv_obj_find_by_name(menu_, "context_menu");

    if (menu_card) {
        // Update layout to get accurate dimensions
        lv_obj_update_layout(menu_card);

        // Get the position of the slot widget in screen coordinates
        lv_point_t slot_pos;
        lv_obj_get_coords(near_widget, (lv_area_t*)&slot_pos);

        // Calculate positioning
        int32_t screen_width = lv_obj_get_width(parent);
        int32_t menu_width = lv_obj_get_width(menu_card);
        int32_t slot_center_x = slot_pos.x + lv_obj_get_width(near_widget) / 2;
        int32_t slot_center_y = slot_pos.y + lv_obj_get_height(near_widget) / 2;

        // Position to the right of slot, or left if near screen edge
        int32_t menu_x = slot_center_x + 20;
        if (menu_x + menu_width > screen_width - 10) {
            menu_x = slot_center_x - menu_width - 20;
        }

        // Center vertically on the slot
        int32_t menu_y = slot_center_y - lv_obj_get_height(menu_card) / 2;

        // Clamp to screen bounds
        int32_t screen_height = lv_obj_get_height(parent);
        if (menu_y < 10) {
            menu_y = 10;
        }
        if (menu_y + lv_obj_get_height(menu_card) > screen_height - 10) {
            menu_y = screen_height - lv_obj_get_height(menu_card) - 10;
        }

        lv_obj_set_pos(menu_card, menu_x, menu_y);
    }

    spdlog::debug("[AmsContextMenu] Shown for slot {}", slot_index);
    return true;
}

void AmsContextMenu::hide() {
    // Check if LVGL is initialized - may be called from destructor during static destruction
    if (lv_obj_safe_delete(menu_)) {
        slot_index_ = -1;
        spdlog::debug("[AmsContextMenu] hide()");
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsContextMenu::handle_backdrop_clicked() {
    int slot = slot_index_; // Capture before hide() resets it
    spdlog::debug("[AmsContextMenu] Backdrop clicked");

    hide(); // Hide before callback (consistent with other handlers)

    if (action_callback_) {
        action_callback_(MenuAction::CANCELLED, slot);
    }
}

void AmsContextMenu::handle_load() {
    int slot = slot_index_;
    spdlog::info("[AmsContextMenu] Load requested for slot {}", slot);

    hide(); // Hide first so slot_index_ is valid in callback

    if (action_callback_) {
        action_callback_(MenuAction::LOAD, slot);
    }
}

void AmsContextMenu::handle_unload() {
    int slot = slot_index_;
    spdlog::info("[AmsContextMenu] Unload requested for slot {}", slot);

    hide();

    if (action_callback_) {
        action_callback_(MenuAction::UNLOAD, slot);
    }
}

void AmsContextMenu::handle_edit() {
    int slot = slot_index_;
    spdlog::info("[AmsContextMenu] Edit requested for slot {}", slot);

    hide();

    if (action_callback_) {
        action_callback_(MenuAction::EDIT, slot);
    }
}

void AmsContextMenu::handle_spoolman() {
    int slot = slot_index_;
    spdlog::info("[AmsContextMenu] Spoolman requested for slot {}", slot);

    hide();

    if (action_callback_) {
        action_callback_(MenuAction::SPOOLMAN, slot);
    }
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsContextMenu::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "ams_context_backdrop_cb", on_backdrop_cb);
    lv_xml_register_event_cb(nullptr, "ams_context_load_cb", on_load_cb);
    lv_xml_register_event_cb(nullptr, "ams_context_unload_cb", on_unload_cb);
    lv_xml_register_event_cb(nullptr, "ams_context_edit_cb", on_edit_cb);
    lv_xml_register_event_cb(nullptr, "ams_context_spoolman_cb", on_spoolman_cb);
    lv_xml_register_event_cb(nullptr, "ams_context_tool_changed_cb", on_tool_changed_cb);
    lv_xml_register_event_cb(nullptr, "ams_context_backup_changed_cb", on_backup_changed_cb);

    callbacks_registered_ = true;
    spdlog::debug("[AmsContextMenu] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

AmsContextMenu* AmsContextMenu::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Traverse parent chain to find menu root with user_data
    lv_obj_t* obj = target;
    while (obj) {
        void* user_data = lv_obj_get_user_data(obj);
        if (user_data) {
            return static_cast<AmsContextMenu*>(user_data);
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[AmsContextMenu] Could not find instance from event target");
    return nullptr;
}

void AmsContextMenu::on_backdrop_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_backdrop_clicked();
    }
}

void AmsContextMenu::on_load_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_load();
    }
}

void AmsContextMenu::on_unload_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_unload();
    }
}

void AmsContextMenu::on_edit_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_edit();
    }
}

void AmsContextMenu::on_spoolman_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_spoolman();
    }
}

void AmsContextMenu::on_tool_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_tool_changed();
    }
}

void AmsContextMenu::on_backup_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_backup_changed();
    }
}

// ============================================================================
// Dropdown Handlers
// ============================================================================

void AmsContextMenu::handle_tool_changed() {
    if (!tool_dropdown_ || !backend_) {
        return;
    }

    int selected = static_cast<int>(lv_dropdown_get_selected(tool_dropdown_));
    // Option 0 = "None", options 1+ = T0, T1, T2...
    int tool_number = selected - 1; // -1 = None

    spdlog::info("[AmsContextMenu] Tool mapping changed for slot {}: tool {}", slot_index_,
                 tool_number >= 0 ? tool_number : -1);

    if (tool_number >= 0) {
        // Set this slot as the mapping for the selected tool
        auto result = backend_->set_tool_mapping(tool_number, slot_index_);
        if (!result.success()) {
            spdlog::warn("[AmsContextMenu] Failed to set tool mapping: {}", result.user_msg);
        }
    }
    // Note: "None" selection doesn't clear mapping - user needs to map another slot to that tool
}

void AmsContextMenu::handle_backup_changed() {
    if (!backup_dropdown_ || !backend_) {
        return;
    }

    int selected = static_cast<int>(lv_dropdown_get_selected(backup_dropdown_));

    // Convert dropdown index back to actual slot index
    // Dropdown: None=0, then all slots except current slot
    int backup_slot = -1; // Default to None
    if (selected > 0) {
        // Find the actual slot index by counting through slots (skipping current)
        int dropdown_idx = 0;
        for (int i = 0; i < total_slots_; ++i) {
            if (i != slot_index_) {
                dropdown_idx++;
                if (dropdown_idx == selected) {
                    backup_slot = i;
                    break;
                }
            }
        }
    }

    // Validate material compatibility if a backup slot was selected
    if (backup_slot >= 0 && slot_index_ >= 0) {
        std::string current_material = backend_->get_slot_info(slot_index_).material;
        std::string backup_material = backend_->get_slot_info(backup_slot).material;

        // Only check compatibility if both slots have materials set
        if (!current_material.empty() && !backup_material.empty() &&
            !filament::are_materials_compatible(current_material, backup_material)) {
            spdlog::warn("[AmsContextMenu] Incompatible backup: {} cannot use {} as backup",
                         current_material, backup_material);

            // Show toast error
            std::string msg = "Incompatible materials: " + current_material + " cannot use " +
                              backup_material + " as backup";
            ui_toast_show(ToastSeverity::ERROR, msg.c_str());

            // Reset dropdown to "None" (index 0)
            lv_dropdown_set_selected(backup_dropdown_, 0);
            return;
        }
    }

    spdlog::info("[AmsContextMenu] Backup slot changed for slot {}: backup {}", slot_index_,
                 backup_slot >= 0 ? backup_slot : -1);

    auto result = backend_->set_endless_spool_backup(slot_index_, backup_slot);
    if (!result.success()) {
        spdlog::warn("[AmsContextMenu] Failed to set endless spool backup: {}", result.user_msg);
    }
}

// ============================================================================
// Dropdown Configuration
// ============================================================================

void AmsContextMenu::configure_dropdowns() {
    if (!menu_) {
        return;
    }

    // Find dropdown widgets
    tool_dropdown_ = lv_obj_find_by_name(menu_, "tool_dropdown");
    backup_dropdown_ = lv_obj_find_by_name(menu_, "backup_dropdown");

    // Find row containers and divider
    lv_obj_t* tool_row = lv_obj_find_by_name(menu_, "tool_dropdown_row");
    lv_obj_t* backup_row = lv_obj_find_by_name(menu_, "backup_dropdown_row");
    lv_obj_t* divider = lv_obj_find_by_name(menu_, "dropdown_divider");

    bool show_any_dropdown = false;

    // Configure tool mapping dropdown
    if (backend_) {
        auto tool_caps = backend_->get_tool_mapping_capabilities();
        if (tool_caps.supported) {
            populate_tool_dropdown();
            if (tool_row) {
                lv_obj_remove_flag(tool_row, LV_OBJ_FLAG_HIDDEN);
            }
            // Disable dropdown if not editable
            if (tool_dropdown_ && !tool_caps.editable) {
                lv_obj_add_state(tool_dropdown_, LV_STATE_DISABLED);
            }
            show_any_dropdown = true;
            spdlog::debug("[AmsContextMenu] Tool mapping enabled (editable={})",
                          tool_caps.editable);
        }
    }

    // Configure endless spool dropdown
    if (backend_) {
        auto es_caps = backend_->get_endless_spool_capabilities();
        if (es_caps.supported) {
            populate_backup_dropdown();
            if (backup_row) {
                lv_obj_remove_flag(backup_row, LV_OBJ_FLAG_HIDDEN);
            }
            // Disable dropdown if not editable
            if (backup_dropdown_ && !es_caps.editable) {
                lv_obj_add_state(backup_dropdown_, LV_STATE_DISABLED);
            }
            show_any_dropdown = true;
            spdlog::debug("[AmsContextMenu] Endless spool enabled (editable={})", es_caps.editable);
        }
    }

    // Show divider only if any dropdown is visible
    if (divider && show_any_dropdown) {
        lv_obj_remove_flag(divider, LV_OBJ_FLAG_HIDDEN);
    }
}

void AmsContextMenu::populate_tool_dropdown() {
    if (!tool_dropdown_) {
        return;
    }

    std::string options = build_tool_options();
    lv_dropdown_set_options(tool_dropdown_, options.c_str());

    int current_tool = get_current_tool_for_slot();
    // Map tool number to dropdown index: None=0, T0=1, T1=2, etc.
    int selected_index = (current_tool >= 0) ? (current_tool + 1) : 0;
    lv_dropdown_set_selected(tool_dropdown_, static_cast<uint32_t>(selected_index));

    spdlog::debug("[AmsContextMenu] Tool dropdown populated: slot {} maps to tool {}", slot_index_,
                  current_tool);
}

void AmsContextMenu::populate_backup_dropdown() {
    if (!backup_dropdown_) {
        return;
    }

    std::string options = build_backup_options();
    lv_dropdown_set_options(backup_dropdown_, options.c_str());

    int current_backup = get_current_backup_for_slot();
    // Map backup slot to dropdown index, accounting for skipped current slot
    // Dropdown: None=0, then all slots except current slot
    int selected_index = 0; // Default to None
    if (current_backup >= 0) {
        // Count how many slots appear before the backup slot in the dropdown
        // (which skips the current slot)
        selected_index = 1; // Start after "None"
        for (int i = 0; i < current_backup; ++i) {
            if (i != slot_index_) {
                selected_index++;
            }
        }
    }
    lv_dropdown_set_selected(backup_dropdown_, static_cast<uint32_t>(selected_index));

    spdlog::debug("[AmsContextMenu] Backup dropdown populated: slot {} backup is {}", slot_index_,
                  current_backup);
}

std::string AmsContextMenu::build_tool_options() const {
    std::string options = "None";
    // Add tool options T0, T1, T2... based on total slots
    for (int i = 0; i < total_slots_; ++i) {
        options += "\nT" + std::to_string(i);
    }
    return options;
}

std::string AmsContextMenu::build_backup_options() const {
    std::string options = "None";

    // Get current slot's material for compatibility checking
    std::string current_material;
    if (backend_ && slot_index_ >= 0) {
        current_material = backend_->get_slot_info(slot_index_).material;
    }

    // Add slot options Slot 1, Slot 2... based on total slots
    // Skip the current slot (can't be backup for itself)
    // Mark incompatible materials
    for (int i = 0; i < total_slots_; ++i) {
        if (i != slot_index_) {
            std::string slot_option = "\nSlot " + std::to_string(i + 1);

            // Check material compatibility if we have a current material
            if (backend_ && !current_material.empty()) {
                std::string other_material = backend_->get_slot_info(i).material;
                if (!other_material.empty() &&
                    !filament::are_materials_compatible(current_material, other_material)) {
                    slot_option += " (incompatible)";
                }
            }

            options += slot_option;
        }
    }
    return options;
}

int AmsContextMenu::get_current_tool_for_slot() const {
    if (!backend_) {
        return -1;
    }

    // Get tool mapping and find which tool maps to this slot
    auto mapping = backend_->get_tool_mapping();
    for (size_t tool = 0; tool < mapping.size(); ++tool) {
        if (mapping[tool] == slot_index_) {
            return static_cast<int>(tool);
        }
    }
    return -1; // No tool maps to this slot
}

int AmsContextMenu::get_current_backup_for_slot() const {
    if (!backend_) {
        return -1;
    }

    auto configs = backend_->get_endless_spool_config();
    for (const auto& config : configs) {
        if (config.slot_index == slot_index_) {
            return config.backup_slot;
        }
    }
    return -1; // No backup configured
}

} // namespace helix::ui
