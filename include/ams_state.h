// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "ams_backend.h"
#include "ams_types.h"
#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"

#include <memory>
#include <mutex>

// Forward declarations
class MoonrakerAPI;
class MoonrakerClient;

namespace helix {
class PrinterDiscovery;
}

/**
 * @file ams_state.h
 * @brief LVGL reactive state management for AMS UI binding
 *
 * Provides LVGL subjects that automatically update bound XML widgets
 * when AMS state changes. Bridges the AmsBackend to the UI layer.
 *
 * Usage:
 * 1. Call init_subjects() BEFORE creating XML components
 * 2. Call set_backend() to connect to an AMS backend
 * 3. Subjects auto-update when backend emits events
 *
 * Thread Safety:
 * All public methods are thread-safe. Subject updates are posted
 * to LVGL's thread via lv_async_call when called from background threads.
 */
class AmsState {
  public:
    /**
     * @brief Maximum number of slots supported for per-slot subjects
     *
     * Per-slot subjects (color, status) are allocated statically.
     * Systems with more slots will only have subjects for the first MAX_SLOTS.
     */
    static constexpr int MAX_SLOTS = 16;

    /// @name Dryer Constants
    /// @{
    static constexpr int DEFAULT_DRYER_TEMP_C = 55;        ///< Default dryer temp (PETG)
    static constexpr int DEFAULT_DRYER_DURATION_MIN = 240; ///< Default duration (4 hours)
    static constexpr int MIN_DRYER_TEMP_C = 35;            ///< Minimum dryer temperature
    static constexpr int MAX_DRYER_TEMP_C = 70;            ///< Maximum dryer temperature
    static constexpr int MIN_DRYER_DURATION_MIN = 30;      ///< Minimum duration (30 min)
    static constexpr int MAX_DRYER_DURATION_MIN = 720;     ///< Maximum duration (12 hours)
    static constexpr int DRYER_TEMP_STEP_C = 5;            ///< Temperature adjustment step
    static constexpr int DRYER_DURATION_STEP_MIN = 30;     ///< Duration adjustment step
    /// @}

    /**
     * @brief Get the singleton instance
     * @return Reference to the global AmsState instance
     */
    static AmsState& instance();

    /**
     * @brief Map AMS system/type name to logo image path
     *
     * Maps both generic firmware names (Happy Hare, AFC) and specific hardware
     * names (ERCF, Box Turtle, etc.) to their logo assets. Performs case-insensitive
     * matching and strips common suffixes like " (mock)".
     *
     * @param type_name System or type name (e.g., "AFC", "Happy Hare", "ERCF")
     * @return Logo asset path or nullptr if no matching logo
     */
    static const char* get_logo_path(const std::string& type_name);

    // Non-copyable, non-movable singleton
    AmsState(const AmsState&) = delete;
    AmsState& operator=(const AmsState&) = delete;

    /**
     * @brief Initialize all LVGL subjects
     *
     * MUST be called BEFORE creating XML components that bind to these subjects.
     * Can be called multiple times safely - subsequent calls are ignored.
     *
     * @param register_xml If true, registers subjects with LVGL XML system (default).
     *                     Set to false in tests to avoid XML observer creation.
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects for clean shutdown
     *
     * Must be called before lv_deinit() to prevent observer corruption.
     * Disconnects all observers from subjects.
     */
    void deinit_subjects();

    /**
     * @brief Initialize AMS backend from discovered hardware
     *
     * Called after Moonraker discovery completes. If the printer has an MMU system
     * (AFC/Box Turtle, Happy Hare, etc.), creates and starts the appropriate backend.
     * Does nothing if no MMU is detected or if already in mock mode.
     *
     * @param hardware Discovered printer hardware
     * @param api MoonrakerAPI instance for making API calls
     * @param client MoonrakerClient instance for WebSocket communication
     */
    void init_backend_from_hardware(const helix::PrinterDiscovery& hardware, MoonrakerAPI* api,
                                    MoonrakerClient* client);

    /**
     * @brief Set the AMS backend
     *
     * Connects to the backend and starts receiving state updates.
     * Automatically registers event callback to sync state.
     *
     * @param backend Backend instance (ownership transferred)
     */
    void set_backend(std::unique_ptr<AmsBackend> backend);

