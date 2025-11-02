/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_wizard_wifi.h"
#include "ui_theme.h"
#include "ui_icon.h"
#include "wifi_manager.h"
#include "ethernet_manager.h"
#include "ui_keyboard.h"
#include "lvgl/lvgl.h"
#include <spdlog/spdlog.h>
#include <memory>
#include <cstring>
#include <algorithm>

// ============================================================================
// Static Data & Subjects
// ============================================================================

// Helper type for constant name/value pairs
struct WifiConstant {
    const char* name;
    const char* value;
};

// Helper: Register array of constants to a scope
static void register_wifi_constants_to_scope(lv_xml_component_scope_t* scope,
                                             const WifiConstant* constants) {
    if (!scope) return;
    for (int i = 0; constants[i].name != NULL; i++) {
        lv_xml_register_const(scope, constants[i].name, constants[i].value);
    }
}

// Subject declarations (module scope)
static lv_subject_t wifi_enabled;
static lv_subject_t wifi_status;
static lv_subject_t ethernet_status;
static lv_subject_t wifi_scanning;  // 0=not scanning, 1=scanning

// String buffers (must be persistent)
static char wifi_status_buffer[64];
static char ethernet_status_buffer[64];

// WiFi screen instance
static lv_obj_t* wifi_screen_root = nullptr;
static lv_obj_t* password_modal = nullptr;
static lv_obj_t* network_list_container = nullptr;

// WiFiManager instance
// Using shared_ptr instead of unique_ptr for async callback safety
static std::shared_ptr<WiFiManager> wifi_manager = nullptr;

// EthernetManager instance
static std::unique_ptr<EthernetManager> ethernet_manager = nullptr;

// Current network selection (for password modal)
static char current_ssid[64] = "";
static bool current_secured = false;

// Per-instance network item data (reactive approach)
// Each network item gets its own subject set for reactive UI updates
struct NetworkItemData {
    WiFiNetwork network;           // Network info for click handler
    lv_subject_t* ssid;           // Subject for SSID label binding
    lv_subject_t* signal_strength;// Subject for signal icon color
    lv_subject_t* is_secured;     // Subject for lock icon visibility
    char ssid_buffer[64];         // Persistent buffer for SSID subject

    NetworkItemData(const WiFiNetwork& net) : network(net) {
        ssid = new lv_subject_t();
        signal_strength = new lv_subject_t();
        is_secured = new lv_subject_t();

        // Initialize subjects
        strncpy(ssid_buffer, network.ssid.c_str(), sizeof(ssid_buffer) - 1);
        ssid_buffer[sizeof(ssid_buffer) - 1] = '\0';
        lv_subject_init_string(ssid, ssid_buffer, nullptr, sizeof(ssid_buffer), ssid_buffer);
        lv_subject_init_int(signal_strength, network.signal_strength);
        lv_subject_init_int(is_secured, network.is_secured ? 1 : 0);
    }

    ~NetworkItemData() {
        delete ssid;
        delete signal_strength;
        delete is_secured;
    }
};

// ============================================================================
// Forward Declarations
// ============================================================================

static void on_wifi_toggle_changed(lv_event_t* e);
static void on_network_item_clicked(lv_event_t* e);
static void on_modal_cancel_clicked(lv_event_t* e);
static void on_modal_connect_clicked(lv_event_t* e);

static void update_wifi_status(const char* status);
static void update_ethernet_status();
static void populate_network_list(const std::vector<WiFiNetwork>& networks);
static void clear_network_list();

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Map WiFi signal strength percentage to Material icon name
 * @param signal_strength Signal strength (0-100%)
 * @param is_secured Whether the network is secured (shows lock variant)
 * @return Material icon name for appropriate signal tier (locked or unlocked)
 */
