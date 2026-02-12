// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_spoolman_picker.h"

#include "ui_modal.h"
#include "ui_utils.h"

#include "moonraker_api.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
bool AmsSpoolmanPicker::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsSpoolmanPicker::AmsSpoolmanPicker() {
    spdlog::debug("[AmsSpoolmanPicker] Constructed");
}

AmsSpoolmanPicker::~AmsSpoolmanPicker() {
    hide();
    deinit_subjects();
    spdlog::trace("[AmsSpoolmanPicker] Destroyed");
}

AmsSpoolmanPicker::AmsSpoolmanPicker(AmsSpoolmanPicker&& other) noexcept
    : picker_(other.picker_), parent_(other.parent_), slot_index_(other.slot_index_),
      current_spool_id_(other.current_spool_id_), api_(other.api_),
      completion_callback_(std::move(other.completion_callback_)),
      cached_spools_(std::move(other.cached_spools_)),
      callback_guard_(std::move(other.callback_guard_)),
      slot_indicator_observer_(other.slot_indicator_observer_) {
    std::memcpy(slot_indicator_buf_, other.slot_indicator_buf_, sizeof(slot_indicator_buf_));

    // Subjects cannot be safely moved - force re-initialization
    // LVGL subjects contain internal pointers that become invalid after move
    subjects_initialized_ = false;

    // Clear moved-from object
    other.picker_ = nullptr;
    other.parent_ = nullptr;
    other.slot_index_ = -1;
    other.api_ = nullptr;
    other.subjects_initialized_ = false;
    other.slot_indicator_observer_ = nullptr;
}

