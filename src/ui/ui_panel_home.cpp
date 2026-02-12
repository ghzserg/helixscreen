// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_home.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_icon.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_overlay_network_settings.h"
#include "ui_panel_ams.h"
#include "ui_panel_print_status.h"
#include "ui_panel_temp_control.h"
#include "ui_printer_manager_overlay.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "ams_state.h"
#include "app_globals.h"
#include "config.h"
#include "ethernet_manager.h"
#include "filament_sensor_manager.h"
#include "format_utils.h"
#include "injection_point_manager.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "prerendered_images.h"
#include "printer_detector.h"
#include "printer_images.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "wifi_manager.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

// Signal polling interval (5 seconds)
static constexpr uint32_t SIGNAL_POLL_INTERVAL_MS = 5000;

using helix::ui::temperature::centi_to_degrees;

HomePanel::HomePanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents with default values
    std::strcpy(status_buffer_, "Welcome to HelixScreen");
    std::snprintf(temp_buffer_, sizeof(temp_buffer_), "%s°C", helix::fmt::UNAVAILABLE);
    std::strcpy(network_label_buffer_, "WiFi");

    // Subscribe to PrinterState subjects (ObserverGuard handles cleanup)
    // Note: Connection state dimming is now handled by XML binding to printer_connection_state
    using helix::ui::observe_int_sync;
    using helix::ui::observe_print_state;
    using helix::ui::observe_string;

    extruder_temp_observer_ = observe_int_sync<HomePanel>(
        printer_state_.get_extruder_temp_subject(), this,
        [](HomePanel* self, int temp) { self->on_extruder_temp_changed(temp); });
    extruder_target_observer_ = observe_int_sync<HomePanel>(
        printer_state_.get_extruder_target_subject(), this,
        [](HomePanel* self, int target) { self->on_extruder_target_changed(target); });

    // Subscribe to print state for dynamic print card updates
    print_state_observer_ = observe_print_state<HomePanel>(
        printer_state_.get_print_state_enum_subject(), this,
        [](HomePanel* self, PrintJobState state) { self->on_print_state_changed(state); });
    print_progress_observer_ = observe_int_sync<HomePanel>(
        printer_state_.get_print_progress_subject(), this,
        [](HomePanel* self, int /*progress*/) { self->on_print_progress_or_time_changed(); });
    print_time_left_observer_ = observe_int_sync<HomePanel>(
        printer_state_.get_print_time_left_subject(), this,
        [](HomePanel* self, int /*time*/) { self->on_print_progress_or_time_changed(); });
    print_thumbnail_path_observer_ = observe_string<HomePanel>(
        printer_state_.get_print_thumbnail_path_subject(), this,
        [](HomePanel* self, const char* path) { self->on_print_thumbnail_path_changed(path); });

    spdlog::debug("[{}] Subscribed to PrinterState extruder temperature and target", get_name());
    spdlog::debug("[{}] Subscribed to PrinterState print state/progress/time/thumbnail",
                  get_name());

    // Subscribe to filament runout for idle modal
    auto& fsm = helix::FilamentSensorManager::instance();
    filament_runout_observer_ = observe_int_sync<HomePanel>(
        fsm.get_any_runout_subject(), this, [](HomePanel* self, int any_runout) {
            spdlog::debug("[{}] Filament runout subject changed: {}", self->get_name(), any_runout);
            if (any_runout == 1) {
                self->check_and_show_idle_runout_modal();
            } else {
                self->runout_modal_shown_ = false;
            }
        });
    spdlog::debug("[{}] Subscribed to filament_any_runout subject", get_name());

    // Load configured LEDs from wizard settings and tell PrinterState to track them
    Config* config = Config::get_instance();
    if (config) {
        // Load configured LEDs (multi-LED support)
        auto& leds_json = config->get_json(helix::wizard::LED_SELECTED);
        if (leds_json.is_array()) {
            for (const auto& led : leds_json) {
                if (led.is_string() && !led.get<std::string>().empty()) {
                    configured_leds_.push_back(led.get<std::string>());
                }
            }
        }
        // Fallback: check legacy single LED path
        if (configured_leds_.empty()) {
            std::string single_led = config->get<std::string>(helix::wizard::LED_STRIP, "");
            if (!single_led.empty()) {
                configured_leds_.push_back(single_led);
            }
        }
        if (!configured_leds_.empty()) {
            // Tell PrinterState to track the first LED for state updates
            printer_state_.set_tracked_led(configured_leds_.front());

            // Subscribe to LED state changes from PrinterState
            led_state_observer_ = observe_int_sync<HomePanel>(
                printer_state_.get_led_state_subject(), this,
                [](HomePanel* self, int state) { self->on_led_state_changed(state); });

            // Subscribe to LED brightness changes for dynamic icon updates
            led_brightness_observer_ = observe_int_sync<HomePanel>(
                printer_state_.get_led_brightness_subject(), this,
                [](HomePanel* self, int /*brightness*/) { self->update_light_icon(); });

            spdlog::debug("[{}] Configured {} LED(s) (observing state)", get_name(),
                          configured_leds_.size());
        } else {
            spdlog::debug("[{}] No LED configured - light control will be hidden", get_name());
        }
    }
}

HomePanel::~HomePanel() {
    // Deinit subjects FIRST - disconnects observers before subject memory is freed
    // This prevents crashes during lv_deinit() when widgets try to unsubscribe
    deinit_subjects();

    // ObserverGuard handles observer cleanup automatically

    // Clean up timers and animations - must be deleted explicitly before LVGL shutdown
    // Check lv_is_initialized() to avoid crash during static destruction
    if (lv_is_initialized()) {
        // Stop tip fade animations (var=this, not an lv_obj_t*, so lv_obj_delete won't clean them)
        lv_anim_delete(this, nullptr);

        if (signal_poll_timer_) {
            lv_timer_delete(signal_poll_timer_);
            signal_poll_timer_ = nullptr;
        }
        if (tip_rotation_timer_) {
            lv_timer_delete(tip_rotation_timer_);
            tip_rotation_timer_ = nullptr;
        }
    }
}

