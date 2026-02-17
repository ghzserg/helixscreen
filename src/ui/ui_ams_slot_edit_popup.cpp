// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_slot_edit_popup.h"

#include "ui_toast_manager.h"
#include "ui_utils.h"

#include "ams_backend.h"
#include "filament_database.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
bool AmsSlotEditPopup::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsSlotEditPopup::AmsSlotEditPopup() {
    // Initialize subjects for button enabled states
    lv_subject_init_int(&can_load_subject_, 1);
    lv_subject_init_int(&can_unload_subject_, 0);
    lv_xml_register_subject(nullptr, "slot_edit_can_load", &can_load_subject_);
    lv_xml_register_subject(nullptr, "slot_edit_can_unload", &can_unload_subject_);
    subjects_initialized_ = true;
    spdlog::debug("[AmsSlotEditPopup] Constructed");
}

AmsSlotEditPopup::~AmsSlotEditPopup() {
    hide();

    // Clean up subjects
    if (subjects_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&can_load_subject_);
        lv_subject_deinit(&can_unload_subject_);
        subjects_initialized_ = false;
    }
    spdlog::trace("[AmsSlotEditPopup] Destroyed");
}

AmsSlotEditPopup::AmsSlotEditPopup(AmsSlotEditPopup&& other) noexcept
    : popup_(other.popup_), parent_(other.parent_), slot_index_(other.slot_index_),
      backend_(other.backend_), total_slots_(other.total_slots_),
      load_callback_(std::move(other.load_callback_)),
      unload_callback_(std::move(other.unload_callback_)),
      can_load_subject_(other.can_load_subject_), can_unload_subject_(other.can_unload_subject_),
      subjects_initialized_(other.subjects_initialized_), tool_dropdown_(other.tool_dropdown_),
      backup_dropdown_(other.backup_dropdown_) {
    other.popup_ = nullptr;
    other.parent_ = nullptr;
    other.slot_index_ = -1;
    other.backend_ = nullptr;
    other.total_slots_ = 0;
    other.subjects_initialized_ = false; // Prevent double-deinit
    other.tool_dropdown_ = nullptr;
    other.backup_dropdown_ = nullptr;
}