static const char* get_wifi_signal_icon(int signal_strength, bool is_secured) {
    if (signal_strength <= 25) {
        return is_secured ? "mat_wifi_strength_1_lock" : "mat_wifi_strength_1";
    } else if (signal_strength <= 50) {
        return is_secured ? "mat_wifi_strength_2_lock" : "mat_wifi_strength_2";
    } else if (signal_strength <= 75) {
        return is_secured ? "mat_wifi_strength_3_lock" : "mat_wifi_strength_3";
    } else {
        return is_secured ? "mat_wifi_strength_4_lock" : "mat_wifi_strength_4";
    }
}

/**
 * @brief Apply visual highlight to connected network item
 * @param item The network item widget to highlight
 *
 * Applies card-like background and accent border to indicate active connection
 */
static void apply_connected_network_highlight(lv_obj_t* item) {
    if (!item) return;

    // Left accent border (4px primary color)
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 4, LV_PART_MAIN);
    lv_color_t accent = ui_theme_get_color("primary_color");
    lv_obj_set_style_border_color(item, accent, LV_PART_MAIN);

    // Slightly lighter background for card-like effect
    lv_color_t bg = lv_color_hex(0x262626);  // Lighter than card_bg_dark (#131313)
    lv_obj_set_style_bg_color(item, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);

    // Brighter text for SSID
    lv_obj_t* ssid_label = lv_obj_find_by_name(item, "ssid_label");
    if (ssid_label) {
        lv_color_t bright_text = lv_color_hex(0xE3E3E3);  // Brighter than text_primary
        lv_obj_set_style_text_color(ssid_label, bright_text, LV_PART_MAIN);
    }

    spdlog::trace("[WiFi Screen] Applied connected network highlight");
}

// ============================================================================
// Public API Implementation
// ============================================================================

void ui_wizard_wifi_init_subjects() {
    spdlog::debug("[WiFi Screen] Initializing subjects");

    // Initialize subjects with defaults
    lv_subject_init_int(&wifi_enabled, 0);   // WiFi off by default
    lv_subject_init_int(&wifi_scanning, 0);  // Not scanning by default

    lv_subject_init_string(&wifi_status, wifi_status_buffer, nullptr,
                           sizeof(wifi_status_buffer), "WiFi Disabled");

    lv_subject_init_string(&ethernet_status, ethernet_status_buffer, nullptr,
                           sizeof(ethernet_status_buffer), "Checking...");

    // Register subjects globally
    lv_xml_register_subject(nullptr, "wifi_enabled", &wifi_enabled);
    lv_xml_register_subject(nullptr, "wifi_status", &wifi_status);
    lv_xml_register_subject(nullptr, "ethernet_status", &ethernet_status);
    lv_xml_register_subject(nullptr, "wifi_scanning", &wifi_scanning);

    spdlog::info("[WiFi Screen] Subjects initialized");
}

void ui_wizard_wifi_register_callbacks() {
    spdlog::debug("[WiFi Screen] Registering event callbacks");

    lv_xml_register_event_cb(nullptr, "on_wifi_toggle_changed", on_wifi_toggle_changed);
    lv_xml_register_event_cb(nullptr, "on_network_item_clicked", on_network_item_clicked);
    lv_xml_register_event_cb(nullptr, "on_modal_cancel_clicked", on_modal_cancel_clicked);
    lv_xml_register_event_cb(nullptr, "on_modal_connect_clicked", on_modal_connect_clicked);

    spdlog::info("[WiFi Screen] Callbacks registered");
}