void HomePanel::init_subjects() {
    using helix::ui::observe_int_sync;

    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize subjects with default values
    // Note: LED state (led_state) is managed by PrinterState and already registered
    UI_MANAGED_SUBJECT_STRING(status_subject_, status_buffer_, "Welcome to HelixScreen",
                              "status_text", subjects_);
    UI_MANAGED_SUBJECT_STRING(temp_subject_, temp_buffer_, "— °C", "temp_text", subjects_);

    // Network icon state: integer 0-5 for conditional icon visibility
    // 0=disconnected, 1-4=wifi strength, 5=ethernet
    // Note: Uses unique name to avoid conflict with navigation_bar's network_icon_state
    UI_MANAGED_SUBJECT_INT(network_icon_state_, 0, "home_network_icon_state", subjects_);

    UI_MANAGED_SUBJECT_STRING(network_label_subject_, network_label_buffer_, "WiFi",
                              "network_label", subjects_);

    // Printer type and host - two subjects for flexible XML layout
    UI_MANAGED_SUBJECT_STRING(printer_type_subject_, printer_type_buffer_, "", "printer_type_text",
                              subjects_);
    UI_MANAGED_SUBJECT_STRING(printer_host_subject_, printer_host_buffer_, "", "printer_host_text",
                              subjects_);
    UI_MANAGED_SUBJECT_INT(printer_info_visible_, 0, "printer_info_visible", subjects_);

    // Register event callbacks BEFORE loading XML
    // Note: These use static trampolines that will look up the global instance
    lv_xml_register_event_cb(nullptr, "light_toggle_cb", light_toggle_cb);
    lv_xml_register_event_cb(nullptr, "print_card_clicked_cb", print_card_clicked_cb);
    lv_xml_register_event_cb(nullptr, "tip_text_clicked_cb", tip_text_clicked_cb);
    lv_xml_register_event_cb(nullptr, "temp_clicked_cb", temp_clicked_cb);
    lv_xml_register_event_cb(nullptr, "printer_status_clicked_cb", printer_status_clicked_cb);
    lv_xml_register_event_cb(nullptr, "network_clicked_cb", network_clicked_cb);
    lv_xml_register_event_cb(nullptr, "printer_manager_clicked_cb", printer_manager_clicked_cb);
    lv_xml_register_event_cb(nullptr, "ams_clicked_cb", ams_clicked_cb);

    // Computed subject for filament status visibility:
    // Show when sensors exist AND (no AMS OR bypass active)
    // NOTE: Must be initialized BEFORE creating observers that call
    // update_filament_status_visibility()
    UI_MANAGED_SUBJECT_INT(show_filament_status_, 0, "show_filament_status", subjects_);

    // Subscribe to AmsState slot_count to show/hide AMS indicator
    // AmsState::init_subjects() is called in main.cpp before us
    // NOTE: Observer callback may fire immediately - show_filament_status_ must be initialized
    // first
    ams_slot_count_observer_ = observe_int_sync<HomePanel>(
        AmsState::instance().get_slot_count_subject(), this,
        [](HomePanel* self, int slot_count) { self->update_ams_indicator(slot_count); });

    // Observe inputs that affect filament status visibility
    ams_bypass_observer_ = observe_int_sync<HomePanel>(
        AmsState::instance().get_bypass_active_subject(), this,
        [](HomePanel* self, int /*bypass*/) { self->update_filament_status_visibility(); });
    filament_sensor_count_observer_ = observe_int_sync<HomePanel>(
        helix::FilamentSensorManager::instance().get_sensor_count_subject(), this,
        [](HomePanel* self, int /*count*/) { self->update_filament_status_visibility(); });

    // Compute initial visibility (observers may have already fired, but this ensures correct
    // initial state)
    update_filament_status_visibility();

    subjects_initialized_ = true;
    spdlog::debug("[{}] Registered subjects and event callbacks", get_name());

    // Set initial tip of the day
    update_tip_of_day();
}

void HomePanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    // SubjectManager handles all lv_subject_deinit() calls via RAII
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[{}] Subjects deinitialized", get_name());
}

void HomePanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up...", get_name());

    // Widget visibility (light button/divider) is handled by XML bindings to printer_has_led
    // subject Printer image opacity is handled by XML styles bound to printer_connection_state
    // subject No C++ widget manipulation needed - everything is declarative

    // Attach heating icon animator (gradient color + pulse while heating)
    lv_obj_t* temp_icon = lv_obj_find_by_name(panel_, "temp_icon");
    if (temp_icon) {
        temp_icon_animator_.attach(temp_icon);
        // Initialize with cached values (observers may have already fired)
        cached_extruder_temp_ = lv_subject_get_int(printer_state_.get_extruder_temp_subject());
        cached_extruder_target_ = lv_subject_get_int(printer_state_.get_extruder_target_subject());
        temp_icon_animator_.update(cached_extruder_temp_, cached_extruder_target_);
        spdlog::debug("[{}] Heating icon animator attached", get_name());
    }

    // Find light icon for dynamic brightness/color updates
    light_icon_ = lv_obj_find_by_name(panel_, "light_icon");
    if (light_icon_) {
        spdlog::debug("[{}] Found light_icon for dynamic brightness/color", get_name());
        update_light_icon(); // Initialize with current state
    }

    // AMS mini status is now created declaratively via XML <ams_mini_status/>

    // Cache tip label for fade animation
    tip_label_ = lv_obj_find_by_name(panel_, "status_text_label");
    if (!tip_label_) {
        spdlog::warn("[{}] Could not find status_text_label for tip animation", get_name());
    }

    // Start tip rotation timer (60 seconds = 60000ms)
    if (!tip_rotation_timer_) {
        tip_rotation_timer_ = lv_timer_create(tip_rotation_timer_cb, 60000, this);
        spdlog::debug("[{}] Started tip rotation timer (60s interval)", get_name());
    }

    // Use global WiFiManager for signal strength queries
    if (!wifi_manager_) {
        wifi_manager_ = get_wifi_manager();
    }

    // Initialize EthernetManager for Ethernet status detection
    if (!ethernet_manager_) {
        ethernet_manager_ = std::make_unique<EthernetManager>();
        spdlog::debug("[{}] EthernetManager initialized for connection detection", get_name());
    }

    // Detect actual network type (Ethernet vs WiFi vs disconnected)
    // This sets current_network_ and updates the icon state accordingly
    detect_network_type();

    // Start signal polling timer if on WiFi
    if (!signal_poll_timer_ && current_network_ == NETWORK_WIFI) {
        signal_poll_timer_ = lv_timer_create(signal_poll_timer_cb, SIGNAL_POLL_INTERVAL_MS, this);
        spdlog::debug("[{}] Started signal polling timer ({}ms)", get_name(),
                      SIGNAL_POLL_INTERVAL_MS);
    }

    // Load printer image from config (if available)
    reload_from_config();

    // Check initial AMS state and show indicator if AMS is already available
    // (The observer may have fired before panel_ was set during init_subjects)
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    if (slot_count > 0) {
        update_ams_indicator(slot_count);
    }

    // Look up print card widgets for dynamic updates during printing
    print_card_thumb_ = lv_obj_find_by_name(panel_, "print_card_thumb");
    print_card_active_thumb_ = lv_obj_find_by_name(panel_, "print_card_active_thumb");
    print_card_label_ = lv_obj_find_by_name(panel_, "print_card_label");
    if (print_card_thumb_ && print_card_active_thumb_ && print_card_label_) {
        spdlog::debug("[{}] Found print card widgets for dynamic updates", get_name());

        // Check initial print state (observer may have fired before setup)
        auto state = static_cast<PrintJobState>(
            lv_subject_get_int(printer_state_.get_print_state_enum_subject()));
        if (state == PrintJobState::PRINTING || state == PrintJobState::PAUSED) {
            // Already printing - load thumbnail and update label
            on_print_state_changed(state);
        }
    }

    // Register plugin injection point for home panel widgets
    lv_obj_t* widget_area = lv_obj_find_by_name(panel_, "home_widget_area");
    if (widget_area) {
        helix::plugin::InjectionPointManager::instance().register_point("home_widget_area",
                                                                        widget_area);
        spdlog::debug("[{}] Registered injection point: home_widget_area", get_name());
    }

    spdlog::debug("[{}] Setup complete!", get_name());
}

void HomePanel::on_activate() {
    // Re-detect network type in case it changed while on another panel
    detect_network_type();

    // Start signal polling timer when panel becomes visible (only for WiFi)
    if (!signal_poll_timer_ && current_network_ == NETWORK_WIFI) {
        signal_poll_timer_ = lv_timer_create(signal_poll_timer_cb, SIGNAL_POLL_INTERVAL_MS, this);
        spdlog::debug("[{}] Started signal polling timer ({}ms interval)", get_name(),
                      SIGNAL_POLL_INTERVAL_MS);
    }

    // Resume tip rotation timer when panel becomes visible
    if (!tip_rotation_timer_) {
        tip_rotation_timer_ = lv_timer_create(tip_rotation_timer_cb, 60000, this);
        spdlog::debug("[{}] Resumed tip rotation timer", get_name());
    }

    // Start Spoolman polling for AMS mini status updates
    AmsState::instance().start_spoolman_polling();
}

void HomePanel::on_deactivate() {
    AmsState::instance().stop_spoolman_polling();

    // Stop signal polling timer when panel is hidden (saves CPU)
    if (signal_poll_timer_) {
        lv_timer_delete(signal_poll_timer_);
        signal_poll_timer_ = nullptr;
        spdlog::debug("[{}] Stopped signal polling timer", get_name());
    }

    // Stop tip rotation timer when panel is hidden (saves CPU)
    if (tip_rotation_timer_) {
        lv_timer_delete(tip_rotation_timer_);
        tip_rotation_timer_ = nullptr;
        spdlog::debug("[{}] Stopped tip rotation timer", get_name());
    }
}

void HomePanel::update_tip_of_day() {
    auto tip = TipsManager::get_instance()->get_random_unique_tip();

    if (!tip.title.empty()) {
        // Use animated transition if label is available and not already animating
        if (tip_label_ && !tip_animating_) {
            start_tip_fade_transition(tip);
        } else {
            // Fallback: instant update (initial load or animation in progress)
            current_tip_ = tip;
            std::snprintf(status_buffer_, sizeof(status_buffer_), "%s", tip.title.c_str());
            lv_subject_copy_string(&status_subject_, status_buffer_);
            spdlog::trace("[{}] Updated tip (instant): {}", get_name(), tip.title);
        }
    } else {
        spdlog::warn("[{}] Failed to get tip, keeping current", get_name());
    }
}

// Animation duration constants
static constexpr uint32_t TIP_FADE_DURATION_MS = 300;

