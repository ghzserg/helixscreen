// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_motion.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_jog_pad.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <memory>

// Distance values in mm (indexed by jog_distance_t)
static const float distance_values[] = {0.1f, 1.0f, 10.0f, 100.0f};

// Forward declarations for XML event callbacks
static void on_motion_z_up_10(lv_event_t* e);
static void on_motion_z_up_1(lv_event_t* e);
static void on_motion_z_down_1(lv_event_t* e);
static void on_motion_z_down_10(lv_event_t* e);

MotionPanel::MotionPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents
    std::strcpy(pos_x_buf_, "X:    --  mm");
    std::strcpy(pos_y_buf_, "Y:    --  mm");
    std::strcpy(pos_z_buf_, "Z:    --  mm");
    std::strcpy(z_axis_label_buf_, "Z Axis"); // Default before kinematics detected
}

void MotionPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize position subjects with default placeholder values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_x_subject_, pos_x_buf_, "X:    --  mm", "motion_pos_x");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_y_subject_, pos_y_buf_, "Y:    --  mm", "motion_pos_y");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pos_z_subject_, pos_z_buf_, "Z:    --  mm", "motion_pos_z");

    // Z-axis label: "Bed" (cartesian) or "Print Head" (corexy/delta)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(z_axis_label_subject_, z_axis_label_buf_, "Z Axis",
                                        "motion_z_axis_label");

    // Register Z-axis button event callbacks for XML event_cb
    lv_xml_register_event_cb(nullptr, "on_motion_z_up_10", on_motion_z_up_10);
    lv_xml_register_event_cb(nullptr, "on_motion_z_up_1", on_motion_z_up_1);
    lv_xml_register_event_cb(nullptr, "on_motion_z_down_1", on_motion_z_down_1);
    lv_xml_register_event_cb(nullptr, "on_motion_z_down_10", on_motion_z_down_10);

    // Register PrinterState observers (RAII - auto-removed on destruction)
    register_position_observers();

    subjects_initialized_ = true;
    spdlog::debug(
        "[{}] Subjects initialized: X/Y/Z position + Z-axis label + observers + Z callbacks",
        get_name());
}

void MotionPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up event handlers...", get_name());

    // Use standard overlay panel setup (wires header, back button, handles responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Setup all button groups
    setup_distance_buttons();
    setup_jog_pad();
    setup_z_buttons();
    setup_home_buttons();

    spdlog::debug("[{}] Setup complete!", get_name());
}

void MotionPanel::setup_distance_buttons() {
    const char* dist_names[] = {"dist_0_1", "dist_1", "dist_10", "dist_100"};

    for (int i = 0; i < 4; i++) {
        dist_buttons_[i] = lv_obj_find_by_name(panel_, dist_names[i]);
        if (dist_buttons_[i]) {
            // Pass 'this' as user_data for trampoline
            lv_obj_add_event_cb(dist_buttons_[i], on_distance_button_clicked, LV_EVENT_CLICKED,
                                this);
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

        spdlog::debug("[{}] Jog pad widget created (size: {}px)", get_name(), jog_size);
    } else {
        spdlog::error("[{}] Failed to create jog pad widget!", get_name());
    }
}

void MotionPanel::setup_z_buttons() {
    // Z buttons use declarative XML event_cb - callbacks registered in init_subjects()
    // No imperative lv_obj_add_event_cb() needed
    spdlog::debug("[{}] Z-axis controls (declarative XML event_cb)", get_name());
}

void MotionPanel::setup_home_buttons() {
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content)
        return;

    const char* home_names[] = {"home_all", "home_x", "home_y", "home_z"};

    for (const char* name : home_names) {
        lv_obj_t* btn = lv_obj_find_by_name(overlay_content, name);
        if (btn) {
            lv_obj_add_event_cb(btn, on_home_button_clicked, LV_EVENT_CLICKED, this);
        }
    }

    spdlog::debug("[{}] Home buttons (4 buttons)", get_name());
}

