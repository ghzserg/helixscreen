// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_change_host_modal.h"

#include "ui_emergency_stop.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "theme_manager.h"
#include "utils/network_validation.h"

#include <spdlog/spdlog.h>

// Static member initialization
bool ChangeHostModal::callbacks_registered_ = false;
ChangeHostModal* ChangeHostModal::active_instance_ = nullptr;

// ============================================================================
// Construction / Destruction
// ============================================================================

ChangeHostModal::ChangeHostModal() {
    spdlog::debug("[ChangeHostModal] Constructed");
}

ChangeHostModal::~ChangeHostModal() {
    deinit_subjects();
    spdlog::trace("[ChangeHostModal] Destroyed");
}

// ============================================================================
// Public API
// ============================================================================

void ChangeHostModal::set_completion_callback(CompletionCallback callback) {
    completion_callback_ = std::move(callback);
}

bool ChangeHostModal::show_modal(lv_obj_t* parent) {
    register_callbacks();
    init_subjects();

    // Populate with current config values
    Config* config = Config::get_instance();
    if (config) {
        std::string host = config->get<std::string>(config->df() + "moonraker_host", "");
        int port = config->get<int>(config->df() + "moonraker_port", 7125);

        lv_subject_copy_string(&host_ip_subject_, host.c_str());
        lv_subject_copy_string(&host_port_subject_, std::to_string(port).c_str());
    }

    bool result = show(parent);
    if (result && dialog()) {
        // Reset state
        lv_subject_set_int(&testing_subject_, 0);
        lv_subject_set_int(&validated_subject_, 0);

        // Set active instance for static callback dispatch
        active_instance_ = this;

        // Register keyboards for text inputs
        lv_obj_t* host_input = lv_obj_find_by_name(dialog(), "host_input");
        if (host_input) {
            ui_modal_register_keyboard(dialog(), host_input);
        }

        lv_obj_t* port_input = lv_obj_find_by_name(dialog(), "port_input");
        if (port_input) {
            ui_modal_register_keyboard(dialog(), port_input);
        }

        // Observe text input changes to invalidate validation when user edits
        host_ip_observer_ =
            lv_subject_add_observer_obj(&host_ip_subject_, on_input_changed_cb, dialog(), nullptr);
        host_port_observer_ = lv_subject_add_observer_obj(&host_port_subject_, on_input_changed_cb,
                                                          dialog(), nullptr);
    }

    return result;
}

// ============================================================================
// Modal Hooks
// ============================================================================

void ChangeHostModal::on_show() {
    spdlog::debug("[ChangeHostModal] on_show");
}

void ChangeHostModal::on_hide() {
    // Increment generation to invalidate any pending async callbacks
    ++test_generation_;

    // Clear active instance
    active_instance_ = nullptr;

    // Observers are auto-removed when dialog is destroyed (lv_subject_add_observer_obj)
    host_ip_observer_ = nullptr;
    host_port_observer_ = nullptr;

    spdlog::debug("[ChangeHostModal] on_hide");
}

// ============================================================================
// Subject Management
// ============================================================================

void ChangeHostModal::init_subjects() {
    if (subjects_initialized_)
        return;

    lv_subject_init_string(&host_ip_subject_, host_ip_buf_, nullptr, sizeof(host_ip_buf_), "");
    lv_subject_init_string(&host_port_subject_, host_port_buf_, nullptr, sizeof(host_port_buf_),
                           "7125");
    lv_subject_init_int(&testing_subject_, 0);
    lv_subject_init_int(&validated_subject_, 0);

    // Register subjects for XML binding
    lv_xml_register_subject(nullptr, "change_host_ip", &host_ip_subject_);
    lv_xml_register_subject(nullptr, "change_host_port", &host_port_subject_);
    lv_xml_register_subject(nullptr, "change_host_testing", &testing_subject_);
    lv_xml_register_subject(nullptr, "change_host_validated", &validated_subject_);

    subjects_initialized_ = true;
    spdlog::trace("[ChangeHostModal] Subjects initialized");
}