void HomePanel::start_tip_fade_transition(const PrintingTip& new_tip) {
    if (!tip_label_ || tip_animating_) {
        return;
    }

    // Store the pending tip to apply after fade-out
    pending_tip_ = new_tip;
    tip_animating_ = true;

    spdlog::debug("[{}] Starting tip fade transition to: {}", get_name(), new_tip.title);

    // Skip animation if disabled - apply text immediately
    if (!SettingsManager::instance().get_animations_enabled()) {
        current_tip_ = pending_tip_;
        std::snprintf(status_buffer_, sizeof(status_buffer_), "%s", pending_tip_.title.c_str());
        lv_subject_copy_string(&status_subject_, status_buffer_);
        lv_obj_set_style_opa(tip_label_, LV_OPA_COVER, LV_PART_MAIN);
        tip_animating_ = false;
        spdlog::debug("[{}] Animations disabled - applied tip instantly", get_name());
        return;
    }

    // Fade out animation
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, 255, 0);
    lv_anim_set_duration(&anim, TIP_FADE_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);

    // Execute callback: update opacity on each frame
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
        auto* self = static_cast<HomePanel*>(var);
        if (self->tip_label_) {
            lv_obj_set_style_opa(self->tip_label_, static_cast<lv_opa_t>(value), LV_PART_MAIN);
        }
    });

    // Completion callback: apply new text and start fade-in
    lv_anim_set_completed_cb(&anim, [](lv_anim_t* a) {
        auto* self = static_cast<HomePanel*>(a->var);
        self->apply_pending_tip();
    });

    lv_anim_start(&anim);
}

void HomePanel::apply_pending_tip() {
    // Apply the pending tip text
    current_tip_ = pending_tip_;
    std::snprintf(status_buffer_, sizeof(status_buffer_), "%s", pending_tip_.title.c_str());
    lv_subject_copy_string(&status_subject_, status_buffer_);

    spdlog::debug("[{}] Applied pending tip: {}", get_name(), pending_tip_.title);

    // Skip animation if disabled - show at full opacity immediately
    if (!SettingsManager::instance().get_animations_enabled()) {
        if (tip_label_) {
            lv_obj_set_style_opa(tip_label_, LV_OPA_COVER, LV_PART_MAIN);
        }
        tip_animating_ = false;
        return;
    }

    // Fade in animation
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, 0, 255);
    lv_anim_set_duration(&anim, TIP_FADE_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);

    // Execute callback: update opacity on each frame
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
        auto* self = static_cast<HomePanel*>(var);
        if (self->tip_label_) {
            lv_obj_set_style_opa(self->tip_label_, static_cast<lv_opa_t>(value), LV_PART_MAIN);
        }
    });

    // Completion callback: mark animation as done
    lv_anim_set_completed_cb(&anim, [](lv_anim_t* a) {
        auto* self = static_cast<HomePanel*>(a->var);
        self->tip_animating_ = false;
    });

    lv_anim_start(&anim);
}

void HomePanel::detect_network_type() {
    // Priority: Ethernet > WiFi > Disconnected
    // This ensures users on wired connections see the Ethernet icon even if WiFi is also available

    // Check Ethernet first (higher priority - more reliable connection)
    if (ethernet_manager_) {
        EthernetInfo eth_info = ethernet_manager_->get_info();
        if (eth_info.connected) {
            spdlog::debug("[{}] Detected Ethernet connection on {} ({})", get_name(),
                          eth_info.interface, eth_info.ip_address);
            set_network(NETWORK_ETHERNET);
            return;
        }
    }

    // Check WiFi second
    if (wifi_manager_ && wifi_manager_->is_connected()) {
        spdlog::info("[{}] Detected WiFi connection ({})", get_name(),
                     wifi_manager_->get_connected_ssid());
        set_network(NETWORK_WIFI);
        return;
    }

    // Neither connected
    spdlog::info("[{}] No network connection detected", get_name());
    set_network(NETWORK_DISCONNECTED);
}

void HomePanel::handle_light_toggle() {
    spdlog::info("[{}] Light button clicked", get_name());

    // Check if LED is configured
    if (configured_leds_.empty()) {
        spdlog::warn("[{}] Light toggle called but no LED configured", get_name());
        return;
    }

    // Toggle to opposite of current state
    // Note: UI will update when Moonraker notification arrives (via PrinterState observer)
    bool new_state = !light_on_;

    // Send command to Moonraker for all configured LEDs
    if (api_) {
        for (const auto& led : configured_leds_) {
            if (new_state) {
                api_->set_led_on(
                    led,
                    [this]() {
                        spdlog::info("[{}] LED turned ON - waiting for state update", get_name());
                    },
                    [](const MoonrakerError& err) {
                        spdlog::error("[Home Panel] Failed to turn LED on: {}", err.message);
                        NOTIFY_ERROR("Failed to turn light on: {}", err.user_message());
                    });
            } else {
                api_->set_led_off(
                    led,
                    [this]() {
                        spdlog::info("[{}] LED turned OFF - waiting for state update", get_name());
                    },
                    [](const MoonrakerError& err) {
                        spdlog::error("[Home Panel] Failed to turn LED off: {}", err.message);
                        NOTIFY_ERROR("Failed to turn light off: {}", err.user_message());
                    });
            }
        }
    } else {
        spdlog::warn("[{}] API not available - cannot control LED", get_name());
        NOTIFY_ERROR("Cannot control light: printer not connected");
    }
}

void HomePanel::handle_print_card_clicked() {
    // Check if a print is in progress
    if (!printer_state_.can_start_new_print()) {
        // Print in progress - show print status overlay
        spdlog::info("[{}] Print card clicked - showing print status (print in progress)",
                     get_name());

        extern PrintStatusPanel& get_global_print_status_panel();
        lv_obj_t* status_panel = get_global_print_status_panel().get_panel();
        if (status_panel) {
            NavigationManager::instance().register_overlay_instance(
                status_panel, &get_global_print_status_panel());
            ui_nav_push_overlay(status_panel);
        } else {
            spdlog::error("[{}] Print status panel not available", get_name());
        }
    } else {
        // No print in progress - navigate to print select panel
        spdlog::info("[{}] Print card clicked - navigating to print select panel", get_name());
        ui_nav_set_active(UI_PANEL_PRINT_SELECT);
    }
}

