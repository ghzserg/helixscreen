// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_connection.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_keyboard.h"
#include "ui_notification.h"
#include "ui_subject_registry.h"
#include "ui_wizard.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "moonraker_client.h"
#include "wizard_validation.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <string>

// ============================================================================
// External Subject (defined in ui_wizard.cpp)
// ============================================================================

// Controls wizard Next button globally - shared across wizard steps
extern lv_subject_t connection_test_passed;

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardConnectionStep> g_wizard_connection_step;

WizardConnectionStep* get_wizard_connection_step() {
    if (!g_wizard_connection_step) {
        g_wizard_connection_step = std::make_unique<WizardConnectionStep>();
    }
    return g_wizard_connection_step.get();
}

void destroy_wizard_connection_step() {
    g_wizard_connection_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardConnectionStep::WizardConnectionStep() {
    // Zero-initialize buffers
    std::memset(connection_ip_buffer_, 0, sizeof(connection_ip_buffer_));
    std::memset(connection_port_buffer_, 0, sizeof(connection_port_buffer_));
    std::memset(connection_status_icon_buffer_, 0, sizeof(connection_status_icon_buffer_));
    std::memset(connection_status_text_buffer_, 0, sizeof(connection_status_text_buffer_));

    spdlog::debug("[{}] Instance created", get_name());
}

WizardConnectionStep::~WizardConnectionStep() {
    // NOTE: Do NOT call LVGL functions here - LVGL may be destroyed first
    // NOTE: Do NOT log here - spdlog may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Move Semantics
// ============================================================================

WizardConnectionStep::WizardConnectionStep(WizardConnectionStep&& other) noexcept
    : screen_root_(other.screen_root_), connection_ip_(other.connection_ip_),
      connection_port_(other.connection_port_),
      connection_status_icon_(other.connection_status_icon_),
      connection_status_text_(other.connection_status_text_),
      connection_testing_(other.connection_testing_),
      connection_validated_(other.connection_validated_),
      subjects_initialized_(other.subjects_initialized_), saved_ip_(std::move(other.saved_ip_)),
      saved_port_(std::move(other.saved_port_)) {
    // Move buffers
    std::memcpy(connection_ip_buffer_, other.connection_ip_buffer_, sizeof(connection_ip_buffer_));
    std::memcpy(connection_port_buffer_, other.connection_port_buffer_,
                sizeof(connection_port_buffer_));
    std::memcpy(connection_status_icon_buffer_, other.connection_status_icon_buffer_,
                sizeof(connection_status_icon_buffer_));
    std::memcpy(connection_status_text_buffer_, other.connection_status_text_buffer_,
                sizeof(connection_status_text_buffer_));

    // Null out other
    other.screen_root_ = nullptr;
    other.subjects_initialized_ = false;
    other.connection_validated_ = false;
}

WizardConnectionStep& WizardConnectionStep::operator=(WizardConnectionStep&& other) noexcept {
    if (this != &other) {
        screen_root_ = other.screen_root_;
        connection_ip_ = other.connection_ip_;
        connection_port_ = other.connection_port_;
        connection_status_icon_ = other.connection_status_icon_;
        connection_status_text_ = other.connection_status_text_;
        connection_testing_ = other.connection_testing_;
        connection_validated_ = other.connection_validated_;
        subjects_initialized_ = other.subjects_initialized_;
        saved_ip_ = std::move(other.saved_ip_);
        saved_port_ = std::move(other.saved_port_);

        // Move buffers
        std::memcpy(connection_ip_buffer_, other.connection_ip_buffer_,
                    sizeof(connection_ip_buffer_));
        std::memcpy(connection_port_buffer_, other.connection_port_buffer_,
                    sizeof(connection_port_buffer_));
        std::memcpy(connection_status_icon_buffer_, other.connection_status_icon_buffer_,
                    sizeof(connection_status_icon_buffer_));
        std::memcpy(connection_status_text_buffer_, other.connection_status_text_buffer_,
                    sizeof(connection_status_text_buffer_));

        // Null out other
        other.screen_root_ = nullptr;
        other.subjects_initialized_ = false;
        other.connection_validated_ = false;
    }
    return *this;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardConnectionStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Load existing values from config if available
    Config* config = Config::get_instance();
    std::string default_ip = "";
    std::string default_port = "7125"; // Default Moonraker port

    try {
        std::string default_printer =
            config->get<std::string>("/default_printer", "default_printer");
        std::string printer_path = "/printers/" + default_printer;

        default_ip = config->get<std::string>(printer_path + "/moonraker_host", "");
        int port_num = config->get<int>(printer_path + "/moonraker_port", 7125);
        default_port = std::to_string(port_num);

        spdlog::debug("[{}] Loaded from config: {}:{}", get_name(), default_ip, default_port);
    } catch (const std::exception& e) {
        spdlog::debug("[{}] No existing config, using defaults: {}", get_name(), e.what());
    }

    // Initialize with values from config or defaults
    strncpy(connection_ip_buffer_, default_ip.c_str(), sizeof(connection_ip_buffer_) - 1);
    connection_ip_buffer_[sizeof(connection_ip_buffer_) - 1] = '\0';

    strncpy(connection_port_buffer_, default_port.c_str(), sizeof(connection_port_buffer_) - 1);
    connection_port_buffer_[sizeof(connection_port_buffer_) - 1] = '\0';

    UI_SUBJECT_INIT_AND_REGISTER_STRING(connection_ip_, connection_ip_buffer_,
                                        connection_ip_buffer_, "connection_ip");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(connection_port_, connection_port_buffer_,
                                        connection_port_buffer_, "connection_port");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(connection_status_icon_, connection_status_icon_buffer_, "",
                                        "connection_status_icon");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(connection_status_text_, connection_status_text_buffer_, "",
                                        "connection_status_text");
    UI_SUBJECT_INIT_AND_REGISTER_INT(connection_testing_, 0, "connection_testing");

    // Set connection_test_passed to 0 (disabled) for this step
    lv_subject_set_int(&connection_test_passed, 0);

    // Reset validation state
    connection_validated_ = false;
    subjects_initialized_ = true;

    // Check if we have a saved configuration
    if (!default_ip.empty() && !default_port.empty()) {
        spdlog::debug("[{}] Have saved config, but needs validation", get_name());
    }

    spdlog::debug("[{}] Subjects initialized (IP: {}, Port: {})", get_name(),
                  default_ip.empty() ? "<empty>" : default_ip, default_port);
}

// ============================================================================
// Static Trampolines for LVGL Callbacks
// ============================================================================

void WizardConnectionStep::on_test_connection_clicked_static(lv_event_t* e) {
    auto* self = static_cast<WizardConnectionStep*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_test_connection_clicked();
    }
}

void WizardConnectionStep::on_ip_input_changed_static(lv_event_t* e) {
    auto* self = static_cast<WizardConnectionStep*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_ip_input_changed();
    }
}