void ui_wizard_wifi_register_responsive_constants() {
    spdlog::debug("[WiFi Screen] Registering responsive constants to WiFi network list scopes");

    // 1. Detect screen size using custom breakpoints
    lv_display_t* display = lv_display_get_default();
    int32_t hor_res = lv_display_get_horizontal_resolution(display);
    int32_t ver_res = lv_display_get_vertical_resolution(display);
    int32_t greater_res = LV_MAX(hor_res, ver_res);

    // 2. Determine responsive values based on breakpoint
    const char* list_item_padding;
    const char* list_item_font;
    const char* size_label;

    // Buffer for calculated height value (static for persistence beyond this scope)
    static char list_item_height_buf[16];

    if (greater_res <= UI_BREAKPOINT_SMALL_MAX) {  // ≤480: 480x320
        list_item_padding = "4";
        list_item_font = "montserrat_14";
        size_label = "SMALL";
    } else if (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) {  // 481-800: 800x480
        list_item_padding = "6";
        list_item_font = "montserrat_16";
        size_label = "MEDIUM";
    } else {  // >800: 1024x600+
        list_item_padding = "8";
        list_item_font = lv_xml_get_const(NULL, "font_body");
        size_label = "LARGE";
    }

    // Calculate list_item_height based on font height (1:1 ratio)
    // Padding (list_item_padding) provides vertical spacing between items
    const lv_font_t* item_font_ptr = lv_xml_get_font(NULL, list_item_font);
    if (item_font_ptr) {
        int32_t font_height = ui_theme_get_font_height(item_font_ptr);
        snprintf(list_item_height_buf, sizeof(list_item_height_buf), "%d", font_height);
        spdlog::debug("[WiFi Screen] Calculated list_item_height={}px from font_height ({})",
                      font_height, list_item_font);
    } else {
        // Fallback to hardcoded values if font not found
        const char* fallback_height = (greater_res <= UI_BREAKPOINT_SMALL_MAX) ? "60" :
                                      (greater_res <= UI_BREAKPOINT_MEDIUM_MAX) ? "80" : "100";
        snprintf(list_item_height_buf, sizeof(list_item_height_buf), "%s", fallback_height);
        spdlog::warn("[WiFi Screen] Failed to get font '{}', using fallback list_item_height={}",
                     list_item_font, fallback_height);
    }
    const char* list_item_height = list_item_height_buf;

    // 3. Define WiFi-specific constants in array
    WifiConstant constants[] = {
        {"list_item_padding", list_item_padding},
        {"list_item_height", list_item_height},
        {"list_item_font", list_item_font},
        {NULL, NULL}  // Sentinel
    };

    // 4. Register to wifi_network_item scope
    lv_xml_component_scope_t* item_scope = lv_xml_component_get_scope("wifi_network_item");
    register_wifi_constants_to_scope(item_scope, constants);

    // 5. Register to wizard_wifi_setup scope (for network_list_container)
    lv_xml_component_scope_t* wifi_setup_scope = lv_xml_component_get_scope("wizard_wifi_setup");
    register_wifi_constants_to_scope(wifi_setup_scope, constants);

    spdlog::info("[WiFi Screen] Registered 3 constants to wifi_network_item and wizard_wifi_setup scopes ({})",
                 size_label);
    spdlog::debug("[WiFi Screen] Values: list_item_padding={}, list_item_height={}, list_item_font={}",
                  list_item_padding, list_item_height, list_item_font);
}

lv_obj_t* ui_wizard_wifi_create(lv_obj_t* parent) {
    spdlog::debug("[WiFi Screen] Creating WiFi setup screen");

    if (!parent) {
        spdlog::error("[WiFi Screen] Cannot create: null parent");
        return nullptr;
    }

    // Register wifi_network_item component FIRST (needed for constant registration)
    static bool network_item_registered = false;
    if (!network_item_registered) {
        lv_xml_register_component_from_file("A:ui_xml/wifi_network_item.xml");
        network_item_registered = true;
        spdlog::debug("[WiFi Screen] Registered wifi_network_item component");
    }

    // Create WiFi screen from XML
    wifi_screen_root = (lv_obj_t*)lv_xml_create(parent, "wizard_wifi_setup", nullptr);

    if (!wifi_screen_root) {
        spdlog::error("[WiFi Screen] Failed to create wizard_wifi_setup from XML");
        return nullptr;
    }

    // Register responsive constants for WiFi network list
    // Must be done AFTER components are registered but before creating network items
    ui_wizard_wifi_register_responsive_constants();

    // Find password modal (hidden by default in XML)
    password_modal = lv_obj_find_by_name(wifi_screen_root, "wifi_password_modal");
    if (!password_modal) {
        spdlog::warn("[WiFi Screen] Password modal not found in XML");
    }

    // Find network list container
    network_list_container = lv_obj_find_by_name(wifi_screen_root, "network_list_container");
    if (!network_list_container) {
        spdlog::error("[WiFi Screen] Network list container not found in XML");
        return nullptr;
    }

    // Update layout to ensure SIZE_CONTENT calculates correctly
    lv_obj_update_layout(wifi_screen_root);

    spdlog::info("[WiFi Screen] WiFi screen created successfully");
    return wifi_screen_root;
}