AmsSpoolmanPicker& AmsSpoolmanPicker::operator=(AmsSpoolmanPicker&& other) noexcept {
    if (this != &other) {
        hide(); // Clean up current state

        picker_ = other.picker_;
        parent_ = other.parent_;
        slot_index_ = other.slot_index_;
        current_spool_id_ = other.current_spool_id_;
        api_ = other.api_;
        completion_callback_ = std::move(other.completion_callback_);
        cached_spools_ = std::move(other.cached_spools_);
        callback_guard_ = std::move(other.callback_guard_);
        slot_indicator_observer_ = other.slot_indicator_observer_;
        std::memcpy(slot_indicator_buf_, other.slot_indicator_buf_, sizeof(slot_indicator_buf_));

        // Subjects cannot be safely moved - force re-initialization
        subjects_initialized_ = false;

        other.picker_ = nullptr;
        other.parent_ = nullptr;
        other.slot_index_ = -1;
        other.api_ = nullptr;
        other.subjects_initialized_ = false;
        other.slot_indicator_observer_ = nullptr;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void AmsSpoolmanPicker::set_completion_callback(CompletionCallback callback) {
    completion_callback_ = std::move(callback);
}

bool AmsSpoolmanPicker::show_for_slot(lv_obj_t* parent, int slot_index, int current_spool_id,
                                      MoonrakerAPI* api) {
    // Hide any existing picker first
    hide();

    if (!parent) {
        spdlog::warn("[AmsSpoolmanPicker] Cannot show - no parent");
        return false;
    }

    // Register callbacks once (idempotent)
    register_callbacks();

    // Initialize subjects if needed
    init_subjects();

    // Store state
    parent_ = parent;
    slot_index_ = slot_index;
    current_spool_id_ = current_spool_id;
    api_ = api;

    // Create callback guard for async operations [L012]
    callback_guard_ = std::make_shared<bool>(true);

    // Create picker via Modal system (provides backdrop + stacking)
    picker_ = ui_modal_show("spoolman_picker_modal");
    if (!picker_) {
        spdlog::error("[AmsSpoolmanPicker] Failed to create picker from XML");
        return false;
    }

    // Store 'this' in picker's user_data for callback traversal
    lv_obj_set_user_data(picker_, this);

    // Update slot indicator text via subject
    snprintf(slot_indicator_buf_, sizeof(slot_indicator_buf_), "Assigning to Slot %d",
             slot_index + 1);
    lv_subject_copy_string(&slot_indicator_subject_, slot_indicator_buf_);

    // Bind slot indicator label to subject (save observer for cleanup)
    lv_obj_t* slot_indicator = lv_obj_find_by_name(picker_, "slot_indicator");
    if (slot_indicator) {
        slot_indicator_observer_ =
            lv_label_bind_text(slot_indicator, &slot_indicator_subject_, nullptr);
    }

    // Show/hide unlink button based on whether slot has current assignment
    if (current_spool_id > 0) {
        lv_obj_t* btn_unlink = lv_obj_find_by_name(picker_, "btn_unlink");
        if (btn_unlink) {
            lv_obj_remove_flag(btn_unlink, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Show loading state initially (0=LOADING, 1=EMPTY, 2=CONTENT)
    lv_subject_set_int(&picker_state_subject_, 0);

    // Populate the picker with spools from API
    populate_spools();

    spdlog::info("[AmsSpoolmanPicker] Shown for slot {}", slot_index);
    return true;
}

void AmsSpoolmanPicker::hide() {
    // Invalidate async callbacks first [L012]
    callback_guard_.reset();

    // Observer cleanup is handled by SubjectManager::deinit_all() in deinit_subjects()
    // which calls lv_subject_deinit() on each subject. We avoid manual lv_observer_remove()
    // to prevent potential crashes from stale observer pointers during shutdown.
    slot_indicator_observer_ = nullptr;

    if (picker_) {
        ui_modal_hide(picker_);
        picker_ = nullptr;
        slot_index_ = -1;
        current_spool_id_ = 0;
        cached_spools_.clear();
        spdlog::debug("[AmsSpoolmanPicker] Hidden");
    }
}

// ============================================================================
// Subject Management
// ============================================================================

void AmsSpoolmanPicker::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize slot indicator string subject (local binding only, not XML registered)
    slot_indicator_buf_[0] = '\0';
    lv_subject_init_string(&slot_indicator_subject_, slot_indicator_buf_, nullptr,
                           sizeof(slot_indicator_buf_), "Assigning to Slot 1");
    subjects_.register_subject(&slot_indicator_subject_);

    // Initialize picker state subject (0=LOADING, 1=EMPTY, 2=CONTENT)
    UI_MANAGED_SUBJECT_INT(picker_state_subject_, 0, "ams_picker_state", subjects_);

    subjects_initialized_ = true;
    spdlog::debug("[AmsSpoolmanPicker] Subjects initialized");
}

void AmsSpoolmanPicker::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    // SubjectManager handles all lv_subject_deinit() calls via RAII
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[AmsSpoolmanPicker] Subjects deinitialized");
}

// ============================================================================
// Internal Methods
// ============================================================================

void AmsSpoolmanPicker::populate_spools() {
    if (!picker_ || !api_) {
        // No API - show empty state
        lv_subject_set_int(&picker_state_subject_, 1);
        return;
    }

    // Use weak_ptr pattern for async callback safety [L012]
    std::weak_ptr<bool> weak_guard = callback_guard_;

    api_->get_spoolman_spools(
        [this, weak_guard](const std::vector<SpoolInfo>& spools) {
            // Check if picker still exists and subjects are valid
            if (weak_guard.expired() || !picker_ || !subjects_initialized_) {
                spdlog::trace("[AmsSpoolmanPicker] Callback ignored - picker destroyed or moved");
                return;
            }

            if (spools.empty()) {
                lv_subject_set_int(&picker_state_subject_, 1); // Empty state
                return;
            }

            // Show content state
            lv_subject_set_int(&picker_state_subject_, 2);

            // Cache spools for lookup on selection
            cached_spools_ = spools;

            // Find the spool list container
            lv_obj_t* spool_list = lv_obj_find_by_name(picker_, "spool_list");
            if (!spool_list) {
                spdlog::error("[AmsSpoolmanPicker] spool_list not found");
                return;
            }

            // Create a spool item for each spool
            for (const auto& spool : spools) {
                lv_obj_t* item =
                    static_cast<lv_obj_t*>(lv_xml_create(spool_list, "spool_item", nullptr));
                if (!item) {
                    continue;
                }

                // Store spool_id in user_data for click handler
                lv_obj_set_user_data(item,
                                     reinterpret_cast<void*>(static_cast<intptr_t>(spool.id)));

                // Update spool name (vendor + material)
                lv_obj_t* name_label = lv_obj_find_by_name(item, "spool_name");
                if (name_label) {
                    std::string name = spool.vendor.empty() ? spool.material
                                                            : (spool.vendor + " " + spool.material);
                    lv_label_set_text(name_label, name.c_str());
                }

                // Update color name
                lv_obj_t* color_label = lv_obj_find_by_name(item, "spool_color");
                if (color_label && !spool.color_name.empty()) {
                    lv_label_set_text(color_label, spool.color_name.c_str());
                }

                // Update weight
                lv_obj_t* weight_label = lv_obj_find_by_name(item, "spool_weight");
                if (weight_label && spool.remaining_weight_g > 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.0fg", spool.remaining_weight_g);
                    lv_label_set_text(weight_label, buf);
                }

                // Update color swatch
                lv_obj_t* swatch = lv_obj_find_by_name(item, "spool_swatch");
                if (swatch && !spool.color_hex.empty()) {
                    lv_color_t color = theme_manager_parse_hex_color(spool.color_hex.c_str());
                    lv_obj_set_style_bg_color(swatch, color, 0);
                    lv_obj_set_style_border_color(swatch, color, 0);
                }

                // Show checkmark if this is the currently assigned spool
                if (spool.id == current_spool_id_) {
                    lv_obj_t* check_icon = lv_obj_find_by_name(item, "selected_icon");
                    if (check_icon) {
                        lv_obj_remove_flag(check_icon, LV_OBJ_FLAG_HIDDEN);
                    }
                }
            }

            spdlog::info("[AmsSpoolmanPicker] Populated with {} spools", spools.size());
        },
        [this, weak_guard](const MoonrakerError& err) {
            // Check if picker still exists and subjects are valid
            if (weak_guard.expired() || !picker_ || !subjects_initialized_) {
                return;
            }

            spdlog::warn("[AmsSpoolmanPicker] Failed to fetch spools: {}", err.message);
            lv_subject_set_int(&picker_state_subject_, 1); // Empty/error state
        });
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsSpoolmanPicker::handle_close() {
    spdlog::debug("[AmsSpoolmanPicker] Close requested");

    if (completion_callback_) {
        PickerResult result;
        result.action = PickerAction::CANCELLED;
        result.slot_index = slot_index_;
        completion_callback_(result);
    }

    hide();
}

void AmsSpoolmanPicker::handle_unlink() {
    spdlog::info("[AmsSpoolmanPicker] Unlink requested for slot {}", slot_index_);

    if (completion_callback_) {
        PickerResult result;
        result.action = PickerAction::UNLINK;
        result.slot_index = slot_index_;
        completion_callback_(result);
    }

    hide();
}

void AmsSpoolmanPicker::handle_spool_selected(int spool_id) {
    spdlog::info("[AmsSpoolmanPicker] Spool {} selected for slot {}", spool_id, slot_index_);

    if (completion_callback_) {
        PickerResult result;
        result.action = PickerAction::ASSIGN;
        result.slot_index = slot_index_;
        result.spool_id = spool_id;

        // Look up full spool info from cache
        for (const auto& spool : cached_spools_) {
            if (spool.id == spool_id) {
                result.spool_info = spool;
                break;
            }
        }

        completion_callback_(result);
    }

    hide();
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsSpoolmanPicker::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "spoolman_picker_close_cb", on_close_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_picker_unlink_cb", on_unlink_cb);
    lv_xml_register_event_cb(nullptr, "spoolman_spool_item_clicked_cb", on_spool_item_cb);

    callbacks_registered_ = true;
    spdlog::debug("[AmsSpoolmanPicker] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

AmsSpoolmanPicker* AmsSpoolmanPicker::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Traverse parent chain to find picker root with user_data
    lv_obj_t* obj = target;
    while (obj) {
        void* user_data = lv_obj_get_user_data(obj);
        if (user_data) {
            return static_cast<AmsSpoolmanPicker*>(user_data);
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[AmsSpoolmanPicker] Could not find instance from event target");
    return nullptr;
}

void AmsSpoolmanPicker::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_close();
    }
}

void AmsSpoolmanPicker::on_unlink_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_unlink();
    }
}

void AmsSpoolmanPicker::on_spool_item_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self) {
        return;
    }

    // Get spool_id from the clicked item's user_data
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto spool_id = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
    self->handle_spool_selected(spool_id);
}

} // namespace helix::ui
