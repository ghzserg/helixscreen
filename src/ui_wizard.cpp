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

#include "ui_wizard.h"
#include "ui_keyboard.h"
#include "ui_theme.h"
#include "wizard_validation.h"
#include "wifi_manager.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cstdio>
#include <string>

// Wizard state
static lv_obj_t* wizard_root = nullptr;
static lv_obj_t* wizard_content_area = nullptr;
static lv_obj_t* btn_back = nullptr;
static lv_obj_t* btn_next = nullptr;

static WizardStep current_step = WizardStep::WIFI_SETUP;
static Config* config_instance = nullptr;
static MoonrakerClient* mr_client_instance = nullptr;
static std::function<void()> completion_callback;

// Subjects for reactive UI updates
static lv_subject_t wizard_title;
static char wizard_title_buffer[64];

static lv_subject_t wizard_progress;
static char wizard_progress_buffer[32];

static lv_subject_t wizard_next_button_text;
static char wizard_next_button_buffer[16];

static lv_subject_t printer_detection_status;
static char printer_detection_status_buffer[128];

static lv_subject_t connection_status;
static char connection_status_buffer[128];

static lv_subject_t connection_ip;
static char connection_ip_buffer[64];

static lv_subject_t connection_port;
static char connection_port_buffer[16];

static lv_subject_t wifi_status;
static char wifi_status_buffer[128];

static lv_subject_t ethernet_status;
static char ethernet_status_buffer[128];

static bool subjects_initialized = false;

// Screen object references
static lv_obj_t* wifi_setup_screen = nullptr;
static lv_obj_t* connection_screen = nullptr;
static lv_obj_t* printer_identify_screen = nullptr;
static lv_obj_t* bed_select_screen = nullptr;
static lv_obj_t* hotend_select_screen = nullptr;
static lv_obj_t* fan_select_screen = nullptr;
static lv_obj_t* led_select_screen = nullptr;
static lv_obj_t* summary_screen = nullptr;

// WiFi network list
static lv_obj_t* network_list_container = nullptr;
static lv_obj_t* wifi_toggle_switch = nullptr;
static lv_timer_t* wifi_scan_delay_timer = nullptr;

// Printer identification widgets
static lv_obj_t* printer_name_input = nullptr;
static lv_obj_t* printer_type_roller = nullptr;

// Hardware selection widgets
static lv_obj_t* bed_heater_dropdown = nullptr;
static lv_obj_t* bed_sensor_dropdown = nullptr;
static lv_obj_t* hotend_heater_dropdown = nullptr;
static lv_obj_t* hotend_sensor_dropdown = nullptr;
static lv_obj_t* hotend_fan_dropdown = nullptr;
static lv_obj_t* part_cooling_fan_dropdown = nullptr;
static lv_obj_t* led_main_dropdown = nullptr;

// Connection state
static bool connection_tested = false;
static bool connection_successful = false;
static uint32_t connection_test_start_time = 0;  // Track when connection test started

// Hardware mapping storage
static std::string selected_bed_heater;
static std::string selected_bed_sensor;
static std::string selected_hotend_heater;
static std::string selected_hotend_sensor;
static std::string selected_hotend_fan;
static std::string selected_part_cooling_fan;
static std::string selected_main_led;

// Printer identification storage
static std::string selected_printer_name;
static std::string selected_printer_type;
static int printer_type_confidence = 0;

// Forward declarations
static void on_back_clicked(lv_event_t* e);
static void on_next_clicked(lv_event_t* e);
static void on_test_connection_clicked(lv_event_t* e);
static void update_ui_for_step(WizardStep step);
static bool validate_current_step();
static void populate_hardware_dropdowns();
static std::string get_dropdown_options_from_vector(const std::vector<std::string>& items, const std::string& none_option = "");
static void detect_printer_type();
static void check_connection_timeout(lv_timer_t* timer);
static void clear_network_list();
static void populate_network_list(const std::vector<WiFiNetwork>& networks);
static void on_wifi_toggle_changed(lv_event_t* e);
static void wifi_scan_delay_callback(lv_timer_t* timer);
static void on_network_item_clicked(lv_event_t* e);

void ui_wizard_init_subjects() {
    if (subjects_initialized) {
        spdlog::warn("Wizard subjects already initialized");
        return;
    }

    lv_subject_init_string(&wizard_title, wizard_title_buffer, nullptr,
                           sizeof(wizard_title_buffer), "Connect to Moonraker");
    lv_xml_register_subject(nullptr, "wizard_title", &wizard_title);

    lv_subject_init_string(&wizard_progress, wizard_progress_buffer, nullptr,
                           sizeof(wizard_progress_buffer), "Step 1 of 7");
    lv_xml_register_subject(nullptr, "wizard_progress", &wizard_progress);

    lv_subject_init_string(&wizard_next_button_text, wizard_next_button_buffer, nullptr,
                           sizeof(wizard_next_button_buffer), "Next");
    lv_xml_register_subject(nullptr, "wizard_next_button_text", &wizard_next_button_text);

    lv_subject_init_string(&printer_detection_status, printer_detection_status_buffer, nullptr,
                           sizeof(printer_detection_status_buffer), "Detecting printer...");
    lv_xml_register_subject(nullptr, "printer_detection_status", &printer_detection_status);

    lv_subject_init_string(&connection_status, connection_status_buffer, nullptr,
                           sizeof(connection_status_buffer), "Enter IP address and port, then click Test Connection");
    lv_xml_register_subject(nullptr, "connection_status", &connection_status);

    lv_subject_init_string(&connection_ip, connection_ip_buffer, nullptr,
                           sizeof(connection_ip_buffer), "127.0.0.1");
    lv_xml_register_subject(nullptr, "connection_ip", &connection_ip);

    lv_subject_init_string(&connection_port, connection_port_buffer, nullptr,
                           sizeof(connection_port_buffer), "7125");
    lv_xml_register_subject(nullptr, "connection_port", &connection_port);

    lv_subject_init_string(&wifi_status, wifi_status_buffer, nullptr,
                           sizeof(wifi_status_buffer), "Enable WiFi to scan for networks");
    lv_xml_register_subject(nullptr, "wifi_status", &wifi_status);

    lv_subject_init_string(&ethernet_status, ethernet_status_buffer, nullptr,
                           sizeof(ethernet_status_buffer), "Checking...");
    lv_xml_register_subject(nullptr, "ethernet_status", &ethernet_status);

    subjects_initialized = true;
    spdlog::info("Wizard subjects initialized");
}