void ui_wizard_wifi_init_wifi_manager() {
    spdlog::debug("[WiFi Screen] Initializing WiFi and Ethernet managers");

    // Create WiFiManager instance as shared_ptr for async callback safety
    wifi_manager = std::make_shared<WiFiManager>();

    // Initialize self-reference for safe async callback handling
    wifi_manager->init_self_reference(wifi_manager);

    // Create EthernetManager instance
    ethernet_manager = std::make_unique<EthernetManager>();

    // Update ethernet status
    update_ethernet_status();

    spdlog::info("[WiFi Screen] WiFi and Ethernet managers initialized");
}

void ui_wizard_wifi_cleanup() {
    spdlog::debug("[WiFi Screen] Cleaning up WiFi screen");

    // Stop WiFi scanning and disable if enabled
    if (wifi_manager) {
        spdlog::debug("[WiFi Screen] Stopping scan and disabling WiFi");
        wifi_manager->stop_scan();
        if (lv_subject_get_int(&wifi_enabled) == 1) {
            wifi_manager->set_enabled(false);
        }
    }

    // Clear network list BEFORE destroying wifi_manager
    // This ensures network data is freed while manager still exists
    spdlog::debug("[WiFi Screen] Clearing network list");
    clear_network_list();

    // Destroy WiFi manager (weak_ptr in async callbacks safely handles this)
    spdlog::debug("[WiFi Screen] Destroying WiFi manager");
    wifi_manager.reset();

    // Destroy Ethernet manager
    spdlog::debug("[WiFi Screen] Destroying Ethernet manager");
    ethernet_manager.reset();

    // Reset UI references
    wifi_screen_root = nullptr;
    password_modal = nullptr;
    network_list_container = nullptr;
    current_ssid[0] = '\0';
    current_secured = false;

    spdlog::info("[WiFi Screen] Cleanup complete");
}

void ui_wizard_wifi_show_password_modal(const char* ssid) {
    if (!password_modal) {
        spdlog::error("[WiFi Screen] Cannot show password modal: modal not found");
        return;
    }

    if (!ssid) {
        spdlog::error("[WiFi Screen] Cannot show password modal: null SSID");
        return;
    }

    spdlog::debug("[WiFi Screen] Showing password modal for SSID: {}", ssid);

    // Update modal title with SSID
    lv_obj_t* modal_ssid_label = lv_obj_find_by_name(password_modal, "modal_ssid");
    if (modal_ssid_label) {
        lv_label_set_text(modal_ssid_label, ssid);
    }

    // Clear password input
    lv_obj_t* password_input = lv_obj_find_by_name(password_modal, "password_input");
    if (password_input) {
        lv_textarea_set_text(password_input, "");
        // Register textarea with keyboard for input
        ui_keyboard_register_textarea(password_input);
        // Focus textarea to trigger keyboard
        lv_obj_add_state(password_input, LV_STATE_FOCUSED);
    }

    // Hide status message
    lv_obj_t* modal_status = lv_obj_find_by_name(password_modal, "modal_status");
    if (modal_status) {
        lv_obj_add_flag(modal_status, LV_OBJ_FLAG_HIDDEN);
    }

    // Show modal
    lv_obj_remove_flag(password_modal, LV_OBJ_FLAG_HIDDEN);
}

