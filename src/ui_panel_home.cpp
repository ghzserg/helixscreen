#include "ui_panel_home.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static lv_obj_t* home_panel = nullptr;

// Widget references for direct updates
static lv_obj_t* network_icon_label = nullptr;
static lv_obj_t* network_text_label = nullptr;
static lv_obj_t* light_icon_label = nullptr;

// Subjects for reactive data binding
static lv_subject_t status_subject;
static lv_subject_t temp_subject;
static lv_subject_t network_icon_subject;
static lv_subject_t network_label_subject;
static lv_subject_t network_color_subject;
static lv_subject_t light_icon_color_subject;

static char status_buffer[128];
static char temp_buffer[32];
static char network_icon_buffer[8];
static char network_label_buffer[32];
static char network_color_buffer[16];
static char light_icon_color_buffer[16];

static bool subjects_initialized = false;
static bool light_on = false;
static network_type_t current_network = NETWORK_WIFI;

// Forward declarations
static void light_toggle_event_cb(lv_event_t* e);
static void network_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
static void light_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

void ui_panel_home_init_subjects() {
    if (subjects_initialized) {
        printf("WARNING: Home panel subjects already initialized\n");
        return;
    }

    printf("DEBUG: Initializing home panel subjects\n");

    // Initialize subjects with default values
    lv_subject_init_string(&status_subject, status_buffer, NULL, sizeof(status_buffer), "Hasta la vista, misprints!");
    lv_subject_init_string(&temp_subject, temp_buffer, NULL, sizeof(temp_buffer), "30 °C");
    lv_subject_init_string(&network_icon_subject, network_icon_buffer, NULL, sizeof(network_icon_buffer), ICON_WIFI);
    lv_subject_init_string(&network_label_subject, network_label_buffer, NULL, sizeof(network_label_buffer), "Wi-Fi");
    lv_subject_init_string(&network_color_subject, network_color_buffer, NULL, sizeof(network_color_buffer), "0xff4444");
    lv_subject_init_string(&light_icon_color_subject, light_icon_color_buffer, NULL, sizeof(light_icon_color_buffer), "0x909090");

    // Register subjects globally so XML can bind to them
    lv_xml_register_subject(NULL, "status_text", &status_subject);
    lv_xml_register_subject(NULL, "temp_text", &temp_subject);
    lv_xml_register_subject(NULL, "network_icon", &network_icon_subject);
    lv_xml_register_subject(NULL, "network_label", &network_label_subject);
    lv_xml_register_subject(NULL, "network_color", &network_color_subject);
    lv_xml_register_subject(NULL, "light_icon_color", &light_icon_color_subject);

    // Register event callbacks BEFORE loading XML
    lv_xml_register_event_cb(NULL, "light_toggle_cb", light_toggle_event_cb);

    subjects_initialized = true;
    printf("DEBUG: Registered subjects: status_text, temp_text, network_icon, network_label, network_color, light_icon_color\n");
    printf("DEBUG: Registered event callback: light_toggle_cb\n");
}

void ui_panel_home_setup_observers(lv_obj_t* panel) {
    if (!subjects_initialized) {
        printf("ERROR: Subjects not initialized! Call ui_panel_home_init_subjects() first!\n");
        return;
    }

    home_panel = panel;

    // Use LVGL 9's name-based widget lookup - resilient to layout changes
    network_icon_label = lv_obj_find_by_name(home_panel, "network_icon");
    network_text_label = lv_obj_find_by_name(home_panel, "network_label");
    light_icon_label = lv_obj_find_by_name(home_panel, "light_icon");

    if (!network_icon_label || !network_text_label || !light_icon_label) {
        printf("ERROR: Failed to find named widgets (net_icon=%p, net_label=%p, light=%p)\n",
               network_icon_label, network_text_label, light_icon_label);
        return;
    }

    // Add observers to watch subjects and update widgets
    lv_subject_add_observer(&network_icon_subject, network_observer_cb, nullptr);
    lv_subject_add_observer(&network_label_subject, network_observer_cb, nullptr);
    lv_subject_add_observer(&network_color_subject, network_observer_cb, nullptr);
    lv_subject_add_observer(&light_icon_color_subject, light_observer_cb, nullptr);

    printf("DEBUG: Home panel observers set up successfully\n");
}

