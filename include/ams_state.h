// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_backend.h"
#include "ams_types.h"
#include "lvgl/lvgl.h"

#include <memory>
#include <mutex>

// Forward declarations
class PrinterCapabilities;
class MoonrakerAPI;
class MoonrakerClient;

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
     * @brief Maximum number of gates supported for per-gate subjects
     *
     * Per-gate subjects (color, status) are allocated statically.
     * Systems with more gates will only have subjects for the first MAX_GATES.
     */
    static constexpr int MAX_GATES = 16;

    /**
     * @brief Get the singleton instance
     * @return Reference to the global AmsState instance
     */
    static AmsState& instance();

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
     * @brief Reset initialization state for testing
     *
     * FOR TESTING ONLY. Clears the initialization flag so init_subjects()
     * can be called again after lv_init() creates a new LVGL context.
     */
    void reset_for_testing();

    /**
     * @brief Initialize AMS backend from detected printer capabilities
     *
     * Called after Moonraker discovery completes. If the printer has an MMU system
     * (AFC/Box Turtle, Happy Hare, etc.), creates and starts the appropriate backend.
     * Does nothing if no MMU is detected or if already in mock mode.
     *
     * @param caps Detected printer capabilities
     * @param api MoonrakerAPI instance for making API calls
     * @param client MoonrakerClient instance for WebSocket communication
     */
    void init_backend_from_capabilities(const PrinterCapabilities& caps, MoonrakerAPI* api,
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
     * @brief Get current gate subject
     * @return Subject holding current gate index (-1 if none)
     */
    lv_subject_t* get_current_gate_subject() {
        return &current_gate_;
    }

    /**
     * @brief Get current tool subject
     * @return Subject holding current tool index (-1 if none)
     */
    lv_subject_t* get_current_tool_subject() {
        return &current_tool_;
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
     * @brief Get gate count subject
     * @return Subject holding total number of gates
     */
    lv_subject_t* get_gate_count_subject() {
        return &gate_count_;
    }

    /**
     * @brief Get gates version subject
     *
     * Incremented whenever gate data changes. UI can observe this
     * to know when to refresh gate displays.
     *
     * @return Subject holding version counter
     */
    lv_subject_t* get_gates_version_subject() {
        return &gates_version_;
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
     * @brief Get path active gate subject
     * @return Subject holding gate index whose path is being shown (-1=none)
     */
    lv_subject_t* get_path_active_gate_subject() {
        return &path_active_gate_;
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
    // Per-Gate Subject Accessors
    // ========================================================================

    /**
     * @brief Get gate color subject for a specific gate
     *
     * Holds 0xRRGGBB color value for UI display.
     *
     * @param gate_index Gate index (0 to MAX_GATES-1)
     * @return Subject pointer or nullptr if out of range
     */
    [[nodiscard]] lv_subject_t* get_gate_color_subject(int gate_index);

    /**
     * @brief Get gate status subject for a specific gate
     *
     * Holds GateStatus enum as int.
     *
     * @param gate_index Gate index (0 to MAX_GATES-1)
     * @return Subject pointer or nullptr if out of range
     */
    [[nodiscard]] lv_subject_t* get_gate_status_subject(int gate_index);

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
     * @brief Update a single gate's subjects
     *
     * Called when backend emits GATE_CHANGED event.
     *
     * @param gate_index Gate that changed
     */
    void update_gate(int gate_index);

  private:
    AmsState();
    ~AmsState();

    /**
     * @brief Handle backend event callback
     * @param event Event name
     * @param data Event data
     */
    void on_backend_event(const std::string& event, const std::string& data);

    /**
     * @brief Bump the gates version counter
     */
    void bump_gates_version();

    mutable std::recursive_mutex mutex_;
    std::unique_ptr<AmsBackend> backend_;
    bool initialized_ = false;

    // System-level subjects
    lv_subject_t ams_type_;
    lv_subject_t ams_action_;
    lv_subject_t current_gate_;
    lv_subject_t current_tool_;
    lv_subject_t filament_loaded_;
    lv_subject_t bypass_active_;
    lv_subject_t gate_count_;
    lv_subject_t gates_version_;

    // String subject for action detail (needs buffer)
    lv_subject_t ams_action_detail_;
    char action_detail_buf_[64];

    // Filament path visualization subjects
    lv_subject_t path_topology_;
    lv_subject_t path_active_gate_;
    lv_subject_t path_filament_segment_;
    lv_subject_t path_error_segment_;
    lv_subject_t path_anim_progress_;

    // Per-gate subjects (color and status)
    lv_subject_t gate_colors_[MAX_GATES];
    lv_subject_t gate_statuses_[MAX_GATES];
};
