// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "calibration_types.h" // For ScrewTiltResult
#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace helix {
class MoonrakerClient;
}
using helix::MoonrakerClient;
class MoonrakerAPI;

/**
 * @file ui_panel_screws_tilt.h
 * @brief Screws Tilt Adjust panel for manual bed leveling
 *
 * Interactive panel that guides the user through the SCREWS_TILT_CALCULATE
 * workflow. Shows a visual bed diagram with screw positions and adjustment
 * indicators, supporting an iterative probe-adjust-reprobe workflow.
 *
 * ## State Machine:
 * - IDLE: Shows instructions and Start button
 * - PROBING: Waiting for SCREWS_TILT_CALCULATE to complete
 * - RESULTS: Showing bed diagram and adjustment values
 * - LEVELED: All screws within tolerance
 * - ERROR: Something went wrong
 *
 * ## Usage:
 * ```cpp
 * ScrewsTiltPanel& panel = get_global_screws_tilt_panel();
 * panel.setup(lv_obj, parent_screen, moonraker_client, moonraker_api);
 * ui_nav_push_overlay(lv_obj);
 * ```
 */
class ScrewsTiltPanel : public OverlayBase {
  public:
    /**
     * @brief Panel state machine states
     */
    enum class State {
        IDLE,    ///< Ready to start, showing instructions
        PROBING, ///< SCREWS_TILT_CALCULATE running
        RESULTS, ///< Showing adjustment results
        LEVELED, ///< All screws within tolerance
        ERROR    ///< Error occurred
    };

    ScrewsTiltPanel() = default;
    ~ScrewsTiltPanel() override;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive XML bindings
     *
     * Must be called BEFORE XML creation (from register_callbacks).
     * Subject bindings are resolved at XML parse time.
     */
    void init_subjects() override;

    /**
     * @brief Deinitialize subjects to disconnect observers before destruction
     */
    void deinit_subjects();

    /**
     * @brief Create overlay UI from XML
     *
     * @param parent Parent screen widget to attach overlay to
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Get human-readable overlay name
     * @return "Screws Tilt Adjust"
     */
    const char* get_name() const override {
        return "Screws Tilt Adjust";
    }

    /**
     * @brief Called when overlay becomes visible
     *
     * Resets probe count and state to IDLE.
     */
    void on_activate() override;

    /**
     * @brief Called when overlay is being hidden
     *
     * Aborts probing if in progress, clears results.
     */
    void on_deactivate() override;

    /**
     * @brief Clean up resources for async-safe destruction
     */
    void cleanup() override;

    //
    // === Public API ===
    //

    /**
     * @brief Show overlay panel
     *
     * Pushes overlay onto navigation stack and registers with NavigationManager.
     * on_activate() will be called automatically after animation completes.
     */
    void show();

    /**
     * @brief Set Moonraker client and API references
     *
     * @param client Moonraker client for sending commands
     * @param api Moonraker API for calculate_screws_tilt()
     */
    void set_client(MoonrakerClient* client, MoonrakerAPI* api) {
        client_ = client;
        api_ = api;
    }

    /**
     * @brief Get current panel state
     * @return Current State
     */
    [[nodiscard]] State get_state() const {
        return state_;
    }

    /**
     * @brief Called when screws tilt calculation completes successfully
     * @param results Vector of screw adjustment results
     */
    void on_screws_tilt_results(const std::vector<ScrewTiltResult>& results);

    /**
     * @brief Called when screws tilt calculation fails
     * @param message Error message
     */
    void on_screws_tilt_error(const std::string& message);

    //
    // === Event Handlers (public for XML event_cb callbacks) ===
    //

    void handle_start_clicked();
    void handle_cancel_clicked();
    void handle_reprobe_clicked();
    void handle_done_clicked();
    void handle_retry_clicked();

  private:
    // State management
    State state_ = State::IDLE;
    void set_state(State new_state);

    // Async safety flag - survives after panel destruction [L012]
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    // Command helpers
    void start_probing();
    void cancel_probing();

    // UI setup (called by create())
    void setup_widgets();

    // UI update helpers
    void populate_results(const std::vector<ScrewTiltResult>& results);
    void clear_results();
    void update_screw_diagram();
    void create_screw_indicator(size_t index, const ScrewTiltResult& screw, bool is_worst = false);
    [[nodiscard]] lv_color_t get_adjustment_color(const ScrewTiltResult& screw,
                                                  bool is_worst_screw = false) const;
    [[nodiscard]] size_t find_worst_screw_index() const;

    /**
     * @brief Check if all screws are within tolerance
     * @param tolerance_minutes Maximum adjustment in minutes (default 5 = ~0.04mm)
     * @return true if all screws are level
     */
    [[nodiscard]] bool check_all_level(int tolerance_minutes = 5) const;

    // Widget references
    // Note: overlay_root_ inherited from OverlayBase
    lv_obj_t* parent_screen_ = nullptr;
    MoonrakerClient* client_ = nullptr;
    MoonrakerAPI* api_ = nullptr;

    // Results UI elements
    lv_obj_t* bed_diagram_container_ = nullptr;
    lv_obj_t* results_instruction_ = nullptr;

    // Dynamic screw indicators (bed diagram only - positions vary)
    std::vector<lv_obj_t*> screw_indicators_;

    // Screw row dot widgets (for color updates - XML handles text via subjects)
    std::array<lv_obj_t*, 4> screw_dots_ = {nullptr, nullptr, nullptr, nullptr};

    // Subjects for reactive screw list (4 slots max)
    static constexpr size_t MAX_SCREWS = 4;
    static constexpr size_t SCREW_NAME_BUF_SIZE = 32;
    static constexpr size_t SCREW_ADJ_BUF_SIZE = 24; // "Tighten ¼ turn" etc.

    std::array<lv_subject_t, MAX_SCREWS> screw_visible_subjects_;
    std::array<lv_subject_t, MAX_SCREWS> screw_name_subjects_;
    std::array<lv_subject_t, MAX_SCREWS> screw_adjustment_subjects_;

    // Fixed char arrays for string subjects (LVGL requires stable buffers)
    char screw_name_bufs_[MAX_SCREWS][SCREW_NAME_BUF_SIZE] = {};
    char screw_adj_bufs_[MAX_SCREWS][SCREW_ADJ_BUF_SIZE] = {};

    // Subjects for status labels
    static constexpr size_t PROBE_COUNT_BUF_SIZE = 64;
    static constexpr size_t ERROR_MSG_BUF_SIZE = 256;
    lv_subject_t probe_count_subject_;
    lv_subject_t error_message_subject_;
    char probe_count_buf_[PROBE_COUNT_BUF_SIZE] = {};
    char error_message_buf_[ERROR_MSG_BUF_SIZE] = {};

    // Screw data
    std::vector<ScrewTiltResult> screw_results_;

    // Tracking
    int probe_count_ = 0;

    // RAII subject manager for automatic cleanup
    SubjectManager subjects_;
};

// Global instance accessor
ScrewsTiltPanel& get_global_screws_tilt_panel();

// Destroy the global instance (call during shutdown)
void destroy_screws_tilt_panel();

/**
 * @brief Register XML event callbacks for screws tilt panel
 *
 * Call this once at startup before creating any screws_tilt_panel XML.
 * Registers callbacks for all button events (start, cancel, done, reprobe, retry).
 */
void ui_panel_screws_tilt_register_callbacks();

/**
 * @brief Initialize row click callback for opening from Advanced panel
 *
 * Must be called during app initialization before XML creation.
 * Registers "on_screws_tilt_row_clicked" callback.
 */
void init_screws_tilt_row_handler();
