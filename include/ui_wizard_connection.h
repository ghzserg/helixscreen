// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

/**
 * @file ui_wizard_connection.h
 * @brief Wizard Moonraker connection step - WebSocket configuration and testing
 *
 * Handles Moonraker WebSocket configuration during first-run wizard:
 * - IP address or hostname entry
 * - Port number configuration (default: 7125)
 * - Connection testing with async feedback
 * - Auto-discovery trigger on success
 * - Configuration persistence
 *
 * ## Class-Based Architecture (Phase 6)
 *
 * This step has been migrated from function-based to class-based design:
 * - Instance members instead of static globals
 * - Async WebSocket callbacks with captured instance reference
 * - Static trampolines for LVGL event callbacks
 * - Global singleton getter for backwards compatibility
 *
 * ## Subject Bindings (5 total):
 *
 * - connection_ip (string) - IP address or hostname
 * - connection_port (string) - Port number (default "7125")
 * - connection_status_icon (string) - FontAwesome icon (check/xmark/empty)
 * - connection_status_text (string) - Status message text
 * - connection_testing (int) - 0=idle, 1=testing (controls spinner)
 *
 * ## External Subject:
 *
 * - connection_test_passed (extern) - Controls wizard Next button globally
 *
 * Initialization Order (CRITICAL):
 *   1. Register XML component (wizard_connection.xml)
 *   2. init_subjects()
 *   3. register_callbacks()
 *   4. create(parent)
 */

/**
 * @class WizardConnectionStep
 * @brief Moonraker WebSocket connection step for the first-run wizard
 *
 * Allows user to enter Moonraker IP/port and test the connection.
 * On success, triggers hardware discovery for subsequent wizard steps.
 */
class WizardConnectionStep {
  public:
    /// Auto-probe state for localhost detection
    enum class AutoProbeState {
        IDLE,        ///< Never probed or probe complete
        IN_PROGRESS, ///< Currently probing localhost
        SUCCEEDED,   ///< Found printer at localhost
        FAILED       ///< No printer at localhost (silent failure)
    };

  public:
    WizardConnectionStep();
    ~WizardConnectionStep();

    // Non-copyable
    WizardConnectionStep(const WizardConnectionStep&) = delete;
    WizardConnectionStep& operator=(const WizardConnectionStep&) = delete;

    // Movable
    WizardConnectionStep(WizardConnectionStep&& other) noexcept;
    WizardConnectionStep& operator=(WizardConnectionStep&& other) noexcept;

    /**
     * @brief Initialize reactive subjects
     *
     * Creates and registers 5 subjects. Loads existing values from config.
     * Sets connection_test_passed to 0 (disabled) for this step.
     */
    void init_subjects();

    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks:
     * - on_test_connection_clicked
     * - on_ip_input_changed
     * - on_port_input_changed
     */
    void register_callbacks();

    /**
     * @brief Create the connection UI from XML
     *
     * @param parent Parent container (wizard_content)
     * @return Root object of the step, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent);

    /**
     * @brief Cleanup resources
     *
     * Cancels any ongoing connection test and resets UI references.
     */
    void cleanup();

    /**
     * @brief Get the configured Moonraker URL
     *
     * @param buffer Output buffer for the URL
     * @param size Size of the output buffer
     * @return true if URL was successfully constructed
     */
    bool get_url(char* buffer, size_t size) const;

    /**
     * @brief Check if connection has been successfully tested
     *
     * @return true if a successful connection test has been performed
     */
    bool is_validated() const;

    /**
     * @brief Get step name for logging
     */
    const char* get_name() const {
        return "Wizard Connection";
    }

    /**
     * @brief Check if this step has been cleaned up
     *
     * Thread-safe check for use in async callbacks. Returns true if cleanup()
     * has been called, meaning any pending async work should be abandoned.
     *
     * @return true if cleanup has been called
     */
    bool is_stale() const {
        return cleanup_called_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if a connection generation is still current
     *
     * Thread-safe check for use in async callbacks. Returns true if the
     * given generation matches the current generation, meaning the callback
     * is still relevant.
     *
     * @param generation The generation captured when the async operation started
     * @return true if the generation is still current
     */
    bool is_current_generation(uint64_t generation) const {
        return connection_generation_.load(std::memory_order_acquire) == generation;
    }

  private:
    // Screen instance
    lv_obj_t* screen_root_ = nullptr;

    // Subjects (5 total)
    lv_subject_t connection_ip_;
    lv_subject_t connection_port_;
    lv_subject_t connection_status_icon_;
    lv_subject_t connection_status_text_;
    lv_subject_t connection_testing_;

    // String buffers (must be persistent)
    char connection_ip_buffer_[128];
    char connection_port_buffer_[8];
    char connection_status_icon_buffer_[8];
    char connection_status_text_buffer_[256];

    // State tracking (main thread only)
    bool connection_validated_ = false;
    bool subjects_initialized_ = false;

    // Thread-safe state for async callback guards
    std::atomic<bool> cleanup_called_{false};          ///< Guards async callbacks after navigation
    std::atomic<uint64_t> connection_generation_{0};   ///< Invalidates stale callbacks

    // Auto-probe state for localhost detection (atomic for cross-thread access)
    std::atomic<AutoProbeState> auto_probe_state_{AutoProbeState::IDLE};
    bool auto_probe_attempted_ = false;  // Main thread only
    lv_timer_t* auto_probe_timer_ = nullptr;

    // Saved values for async callback - protected by mutex for thread-safe access
    mutable std::mutex saved_values_mutex_;
    std::string saved_ip_;    // Protected by saved_values_mutex_
    std::string saved_port_;  // Protected by saved_values_mutex_

    // Event handler implementations
    void handle_test_connection_clicked();
    void handle_ip_input_changed();
    void handle_port_input_changed();

    // Async callback handlers (called from WebSocket callbacks)
    void on_connection_success();
    void on_connection_failure();

    // Auto-probe methods for localhost detection
    bool should_auto_probe() const;
    void attempt_auto_probe();
    void on_auto_probe_success();
    void on_auto_probe_failure();

    // Static trampolines for LVGL callbacks
    static void on_test_connection_clicked_static(lv_event_t* e);
    static void on_ip_input_changed_static(lv_event_t* e);
    static void on_port_input_changed_static(lv_event_t* e);
    static void auto_probe_timer_cb(lv_timer_t* timer);
};

// ============================================================================
// Global Instance Access
// ============================================================================

/**
 * @brief Get the global WizardConnectionStep instance
 *
 * Creates the instance on first call. Used by wizard framework.
 */
WizardConnectionStep* get_wizard_connection_step();

/**
 * @brief Destroy the global WizardConnectionStep instance
 *
 * Call during application shutdown.
 */
void destroy_wizard_connection_step();