void ChangeHostModal::deinit_subjects() {
    if (!subjects_initialized_)
        return;

    lv_subject_deinit(&host_ip_subject_);
    lv_subject_deinit(&host_port_subject_);
    lv_subject_deinit(&testing_subject_);
    lv_subject_deinit(&validated_subject_);

    subjects_initialized_ = false;
    spdlog::trace("[ChangeHostModal] Subjects deinitialized");
}

// ============================================================================
// Event Handlers
// ============================================================================

void ChangeHostModal::handle_test_connection() {
    const char* ip = lv_subject_get_string(&host_ip_subject_);
    const char* port_str = lv_subject_get_string(&host_port_subject_);

    spdlog::debug("[ChangeHostModal] Test connection: {}:{}", ip ? ip : "",
                  port_str ? port_str : "");

    // Reset validation state
    lv_subject_set_int(&validated_subject_, 0);

    // Validate inputs
    if (!ip || strlen(ip) == 0) {
        set_status(nullptr, nullptr, "Please enter a host address");
        return;
    }

    if (!is_valid_ip_or_hostname(ip)) {
        set_status("icon_xmark_circle", "danger", "Invalid IP address or hostname");
        return;
    }

    if (!is_valid_port(port_str)) {
        set_status("icon_xmark_circle", "danger", "Invalid port (must be 1-65535)");
        return;
    }

    // Get the global MoonrakerClient (same approach as wizard)
    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        set_status("icon_xmark_circle", "danger", "Client not available");
        return;
    }

    // Suppress recovery modal during intentional host change
    EmergencyStopOverlay::instance().suppress_recovery_dialog(10000);
    client->disconnect();

    // Increment generation for stale callback detection
    uint64_t this_generation = ++test_generation_;

    // Store values for async callback (thread-safe)
    {
        std::lock_guard<std::mutex> lock(saved_values_mutex_);
        saved_ip_ = ip;
        saved_port_ = port_str;
    }

    // Set UI to testing state
    lv_subject_set_int(&testing_subject_, 1);
    set_status("icon_question_circle", "text_muted", "Testing connection...");

    // Shorter timeout for testing
    client->set_connection_timeout(5000);

    // Construct WebSocket URL
    std::string ws_url = "ws://" + std::string(ip) + ":" + std::string(port_str) + "/websocket";

    ChangeHostModal* self = this;
    int result = client->connect(
        ws_url.c_str(),
        [self, this_generation]() {
            if (self->test_generation_.load() != this_generation) {
                spdlog::debug("[ChangeHostModal] Ignoring stale success callback");
                return;
            }
            self->on_test_success();
        },
        [self, this_generation]() {
            if (self->test_generation_.load() != this_generation) {
                spdlog::debug("[ChangeHostModal] Ignoring stale failure callback");
                return;
            }
            self->on_test_failure();
        });

    // Disable automatic reconnection for testing
    client->setReconnect(nullptr);

    if (result != 0) {
        spdlog::error("[ChangeHostModal] Failed to initiate test connection: {}", result);
        set_status("icon_xmark_circle", "danger", "Error starting connection test");
        lv_subject_set_int(&testing_subject_, 0);
    }
}

void ChangeHostModal::on_test_success() {
    spdlog::info("[ChangeHostModal] Test connection successful");

    ui_async_call(
        [](void* ctx) {
            auto* self = static_cast<ChangeHostModal*>(ctx);
            if (!self->is_visible())
                return;

            self->set_status("icon_check_circle", "success", "Connection successful!");
            lv_subject_set_int(&self->testing_subject_, 0);
            lv_subject_set_int(&self->validated_subject_, 1);

            spdlog::info("[ChangeHostModal] Test passed, Save button enabled");
        },
        this);
}

void ChangeHostModal::on_test_failure() {
    spdlog::warn("[ChangeHostModal] Test connection failed");

    ui_async_call(
        [](void* ctx) {
            auto* self = static_cast<ChangeHostModal*>(ctx);
            if (!self->is_visible())
                return;

            self->set_status("icon_xmark_circle", "danger", "Connection failed");
            lv_subject_set_int(&self->testing_subject_, 0);

            spdlog::debug("[ChangeHostModal] Test failed, keeping Save disabled");
        },
        this);
}