    /**
     * @brief Get the current backend
     * @return Pointer to backend (may be nullptr)
     */
    [[nodiscard]] AmsBackend* get_backend() const;

    /**
     * @brief Check if AMS is available
     * @return true if backend is set and AMS type is not NONE
     */
    [[nodiscard]] bool is_available() const;

    /**
     * @brief Set Moonraker API for Spoolman integration
     *
     * When set, AmsState will automatically call set_active_spool() when
     * a slot with a Spoolman ID becomes loaded. Pass nullptr to disable.
     *
     * @param api MoonrakerAPI instance (not owned)
     */
    void set_moonraker_api(MoonrakerAPI* api);

    // ========================================================================
    // System-level Subject Accessors
    // ========================================================================

    /**
     * @brief Get AMS type subject
     * @return Subject holding AmsType enum as int (0=none, 1=happy_hare, 2=afc)
     */
    lv_subject_t* get_ams_type_subject() {
        return &ams_type_;
    }

    /**
     * @brief Get current action subject
     * @return Subject holding AmsAction enum as int
     */
    lv_subject_t* get_ams_action_subject() {
        return &ams_action_;
    }

    /**
     * @brief Get action detail string subject
     * @return Subject holding current operation description
     */
    lv_subject_t* get_ams_action_detail_subject() {
        return &ams_action_detail_;
    }

    /**
     * @brief Get system name subject
     * @return Subject holding AMS system display name (e.g., "Happy Hare", "AFC")
     */
    lv_subject_t* get_ams_system_name_subject() {
        return &ams_system_name_;
    }

    /**
     * @brief Get current slot subject
     * @return Subject holding current slot index (-1 if none)
     */
    lv_subject_t* get_current_slot_subject() {
        return &current_slot_;
    }

    /**
     * @brief Get current tool subject
     * @return Subject holding current tool index (-1 if none)
     */
    lv_subject_t* get_current_tool_subject() {
        return &ams_current_tool_;
    }

    /**
     * @brief Get current tool text subject
     * @return Subject holding formatted tool string (e.g., "T0", "T1", or "---")
     */
    lv_subject_t* get_current_tool_text_subject() {
        return &ams_current_tool_text_;
    }

    /**
     * @brief Get filament loaded subject
     * @return Subject holding 0 (not loaded) or 1 (loaded)
     */
    lv_subject_t* get_filament_loaded_subject() {
        return &filament_loaded_;
    }

    /**
     * @brief Get bypass active subject
     *
     * Bypass mode allows external spool to feed directly to toolhead,
     * bypassing the MMU/hub system.
     *
     * @return Subject holding 0 (bypass inactive) or 1 (bypass active)
     */
    lv_subject_t* get_bypass_active_subject() {
        return &bypass_active_;
    }

    /**
     * @brief Get supports bypass subject
     * @return Subject holding 1 if backend supports bypass, 0 otherwise
     */
    lv_subject_t* get_supports_bypass_subject() {
        return &supports_bypass_;
    }

    /**
     * @brief Get slot count subject
     * @return Subject holding total number of slots
     */
    lv_subject_t* get_slot_count_subject() {
        return &ams_slot_count_;
    }

    /**
     * @brief Get slots version subject
     *
     * Incremented whenever slot data changes. UI can observe this
     * to know when to refresh slot displays.
     *
     * @return Subject holding version counter
     */
    lv_subject_t* get_slots_version_subject() {
        return &slots_version_;
    }

    // ========================================================================
    // Filament Path Visualization Subjects
    // ========================================================================

    /**
     * @brief Get path topology subject
     * @return Subject holding PathTopology enum as int (0=linear, 1=hub)
     */
    lv_subject_t* get_path_topology_subject() {
        return &path_topology_;
    }

    /**
     * @brief Get path active slot subject
     * @return Subject holding slot index whose path is being shown (-1=none)
     */
    lv_subject_t* get_path_active_slot_subject() {
        return &path_active_slot_;
    }

    /**
     * @brief Get path filament segment subject
     *
     * Indicates where the filament currently is along the path.
     *
     * @return Subject holding PathSegment enum as int
     */
    lv_subject_t* get_path_filament_segment_subject() {
        return &path_filament_segment_;
    }

    /**
     * @brief Get path error segment subject
     *
     * Indicates which segment has an error (for highlighting).
     *
     * @return Subject holding PathSegment enum as int (NONE if no error)
     */
    lv_subject_t* get_path_error_segment_subject() {
        return &path_error_segment_;
    }