void HomePanel::handle_tip_text_clicked() {
    if (current_tip_.title.empty()) {
        spdlog::warn("[{}] No tip available to display", get_name());
        return;
    }

    spdlog::info("[{}] Tip text clicked - showing detail dialog", get_name());

    // Use alert helper which auto-handles OK button to close
    ui_modal_show_alert(current_tip_.title.c_str(), current_tip_.content.c_str(),
                        ModalSeverity::Info);
}

void HomePanel::handle_tip_rotation_timer() {
    update_tip_of_day();
}

void HomePanel::set_temp_control_panel(TempControlPanel* temp_panel) {
    temp_control_panel_ = temp_panel;
    spdlog::trace("[{}] TempControlPanel reference set", get_name());
}

void HomePanel::handle_temp_clicked() {
    spdlog::info("[{}] Temperature icon clicked - opening nozzle temp panel", get_name());

    if (!temp_control_panel_) {
        spdlog::error("[{}] TempControlPanel not initialized", get_name());
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create nozzle temp panel on first access (lazy initialization)
    if (!nozzle_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating nozzle temperature panel...", get_name());

        // Create from XML
        nozzle_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            // Setup via injected TempControlPanel
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);
            NavigationManager::instance().register_overlay_instance(
                nozzle_temp_panel_, temp_control_panel_->get_nozzle_lifecycle());

            // Initially hidden
            lv_obj_add_flag(nozzle_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Nozzle temp panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create nozzle temp panel from XML", get_name());
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    // Push nozzle temp panel onto navigation history and show it
    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void HomePanel::handle_printer_status_clicked() {
    spdlog::info("[{}] Printer status icon clicked - navigating to advanced settings", get_name());

    // Navigate to advanced settings panel
    ui_nav_set_active(UI_PANEL_ADVANCED);
}

void HomePanel::handle_network_clicked() {
    spdlog::info("[{}] Network icon clicked - opening network settings directly", get_name());

    // Open Network settings overlay directly (same as Settings panel's Network row)
    auto& overlay = get_network_settings_overlay();

    if (!overlay.is_created()) {
        overlay.init_subjects();
        overlay.register_callbacks();
        overlay.create(parent_screen_);
    }

    overlay.show();
}

void HomePanel::handle_printer_manager_clicked() {
    // Gate behind beta features flag
    Config* config = Config::get_instance();
    if (!config || !config->is_beta_features_enabled()) {
        spdlog::debug("[{}] Printer Manager requires beta features", get_name());
        return;
    }

    spdlog::info("[{}] Printer image clicked - opening Printer Manager overlay", get_name());

    auto& overlay = get_printer_manager_overlay();

    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
        overlay.create(parent_screen_);
        NavigationManager::instance().register_overlay_instance(overlay.get_root(), &overlay);
    }

    // Push overlay onto navigation stack
    ui_nav_push_overlay(overlay.get_root());
}

void HomePanel::handle_ams_clicked() {
    spdlog::info("[{}] AMS indicator clicked - opening AMS panel overlay", get_name());

    // Open AMS panel overlay for multi-filament management
    auto& ams_panel = get_global_ams_panel();
    if (!ams_panel.are_subjects_initialized()) {
        ams_panel.init_subjects();
    }
    lv_obj_t* panel_obj = ams_panel.get_panel();
    if (panel_obj) {
        ui_nav_push_overlay(panel_obj);
    }
}

void HomePanel::on_led_state_changed(int state) {
    // Update local light_on_ state from PrinterState's led_state subject
    light_on_ = (state != 0);

    spdlog::debug("[{}] LED state changed: {} (from PrinterState)", get_name(),
                  light_on_ ? "ON" : "OFF");

    // Update light icon when state changes
    update_light_icon();
}

void HomePanel::update_light_icon() {
    if (!light_icon_) {
        return;
    }

    // Get current brightness
    int brightness = lv_subject_get_int(printer_state_.get_led_brightness_subject());

    // Set icon based on brightness level
    const char* icon_name = ui_brightness_to_lightbulb_icon(brightness);
    ui_icon_set_source(light_icon_, icon_name);

    // Calculate icon color from LED RGBW values
    if (brightness == 0) {
        // OFF state - use muted gray from design tokens
        ui_icon_set_color(light_icon_, theme_manager_get_color("light_icon_off"), LV_OPA_COVER);
    } else {
        // Get RGB values from PrinterState
        int r = lv_subject_get_int(printer_state_.get_led_r_subject());
        int g = lv_subject_get_int(printer_state_.get_led_g_subject());
        int b = lv_subject_get_int(printer_state_.get_led_b_subject());
        int w = lv_subject_get_int(printer_state_.get_led_w_subject());

        lv_color_t icon_color;
        // If white channel dominant or RGB near white, use gold from design tokens
        if (w > std::max({r, g, b}) || (r > 200 && g > 200 && b > 200)) {
            icon_color = theme_manager_get_color("light_icon_on");
        } else {
            // Use actual LED color, boost if too dark for visibility
            int max_val = std::max({r, g, b});
            if (max_val < 128 && max_val > 0) {
                float scale = 128.0f / static_cast<float>(max_val);
                icon_color =
                    lv_color_make(static_cast<uint8_t>(std::min(255, static_cast<int>(r * scale))),
                                  static_cast<uint8_t>(std::min(255, static_cast<int>(g * scale))),
                                  static_cast<uint8_t>(std::min(255, static_cast<int>(b * scale))));
            } else {
                icon_color = lv_color_make(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                           static_cast<uint8_t>(b));
            }
        }

        ui_icon_set_color(light_icon_, icon_color, LV_OPA_COVER);
    }

    spdlog::trace("[{}] Light icon: {} at {}%", get_name(), icon_name, brightness);
}

void HomePanel::on_extruder_temp_changed(int temp_centi) {
    int temp_deg = centi_to_degrees(temp_centi);

    // Format temperature for display and update the string subject
    // Guard: Observer callback fires during constructor before init_subjects()
    helix::fmt::format_temp(temp_deg, temp_buffer_, sizeof(temp_buffer_));
    if (subjects_initialized_) {
        lv_subject_copy_string(&temp_subject_, temp_buffer_);
    }

    // Update cached value and animator (animator expects centidegrees)
    cached_extruder_temp_ = temp_centi;
    update_temp_icon_animation();

    spdlog::trace("[{}] Extruder temperature updated: {}°C", get_name(), temp_deg);
}

void HomePanel::on_extruder_target_changed(int target_centi) {
    // Animator expects centidegrees
    cached_extruder_target_ = target_centi;
    update_temp_icon_animation();
    spdlog::trace("[{}] Extruder target updated: {}°C", get_name(), centi_to_degrees(target_centi));
}

void HomePanel::update_temp_icon_animation() {
    temp_icon_animator_.update(cached_extruder_temp_, cached_extruder_target_);
}

void HomePanel::reload_from_config() {
    using helix::ui::observe_int_sync;

    Config* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[{}] reload_from_config: Config not available", get_name());
        return;
    }

    // Reload LED configuration from wizard settings
    // LED visibility is controlled by printer_has_led subject set via set_printer_capabilities()
    // which is called by the on_discovery_complete_ callback after hardware discovery
    std::vector<std::string> new_leds;
    auto& leds_json = config->get_json(helix::wizard::LED_SELECTED);
    if (leds_json.is_array()) {
        for (const auto& led : leds_json) {
            if (led.is_string() && !led.get<std::string>().empty()) {
                new_leds.push_back(led.get<std::string>());
            }
        }
    }
    // Fallback: check legacy single LED path
    if (new_leds.empty()) {
        std::string single_led = config->get<std::string>(helix::wizard::LED_STRIP, "");
        if (!single_led.empty()) {
            new_leds.push_back(single_led);
        }
    }
    if (new_leds != configured_leds_) {
        configured_leds_ = new_leds;
        if (!configured_leds_.empty() && configured_leds_.front() != "None") {
            // Tell PrinterState to track the first LED for state updates
            printer_state_.set_tracked_led(configured_leds_.front());

            // Subscribe to LED state changes if not already subscribed
            if (!led_state_observer_) {
                led_state_observer_ = observe_int_sync<HomePanel>(
                    printer_state_.get_led_state_subject(), this,
                    [](HomePanel* self, int state) { self->on_led_state_changed(state); });
            }

            // Subscribe to LED brightness changes if not already subscribed
            if (!led_brightness_observer_) {
                led_brightness_observer_ = observe_int_sync<HomePanel>(
                    printer_state_.get_led_brightness_subject(), this,
                    [](HomePanel* self, int /*brightness*/) { self->update_light_icon(); });
            }

            spdlog::info("[{}] Reloaded LED config: {} LED(s)", get_name(),
                         configured_leds_.size());
        } else {
            // No LED configured - clear tracking
            printer_state_.set_tracked_led("");
            spdlog::debug("[{}] LED config cleared", get_name());
        }
    }

    // Update printer type in PrinterState (triggers capability cache refresh)
    std::string printer_type = config->get<std::string>(helix::wizard::PRINTER_TYPE, "");
    printer_state_.set_printer_type_sync(printer_type);

    // Update printer image based on configured printer type
    if (!printer_type.empty() && panel_) {
        std::string image_path = PrinterImages::get_best_printer_image(printer_type);
        lv_obj_t* printer_image = lv_obj_find_by_name(panel_, "printer_image");
        if (printer_image) {
            lv_image_set_src(printer_image, image_path.c_str());
            spdlog::debug("[{}] Printer image: '{}' for '{}'", get_name(), image_path,
                          printer_type);
        }
    }

    // Update printer type/host overlay
    // Always visible (even for localhost) to maintain consistent flex layout.
    // Hidden flag removes elements from flex, causing printer image to scale differently.
    std::string host = config->get<std::string>(helix::wizard::MOONRAKER_HOST, "");

    if (host.empty() || host == "127.0.0.1" || host == "localhost") {
        // Space keeps the text_small at its font height for consistent layout
        std::strncpy(printer_type_buffer_, " ", sizeof(printer_type_buffer_) - 1);
        lv_subject_copy_string(&printer_type_subject_, printer_type_buffer_);
        lv_subject_set_int(&printer_info_visible_, 1);
    } else {
        std::strncpy(printer_type_buffer_, printer_type.empty() ? "Printer" : printer_type.c_str(),
                     sizeof(printer_type_buffer_) - 1);
        std::strncpy(printer_host_buffer_, host.c_str(), sizeof(printer_host_buffer_) - 1);
        lv_subject_copy_string(&printer_type_subject_, printer_type_buffer_);
        lv_subject_copy_string(&printer_host_subject_, printer_host_buffer_);
        lv_subject_set_int(&printer_info_visible_, 1);
    }
}

void HomePanel::light_toggle_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] light_toggle_cb");
    (void)e;
    // XML-registered callbacks don't have user_data set to 'this'
    // Use the global instance via legacy API bridge
    // This will be fixed when main.cpp switches to class-based instantiation
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_light_toggle();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::print_card_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] print_card_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_print_card_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::tip_text_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] tip_text_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_tip_text_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::temp_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] temp_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_temp_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::printer_status_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] printer_status_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_printer_status_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::network_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] network_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_network_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::printer_manager_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] printer_manager_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_printer_manager_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::ams_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] ams_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_ams_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::tip_rotation_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<HomePanel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->handle_tip_rotation_timer();
    }
}