void ChangeHostModal::handle_save() {
    spdlog::debug("[ChangeHostModal] Save clicked");

    const char* ip = lv_subject_get_string(&host_ip_subject_);
    const char* port_str = lv_subject_get_string(&host_port_subject_);

    if (!ip || !port_str) {
        spdlog::error("[ChangeHostModal] Cannot save - null subjects");
        return;
    }

    // Validate port before saving (defensive — should already be validated)
    int port = 7125;
    try {
        port = std::stoi(port_str);
    } catch (const std::exception& e) {
        spdlog::error("[ChangeHostModal] Invalid port '{}': {}", port_str, e.what());
        return;
    }

    // Save to config
    Config* config = Config::get_instance();
    if (config) {
        config->set(config->df() + "moonraker_host", std::string(ip));
        config->set(config->df() + "moonraker_port", port);
        config->save();
        spdlog::info("[ChangeHostModal] Saved new host: {}:{}", ip, port);
    }

    // Close modal
    hide();

    // Fire completion callback
    if (completion_callback_) {
        completion_callback_(true);
    }
}

void ChangeHostModal::handle_cancel() {
    spdlog::debug("[ChangeHostModal] Cancel clicked");

    // Increment generation to invalidate any pending test callbacks
    ++test_generation_;

    hide();

    if (completion_callback_) {
        completion_callback_(false);
    }
}

// ============================================================================
// Status Display
// ============================================================================

void ChangeHostModal::set_status(const char* icon_name, const char* color_token, const char* text) {
    if (!dialog())
        return;

    lv_obj_t* icon_label = lv_obj_find_by_name(dialog(), "status_icon");
    if (icon_label) {
        if (icon_name) {
            const char* icon_text = lv_xml_get_const(nullptr, icon_name);
            lv_label_set_text(icon_label, icon_text ? icon_text : "");
        } else {
            lv_label_set_text(icon_label, "");
        }
        if (color_token) {
            lv_obj_set_style_text_color(icon_label, theme_manager_get_color(color_token),
                                        LV_PART_MAIN);
        }
    }

    lv_obj_t* text_label = lv_obj_find_by_name(dialog(), "status_text");
    if (text_label) {
        lv_label_set_text(text_label, text ? text : "");
    }
}

// ============================================================================
// Input Change Observer
// ============================================================================

void ChangeHostModal::on_input_changed_cb(lv_observer_t* /*observer*/, lv_subject_t* /*subject*/) {
    // Reset validation when user edits host or port after a successful test
    lv_subject_t* validated = lv_xml_get_subject(nullptr, "change_host_validated");
    if (validated && lv_subject_get_int(validated) != 0) {
        lv_subject_set_int(validated, 0);
        spdlog::debug("[ChangeHostModal] Input changed, validation reset");
    }
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void ChangeHostModal::register_callbacks() {
    if (callbacks_registered_)
        return;

    lv_xml_register_event_cb(nullptr, "on_change_host_test", on_test_connection_cb);
    lv_xml_register_event_cb(nullptr, "on_change_host_save", on_save_cb);
    lv_xml_register_event_cb(nullptr, "on_change_host_cancel", on_cancel_cb);

    callbacks_registered_ = true;
    spdlog::trace("[ChangeHostModal] Callbacks registered");
}

void ChangeHostModal::on_test_connection_cb(lv_event_t* /*e*/) {
    if (active_instance_) {
        active_instance_->handle_test_connection();
    }
}

void ChangeHostModal::on_save_cb(lv_event_t* /*e*/) {
    if (active_instance_) {
        active_instance_->handle_save();
    }
}

void ChangeHostModal::on_cancel_cb(lv_event_t* /*e*/) {
    if (active_instance_) {
        active_instance_->handle_cancel();
    }
}
