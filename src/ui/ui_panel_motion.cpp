// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_motion.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_jog_pad.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_panel_common.h"
#include "ui_panel_singleton_macros.h"
#include "ui_subject_registry.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "format_utils.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "subject_managed_panel.h"
#include "theme_manager.h"
#include "unit_conversions.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <memory>

// Forward declarations for XML event callbacks
static void on_motion_z_up_10(lv_event_t* e);
static void on_motion_z_up_1(lv_event_t* e);
static void on_motion_z_down_1(lv_event_t* e);
static void on_motion_z_down_10(lv_event_t* e);

// ============================================================================
// Global Instance (via DEFINE_GLOBAL_PANEL macro)
// ============================================================================

DEFINE_GLOBAL_PANEL(MotionPanel, motion)

// ============================================================================
// Constructor
// ============================================================================

MotionPanel::MotionPanel() {
    // Initialize buffer contents (axis labels are in XML, values only here)
    std::strcpy(pos_x_buf_, "— mm");
    std::strcpy(pos_y_buf_, "— mm");
    std::strcpy(pos_z_buf_, "— mm");
    std::strcpy(z_axis_label_buf_, "Z Axis"); // Default before kinematics detected

    spdlog::debug("[MotionPanel] Instance created");
}

MotionPanel::~MotionPanel() {
    // SubjectManager (subjects_) handles deinit automatically via RAII
    // No need to call deinit_subjects() manually
}

// ============================================================================
// Subject Initialization
// ============================================================================

void MotionPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[{}] Subjects already initialized", get_name());
        return;
    }

    spdlog::debug("[{}] Initializing subjects", get_name());

    // Initialize position subjects with default placeholder values
    // Axis labels are in XML, subjects contain values only
    UI_MANAGED_SUBJECT_STRING(pos_x_subject_, pos_x_buf_, "— mm", "motion_pos_x", subjects_);
    UI_MANAGED_SUBJECT_STRING(pos_y_subject_, pos_y_buf_, "— mm", "motion_pos_y", subjects_);
    UI_MANAGED_SUBJECT_STRING(pos_z_subject_, pos_z_buf_, "— mm", "motion_pos_z", subjects_);

    // Z-axis label: "Bed" (cartesian) or "Print Head" (corexy/delta)
    UI_MANAGED_SUBJECT_STRING(z_axis_label_subject_, z_axis_label_buf_, "Z Axis",
                              "motion_z_axis_label", subjects_);

    // Register PrinterState observers (RAII - auto-removed on destruction)
    register_position_observers();

    subjects_initialized_ = true;

    // Sync initial position values (observers only fire on change, not on subscribe)
    // Without this, panel shows dashes until next position update even if printer is homed
    int x_centimm = lv_subject_get_int(get_printer_state().get_gcode_position_x_subject());
    int y_centimm = lv_subject_get_int(get_printer_state().get_gcode_position_y_subject());
    gcode_z_centimm_ = lv_subject_get_int(get_printer_state().get_gcode_position_z_subject());
    actual_z_centimm_ = lv_subject_get_int(get_printer_state().get_position_z_subject());
    int bed_moves = lv_subject_get_int(get_printer_state().get_printer_bed_moves_subject());

    // Update X position display
    float x = static_cast<float>(helix::units::from_centimm(x_centimm));
    current_x_ = x;
    helix::fmt::format_distance_mm(x, 2, pos_x_buf_, sizeof(pos_x_buf_));
    lv_subject_copy_string(&pos_x_subject_, pos_x_buf_);

    // Update Y position display
    float y = static_cast<float>(helix::units::from_centimm(y_centimm));
    current_y_ = y;
    helix::fmt::format_distance_mm(y, 2, pos_y_buf_, sizeof(pos_y_buf_));
    lv_subject_copy_string(&pos_y_subject_, pos_y_buf_);

    // Update Z position display (uses gcode_z_centimm_ and actual_z_centimm_ we just set)
    current_z_ = static_cast<float>(helix::units::from_centimm(gcode_z_centimm_));
    update_z_display();

    // Update Z axis label
    update_z_axis_label(bed_moves != 0);

    spdlog::debug("[{}] Subjects initialized: X/Y/Z position + Z-axis label + observers ({} "
                  "subjects managed)",
                  get_name(), subjects_.count());
}