void WizardConnectionStep::on_port_input_changed_static(lv_event_t* e) {
    auto* self = static_cast<WizardConnectionStep*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_port_input_changed();
    }
}

// ============================================================================
// Event Handler Implementations
// ============================================================================

void WizardConnectionStep::handle_test_connection_clicked() {
    LVGL_SAFE_EVENT_CB_BEGIN("[Wizard Connection] handle_test_connection_clicked");

    // Get values from subjects
    const char* ip = lv_subject_get_string(&connection_ip_);
    const char* port_str = lv_subject_get_string(&connection_port_);

    spdlog::debug("[{}] Test connection clicked: {}:{}", get_name(), ip, port_str);

    // Clear previous validation state
    connection_validated_ = false;
    lv_subject_set_int(&connection_test_passed, 0);

    // Validate inputs
    if (!ip || strlen(ip) == 0) {
        lv_subject_copy_string(&connection_status_icon_, "");
        lv_subject_copy_string(&connection_status_text_, "Please enter an IP address or hostname");
        spdlog::warn("[{}] Empty IP address", get_name());
        return;
    }

    if (!is_valid_ip_or_hostname(ip)) {
        const char* error_icon = lv_xml_get_const(nullptr, "icon_xmark_circle");
        lv_subject_copy_string(&connection_status_icon_, error_icon ? error_icon : "");
        lv_subject_copy_string(&connection_status_text_, "Invalid IP address or hostname");
        spdlog::warn("[{}] Invalid IP/hostname: {}", get_name(), ip);
        return;
    }

    if (!is_valid_port(port_str)) {
        const char* error_icon = lv_xml_get_const(nullptr, "icon_xmark_circle");
        lv_subject_copy_string(&connection_status_icon_, error_icon ? error_icon : "");
        lv_subject_copy_string(&connection_status_text_, "Invalid port (must be 1-65535)");
        spdlog::warn("[{}] Invalid port: {}", get_name(), port_str);
        return;
    }

    // Get MoonrakerClient instance
    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        const char* error_icon = lv_xml_get_const(nullptr, "icon_xmark_circle");
        lv_subject_copy_string(&connection_status_icon_, error_icon ? error_icon : "");
        lv_subject_copy_string(&connection_status_text_, "Error: Moonraker client not initialized");
        lv_subject_set_int(&connection_testing_, 0);
        LOG_ERROR_INTERNAL("[{}] MoonrakerClient is nullptr", get_name());
        return;
    }

    // Disconnect any previous connection attempt
    client->disconnect();

    // Increment generation to invalidate any pending callbacks from previous attempts
    uint64_t this_generation = ++connection_generation_;

    // Store IP/port for async callback (thread-safe)
    {
        std::lock_guard<std::mutex> lock(saved_values_mutex_);
        saved_ip_ = ip;
        saved_port_ = port_str;
    }

    // Set UI to testing state
    lv_subject_set_int(&connection_testing_, 1);
    const char* testing_icon = lv_xml_get_const(nullptr, "icon_question_circle");
    lv_subject_copy_string(&connection_status_icon_, testing_icon ? testing_icon : "");
    lv_subject_copy_string(&connection_status_text_, "Testing connection...");

    spdlog::debug("[{}] Starting connection test to {}:{}", get_name(), ip, port_str);

    // Set shorter timeout for wizard testing
    client->set_connection_timeout(5000);

    // Construct WebSocket URL
    std::string ws_url = "ws://" + std::string(ip) + ":" + std::string(port_str) + "/websocket";

    // Capture generation counter to detect stale callbacks
    // If cleanup_called_ or generation changes, callback will be ignored
    WizardConnectionStep* self = this;

    int result = client->connect(
        ws_url.c_str(),
        // On connected callback - check generation before proceeding
        [self, this_generation]() {
            if (self->is_stale() || !self->is_current_generation(this_generation)) {
                spdlog::debug("[Wizard Connection] Ignoring stale success callback");
                return;
            }
            self->on_connection_success();
        },
        // On disconnected callback - check generation before proceeding
        [self, this_generation]() {
            if (self->is_stale() || !self->is_current_generation(this_generation)) {
                spdlog::debug("[Wizard Connection] Ignoring stale failure callback");
                return;
            }
            self->on_connection_failure();
        });

    // Disable automatic reconnection for wizard testing
    client->setReconnect(nullptr);

    if (result != 0) {
        spdlog::error("[{}] Failed to initiate connection: {}", get_name(), result);
        const char* error_icon = lv_xml_get_const(nullptr, "icon_xmark_circle");
        lv_subject_copy_string(&connection_status_icon_, error_icon ? error_icon : "");
        lv_subject_copy_string(&connection_status_text_, "Error starting connection test");
        lv_subject_set_int(&connection_testing_, 0);
    }

    LVGL_SAFE_EVENT_CB_END();
}