lv_obj_t* ui_panel_home_create(lv_obj_t* parent) {
    printf("DEBUG: ui_panel_home_create called\n");

    if (!subjects_initialized) {
        printf("ERROR: Subjects not initialized! Call ui_panel_home_init_subjects() first!\n");
        return nullptr;
    }

    // Create the XML component (will bind to subjects automatically)
    home_panel = (lv_obj_t*)lv_xml_create(parent, "home_panel", nullptr);
    if (!home_panel) {
        printf("ERROR: Failed to create home_panel from XML\n");
        return nullptr;
    }

    // Setup observers
    ui_panel_home_setup_observers(home_panel);

    printf("DEBUG: XML home_panel created successfully with reactive observers\n");
    return home_panel;
}

void ui_panel_home_update(const char* status_text, int temp) {
    // Update subjects - all bound widgets update automatically
    if (status_text) {
        lv_subject_copy_string(&status_subject, status_text);
        printf("DEBUG: Updated status_text subject to: %s\n", status_text);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d °C", temp);
    lv_subject_copy_string(&temp_subject, buf);
    printf("DEBUG: Updated temp_text subject to: %s\n", buf);
}

void ui_panel_home_set_network(network_type_t type) {
    current_network = type;

    switch (type) {
        case NETWORK_WIFI:
            lv_subject_copy_string(&network_icon_subject, ICON_WIFI);
            lv_subject_copy_string(&network_label_subject, "Wi-Fi");
            lv_subject_copy_string(&network_color_subject, "0xff4444");  // Primary color
            break;
        case NETWORK_ETHERNET:
            lv_subject_copy_string(&network_icon_subject, ICON_ETHERNET);
            lv_subject_copy_string(&network_label_subject, "Ethernet");
            lv_subject_copy_string(&network_color_subject, "0xff4444");  // Primary color
            break;
        case NETWORK_DISCONNECTED:
            lv_subject_copy_string(&network_icon_subject, ICON_WIFI_SLASH);
            lv_subject_copy_string(&network_label_subject, "Disconnected");
            lv_subject_copy_string(&network_color_subject, "0x909090");  // Text secondary
            break;
    }
    printf("DEBUG: Updated network status to type %d\n", type);
}

void ui_panel_home_set_light(bool is_on) {
    light_on = is_on;

    if (is_on) {
        // Light is on - show bright yellow/white
        lv_subject_copy_string(&light_icon_color_subject, "0xFFD700");
    } else {
        // Light is off - show muted
        lv_subject_copy_string(&light_icon_color_subject, "0x909090");
    }
    printf("DEBUG: Updated light state to: %s\n", is_on ? "ON" : "OFF");
}

bool ui_panel_home_get_light_state() {
    return light_on;
}

static void light_toggle_event_cb(lv_event_t* e) {
    (void)e;  // Unused parameter

    // Toggle the light state
    ui_panel_home_set_light(!light_on);

    // TODO: Add callback to send command to Klipper
    // For now, just log the state change
    printf("Light toggled: %s\n", light_on ? "ON" : "OFF");
}

// Observer callback for network icon/label/color changes
static void network_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)observer;  // Unused parameter

    if (!network_icon_label || !network_text_label) {
        return;
    }

    // Update network icon text
    const char* icon = lv_subject_get_string(&network_icon_subject);
    if (icon) {
        lv_label_set_text(network_icon_label, icon);
    }

    // Update network label text
    const char* label = lv_subject_get_string(&network_label_subject);
    if (label) {
        lv_label_set_text(network_text_label, label);
    }

    // Update network icon color
    const char* color_str = lv_subject_get_string(&network_color_subject);
    if (color_str) {
        uint32_t color = strtoul(color_str, nullptr, 16);
        lv_obj_set_style_text_color(network_icon_label, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(network_text_label, lv_color_hex(color), 0);
    }

    printf("DEBUG: Network observer updated widgets\n");
}

// Observer callback for light icon color changes
static void light_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)observer;  // Unused parameter
    (void)subject;   // Unused parameter

    if (!light_icon_label) {
        return;
    }

    // Update light icon color
    const char* color_str = lv_subject_get_string(&light_icon_color_subject);
    if (color_str) {
        uint32_t color = strtoul(color_str, nullptr, 16);
        lv_obj_set_style_text_color(light_icon_label, lv_color_hex(color), 0);
    }

    printf("DEBUG: Light observer updated icon color\n");
}
