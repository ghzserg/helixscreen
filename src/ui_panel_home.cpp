// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_home.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_fonts.h"
#include "ui_icon.h"
#include "ui_nav.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"

#include "app_globals.h"
#include "config.h"
#include "moonraker_api.h"
#include "printer_images.h"
#include "printer_state.h"
#include "printer_types.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <memory>

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

HomePanel::HomePanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents with default values
    std::strcpy(status_buffer_, "Welcome to HelixScreen");
    std::strcpy(temp_buffer_, "-- °C");
    std::strcpy(network_icon_buffer_, ICON_WIFI);
    std::strcpy(network_label_buffer_, "Wi-Fi");
    std::strcpy(network_color_buffer_, "0xff4444");

    // Subscribe to PrinterState extruder temperature for reactive updates
    extruder_temp_observer_ = lv_subject_add_observer(printer_state_.get_extruder_temp_subject(),
                                                      extruder_temp_observer_cb, this);

    // Subscribe to printer connection state for image dimming
    connection_state_observer_ = lv_subject_add_observer(
        printer_state_.get_printer_connection_state_subject(), connection_state_observer_cb, this);

    spdlog::debug("[{}] Subscribed to PrinterState extruder temperature and connection state",
                  get_name());

    // Load configured LED from wizard settings and tell PrinterState to track it
    Config* config = Config::get_instance();
    if (config) {
        configured_led_ = config->get<std::string>(WizardConfigPaths::LED_STRIP, "");
        if (!configured_led_.empty()) {
            // Tell PrinterState to track this LED for state updates
            printer_state_.set_tracked_led(configured_led_);

            // Subscribe to LED state changes from PrinterState
            led_state_observer_ = lv_subject_add_observer(printer_state_.get_led_state_subject(),
                                                          led_state_observer_cb, this);

            spdlog::info("[{}] Configured LED: {} (observing state)", get_name(), configured_led_);
        } else {
            spdlog::debug("[{}] No LED configured - light control will be hidden", get_name());
        }
    }
}