void HomePanel::update(const char* status_text, int temp) {
    // Update subjects - all bound widgets update automatically
    if (status_text) {
        lv_subject_copy_string(&status_subject_, status_text);
        spdlog::debug("[{}] Updated status_text subject to: {}", get_name(), status_text);
    }

    char buf[32];
    helix::fmt::format_temp(temp, buf, sizeof(buf));
    lv_subject_copy_string(&temp_subject_, buf);
    spdlog::debug("[{}] Updated temp_text subject to: {}", get_name(), buf);
}

void HomePanel::set_network(network_type_t type) {
    current_network_ = type;

    // Update label text
    switch (type) {
    case NETWORK_WIFI:
        lv_subject_copy_string(&network_label_subject_, "WiFi");
        break;
    case NETWORK_ETHERNET:
        lv_subject_copy_string(&network_label_subject_, "Ethernet");
        break;
    case NETWORK_DISCONNECTED:
        lv_subject_copy_string(&network_label_subject_, "Disconnected");
        break;
    }

    // Update the icon state (will query WiFi signal strength if connected)
    update_network_icon_state();

    spdlog::debug("[{}] Network type set to {} (icon state will be computed)", get_name(),
                  static_cast<int>(type));
}

int HomePanel::compute_network_icon_state() {
    // State values:
    // 0 = Disconnected (wifi_off, disabled variant)
    // 1 = WiFi strength 1 (≤25%, warning variant)
    // 2 = WiFi strength 2 (26-50%, accent variant)
    // 3 = WiFi strength 3 (51-75%, accent variant)
    // 4 = WiFi strength 4 (>75%, accent variant)
    // 5 = Ethernet connected (accent variant)

    if (current_network_ == NETWORK_DISCONNECTED) {
        spdlog::trace("[{}] Network disconnected -> state 0", get_name());
        return 0;
    }

    if (current_network_ == NETWORK_ETHERNET) {
        spdlog::trace("[{}] Network ethernet -> state 5", get_name());
        return 5;
    }

    // WiFi - get signal strength from WiFiManager
    int signal = 0;
    if (wifi_manager_) {
        signal = wifi_manager_->get_signal_strength();
        spdlog::trace("[{}] WiFi signal strength: {}%", get_name(), signal);
    } else {
        spdlog::warn("[{}] WiFiManager not available for signal query", get_name());
    }

    // Map signal percentage to icon state (1-4)
    int state;
    if (signal <= 25)
        state = 1; // Weak (warning)
    else if (signal <= 50)
        state = 2; // Fair
    else if (signal <= 75)
        state = 3; // Good
    else
        state = 4; // Strong

    spdlog::trace("[{}] WiFi signal {}% -> state {}", get_name(), signal, state);
    return state;
}