    /**
     * @brief Get path animation progress subject
     *
     * Used for load/unload animations.
     *
     * @return Subject holding progress 0-100
     */
    lv_subject_t* get_path_anim_progress_subject() {
        return &path_anim_progress_;
    }

    // ========================================================================
    // Dryer Subject Accessors (for AMS systems with integrated drying)
    // ========================================================================

    /**
     * @brief Get dryer supported subject
     * @return Subject holding 1 if dryer is available, 0 otherwise
     */
    lv_subject_t* get_dryer_supported_subject() {
        return &dryer_supported_;
    }

    /**
     * @brief Get dryer active subject
     * @return Subject holding 1 if currently drying, 0 otherwise
     */
    lv_subject_t* get_dryer_active_subject() {
        return &dryer_active_;
    }

    /**
     * @brief Get dryer current temperature subject
     * @return Subject holding current temp in degrees C (integer)
     */
    lv_subject_t* get_dryer_current_temp_subject() {
        return &dryer_current_temp_;
    }

    /**
     * @brief Get dryer target temperature subject
     * @return Subject holding target temp in degrees C (integer, 0 = off)
     */
    lv_subject_t* get_dryer_target_temp_subject() {
        return &dryer_target_temp_;
    }

    /**
     * @brief Get dryer remaining minutes subject
     * @return Subject holding minutes remaining
     */
    lv_subject_t* get_dryer_remaining_min_subject() {
        return &dryer_remaining_min_;
    }

    /**
     * @brief Get dryer progress percentage subject
     * @return Subject holding 0-100 progress, or -1 if not drying
     */
    lv_subject_t* get_dryer_progress_pct_subject() {
        return &dryer_progress_pct_;
    }

    /**
     * @brief Get dryer current temperature text subject
     * @return Subject holding formatted temp string (e.g., "45C")
     */
    lv_subject_t* get_dryer_current_temp_text_subject() {
        return &dryer_current_temp_text_;
    }

    /**
     * @brief Get dryer target temperature text subject
     * @return Subject holding formatted temp string (e.g., "55C" or "---")
     */
    lv_subject_t* get_dryer_target_temp_text_subject() {
        return &dryer_target_temp_text_;
    }

    /**
     * @brief Get dryer time remaining text subject
     * @return Subject holding formatted time string (e.g., "2:30 left" or "")
     */
    lv_subject_t* get_dryer_time_text_subject() {
        return &dryer_time_text_;
    }

    /**
     * @brief Get dryer modal temperature text subject
     * @return Subject holding formatted temp string (e.g., "55°C")
     */
    lv_subject_t* get_dryer_modal_temp_text_subject() {
        return &dryer_modal_temp_text_;
    }

    /**
     * @brief Get dryer modal duration text subject
     * @return Subject holding formatted duration string (e.g., "4h", "4h 30m")
     */
    lv_subject_t* get_dryer_modal_duration_text_subject() {
        return &dryer_modal_duration_text_;
    }

    /**
     * @brief Get current modal target temperature
     * @return Temperature in degrees C
     */
    [[nodiscard]] int get_modal_target_temp() const {
        return modal_target_temp_c_;
    }

    /**
     * @brief Get current modal duration
     * @return Duration in minutes
     */
    [[nodiscard]] int get_modal_duration_min() const {
        return modal_duration_min_;
    }

    /**
     * @brief Adjust modal target temperature
     * @param delta_c Change in degrees (+5 or -5)
     */
    void adjust_modal_temp(int delta_c);

    /**
     * @brief Adjust modal duration
     * @param delta_min Change in minutes (+30 or -30)
     */
    void adjust_modal_duration(int delta_min);

    /**
     * @brief Set modal values from a preset
     * @param temp_c Target temperature
     * @param duration_min Duration in minutes
     */
    void set_modal_preset(int temp_c, int duration_min);

    /**
     * @brief Update modal text subjects from current values
     */
    void update_modal_text_subjects();

    // ========================================================================
    // Currently Loaded Display Subjects (for reactive "Currently Loaded" card)
    // ========================================================================

    /**
     * @brief Get current material text subject
     * @return Subject holding material/color text (e.g., "Red PLA", "External", "---")
     */
    lv_subject_t* get_current_material_text_subject() {
        return &current_material_text_;
    }

