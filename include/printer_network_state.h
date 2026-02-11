// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>

// Forward declare ConnectionState and KlippyState (defined in moonraker_client.h and
// printer_state.h)
enum class ConnectionState;
enum class KlippyState;

namespace helix {

/**
 * @brief Manages network and connection state subjects for Moonraker connectivity
 *
 * Tracks WebSocket connection state to Moonraker, network connectivity status,
 * and Klipper firmware state. Also maintains a derived nav_buttons_enabled
 * subject that combines connection and klippy state for UI gating.
 *
 * Extracted from PrinterState as part of god class decomposition.
 *
 * Subjects (5 total):
 * - printer_connection_state_ (int) - ConnectionState enum values
 * - printer_connection_message_ (string, 128-byte buffer) - status message
 * - network_status_ (int) - NetworkStatus enum values
 * - klippy_state_ (int) - KlippyState enum values
 * - nav_buttons_enabled_ (int, derived) - 1 when connected AND klippy ready
 *
 * Additional state:
 * - was_ever_connected_ (bool) - tracks if ever successfully connected this session
 *
 * @note The was_ever_connected_ flag persists across resets - it tracks session lifetime.
 */
class PrinterNetworkState {
  public:
    PrinterNetworkState();
    ~PrinterNetworkState() = default;

    // Non-copyable
    PrinterNetworkState(const PrinterNetworkState&) = delete;
    PrinterNetworkState& operator=(const PrinterNetworkState&) = delete;

    /**
     * @brief Initialize network state subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    // ========================================================================
    // Setters
    // ========================================================================

    /**
     * @brief Set printer connection state (synchronous, must be on UI thread)
     *
     * This is a synchronous setter intended to be called from within
     * helix::async::invoke() by PrinterState, which handles the async dispatch.
     *
     * @param state ConnectionState enum value (0-4)
     * @param message Status message ("Connecting...", "Ready", "Disconnected", etc.)
     */
    void set_printer_connection_state_internal(int state, const char* message);

    /**
     * @brief Set network connectivity status
     *
     * @param status NetworkStatus enum value (0=disconnected, 1=connecting, 2=connected)
     */
    void set_network_status(int status);

    /**
     * @brief Set Klipper firmware state (synchronous, must be on UI thread)
     *
     * This is a synchronous setter intended to be called from within
     * helix::async::invoke() by PrinterState, which handles the async dispatch.
     *
     * @param state KlippyState enum value
     */
    void set_klippy_state_internal(KlippyState state);

    // ========================================================================
    // Subject accessors
    // ========================================================================

    /// Printer connection state (0=disconnected, 1=connecting, 2=connected, 3=reconnecting,
    /// 4=failed)
    lv_subject_t* get_printer_connection_state_subject() {
        return &printer_connection_state_;
    }

    /// Status message string (128-byte buffer)
    lv_subject_t* get_printer_connection_message_subject() {
        return &printer_connection_message_;
    }

    /// Network connectivity (0=disconnected, 1=connecting, 2=connected)
    lv_subject_t* get_network_status_subject() {
        return &network_status_;
    }

    /// Klipper firmware state (0=ready, 1=startup, 2=shutdown, 3=error)
    lv_subject_t* get_klippy_state_subject() {
        return &klippy_state_;
    }

    /// Combined nav button enabled state (1 when connected AND klippy ready, else 0)
    lv_subject_t* get_nav_buttons_enabled_subject() {
        return &nav_buttons_enabled_;
    }

    // ========================================================================
    // Query methods
    // ========================================================================

    /**
     * @brief Check if printer has ever connected this session
     *
     * Returns true if we've successfully connected to Moonraker at least once.
     * Used to distinguish "never connected" (gray icon) from "disconnected after
     * being connected" (yellow warning icon).
     *
     * @return true if ever connected this session
     */
    bool was_ever_connected() const {
        return was_ever_connected_;
    }

    /**
     * @brief Update combined nav_buttons_enabled subject
     *
     * Recalculates nav_buttons_enabled based on connection and klippy state.
     * Called whenever printer_connection_state or klippy_state changes.
     * Public so PrinterState can call it when needed.
     */
    void update_nav_buttons_enabled();

  private:
    friend class PrinterNetworkStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Network state subjects
    lv_subject_t printer_connection_state_{};   // Integer: ConnectionState enum values
    lv_subject_t printer_connection_message_{}; // String buffer
    lv_subject_t network_status_{};             // Integer: NetworkStatus enum values
    lv_subject_t klippy_state_{};               // Integer: KlippyState enum values
    lv_subject_t nav_buttons_enabled_{};        // Derived: 1 when connected AND klippy ready

    // String buffer for connection message
    char printer_connection_message_buf_[128];

    // Track if we've ever successfully connected (for UI display)
    bool was_ever_connected_ = false;
};

} // namespace helix
