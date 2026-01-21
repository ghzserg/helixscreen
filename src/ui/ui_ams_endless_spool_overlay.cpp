// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_endless_spool_overlay.cpp
 * @brief Implementation of AmsEndlessSpoolOverlay
 */

#include "ui_ams_endless_spool_overlay.h"

#include "ui_event_safety.h"
#include "ui_icon_codepoints.h"
#include "ui_nav_manager.h"
#include "ui_theme.h"

#include "ams_backend.h"
#include "ams_error.h"
#include "ams_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::ui {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<AmsEndlessSpoolOverlay> g_ams_endless_spool_overlay;

AmsEndlessSpoolOverlay& get_ams_endless_spool_overlay() {
    if (!g_ams_endless_spool_overlay) {
        g_ams_endless_spool_overlay = std::make_unique<AmsEndlessSpoolOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "AmsEndlessSpoolOverlay", []() { g_ams_endless_spool_overlay.reset(); });
    }
    return *g_ams_endless_spool_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

AmsEndlessSpoolOverlay::AmsEndlessSpoolOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

AmsEndlessSpoolOverlay::~AmsEndlessSpoolOverlay() {
    if (subjects_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&supported_subject_);
        lv_subject_deinit(&editable_subject_);
        lv_subject_deinit(&description_subject_);
        lv_subject_deinit(&editable_text_subject_);
    }
    spdlog::debug("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void AmsEndlessSpoolOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize supported subject (default: not supported)
    lv_subject_init_int(&supported_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_endless_spool_supported", &supported_subject_);

    // Initialize editable subject (default: not editable)
    lv_subject_init_int(&editable_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_endless_spool_editable", &editable_subject_);

    // Initialize description subject
    snprintf(description_buf_, sizeof(description_buf_), "Endless spool is not available.");
    lv_subject_init_string(&description_subject_, description_buf_, nullptr,
                           sizeof(description_buf_), description_buf_);
    lv_xml_register_subject(nullptr, "ams_endless_spool_description", &description_subject_);

    // Initialize editable hint text subject
    snprintf(editable_text_buf_, sizeof(editable_text_buf_), "");
    lv_subject_init_string(&editable_text_subject_, editable_text_buf_, nullptr,
                           sizeof(editable_text_buf_), editable_text_buf_);
    lv_xml_register_subject(nullptr, "ams_endless_spool_editable_text", &editable_text_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void AmsEndlessSpoolOverlay::register_callbacks() {
    // Register backup dropdown callback
    lv_xml_register_event_cb(nullptr, "on_ams_endless_spool_backup_changed", on_backup_changed);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

int AmsEndlessSpoolOverlay::get_slot_count() const {
    return total_slots_;
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* AmsEndlessSpoolOverlay::create(lv_obj_t* parent) {
    if (overlay_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    // Create from XML component
    overlay_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "ams_settings_endless_spool", nullptr));
    if (!overlay_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Find slot container for dynamic row creation
    slot_container_ = lv_obj_find_by_name(overlay_, "slot_container");

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_;
}

void AmsEndlessSpoolOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    // Ensure subjects and callbacks are initialized
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    // Lazy create overlay
    if (!overlay_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Query backend for capabilities and configuration
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        // No backend - show not supported
        lv_subject_set_int(&supported_subject_, 0);
        snprintf(description_buf_, sizeof(description_buf_), "No multi-filament system detected.");
        lv_subject_copy_string(&description_subject_, description_buf_);
        clear_slot_rows();
    } else {
        // Get capabilities
        auto capabilities = backend->get_endless_spool_capabilities();

        lv_subject_set_int(&supported_subject_, capabilities.supported ? 1 : 0);
        lv_subject_set_int(&editable_subject_, capabilities.editable ? 1 : 0);

        // Update description
        if (!capabilities.supported) {
            snprintf(description_buf_, sizeof(description_buf_),
                     "Endless spool is not supported by this backend.");
        } else if (!capabilities.description.empty()) {
            snprintf(description_buf_, sizeof(description_buf_), "%s",
                     capabilities.description.c_str());
        } else {
            snprintf(description_buf_, sizeof(description_buf_),
                     "Automatic backup slot switching when filament runs out.");
        }
        lv_subject_copy_string(&description_subject_, description_buf_);

        // Update editable hint
        if (capabilities.supported) {
            if (capabilities.editable) {
                snprintf(editable_text_buf_, sizeof(editable_text_buf_),
                         "Tap a slot to change its backup.");
            } else {
                snprintf(editable_text_buf_, sizeof(editable_text_buf_),
                         "Configuration is read-only (edit via config file).");
            }
            lv_subject_copy_string(&editable_text_subject_, editable_text_buf_);
        }

        // Get configuration and update slot rows
        if (capabilities.supported) {
            auto system_info = backend->get_system_info();
            total_slots_ = system_info.total_slots;
            update_slot_rows();
        } else {
            clear_slot_rows();
        }
    }

    // Push onto navigation stack
    ui_nav_push_overlay(overlay_);
}

void AmsEndlessSpoolOverlay::refresh() {
    if (!overlay_) {
        return;
    }

    // Re-query backend and update UI
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        auto capabilities = backend->get_endless_spool_capabilities();
        if (capabilities.supported) {
            update_slot_rows();
        }
    }
}

// ============================================================================
// SLOT ROW MANAGEMENT
// ============================================================================

void AmsEndlessSpoolOverlay::clear_slot_rows() {
    if (!slot_container_) {
        return;
    }

    // Clear tracked dropdown widgets
    dropdown_widgets_.clear();

    // Delete all children of slot container
    lv_obj_clean(slot_container_);
}

void AmsEndlessSpoolOverlay::update_slot_rows() {
    if (!slot_container_) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        clear_slot_rows();
        return;
    }

    auto capabilities = backend->get_endless_spool_capabilities();
    auto configs = backend->get_endless_spool_config();

    // Clear existing rows
    clear_slot_rows();

    // Create a row for each slot
    for (const auto& config : configs) {
        if (config.slot_index >= 0 && config.slot_index < total_slots_) {
            create_slot_row(slot_container_, config.slot_index, config.backup_slot, total_slots_,
                            capabilities.editable);
        }
    }

    spdlog::debug("[{}] Created {} slot rows", get_name(), configs.size());
}

lv_obj_t* AmsEndlessSpoolOverlay::create_slot_row(lv_obj_t* parent, int slot_index, int backup_slot,
                                                  int total_slots, bool editable) {
    // Create card container for this slot
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, ui_theme_get_color("card_bg"), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, ui_theme_get_spacing("border_radius"), 0);
    lv_obj_set_style_pad_all(card, ui_theme_get_spacing("space_md"), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: Slot label
    lv_obj_t* left_container = lv_obj_create(card);
    lv_obj_set_width(left_container, LV_SIZE_CONTENT);
    lv_obj_set_height(left_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_container, 0, 0);
    lv_obj_set_style_pad_all(left_container, 0, 0);
    lv_obj_set_flex_flow(left_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(left_container, ui_theme_get_spacing("space_sm"), 0);
    lv_obj_remove_flag(left_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(left_container, 1);

    // Slot label
    lv_obj_t* slot_label = lv_label_create(left_container);
    char slot_text[32];
    snprintf(slot_text, sizeof(slot_text), "Slot %d", slot_index);
    lv_label_set_text(slot_label, slot_text);
    lv_obj_set_style_text_color(slot_label, ui_theme_get_color("text_primary"), 0);

    // Arrow indicator
    lv_obj_t* arrow_label = lv_label_create(left_container);
    lv_label_set_text(arrow_label, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow_label, ui_theme_get_color("text_secondary"), 0);

    // Right side: Backup indicator or dropdown
    if (editable) {
        // Create dropdown for backup selection
        lv_obj_t* dropdown = lv_dropdown_create(card);
        lv_obj_set_width(dropdown, 100);
        lv_obj_set_height(dropdown, 36);
        lv_obj_set_style_pad_left(dropdown, ui_theme_get_spacing("space_sm"), 0);
        lv_obj_set_style_pad_right(dropdown, ui_theme_get_spacing("space_sm"), 0);

        // Build options (None, Slot 0, Slot 1, etc. - excluding current slot)
        std::string options = build_dropdown_options(slot_index, total_slots);
        lv_dropdown_set_options(dropdown, options.c_str());

        // Set current selection
        int dropdown_idx = backup_slot_to_dropdown_index(backup_slot, slot_index, total_slots);
        lv_dropdown_set_selected(dropdown, static_cast<uint32_t>(dropdown_idx));

        // Store slot_index in user_data for callback
        lv_obj_set_user_data(dropdown, reinterpret_cast<void*>(static_cast<intptr_t>(slot_index)));

        // Add event callback
        lv_obj_add_event_cb(dropdown, on_backup_changed, LV_EVENT_VALUE_CHANGED, nullptr);

        // Track for later cleanup
        dropdown_widgets_.push_back(dropdown);
    } else {
        // Read-only: Show backup slot as label with lock icon
        lv_obj_t* right_container = lv_obj_create(card);
        lv_obj_set_width(right_container, LV_SIZE_CONTENT);
        lv_obj_set_height(right_container, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(right_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right_container, 0, 0);
        lv_obj_set_style_pad_all(right_container, 0, 0);
        lv_obj_set_flex_flow(right_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(right_container, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(right_container, ui_theme_get_spacing("space_xs"), 0);
        lv_obj_remove_flag(right_container, LV_OBJ_FLAG_SCROLLABLE);

        // Backup slot label
        lv_obj_t* backup_label = lv_label_create(right_container);
        char backup_text[32];
        if (backup_slot < 0) {
            snprintf(backup_text, sizeof(backup_text), "None");
        } else {
            snprintf(backup_text, sizeof(backup_text), "Slot %d", backup_slot);
        }
        lv_label_set_text(backup_label, backup_text);
        lv_obj_set_style_text_color(backup_label, ui_theme_get_color("text_secondary"), 0);

        // Lock icon to indicate read-only
        lv_obj_t* lock_label = lv_label_create(right_container);
        lv_label_set_text(lock_label, ui_icon::lookup_codepoint("lock"));
        lv_obj_set_style_text_color(lock_label, ui_theme_get_color("text_tertiary"), 0);
    }

    return card;
}

std::string AmsEndlessSpoolOverlay::build_dropdown_options(int slot_index, int total_slots) {
    std::string options = "None";
    for (int i = 0; i < total_slots; i++) {
        if (i != slot_index) {
            options += "\nSlot " + std::to_string(i);
        }
    }
    return options;
}

int AmsEndlessSpoolOverlay::backup_slot_to_dropdown_index(int backup_slot, int slot_index,
                                                          int total_slots) {
    if (backup_slot < 0 || backup_slot >= total_slots) {
        return 0; // "None" is first option (or invalid slot)
    }

    // Calculate index: skip "None" (index 0), and account for skipped source slot
    // Dropdown order is: "None", then all slots except slot_index
    int idx = 1; // Start after "None"
    for (int slot = 0; slot < backup_slot; slot++) {
        if (slot != slot_index) {
            idx++;
        }
    }
    return idx;
}

int AmsEndlessSpoolOverlay::dropdown_index_to_backup_slot(int dropdown_index, int slot_index,
                                                          int total_slots) {
    if (dropdown_index == 0) {
        return -1; // "None" selected
    }

    // Walk through slots, skipping source slot, until we've counted dropdown_index entries
    int count = 0;
    for (int slot = 0; slot < total_slots; slot++) {
        if (slot == slot_index) {
            continue; // Skip source slot
        }
        count++;
        if (count == dropdown_index) {
            return slot;
        }
    }
    return -1; // Invalid index
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void AmsEndlessSpoolOverlay::on_backup_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsEndlessSpoolOverlay] on_backup_changed");

    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!dropdown || !lv_obj_is_valid(dropdown)) {
        spdlog::warn("[AmsEndlessSpoolOverlay] Stale callback - dropdown no longer valid");
    } else {
        // Get slot_index from user_data
        int slot_index =
            static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(dropdown)));
        int selected = static_cast<int>(lv_dropdown_get_selected(dropdown));

        auto& overlay = get_ams_endless_spool_overlay();
        int total_slots = overlay.get_slot_count();
        int backup_slot = overlay.dropdown_index_to_backup_slot(selected, slot_index, total_slots);

        spdlog::info("[AmsEndlessSpoolOverlay] Slot {} backup changed to: {}", slot_index,
                     backup_slot);

        // Apply change via backend
        AmsBackend* backend = AmsState::instance().get_backend();
        if (backend) {
            AmsError result = backend->set_endless_spool_backup(slot_index, backup_slot);
            if (!result.success()) {
                spdlog::error("[AmsEndlessSpoolOverlay] Failed to set backup: {}",
                              result.technical_msg);
                // TODO: Show error toast to user
            }
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