void WizardConnectionStep::on_connection_success() {
    // NOTE: This is called from WebSocket thread - only do thread-safe operations here
    spdlog::info("[Wizard Connection] Connection successful!");

    // Defer ALL operations (including config) to main thread
    lv_async_call(
        [](void* ctx) {
            auto* self = static_cast<WizardConnectionStep*>(ctx);

            if (self->is_stale()) {
                spdlog::debug("[Wizard Connection] Cleanup called, skipping connection success UI");
                return;
            }

            // Get saved values under lock
            std::string ip, port;
            {
                std::lock_guard<std::mutex> lock(self->saved_values_mutex_);
                ip = self->saved_ip_;
                port = self->saved_port_;
            }

            // NOW safe to access config (on main thread)
            Config* config = Config::get_instance();
            try {
                std::string default_printer =
                    config->get<std::string>("/default_printer", "default_printer");
                std::string printer_path = "/printers/" + default_printer;

                config->set(printer_path + "/moonraker_host", ip);
                config->set(printer_path + "/moonraker_port", std::stoi(port));
                if (config->save()) {
                    spdlog::debug("[Wizard Connection] Saved configuration: {}:{}", ip, port);
                } else {
                    spdlog::error("[Wizard Connection] Failed to save configuration!");
                }
            } catch (const std::exception& e) {
                spdlog::error("[Wizard Connection] Failed to save config: {}", e.what());
            }

            // Show "discovering" status - don't enable Next yet until discovery completes
            const char* spinner_icon = lv_xml_get_const(nullptr, "icon_loading");
            lv_subject_copy_string(&self->connection_status_icon_,
                                   spinner_icon ? spinner_icon : "");
            lv_subject_copy_string(&self->connection_status_text_,
                                   "Connected! Discovering printer...");
            lv_subject_set_int(&self->connection_testing_, 0);

            // Trigger hardware discovery - only enable Next when this completes
            MoonrakerClient* client = get_moonraker_client();
            if (client) {
                // Capture generation for discovery callback
                uint64_t discover_gen = self->connection_generation_.load();

                client->discover_printer([self, discover_gen]() {
                    // Check if still valid before queueing UI update
                    if (self->is_stale() || !self->is_current_generation(discover_gen)) {
                        spdlog::debug("[Wizard Connection] Ignoring stale discovery callback");
                        return;
                    }

                    spdlog::info("[Wizard Connection] Hardware discovery complete!");

                    // Defer discovery UI update to main thread
                    lv_async_call(
                        [](void* ctx2) {
                            auto* self2 = static_cast<WizardConnectionStep*>(ctx2);

                            if (self2->is_stale()) {
                                spdlog::debug("[Wizard Connection] Cleanup called, skipping "
                                              "discovery UI update");
                                return;
                            }

                            MoonrakerClient* client = get_moonraker_client();
                            if (client) {
                                auto heaters = client->get_heaters();
                                auto sensors = client->get_sensors();
                                auto fans = client->get_fans();
                                spdlog::info(
                                    "[Wizard Connection] Discovered {} heaters, {} sensors, {} fans",
                                    heaters.size(), sensors.size(), fans.size());
                                spdlog::info("[Wizard Connection] Hostname: '{}'",
                                             client->get_hostname());
                            }

                            // NOW enable Next button - discovery is complete
                            const char* check_icon = lv_xml_get_const(nullptr, "icon_check_circle");
                            lv_subject_copy_string(&self2->connection_status_icon_,
                                                   check_icon ? check_icon : "");
                            lv_subject_copy_string(&self2->connection_status_text_,
                                                   "Connection successful!");
                            self2->connection_validated_ = true;
                            lv_subject_set_int(&connection_test_passed, 1);
                        },
                        self);
                });
            } else {
                // No client available - still show success but warn
                const char* check_icon = lv_xml_get_const(nullptr, "icon_check_circle");
                lv_subject_copy_string(&self->connection_status_icon_,
                                       check_icon ? check_icon : "");
                lv_subject_copy_string(&self->connection_status_text_, "Connected (no discovery)");
                self->connection_validated_ = true;
                lv_subject_set_int(&connection_test_passed, 1);
            }
        },
        this);
}