void MotionPanel::deinit_subjects() {
    // NOTE: This method exists for API symmetry with init_subjects() and to support
    // explicit cleanup if needed. However, it's NOT called in the destructor because
    // SubjectManager handles cleanup via RAII. This is intentional - RAII is preferred.
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[{}] Deinitializing subjects", get_name());

    // SubjectManager handles deinitialization of all registered subjects
    subjects_.deinit_all();

    subjects_initialized_ = false;
    spdlog::debug("[{}] Subjects deinitialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void MotionPanel::register_callbacks() {
    if (callbacks_registered_) {
        spdlog::debug("[{}] Callbacks already registered", get_name());
        return;
    }

    spdlog::debug("[{}] Registering event callbacks", get_name());

    // Register Z-axis button event callbacks for XML event_cb
    lv_xml_register_event_cb(nullptr, "on_motion_z_up_10", on_motion_z_up_10);
    lv_xml_register_event_cb(nullptr, "on_motion_z_up_1", on_motion_z_up_1);
    lv_xml_register_event_cb(nullptr, "on_motion_z_down_1", on_motion_z_down_1);
    lv_xml_register_event_cb(nullptr, "on_motion_z_down_10", on_motion_z_down_10);

    callbacks_registered_ = true;
    spdlog::debug("[{}] Event callbacks registered", get_name());
}

// ============================================================================
// Create
// ============================================================================

lv_obj_t* MotionPanel::create(lv_obj_t* parent) {
    if (!parent) {
        spdlog::error("[{}] Cannot create: null parent", get_name());
        return nullptr;
    }

    spdlog::debug("[{}] Creating overlay from XML", get_name());

    parent_screen_ = parent;

    // Reset cleanup flag when (re)creating
    cleanup_called_ = false;

    // Create overlay from XML
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "motion_panel", nullptr));

    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create from XML", get_name());
        return nullptr;
    }

    // Use standard overlay panel setup (wires header, handles responsive padding)
    // Note: Back button is wired via header_bar.xml default callback (on_header_back_clicked)
    ui_overlay_panel_setup_standard(overlay_root_, parent_screen_, "overlay_header",
                                    "overlay_content");

    // Setup jog pad and Z-axis controls
    setup_jog_pad();
    setup_z_buttons();

    // Initially hidden
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created successfully", get_name());
    return overlay_root_;
}

// ============================================================================
// Lifecycle Hooks
// ============================================================================

void MotionPanel::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[{}] on_activate()", get_name());

    // Nothing special needed for motion panel on activation
}

void MotionPanel::on_deactivate() {
    spdlog::debug("[{}] on_deactivate()", get_name());

    // Call base class
    OverlayBase::on_deactivate();
}

// ============================================================================
// Jog Pad Setup
// ============================================================================

void MotionPanel::setup_jog_pad() {
    // Find overlay_content to access motion panel widgets
    lv_obj_t* overlay_content = lv_obj_find_by_name(overlay_root_, "overlay_content");
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
    lv_obj_t* header = lv_obj_find_by_name(overlay_root_, "overlay_header");
    lv_coord_t header_height = header ? lv_obj_get_height(header) : 60;

    // Available height = screen height - header - padding (40px top+bottom)
    lv_coord_t available_height = screen_height - header_height - 40;

    // Jog pad = 80% of available height (leaves room for distance/home buttons)
    lv_coord_t jog_size = (lv_coord_t)(available_height * 0.80f);

    // Delete placeholder container
    lv_obj_safe_delete(jog_pad_container);

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
    // Z buttons use declarative XML event_cb - callbacks registered in register_callbacks()
    spdlog::debug("[{}] Z-axis controls (declarative XML event_cb)", get_name());
}

// ============================================================================
// Position Observers
// ============================================================================

void MotionPanel::register_position_observers() {
    // Subscribe to PrinterState position updates so UI reflects real printer position
    // Using observer factory for type-safe lambda-based observers with RAII cleanup

    using helix::ui::observe_int_sync;

    // Use gcode position (commanded) for X/Y display and jog calculations
    position_x_observer_ = observe_int_sync<MotionPanel>(
        get_printer_state().get_gcode_position_x_subject(), this,
        [](MotionPanel* self, int centimm) {
            if (!self->subjects_initialized_)
                return;
            float x = static_cast<float>(helix::units::from_centimm(centimm));
            self->current_x_ = x;
            helix::fmt::format_distance_mm(x, 2, self->pos_x_buf_, sizeof(self->pos_x_buf_));
            lv_subject_copy_string(&self->pos_x_subject_, self->pos_x_buf_);
        });

    position_y_observer_ = observe_int_sync<MotionPanel>(
        get_printer_state().get_gcode_position_y_subject(), this,
        [](MotionPanel* self, int centimm) {
            if (!self->subjects_initialized_)
                return;
            float y = static_cast<float>(helix::units::from_centimm(centimm));
            self->current_y_ = y;
            helix::fmt::format_distance_mm(y, 2, self->pos_y_buf_, sizeof(self->pos_y_buf_));
            lv_subject_copy_string(&self->pos_y_subject_, self->pos_y_buf_);
        });

    // Z needs both gcode (commanded) and actual (with mesh compensation) positions
    // Display shows commanded with actual in brackets when they differ
    gcode_z_observer_ = observe_int_sync<MotionPanel>(
        get_printer_state().get_gcode_position_z_subject(), this,
        [](MotionPanel* self, int centimm) {
            if (!self->subjects_initialized_)
                return;
            self->gcode_z_centimm_ = centimm;
            self->current_z_ = static_cast<float>(helix::units::from_centimm(centimm));
            self->update_z_display();
        });

    actual_z_observer_ = observe_int_sync<MotionPanel>(get_printer_state().get_position_z_subject(),
                                                       this, [](MotionPanel* self, int centimm) {
                                                           if (!self->subjects_initialized_)
                                                               return;
                                                           self->actual_z_centimm_ = centimm;
                                                           self->update_z_display();
                                                       });

    // Watch for kinematics changes to update Z-axis label ("Bed" vs "Print Head")
    bed_moves_observer_ =
        observe_int_sync<MotionPanel>(get_printer_state().get_printer_bed_moves_subject(), this,
                                      [](MotionPanel* self, int bed_moves) {
                                          if (!self->subjects_initialized_)
                                              return;
                                          self->update_z_axis_label(bed_moves != 0);
                                      });

    spdlog::debug("[{}] Position + kinematics observers registered (observer factory)", get_name());
}