lv_obj_t* ui_wizard_create(lv_obj_t* parent,
                            Config* config,
                            MoonrakerClient* mr_client,
                            std::function<void()> on_complete) {
    if (!subjects_initialized) {
        spdlog::error("Wizard subjects not initialized - call ui_wizard_init_subjects() first");
        return nullptr;
    }

    config_instance = config;
    mr_client_instance = mr_client;
    completion_callback = on_complete;

    // Initialize connection subjects from config
    std::string host = config_instance->get<std::string>(config_instance->df() + "moonraker_host");
    int port = config_instance->get<int>(config_instance->df() + "moonraker_port");
    lv_subject_copy_string(&connection_ip, host.c_str());
    lv_subject_copy_string(&connection_port, std::to_string(port).c_str());

    // Register event callbacks
    lv_xml_register_event_cb(nullptr, "on_back_clicked", on_back_clicked);
    lv_xml_register_event_cb(nullptr, "on_next_clicked", on_next_clicked);
    lv_xml_register_event_cb(nullptr, "on_test_connection_clicked", on_test_connection_clicked);

    // Create wizard container from XML
    wizard_root = (lv_obj_t*)lv_xml_create(parent, "wizard_container", nullptr);
    if (!wizard_root) {
        spdlog::error("Failed to create wizard_container from XML");
        return nullptr;
    }

    // Get references to UI elements
    wizard_content_area = lv_obj_find_by_name(wizard_root, "wizard_content");
    btn_back = lv_obj_find_by_name(wizard_root, "btn_back");
    btn_next = lv_obj_find_by_name(wizard_root, "btn_next");

    if (!wizard_content_area || !btn_back || !btn_next) {
        spdlog::error("Failed to find wizard UI elements");
        lv_obj_delete(wizard_root);
        wizard_root = nullptr;
        return nullptr;
    }

    // Wire event handlers to buttons
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn_next, on_next_clicked, LV_EVENT_CLICKED, nullptr);

    // Create timer to check connection timeout (runs every second)
    lv_timer_create(check_connection_timeout, 1000, nullptr);

    // Show first step (WiFi setup if hardware available, otherwise skip to CONNECTION)
    ui_wizard_goto_step(WizardStep::WIFI_SETUP);

    spdlog::info("Wizard created successfully");
    return wizard_root;
}