void WizardConnectionStep::on_connection_failure() {
    // NOTE: This is called from WebSocket thread - only do thread-safe operations here
    spdlog::debug("[Wizard Connection] on_disconnected fired");

    // Defer LVGL operations to main thread
    lv_async_call(
        [](void* ctx) {
            auto* self = static_cast<WizardConnectionStep*>(ctx);

            if (self->is_stale()) {
                spdlog::debug("[Wizard Connection] Cleanup called, skipping connection failure UI");
                return;
            }

            // Check if we're still in testing mode (must check on main thread)
            int testing_state = lv_subject_get_int(&self->connection_testing_);
            spdlog::debug("[Wizard Connection] Connection failure, testing_state={}", testing_state);

            if (testing_state == 1) {
                spdlog::error("[Wizard Connection] Connection failed");

                const char* error_icon = lv_xml_get_const(nullptr, "icon_xmark_circle");
                lv_subject_copy_string(&self->connection_status_icon_,
                                       error_icon ? error_icon : "");
                lv_subject_copy_string(&self->connection_status_text_,
                                       "Connection failed. Check IP/port and try again.");
                lv_subject_set_int(&self->connection_testing_, 0);
                self->connection_validated_ = false;
                lv_subject_set_int(&connection_test_passed, 0);
            } else {
                spdlog::debug("[Wizard Connection] Ignoring disconnect (not in testing mode)");
            }
        },
        this);
}

// ============================================================================
// Auto-Probe Methods
// ============================================================================

