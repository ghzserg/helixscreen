// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_home.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_fonts.h"
#include "ui_nav.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"

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
    std::strcpy(temp_buffer_, "30 °C");
    std::strcpy(network_icon_buffer_, ICON_WIFI);
    std::strcpy(network_label_buffer_, "Wi-Fi");
    std::strcpy(network_color_buffer_, "0xff4444");

    // Default colors (will be overwritten by init_home_panel_colors)
    light_icon_on_color_ = lv_color_hex(0xFFD700);
    light_icon_off_color_ = lv_color_hex(0x909090);
}

HomePanel::~HomePanel() {
    // NOTE: Do NOT log or call LVGL functions here! Destructor may be called
    // during static destruction after LVGL and spdlog have already been destroyed.
    // The timer and dialog will be cleaned up by LVGL when it shuts down.
    //
    // If we need explicit cleanup, it should be done in a separate cleanup()
    // method called before exit(), not in the destructor.

    tip_rotation_timer_ = nullptr;
    tip_detail_dialog_ = nullptr;

    // Observers cleaned up by PanelBase::~PanelBase()
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

    // Initialize theme-aware colors for light icon
    init_home_panel_colors();

    // Initialize subjects with default values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(status_subject_, status_buffer_,
                                        "Welcome to HelixScreen", "status_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(temp_subject_, temp_buffer_,
                                        "30 °C", "temp_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(network_icon_subject_, network_icon_buffer_,
                                        ICON_WIFI, "network_icon");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(network_label_subject_, network_label_buffer_,
                                        "Wi-Fi", "network_label");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(network_color_subject_, network_color_buffer_,
                                        "0xff4444", "network_color");
    UI_SUBJECT_INIT_AND_REGISTER_COLOR(light_icon_color_subject_, light_icon_off_color_,
                                       "light_icon_color");

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

    spdlog::debug("[{}] Setting up observers and responsive sizing...", get_name());

    // Use LVGL 9's name-based widget lookup - resilient to layout changes
    network_icon_label_ = lv_obj_find_by_name(panel_, "network_icon");
    network_text_label_ = lv_obj_find_by_name(panel_, "network_label");
    light_icon_label_ = lv_obj_find_by_name(panel_, "light_icon");

    if (!network_icon_label_ || !network_text_label_ || !light_icon_label_) {
        spdlog::error("[{}] Failed to find named widgets (net_icon={}, net_label={}, light={})",
                      get_name(),
                      static_cast<void*>(network_icon_label_),
                      static_cast<void*>(network_text_label_),
                      static_cast<void*>(light_icon_label_));
        return;
    }

    // Apply responsive sizing based on screen dimensions
    setup_responsive_sizing();

    // Add observers to watch subjects and update widgets
    // Pass 'this' as user_data for static trampolines
    auto* net_obs1 = lv_subject_add_observer(&network_icon_subject_, network_observer_cb, this);
    auto* net_obs2 = lv_subject_add_observer(&network_label_subject_, network_observer_cb, this);
    auto* net_obs3 = lv_subject_add_observer(&network_color_subject_, network_observer_cb, this);
    auto* light_obs = lv_subject_add_observer(&light_icon_color_subject_, light_observer_cb, this);

    // Register observers for RAII cleanup
    register_observer(net_obs1);
    register_observer(net_obs2);
    register_observer(net_obs3);
    register_observer(light_obs);

    // Apply initial light icon color (observers only fire on *changes*, not initial state)
    if (light_icon_label_) {
        lv_color_t initial_color = lv_subject_get_color(&light_icon_color_subject_);
        lv_obj_set_style_img_recolor(light_icon_label_, initial_color, LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(light_icon_label_, 255, LV_PART_MAIN);
        spdlog::debug("[{}] Applied initial light icon color", get_name());
    }

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

void HomePanel::init_home_panel_colors() {
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("home_panel");
    if (scope) {
        bool use_dark_mode = ui_theme_is_dark_mode();

        // Load light icon ON color
        const char* on_str = lv_xml_get_const(
            scope, use_dark_mode ? "light_icon_on_dark" : "light_icon_on_light");
        light_icon_on_color_ = on_str ? ui_theme_parse_color(on_str) : lv_color_hex(0xFFD700);

        // Load light icon OFF color
        const char* off_str = lv_xml_get_const(
            scope, use_dark_mode ? "light_icon_off_dark" : "light_icon_off_light");
        light_icon_off_color_ = off_str ? ui_theme_parse_color(off_str) : lv_color_hex(0x909090);

        spdlog::debug("[{}] Light icon colors loaded: on={}, off={} ({})",
                      get_name(),
                      on_str ? on_str : "default",
                      off_str ? off_str : "default",
                      use_dark_mode ? "dark" : "light");
    } else {
        // Fallback to defaults if scope not found
        light_icon_on_color_ = lv_color_hex(0xFFD700);
        light_icon_off_color_ = lv_color_hex(0x909090);
        spdlog::warn("[{}] Failed to get home_panel component scope, using defaults", get_name());
    }
}

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

void HomePanel::setup_responsive_sizing() {
    // Get screen dimensions for responsive sizing
    lv_display_t* display = lv_display_get_default();
    int32_t screen_height = lv_display_get_vertical_resolution(display);

    // 1. Set responsive printer image size and SCALE (not just crop/pad)
    lv_obj_t* printer_image = lv_obj_find_by_name(panel_, "printer_image");
    if (printer_image) {
        int32_t printer_size;
        uint16_t zoom_level;  // 256 = 100%, 128 = 50%, 512 = 200%

        // Calculate printer image size and zoom based on screen height
        if (screen_height <= UI_SCREEN_TINY_H) {
            printer_size = 150;  // Tiny screens (480x320)
            zoom_level = 96;     // 37.5% zoom to scale 400px -> 150px
        } else if (screen_height <= UI_SCREEN_SMALL_H) {
            printer_size = 250;  // Small screens (800x480)
            zoom_level = 160;    // 62.5% zoom to scale 400px -> 250px
        } else if (screen_height <= UI_SCREEN_MEDIUM_H) {
            printer_size = 300;  // Medium screens (1024x600)
            zoom_level = 192;    // 75% zoom to scale 400px -> 300px
        } else {
            printer_size = 400;  // Large screens (1280x720+)
            zoom_level = 256;    // 100% zoom (original size)
        }

        lv_obj_set_width(printer_image, printer_size);
        lv_obj_set_height(printer_image, printer_size);
        lv_image_set_scale(printer_image, zoom_level);
        spdlog::debug("[{}] Set printer image: size={}px, zoom={} ({}%) for screen height {}",
                      get_name(), printer_size, zoom_level, (zoom_level * 100) / 256, screen_height);
    } else {
        spdlog::warn("[{}] Printer image not found - size not adjusted", get_name());
    }

    // 2. Set responsive info card icon sizes
    const lv_font_t* info_icon_font;
    if (screen_height <= UI_SCREEN_TINY_H) {
        info_icon_font = &fa_icons_24;  // Tiny: 24px icons
    } else if (screen_height <= UI_SCREEN_SMALL_H) {
        info_icon_font = &fa_icons_24;  // Small: 24px icons
    } else {
        info_icon_font = &fa_icons_48;  // Medium/Large: 48px icons
    }

    lv_obj_t* temp_icon = lv_obj_find_by_name(panel_, "temp_icon");
    if (temp_icon) {
        lv_obj_set_style_text_font(temp_icon, info_icon_font, 0);
    }

    if (network_icon_label_) {
        lv_obj_set_style_text_font(network_icon_label_, info_icon_font, 0);
    }
    if (light_icon_label_) {
        lv_obj_set_style_text_font(light_icon_label_, info_icon_font, 0);
    }

    // 3. Set responsive status text font for tiny screens
    if (screen_height <= UI_SCREEN_TINY_H) {
        lv_obj_t* status_text = lv_obj_find_by_name(panel_, "status_text_label");
        if (status_text) {
            lv_obj_set_style_text_font(status_text, UI_FONT_HEADING, 0);
            spdlog::debug("[{}] Set status text to UI_FONT_HEADING for tiny screen", get_name());
        }
    }

    int icon_size = (info_icon_font == &fa_icons_24)   ? 24
                  : (info_icon_font == &fa_icons_32) ? 32
                                                     : 48;
    spdlog::debug("[{}] Set info card icons to {}px for screen height {}",
                  get_name(), icon_size, screen_height);
}

void HomePanel::close_tip_dialog() {
    if (tip_detail_dialog_) {
        lv_obj_delete(tip_detail_dialog_);
        tip_detail_dialog_ = nullptr;
        spdlog::debug("[{}] Tip dialog closed", get_name());
    }
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void HomePanel::handle_light_toggle() {
    spdlog::info("[{}] Light button clicked", get_name());

    // Toggle the light state
    set_light(!light_on_);

    // TODO: Add callback to send command to Klipper
    spdlog::debug("[{}] Light toggled: {}", get_name(), light_on_ ? "ON" : "OFF");
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

    // Create dialog with current tip data
    const char* attrs[] = {
        "title", current_tip_.title.c_str(),
        "content", current_tip_.content.c_str(),
        nullptr
    };

    lv_obj_t* screen = lv_screen_active();
    tip_detail_dialog_ = static_cast<lv_obj_t*>(
        lv_xml_create(screen, "tip_detail_dialog", attrs));

    if (!tip_detail_dialog_) {
        spdlog::error("[{}] Failed to create tip detail dialog from XML", get_name());
        return;
    }

    // Wire up Ok button to close dialog
    lv_obj_t* btn_ok = lv_obj_find_by_name(tip_detail_dialog_, "btn_ok");
    if (btn_ok) {
        lv_obj_add_event_cb(btn_ok, tip_dialog_close_cb, LV_EVENT_CLICKED, this);
    }

    // Backdrop click to close - but only if clicking the backdrop itself
    lv_obj_add_event_cb(tip_detail_dialog_, tip_dialog_close_cb, LV_EVENT_CLICKED, this);

    // Bring to foreground
    lv_obj_move_foreground(tip_detail_dialog_);
    spdlog::debug("[{}] Tip dialog shown: {}", get_name(), current_tip_.title);
}

void HomePanel::handle_tip_dialog_close() {
    close_tip_dialog();
}

void HomePanel::handle_tip_rotation_timer() {
    update_tip_of_day();
}

void HomePanel::on_network_changed() {
    if (!network_icon_label_ || !network_text_label_) {
        return;
    }

    // Update network icon text
    const char* icon = lv_subject_get_string(&network_icon_subject_);
    if (icon) {
        lv_label_set_text(network_icon_label_, icon);
    }

    // Update network label text
    const char* label = lv_subject_get_string(&network_label_subject_);
    if (label) {
        lv_label_set_text(network_text_label_, label);
    }

    // Network icon/label colors are now handled by theme
    spdlog::trace("[{}] Network observer updated widgets", get_name());
}

void HomePanel::on_light_color_changed(lv_subject_t* subject) {
    if (!light_icon_label_) {
        return;
    }

    // Update light icon color using image recolor (Material Design icons are monochrome)
    lv_color_t color = lv_subject_get_color(subject);
    lv_obj_set_style_img_recolor(light_icon_label_, color, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(light_icon_label_, 255, LV_PART_MAIN);

    spdlog::trace("[{}] Light observer updated icon color", get_name());
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

void HomePanel::tip_dialog_close_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] tip_dialog_close_cb");
    auto* self = static_cast<HomePanel*>(lv_event_get_user_data(e));
    if (self) {
        // For backdrop clicks, only close if clicking the dialog itself (not children)
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_obj_t* current_target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));

        // Ok button always closes; backdrop only closes if target == current_target
        if (target == current_target || lv_obj_get_name(target) != nullptr) {
            // Check if it's the Ok button or the backdrop itself
            const char* name = lv_obj_get_name(target);
            if ((name && strcmp(name, "btn_ok") == 0) || target == current_target) {
                self->handle_tip_dialog_close();
            }
        }
    }
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::tip_rotation_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<HomePanel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->handle_tip_rotation_timer();
    }
}

void HomePanel::network_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;  // We read subjects directly in the handler
    auto* self = static_cast<HomePanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_network_changed();
    }
}

void HomePanel::light_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<HomePanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_light_color_changed(subject);
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
    light_on_ = is_on;

    if (is_on) {
        // Light is on - show theme-aware ON color
        lv_subject_set_color(&light_icon_color_subject_, light_icon_on_color_);
    } else {
        // Light is off - show theme-aware OFF color
        lv_subject_set_color(&light_icon_color_subject_, light_icon_off_color_);
    }
    spdlog::debug("[{}] Updated light state to: {}", get_name(), is_on ? "ON" : "OFF");
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