void ui_wizard_goto_step(WizardStep step) {
    current_step = step;

    // Hide all screens
    if (wifi_setup_screen) lv_obj_add_flag(wifi_setup_screen, LV_OBJ_FLAG_HIDDEN);
    if (connection_screen) lv_obj_add_flag(connection_screen, LV_OBJ_FLAG_HIDDEN);
    if (printer_identify_screen) lv_obj_add_flag(printer_identify_screen, LV_OBJ_FLAG_HIDDEN);
    if (bed_select_screen) lv_obj_add_flag(bed_select_screen, LV_OBJ_FLAG_HIDDEN);
    if (hotend_select_screen) lv_obj_add_flag(hotend_select_screen, LV_OBJ_FLAG_HIDDEN);
    if (fan_select_screen) lv_obj_add_flag(fan_select_screen, LV_OBJ_FLAG_HIDDEN);
    if (led_select_screen) lv_obj_add_flag(led_select_screen, LV_OBJ_FLAG_HIDDEN);
    if (summary_screen) lv_obj_add_flag(summary_screen, LV_OBJ_FLAG_HIDDEN);

    // Create and show current step screen
    lv_obj_t* screen = nullptr;
    switch (step) {
        case WizardStep::WIFI_SETUP:
            // Check if WiFi hardware is available
            if (!WiFiManager::has_hardware()) {
                spdlog::info("[Wizard] No WiFi hardware detected, skipping WiFi setup");
                ui_wizard_goto_step(WizardStep::CONNECTION);
                return;
            }

            if (!wifi_setup_screen && wizard_content_area) {
                wifi_setup_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_wifi_setup", nullptr);
                if (wifi_setup_screen) {
                    // Check Ethernet status and update subject
                    if (WiFiManager::has_ethernet()) {
                        std::string eth_ip = WiFiManager::get_ethernet_ip();
                        if (!eth_ip.empty()) {
                            std::string status = "Connected: " + eth_ip;
                            lv_subject_copy_string(&ethernet_status, status.c_str());
                        } else {
                            lv_subject_copy_string(&ethernet_status, "Present (Not Connected)");
                        }
                    } else {
                        lv_subject_copy_string(&ethernet_status, "Not Present");
                    }

                    // Find and store widget references
                    network_list_container = lv_obj_find_by_name(wifi_setup_screen, "network_list_container");
                    wifi_toggle_switch = lv_obj_find_by_name(wifi_setup_screen, "wifi_toggle");

                    // Dim network list container initially (WiFi starts disabled)
                    if (network_list_container) {
                        lv_obj_add_state(network_list_container, LV_STATE_DISABLED);
                        lv_obj_set_style_opa(network_list_container, UI_DISABLED_OPA, LV_PART_MAIN);
                    }

                    // Wire up WiFi toggle event handler
                    if (wifi_toggle_switch) {
                        lv_obj_add_event_cb(wifi_toggle_switch, on_wifi_toggle_changed, LV_EVENT_VALUE_CHANGED, nullptr);
                        spdlog::info("[Wizard] WiFi toggle event handler registered");
                    } else {
                        spdlog::error("[Wizard] WiFi toggle switch not found");
                    }

                    // Force layout update AFTER subject bindings are populated
                    lv_obj_update_layout(wifi_setup_screen);

                    spdlog::info("[Wizard] WiFi setup screen created");
                }
            }
            screen = wifi_setup_screen;
            break;

        case WizardStep::CONNECTION:
            if (!connection_screen && wizard_content_area) {
                connection_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_connection", nullptr);
                if (connection_screen) {
                    // Register textareas with global keyboard for auto show/hide
                    lv_obj_t* ip_input = lv_obj_find_by_name(connection_screen, "ip_input");
                    lv_obj_t* port_input = lv_obj_find_by_name(connection_screen, "port_input");
                    if (ip_input) {
                        ui_keyboard_register_textarea(ip_input);
                    }
                    if (port_input) {
                        ui_keyboard_register_textarea(port_input);
                    }

                    // Pre-populate IP and port from config
                    std::string host = config_instance->get<std::string>(config_instance->df() + "moonraker_host");
                    int port = config_instance->get<int>(config_instance->df() + "moonraker_port");
                    if (ip_input) {
                        lv_textarea_set_text(ip_input, host.c_str());
                    }
                    if (port_input) {
                        lv_textarea_set_text(port_input, std::to_string(port).c_str());
                    }

                    lv_obj_t* btn_test = lv_obj_find_by_name(connection_screen, "btn_test_connection");
                    if (btn_test) {
                        lv_obj_add_event_cb(btn_test, on_test_connection_clicked, LV_EVENT_CLICKED, nullptr);
                    }
                }
            }
            screen = connection_screen;
            break;

        case WizardStep::PRINTER_IDENTIFY:
            if (!printer_identify_screen && wizard_content_area) {
                printer_identify_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_printer_identify", nullptr);
                if (printer_identify_screen) {
                    printer_name_input = lv_obj_find_by_name(printer_identify_screen, "printer_name_input");
                    printer_type_roller = lv_obj_find_by_name(printer_identify_screen, "printer_type_roller");

                    // Register printer name input with keyboard
                    if (printer_name_input) {
                        ui_keyboard_register_textarea(printer_name_input);
                    }

                    // Pre-fill printer name from config if available
                    std::string printer_name = config_instance->get<std::string>(config_instance->df() + "printer_name", "");
                    if (!printer_name.empty() && printer_name_input) {
                        lv_textarea_set_text(printer_name_input, printer_name.c_str());
                    } else if (printer_name_input) {
                        // Default to hostname if no saved name
                        lv_textarea_set_text(printer_name_input, "My Printer");
                    }

                    // Run printer detection
                    detect_printer_type();
                }
            }
            screen = printer_identify_screen;
            break;

        case WizardStep::BED_SELECT:
            if (!bed_select_screen && wizard_content_area) {
                bed_select_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_bed_select", nullptr);
                if (bed_select_screen) {
                    bed_heater_dropdown = lv_obj_find_by_name(bed_select_screen, "bed_heater_dropdown");
                    bed_sensor_dropdown = lv_obj_find_by_name(bed_select_screen, "bed_sensor_dropdown");
                    populate_hardware_dropdowns();
                }
            }
            screen = bed_select_screen;
            break;

        case WizardStep::HOTEND_SELECT:
            if (!hotend_select_screen && wizard_content_area) {
                hotend_select_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_hotend_select", nullptr);
                if (hotend_select_screen) {
                    hotend_heater_dropdown = lv_obj_find_by_name(hotend_select_screen, "hotend_heater_dropdown");
                    hotend_sensor_dropdown = lv_obj_find_by_name(hotend_select_screen, "hotend_sensor_dropdown");
                    populate_hardware_dropdowns();
                }
            }
            screen = hotend_select_screen;
            break;

        case WizardStep::FAN_SELECT:
            if (!fan_select_screen && wizard_content_area) {
                fan_select_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_fan_select", nullptr);
                if (fan_select_screen) {
                    hotend_fan_dropdown = lv_obj_find_by_name(fan_select_screen, "hotend_fan_dropdown");
                    part_cooling_fan_dropdown = lv_obj_find_by_name(fan_select_screen, "part_cooling_fan_dropdown");
                    populate_hardware_dropdowns();
                }
            }
            screen = fan_select_screen;
            break;

        case WizardStep::LED_SELECT:
            if (!led_select_screen && wizard_content_area) {
                led_select_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_led_select", nullptr);
                if (led_select_screen) {
                    led_main_dropdown = lv_obj_find_by_name(led_select_screen, "led_main_dropdown");
                    populate_hardware_dropdowns();
                }
            }
            screen = led_select_screen;
            break;

        case WizardStep::SUMMARY:
            if (!summary_screen && wizard_content_area) {
                summary_screen = (lv_obj_t*)lv_xml_create(wizard_content_area, "wizard_summary", nullptr);
            }

            // Populate summary labels with selected values
            if (summary_screen) {
                lv_obj_t* summary_printer_name = lv_obj_find_by_name(summary_screen, "summary_printer_name");
                lv_obj_t* summary_printer_type = lv_obj_find_by_name(summary_screen, "summary_printer_type");
                lv_obj_t* summary_host = lv_obj_find_by_name(summary_screen, "summary_host");
                lv_obj_t* summary_bed = lv_obj_find_by_name(summary_screen, "summary_bed");
                lv_obj_t* summary_hotend = lv_obj_find_by_name(summary_screen, "summary_hotend");
                lv_obj_t* summary_part_fan = lv_obj_find_by_name(summary_screen, "summary_part_fan");

                if (summary_printer_name) {
                    lv_label_set_text(summary_printer_name, selected_printer_name.c_str());
                }

                if (summary_printer_type) {
                    lv_label_set_text(summary_printer_type, selected_printer_type.c_str());
                }

                if (summary_host) {
                    const char* host = static_cast<const char*>(lv_subject_get_pointer(&connection_ip));
                    const char* port = static_cast<const char*>(lv_subject_get_pointer(&connection_port));
                    std::string host_port = std::string(host ? host : "127.0.0.1") + ":" + std::string(port ? port : "7125");
                    lv_label_set_text(summary_host, host_port.c_str());
                }

                if (summary_bed) {
                    lv_label_set_text(summary_bed, selected_bed_heater.c_str());
                }

                if (summary_hotend) {
                    lv_label_set_text(summary_hotend, selected_hotend_heater.c_str());
                }

                if (summary_part_fan) {
                    lv_label_set_text(summary_part_fan, selected_part_cooling_fan.c_str());
                }
            }

            screen = summary_screen;
            break;

        default:
            spdlog::error("Invalid wizard step: {}", static_cast<int>(step));
            return;
    }

    if (screen) {
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_HIDDEN);
    }

    update_ui_for_step(step);
}