void ui_wizard_wifi_hide_password_modal() {
    if (!password_modal) {
        return;
    }

    spdlog::debug("[WiFi Screen] Hiding password modal");
    lv_obj_add_flag(password_modal, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================================
// Event Handlers
// ============================================================================

static void on_wifi_toggle_changed(lv_event_t* e) {
    lv_obj_t* toggle = (lv_obj_t*)lv_event_get_target(e);
    if (!toggle) {
        return;
    }

    // Get toggle state (ui_switch stores checked state)
    bool checked = lv_obj_get_state(toggle) & LV_STATE_CHECKED;

    spdlog::debug("[WiFi Screen] WiFi toggle changed: {}", checked ? "ON" : "OFF");

    // Update wifi_enabled subject
    lv_subject_set_int(&wifi_enabled, checked ? 1 : 0);

    if (checked) {
        // Enable WiFi
        update_wifi_status("Enabling WiFi...");

        if (wifi_manager) {
            wifi_manager->set_enabled(true);
            update_wifi_status("WiFi Enabled");

            // Show scanning indicator
            lv_subject_set_int(&wifi_scanning, 1);

            // Start network scanning
            spdlog::debug("[WiFi Screen] About to call start_scan with callback");
            wifi_manager->start_scan([](const std::vector<WiFiNetwork>& networks) {
                spdlog::info("[WiFi Screen] *** SCAN CALLBACK INVOKED with {} networks ***", networks.size());

                // Hide scanning indicator
                lv_subject_set_int(&wifi_scanning, 0);

                // Populate network list
                populate_network_list(networks);
            });
            spdlog::debug("[WiFi Screen] start_scan call completed");
        } else {
            spdlog::error("[WiFi Screen] WiFi manager not initialized");
            update_wifi_status("WiFi Error");
        }
    } else {
        // Disable WiFi
        update_wifi_status("WiFi Disabled");
        lv_subject_set_int(&wifi_scanning, 0);  // Stop scanning indicator
        clear_network_list();

        if (wifi_manager) {
            wifi_manager->stop_scan();
            wifi_manager->set_enabled(false);
        }
    }
}

static void on_network_item_clicked(lv_event_t* e) {
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    if (!item) {
        return;
    }

    // Get network item data from user_data
    NetworkItemData* item_data = (NetworkItemData*)lv_obj_get_user_data(item);
    if (!item_data) {
        spdlog::error("[WiFi Screen] No network data found in clicked item");
        return;
    }

    const WiFiNetwork& network = item_data->network;
    spdlog::debug("[WiFi Screen] Network clicked: {} ({}%)", network.ssid, network.signal_strength);

    // Store current network selection
    strncpy(current_ssid, network.ssid.c_str(), sizeof(current_ssid) - 1);
    current_ssid[sizeof(current_ssid) - 1] = '\0';
    current_secured = network.is_secured;

    // Update WiFi status
    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), "Connecting to %s...", network.ssid.c_str());
    update_wifi_status(status_buf);

    if (network.is_secured) {
        // Show password modal for secured networks
        ui_wizard_wifi_show_password_modal(network.ssid.c_str());
    } else {
        // Connect directly to open network
        if (wifi_manager) {
            wifi_manager->connect(network.ssid, "", [](bool success, const std::string& error) {
                if (success) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Connected to %s", current_ssid);
                    update_wifi_status(msg);
                    spdlog::info("[WiFi Screen] Connected to {}", current_ssid);
                } else {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Failed to connect: %s", error.c_str());
                    update_wifi_status(msg);
                    spdlog::error("[WiFi Screen] Connection failed: {}", error);
                }
            });
        } else {
            spdlog::error("[WiFi Screen] WiFi manager not initialized");
            update_wifi_status("WiFi Error");
        }
    }
}

static void on_modal_cancel_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[WiFi Screen] Password modal cancel clicked");
    ui_wizard_wifi_hide_password_modal();
}