HomePanel::~HomePanel() {
    // NOTE: Do NOT log or call LVGL functions here! Destructor may be called
    // during static destruction after LVGL and spdlog have already been destroyed.
    // The timer will be cleaned up by LVGL when it shuts down.
    // The tip_modal_ member uses RAII and will auto-hide if visible.
    //
    // If we need explicit cleanup, it should be done in a separate cleanup()
    // method called before exit(), not in the destructor.

    tip_rotation_timer_ = nullptr;

    // RAII cleanup: remove PrinterState observers
    if (extruder_temp_observer_) {
        lv_observer_remove(extruder_temp_observer_);
        extruder_temp_observer_ = nullptr;
    }
    if (led_state_observer_) {
        lv_observer_remove(led_state_observer_);
        led_state_observer_ = nullptr;
    }
    if (connection_state_observer_) {
        lv_observer_remove(connection_state_observer_);
        connection_state_observer_ = nullptr;
    }

    // Other observers cleaned up by PanelBase::~PanelBase()
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void HomePanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize subjects with default values
    // Note: LED state (led_state) is managed by PrinterState and already registered
    UI_SUBJECT_INIT_AND_REGISTER_STRING(status_subject_, status_buffer_, "Welcome to HelixScreen",
                                        "status_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(temp_subject_, temp_buffer_, "-- °C", "temp_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(network_icon_subject_, network_icon_buffer_, ICON_WIFI,
                                        "network_icon");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(network_label_subject_, network_label_buffer_, "Wi-Fi",
                                        "network_label");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(network_color_subject_, network_color_buffer_, "0xff4444",
                                        "network_color");

    // Register event callbacks BEFORE loading XML
    // Note: These use static trampolines that will look up the global instance
    lv_xml_register_event_cb(nullptr, "light_toggle_cb", light_toggle_cb);
    lv_xml_register_event_cb(nullptr, "print_card_clicked_cb", print_card_clicked_cb);
    lv_xml_register_event_cb(nullptr, "tip_text_clicked_cb", tip_text_clicked_cb);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Registered subjects and event callbacks", get_name());

    // Set initial tip of the day
    update_tip_of_day();
}

void HomePanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up...", get_name());

    // Find light-related widgets for conditional hiding
    light_button_ = lv_obj_find_by_name(panel_, "light_button");
    light_divider_ = lv_obj_find_by_name(panel_, "divider2");

    // Find printer image for connection state dimming
    printer_image_ = lv_obj_find_by_name(panel_, "printer_image");

    // Load printer-specific image based on configured type
    if (printer_image_) {
        Config* cfg = Config::get_instance();
        std::string printer_type =
            cfg->get<std::string>(WizardConfigPaths::PRINTER_TYPE, "Unknown");
        int type_index = PrinterTypes::find_printer_type_index(printer_type);
        const char* image_path = PrinterImages::get_image_path(type_index);
        lv_image_set_src(printer_image_, image_path);
        spdlog::debug("[{}] Printer image set: type='{}' (idx={}) -> {}", get_name(), printer_type,
                      type_index, image_path);
    }

    // Apply initial connection state dimming
    int conn_state = lv_subject_get_int(printer_state_.get_printer_connection_state_subject());
    update_printer_image_opacity(conn_state);

    // If no LED is configured, hide the light button and divider
    // Note: LED on/off visual state is handled by XML binding to PrinterState's led_state subject
    if (configured_led_.empty()) {
        if (light_button_) {
            lv_obj_add_flag(light_button_, LV_OBJ_FLAG_HIDDEN);
            spdlog::debug("[{}] Light button hidden (no LED configured)", get_name());
        }
        if (light_divider_) {
            lv_obj_add_flag(light_divider_, LV_OBJ_FLAG_HIDDEN);
            spdlog::debug("[{}] Light divider hidden (no LED configured)", get_name());
        }
    }

    // Apply responsive icon font sizes (fonts are discrete, can't be scaled in XML)
    setup_responsive_icon_fonts();

    // Start tip rotation timer (60 seconds = 60000ms)
    if (!tip_rotation_timer_) {
        tip_rotation_timer_ = lv_timer_create(tip_rotation_timer_cb, 60000, this);
        spdlog::info("[{}] Started tip rotation timer (60s interval)", get_name());
    }

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void HomePanel::update_tip_of_day() {
    auto tip = TipsManager::get_instance()->get_random_unique_tip();

    if (!tip.title.empty()) {
        // Store full tip for dialog display
        current_tip_ = tip;

        std::snprintf(status_buffer_, sizeof(status_buffer_), "%s", tip.title.c_str());
        lv_subject_copy_string(&status_subject_, status_buffer_);
        spdlog::debug("[{}] Updated tip: {}", get_name(), tip.title);
    } else {
        spdlog::warn("[{}] Failed to get tip, keeping current", get_name());
    }
}

void HomePanel::setup_responsive_icon_fonts() {
    // Layout/sizing is handled by XML, but icon fonts need C++ because fonts are discrete sizes.
    // XML can't conditionally switch fonts based on screen size.
    lv_display_t* display = lv_display_get_default();
    int32_t screen_height = lv_display_get_vertical_resolution(display);

    // Select icon sizes and label fonts based on screen size
    const lv_font_t* fa_icon_font;
    const char* mat_icon_size;
    const lv_font_t* label_font;
    int icon_px;

    if (screen_height <= UI_SCREEN_TINY_H) {
        fa_icon_font = &fa_icons_24; // Tiny: 24px icons
        mat_icon_size = "sm";        // 24x24
        label_font = UI_FONT_SMALL;  // Smaller text labels to save space
        icon_px = 24;
    } else if (screen_height <= UI_SCREEN_SMALL_H) {
        fa_icon_font = &fa_icons_32; // Small: 32px icons
        mat_icon_size = "md";        // 32x32
        label_font = UI_FONT_BODY;   // Normal text
        icon_px = 32;
    } else {
        fa_icon_font = &fa_icons_64; // Medium/Large: 64px icons
        mat_icon_size = "xl";        // 64x64
        label_font = UI_FONT_BODY;   // Normal text
        icon_px = 64;
    }

    // Network icon (FontAwesome label)
    lv_obj_t* network_icon = lv_obj_find_by_name(panel_, "network_icon");
    if (network_icon) {
        lv_obj_set_style_text_font(network_icon, fa_icon_font, 0);
    }

    // Network label text
    lv_obj_t* network_label = lv_obj_find_by_name(panel_, "network_label");
    if (network_label) {
        lv_obj_set_style_text_font(network_label, label_font, 0);
    }

    // Temperature icon (Material Design icon widget)
    lv_obj_t* temp_icon = lv_obj_find_by_name(panel_, "temp_icon");
    if (temp_icon) {
        ui_icon_set_size(temp_icon, mat_icon_size);
    }

    // Temperature label text
    lv_obj_t* temp_label = lv_obj_find_by_name(panel_, "temp_text_label");
    if (temp_label) {
        lv_obj_set_style_text_font(temp_label, label_font, 0);
    }

    // Light icons (Material Design icon widgets) - set size for both on/off states
    lv_obj_t* light_icon_off = lv_obj_find_by_name(panel_, "light_icon_off");
    lv_obj_t* light_icon_on = lv_obj_find_by_name(panel_, "light_icon_on");
    if (light_icon_off) {
        ui_icon_set_size(light_icon_off, mat_icon_size);
    }
    if (light_icon_on) {
        ui_icon_set_size(light_icon_on, mat_icon_size);
    }

    spdlog::debug("[{}] Set icons to {}px, labels to {} for screen height {}", get_name(), icon_px,
                  (label_font == UI_FONT_SMALL) ? "small" : "body", screen_height);
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void HomePanel::handle_light_toggle() {
    spdlog::info("[{}] Light button clicked", get_name());

    // Check if LED is configured
    if (configured_led_.empty()) {
        spdlog::warn("[{}] Light toggle called but no LED configured", get_name());
        return;
    }

    // Toggle to opposite of current state
    // Note: UI will update when Moonraker notification arrives (via PrinterState observer)
    bool new_state = !light_on_;

    // Send command to Moonraker
    if (api_) {
        if (new_state) {
            api_->set_led_on(
                configured_led_,
                [this]() {
                    spdlog::info("[{}] LED turned ON - waiting for state update", get_name());
                },
                [](const MoonrakerError& err) {
                    spdlog::error("Failed to turn LED on: {}", err.message);
                    NOTIFY_ERROR("Failed to turn light on: {}", err.user_message());
                });
        } else {
            api_->set_led_off(
                configured_led_,
                [this]() {
                    spdlog::info("[{}] LED turned OFF - waiting for state update", get_name());
                },
                [](const MoonrakerError& err) {
                    spdlog::error("Failed to turn LED off: {}", err.message);
                    NOTIFY_ERROR("Failed to turn light off: {}", err.user_message());
                });
        }
    } else {
        spdlog::warn("[{}] API not available - cannot control LED", get_name());
        NOTIFY_ERROR("Cannot control light: printer not connected");
    }
}

void HomePanel::handle_print_card_clicked() {
    spdlog::info("[{}] Print card clicked - navigating to print select panel", get_name());

    // Navigate to print select panel
    ui_nav_set_active(UI_PANEL_PRINT_SELECT);
}

void HomePanel::handle_tip_text_clicked() {
    if (current_tip_.title.empty()) {
        spdlog::warn("[{}] No tip available to display", get_name());
        return;
    }

    spdlog::info("[{}] Tip text clicked - showing detail dialog", get_name());

    // Show the tip modal (RAII handles cleanup, ModalBase handles backdrop/ESC/button)
    if (!tip_modal_.show(lv_screen_active(), current_tip_.title, current_tip_.content)) {
        spdlog::error("[{}] Failed to show tip detail modal", get_name());
    }
}

void HomePanel::handle_tip_rotation_timer() {
    update_tip_of_day();
}

void HomePanel::on_led_state_changed(int state) {
    // Update local light_on_ state from PrinterState's led_state subject
    light_on_ = (state != 0);

    spdlog::debug("[{}] LED state changed: {} (from PrinterState)", get_name(),
                  light_on_ ? "ON" : "OFF");
}

void HomePanel::on_extruder_temp_changed(int temp) {
    // Format temperature for display and update the string subject
    std::snprintf(temp_buffer_, sizeof(temp_buffer_), "%d °C", temp);
    lv_subject_copy_string(&temp_subject_, temp_buffer_);

    spdlog::trace("[{}] Extruder temperature updated: {}°C", get_name(), temp);
}

void HomePanel::update_printer_image_opacity(int connection_state) {
    if (!printer_image_) {
        return;
    }

    // CONNECTED = 2, all other states show darkened image
    if (connection_state == 2) {
        // Connected - clear recolor effect
        lv_obj_set_style_image_recolor_opa(printer_image_, LV_OPA_TRANSP, 0);
        spdlog::debug("[{}] Printer connected - image normal", get_name());
    } else {
        // Not connected - darken by 50% with black overlay
        lv_obj_set_style_image_recolor(printer_image_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_image_recolor_opa(printer_image_, LV_OPA_50, 0);
        spdlog::debug("[{}] Printer not connected (state={}) - image darkened", get_name(),
                      connection_state);
    }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

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

void HomePanel::tip_rotation_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<HomePanel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->handle_tip_rotation_timer();
    }
}

void HomePanel::led_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<HomePanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_led_state_changed(lv_subject_get_int(subject));
    }
}