bool WizardConnectionStep::should_auto_probe() const {
    // Don't probe if already attempted this session
    if (auto_probe_attempted_) {
        return false;
    }

    // Don't probe if already testing a connection
    if (lv_subject_get_int(const_cast<lv_subject_t*>(&connection_testing_)) == 1) {
        return false;
    }

    // Don't probe if already validated
    if (connection_validated_) {
        return false;
    }

    // Probe both when:
    // 1. IP is empty (no saved config) - will probe 127.0.0.1
    // 2. IP is set but not validated yet - will test the saved config
    return true;
}

void WizardConnectionStep::auto_probe_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<WizardConnectionStep*>(lv_timer_get_user_data(timer));
    if (self) {
        self->attempt_auto_probe();
    }
}

void WizardConnectionStep::attempt_auto_probe() {
    // Get the IP/port from subjects - may be from config or default
    const char* ip = lv_subject_get_string(&connection_ip_);
    const char* port = lv_subject_get_string(&connection_port_);

    // If IP is empty, use localhost as default probe target
    std::string probe_ip = (ip && strlen(ip) > 0) ? ip : "127.0.0.1";
    std::string probe_port = (port && strlen(port) > 0) ? port : "7125";

    spdlog::debug("[{}] Starting auto-probe to {}:{}", get_name(), probe_ip, probe_port);

    // Mark as attempted (prevents re-probe on re-entry)
    auto_probe_attempted_ = true;
    auto_probe_state_.store(AutoProbeState::IN_PROGRESS);

    // Increment generation to invalidate any stale callbacks
    uint64_t this_generation = ++connection_generation_;

    // Clear timer reference (it's already fired)
    auto_probe_timer_ = nullptr;

    // Get MoonrakerClient
    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        spdlog::warn("[{}] Auto-probe: MoonrakerClient not available", get_name());
        auto_probe_state_.store(AutoProbeState::FAILED);
        return;
    }

    // Disconnect any previous connection
    client->disconnect();

    // Store probe target for callbacks (thread-safe)
    {
        std::lock_guard<std::mutex> lock(saved_values_mutex_);
        saved_ip_ = probe_ip;
        saved_port_ = probe_port;
    }

    // Show subtle probing indicator
    const char* probe_icon = lv_xml_get_const(nullptr, "icon_question_circle");
    lv_subject_copy_string(&connection_status_icon_, probe_icon ? probe_icon : "");
    lv_subject_copy_string(&connection_status_text_, "Testing connection...");

    // Set testing state (reuses existing subject for button disable)
    lv_subject_set_int(&connection_testing_, 1);

    // Set short timeout for auto-probe (3 seconds - faster than manual test)
    client->set_connection_timeout(3000);

    // Construct WebSocket URL
    std::string ws_url = "ws://" + probe_ip + ":" + probe_port + "/websocket";

    WizardConnectionStep* self = this;
    int result = client->connect(
        ws_url.c_str(),
        // Check generation before invoking callback
        [self, this_generation]() {
            if (self->is_stale() || !self->is_current_generation(this_generation)) {
                spdlog::debug("[Wizard Connection] Ignoring stale auto-probe success");
                return;
            }
            self->on_auto_probe_success();
        },
        [self, this_generation]() {
            if (self->is_stale() || !self->is_current_generation(this_generation)) {
                spdlog::debug("[Wizard Connection] Ignoring stale auto-probe failure");
                return;
            }
            self->on_auto_probe_failure();
        });

    // Disable auto-reconnect for probe
    client->setReconnect(nullptr);

    if (result != 0) {
        spdlog::debug("[{}] Auto-probe: Failed to initiate connection", get_name());
        auto_probe_state_.store(AutoProbeState::FAILED);
        lv_subject_set_int(&connection_testing_, 0);
        // Silent failure - clear status
        lv_subject_copy_string(&connection_status_icon_, "");
        lv_subject_copy_string(&connection_status_text_, "");
    }
}