static void on_modal_connect_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[WiFi Screen] Password modal connect clicked");

    if (!password_modal) {
        spdlog::error("[WiFi Screen] Password modal not found");
        return;
    }

    // Get password from textarea
    lv_obj_t* password_input = lv_obj_find_by_name(password_modal, "password_input");
    if (!password_input) {
        spdlog::error("[WiFi Screen] Password input not found in modal");
        return;
    }

    const char* password = lv_textarea_get_text(password_input);
    if (!password || strlen(password) == 0) {
        // Show error in modal
        lv_obj_t* modal_status = lv_obj_find_by_name(password_modal, "modal_status");
        if (modal_status) {
            lv_label_set_text(modal_status, "Password cannot be empty");
            lv_obj_remove_flag(modal_status, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    spdlog::debug("[WiFi Screen] Attempting to connect to {} with password", current_ssid);

    // Update WiFi status
    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), "Connecting to %s...", current_ssid);
    update_wifi_status(status_buf);

    // Attempt connection
    if (wifi_manager) {
        wifi_manager->connect(current_ssid, password, [](bool success, const std::string& error) {
            if (success) {
                // Connection successful - hide modal and update status
                ui_wizard_wifi_hide_password_modal();

                char msg[128];
                snprintf(msg, sizeof(msg), "Connected to %s", current_ssid);
                update_wifi_status(msg);
                spdlog::info("[WiFi Screen] Connected to {}", current_ssid);
            } else {
                // Connection failed - show error in modal
                lv_obj_t* modal_status = lv_obj_find_by_name(password_modal, "modal_status");
                if (modal_status) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "Connection failed: %s", error.c_str());
                    lv_label_set_text(modal_status, error_msg);
                    lv_obj_remove_flag(modal_status, LV_OBJ_FLAG_HIDDEN);
                }

                update_wifi_status("Connection failed");
                spdlog::error("[WiFi Screen] Connection failed: {}", error);
            }
        });
    } else {
        spdlog::error("[WiFi Screen] WiFi manager not initialized");
        update_wifi_status("WiFi Error");
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

static void update_wifi_status(const char* status) {
    if (!status) {
        return;
    }

    spdlog::debug("[WiFi Screen] Updating WiFi status: {}", status);
    lv_subject_copy_string(&wifi_status, status);
}

static void update_ethernet_status() {
    if (!ethernet_manager) {
        spdlog::warn("[WiFi Screen] Ethernet manager not initialized");
        lv_subject_copy_string(&ethernet_status, "Unknown");
        return;
    }

    EthernetInfo info = ethernet_manager->get_info();

    if (info.connected) {
        // Format: "Connected (192.168.1.100)"
        char status_buf[128];
        snprintf(status_buf, sizeof(status_buf), "Connected (%s)", info.ip_address.c_str());
        lv_subject_copy_string(&ethernet_status, status_buf);
        spdlog::debug("[WiFi Screen] Ethernet status: {}", status_buf);
    } else {
        // Show status message (e.g., "No cable", "No connection", etc.)
        lv_subject_copy_string(&ethernet_status, info.status.c_str());
        spdlog::debug("[WiFi Screen] Ethernet status: {}", info.status);
    }
}

static void populate_network_list(const std::vector<WiFiNetwork>& networks) {
    spdlog::debug("[WiFi Screen] Populating network list with {} networks", networks.size());

    if (!network_list_container) {
        spdlog::error("[WiFi Screen] Network list container not found");
        return;
    }

    // Clear existing network items
    clear_network_list();

    // Sort networks by signal strength (descending)
    std::vector<WiFiNetwork> sorted_networks = networks;
    std::sort(sorted_networks.begin(), sorted_networks.end(),
              [](const WiFiNetwork& a, const WiFiNetwork& b) {
                  return a.signal_strength > b.signal_strength;
              });

    // Get connected network SSID for highlighting
    std::string connected_ssid;
    if (wifi_manager) {
        connected_ssid = wifi_manager->get_connected_ssid();
        if (!connected_ssid.empty()) {
            spdlog::debug("[WiFi Screen] Currently connected to: {}", connected_ssid);
        }
    }

    // Create network items
    for (const auto& network : sorted_networks) {
        // Create network item from component
        lv_obj_t* item = (lv_obj_t*)lv_xml_create(network_list_container, "wifi_network_item", nullptr);
        if (!item) {
            spdlog::error("[WiFi Screen] Failed to create network item for SSID: {}", network.ssid);
            continue;
        }

        // Set unique name for tracking (network_item_0, network_item_1, etc.)
        static int item_counter = 0;
        char item_name[32];
        snprintf(item_name, sizeof(item_name), "network_item_%d", item_counter++);
        lv_obj_set_name(item, item_name);

        // Create per-instance subjects and bind to widgets (reactive approach)
        NetworkItemData* item_data = new NetworkItemData(network);

        // Find widgets within the component
        lv_obj_t* ssid_label = lv_obj_find_by_name(item, "ssid_label");
        lv_obj_t* security_label = lv_obj_find_by_name(item, "security_label");
        lv_obj_t* signal_icon = lv_obj_find_by_name(item, "signal_icon");

        // Bind SSID label to subject
        if (ssid_label) {
            lv_label_bind_text(ssid_label, item_data->ssid, nullptr);
        }

        // Set security type text: show encryption type for secured networks, empty for open
        if (security_label) {
            if (network.is_secured) {
                lv_label_set_text(security_label, network.security_type.c_str());
            } else {
                lv_label_set_text(security_label, "");  // Empty string for open networks
            }
        }

        // Set signal strength icon (includes lock indicator for secured networks)
        if (signal_icon) {
            const char* icon_name = get_wifi_signal_icon(network.signal_strength, network.is_secured);
            ui_icon_set_source(signal_icon, icon_name);
            spdlog::trace("[WiFi Screen] Set signal icon '{}' for {}% strength ({})",
                          icon_name, network.signal_strength,
                          network.is_secured ? "secured" : "open");
        }

        // Highlight connected network with card-like background and accent border
        bool is_connected = (!connected_ssid.empty() && network.ssid == connected_ssid);
        if (is_connected) {
            apply_connected_network_highlight(item);
            spdlog::debug("[WiFi Screen] Highlighted connected network: {}", network.ssid);
        }

        // Store NetworkItemData in user_data for click handler and cleanup
        lv_obj_set_user_data(item, item_data);

        // Register click event
        lv_obj_add_event_cb(item, on_network_item_clicked, LV_EVENT_CLICKED, nullptr);

        spdlog::debug("[WiFi Screen] Added network: {} ({}%, {})",
                     network.ssid, network.signal_strength,
                     network.is_secured ? "secured" : "open");
    }

    spdlog::info("[WiFi Screen] Populated {} network items", sorted_networks.size());
}

static void clear_network_list() {
    if (!network_list_container) {
        spdlog::debug("[WiFi Screen] clear_network_list: container is NULL, skipping");
        return;
    }

    spdlog::debug("[WiFi Screen] Clearing network list");

    // Find all network items and delete them
    // Keep title and placeholder (they're part of the XML template)
    int32_t child_count = lv_obj_get_child_count(network_list_container);
    spdlog::debug("[WiFi Screen] Network list has {} children", child_count);

    for (int32_t i = child_count - 1; i >= 0; i--) {
        lv_obj_t* child = lv_obj_get_child(network_list_container, i);
        if (!child) {
            spdlog::warn("[WiFi Screen] Child at index {} is NULL", i);
            continue;
        }

        const char* name = lv_obj_get_name(child);

        // Only delete network items, keep title and placeholder
        if (name && strncmp(name, "network_item_", 13) == 0) {
            spdlog::debug("[WiFi Screen] Deleting network item: {}", name);

            // Get user data before deleting widget
            NetworkItemData* item_data = (NetworkItemData*)lv_obj_get_user_data(child);

            // Delete the widget FIRST (this removes event handlers and subject bindings)
            lv_obj_delete(child);

            // Now safe to free NetworkItemData (subjects no longer referenced by LVGL)
            if (item_data) {
                delete item_data;  // Destructor cleans up subjects
            }
        }
    }

    spdlog::debug("[WiFi Screen] Network list cleared");
}