void MotionPanel::register_position_observers() {
    // Subscribe to PrinterState position updates so UI reflects real printer position
    // Using ObserverGuard for RAII - observers automatically removed on destruction

    position_x_observer_ =
        ObserverGuard(printer_state_.get_position_x_subject(), on_position_x_changed, this);

    position_y_observer_ =
        ObserverGuard(printer_state_.get_position_y_subject(), on_position_y_changed, this);

    position_z_observer_ =
        ObserverGuard(printer_state_.get_position_z_subject(), on_position_z_changed, this);

    // Watch for kinematics changes to update Z-axis label ("Bed" vs "Print Head")
    bed_moves_observer_ =
        ObserverGuard(printer_state_.get_printer_bed_moves_subject(), on_bed_moves_changed, this);

    spdlog::debug("[{}] Position + kinematics observers registered (RAII ObserverGuard)",
                  get_name());
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

void MotionPanel::on_position_x_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MotionPanel*>(lv_observer_get_user_data(observer));
    if (!self || !self->subjects_initialized_)
        return;

    float x = static_cast<float>(lv_subject_get_int(subject));
    self->current_x_ = x;
    snprintf(self->pos_x_buf_, sizeof(self->pos_x_buf_), "X: %6.1f mm", x);
    lv_subject_copy_string(&self->pos_x_subject_, self->pos_x_buf_);
}

void MotionPanel::on_position_y_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MotionPanel*>(lv_observer_get_user_data(observer));
    if (!self || !self->subjects_initialized_)
        return;

    float y = static_cast<float>(lv_subject_get_int(subject));
    self->current_y_ = y;
    snprintf(self->pos_y_buf_, sizeof(self->pos_y_buf_), "Y: %6.1f mm", y);
    lv_subject_copy_string(&self->pos_y_subject_, self->pos_y_buf_);
}

void MotionPanel::on_position_z_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MotionPanel*>(lv_observer_get_user_data(observer));
    if (!self || !self->subjects_initialized_)
        return;

    float z = static_cast<float>(lv_subject_get_int(subject));
    self->current_z_ = z;
    snprintf(self->pos_z_buf_, sizeof(self->pos_z_buf_), "Z: %6.1f mm", z);
    lv_subject_copy_string(&self->pos_z_subject_, self->pos_z_buf_);
}

void MotionPanel::on_bed_moves_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MotionPanel*>(lv_observer_get_user_data(observer));
    if (!self || !self->subjects_initialized_)
        return;

    bool bed_moves = lv_subject_get_int(subject) != 0;
    self->update_z_axis_label(bed_moves);
}

void MotionPanel::update_z_axis_label(bool bed_moves) {
    bed_moves_ = bed_moves; // Store for Z button direction inversion
    const char* label = bed_moves ? "Bed" : "Print Head";
    std::strncpy(z_axis_label_buf_, label, sizeof(z_axis_label_buf_) - 1);
    z_axis_label_buf_[sizeof(z_axis_label_buf_) - 1] = '\0';
    lv_subject_copy_string(&z_axis_label_subject_, z_axis_label_buf_);
    spdlog::debug("[{}] Z-axis label updated: {} (bed_moves={})", get_name(), label, bed_moves);
}

void MotionPanel::handle_distance_button(lv_obj_t* btn) {
    // Find which button was clicked
    for (int i = 0; i < 4; i++) {
        if (btn == dist_buttons_[i]) {
            current_distance_ = (jog_distance_t)i;
            spdlog::debug("[{}] Distance selected: {:.1f}mm", get_name(), distance_values[i]);
            update_distance_buttons();
            return;
        }
    }
}

