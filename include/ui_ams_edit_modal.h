// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_color_picker.h"
#include "ui_modal.h"

#include "ams_types.h"
#include "spoolman_types.h"
#include "subject_managed_panel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class MoonrakerAPI;

namespace helix::ui {

/**
 * @file ui_ams_edit_modal.h
 * @brief Unified modal for editing AMS filament slot properties
 *
 * Supports two views:
 * - Form view: manual editing of vendor, material, color, remaining weight
 * - Picker view: Spoolman spool selection from a scrollable list
 *
 * Smart first-view logic shows the picker for empty slots when Spoolman
 * is available, and the form view otherwise. SpoolmanSlotSaver handles
 * syncing edits back to Spoolman on save.
 *
 * ## Usage:
 * @code
 * helix::ui::AmsEditModal modal;
 * modal.set_completion_callback([](const EditResult& result) {
 *     if (result.saved) {
 *         // Apply result.slot_info to backend
 *     }
 * });
 * modal.show_for_slot(parent, slot_index, slot_info, api);
 * @endcode
 */
class AmsEditModal : public Modal {
  public:
    /**
     * @brief Result returned when modal closes
     */
    struct EditResult {
        bool saved = false;  ///< True if user saved, false if cancelled
        int slot_index = -1; ///< Slot that was edited
        SlotInfo slot_info;  ///< Final slot info (valid if saved)
    };

    using CompletionCallback = std::function<void(const EditResult& result)>;

    AmsEditModal();
    ~AmsEditModal() override;

    // Non-copyable
    AmsEditModal(const AmsEditModal&) = delete;
    AmsEditModal& operator=(const AmsEditModal&) = delete;

    // Movable
    AmsEditModal(AmsEditModal&& other) noexcept;
    AmsEditModal& operator=(AmsEditModal&& other) noexcept;

    /**
     * @brief Show modal for editing a specific slot
     * @param parent Parent screen for the modal
     * @param slot_index Index of slot being edited (0-based)
     * @param initial_info Initial slot info to populate form
     * @param api MoonrakerAPI for Spoolman sync (may be nullptr)
     * @return true if modal was created successfully
     */
    bool show_for_slot(lv_obj_t* parent, int slot_index, const SlotInfo& initial_info,
                       MoonrakerAPI* api);

    /**
     * @brief Set callback for when editing completes
     * @param callback Function called with edit result
     */
    void set_completion_callback(CompletionCallback callback);

    // Modal interface
    [[nodiscard]] const char* get_name() const override {
        return "Edit Filament Modal";
    }
    [[nodiscard]] const char* component_name() const override {
        return "ams_edit_modal";
    }

  protected:
    void on_show() override;
    void on_hide() override;

  private:
    // === State ===
    int slot_index_ = -1;
    SlotInfo original_info_; ///< Original info for reset
    SlotInfo working_info_;  ///< Working copy being edited
    MoonrakerAPI* api_ = nullptr;
    CompletionCallback completion_callback_;
    int remaining_pre_edit_pct_ = 0; ///< Remaining % before edit mode

    // === Owned color picker ===
    std::unique_ptr<ColorPicker> color_picker_;

    // === Subjects for XML binding ===
    SubjectManager subjects_;
    lv_subject_t slot_indicator_subject_;
    lv_subject_t color_name_subject_;
    lv_subject_t temp_nozzle_subject_;
    lv_subject_t temp_bed_subject_;
    lv_subject_t remaining_pct_subject_;
    lv_subject_t remaining_mode_subject_; ///< 0=view, 1=edit
    lv_subject_t save_btn_text_subject_;  ///< "Save" or "Close"
    lv_subject_t view_mode_subject_;      ///< 0=form, 1=picker
    lv_subject_t picker_state_subject_;   ///< 0=loading, 1=empty, 2=content

    char slot_indicator_buf_[32] = {0};
    char color_name_buf_[32] = {0};
    char temp_nozzle_buf_[16] = {0};
    char temp_bed_buf_[16] = {0};
    char remaining_pct_buf_[16] = {0};
    char save_btn_text_buf_[16] = {0};
    bool subjects_initialized_ = false;