    /**
     * @brief Get current slot text subject
     * @return Subject holding slot text (e.g., "Slot 1", "Bypass", "None")
     */
    lv_subject_t* get_current_slot_text_subject() {
        return &current_slot_text_;
    }

    /**
     * @brief Get current weight text subject
     * @return Subject holding weight text (e.g., "450g", "")
     */
    lv_subject_t* get_current_weight_text_subject() {
        return &current_weight_text_;
    }

    /**
     * @brief Get current has weight subject
     * @return Subject holding 1 if weight data available, 0 otherwise (for visibility binding)
     */
    lv_subject_t* get_current_has_weight_subject() {
        return &current_has_weight_;
    }

    /**
     * @brief Get current color subject
     * @return Subject holding 0xRRGGBB color value for the swatch
     */
    lv_subject_t* get_current_color_subject() {
        return &current_color_;
    }

    // ========================================================================
    // Per-Slot Subject Accessors
    // ========================================================================

    /**
     * @brief Get slot color subject for a specific slot
     *
     * Holds 0xRRGGBB color value for UI display.
     *
     * @param slot_index Slot index (0 to MAX_SLOTS-1)
     * @return Subject pointer or nullptr if out of range
     */
    [[nodiscard]] lv_subject_t* get_slot_color_subject(int slot_index);

    /**
     * @brief Get slot status subject for a specific slot
     *
     * Holds SlotStatus enum as int.
     *
     * @param slot_index Slot index (0 to MAX_SLOTS-1)
     * @return Subject pointer or nullptr if out of range
     */
    [[nodiscard]] lv_subject_t* get_slot_status_subject(int slot_index);

    // ========================================================================
    // Direct State Update (called by backend event handler)
    // ========================================================================

    /**
     * @brief Update state from backend system info
     *
     * Called internally when backend emits STATE_CHANGED event.
     * Updates all subjects from the current backend state.
     */
    void sync_from_backend();

    /**
     * @brief Update a single slot's subjects
     *
     * Called when backend emits SLOT_CHANGED event.
     *
     * @param slot_index Slot that changed
     */
    void update_slot(int slot_index);

    /**
     * @brief Update dryer subjects from backend dryer info
     *
     * Called when backend reports dryer state changes.
     * Updates all dryer-related subjects for UI binding.
     */
    void sync_dryer_from_backend();

    /**
     * @brief Update "Currently Loaded" display subjects from backend
     *
     * Called when current slot changes to update the reactive UI.
     * Updates material text, slot text, weight info, and color subjects.
     */
    void sync_current_loaded_from_backend();

    /**
     * @brief Set action detail text directly (for UI-managed states)
     *
     * Used when UI is managing a process (like preheat) that the backend
     * doesn't know about. Updates the ams_action_detail_ subject.
     *
     * @param detail The status text to display (e.g., "Heating to 230°C...")
     */
    void set_action_detail(const std::string& detail);

    /**
     * @brief Set the current AMS action state directly
     *
     * Used by UI to indicate operation in progress (e.g., during UI-managed preheat
     * before backend starts). Triggers XML binding updates for action-dependent UI.
     *
     * @param action The action state to set
     */
    void set_action(AmsAction action);

    // ========================================================================
    // Spoolman Weight Polling
    // ========================================================================

    /**
     * @brief Refresh weights from Spoolman for all linked slots
     *
     * Queries Spoolman for updated weight info for each slot that has a
     * spoolman_id > 0. Updates remaining_weight_g and total_weight_g in
     * the backend's slot data.
     */
    void refresh_spoolman_weights();

    /**
     * @brief Start periodic Spoolman weight polling
     *
     * Uses a reference count pattern - multiple panels can call start_spoolman_polling()
     * and the timer is only created on the first call. The timer will refresh weights
     * every 30 seconds.
     */
    void start_spoolman_polling();

    /**
     * @brief Stop periodic Spoolman weight polling
     *
     * Decrements the reference count. The timer is only deleted when the count
     * reaches zero (all panels that started polling have stopped).
     */
    void stop_spoolman_polling();

  private:
    friend class AmsStateTestAccess;

    AmsState();
    ~AmsState();