void MotionPanel::handle_z_button(const char* name) {
    spdlog::debug("[{}] Z button callback fired! Button name: '{}'", get_name(),
                  name ? name : "(null)");

    if (!name) {
        spdlog::error("[{}] Button has no name!", get_name());
        return;
    }

    // Determine Z distance from button name
    // Up arrow = positive visual direction, Down arrow = negative visual direction
    double distance = 0.0;
    if (strcmp(name, "z_up_10") == 0) {
        distance = 10.0;
    } else if (strcmp(name, "z_up_1") == 0) {
        distance = 1.0;
    } else if (strcmp(name, "z_down_1") == 0) {
        distance = -1.0;
    } else if (strcmp(name, "z_down_10") == 0) {
        distance = -10.0;
    } else {
        spdlog::error("[{}] Unknown button name: '{}'", get_name(), name);
        return;
    }

    // For cartesian printers (bed moves on Z), invert direction so arrows match physical motion:
    // - Up arrow = bed moves UP toward nozzle = G-code Z- (bed rises, gap decreases)
    // - Down arrow = bed moves DOWN away from nozzle = G-code Z+ (bed lowers, gap increases)
    if (bed_moves_) {
        distance = -distance;
        spdlog::debug("[{}] Cartesian printer: inverted Z direction for bed movement", get_name());
    }

    spdlog::debug("[{}] Z jog: {:+.0f}mm (bed_moves={})", get_name(), distance, bed_moves_);

    if (api_) {
        // Z feedrate: 600 mm/min (10 mm/s) - slower for safety
        constexpr double Z_FEEDRATE = 600.0;

        api_->move_axis(
            'Z', distance, Z_FEEDRATE, []() { spdlog::debug("[MotionPanel] Z jog complete"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Z jog failed: {}", err.user_message());
            });
    }
}

void MotionPanel::handle_home_button(const char* name) {
    if (!name)
        return;

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

void MotionPanel::on_distance_button_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_distance_button_clicked");
    auto* self = static_cast<MotionPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->handle_distance_button(btn);
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

void MotionPanel::set_position(float x, float y, float z) {
    current_x_ = x;
    current_y_ = y;
    current_z_ = z;

    if (!subjects_initialized_)
        return;

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

    spdlog::debug("[{}] Jog command: {} {:.1f}mm", get_name(), dir_names[direction], distance_mm);

    // Calculate dx/dy from direction
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

    // Send jog commands via Moonraker API
    if (api_) {
        // Default feedrate: 6000 mm/min (100 mm/s) for XY jog moves
        constexpr double JOG_FEEDRATE = 6000.0;

        if (dx != 0.0f) {
            api_->move_axis(
                'X', static_cast<double>(dx), JOG_FEEDRATE,
                []() { spdlog::debug("[MotionPanel] X jog complete"); },
                [](const MoonrakerError& err) {
                    NOTIFY_ERROR("X jog failed: {}", err.user_message());
                });
        }
        if (dy != 0.0f) {
            api_->move_axis(
                'Y', static_cast<double>(dy), JOG_FEEDRATE,
                []() { spdlog::debug("[MotionPanel] Y jog complete"); },
                [](const MoonrakerError& err) {
                    NOTIFY_ERROR("Y jog failed: {}", err.user_message());
                });
        }
    }
}

void MotionPanel::home(char axis) {
    spdlog::debug("[{}] Home command: {} axis", get_name(), axis);

    if (api_) {
        // Convert axis char to string for API ("" for all, "X", "Y", "Z", or "XY")
        std::string axes_str;
        if (axis == 'A') {
            axes_str = ""; // Empty string = home all
        } else {
            axes_str = std::string(1, axis);
        }

        api_->home_axes(
            axes_str,
            [axis]() {
                if (axis == 'A') {
                    NOTIFY_SUCCESS("All axes homed");
                } else {
                    NOTIFY_SUCCESS("{} axis homed", axis);
                }
            },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Homing failed: {}", err.user_message());
            });
    }
}

// Static callbacks for XML event_cb (Z-axis buttons)
static void on_motion_z_up_10(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_motion_z_up_10");
    (void)e;
    get_global_motion_panel().handle_z_button("z_up_10");
    LVGL_SAFE_EVENT_CB_END();
}

static void on_motion_z_up_1(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_motion_z_up_1");
    (void)e;
    get_global_motion_panel().handle_z_button("z_up_1");
    LVGL_SAFE_EVENT_CB_END();
}

static void on_motion_z_down_1(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_motion_z_down_1");
    (void)e;
    get_global_motion_panel().handle_z_button("z_down_1");
    LVGL_SAFE_EVENT_CB_END();
}

static void on_motion_z_down_10(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MotionPanel] on_motion_z_down_10");
    (void)e;
    get_global_motion_panel().handle_z_button("z_down_10");
    LVGL_SAFE_EVENT_CB_END();
}

static std::unique_ptr<MotionPanel> g_motion_panel;

MotionPanel& get_global_motion_panel() {
    if (!g_motion_panel) {
        g_motion_panel = std::make_unique<MotionPanel>(get_printer_state(), nullptr);
    }
    return *g_motion_panel;
}