void HomePanel::update_network_icon_state() {
    int new_state = compute_network_icon_state();
    int old_state = lv_subject_get_int(&network_icon_state_);

    if (new_state != old_state) {
        lv_subject_set_int(&network_icon_state_, new_state);
        spdlog::debug("[{}] Network icon state: {} -> {}", get_name(), old_state, new_state);
    }
}

void HomePanel::signal_poll_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<HomePanel*>(lv_timer_get_user_data(timer));
    if (self && self->current_network_ == NETWORK_WIFI) {
        self->update_network_icon_state();
    }
}

void HomePanel::set_light(bool is_on) {
    // Note: The actual LED state is managed by PrinterState via Moonraker notifications.
    // This method is only used for local state updates when API is unavailable.
    light_on_ = is_on;
    spdlog::debug("[{}] Local light state: {}", get_name(), is_on ? "ON" : "OFF");
}

void HomePanel::update_ams_indicator(int /* slot_count */) {
    // AMS mini status widget auto-updates via observers bound to AmsState.
    // This method only needs to update filament status visibility.
    update_filament_status_visibility();
}

void HomePanel::update_filament_status_visibility() {
    // Computed subject: show filament status when sensors exist AND (no AMS OR bypass active)
    int sensor_count =
        lv_subject_get_int(helix::FilamentSensorManager::instance().get_sensor_count_subject());
    int ams_slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    int bypass_active = lv_subject_get_int(AmsState::instance().get_bypass_active_subject());

    bool has_sensors = sensor_count > 0;
    bool has_ams = ams_slot_count > 0;
    bool in_bypass = bypass_active != 0;

    // Show filament status if: has sensors AND (no AMS OR bypass mode active)
    bool show = has_sensors && (!has_ams || in_bypass);
    int new_value = show ? 1 : 0;

    if (lv_subject_get_int(&show_filament_status_) != new_value) {
        lv_subject_set_int(&show_filament_status_, new_value);
        spdlog::debug("[{}] Filament status visibility: {} (sensors={}, ams={}, bypass={})",
                      get_name(), show ? "shown" : "hidden", sensor_count, ams_slot_count,
                      bypass_active);
    }
}

// ============================================================================
// PRINT CARD DYNAMIC UPDATES
// ============================================================================

