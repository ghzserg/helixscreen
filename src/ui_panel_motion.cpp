// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_motion.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_event_safety.h"
#include "ui_jog_pad.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <memory>

// Distance values in mm (indexed by jog_distance_t)
static const float distance_values[] = {0.1f, 1.0f, 10.0f, 100.0f};

// ============================================================================
// CONSTRUCTOR
// ============================================================================

MotionPanel::MotionPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents
    std::strcpy(pos_x_buf_, "X:    --  mm");
    std::strcpy(pos_y_buf_, "Y:    --  mm");
    std::strcpy(pos_z_buf_, "Z:    --  mm");
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void MotionPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize position subjects with default placeholder values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_x_subject_, pos_x_buf_, "X:    --  mm", "motion_pos_x");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_y_subject_, pos_y_buf_, "Y:    --  mm", "motion_pos_y");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_z_subject_, pos_z_buf_, "Z:    --  mm", "motion_pos_z");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized: X/Y/Z position displays", get_name());
}

void MotionPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up event handlers...", get_name());

    // Use standard overlay panel setup (wires header, back button, handles responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Setup all button groups
    setup_distance_buttons();
    setup_jog_pad();
    setup_z_buttons();
    setup_home_buttons();

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// PRIVATE SETUP HELPERS
// ============================================================================

void MotionPanel::setup_distance_buttons() {
    const char* dist_names[] = {"dist_0_1", "dist_1", "dist_10", "dist_100"};

    for (int i = 0; i < 4; i++) {
        dist_buttons_[i] = lv_obj_find_by_name(panel_, dist_names[i]);
        if (dist_buttons_[i]) {
            // Pass 'this' as user_data for trampoline
            lv_obj_add_event_cb(dist_buttons_[i], on_distance_button_clicked, LV_EVENT_CLICKED, this);
        }
    }

    update_distance_buttons();
    spdlog::debug("[{}] Distance selector (4 buttons)", get_name());
}

void MotionPanel::setup_jog_pad() {
    // Find overlay_content to access motion panel widgets
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return;
    }

    // Find jog pad container from XML and replace it with the widget
    lv_obj_t* jog_pad_container = lv_obj_find_by_name(overlay_content, "jog_pad_container");
    if (!jog_pad_container) {
        spdlog::warn("[{}] jog_pad_container NOT FOUND in XML!", get_name());
        return;
    }

    // Get parent container (left_column)
    lv_obj_t* left_column = lv_obj_get_parent(jog_pad_container);

    // Calculate jog pad size as 80% of available vertical height (after header)
    lv_display_t* disp = lv_display_get_default();
    lv_coord_t screen_height = lv_display_get_vertical_resolution(disp);

    // Get header height (varies by screen size: 50-70px)
    lv_obj_t* header = lv_obj_find_by_name(panel_, "overlay_header");
    lv_coord_t header_height = header ? lv_obj_get_height(header) : 60;

    // Available height = screen height - header - padding (40px top+bottom)
    lv_coord_t available_height = screen_height - header_height - 40;

    // Jog pad = 80% of available height (leaves room for distance/home buttons)
    lv_coord_t jog_size = (lv_coord_t)(available_height * 0.80f);

    // Delete placeholder container
    lv_obj_delete(jog_pad_container);

    // Create jog pad widget
    jog_pad_ = ui_jog_pad_create(left_column);
    if (jog_pad_) {
        lv_obj_set_name(jog_pad_, "jog_pad");
        lv_obj_set_width(jog_pad_, jog_size);
        lv_obj_set_height(jog_pad_, jog_size);
        lv_obj_set_align(jog_pad_, LV_ALIGN_CENTER);

        // Set callbacks - pass 'this' as user_data
        ui_jog_pad_set_jog_callback(jog_pad_, jog_pad_jog_cb, this);
        ui_jog_pad_set_home_callback(jog_pad_, jog_pad_home_cb, this);

        // Set initial distance
        ui_jog_pad_set_distance(jog_pad_, current_distance_);

        spdlog::info("[{}] Jog pad widget created (size: {}px)", get_name(), jog_size);
    } else {
        spdlog::error("[{}] Failed to create jog pad widget!", get_name());
    }
}