void WizardConnectionStep::on_auto_probe_success() {
    // NOTE: This is called from WebSocket thread - only do thread-safe operations here

    // Verify we're still in auto-probe mode (atomic read)
    if (auto_probe_state_.load() != AutoProbeState::IN_PROGRESS) {
        spdlog::debug("[Wizard Connection] Ignoring auto-probe success (state changed)");
        return;
    }

    // Get saved values under lock for logging
    std::string ip_copy, port_copy;
    {
        std::lock_guard<std::mutex> lock(saved_values_mutex_);
        ip_copy = saved_ip_;
        port_copy = saved_port_;
    }

    spdlog::info("[Wizard Connection] Auto-probe successful! Connected to {}:{}", ip_copy,
                 port_copy);

    auto_probe_state_.store(AutoProbeState::SUCCEEDED);

    // Defer ALL operations (including config) to main thread
    lv_async_call(
        [](void* ctx) {
            auto* self = static_cast<WizardConnectionStep*>(ctx);

            if (self->is_stale()) {
                spdlog::debug("[Wizard Connection] Cleanup called, skipping auto-probe UI update");
                return;
            }

            // Get saved values under lock
            std::string ip, port;
            {
                std::lock_guard<std::mutex> lock(self->saved_values_mutex_);
                ip = self->saved_ip_;
                port = self->saved_port_;
            }

            // NOW safe to access config (on main thread)
            Config* config = Config::get_instance();
            try {
                std::string default_printer =
                    config->get<std::string>("/default_printer", "default_printer");
                std::string printer_path = "/printers/" + default_printer;

                config->set(printer_path + "/moonraker_host", ip);
                config->set(printer_path + "/moonraker_port", std::stoi(port));
                if (config->save()) {
                    spdlog::debug("[Wizard Connection] Auto-probe: Saved configuration");
                }
            } catch (const std::exception& e) {
                spdlog::error("[Wizard Connection] Auto-probe: Failed to save config: {}", e.what());
            }

            // Update subjects with the successful connection target
            lv_subject_copy_string(&self->connection_ip_, ip.c_str());
            lv_subject_copy_string(&self->connection_port_, port.c_str());

            // Hide help text on successful auto-probe
            if (self->screen_root_) {
                lv_obj_t* help_text = lv_obj_find_by_name(self->screen_root_, "help_text");
                if (help_text) {
                    lv_obj_add_flag(help_text, LV_OBJ_FLAG_HIDDEN);
                }
            }

            // Show "discovering" status - don't enable Next until discovery completes
            const char* spinner_icon = lv_xml_get_const(nullptr, "icon_loading");
            lv_subject_copy_string(&self->connection_status_icon_,
                                   spinner_icon ? spinner_icon : "");
            lv_subject_copy_string(&self->connection_status_text_, "Connected, discovering...");

            // Clear testing state
            lv_subject_set_int(&self->connection_testing_, 0);

            // Trigger hardware discovery - only enable Next when this completes
            MoonrakerClient* client = get_moonraker_client();
            if (client) {
                // Capture generation for discovery callback
                uint64_t discover_gen = self->connection_generation_.load();

                client->discover_printer([self, discover_gen]() {
                    // Check if still valid before queueing UI update
                    if (self->is_stale() || !self->is_current_generation(discover_gen)) {
                        spdlog::debug("[Wizard Connection] Ignoring stale discovery callback");
                        return;
                    }

                    spdlog::info("[Wizard Connection] Auto-probe: Hardware discovery complete");

                    // Defer discovery completion UI update to main thread
                    lv_async_call(
                        [](void* ctx2) {
                            auto* self2 = static_cast<WizardConnectionStep*>(ctx2);

                            if (self2->is_stale()) {
                                spdlog::debug("[Wizard Connection] Cleanup called, skipping "
                                              "discovery UI update");
                                return;
                            }

                            MoonrakerClient* client = get_moonraker_client();
                            if (client) {
                                spdlog::info("[Wizard Connection] Hostname: '{}'",
                                             client->get_hostname());
                            }

                            // NOW enable Next button - discovery is complete
                            const char* check_icon = lv_xml_get_const(nullptr, "icon_check_circle");
                            lv_subject_copy_string(&self2->connection_status_icon_,
                                                   check_icon ? check_icon : "");
                            lv_subject_copy_string(&self2->connection_status_text_,
                                                   "Connection successful!");
                            self2->connection_validated_ = true;
                            lv_subject_set_int(&connection_test_passed, 1);
                        },
                        self);
                });
            } else {
                // No client - still show success
                const char* check_icon = lv_xml_get_const(nullptr, "icon_check_circle");
                lv_subject_copy_string(&self->connection_status_icon_,
                                       check_icon ? check_icon : "");
                lv_subject_copy_string(&self->connection_status_text_, "Connection successful!");
                self->connection_validated_ = true;
                lv_subject_set_int(&connection_test_passed, 1);
            }
        },
        this);
}