AmsSlotEditPopup& AmsSlotEditPopup::operator=(AmsSlotEditPopup&& other) noexcept {
    if (this != &other) {
        hide();

        // Deinit our subjects if we own them
        if (subjects_initialized_) {
            lv_subject_deinit(&can_load_subject_);
            lv_subject_deinit(&can_unload_subject_);
        }

        popup_ = other.popup_;
        parent_ = other.parent_;
        slot_index_ = other.slot_index_;
        backend_ = other.backend_;
        total_slots_ = other.total_slots_;
        load_callback_ = std::move(other.load_callback_);
        unload_callback_ = std::move(other.unload_callback_);
        can_load_subject_ = other.can_load_subject_;
        can_unload_subject_ = other.can_unload_subject_;
        subjects_initialized_ = other.subjects_initialized_;
        tool_dropdown_ = other.tool_dropdown_;
        backup_dropdown_ = other.backup_dropdown_;

        other.popup_ = nullptr;
        other.parent_ = nullptr;
        other.slot_index_ = -1;
        other.backend_ = nullptr;
        other.total_slots_ = 0;
        other.subjects_initialized_ = false; // Prevent double-deinit
        other.tool_dropdown_ = nullptr;
        other.backup_dropdown_ = nullptr;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void AmsSlotEditPopup::set_load_callback(LoadCallback callback) {
    load_callback_ = std::move(callback);
}

void AmsSlotEditPopup::set_unload_callback(UnloadCallback callback) {
    unload_callback_ = std::move(callback);
}

bool AmsSlotEditPopup::show_for_slot(lv_obj_t* parent, int slot_index, lv_obj_t* near_widget,
                                     AmsBackend* backend) {
    // Hide any existing popup first
    hide();

    if (!parent || !near_widget) {
        spdlog::warn("[AmsSlotEditPopup] Cannot show - missing parent or widget");
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

    // Determine button states based on slot status
    bool can_load = true;
    bool is_loaded = false;
    if (backend_) {
        SlotInfo slot_info = backend_->get_slot_info(slot_index);
        is_loaded = (slot_info.status == SlotStatus::LOADED);

        // Can't load if this slot is already loaded
        can_load = !is_loaded;

        // Check if system is busy
        AmsSystemInfo sys_info = backend_->get_system_info();
        if (sys_info.action != AmsAction::IDLE && sys_info.action != AmsAction::ERROR) {
            can_load = false;
        }
    }

    // Update subjects for button states
    lv_subject_set_int(&can_load_subject_, can_load ? 1 : 0);
    lv_subject_set_int(&can_unload_subject_, is_loaded ? 1 : 0);

    // Create popup from XML
    popup_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "ams_slot_edit_popup", nullptr));
    if (!popup_) {
        spdlog::error("[AmsSlotEditPopup] Failed to create popup from XML");
        return false;
    }

    // Store 'this' in popup's user_data for callback traversal
    lv_obj_set_user_data(popup_, this);

    // Update header text
    lv_obj_t* header = lv_obj_find_by_name(popup_, "slot_header");
    if (header) {
        std::string header_text = "Slot " + std::to_string(slot_index + 1);
        lv_label_set_text(header, header_text.c_str());
    }

    // Configure dropdowns based on backend capabilities
    configure_dropdowns();

    // Position the popup near the slot
    lv_obj_t* popup_card = lv_obj_find_by_name(popup_, "slot_edit_popup");
    if (popup_card) {
        position_popup_near_widget(popup_card, near_widget);
    }

    spdlog::debug("[AmsSlotEditPopup] Shown for slot {}", slot_index);
    return true;
}

void AmsSlotEditPopup::hide() {
    if (helix::ui::safe_delete(popup_)) {
        slot_index_ = -1;
        tool_dropdown_ = nullptr;
        backup_dropdown_ = nullptr;
        spdlog::debug("[AmsSlotEditPopup] hide()");
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsSlotEditPopup::handle_backdrop_clicked() {
    spdlog::debug("[AmsSlotEditPopup] Backdrop clicked - closing");
    hide();
}

void AmsSlotEditPopup::handle_close_clicked() {
    spdlog::debug("[AmsSlotEditPopup] Close clicked");
    hide();
}

void AmsSlotEditPopup::handle_load_clicked() {
    int slot = slot_index_;
    spdlog::info("[AmsSlotEditPopup] Load requested for slot {}", slot);

    hide();

    if (load_callback_) {
        load_callback_(slot);
    }
}

void AmsSlotEditPopup::handle_unload_clicked() {
    spdlog::info("[AmsSlotEditPopup] Unload requested");

    hide();

    if (unload_callback_) {
        unload_callback_();
    }
}

void AmsSlotEditPopup::handle_tool_changed() {
    if (!tool_dropdown_ || !backend_) {
        return;
    }

    int selected = static_cast<int>(lv_dropdown_get_selected(tool_dropdown_));
    // Option 0 = "None", options 1+ = T0, T1, T2...
    int tool_number = selected - 1; // -1 = None

    spdlog::info("[AmsSlotEditPopup] Tool mapping changed for slot {}: tool {}", slot_index_,
                 tool_number >= 0 ? tool_number : -1);

    if (tool_number >= 0) {
        // Check for duplicate mapping (another tool already using this slot)
        auto mapping = backend_->get_tool_mapping();
        for (size_t i = 0; i < mapping.size(); ++i) {
            if (static_cast<int>(i) != tool_number && mapping[i] == slot_index_) {
                spdlog::warn("[AmsSlotEditPopup] Tool {} will share slot {} with tool {}",
                             tool_number, slot_index_, i);
                std::string msg =
                    "T" + std::to_string(tool_number) + " shares slot with T" + std::to_string(i);
                ToastManager::instance().show(ToastSeverity::WARNING, msg.c_str());
                break;
            }
        }

        auto result = backend_->set_tool_mapping(tool_number, slot_index_);
        if (!result.success()) {
            spdlog::warn("[AmsSlotEditPopup] Failed to set tool mapping: {}", result.user_msg);
            ToastManager::instance().show(ToastSeverity::ERROR, result.user_msg.c_str());
        }
    }
}

void AmsSlotEditPopup::handle_backup_changed() {
    if (!backup_dropdown_ || !backend_) {
        return;
    }

    int selected = static_cast<int>(lv_dropdown_get_selected(backup_dropdown_));

    // Convert dropdown index back to actual slot index
    // Dropdown: None=0, then all slots except current slot
    int backup_slot = -1; // Default to None
    if (selected > 0) {
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

    // Validate material compatibility
    if (backup_slot >= 0 && slot_index_ >= 0) {
        std::string current_material = backend_->get_slot_info(slot_index_).material;
        std::string backup_material = backend_->get_slot_info(backup_slot).material;

        if (!current_material.empty() && !backup_material.empty() &&
            !filament::are_materials_compatible(current_material, backup_material)) {
            spdlog::warn("[AmsSlotEditPopup] Incompatible backup: {} cannot use {} as backup",
                         current_material, backup_material);

            std::string msg = "Incompatible: " + current_material + " / " + backup_material;
            ToastManager::instance().show(ToastSeverity::ERROR, msg.c_str());

            // Reset dropdown to "None"
            lv_dropdown_set_selected(backup_dropdown_, 0);
            return;
        }
    }

    spdlog::info("[AmsSlotEditPopup] Backup slot changed for slot {}: backup {}", slot_index_,
                 backup_slot >= 0 ? backup_slot : -1);

    auto result = backend_->set_endless_spool_backup(slot_index_, backup_slot);
    if (!result.success()) {
        spdlog::warn("[AmsSlotEditPopup] Failed to set endless spool backup: {}", result.user_msg);
        ToastManager::instance().show(ToastSeverity::ERROR, result.user_msg.c_str());
    }
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsSlotEditPopup::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "slot_edit_popup_backdrop_cb", on_backdrop_cb);
    lv_xml_register_event_cb(nullptr, "slot_edit_popup_close_cb", on_close_cb);
    lv_xml_register_event_cb(nullptr, "slot_edit_popup_load_cb", on_load_cb);
    lv_xml_register_event_cb(nullptr, "slot_edit_popup_unload_cb", on_unload_cb);
    lv_xml_register_event_cb(nullptr, "slot_edit_popup_tool_changed_cb", on_tool_changed_cb);
    lv_xml_register_event_cb(nullptr, "slot_edit_popup_backup_changed_cb", on_backup_changed_cb);

    callbacks_registered_ = true;
    spdlog::debug("[AmsSlotEditPopup] Callbacks registered");
}

// ============================================================================
// Static Callbacks
// ============================================================================

AmsSlotEditPopup* AmsSlotEditPopup::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Traverse parent chain to find popup root with user_data
    lv_obj_t* obj = target;
    while (obj) {
        void* user_data = lv_obj_get_user_data(obj);
        if (user_data) {
            return static_cast<AmsSlotEditPopup*>(user_data);
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[AmsSlotEditPopup] Could not find instance from event target");
    return nullptr;
}

void AmsSlotEditPopup::on_backdrop_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_backdrop_clicked();
    }
}

void AmsSlotEditPopup::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_close_clicked();
    }
}