void HomePanel::on_print_thumbnail_path_changed(const char* path) {
    if (!print_card_active_thumb_) {
        return;
    }

    // Defer the image update to avoid LVGL assertion when called during render
    // (observer callbacks can fire during subject updates which may be mid-render)
    std::string path_copy = path ? path : "";
    ui_async_call(
        [](void* user_data) {
            auto* self = static_cast<HomePanel*>(user_data);
            // Guard against async callback firing after display destruction
            if (!self->print_card_active_thumb_ ||
                !lv_obj_is_valid(self->print_card_active_thumb_)) {
                return;
            }

            const char* current_path =
                lv_subject_get_string(self->printer_state_.get_print_thumbnail_path_subject());

            if (current_path && current_path[0] != '\0') {
                // Thumbnail available - set it on the active print card
                lv_image_set_src(self->print_card_active_thumb_, current_path);
                spdlog::debug("[{}] Active print thumbnail updated: {}", self->get_name(),
                              current_path);
            } else {
                // No thumbnail - revert to benchy placeholder
                lv_image_set_src(self->print_card_active_thumb_,
                                 "A:assets/images/benchy_thumbnail_white.png");
                spdlog::debug("[{}] Active print thumbnail cleared", self->get_name());
            }
        },
        this);
}

void HomePanel::on_print_state_changed(PrintJobState state) {
    if (!print_card_thumb_ || !print_card_label_) {
        return; // Widgets not found (shouldn't happen after setup)
    }

    bool is_active = (state == PrintJobState::PRINTING || state == PrintJobState::PAUSED);

    if (is_active) {
        spdlog::debug("[{}] Print active - updating card progress display", get_name());
        update_print_card_from_state(); // Update label immediately
    } else {
        spdlog::debug("[{}] Print not active - reverting card to idle state", get_name());
        reset_print_card_to_idle();
    }
}

void HomePanel::on_print_progress_or_time_changed() {
    update_print_card_from_state();
}

void HomePanel::update_print_card_from_state() {
    auto state = static_cast<PrintJobState>(
        lv_subject_get_int(printer_state_.get_print_state_enum_subject()));

    // Only update if actively printing
    if (state != PrintJobState::PRINTING && state != PrintJobState::PAUSED) {
        return;
    }

    int progress = lv_subject_get_int(printer_state_.get_print_progress_subject());
    int time_left = lv_subject_get_int(printer_state_.get_print_time_left_subject());

    update_print_card_label(progress, time_left);
}

void HomePanel::update_print_card_label(int progress, int time_left_secs) {
    if (!print_card_label_) {
        return;
    }

    char buf[64];
    int hours = time_left_secs / 3600;
    int minutes = (time_left_secs % 3600) / 60;

    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%d%% \u2022 %dh %02dm left", progress, hours, minutes);
    } else if (minutes > 0) {
        snprintf(buf, sizeof(buf), "%d%% \u2022 %dm left", progress, minutes);
    } else {
        snprintf(buf, sizeof(buf), "%d%% \u2022 < 1m left", progress);
    }

    lv_label_set_text(print_card_label_, buf);
}

void HomePanel::reset_print_card_to_idle() {
    // Reset idle thumbnail to benchy (active thumb is handled by observer when path clears)
    if (print_card_thumb_) {
        lv_image_set_src(print_card_thumb_, "A:assets/images/benchy_thumbnail_white.png");
    }
    if (print_card_label_) {
        lv_label_set_text(print_card_label_, "Print Files");
    }
}

// ============================================================================
// Filament Runout Modal
// ============================================================================

void HomePanel::check_and_show_idle_runout_modal() {
    // Grace period - don't show modal during startup
    auto& fsm = helix::FilamentSensorManager::instance();
    if (fsm.is_in_startup_grace_period()) {
        spdlog::debug("[{}] In startup grace period - skipping runout modal", get_name());
        return;
    }

    // Verify actual sensor state — callers may trigger this from stale subject values
    // during discovery races, so always re-check the authoritative sensor state
    if (!fsm.has_any_runout()) {
        spdlog::debug("[{}] No actual runout detected - skipping modal", get_name());
        return;
    }

    // Check suppression logic (AMS without bypass, wizard active, etc.)
    if (!get_runtime_config()->should_show_runout_modal()) {
        spdlog::debug("[{}] Runout modal suppressed by runtime config", get_name());
        return;
    }

    // Only show modal if not already shown
    if (runout_modal_shown_) {
        spdlog::debug("[{}] Runout modal already shown - skipping", get_name());
        return;
    }

    // Only show if printer is idle (not printing/paused)
    int print_state = lv_subject_get_int(printer_state_.get_print_state_enum_subject());
    if (print_state != static_cast<int>(PrintJobState::STANDBY) &&
        print_state != static_cast<int>(PrintJobState::COMPLETE) &&
        print_state != static_cast<int>(PrintJobState::CANCELLED)) {
        spdlog::debug("[{}] Print active (state={}) - skipping idle runout modal", get_name(),
                      print_state);
        return;
    }

    spdlog::info("[{}] Showing idle runout modal", get_name());
    show_idle_runout_modal();
    runout_modal_shown_ = true;
}

void HomePanel::trigger_idle_runout_check() {
    spdlog::debug("[{}] Triggering deferred runout check", get_name());
    runout_modal_shown_ = false; // Allow modal to show again
    check_and_show_idle_runout_modal();
}

void HomePanel::show_idle_runout_modal() {
    if (runout_modal_.is_visible()) {
        return;
    }

    // Configure callbacks for the modal buttons
    runout_modal_.set_on_load_filament([this]() {
        spdlog::info("[{}] User chose to load filament (idle)", get_name());
        ui_nav_set_active(UI_PANEL_FILAMENT);
    });

    runout_modal_.set_on_resume([]() {
        // Resume not applicable when idle, but modal handles this
    });

    runout_modal_.set_on_cancel_print([]() {
        // Cancel not applicable when idle, but modal handles this
    });

    runout_modal_.show(parent_screen_);
}

static std::unique_ptr<HomePanel> g_home_panel;

HomePanel& get_global_home_panel() {
    if (!g_home_panel) {
        g_home_panel = std::make_unique<HomePanel>(get_printer_state(), nullptr);
        StaticPanelRegistry::instance().register_destroy("HomePanel",
                                                         []() { g_home_panel.reset(); });
    }
    return *g_home_panel;
}
