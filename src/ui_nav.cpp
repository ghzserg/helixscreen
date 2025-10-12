#include "ui_nav.h"
#include "ui_theme.h"
#include "lvgl/lvgl.h"
#include <cstdio>
#include <cstdlib>  // for atoi

// Active panel tracking
static lv_subject_t active_panel_subject;
static ui_panel_id_t active_panel = UI_PANEL_HOME;

// Icon color subjects (one per navbar button)
static lv_subject_t icon_color_subjects[UI_PANEL_COUNT];

// Panel widget tracking for show/hide
static lv_obj_t* panel_widgets[UI_PANEL_COUNT] = {nullptr};

// Subjects initialization flag
static bool subjects_initialized = false;

// Observer callback - updates all icon colors when active panel changes
static void active_panel_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t new_active_panel = lv_subject_get_int(subject);

    // Update all icon color subjects based on which panel is active
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (i == new_active_panel) {
            lv_subject_set_color(&icon_color_subjects[i], UI_COLOR_PRIMARY);
        } else {
            lv_subject_set_color(&icon_color_subjects[i], UI_COLOR_NAV_INACTIVE);
        }
    }

    // Show/hide panels if widgets are set
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (panel_widgets[i]) {
            if (i == new_active_panel) {
                lv_obj_remove_flag(panel_widgets[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(panel_widgets[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// Observer callback for icon color changes - updates label style
static void icon_color_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    lv_obj_t* label = (lv_obj_t*)lv_observer_get_target(observer);
    lv_color_t color = lv_subject_get_color(subject);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

// Observer callback for icon color changes - updates image recolor style
static void icon_image_color_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    lv_obj_t* image = (lv_obj_t*)lv_observer_get_target(observer);
    lv_color_t color = lv_subject_get_color(subject);
    lv_obj_set_style_img_recolor(image, color, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(image, 255, LV_PART_MAIN);
}

// Button click event handler - switches active panel
static void nav_button_clicked_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int panel_id = (int)(uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED) {
        ui_nav_set_active((ui_panel_id_t)panel_id);
    }
}

void ui_nav_init() {
    if (subjects_initialized) {
        LV_LOG_WARN("Navigation subjects already initialized");
        return;
    }

    LV_LOG_USER("Initializing navigation reactive subjects...");

    // Initialize active panel subject (starts at home)
    lv_subject_init_int(&active_panel_subject, UI_PANEL_HOME);

    // Initialize icon color subjects (all inactive except home)
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (i == UI_PANEL_HOME) {
            lv_subject_init_color(&icon_color_subjects[i], UI_COLOR_PRIMARY);
        } else {
            lv_subject_init_color(&icon_color_subjects[i], UI_COLOR_NAV_INACTIVE);
        }
    }

    // Register subjects for XML binding
    lv_xml_register_subject(NULL, "active_panel", &active_panel_subject);
    lv_xml_register_subject(NULL, "nav_icon_0_color", &icon_color_subjects[0]);
    lv_xml_register_subject(NULL, "nav_icon_1_color", &icon_color_subjects[1]);
    lv_xml_register_subject(NULL, "nav_icon_2_color", &icon_color_subjects[2]);
    lv_xml_register_subject(NULL, "nav_icon_3_color", &icon_color_subjects[3]);
    lv_xml_register_subject(NULL, "nav_icon_4_color", &icon_color_subjects[4]);
    lv_xml_register_subject(NULL, "nav_icon_5_color", &icon_color_subjects[5]);

    // Add observer to active panel subject to update icon colors
    lv_subject_add_observer(&active_panel_subject, active_panel_observer_cb, NULL);

    subjects_initialized = true;

    LV_LOG_USER("Navigation subjects initialized successfully");
}

void ui_nav_wire_events(lv_obj_t* navbar) {
    if (!navbar) {
        LV_LOG_ERROR("NULL navbar provided to ui_nav_wire_events");
        return;
    }

    if (!subjects_initialized) {
        LV_LOG_ERROR("Navigation subjects not initialized! Call ui_nav_init() first!");
        return;
    }

    // Ensure navbar container doesn't block clicks to children
    lv_obj_remove_flag(navbar, LV_OBJ_FLAG_CLICKABLE);

    // Name-based widget lookup for navigation buttons and icons (order matches ui_panel_id_t enum)
    const char* button_names[] = {"nav_btn_home", "nav_btn_print_select", "nav_btn_controls", "nav_btn_filament", "nav_btn_settings", "nav_btn_advanced"};
    const char* icon_names[] = {"nav_icon_home", "nav_icon_print_select", "nav_icon_controls", "nav_icon_filament", "nav_icon_settings", "nav_icon_advanced"};

    // Bind colors to icon widgets and add click event handlers to buttons
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        lv_obj_t* btn = lv_obj_find_by_name(navbar, button_names[i]);
        lv_obj_t* icon_widget = lv_obj_find_by_name(navbar, icon_names[i]);

        if (!btn || !icon_widget) {
            LV_LOG_ERROR("Failed to find nav button/icon %d: btn=%p, icon=%p", i, btn, icon_widget);
            continue;
        }

        // Check if it's an image or label and use appropriate observer
        if (lv_obj_check_type(icon_widget, &lv_image_class)) {
            // Image widget - bind img_recolor to icon color subject
            lv_subject_add_observer_obj(&icon_color_subjects[i], icon_image_color_observer_cb, icon_widget, NULL);
        } else {
            // Label widget - bind text_color to icon color subject
            lv_subject_add_observer_obj(&icon_color_subjects[i], icon_color_observer_cb, icon_widget, NULL);
        }

        // Make icon widget non-clickable so clicks pass through to button
        lv_obj_add_flag(icon_widget, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(icon_widget, LV_OBJ_FLAG_CLICKABLE);

        // Ensure button is clickable and add event handler
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, nav_button_clicked_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }

    // Force update all icon color subjects now that bindings exist
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (i == active_panel) {
            lv_subject_set_color(&icon_color_subjects[i], UI_COLOR_PRIMARY);
        } else {
            lv_subject_set_color(&icon_color_subjects[i], UI_COLOR_NAV_INACTIVE);
        }
    }
}

void ui_nav_set_active(ui_panel_id_t panel_id) {
    if (panel_id >= UI_PANEL_COUNT) {
        LV_LOG_ERROR("Invalid panel ID: %d", panel_id);
        return;
    }

    if (panel_id == active_panel) {
        return;
    }

    // Update active panel subject - this triggers observer and icon color updates
    lv_subject_set_int(&active_panel_subject, panel_id);
    active_panel = panel_id;
}

ui_panel_id_t ui_nav_get_active() {
    return active_panel;
}

void ui_nav_set_panels(lv_obj_t** panels) {
    if (!panels) {
        LV_LOG_ERROR("NULL panels array provided");
        return;
    }

    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        panel_widgets[i] = panels[i];
    }

    // Hide all panels except active one
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (panel_widgets[i]) {
            if (i == active_panel) {
                lv_obj_remove_flag(panel_widgets[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(panel_widgets[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    LV_LOG_USER("Panel widgets registered for show/hide management");
}