void AmsSlotEditPopup::on_load_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_load_clicked();
    }
}

void AmsSlotEditPopup::on_unload_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_unload_clicked();
    }
}

void AmsSlotEditPopup::on_tool_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_tool_changed();
    }
}

void AmsSlotEditPopup::on_backup_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_backup_changed();
    }
}

// ============================================================================
// Dropdown Configuration
// ============================================================================

void AmsSlotEditPopup::configure_dropdowns() {
    if (!popup_) {
        return;
    }

    // Find dropdown widgets
    tool_dropdown_ = lv_obj_find_by_name(popup_, "tool_dropdown");
    backup_dropdown_ = lv_obj_find_by_name(popup_, "backup_dropdown");

    // Find row containers and divider
    lv_obj_t* tool_row = lv_obj_find_by_name(popup_, "tool_row");
    lv_obj_t* backup_row = lv_obj_find_by_name(popup_, "backup_row");
    lv_obj_t* divider = lv_obj_find_by_name(popup_, "button_divider");

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
            spdlog::debug("[AmsSlotEditPopup] Tool mapping enabled (editable={})",
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
            spdlog::debug("[AmsSlotEditPopup] Endless spool enabled (editable={})",
                          es_caps.editable);
        }
    }

    // Show divider only if any dropdown is visible
    if (divider && show_any_dropdown) {
        lv_obj_remove_flag(divider, LV_OBJ_FLAG_HIDDEN);
    }
}

void AmsSlotEditPopup::populate_tool_dropdown() {
    if (!tool_dropdown_) {
        return;
    }

    std::string options = build_tool_options();
    lv_dropdown_set_options(tool_dropdown_, options.c_str());

    int current_tool = get_current_tool_for_slot();
    // Map tool number to dropdown index: None=0, T0=1, T1=2, etc.
    int selected_index = (current_tool >= 0) ? (current_tool + 1) : 0;
    lv_dropdown_set_selected(tool_dropdown_, static_cast<uint32_t>(selected_index));

    spdlog::debug("[AmsSlotEditPopup] Tool dropdown populated: slot {} maps to tool {}",
                  slot_index_, current_tool);
}