void ui_wizard_next() {
    if (!validate_current_step()) {
        spdlog::warn("Current wizard step validation failed");
        return;
    }

    if (current_step == WizardStep::SUMMARY) {
        ui_wizard_complete();
        return;
    }

    // Advance to next step
    int next_step_int = static_cast<int>(current_step) + 1;
    if (next_step_int >= static_cast<int>(WizardStep::TOTAL_STEPS)) {
        spdlog::error("Attempted to advance past final wizard step");
        return;
    }

    ui_wizard_goto_step(static_cast<WizardStep>(next_step_int));
}

void ui_wizard_back() {
    if (current_step == WizardStep::WIFI_SETUP) {
        spdlog::warn("Already at first wizard step, cannot go back");
        return;
    }

    int prev_step_int = static_cast<int>(current_step) - 1;
    ui_wizard_goto_step(static_cast<WizardStep>(prev_step_int));
}

WizardStep ui_wizard_get_current_step() {
    return current_step;
}

bool ui_wizard_is_active() {
    return wizard_root != nullptr && !lv_obj_has_flag(wizard_root, LV_OBJ_FLAG_HIDDEN);
}

void ui_wizard_hide() {
    if (wizard_root) {
        lv_obj_add_flag(wizard_root, LV_OBJ_FLAG_HIDDEN);
        spdlog::info("Wizard hidden");
    }
}

void ui_wizard_complete() {
    spdlog::info("Completing wizard - saving configuration");

    // Save Moonraker connection details (already saved during test connection, but save again for safety)
    const char* host = static_cast<const char*>(lv_subject_get_pointer(&connection_ip));
    const char* port_str = static_cast<const char*>(lv_subject_get_pointer(&connection_port));
    int port = port_str ? std::stoi(port_str) : 7125;

    if (host) {
        config_instance->set(config_instance->df() + "moonraker_host", std::string(host));
    }
    config_instance->set(config_instance->df() + "moonraker_port", port);

    // Save printer identification
    config_instance->set(config_instance->df() + "printer_name", selected_printer_name);
    config_instance->set(config_instance->df() + "printer_type", selected_printer_type);
    config_instance->set(config_instance->df() + "printer_type_confidence", printer_type_confidence);

    // Save all hardware mappings to config
    auto& hardware_map = config_instance->get_json(config_instance->df() + "hardware_map");

    // Heated bed mapping
    hardware_map["heated_bed"]["heater"] = selected_bed_heater;
    hardware_map["heated_bed"]["sensor"] = selected_bed_sensor;

    // Hotend mapping
    hardware_map["hotend"]["heater"] = selected_hotend_heater;
    hardware_map["hotend"]["sensor"] = selected_hotend_sensor;

    // Fan mapping
    hardware_map["fans"]["hotend_fan"] = selected_hotend_fan;
    hardware_map["fans"]["part_cooling_fan"] = selected_part_cooling_fan;

    // LED mapping (optional)
    if (!selected_main_led.empty()) {
        hardware_map["leds"]["main"] = selected_main_led;
    }

    config_instance->save();
    spdlog::info("Configuration saved successfully");

    // Hide wizard
    ui_wizard_hide();

    // Invoke completion callback
    if (completion_callback) {
        completion_callback();
    }
}

// ============================================================================
// WiFi Network List Management
// ============================================================================

static void clear_network_list() {
    if (!network_list_container) return;

    // Remove all child widgets that are network items
    // Iterate backwards to avoid index issues when deleting
    int32_t child_count = lv_obj_get_child_count(network_list_container);
    for (int32_t i = child_count - 1; i >= 0; i--) {
        lv_obj_t* child = lv_obj_get_child(network_list_container, i);
        const char* name = (const char*)lv_obj_get_user_data(child);
        // Remove network items (keep labels/placeholders)
        if (name && strcmp(name, "network_item") == 0) {
            lv_obj_delete(child);
        }
    }

    spdlog::debug("[WiFi] Network list cleared");
}