void MotionPanel::setup_z_buttons() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) return;

    const char* z_names[] = {"z_up_10", "z_up_1", "z_down_1", "z_down_10"};
    int z_found = 0;

    for (const char* name : z_names) {
        lv_obj_t* btn = lv_obj_find_by_name(overlay_content, name);
        if (btn) {
            spdlog::debug("[{}] Found '{}' at {}", get_name(), name, (void*)btn);
            lv_obj_add_event_cb(btn, on_z_button_clicked, LV_EVENT_CLICKED, this);
            z_found++;
        } else {
            spdlog::warn("[{}] Z button '{}' NOT FOUND!", get_name(), name);
        }
    }

    spdlog::debug("[{}] Z-axis controls ({}/4 buttons found)", get_name(), z_found);
}

void MotionPanel::setup_home_buttons() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) return;

    const char* home_names[] = {"home_all", "home_x", "home_y", "home_z"};

    for (const char* name : home_names) {
        lv_obj_t* btn = lv_obj_find_by_name(overlay_content, name);
        if (btn) {
            lv_obj_add_event_cb(btn, on_home_button_clicked, LV_EVENT_CLICKED, this);
        }
    }

    spdlog::debug("[{}] Home buttons (4 buttons)", get_name());
}

void MotionPanel::update_distance_buttons() {
    for (int i = 0; i < 4; i++) {
        if (dist_buttons_[i]) {
            if (i == current_distance_) {
                // Active state - theme handles colors
                lv_obj_add_state(dist_buttons_[i], LV_STATE_CHECKED);
            } else {
                // Inactive state - theme handles colors
                lv_obj_remove_state(dist_buttons_[i], LV_STATE_CHECKED);
            }
        }
    }

    // Update jog pad widget distance if it exists
    if (jog_pad_) {
        ui_jog_pad_set_distance(jog_pad_, current_distance_);
    }
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void MotionPanel::handle_distance_button(lv_obj_t* btn) {
    // Find which button was clicked
    for (int i = 0; i < 4; i++) {
        if (btn == dist_buttons_[i]) {
            current_distance_ = (jog_distance_t)i;
            spdlog::info("[{}] Distance selected: {:.1f}mm", get_name(), distance_values[i]);
            update_distance_buttons();
            return;
        }
    }
}

void MotionPanel::handle_z_button(const char* name) {
    spdlog::info("[{}] Z button callback fired! Button name: '{}'", get_name(), name ? name : "(null)");

    if (!name) {
        spdlog::error("[{}] Button has no name!", get_name());
        return;
    }

    if (strcmp(name, "z_up_10") == 0) {
        set_position(current_x_, current_y_, current_z_ + 10.0f);
        spdlog::info("[{}] Z jog: +10mm (now {:.1f}mm)", get_name(), current_z_);
    } else if (strcmp(name, "z_up_1") == 0) {
        set_position(current_x_, current_y_, current_z_ + 1.0f);
        spdlog::info("[{}] Z jog: +1mm (now {:.1f}mm)", get_name(), current_z_);
    } else if (strcmp(name, "z_down_1") == 0) {
        set_position(current_x_, current_y_, current_z_ - 1.0f);
        spdlog::info("[{}] Z jog: -1mm (now {:.1f}mm)", get_name(), current_z_);
    } else if (strcmp(name, "z_down_10") == 0) {
        set_position(current_x_, current_y_, current_z_ - 10.0f);
        spdlog::info("[{}] Z jog: -10mm (now {:.1f}mm)", get_name(), current_z_);
    } else {
        spdlog::error("[{}] Unknown button name: '{}'", get_name(), name);
    }
}

void MotionPanel::handle_home_button(const char* name) {
    if (!name) return;

    if (strcmp(name, "home_all") == 0) {
        home('A');
    } else if (strcmp(name, "home_x") == 0) {
        home('X');
    } else if (strcmp(name, "home_y") == 0) {
        home('Y');
    } else if (strcmp(name, "home_z") == 0) {
        home('Z');
    }
}

// ============================================================================
// JOG PAD CALLBACKS
// ============================================================================

void MotionPanel::jog_pad_jog_cb(jog_direction_t direction, float distance_mm, void* user_data) {
    auto* self = static_cast<MotionPanel*>(user_data);
    if (self) {
        self->jog(direction, distance_mm);
    }
}

void MotionPanel::jog_pad_home_cb(void* user_data) {
    auto* self = static_cast<MotionPanel*>(user_data);
    if (self) {
        self->home('A'); // Home XY
    }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void MotionPanel::on_distance_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_distance_button_clicked");
    auto* self = static_cast<MotionPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->handle_distance_button(btn);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void MotionPanel::on_z_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_z_button_clicked");
    auto* self = static_cast<MotionPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const char* name = lv_obj_get_name(btn);
        self->handle_z_button(name);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void MotionPanel::on_home_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_home_button_clicked");
    auto* self = static_cast<MotionPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const char* name = lv_obj_get_name(btn);
        self->handle_home_button(name);
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// PUBLIC API
// ============================================================================

void MotionPanel::set_position(float x, float y, float z) {
    current_x_ = x;
    current_y_ = y;
    current_z_ = z;

    // Update subjects (will automatically update bound UI elements)
    snprintf(pos_x_buf_, sizeof(pos_x_buf_), "X: %6.1f mm", x);
    snprintf(pos_y_buf_, sizeof(pos_y_buf_), "Y: %6.1f mm", y);
    snprintf(pos_z_buf_, sizeof(pos_z_buf_), "Z: %6.1f mm", z);

    lv_subject_copy_string(&pos_x_subject_, pos_x_buf_);
    lv_subject_copy_string(&pos_y_subject_, pos_y_buf_);
    lv_subject_copy_string(&pos_z_subject_, pos_z_buf_);
}

void MotionPanel::set_distance(jog_distance_t dist) {
    if (dist >= 0 && dist <= 3) {
        current_distance_ = dist;
        update_distance_buttons();
    }
}

void MotionPanel::jog(jog_direction_t direction, float distance_mm) {
    const char* dir_names[] = {"N(+Y)",    "S(-Y)",    "E(+X)",    "W(-X)",
                               "NE(+X+Y)", "NW(-X+Y)", "SE(+X-Y)", "SW(-X-Y)"};

    spdlog::info("[{}] Jog command: {} {:.1f}mm", get_name(), dir_names[direction], distance_mm);

    // Mock position update (simulate jog movement)
    float dx = 0.0f, dy = 0.0f;

    switch (direction) {
    case JOG_DIR_N:
        dy = distance_mm;
        break;
    case JOG_DIR_S:
        dy = -distance_mm;
        break;
    case JOG_DIR_E:
        dx = distance_mm;
        break;
    case JOG_DIR_W:
        dx = -distance_mm;
        break;
    case JOG_DIR_NE:
        dx = distance_mm;
        dy = distance_mm;
        break;
    case JOG_DIR_NW:
        dx = -distance_mm;
        dy = distance_mm;
        break;
    case JOG_DIR_SE:
        dx = distance_mm;
        dy = -distance_mm;
        break;
    case JOG_DIR_SW:
        dx = -distance_mm;
        dy = -distance_mm;
        break;
    }

    set_position(current_x_ + dx, current_y_ + dy, current_z_);

    // TODO: Send actual G-code command via Moonraker API
    // Example: G0 X{new_x} Y{new_y} F{feedrate}
}

void MotionPanel::home(char axis) {
    spdlog::info("[{}] Home command: {} axis", get_name(), axis);

    // Mock position update (simulate homing)
    switch (axis) {
    case 'X':
        set_position(0.0f, current_y_, current_z_);
        break;
    case 'Y':
        set_position(current_x_, 0.0f, current_z_);
        break;
    case 'Z':
        set_position(current_x_, current_y_, 0.0f);
        break;
    case 'A':
        set_position(0.0f, 0.0f, 0.0f);
        break; // All axes
    }

    // TODO: Send actual G-code command via Moonraker API
    // Example: G28 X (home X), G28 (home all)
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<MotionPanel> g_motion_panel;

MotionPanel& get_global_motion_panel() {
    if (!g_motion_panel) {
        g_motion_panel = std::make_unique<MotionPanel>(get_printer_state(), nullptr);
    }
    return *g_motion_panel;
}
