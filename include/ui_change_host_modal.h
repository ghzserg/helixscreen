// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

/**
 * @file ui_change_host_modal.h
 * @brief Modal dialog for changing the Moonraker host connection
 *
 * Allows users to enter a new IP/hostname and port, test the connection,
 * and save the new configuration. Reconnection is handled by the caller
 * via the completion callback.
 *
 * ## Usage:
 * @code
 * auto modal = std::make_unique<ChangeHostModal>();
 * modal->set_completion_callback([](bool changed) {
 *     if (changed) { // reconnect to new host }
 * });
 * modal->show_modal(lv_screen_active());
 * @endcode
 */
class ChangeHostModal : public Modal {
  public:
    using CompletionCallback = std::function<void(bool changed)>;

    ChangeHostModal();
    ~ChangeHostModal() override;

    // Non-copyable
    ChangeHostModal(const ChangeHostModal&) = delete;
    ChangeHostModal& operator=(const ChangeHostModal&) = delete;

    /**
     * @brief Show the change host modal
     * @param parent Parent screen for the modal
     * @return true if modal was created successfully
     */
    bool show_modal(lv_obj_t* parent);

    /**
     * @brief Set callback for when modal closes
     * @param callback Function called with true if host was changed, false if cancelled
     */
    void set_completion_callback(CompletionCallback callback);

    // Modal interface
    const char* get_name() const override {
        return "Change Host";
    }
    const char* component_name() const override {
        return "change_host_modal";
    }

  protected:
    void on_show() override;
    void on_hide() override;

  private:
    // === Subjects for XML binding ===
    SubjectManager subjects_;
    lv_subject_t host_ip_subject_;
    lv_subject_t host_port_subject_;
    lv_subject_t testing_subject_;
    lv_subject_t validated_subject_;

    char host_ip_buf_[256] = {0};
    char host_port_buf_[8] = {0};
    bool subjects_initialized_ = false;

    // === Stale callback protection ===
    std::atomic<uint64_t> test_generation_{0};
    std::mutex saved_values_mutex_;
    std::string saved_ip_;
    std::string saved_port_;

    // === Completion callback ===
    CompletionCallback completion_callback_;

    // === Input change observers (reset validation on edit) ===
    lv_observer_t* host_ip_observer_ = nullptr;
    lv_observer_t* host_port_observer_ = nullptr;

    // === Internal methods ===
    void init_subjects();
    void deinit_subjects();
    void handle_test_connection();
    void handle_save();
    void handle_cancel();
    void set_status(const char* icon_name, const char* color_token, const char* text);
    void on_test_success(lv_obj_t* guard_widget);
    void on_test_failure(lv_obj_t* guard_widget);
    static void on_input_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    // === Static callback registration ===
    static void register_callbacks();
    static bool callbacks_registered_;
    static ChangeHostModal* active_instance_;

    // === Static callbacks ===
    static void on_test_connection_cb(lv_event_t* e);
    static void on_save_cb(lv_event_t* e);
    static void on_cancel_cb(lv_event_t* e);
};