void WizardConnectionStep::on_auto_probe_failure() {
    // NOTE: This is called from WebSocket thread - only do thread-safe operations here

    // Verify we're still in auto-probe mode (atomic read)
    if (auto_probe_state_.load() != AutoProbeState::IN_PROGRESS) {
        spdlog::debug("[Wizard Connection] Ignoring auto-probe failure (state changed)");
        return;
    }

    spdlog::debug("[Wizard Connection] Auto-probe: No printer at localhost (silent failure)");

    auto_probe_state_.store(AutoProbeState::FAILED);

    // Defer LVGL operations to main thread
    lv_async_call(
        [](void* ctx) {
            auto* self = static_cast<WizardConnectionStep*>(ctx);

            if (self->is_stale()) {
                spdlog::debug("[Wizard Connection] Cleanup called, skipping auto-probe failure UI");
                return;
            }

            // Silent failure - just clear status, don't show error
            lv_subject_copy_string(&self->connection_status_icon_, "");
            lv_subject_copy_string(&self->connection_status_text_, "");
            lv_subject_set_int(&self->connection_testing_, 0);

            // Leave fields empty - user will enter manually
        },
        this);
}

// ============================================================================
// Input Change Handlers
// ============================================================================

void WizardConnectionStep::handle_ip_input_changed() {
    LVGL_SAFE_EVENT_CB_BEGIN("[Wizard Connection] handle_ip_input_changed");

    // If auto-probe is in progress, cancel it
    if (auto_probe_state_.load() == AutoProbeState::IN_PROGRESS) {
        spdlog::debug("[{}] User input during auto-probe, cancelling", get_name());
        auto_probe_state_.store(AutoProbeState::FAILED); // Mark as failed to ignore callbacks
        MoonrakerClient* client = get_moonraker_client();
        if (client) {
            client->disconnect();
        }
        lv_subject_set_int(&connection_testing_, 0);
    }

    // Clear any previous status message
    const char* current_status = lv_subject_get_string(&connection_status_text_);
    if (current_status && strlen(current_status) > 0) {
        lv_subject_copy_string(&connection_status_icon_, "");
        lv_subject_copy_string(&connection_status_text_, "");
    }

    // Clear validation state
    connection_validated_ = false;
    lv_subject_set_int(&connection_test_passed, 0);

    LVGL_SAFE_EVENT_CB_END();
}