    /**
     * @brief Handle backend event callback
     * @param event Event name
     * @param data Event data
     */
    void on_backend_event(const std::string& event, const std::string& data);

    /**
     * @brief Bump the slots version counter
     */
    void bump_slots_version();

    /**
     * @brief Probe for ValgACE via REST endpoint
     *
     * Makes an async REST call to /server/ace/info. If successful,
     * creates ValgACE backend via lv_async_call to maintain thread safety.
     *
     * @param api MoonrakerAPI instance for REST calls
     * @param client MoonrakerClient instance for the backend
     */
    void probe_valgace(MoonrakerAPI* api, MoonrakerClient* client);

    /**
     * @brief Create and start ValgACE backend
     *
     * Called on main thread after successful ValgACE probe.
     * Must be called from LVGL thread context.
     *
     * @param api MoonrakerAPI instance
     * @param client MoonrakerClient instance
     */
    void create_valgace_backend(MoonrakerAPI* api, MoonrakerClient* client);

    mutable std::recursive_mutex mutex_;
    std::unique_ptr<AmsBackend> backend_;
    bool initialized_ = false;

    // Moonraker API for Spoolman integration
    MoonrakerAPI* api_ = nullptr;
    int last_synced_spoolman_id_ = 0; ///< Track to avoid duplicate set_active_spool calls

    // Spoolman weight polling
    lv_timer_t* spoolman_poll_timer_ = nullptr;
    int spoolman_poll_refcount_ = 0;

    // Subject manager for automatic cleanup
    SubjectManager subjects_;

    // System-level subjects
    lv_subject_t ams_type_;
    lv_subject_t ams_action_;
    lv_subject_t current_slot_;
    lv_subject_t ams_current_tool_;
    lv_subject_t filament_loaded_;
    lv_subject_t bypass_active_;
    lv_subject_t supports_bypass_;
    lv_subject_t ams_slot_count_;
    lv_subject_t slots_version_;

    // String subjects (need buffers)
    lv_subject_t ams_action_detail_;
    char action_detail_buf_[64];
    lv_subject_t ams_system_name_;
    char system_name_buf_[32];
    lv_subject_t ams_current_tool_text_;
    char ams_current_tool_text_buf_[16]; // "T0" to "T15" or "---"

    // Filament path visualization subjects
    lv_subject_t path_topology_;
    lv_subject_t path_active_slot_;
    lv_subject_t path_filament_segment_;
    lv_subject_t path_error_segment_;
    lv_subject_t path_anim_progress_;

    // Dryer subjects (for AMS systems with integrated drying)
    lv_subject_t dryer_supported_;
    lv_subject_t dryer_active_;
    lv_subject_t dryer_current_temp_;
    lv_subject_t dryer_target_temp_;
    lv_subject_t dryer_remaining_min_;
    lv_subject_t dryer_progress_pct_;

    // Dryer text subjects (need buffers)
    lv_subject_t dryer_current_temp_text_;
    char dryer_current_temp_text_buf_[16];
    lv_subject_t dryer_target_temp_text_;
    char dryer_target_temp_text_buf_[16];
    lv_subject_t dryer_time_text_;
    char dryer_time_text_buf_[32];

    // Dryer modal editing subjects (user-adjustable values)
    lv_subject_t dryer_modal_temp_text_;
    char dryer_modal_temp_text_buf_[16];
    lv_subject_t dryer_modal_duration_text_;
    char dryer_modal_duration_text_buf_[16];
    int modal_target_temp_c_ = DEFAULT_DRYER_TEMP_C;      ///< Modal's target temp (default PETG)
    int modal_duration_min_ = DEFAULT_DRYER_DURATION_MIN; ///< Modal's duration (default)

    // Currently Loaded display subjects (reactive binding for "Currently Loaded" card)
    lv_subject_t current_material_text_;
    char current_material_text_buf_[48];
    lv_subject_t current_slot_text_;
    char current_slot_text_buf_[24];
    lv_subject_t current_weight_text_;
    char current_weight_text_buf_[16];
    lv_subject_t current_has_weight_;
    lv_subject_t current_color_;

    // Per-slot subjects (color and status)
    lv_subject_t slot_colors_[MAX_SLOTS];
    lv_subject_t slot_statuses_[MAX_SLOTS];

    // Observer for print state changes to auto-refresh Spoolman weights
    ObserverGuard print_state_observer_;
};