void AmsSlotEditPopup::populate_backup_dropdown() {
    if (!backup_dropdown_) {
        return;
    }

    std::string options = build_backup_options();
    lv_dropdown_set_options(backup_dropdown_, options.c_str());

    int current_backup = get_current_backup_for_slot();
    // Map backup slot to dropdown index, accounting for skipped current slot
    // Dropdown is: "None", then all slots except slot_index_ in order
    int selected_index = 0; // Default to None
    if (current_backup >= 0 && current_backup != slot_index_) {
        // Calculate position in dropdown by counting slots before current_backup
        // that aren't the current slot
        selected_index = 1; // Index 0 is "None"
        for (int i = 0; i < total_slots_; ++i) {
            if (i == slot_index_) {
                continue; // Skip current slot (not in dropdown)
            }
            if (i == current_backup) {
                break; // Found our target
            }
            selected_index++;
        }
    }
    lv_dropdown_set_selected(backup_dropdown_, static_cast<uint32_t>(selected_index));

    spdlog::debug("[AmsSlotEditPopup] Backup dropdown populated: slot {} backup is {}", slot_index_,
                  current_backup);
}

std::string AmsSlotEditPopup::build_tool_options() const {
    std::string options = "None";
    for (int i = 0; i < total_slots_; ++i) {
        options += "\nT" + std::to_string(i);
    }
    return options;
}

std::string AmsSlotEditPopup::build_backup_options() const {
    std::string options = "None";

    // Get current slot's material for compatibility indication
    std::string current_material;
    if (backend_ && slot_index_ >= 0) {
        current_material = backend_->get_slot_info(slot_index_).material;
    }

    // Add slot options (skip current slot)
    for (int i = 0; i < total_slots_; ++i) {
        if (i != slot_index_) {
            std::string slot_option = "\nSlot " + std::to_string(i + 1);

            // Check material compatibility
            if (backend_ && !current_material.empty()) {
                std::string other_material = backend_->get_slot_info(i).material;
                if (!other_material.empty() &&
                    !filament::are_materials_compatible(current_material, other_material)) {
                    slot_option += " (!)";
                }
            }

            options += slot_option;
        }
    }
    return options;
}

int AmsSlotEditPopup::get_current_tool_for_slot() const {
    if (!backend_) {
        return -1;
    }

    auto mapping = backend_->get_tool_mapping();
    for (size_t tool = 0; tool < mapping.size(); ++tool) {
        if (mapping[tool] == slot_index_) {
            return static_cast<int>(tool);
        }
    }
    return -1;
}

int AmsSlotEditPopup::get_current_backup_for_slot() const {
    if (!backend_) {
        return -1;
    }

    auto configs = backend_->get_endless_spool_config();
    for (const auto& config : configs) {
        if (config.slot_index == slot_index_) {
            return config.backup_slot;
        }
    }
    return -1;
}

// ============================================================================
// Position Calculation
// ============================================================================

void AmsSlotEditPopup::position_popup_near_widget(lv_obj_t* popup_card, lv_obj_t* near_widget) {
    if (!popup_card || !near_widget || !parent_) {
        return;
    }

    // Update layout to get accurate dimensions
    lv_obj_update_layout(popup_card);

    // Get the position of the slot widget in screen coordinates
    lv_area_t slot_area;
    lv_obj_get_coords(near_widget, &slot_area);

    // Calculate positioning
    int32_t screen_width = lv_obj_get_width(parent_);
    int32_t screen_height = lv_obj_get_height(parent_);
    int32_t popup_width = lv_obj_get_width(popup_card);
    int32_t popup_height = lv_obj_get_height(popup_card);
    int32_t slot_center_y = (slot_area.y1 + slot_area.y2) / 2;

    // Position to the right of slot, or left if near screen edge
    int32_t popup_x = slot_area.x2 + 10;
    if (popup_x + popup_width > screen_width - 10) {
        popup_x = slot_area.x1 - popup_width - 10;
    }

    // Center vertically on the slot
    int32_t popup_y = slot_center_y - popup_height / 2;

    // Clamp to screen bounds
    if (popup_y < 10) {
        popup_y = 10;
    }
    if (popup_y + popup_height > screen_height - 10) {
        popup_y = screen_height - popup_height - 10;
    }

    // Ensure X is also in bounds
    if (popup_x < 10) {
        popup_x = 10;
    }

    lv_obj_set_pos(popup_card, popup_x, popup_y);

    spdlog::debug("[AmsSlotEditPopup] Positioned at ({}, {})", popup_x, popup_y);
}

} // namespace helix::ui