void HomePanel::extruder_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<HomePanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_extruder_temp_changed(lv_subject_get_int(subject));
    }
}

void HomePanel::connection_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<HomePanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->update_printer_image_opacity(lv_subject_get_int(subject));
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void HomePanel::update(const char* status_text, int temp) {
    // Update subjects - all bound widgets update automatically
    if (status_text) {
        lv_subject_copy_string(&status_subject_, status_text);
        spdlog::debug("[{}] Updated status_text subject to: {}", get_name(), status_text);
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d °C", temp);
    lv_subject_copy_string(&temp_subject_, buf);
    spdlog::debug("[{}] Updated temp_text subject to: {}", get_name(), buf);
}

void HomePanel::set_network(network_type_t type) {
    current_network_ = type;

    switch (type) {
    case NETWORK_WIFI:
        lv_subject_copy_string(&network_icon_subject_, ICON_WIFI);
        lv_subject_copy_string(&network_label_subject_, "Wi-Fi");
        lv_subject_copy_string(&network_color_subject_, "0xff4444");
        break;
    case NETWORK_ETHERNET:
        lv_subject_copy_string(&network_icon_subject_, ICON_ETHERNET);
        lv_subject_copy_string(&network_label_subject_, "Ethernet");
        lv_subject_copy_string(&network_color_subject_, "0xff4444");
        break;
    case NETWORK_DISCONNECTED:
        lv_subject_copy_string(&network_icon_subject_, ICON_WIFI_SLASH);
        lv_subject_copy_string(&network_label_subject_, "Disconnected");
        lv_subject_copy_string(&network_color_subject_, "0x909090");
        break;
    }
    spdlog::debug("[{}] Updated network status to type {}", get_name(), static_cast<int>(type));
}

void HomePanel::set_light(bool is_on) {
    // Note: The actual LED state is managed by PrinterState via Moonraker notifications.
    // This method is only used for local state updates when API is unavailable.
    light_on_ = is_on;
    spdlog::debug("[{}] Local light state: {}", get_name(), is_on ? "ON" : "OFF");
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<HomePanel> g_home_panel;

HomePanel& get_global_home_panel() {
    if (!g_home_panel) {
        g_home_panel = std::make_unique<HomePanel>(get_printer_state(), nullptr);
    }
    return *g_home_panel;
}