static void populate_network_list(const std::vector<WiFiNetwork>& networks) {
    spdlog::info("[WiFi] populate_network_list called with {} networks", networks.size());

    if (!network_list_container) {
        spdlog::warn("[WiFi] Network list container not found");
        return;
    }

    spdlog::debug("[WiFi] Clearing network list");
    // Clear existing network items
    clear_network_list();
    spdlog::debug("[WiFi] Network list cleared successfully");

    // Hide placeholder if we have networks
    lv_obj_t* placeholder = lv_obj_find_by_name(network_list_container, "network_list_placeholder");
    if (placeholder) {
        if (networks.empty()) {
            lv_obj_remove_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Create network list items
    spdlog::debug("[WiFi] Creating {} network list items", networks.size());
    for (size_t i = 0; i < networks.size(); i++) {
        const auto& network = networks[i];
        spdlog::debug("[WiFi] Creating item {} for network: {}", i, network.ssid);

        lv_obj_t* item = (lv_obj_t*)lv_xml_create(network_list_container, "network_list_item", nullptr);
        if (!item) {
            spdlog::error("[WiFi] Failed to create network list item for {}", network.ssid);
            continue;
        }
        spdlog::debug("[WiFi] Item created successfully for {}", network.ssid);

        // Mark as network item for cleanup
        lv_obj_set_user_data(item, (void*)"network_item");

        // Find child widgets
        lv_obj_t* ssid_label = lv_obj_find_by_name(item, "network_ssid");
        lv_obj_t* signal_label = lv_obj_find_by_name(item, "network_signal");
        lv_obj_t* lock_icon = lv_obj_find_by_name(item, "network_lock");

        // Populate data
        if (ssid_label) {
            lv_label_set_text(ssid_label, network.ssid.c_str());
        }

        if (signal_label) {
            char signal_buf[16];
            snprintf(signal_buf, sizeof(signal_buf), "%d%%", network.signal_strength);
            lv_label_set_text(signal_label, signal_buf);
        }

        // Hide lock icon for open networks
        if (lock_icon) {
            if (network.is_secured) {
                lv_obj_remove_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Add click handler
        lv_obj_add_event_cb(item, on_network_item_clicked, LV_EVENT_CLICKED, (void*)network.ssid.c_str());
    }

    spdlog::info("[WiFi] Populated {} networks in list", networks.size());
}

static void on_network_item_clicked(lv_event_t* e) {
    const char* ssid = (const char*)lv_event_get_user_data(e);
    spdlog::info("[WiFi] Network clicked: {}", ssid ? ssid : "unknown");

    // TODO: Show password modal for encrypted networks
    // TODO: Implement connection flow
}

static void wifi_scan_delay_callback(lv_timer_t* timer) {
    (void)timer;

    // Delete the one-shot timer
    if (wifi_scan_delay_timer) {
        lv_timer_delete(wifi_scan_delay_timer);
        wifi_scan_delay_timer = nullptr;
    }

    spdlog::info("[WiFi] Starting network scan after delay");
    WiFiManager::start_scan(populate_network_list);
}

static void on_wifi_toggle_changed(lv_event_t* e) {
    lv_obj_t* toggle = (lv_obj_t*)lv_event_get_target(e);
    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    spdlog::info("[WiFi] Toggle changed: {}", enabled ? "ON" : "OFF");

    if (enabled) {
        // Enable WiFi and start scanning after 3-second delay
        WiFiManager::set_enabled(true);
        lv_subject_copy_string(&wifi_status, "Scanning for networks...");

        // Un-dim network list container
        if (network_list_container) {
            lv_obj_remove_state(network_list_container, LV_STATE_DISABLED);
            lv_obj_set_style_opa(network_list_container, 255, LV_PART_MAIN);
        }

        // Create 3-second delay timer before starting scan
        wifi_scan_delay_timer = lv_timer_create(wifi_scan_delay_callback, 3000, nullptr);
        lv_timer_set_repeat_count(wifi_scan_delay_timer, 1);  // One-shot timer
    } else {
        // Disable WiFi and stop scanning
        WiFiManager::set_enabled(false);
        WiFiManager::stop_scan();

        // Cancel delay timer if it's running
        if (wifi_scan_delay_timer) {
            lv_timer_delete(wifi_scan_delay_timer);
            wifi_scan_delay_timer = nullptr;
        }

        // Dim network list container and clear networks
        if (network_list_container) {
            lv_obj_add_state(network_list_container, LV_STATE_DISABLED);
            lv_obj_set_style_opa(network_list_container, UI_DISABLED_OPA, LV_PART_MAIN);
        }

        clear_network_list();
        lv_subject_copy_string(&wifi_status, "Enable WiFi to scan for networks");
    }
}

// ============================================================================
// Connection Timeout Handler
// ============================================================================

static void check_connection_timeout(lv_timer_t* timer) {
    (void)timer;

    // Check if connection test is in progress and has timed out (10 seconds)
    if (connection_test_start_time > 0 && !connection_tested) {
        uint32_t elapsed = lv_tick_get() - connection_test_start_time;
        if (elapsed > 10000) {  // 10 second timeout
            spdlog::warn("Connection test timed out after 10 seconds");
            connection_tested = true;
            connection_successful = false;
            connection_test_start_time = 0;

            lv_subject_copy_string(&connection_status, "Failed: Connection timeout");
        }
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

static void on_back_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("Wizard: Back button clicked");
    ui_wizard_back();
}

static void on_next_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("Wizard: Next button clicked");
    ui_wizard_next();
}

static void on_test_connection_clicked(lv_event_t* e) {
    (void)e;
    spdlog::info("Wizard: Testing connection to Moonraker");

    // Read host and port from textareas (subjects are updated on input events)
    lv_obj_t* ip_input = lv_obj_find_by_name(connection_screen, "ip_input");
    lv_obj_t* port_input = lv_obj_find_by_name(connection_screen, "port_input");

    const char* host = ip_input ? lv_textarea_get_text(ip_input) : "";
    const char* port_str = port_input ? lv_textarea_get_text(port_input) : "";

    if (!host || strlen(host) == 0) {
        lv_subject_copy_string(&connection_status, "Error: Please enter network address");
        return;
    }

    if (!port_str || strlen(port_str) == 0) {
        lv_subject_copy_string(&connection_status, "Error: Please enter port number");
        return;
    }

    // Validate IP/hostname
    if (!is_valid_ip_or_hostname(host)) {
        lv_subject_copy_string(&connection_status, "Error: Invalid IP address or hostname");
        return;
    }

    // Validate port
    if (!is_valid_port(port_str)) {
        lv_subject_copy_string(&connection_status, "Error: Invalid port (1-65535)");
        return;
    }

    lv_subject_copy_string(&connection_status, "Testing connection...");

    // Reset connection state
    connection_tested = false;
    connection_successful = false;
    connection_test_start_time = lv_tick_get();  // Start timeout timer

    // Build WebSocket URL
    std::string ws_url = "ws://" + std::string(host) + ":" + std::string(port_str) + "/websocket";

    // Copy host/port to local variables for lambda capture
    std::string host_copy = host;
    std::string port_copy = port_str;

    // Test connection (async)
    mr_client_instance->connect(ws_url.c_str(),
        [ws_url, host_copy, port_copy]() {
            // On connected
            spdlog::info("Connection test successful: {}", ws_url);
            connection_tested = true;
            connection_successful = true;
            connection_test_start_time = 0;  // Stop timeout timer

            lv_subject_copy_string(&connection_status, "Connected! Discovering printer...");

            // Update config with new connection details
            int port = std::stoi(port_copy);
            config_instance->set(config_instance->df() + "moonraker_host", host_copy);
            config_instance->set(config_instance->df() + "moonraker_port", port);
            config_instance->save();
            spdlog::info("Updated Moonraker config: {}:{}", host_copy, port);

            // Start auto-discovery
            mr_client_instance->discover_printer([]() {
                spdlog::info("Printer discovery completed");
                lv_subject_copy_string(&connection_status, "Ready! Click Next to continue.");
            });
        },
        []() {
            // On disconnected (only mark as failed if we haven't already succeeded)
            if (!connection_successful) {
                spdlog::warn("Connection test failed or disconnected");
                connection_tested = true;
                connection_successful = false;
                connection_test_start_time = 0;  // Stop timeout timer

                lv_subject_copy_string(&connection_status, "Failed: Could not connect");
            }
        }
    );
}

// ============================================================================
// UI Update Helpers
// ============================================================================

static void update_ui_for_step(WizardStep step) {
    // Update title
    switch (step) {
        case WizardStep::WIFI_SETUP:
            lv_subject_copy_string(&wizard_title, "WiFi Setup");
            break;
        case WizardStep::CONNECTION:
            lv_subject_copy_string(&wizard_title, "Connect to Moonraker");
            break;
        case WizardStep::PRINTER_IDENTIFY:
            lv_subject_copy_string(&wizard_title, "Identify Your Printer");
            break;
        case WizardStep::BED_SELECT:
            lv_subject_copy_string(&wizard_title, "Select Heated Bed");
            break;
        case WizardStep::HOTEND_SELECT:
            lv_subject_copy_string(&wizard_title, "Select Hotend");
            break;
        case WizardStep::FAN_SELECT:
            lv_subject_copy_string(&wizard_title, "Select Fans");
            break;
        case WizardStep::LED_SELECT:
            lv_subject_copy_string(&wizard_title, "Select LEDs (Optional)");
            break;
        case WizardStep::SUMMARY:
            lv_subject_copy_string(&wizard_title, "Configuration Summary");
            break;
        default:
            break;
    }

    // Update progress text
    char progress_text[32];
    snprintf(progress_text, sizeof(progress_text), "Step %d of %d",
             static_cast<int>(step) + 1,
             static_cast<int>(WizardStep::TOTAL_STEPS));
    lv_subject_copy_string(&wizard_progress, progress_text);

    // Update Next button text
    if (step == WizardStep::SUMMARY) {
        lv_subject_copy_string(&wizard_next_button_text, "Finish");
    } else {
        lv_subject_copy_string(&wizard_next_button_text, "Next");
    }

    // Show/hide Back button
    if (step == WizardStep::WIFI_SETUP) {
        lv_obj_add_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool validate_current_step() {
    switch (current_step) {
        case WizardStep::WIFI_SETUP:
            // WiFi setup is optional - always allow proceeding
            return true;

        case WizardStep::CONNECTION:
            if (!connection_tested || !connection_successful) {
                lv_subject_copy_string(&connection_status, "Please test connection first");
                return false;
            }
            return true;

        case WizardStep::PRINTER_IDENTIFY:
            if (printer_name_input && printer_type_roller) {
                const char* name = lv_textarea_get_text(printer_name_input);
                if (!name || strlen(name) == 0) {
                    lv_subject_copy_string(&printer_detection_status, "Please enter a printer name");
                    return false;
                }

                char type_buf[64];
                lv_roller_get_selected_str(printer_type_roller, type_buf, sizeof(type_buf));
                selected_printer_name = name;
                selected_printer_type = type_buf;
                return true;
            }
            return false;

        case WizardStep::BED_SELECT:
            if (bed_heater_dropdown && bed_sensor_dropdown) {
                char heater_buf[64], sensor_buf[64];
                lv_dropdown_get_selected_str(bed_heater_dropdown, heater_buf, sizeof(heater_buf));
                lv_dropdown_get_selected_str(bed_sensor_dropdown, sensor_buf, sizeof(sensor_buf));

                selected_bed_heater = heater_buf;
                selected_bed_sensor = sensor_buf;
                return true;
            }
            return false;

        case WizardStep::HOTEND_SELECT:
            if (hotend_heater_dropdown && hotend_sensor_dropdown) {
                char heater_buf[64], sensor_buf[64];
                lv_dropdown_get_selected_str(hotend_heater_dropdown, heater_buf, sizeof(heater_buf));
                lv_dropdown_get_selected_str(hotend_sensor_dropdown, sensor_buf, sizeof(sensor_buf));

                selected_hotend_heater = heater_buf;
                selected_hotend_sensor = sensor_buf;
                return true;
            }
            return false;

        case WizardStep::FAN_SELECT:
            if (hotend_fan_dropdown && part_cooling_fan_dropdown) {
                char hotend_fan_buf[64], part_fan_buf[64];
                lv_dropdown_get_selected_str(hotend_fan_dropdown, hotend_fan_buf, sizeof(hotend_fan_buf));
                lv_dropdown_get_selected_str(part_cooling_fan_dropdown, part_fan_buf, sizeof(part_fan_buf));

                selected_hotend_fan = hotend_fan_buf;
                selected_part_cooling_fan = part_fan_buf;
                return true;
            }
            return false;

        case WizardStep::LED_SELECT:
            if (led_main_dropdown) {
                char led_buf[64];
                lv_dropdown_get_selected_str(led_main_dropdown, led_buf, sizeof(led_buf));
                selected_main_led = (strcmp(led_buf, "(None)") == 0) ? "" : led_buf;
            }
            return true;

        case WizardStep::SUMMARY:
            return true;

        default:
            return false;
    }
}

static void populate_hardware_dropdowns() {
    if (!mr_client_instance) {
        spdlog::error("MoonrakerClient not initialized");
        return;
    }

    const auto& heaters = mr_client_instance->get_heaters();
    const auto& sensors = mr_client_instance->get_sensors();
    const auto& fans = mr_client_instance->get_fans();

    // Bed heater dropdown (heaters only)
    if (bed_heater_dropdown) {
        std::string options = get_dropdown_options_from_vector(heaters);
        lv_dropdown_set_options(bed_heater_dropdown, options.c_str());

        // Auto-select "heater_bed" if available
        for (size_t i = 0; i < heaters.size(); i++) {
            if (heaters[i] == "heater_bed") {
                lv_dropdown_set_selected(bed_heater_dropdown, i);
                break;
            }
        }
    }

    // Bed sensor dropdown (heaters + sensors)
    if (bed_sensor_dropdown) {
        std::vector<std::string> all_sensors = heaters;
        all_sensors.insert(all_sensors.end(), sensors.begin(), sensors.end());
        std::string options = get_dropdown_options_from_vector(all_sensors);
        lv_dropdown_set_options(bed_sensor_dropdown, options.c_str());

        // Auto-select "heater_bed" if available
        for (size_t i = 0; i < all_sensors.size(); i++) {
            if (all_sensors[i] == "heater_bed") {
                lv_dropdown_set_selected(bed_sensor_dropdown, i);
                break;
            }
        }
    }

    // Hotend heater dropdown
    if (hotend_heater_dropdown) {
        std::string options = get_dropdown_options_from_vector(heaters);
        lv_dropdown_set_options(hotend_heater_dropdown, options.c_str());

        // Auto-select "extruder" if available
        for (size_t i = 0; i < heaters.size(); i++) {
            if (heaters[i] == "extruder") {
                lv_dropdown_set_selected(hotend_heater_dropdown, i);
                break;
            }
        }
    }

    // Hotend sensor dropdown
    if (hotend_sensor_dropdown) {
        std::vector<std::string> all_sensors = heaters;
        all_sensors.insert(all_sensors.end(), sensors.begin(), sensors.end());
        std::string options = get_dropdown_options_from_vector(all_sensors);
        lv_dropdown_set_options(hotend_sensor_dropdown, options.c_str());

        // Auto-select "extruder" if available
        for (size_t i = 0; i < all_sensors.size(); i++) {
            if (all_sensors[i] == "extruder") {
                lv_dropdown_set_selected(hotend_sensor_dropdown, i);
                break;
            }
        }
    }

    // Hotend fan dropdown
    if (hotend_fan_dropdown) {
        std::string options = get_dropdown_options_from_vector(fans);
        lv_dropdown_set_options(hotend_fan_dropdown, options.c_str());

        // Auto-select "heater_fan hotend_fan" if available
        for (size_t i = 0; i < fans.size(); i++) {
            if (fans[i].find("hotend") != std::string::npos) {
                lv_dropdown_set_selected(hotend_fan_dropdown, i);
                break;
            }
        }
    }

    // Part cooling fan dropdown
    if (part_cooling_fan_dropdown) {
        std::string options = get_dropdown_options_from_vector(fans);
        lv_dropdown_set_options(part_cooling_fan_dropdown, options.c_str());

        // Auto-select "fan" if available
        for (size_t i = 0; i < fans.size(); i++) {
            if (fans[i] == "fan") {
                lv_dropdown_set_selected(part_cooling_fan_dropdown, i);
                break;
            }
        }
    }

    // LED dropdown (with "None" option)
    if (led_main_dropdown) {
        std::string options = get_dropdown_options_from_vector({}, "(None)");
        lv_dropdown_set_options(led_main_dropdown, options.c_str());
    }
}

static std::string get_dropdown_options_from_vector(const std::vector<std::string>& items, const std::string& none_option) {
    std::string options;

    if (!none_option.empty()) {
        options = none_option;
        if (!items.empty()) {
            options += "\n";
        }
    }

    for (size_t i = 0; i < items.size(); i++) {
        options += items[i];
        if (i < items.size() - 1) {
            options += "\n";
        }
    }

    return options;
}

// ============================================================================
// Printer Detection
// ============================================================================

static void detect_printer_type() {
    if (!mr_client_instance) {
        spdlog::error("MoonrakerClient not available for printer detection");
        lv_subject_copy_string(&printer_detection_status, "Unable to detect - please select type");
        printer_type_confidence = 0;
        return;
    }

    spdlog::info("Starting printer type detection");
    lv_subject_copy_string(&printer_detection_status, "Detecting printer...");

    // Query printer.info for hostname
    mr_client_instance->send_jsonrpc("printer.info", json::object(), [](json& result) {
        if (!result.contains("hostname")) {
            spdlog::warn("No hostname in printer.info response");
            lv_subject_copy_string(&printer_detection_status, "Unable to detect - please select type");
            printer_type_confidence = 0;
            return;
        }

        std::string hostname = result["hostname"];
        spdlog::info("Printer hostname: {}", hostname);

        // Convert to lowercase for matching
        std::string hostname_lower = hostname;
        std::transform(hostname_lower.begin(), hostname_lower.end(), hostname_lower.begin(), ::tolower);

        int confidence = 0;
        std::string detected_type = "Generic Klipper";
        int dropdown_index = 0;

        // Voron detection (hostname)
        if (hostname_lower.find("voron") != std::string::npos) {
            confidence += 40;
            if (hostname_lower.find("v2") != std::string::npos) {
                if (hostname_lower.find("350") != std::string::npos) {
                    detected_type = "Voron V2 350";
                    dropdown_index = 6;
                } else if (hostname_lower.find("300") != std::string::npos) {
                    detected_type = "Voron V2 300";
                    dropdown_index = 5;
                } else if (hostname_lower.find("250") != std::string::npos) {
                    detected_type = "Voron V2 250";
                    dropdown_index = 4;
                } else {
                    detected_type = "Voron V2 300"; // Default V2 size
                    dropdown_index = 5;
                }
                confidence += 30;
            } else if (hostname_lower.find("trident") != std::string::npos) {
                detected_type = "Voron Trident";
                dropdown_index = 7;
                confidence += 30;
            } else if (hostname_lower.find("v0") != std::string::npos) {
                detected_type = "Voron V0";
                dropdown_index = 1;
                confidence += 30;
            } else if (hostname_lower.find("v1") != std::string::npos) {
                detected_type = "Voron V1";
                dropdown_index = 2;
                confidence += 30;
            } else if (hostname_lower.find("switchwire") != std::string::npos) {
                detected_type = "Voron Switchwire";
                dropdown_index = 8;
                confidence += 30;
            } else if (hostname_lower.find("legacy") != std::string::npos) {
                detected_type = "Voron Legacy";
                dropdown_index = 9;
                confidence += 30;
            }
        }
        // Creality detection
        else if (hostname_lower.find("ender") != std::string::npos || hostname_lower.find("creality") != std::string::npos ||
                 hostname_lower.find("k1") != std::string::npos) {
            confidence += 40;

            // K1 series detection (check BEFORE Ender to avoid conflicts)
            if (hostname_lower.find("k1") != std::string::npos) {
                if (hostname_lower.find("max") != std::string::npos) {
                    detected_type = "Creality K1 Max";
                    dropdown_index = 15;
                    confidence += 30;
                } else if (hostname_lower.find("k1c") != std::string::npos || hostname_lower.find("k1-c") != std::string::npos) {
                    detected_type = "Creality K1C";
                    dropdown_index = 16;
                    confidence += 30;
                } else {
                    detected_type = "Creality K1";
                    dropdown_index = 14;
                    confidence += 30;
                }
            }
            // Ender series detection
            else if (hostname_lower.find("ender3") != std::string::npos) {
                if (hostname_lower.find("s1") != std::string::npos) {
                    detected_type = "Creality Ender 3 S1";
                    dropdown_index = 12;
                } else if (hostname_lower.find("v2") != std::string::npos) {
                    detected_type = "Creality Ender 3 V2";
                    dropdown_index = 11;
                } else {
                    detected_type = "Creality Ender 3";
                    dropdown_index = 10;
                }
                confidence += 30;
            } else if (hostname_lower.find("ender5") != std::string::npos) {
                detected_type = "Creality Ender 5";
                dropdown_index = 13;
                confidence += 30;
            } else if (hostname_lower.find("cr-10") != std::string::npos || hostname_lower.find("cr10") != std::string::npos) {
                detected_type = "Creality CR-10";
                dropdown_index = 17;
                confidence += 30;
            } else if (hostname_lower.find("cr-6") != std::string::npos || hostname_lower.find("cr6") != std::string::npos) {
                detected_type = "Creality CR-6 SE";
                dropdown_index = 18;
                confidence += 30;
            }
        }
        // Prusa detection
        else if (hostname_lower.find("prusa") != std::string::npos || hostname_lower.find("mk3") != std::string::npos ||
                 hostname_lower.find("mk4") != std::string::npos || hostname_lower.find("i3") != std::string::npos) {
            confidence += 40;
            if (hostname_lower.find("mk4") != std::string::npos) {
                detected_type = "Prusa MK4";
                dropdown_index = 20;
                confidence += 30;
            } else if (hostname_lower.find("mk3") != std::string::npos) {
                detected_type = "Prusa i3 MK3/MK3S";
                dropdown_index = 19;
                confidence += 30;
            } else if (hostname_lower.find("mini") != std::string::npos) {
                detected_type = "Prusa Mini/Mini+";
                dropdown_index = 21;
                confidence += 30;
            } else if (hostname_lower.find("xl") != std::string::npos) {
                detected_type = "Prusa XL";
                dropdown_index = 22;
                confidence += 30;
            }
        }
        // Anycubic detection
        else if (hostname_lower.find("anycubic") != std::string::npos || hostname_lower.find("kobra") != std::string::npos ||
                 hostname_lower.find("vyper") != std::string::npos || hostname_lower.find("chiron") != std::string::npos) {
            confidence += 40;
            if (hostname_lower.find("kobra") != std::string::npos) {
                detected_type = "Anycubic Kobra";
                dropdown_index = 23;
                confidence += 30;
            } else if (hostname_lower.find("vyper") != std::string::npos) {
                detected_type = "Anycubic Vyper";
                dropdown_index = 24;
                confidence += 30;
            } else if (hostname_lower.find("chiron") != std::string::npos) {
                detected_type = "Anycubic Chiron";
                dropdown_index = 25;
                confidence += 30;
            }
        }
        // Rat Rig detection
        else if (hostname_lower.find("ratrig") != std::string::npos || hostname_lower.find("vcore") != std::string::npos ||
                 hostname_lower.find("v-core") != std::string::npos || hostname_lower.find("vminion") != std::string::npos) {
            confidence += 40;
            if (hostname_lower.find("vcore") != std::string::npos || hostname_lower.find("v-core") != std::string::npos) {
                detected_type = "Rat Rig V-Core 3";
                dropdown_index = 26;
                confidence += 30;
            } else if (hostname_lower.find("vminion") != std::string::npos || hostname_lower.find("v-minion") != std::string::npos) {
                detected_type = "Rat Rig V-Minion";
                dropdown_index = 27;
                confidence += 30;
            }
        }
        // FLSUN detection
        else if (hostname_lower.find("flsun") != std::string::npos) {
            confidence += 40;
            if (hostname_lower.find("v400") != std::string::npos) {
                detected_type = "FLSUN V400";
                dropdown_index = 30;
                confidence += 30;
            } else if (hostname_lower.find("super") != std::string::npos || hostname_lower.find("racer") != std::string::npos) {
                detected_type = "FLSUN Super Racer";
                dropdown_index = 29;
                confidence += 30;
            } else if (hostname_lower.find("qq") != std::string::npos) {
                detected_type = "FLSUN QQ-S";
                dropdown_index = 28;
                confidence += 30;
            }
        }
        // FlashForge detection
        else if (hostname_lower.find("flashforge") != std::string::npos || hostname_lower.find("ad5m") != std::string::npos ||
                 hostname_lower.find("5mpro") != std::string::npos || hostname_lower.find("adventurer") != std::string::npos) {
            confidence += 40;
            if (hostname_lower.find("5mpro") != std::string::npos || hostname_lower.find("5m pro") != std::string::npos ||
                hostname_lower.find("ad5m pro") != std::string::npos) {
                detected_type = "FlashForge AD5M Pro";
                dropdown_index = 32;
                confidence += 30;
            } else if (hostname_lower.find("ad5m") != std::string::npos || hostname_lower.find("5m") != std::string::npos) {
                detected_type = "FlashForge AD5M";
                dropdown_index = 31;
                confidence += 30;
            }
        }

        // Update confidence and status display
        printer_type_confidence = confidence;

        char status_text[128];
        if (confidence >= 90) {
            snprintf(status_text, sizeof(status_text), "Detected: %s (High Confidence)", detected_type.c_str());
        } else if (confidence >= 50) {
            snprintf(status_text, sizeof(status_text), "Detected: %s (Medium Confidence)", detected_type.c_str());
        } else {
            snprintf(status_text, sizeof(status_text), "Unable to detect - please select type");
        }

        lv_subject_copy_string(&printer_detection_status, status_text);

        // Pre-select detected type in roller if confidence is high enough
        if (confidence >= 50 && printer_type_roller) {
            lv_roller_set_selected(printer_type_roller, dropdown_index, LV_ANIM_OFF);
        }

        spdlog::info("Printer detection complete: {} (confidence: {})", detected_type, confidence);
    });
}