    // === Observer tracking for cleanup [L020] ===
    lv_observer_t* slot_indicator_observer_ = nullptr;
    lv_observer_t* color_name_observer_ = nullptr;
    lv_observer_t* temp_nozzle_observer_ = nullptr;
    lv_observer_t* temp_bed_observer_ = nullptr;
    lv_observer_t* remaining_pct_observer_ = nullptr;
    lv_observer_t* save_btn_text_observer_ = nullptr;

    // === Async callback guard [L012] ===
    std::shared_ptr<bool> callback_guard_;

    // === Dropdown data (materials from filament_database, vendors from Spoolman) ===
    std::string material_options_;           ///< Newline-separated material names for dropdown
    std::vector<std::string> material_list_; ///< Material names for index lookup
    std::string vendor_options_;             ///< Newline-separated vendor names for dropdown
    std::vector<std::string> vendor_list_;   ///< Vendor names for index lookup
    bool vendors_loaded_ = false;            ///< True once Spoolman vendors have been fetched

    // === Picker state (Spoolman spool selection) ===
    std::vector<SpoolInfo> cached_spools_; ///< Cached spools from Spoolman fetch

    // === Internal Methods ===
    void fetch_vendors_from_spoolman();
    void update_vendor_dropdown();
    void init_subjects();

    /**
     * @brief Deinitialize all subjects (call before destruction)
     *
     * Follows [L041] pattern for safe subject cleanup.
     */
    void deinit_subjects();
    void update_ui();
    void update_temp_display();
    bool is_dirty() const;
    void update_sync_button_state();
    void show_color_picker();

    // === View switching ===
    void switch_to_picker();                           ///< Show picker view, populate spools
    void switch_to_form();                             ///< Show form view
    void populate_picker();                            ///< Async spool fetch and list population
    void render_spool_list(const std::string& filter); ///< Render filtered spool items
    void handle_spool_selected(int spool_id);    ///< Auto-fill working_info_ from selected spool
    void handle_manual_entry();                  ///< Switch from picker to form
    void handle_change_spool();                  ///< Switch from form to picker
    void handle_unlink();                        ///< Clear spoolman_id from working slot
    void handle_picker_search(const char* text); ///< Filter picker list by search text
    void update_spoolman_button_state();         ///< Show/hide change/unlink buttons

    // === Save orchestration ===
    void fire_completion(bool saved); ///< Common exit path for save/cancel

    // === Event Handlers ===
    void handle_close();
    void handle_vendor_changed(int index);
    void handle_material_changed(int index);
    void handle_color_clicked();
    void handle_remaining_changed(int percent);
    void handle_remaining_edit();
    void handle_remaining_accept();
    void handle_remaining_cancel();
    void handle_reset();
    void handle_save();

    // === Static Callback Registration ===
    static void register_callbacks();
    static bool callbacks_registered_;

    // === Static Callbacks ===
    static void on_close_cb(lv_event_t* e);
    static void on_vendor_changed_cb(lv_event_t* e);
    static void on_material_changed_cb(lv_event_t* e);
    static void on_color_clicked_cb(lv_event_t* e);
    static void on_remaining_changed_cb(lv_event_t* e);
    static void on_remaining_edit_cb(lv_event_t* e);
    static void on_remaining_accept_cb(lv_event_t* e);
    static void on_remaining_cancel_cb(lv_event_t* e);
    static void on_reset_cb(lv_event_t* e);
    static void on_save_cb(lv_event_t* e);
    static void on_manual_entry_cb(lv_event_t* e);
    static void on_change_spool_cb(lv_event_t* e);
    static void on_unlink_cb(lv_event_t* e);
    static void on_picker_search_cb(lv_event_t* e);
    static void on_spool_item_cb(lv_event_t* e);

    /**
     * @brief Find AmsEditModal instance from event target
     * Uses static active instance (only one modal can be open at a time)
     */
    static AmsEditModal* get_instance_from_event(lv_event_t* e);

    /// Active instance pointer (set on show, cleared on hide)
    static AmsEditModal* s_active_instance_;
};

} // namespace helix::ui