// Observer callbacks migrated to lambda-based observer factory pattern
// See register_position_observers() for inline observers

void MotionPanel::update_z_axis_label(bool bed_moves) {
    bed_moves_ = bed_moves; // Store for Z button direction inversion
    const char* label = bed_moves ? "Bed" : "Print Head";
    std::strncpy(z_axis_label_buf_, label, sizeof(z_axis_label_buf_) - 1);
    z_axis_label_buf_[sizeof(z_axis_label_buf_) - 1] = '\0';
    lv_subject_copy_string(&z_axis_label_subject_, z_axis_label_buf_);
    spdlog::debug("[{}] Z-axis label updated: {} (bed_moves={})", get_name(), label, bed_moves);
}

void MotionPanel::update_z_display() {
    float gcode_z = static_cast<float>(helix::units::from_centimm(gcode_z_centimm_));
    float actual_z = static_cast<float>(helix::units::from_centimm(actual_z_centimm_));

    // Show actual in brackets only when it differs from commanded (e.g., mesh compensation)
    // Use 1 centimm (0.01mm) threshold to avoid floating point noise
    if (std::abs(gcode_z_centimm_ - actual_z_centimm_) > 1) {
        // Special case: compound format not covered by formatter
        snprintf(pos_z_buf_, sizeof(pos_z_buf_), "%.2f [%.2f] mm", gcode_z, actual_z);
    } else {
        helix::fmt::format_distance_mm(gcode_z, 2, pos_z_buf_, sizeof(pos_z_buf_));
    }
    lv_subject_copy_string(&pos_z_subject_, pos_z_buf_);
}

// ============================================================================
// Z Button Handler
// ============================================================================

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

    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        // Z feedrate: 600 mm/min (10 mm/s) - slower for safety
        constexpr double Z_FEEDRATE = 600.0;

        api->move_axis(
            'Z', distance, Z_FEEDRATE, []() { spdlog::debug("[MotionPanel] Z jog complete"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Z jog failed: {}", err.user_message());
            });
    }
}

// ============================================================================
// Jog Pad Callbacks
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
// Public API
// ============================================================================

void MotionPanel::set_position(float x, float y, float z) {
    current_x_ = x;
    current_y_ = y;
    current_z_ = z;

    // When set directly via API, gcode and actual are the same
    int z_centimm = helix::units::to_centimm(static_cast<double>(z));
    gcode_z_centimm_ = z_centimm;
    actual_z_centimm_ = z_centimm;

    if (!subjects_initialized_)
        return;

    // Update subjects (will automatically update bound UI elements)
    helix::fmt::format_distance_mm(x, 2, pos_x_buf_, sizeof(pos_x_buf_));
    helix::fmt::format_distance_mm(y, 2, pos_y_buf_, sizeof(pos_y_buf_));

    lv_subject_copy_string(&pos_x_subject_, pos_x_buf_);
    lv_subject_copy_string(&pos_y_subject_, pos_y_buf_);
    update_z_display(); // Also copies to pos_z_subject_
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
    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        // Default feedrate: 6000 mm/min (100 mm/s) for XY jog moves
        constexpr double JOG_FEEDRATE = 6000.0;

        if (dx != 0.0f) {
            api->move_axis(
                'X', static_cast<double>(dx), JOG_FEEDRATE,
                []() { spdlog::debug("[MotionPanel] X jog complete"); },
                [](const MoonrakerError& err) {
                    NOTIFY_ERROR("X jog failed: {}", err.user_message());
                });
        }
        if (dy != 0.0f) {
            api->move_axis(
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

    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        // Convert axis char to string for API ("" for all, "X", "Y", "Z", or "XY")
        std::string axes_str;
        if (axis == 'A') {
            axes_str = ""; // Empty string = home all
        } else {
            axes_str = std::string(1, axis);
        }

        api->home_axes(
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

// ============================================================================
// Static Callbacks for XML event_cb (Z-axis buttons)
// ============================================================================

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