void WizardConnectionStep::handle_port_input_changed() {
    LVGL_SAFE_EVENT_CB_BEGIN("[Wizard Connection] handle_port_input_changed");

    // If auto-probe is in progress, cancel it
    if (auto_probe_state_.load() == AutoProbeState::IN_PROGRESS) {
        spdlog::debug("[{}] User input during auto-probe, cancelling", get_name());
        auto_probe_state_.store(AutoProbeState::FAILED); // Mark as failed to ignore callbacks
        MoonrakerClient* client = get_moonraker_client();
        if (client) {
            client->disconnect();
        }
        lv_subject_set_int(&connection_testing_, 0);
    }

    // Clear any previous status message
    const char* current_status = lv_subject_get_string(&connection_status_text_);
    if (current_status && strlen(current_status) > 0) {
        lv_subject_copy_string(&connection_status_icon_, "");
        lv_subject_copy_string(&connection_status_text_, "");
    }

    // Clear validation state
    connection_validated_ = false;
    lv_subject_set_int(&connection_test_passed, 0);

    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Callback Registration
// ============================================================================

void WizardConnectionStep::register_callbacks() {
    spdlog::debug("[{}] Registering event callbacks", get_name());

    // NOTE: We use static trampolines registered via lv_xml_register_event_cb
    // The actual event binding happens in create() where we have 'this' pointer
    lv_xml_register_event_cb(nullptr, "on_test_connection_clicked",
                             on_test_connection_clicked_static);
    lv_xml_register_event_cb(nullptr, "on_ip_input_changed", on_ip_input_changed_static);
    lv_xml_register_event_cb(nullptr, "on_port_input_changed", on_port_input_changed_static);

    spdlog::debug("[{}] Event callbacks registered", get_name());
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardConnectionStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating connection screen", get_name());

    // Reset cleanup guard for fresh screen (atomic store with release semantics)
    cleanup_called_.store(false, std::memory_order_release);

    if (!parent) {
        LOG_ERROR_INTERNAL("[{}] Cannot create: null parent", get_name());
        return nullptr;
    }

    // Create from XML
    screen_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_connection", nullptr));

    if (!screen_root_) {
        LOG_ERROR_INTERNAL("[{}] Failed to create from XML", get_name());
        return nullptr;
    }

    // Find and configure test button - pass 'this' as user_data
    lv_obj_t* test_btn = lv_obj_find_by_name(screen_root_, "btn_test_connection");
    if (test_btn) {
        lv_obj_add_event_cb(test_btn, on_test_connection_clicked_static, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}] Test button callback attached", get_name());
    } else {
        LOG_ERROR_INTERNAL("[{}] Test button not found in XML", get_name());
    }

    // Find input fields and attach change handlers + keyboard support
    lv_obj_t* ip_input = lv_obj_find_by_name(screen_root_, "ip_input");
    if (ip_input) {
        const char* ip_text = lv_subject_get_string(&connection_ip_);
        if (ip_text && strlen(ip_text) > 0) {
            lv_textarea_set_text(ip_input, ip_text);
            spdlog::debug("[{}] Pre-filled IP input: {}", get_name(), ip_text);
        }
        lv_obj_add_event_cb(ip_input, on_ip_input_changed_static, LV_EVENT_VALUE_CHANGED, this);
        ui_keyboard_register_textarea(ip_input);
        spdlog::debug("[{}] IP input configured with keyboard", get_name());
    }

    lv_obj_t* port_input = lv_obj_find_by_name(screen_root_, "port_input");
    if (port_input) {
        const char* port_text = lv_subject_get_string(&connection_port_);
        if (port_text && strlen(port_text) > 0) {
            lv_textarea_set_text(port_input, port_text);
            spdlog::debug("[{}] Pre-filled port input: {}", get_name(), port_text);
        }
        lv_obj_add_event_cb(port_input, on_port_input_changed_static, LV_EVENT_VALUE_CHANGED, this);
        ui_keyboard_register_textarea(port_input);
        spdlog::debug("[{}] Port input configured with keyboard", get_name());
    }

    lv_obj_update_layout(screen_root_);

    // Schedule auto-probe if appropriate (empty config, first visit)
    if (should_auto_probe()) {
        spdlog::debug("[{}] Scheduling auto-probe for localhost", get_name());
        auto_probe_timer_ = lv_timer_create(auto_probe_timer_cb, 100, this);
        lv_timer_set_repeat_count(auto_probe_timer_, 1); // One-shot timer
    }

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardConnectionStep::cleanup() {
    spdlog::debug("[{}] Cleaning up connection screen", get_name());

    // Mark cleanup as called to guard async callbacks (atomic store with release semantics)
    cleanup_called_.store(true, std::memory_order_release);

    // Cancel any pending auto-probe timer
    if (auto_probe_timer_) {
        lv_timer_delete(auto_probe_timer_);
        auto_probe_timer_ = nullptr;
    }

    // If a connection test or auto-probe is in progress, cancel it
    if (lv_subject_get_int(&connection_testing_) == 1 ||
        auto_probe_state_.load() == AutoProbeState::IN_PROGRESS) {
        MoonrakerClient* client = get_moonraker_client();
        if (client) {
            client->disconnect();
        }
        lv_subject_set_int(&connection_testing_, 0);
    }

    // Reset auto-probe state (but NOT auto_probe_attempted_ - that persists)
    auto_probe_state_.store(AutoProbeState::IDLE);

    // Clear status
    lv_subject_copy_string(&connection_status_icon_, "");
    lv_subject_copy_string(&connection_status_text_, "");

    // Reset UI references (wizard framework handles deletion)
    screen_root_ = nullptr;

    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Utility Functions
// ============================================================================

bool WizardConnectionStep::get_url(char* buffer, size_t size) const {
    if (!buffer || size == 0) {
        return false;
    }

    // Cast away const for LVGL API (subject is not modified by get_string)
    const char* ip = lv_subject_get_string(const_cast<lv_subject_t*>(&connection_ip_));
    const char* port_str = lv_subject_get_string(const_cast<lv_subject_t*>(&connection_port_));

    if (!is_valid_ip_or_hostname(ip) || !is_valid_port(port_str)) {
        return false;
    }

    snprintf(buffer, size, "ws://%s:%s/websocket", ip, port_str);
    return true;
}

bool WizardConnectionStep::is_validated() const {
    return connection_validated_;
}
