// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_network_state.cpp
 * @brief Network and connection state management extracted from PrinterState
 *
 * Manages WebSocket connection state, network connectivity, and Klipper firmware
 * state. Maintains a derived nav_buttons_enabled subject for UI gating.
 *
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_network_state.h"

#include "moonraker_client.h" // For ConnectionState enum
#include "printer_state.h"    // For KlippyState enum

#include <spdlog/spdlog.h>

#include <cstring>

namespace helix {

PrinterNetworkState::PrinterNetworkState() {
    // Initialize string buffer
    std::memset(printer_connection_message_buf_, 0, sizeof(printer_connection_message_buf_));
    std::strcpy(printer_connection_message_buf_, "Disconnected");
}

void PrinterNetworkState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterNetworkState] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[PrinterNetworkState] Initializing subjects (register_xml={})", register_xml);

    // Printer connection state subjects (Moonraker WebSocket)
    lv_subject_init_int(&printer_connection_state_, 0); // 0 = disconnected
    lv_subject_init_string(&printer_connection_message_, printer_connection_message_buf_, nullptr,
                           sizeof(printer_connection_message_buf_), "Disconnected");

    // Network connectivity subject (WiFi/Ethernet)
    // Default to connected for mock mode
    lv_subject_init_int(&network_status_, 2); // 0=DISCONNECTED, 1=CONNECTING, 2=CONNECTED

    // Klipper firmware state subject (default to READY)
    lv_subject_init_int(&klippy_state_, static_cast<int>(KlippyState::READY));

    // Combined nav button enabled subject (connected AND klippy ready)
    // Starts disabled (0) - will be updated when connection/klippy state changes
    lv_subject_init_int(&nav_buttons_enabled_, 0);

    // Register with SubjectManager for automatic cleanup
    subjects_.register_subject(&printer_connection_state_);
    subjects_.register_subject(&printer_connection_message_);
    subjects_.register_subject(&network_status_);
    subjects_.register_subject(&klippy_state_);
    subjects_.register_subject(&nav_buttons_enabled_);

    // Register with LVGL XML system for XML bindings
    if (register_xml) {
        spdlog::debug("[PrinterNetworkState] Registering subjects with XML system");
        lv_xml_register_subject(NULL, "printer_connection_state", &printer_connection_state_);
        lv_xml_register_subject(NULL, "printer_connection_message", &printer_connection_message_);
        lv_xml_register_subject(NULL, "network_status", &network_status_);
        lv_xml_register_subject(NULL, "klippy_state", &klippy_state_);
        lv_xml_register_subject(NULL, "nav_buttons_enabled", &nav_buttons_enabled_);
    } else {
        spdlog::debug("[PrinterNetworkState] Skipping XML registration (tests mode)");
    }

    subjects_initialized_ = true;
    spdlog::debug("[PrinterNetworkState] Subjects initialized successfully");
}

void PrinterNetworkState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterNetworkState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterNetworkState::reset_for_testing() {
    if (!subjects_initialized_) {
        spdlog::debug("[PrinterNetworkState] reset_for_testing: subjects not initialized, "
                      "nothing to reset");
        return;
    }

    spdlog::info(
        "[PrinterNetworkState] reset_for_testing: Deinitializing subjects to clear observers");

    // Use SubjectManager for automatic subject cleanup
    subjects_.deinit_all();
    subjects_initialized_ = false;

    // Note: was_ever_connected_ is NOT reset - it tracks session lifetime
}

void PrinterNetworkState::set_printer_connection_state_internal(int state, const char* message) {
    // Called from main thread via ui_async_call
    spdlog::info("[PrinterNetworkState] Printer connection state changed: {} - {}", state, message);

    // Track if we've ever successfully connected
    if (state == static_cast<int>(ConnectionState::CONNECTED) && !was_ever_connected_) {
        was_ever_connected_ = true;
        spdlog::debug(
            "[PrinterNetworkState] First successful connection - was_ever_connected_ = true");
    }

    spdlog::trace(
        "[PrinterNetworkState] Setting printer_connection_state_ subject (at {}) to value {}",
        (void*)&printer_connection_state_, state);
    lv_subject_set_int(&printer_connection_state_, state);
    spdlog::trace("[PrinterNetworkState] Subject value now: {}",
                  lv_subject_get_int(&printer_connection_state_));
    lv_subject_copy_string(&printer_connection_message_, message);
    update_nav_buttons_enabled();
    spdlog::trace("[PrinterNetworkState] Printer connection state update complete, observers "
                  "should be notified");
}

void PrinterNetworkState::set_network_status(int status) {
    spdlog::debug("[PrinterNetworkState] Network status changed: {}", status);
    lv_subject_set_int(&network_status_, status);
}

void PrinterNetworkState::set_klippy_state_internal(KlippyState state) {
    const char* state_names[] = {"READY", "STARTUP", "SHUTDOWN", "ERROR"};
    int state_int = static_cast<int>(state);
    spdlog::info("[PrinterNetworkState] Klippy state changed: {} ({})", state_names[state_int],
                 state_int);
    lv_subject_set_int(&klippy_state_, state_int);
    update_nav_buttons_enabled();
}

void PrinterNetworkState::update_nav_buttons_enabled() {
    // Compute combined state: enabled when connected AND klippy ready
    int connection = lv_subject_get_int(&printer_connection_state_);
    int klippy = lv_subject_get_int(&klippy_state_);
    bool connected = (connection == static_cast<int>(ConnectionState::CONNECTED));
    bool klippy_ready = (klippy == static_cast<int>(KlippyState::READY));
    int enabled = (connected && klippy_ready) ? 1 : 0;

    // Only update if changed to avoid unnecessary observer notifications
    if (lv_subject_get_int(&nav_buttons_enabled_) != enabled) {
        spdlog::debug(
            "[PrinterNetworkState] nav_buttons_enabled: {} (connected={}, klippy_ready={})",
            enabled, connected, klippy_ready);
        lv_subject_set_int(&nav_buttons_enabled_, enabled);
    }
}

} // namespace helix
