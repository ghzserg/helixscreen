// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_status.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_gcode_viewer.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_panel_temp_control.h"
#include "ui_subject_registry.h"
#include "ui_toast.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "config.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "thumbnail_cache.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

// Global instance for legacy API and resize callback
static std::unique_ptr<PrintStatusPanel> g_print_status_panel;

// Forward declarations for XML event callbacks (registered in init_subjects)
static void on_tune_speed_changed_cb(lv_event_t* e);
static void on_tune_flow_changed_cb(lv_event_t* e);
static void on_tune_reset_clicked_cb(lv_event_t* e);

// Z-offset tune callbacks (single handler extracts delta from button name)
static void on_tune_z_offset_cb(lv_event_t* e);
static void on_tune_save_z_offset_cb(lv_event_t* e);

// Helper to get or create the global instance
PrintStatusPanel& get_global_print_status_panel() {
    if (!g_print_status_panel) {
        g_print_status_panel = std::make_unique<PrintStatusPanel>(get_printer_state(), nullptr);
    }
    return *g_print_status_panel;
}

PrintStatusPanel::PrintStatusPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Subscribe to PrinterState temperature subjects (ObserverGuard handles cleanup)
    extruder_temp_observer_ =
        ObserverGuard(printer_state_.get_extruder_temp_subject(), extruder_temp_observer_cb, this);
    extruder_target_observer_ = ObserverGuard(printer_state_.get_extruder_target_subject(),
                                              extruder_target_observer_cb, this);
    bed_temp_observer_ =
        ObserverGuard(printer_state_.get_bed_temp_subject(), bed_temp_observer_cb, this);
    bed_target_observer_ =
        ObserverGuard(printer_state_.get_bed_target_subject(), bed_target_observer_cb, this);

    // Subscribe to print progress and state
    print_progress_observer_ = ObserverGuard(printer_state_.get_print_progress_subject(),
                                             print_progress_observer_cb, this);
    print_state_observer_ =
        ObserverGuard(printer_state_.get_print_state_enum_subject(), print_state_observer_cb, this);
    print_filename_observer_ = ObserverGuard(printer_state_.get_print_filename_subject(),
                                             print_filename_observer_cb, this);

    // Subscribe to speed/flow factors
    speed_factor_observer_ =
        ObserverGuard(printer_state_.get_speed_factor_subject(), speed_factor_observer_cb, this);
    flow_factor_observer_ =
        ObserverGuard(printer_state_.get_flow_factor_subject(), flow_factor_observer_cb, this);
    gcode_z_offset_observer_ = ObserverGuard(printer_state_.get_gcode_z_offset_subject(),
                                             gcode_z_offset_observer_cb, this);

    // Subscribe to layer tracking for G-code viewer ghost layer updates
    print_layer_observer_ = ObserverGuard(printer_state_.get_print_layer_current_subject(),
                                          print_layer_observer_cb, this);

    // Subscribe to excluded objects changes (for syncing from Klipper)
    excluded_objects_observer_ = ObserverGuard(
        printer_state_.get_excluded_objects_version_subject(), excluded_objects_observer_cb, this);

    // Subscribe to print time tracking
    print_duration_observer_ = ObserverGuard(printer_state_.get_print_duration_subject(),
                                             print_duration_observer_cb, this);
    print_time_left_observer_ = ObserverGuard(printer_state_.get_print_time_left_subject(),
                                              print_time_left_observer_cb, this);

    spdlog::debug("[{}] Subscribed to PrinterState subjects", get_name());

    // Load configured LED from wizard settings
    Config* config = Config::get_instance();
    if (config) {
        configured_led_ = config->get<std::string>(helix::wizard::LED_STRIP, "");
        if (!configured_led_.empty()) {
            led_state_observer_ =
                ObserverGuard(printer_state_.get_led_state_subject(), led_state_observer_cb, this);
            spdlog::debug("[{}] Configured LED: {} (observing state)", get_name(), configured_led_);
        }
    }
}

PrintStatusPanel::~PrintStatusPanel() {
    // ObserverGuard handles observer cleanup automatically
    resize_registered_ = false;

    // Clean up exclude object resources
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
        exclude_undo_timer_ = nullptr;
    }
    if (exclude_confirm_dialog_) {
        lv_obj_delete(exclude_confirm_dialog_);
        exclude_confirm_dialog_ = nullptr;
    }
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void PrintStatusPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize all subjects with default values
    // Note: Using "print_filename_display" to avoid collision with PrinterState's "print_filename"
    // This subject contains the formatted display name (path stripped, extension removed)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(filename_subject_, filename_buf_, "No print active",
                                        "print_filename_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(progress_text_subject_, progress_text_buf_, "0%",
                                        "print_progress_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(layer_text_subject_, layer_text_buf_, "Layer 0 / 0",
                                        "print_layer_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(elapsed_subject_, elapsed_buf_, "0h 00m", "print_elapsed");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(remaining_subject_, remaining_buf_, "0h 00m",
                                        "print_remaining");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_temp_subject_, nozzle_temp_buf_, "0 / 0°C",
                                        "nozzle_temp_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_temp_subject_, bed_temp_buf_, "0 / 0°C",
                                        "bed_temp_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(speed_subject_, speed_buf_, "100%", "print_speed_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(flow_subject_, flow_buf_, "100%", "print_flow_text");
    // Pause button icon - MDI icons (pause=F03E4, play=F040A)
    // UTF-8: pause=F3 B0 8F A4, play=F3 B0 90 8A
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pause_button_subject_, pause_button_buf_,
                                        "\xF3\xB0\x8F\xA4", "pause_button_icon");

    // Timelapse button icon (F0567=video, F0568=video-off)
    // MDI icons in Plane 15 (U+F0xxx) use 4-byte UTF-8 encoding
    // Default to video-off (timelapse disabled): U+F0568 = \xF3\xB0\x95\xA8
    UI_SUBJECT_INIT_AND_REGISTER_STRING(timelapse_button_subject_, timelapse_button_buf_,
                                        "\xF3\xB0\x95\xA8", "timelapse_button_icon");

    // Preparing state subjects
    UI_SUBJECT_INIT_AND_REGISTER_INT(preparing_visible_subject_, 0, "preparing_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(preparing_operation_subject_, preparing_operation_buf_,
                                        "Preparing...", "preparing_operation");
    UI_SUBJECT_INIT_AND_REGISTER_INT(preparing_progress_subject_, 0, "preparing_progress");

    // Progress bar subject (integer 0-100 for XML bind_value)

    // Viewer mode subject (0=thumbnail, 1=gcode viewer)
    UI_SUBJECT_INIT_AND_REGISTER_INT(gcode_viewer_mode_subject_, 0, "gcode_viewer_mode");

    // Print complete overlay visibility (0=hidden, 1=visible)
    UI_SUBJECT_INIT_AND_REGISTER_INT(print_complete_visible_subject_, 0, "print_complete_visible");

    // Tuning panel subjects (for tune panel sliders)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(tune_speed_subject_, tune_speed_buf_, "100%",
                                        "tune_speed_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(tune_flow_subject_, tune_flow_buf_, "100%",
                                        "tune_flow_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(tune_z_offset_subject_, tune_z_offset_buf_, "0.000mm",
                                        "tune_z_offset_display");

    // Register XML event callbacks for tune panel
    lv_xml_register_event_cb(nullptr, "on_tune_speed_changed", on_tune_speed_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_flow_changed", on_tune_flow_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_reset_clicked", on_tune_reset_clicked_cb);

    // Z-offset tune callbacks (single handler parses button name for delta)
    lv_xml_register_event_cb(nullptr, "on_tune_z_offset", on_tune_z_offset_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_save_z_offset", on_tune_save_z_offset_cb);

    // Register XML event callbacks for print status panel buttons
    lv_xml_register_event_cb(nullptr, "on_print_status_light", on_light_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_timelapse", on_timelapse_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_pause", on_pause_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_tune", on_tune_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_cancel", on_cancel_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_nozzle_clicked", on_nozzle_card_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_bed_clicked", on_bed_card_clicked);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized (17 subjects)", get_name());
}

void PrintStatusPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up panel...", get_name());

    // Panel width is set via XML using #overlay_panel_width_large (same as print_file_detail)
    // Use standard overlay panel setup for header/content/back button
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return;
    }

    // Find thumbnail section for nested widgets
    lv_obj_t* thumbnail_section = lv_obj_find_by_name(overlay_content, "thumbnail_section");
    if (!thumbnail_section) {
        spdlog::error("[{}] thumbnail_section not found!", get_name());
        return;
    }

    // Find G-code viewer, thumbnail, and gradient background widgets
    gcode_viewer_ = lv_obj_find_by_name(thumbnail_section, "print_gcode_viewer");
    print_thumbnail_ = lv_obj_find_by_name(thumbnail_section, "print_thumbnail");
    gradient_background_ = lv_obj_find_by_name(thumbnail_section, "gradient_background");

    if (gcode_viewer_) {
        spdlog::debug("[{}]   ✓ G-code viewer widget found", get_name());

        // Register long-press callback for exclude object feature
        ui_gcode_viewer_set_object_long_press_callback(gcode_viewer_, on_object_long_pressed, this);
        spdlog::debug("[{}]   ✓ Registered long-press callback for exclude object", get_name());
    } else {
        spdlog::error("[{}]   ✗ G-code viewer widget NOT FOUND", get_name());
    }
    if (print_thumbnail_) {
        spdlog::debug("[{}]   ✓ Print thumbnail widget found", get_name());
    }
    if (gradient_background_) {
        spdlog::debug("[{}]   ✓ Gradient background widget found", get_name());
    }

    // Force layout calculation
    lv_obj_update_layout(panel_);

    // Register resize callback
    ui_resize_handler_register(on_resize_static);
    resize_registered_ = true;

    // Store button references for potential state queries (not event wiring - that's in XML)
    btn_timelapse_ = lv_obj_find_by_name(overlay_content, "btn_timelapse");
    btn_pause_ = lv_obj_find_by_name(overlay_content, "btn_pause");
    btn_tune_ = lv_obj_find_by_name(overlay_content, "btn_tune");
    btn_cancel_ = lv_obj_find_by_name(overlay_content, "btn_cancel");

    // Print complete celebration badge (for animation)
    success_badge_ = lv_obj_find_by_name(overlay_content, "success_badge");
    if (success_badge_) {
        spdlog::debug("[{}]   ✓ Success badge", get_name());
    }

    // Progress bar widget
    progress_bar_ = lv_obj_find_by_name(overlay_content, "print_progress");
    if (progress_bar_) {
        lv_bar_set_range(progress_bar_, 0, 100);
        // WORKAROUND: LVGL bar has a bug where setting value=0 when cur_value=0
        // causes early return without proper layout update, showing full bar.
        // Force update by setting to 1 first, then 0.
        lv_bar_set_value(progress_bar_, 1, LV_ANIM_OFF);
        lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
        spdlog::debug("[{}]   ✓ Progress bar", get_name());
    } else {
        spdlog::error("[{}]   ✗ Progress bar NOT FOUND", get_name());
    }

    // Preparing progress bar (shown during pre-print operations)
    preparing_progress_bar_ = lv_obj_find_by_name(overlay_content, "preparing_progress_bar");
    if (preparing_progress_bar_) {
        lv_bar_set_range(preparing_progress_bar_, 0, 100);
        lv_bar_set_value(preparing_progress_bar_, 0, LV_ANIM_OFF);
        spdlog::debug("[{}]   ✓ Preparing progress bar", get_name());
    }

    // Check if --gcode-file was specified on command line for this panel
    const auto& config = get_runtime_config();
    if (config.gcode_test_file && gcode_viewer_) {
        spdlog::info("[{}] Loading G-code file from command line: {}", get_name(),
                     config.gcode_test_file);
        load_gcode_file(config.gcode_test_file);
    }

    spdlog::info("[{}] Setup complete!", get_name());
}

void PrintStatusPanel::on_activate() {
    spdlog::debug("[{}] on_activate()", get_name());

    // Resume G-code viewer rendering if viewer mode is active (not thumbnail)
    if (gcode_viewer_ && lv_subject_get_int(&gcode_viewer_mode_subject_) == 1) {
        ui_gcode_viewer_set_paused(gcode_viewer_, false);
    }
}

void PrintStatusPanel::on_deactivate() {
    spdlog::debug("[{}] on_deactivate()", get_name());

    // Pause G-code viewer rendering when panel is hidden (CPU optimization)
    if (gcode_viewer_) {
        ui_gcode_viewer_set_paused(gcode_viewer_, true);
    }
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void PrintStatusPanel::format_time(int seconds, char* buf, size_t buf_size) {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    std::snprintf(buf, buf_size, "%dh %02dm", hours, minutes);
}

void PrintStatusPanel::show_gcode_viewer(bool show) {
    // Update viewer mode subject - XML bindings handle visibility reactively
    // Mode 0 = thumbnail (gradient + thumbnail visible, gcode hidden)
    // Mode 1 = gcode viewer (gcode visible, gradient + thumbnail hidden)
    lv_subject_set_int(&gcode_viewer_mode_subject_, show ? 1 : 0);

    // Pause/resume rendering based on visibility mode (CPU optimization)
    if (gcode_viewer_) {
        ui_gcode_viewer_set_paused(gcode_viewer_, !show);
    }

    spdlog::debug("[{}] G-code viewer mode: {}", get_name(), show ? "gcode" : "thumbnail");
}

void PrintStatusPanel::load_gcode_file(const char* file_path) {
    if (!gcode_viewer_ || !file_path) {
        spdlog::warn("[{}] Cannot load G-code: viewer={}, path={}", get_name(),
                     gcode_viewer_ != nullptr, file_path != nullptr);
        return;
    }

    spdlog::info("[{}] Loading G-code file: {}", get_name(), file_path);

    // Register callback to be notified when loading completes
    ui_gcode_viewer_set_load_callback(
        gcode_viewer_,
        [](lv_obj_t* viewer, void* user_data, bool success) {
            auto* self = static_cast<PrintStatusPanel*>(user_data);
            if (!success) {
                spdlog::error("[{}] G-code load failed", self->get_name());
                return;
            }

            // Get layer count from loaded geometry
            int max_layer = ui_gcode_viewer_get_max_layer(viewer);
            spdlog::info("[{}] G-code loaded: {} layers", self->get_name(), max_layer);

            // Show the viewer (hide gradient and thumbnail)
            self->show_gcode_viewer(true);

            // Force layout recalculation now that viewer is visible
            lv_obj_update_layout(viewer);
            // Reset camera to fit model to new viewport dimensions
            ui_gcode_viewer_reset_camera(viewer);

            // Set print progress to layer 0 (entire model in ghost mode initially)
            ui_gcode_viewer_set_print_progress(viewer, 0);

            // Extract filename from path for display
            const char* filename = ui_gcode_viewer_get_filename(viewer);
            if (!filename) {
                filename = "print.gcode";
            }

            // Start print via MoonrakerAPI if not already printing
            // In test mode with auto-start, a print may already be running
            if (self->api_ && self->current_state_ == PrintState::Idle) {
                self->api_->start_print(
                    filename,
                    []() { spdlog::info("[PrintStatusPanel] Print started via Moonraker"); },
                    [](const MoonrakerError& err) {
                        spdlog::error("[PrintStatusPanel] Failed to start print: {}", err.message);
                    });
            } else if (self->current_state_ != PrintState::Idle) {
                spdlog::debug("[{}] Print already running - skipping duplicate start_print",
                              self->get_name());
            } else {
                spdlog::warn("[{}] No API available - G-code loaded but print not started",
                             self->get_name());
            }
        },
        this);

    // Start loading the file
    ui_gcode_viewer_load_file(gcode_viewer_, file_path);
}

void PrintStatusPanel::update_all_displays() {
    // Guard: don't update if subjects aren't initialized yet
    if (!subjects_initialized_) {
        return;
    }

    // Progress text
    std::snprintf(progress_text_buf_, sizeof(progress_text_buf_), "%d%%", current_progress_);
    lv_subject_copy_string(&progress_text_subject_, progress_text_buf_);

    // Layer text
    std::snprintf(layer_text_buf_, sizeof(layer_text_buf_), "Layer %d / %d", current_layer_,
                  total_layers_);
    lv_subject_copy_string(&layer_text_subject_, layer_text_buf_);

    // Time displays
    format_time(elapsed_seconds_, elapsed_buf_, sizeof(elapsed_buf_));
    lv_subject_copy_string(&elapsed_subject_, elapsed_buf_);

    format_time(remaining_seconds_, remaining_buf_, sizeof(remaining_buf_));
    lv_subject_copy_string(&remaining_subject_, remaining_buf_);

    // Temperatures (stored as centi-degrees ×10, divide for display)
    // Show "--" for target when heater is off (target=0) for better UX
    if (nozzle_target_ > 0) {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%d / %d°C", nozzle_current_ / 10,
                      nozzle_target_ / 10);
    } else {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%d / --", nozzle_current_ / 10);
    }
    lv_subject_copy_string(&nozzle_temp_subject_, nozzle_temp_buf_);

    if (bed_target_ > 0) {
        std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%d / %d°C", bed_current_ / 10,
                      bed_target_ / 10);
    } else {
        std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%d / --", bed_current_ / 10);
    }
    lv_subject_copy_string(&bed_temp_subject_, bed_temp_buf_);

    // Speeds
    std::snprintf(speed_buf_, sizeof(speed_buf_), "%d%%", speed_percent_);
    lv_subject_copy_string(&speed_subject_, speed_buf_);

    std::snprintf(flow_buf_, sizeof(flow_buf_), "%d%%", flow_percent_);
    lv_subject_copy_string(&flow_subject_, flow_buf_);

    // Update pause button icon based on state - MDI icons (play=F040A, pause=F03E4)
    // UTF-8: play=F3 B0 90 8A, pause=F3 B0 8F A4
    if (current_state_ == PrintState::Paused) {
        std::snprintf(pause_button_buf_, sizeof(pause_button_buf_),
                      "\xF3\xB0\x90\x8A"); // play icon
    } else {
        std::snprintf(pause_button_buf_, sizeof(pause_button_buf_),
                      "\xF3\xB0\x8F\xA4"); // pause icon
    }
    lv_subject_copy_string(&pause_button_subject_, pause_button_buf_);
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void PrintStatusPanel::handle_nozzle_card_click() {
    spdlog::info("[{}] Nozzle temp card clicked - opening nozzle temp panel", get_name());

    if (!temp_control_panel_) {
        spdlog::error("[{}] TempControlPanel not initialized", get_name());
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create nozzle temp panel on first access (lazy initialization)
    if (!nozzle_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating nozzle temperature panel...", get_name());

        nozzle_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);
            lv_obj_add_flag(nozzle_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Nozzle temp panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create nozzle temp panel from XML", get_name());
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void PrintStatusPanel::handle_bed_card_click() {
    spdlog::info("[{}] Bed temp card clicked - opening bed temp panel", get_name());

    if (!temp_control_panel_) {
        spdlog::error("[{}] TempControlPanel not initialized", get_name());
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create bed temp panel on first access (lazy initialization)
    if (!bed_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating bed temperature panel...", get_name());

        bed_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_temp_panel", nullptr));
        if (bed_temp_panel_) {
            temp_control_panel_->setup_bed_panel(bed_temp_panel_, parent_screen_);
            lv_obj_add_flag(bed_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Bed temp panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create bed temp panel from XML", get_name());
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (bed_temp_panel_) {
        ui_nav_push_overlay(bed_temp_panel_);
    }
}

void PrintStatusPanel::handle_light_button() {
    spdlog::info("[{}] Light button clicked", get_name());

    // Check if LED is configured
    if (configured_led_.empty()) {
        spdlog::warn("[{}] Light toggle called but no LED configured", get_name());
        return;
    }

    // Toggle to opposite of current state
    bool new_state = !led_on_;

    // Send command to Moonraker
    if (api_) {
        if (new_state) {
            api_->set_led_on(
                configured_led_,
                [this]() {
                    spdlog::info("[{}] LED turned ON - waiting for state update", get_name());
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to turn LED on: {}", err.message);
                    NOTIFY_ERROR("Failed to turn light on: {}", err.user_message());
                });
        } else {
            api_->set_led_off(
                configured_led_,
                [this]() {
                    spdlog::info("[{}] LED turned OFF - waiting for state update", get_name());
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to turn LED off: {}", err.message);
                    NOTIFY_ERROR("Failed to turn light off: {}", err.user_message());
                });
        }
    } else {
        spdlog::warn("[{}] API not available - cannot control LED", get_name());
        NOTIFY_ERROR("Cannot control light: printer not connected");
    }
}

void PrintStatusPanel::handle_timelapse_button() {
    spdlog::info("[{}] Timelapse button clicked (current state: {})", get_name(),
                 timelapse_enabled_ ? "enabled" : "disabled");

    // Toggle to opposite of current state
    bool new_state = !timelapse_enabled_;

    if (api_) {
        api_->set_timelapse_enabled(
            new_state,
            [this, new_state]() {
                spdlog::info("[{}] Timelapse {} successfully", get_name(),
                             new_state ? "enabled" : "disabled");

                // Update local state
                timelapse_enabled_ = new_state;

                // Update icon: U+F0567 = video (enabled), U+F0568 = video-off (disabled)
                // MDI Plane 15 icons use 4-byte UTF-8 encoding
                if (timelapse_enabled_) {
                    std::snprintf(timelapse_button_buf_, sizeof(timelapse_button_buf_),
                                  "\xF3\xB0\x95\xA7"); // video
                } else {
                    std::snprintf(timelapse_button_buf_, sizeof(timelapse_button_buf_),
                                  "\xF3\xB0\x95\xA8"); // video-off
                }
                lv_subject_copy_string(&timelapse_button_subject_, timelapse_button_buf_);
            },
            [this](const MoonrakerError& err) {
                spdlog::error("[{}] Failed to toggle timelapse: {}", get_name(), err.message);
                NOTIFY_ERROR("Failed to toggle timelapse: {}", err.user_message());
            });
    } else {
        spdlog::warn("[{}] API not available - cannot control timelapse", get_name());
        NOTIFY_ERROR("Cannot control timelapse: printer not connected");
    }
}

void PrintStatusPanel::handle_pause_button() {
    if (current_state_ == PrintState::Printing) {
        spdlog::info("[{}] Pausing print...", get_name());

        if (api_) {
            api_->pause_print(
                [this]() {
                    spdlog::info("[{}] Pause command sent successfully", get_name());
                    // State will update via PrinterState observer when Moonraker confirms
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to pause print: {}", err.message);
                    NOTIFY_ERROR("Failed to pause print: {}", err.user_message());
                });
        } else {
            // Fall back to local state change for mock mode
            spdlog::warn("[{}] API not available - using local state change", get_name());
            set_state(PrintState::Paused);
        }
    } else if (current_state_ == PrintState::Paused) {
        spdlog::info("[{}] Resuming print...", get_name());

        if (api_) {
            api_->resume_print(
                [this]() {
                    spdlog::info("[{}] Resume command sent successfully", get_name());
                    // State will update via PrinterState observer when Moonraker confirms
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to resume print: {}", err.message);
                    NOTIFY_ERROR("Failed to resume print: {}", err.user_message());
                });
        } else {
            // Fall back to local state change for mock mode
            spdlog::warn("[{}] API not available - using local state change", get_name());
            set_state(PrintState::Printing);
        }
    }
}

void PrintStatusPanel::handle_tune_button() {
    spdlog::info("[{}] Tune button clicked - opening tuning panel", get_name());

    // Create tune panel on first access (lazy initialization)
    if (!tune_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating tuning panel...", get_name());

        tune_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_tune_panel", nullptr));
        if (tune_panel_) {
            setup_tune_panel(tune_panel_);
            lv_obj_add_flag(tune_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Tuning panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create tuning panel from XML", get_name());
            NOTIFY_ERROR("Failed to load tuning panel");
            return;
        }
    }

    // Update displays with current values before showing
    update_tune_display();

    // Set slider values to current PrinterState values
    if (tune_panel_) {
        lv_obj_t* overlay_content = lv_obj_find_by_name(tune_panel_, "overlay_content");
        if (overlay_content) {
            lv_obj_t* speed_slider = lv_obj_find_by_name(overlay_content, "speed_slider");
            lv_obj_t* flow_slider = lv_obj_find_by_name(overlay_content, "flow_slider");

            if (speed_slider) {
                lv_slider_set_value(speed_slider, speed_percent_, LV_ANIM_OFF);
            }
            if (flow_slider) {
                lv_slider_set_value(flow_slider, flow_percent_, LV_ANIM_OFF);
            }
        }
        ui_nav_push_overlay(tune_panel_);
    }
}

void PrintStatusPanel::handle_cancel_button() {
    spdlog::info("[{}] Cancel button clicked - showing confirmation dialog", get_name());

    // Set up the confirm callback to execute the actual cancel
    cancel_modal_.set_on_confirm([this]() {
        spdlog::info("[{}] Cancel confirmed - executing cancel_print", get_name());

        if (api_) {
            api_->cancel_print(
                [this]() {
                    spdlog::info("[{}] Cancel command sent successfully", get_name());
                    // State will update via PrinterState observer when Moonraker confirms
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to cancel print: {}", err.message);
                    NOTIFY_ERROR("Failed to cancel print: {}", err.user_message());
                });
        } else {
            spdlog::warn("[{}] API not available - cannot cancel print", get_name());
            NOTIFY_ERROR("Cannot cancel: not connected to printer");
        }
    });

    // Show the modal (RAII handles cleanup)
    cancel_modal_.show(lv_screen_active());
}

void PrintStatusPanel::handle_resize() {
    spdlog::debug("[{}] Handling resize event", get_name());

    // Reset gcode viewer camera to fit new dimensions
    if (gcode_viewer_ && !lv_obj_has_flag(gcode_viewer_, LV_OBJ_FLAG_HIDDEN)) {
        // Force layout recalculation so viewer gets correct dimensions
        lv_obj_update_layout(gcode_viewer_);
        ui_gcode_viewer_reset_camera(gcode_viewer_);
        spdlog::debug("[{}] Reset gcode viewer camera after resize", get_name());
    }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void PrintStatusPanel::on_nozzle_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_nozzle_card_clicked");
    (void)e;
    get_global_print_status_panel().handle_nozzle_card_click();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_bed_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_bed_card_clicked");
    (void)e;
    get_global_print_status_panel().handle_bed_card_click();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_light_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_light_clicked");
    (void)e;
    get_global_print_status_panel().handle_light_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_timelapse_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_timelapse_clicked");
    (void)e;
    get_global_print_status_panel().handle_timelapse_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_pause_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_pause_clicked");
    (void)e;
    get_global_print_status_panel().handle_pause_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_tune_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_tune_clicked");
    (void)e;
    get_global_print_status_panel().handle_tune_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_cancel_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_cancel_clicked");
    (void)e;
    get_global_print_status_panel().handle_cancel_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_resize_static() {
    // Use global instance for resize callback (registered without user_data)
    if (g_print_status_panel) {
        g_print_status_panel->handle_resize();
    }
}

// ============================================================================
// PRINTERSTATE OBSERVER CALLBACKS
// ============================================================================

void PrintStatusPanel::extruder_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::extruder_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::bed_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::bed_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::print_progress_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_progress_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        // Read enum from integer subject (type-safe, no string parsing)
        auto state = static_cast<PrintJobState>(lv_subject_get_int(subject));
        self->on_print_state_changed(state);
    }
}

void PrintStatusPanel::print_filename_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_filename_changed(lv_subject_get_string(subject));
    }
}

void PrintStatusPanel::speed_factor_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_speed_factor_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::flow_factor_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_flow_factor_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::gcode_z_offset_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_gcode_z_offset_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::led_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_led_state_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_layer_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_layer_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::excluded_objects_observer_cb(lv_observer_t* observer,
                                                    lv_subject_t* subject) {
    (void)subject; // Version number not needed, just signals a change
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_excluded_objects_changed();
    }
}

void PrintStatusPanel::print_duration_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_duration_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_time_left_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_time_left_changed(lv_subject_get_int(subject));
    }
}

// ============================================================================
// OBSERVER INSTANCE METHODS
// ============================================================================

void PrintStatusPanel::on_temperature_changed() {
    // Read all temperature values from PrinterState subjects
    nozzle_current_ = lv_subject_get_int(printer_state_.get_extruder_temp_subject());
    nozzle_target_ = lv_subject_get_int(printer_state_.get_extruder_target_subject());
    bed_current_ = lv_subject_get_int(printer_state_.get_bed_temp_subject());
    bed_target_ = lv_subject_get_int(printer_state_.get_bed_target_subject());

    update_all_displays();

    spdlog::trace("[{}] Temperatures updated: nozzle {}/{}°C, bed {}/{}°C", get_name(),
                  nozzle_current_, nozzle_target_, bed_current_, bed_target_);
}

void PrintStatusPanel::on_print_progress_changed(int progress) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send progress=0 when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring progress update ({}) in Complete state", get_name(), progress);
        return;
    }

    // Update progress display without calling update_all_displays()
    // to avoid redundant updates when multiple subjects change
    current_progress_ = progress;
    if (current_progress_ < 0)
        current_progress_ = 0;
    if (current_progress_ > 100)
        current_progress_ = 100;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    // Update progress text
    std::snprintf(progress_text_buf_, sizeof(progress_text_buf_), "%d%%", current_progress_);
    lv_subject_copy_string(&progress_text_subject_, progress_text_buf_);

    // Update progress bar with smooth animation (300ms ease-out) if animations enabled
    // This complements the subject binding with animated transitions
    if (progress_bar_) {
        lv_anim_enable_t anim_enable =
            SettingsManager::instance().get_animations_enabled() ? LV_ANIM_ON : LV_ANIM_OFF;
        lv_bar_set_value(progress_bar_, current_progress_, anim_enable);
    }

    spdlog::trace("[{}] Progress updated: {}%", get_name(), current_progress_);
}

void PrintStatusPanel::on_print_state_changed(PrintJobState job_state) {
    // Map PrintJobState (from PrinterState) to PrintState (UI-specific)
    // Note: PrintState has a Preparing state that doesn't exist in PrintJobState -
    // that's managed locally by set_preparing()/end_preparing()
    PrintState new_state = PrintState::Idle;

    switch (job_state) {
    case PrintJobState::STANDBY:
        new_state = PrintState::Idle;
        break;
    case PrintJobState::PRINTING:
        new_state = PrintState::Printing;
        break;
    case PrintJobState::PAUSED:
        new_state = PrintState::Paused;
        break;
    case PrintJobState::COMPLETE:
        new_state = PrintState::Complete;
        break;
    case PrintJobState::CANCELLED:
        new_state = PrintState::Cancelled;
        break;
    case PrintJobState::ERROR:
        new_state = PrintState::Error;
        break;
    }

    // Special handling for Complete -> Idle transition:
    // Moonraker/Klipper often transitions to Standby shortly after Complete.
    // We want to keep the "Print Complete!" display visible with final stats
    // until a new print starts (Printing state).
    if (current_state_ == PrintState::Complete && new_state == PrintState::Idle) {
        spdlog::debug("[{}] Ignoring Complete -> Idle transition (preserving complete state)",
                      get_name());
        return;
    }

    // Only update if state actually changed
    if (new_state != current_state_) {
        PrintState old_state = current_state_;

        // When transitioning to Printing from Complete (new print started),
        // reset the complete overlay
        if (old_state == PrintState::Complete && new_state == PrintState::Printing) {
            lv_subject_set_int(&print_complete_visible_subject_, 0);
            spdlog::debug("[{}] New print started - clearing complete overlay", get_name());
        }

        // Clear thumbnail tracking when print ends (Complete/Cancelled/Error/Idle)
        // This ensures it's available during the entire print but cleared for the next one
        bool print_ended =
            (new_state == PrintState::Complete || new_state == PrintState::Cancelled ||
             new_state == PrintState::Error || new_state == PrintState::Idle);
        if (print_ended) {
            if (!thumbnail_source_filename_.empty() || !loaded_thumbnail_filename_.empty()) {
                spdlog::debug("[{}] Clearing thumbnail tracking (print ended)", get_name());
                thumbnail_source_filename_.clear();
                loaded_thumbnail_filename_.clear();
            }
        }

        set_state(new_state);
        spdlog::info("[{}] Print state changed: {} -> {}", get_name(),
                     print_job_state_to_string(job_state), static_cast<int>(new_state));

        // Toggle G-code viewer visibility based on print state
        // Show 3D viewer during printing/paused (real-time progress visualization)
        // On completion, show thumbnail with gradient background instead (more polished look)
        bool show_viewer = (new_state == PrintState::Printing || new_state == PrintState::Paused);
        show_gcode_viewer(show_viewer);

        // Show print complete overlay when entering Complete state
        if (new_state == PrintState::Complete) {
            // Ensure progress shows 100% on completion
            if (current_progress_ < 100) {
                current_progress_ = 100;
                std::snprintf(progress_text_buf_, sizeof(progress_text_buf_), "100%%");
                lv_subject_copy_string(&progress_text_subject_, progress_text_buf_);
            }
            lv_subject_set_int(&print_complete_visible_subject_, 1);

            // Trigger celebratory animation on the success badge
            animate_print_complete();

            spdlog::info("[{}] Print complete! Final progress: {}%, elapsed: {}s", get_name(),
                         current_progress_, elapsed_seconds_);
        }
    }
}

void PrintStatusPanel::on_print_filename_changed(const char* filename) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send empty filename when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring filename update in Complete state", get_name());
        return;
    }

    if (filename && filename[0] != '\0') {
        // Compute effective filename for comparison (respects thumbnail_source override)
        // This ensures we compare apples-to-apples: the effective display name
        std::string raw_filename = filename;
        std::string effective_filename =
            thumbnail_source_filename_.empty() ? raw_filename : thumbnail_source_filename_;
        std::string display_name = get_display_filename(effective_filename);

        // Only update if display would actually change
        if (display_name != filename_buf_) {
            set_filename(filename);
            spdlog::debug("[{}] Filename updated: {}", get_name(), display_name);
        }
    }
}

void PrintStatusPanel::on_speed_factor_changed(int speed) {
    speed_percent_ = speed;
    if (subjects_initialized_) {
        std::snprintf(speed_buf_, sizeof(speed_buf_), "%d%%", speed_percent_);
        lv_subject_copy_string(&speed_subject_, speed_buf_);
    }
    spdlog::trace("[{}] Speed factor updated: {}%", get_name(), speed);
}

void PrintStatusPanel::on_flow_factor_changed(int flow) {
    flow_percent_ = flow;
    if (subjects_initialized_) {
        std::snprintf(flow_buf_, sizeof(flow_buf_), "%d%%", flow_percent_);
        lv_subject_copy_string(&flow_subject_, flow_buf_);
    }
    spdlog::trace("[{}] Flow factor updated: {}%", get_name(), flow);
}

void PrintStatusPanel::on_gcode_z_offset_changed(int microns) {
    // Update display from PrinterState (microns -> mm)
    current_z_offset_ = microns / 1000.0;
    if (subjects_initialized_) {
        std::snprintf(tune_z_offset_buf_, sizeof(tune_z_offset_buf_), "%.3fmm", current_z_offset_);
        lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);
    }
    spdlog::trace("[{}] G-code Z-offset updated: {}µm ({}mm)", get_name(), microns,
                  current_z_offset_);
}

void PrintStatusPanel::on_led_state_changed(int state) {
    led_on_ = (state != 0);
    spdlog::debug("[{}] LED state changed: {} (from PrinterState)", get_name(),
                  led_on_ ? "ON" : "OFF");
}

void PrintStatusPanel::on_print_layer_changed(int current_layer) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send layer=0 when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring layer update ({}) in Complete state", get_name(),
                      current_layer);
        return;
    }

    // Update internal layer state
    current_layer_ = current_layer;
    int total_layers = lv_subject_get_int(printer_state_.get_print_layer_total_subject());
    total_layers_ = total_layers;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    // Update the layer text display
    std::snprintf(layer_text_buf_, sizeof(layer_text_buf_), "Layer %d / %d", current_layer_,
                  total_layers_);
    lv_subject_copy_string(&layer_text_subject_, layer_text_buf_);

    // Update G-code viewer ghost layer if viewer is active and visible
    if (gcode_viewer_ && !lv_obj_has_flag(gcode_viewer_, LV_OBJ_FLAG_HIDDEN)) {
        // Map from Moonraker layer count (e.g., 240) to viewer layer count (e.g., 2912)
        // The slicer metadata and parsed G-code often have different layer counts
        int viewer_max_layer = ui_gcode_viewer_get_max_layer(gcode_viewer_);
        int viewer_layer = current_layer;
        if (total_layers_ > 0 && viewer_max_layer > 0) {
            viewer_layer = (current_layer * viewer_max_layer) / total_layers_;
        }
        ui_gcode_viewer_set_print_progress(gcode_viewer_, viewer_layer);
        spdlog::trace("[{}] G-code viewer ghost layer updated to {} (Moonraker: {}/{})", get_name(),
                      viewer_layer, current_layer, total_layers_);
    }
}

void PrintStatusPanel::on_excluded_objects_changed() {
    // Sync excluded objects from PrinterState (Klipper/Moonraker)
    const auto& klipper_excluded = printer_state_.get_excluded_objects();

    // Merge Klipper's excluded set with our local set
    // This ensures objects excluded via Klipper (e.g., from another client) are shown
    for (const auto& obj : klipper_excluded) {
        if (excluded_objects_.count(obj) == 0) {
            excluded_objects_.insert(obj);
            spdlog::info("[{}] Synced excluded object from Klipper: '{}'", get_name(), obj);
        }
    }

    // Update the G-code viewer visual state
    if (gcode_viewer_) {
        // Combine confirmed excluded with any pending exclusion for visual display
        std::unordered_set<std::string> visual_excluded = excluded_objects_;
        if (!pending_exclude_object_.empty()) {
            visual_excluded.insert(pending_exclude_object_);
        }
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, visual_excluded);
        spdlog::debug("[{}] Updated viewer with {} excluded objects", get_name(),
                      visual_excluded.size());
    }
}

void PrintStatusPanel::on_print_duration_changed(int seconds) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send duration=0 when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring duration update ({}) in Complete state", get_name(), seconds);
        return;
    }

    elapsed_seconds_ = seconds;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    format_time(elapsed_seconds_, elapsed_buf_, sizeof(elapsed_buf_));
    lv_subject_copy_string(&elapsed_subject_, elapsed_buf_);
    spdlog::trace("[{}] Print duration updated: {}s", get_name(), seconds);
}

void PrintStatusPanel::on_print_time_left_changed(int seconds) {
    // Guard: preserve final values when in Complete state
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring time_left update ({}) in Complete state", get_name(), seconds);
        return;
    }

    remaining_seconds_ = seconds;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    format_time(remaining_seconds_, remaining_buf_, sizeof(remaining_buf_));
    lv_subject_copy_string(&remaining_subject_, remaining_buf_);
    spdlog::trace("[{}] Time remaining updated: {}s", get_name(), seconds);
}

// ============================================================================
// TUNE PANEL HELPERS
// ============================================================================

void PrintStatusPanel::setup_tune_panel(lv_obj_t* panel) {
    // Use standard overlay panel setup for back button handling
    ui_overlay_panel_setup_standard(panel, parent_screen_, "overlay_header", "overlay_content");

    // Event handlers are registered via XML event_cb declarations
    // (on_tune_speed_changed, on_tune_flow_changed, on_tune_reset_clicked, on_tune_z_offset)
    // Callbacks registered in init_subjects() via lv_xml_register_event_cb()

    // Update Z-offset icons based on printer kinematics
    update_z_offset_icons(panel);

    spdlog::debug("[{}] Tune panel setup complete (events wired via XML)", get_name());
}

void PrintStatusPanel::update_z_offset_icons(lv_obj_t* panel) {
    // Get kinematics type from PrinterState
    // 0 = unknown, 1 = bed moves Z (CoreXY), 2 = head moves Z (Cartesian/Delta)
    int kin = lv_subject_get_int(printer_state_.get_printer_bed_moves_subject());
    bool bed_moves_z = (kin == 1);

    // Select icon codepoints based on kinematics
    // CoreXY (bed moves): expand icons show bed motion
    // Cartesian/Delta (head moves): arrow icons show head motion
    const char* closer_icon =
        bed_moves_z ? "\xF3\xB0\x9E\x93" : "\xF3\xB0\x81\x85"; // arrow-expand-down : arrow-down
    const char* farther_icon =
        bed_moves_z ? "\xF3\xB0\x9E\x96" : "\xF3\xB0\x81\x9D"; // arrow-expand-up : arrow-up

    // Find and update all closer icons (3 buttons)
    const char* closer_names[] = {"icon_z_closer_01", "icon_z_closer_005", "icon_z_closer_001"};
    for (const char* name : closer_names) {
        lv_obj_t* icon = lv_obj_find_by_name(panel, name);
        if (icon) {
            lv_label_set_text(icon, closer_icon);
        }
    }

    // Find and update all farther icons (3 buttons)
    const char* farther_names[] = {"icon_z_farther_001", "icon_z_farther_005", "icon_z_farther_01"};
    for (const char* name : farther_names) {
        lv_obj_t* icon = lv_obj_find_by_name(panel, name);
        if (icon) {
            lv_label_set_text(icon, farther_icon);
        }
    }

    spdlog::debug("[{}] Z-offset icons set for {} kinematics", get_name(),
                  bed_moves_z ? "bed-moves-Z" : "head-moves-Z");
}

void PrintStatusPanel::update_tune_display() {
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "%d%%", speed_percent_);
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "%d%%", flow_percent_);
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);
}

void PrintStatusPanel::update_button_states() {
    // Buttons should only be enabled during Printing or Paused states
    // When Complete, Cancelled, Error, or Idle - disable print control buttons
    bool buttons_enabled =
        (current_state_ == PrintState::Printing || current_state_ == PrintState::Paused);

    // Helper lambda for enable/disable with visual feedback
    auto set_button_enabled = [](lv_obj_t* btn, bool enabled) {
        if (!btn)
            return;
        if (enabled) {
            lv_obj_remove_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        } else {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(btn, LV_OPA_50, LV_PART_MAIN);
        }
    };

    set_button_enabled(btn_timelapse_, buttons_enabled);
    set_button_enabled(btn_pause_, buttons_enabled);
    set_button_enabled(btn_tune_, buttons_enabled);
    set_button_enabled(btn_cancel_, buttons_enabled);

    spdlog::debug("[{}] Button states updated: {} (state={})", get_name(),
                  buttons_enabled ? "enabled" : "disabled", static_cast<int>(current_state_));
}

void PrintStatusPanel::animate_print_complete() {
    if (!success_badge_) {
        return;
    }

    // Skip animation if disabled - show badge in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        constexpr int32_t SCALE_FINAL = 256; // 100% scale
        lv_obj_set_style_transform_scale(success_badge_, SCALE_FINAL, LV_PART_MAIN);
        lv_obj_set_style_opa(success_badge_, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[{}] Animations disabled - showing success badge instantly", get_name());
        return;
    }

    // Animation constants for celebration effect
    // Stage 1: Quick scale-up with overshoot (300ms)
    // Stage 2: Settle to final size (150ms)
    constexpr int32_t CELEBRATION_DURATION_MS = 300;
    constexpr int32_t SETTLE_DURATION_MS = 150;
    constexpr int32_t SCALE_START = 128;     // 50% scale (128/256)
    constexpr int32_t SCALE_OVERSHOOT = 282; // ~110% scale (slight overshoot)
    constexpr int32_t SCALE_FINAL = 256;     // 100% scale (256/256)

    // Start badge small and transparent
    lv_obj_set_style_transform_scale(success_badge_, SCALE_START, LV_PART_MAIN);
    lv_obj_set_style_opa(success_badge_, LV_OPA_TRANSP, LV_PART_MAIN);

    // Stage 1: Scale up with overshoot + fade in
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, success_badge_);
    lv_anim_set_values(&scale_anim, SCALE_START, SCALE_OVERSHOOT);
    lv_anim_set_duration(&scale_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    // Fade in animation (parallel with scale)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, success_badge_);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    // Stage 2: Settle from overshoot to final size (delayed start)
    lv_anim_t settle_anim;
    lv_anim_init(&settle_anim);
    lv_anim_set_var(&settle_anim, success_badge_);
    lv_anim_set_values(&settle_anim, SCALE_OVERSHOOT, SCALE_FINAL);
    lv_anim_set_duration(&settle_anim, SETTLE_DURATION_MS);
    lv_anim_set_delay(&settle_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&settle_anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&settle_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&settle_anim);

    spdlog::debug("[{}] Print complete celebration animation started", get_name());
}

void PrintStatusPanel::handle_tune_speed_changed(int value) {
    // Update display immediately for responsive feel
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "%d%%", value);
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    // Send G-code command
    if (api_) {
        std::string gcode = "M220 S" + std::to_string(value);
        api_->execute_gcode(
            gcode, [value]() { spdlog::debug("[PrintStatusPanel] Speed set to {}%", value); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Failed to set speed: {}", err.message);
                NOTIFY_ERROR("Failed to set print speed: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_flow_changed(int value) {
    // Update display immediately for responsive feel
    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "%d%%", value);
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);

    // Send G-code command
    if (api_) {
        std::string gcode = "M221 S" + std::to_string(value);
        api_->execute_gcode(
            gcode, [value]() { spdlog::debug("[PrintStatusPanel] Flow set to {}%", value); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Failed to set flow: {}", err.message);
                NOTIFY_ERROR("Failed to set flow rate: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_reset() {
    if (!tune_panel_) {
        return;
    }

    lv_obj_t* overlay_content = lv_obj_find_by_name(tune_panel_, "overlay_content");
    if (!overlay_content) {
        return;
    }

    // Reset sliders to 100%
    lv_obj_t* speed_slider = lv_obj_find_by_name(overlay_content, "speed_slider");
    lv_obj_t* flow_slider = lv_obj_find_by_name(overlay_content, "flow_slider");

    if (speed_slider) {
        lv_slider_set_value(speed_slider, 100, LV_ANIM_ON);
    }
    if (flow_slider) {
        lv_slider_set_value(flow_slider, 100, LV_ANIM_ON);
    }

    // Update displays
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "100%%");
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);
    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "100%%");
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);

    // Send G-code commands
    if (api_) {
        api_->execute_gcode(
            "M220 S100", []() { spdlog::debug("[PrintStatusPanel] Speed reset to 100%"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Failed to reset speed: {}", err.user_message());
            });
        api_->execute_gcode(
            "M221 S100", []() { spdlog::debug("[PrintStatusPanel] Flow reset to 100%"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Failed to reset flow: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_z_offset_changed(double delta) {
    // Update local display immediately for responsive feel
    current_z_offset_ += delta;
    std::snprintf(tune_z_offset_buf_, sizeof(tune_z_offset_buf_), "%.3fmm", current_z_offset_);
    lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);

    // Track pending delta for "unsaved adjustment" notification in Controls panel
    int delta_microns = static_cast<int>(delta * 1000.0);
    get_printer_state().add_pending_z_offset_delta(delta_microns);

    spdlog::debug("[{}] Z-offset adjust: {:+.3f}mm (total: {:.3f}mm)", get_name(), delta,
                  current_z_offset_);

    // Send SET_GCODE_OFFSET Z_ADJUST command to Klipper
    if (api_) {
        char gcode[64];
        std::snprintf(gcode, sizeof(gcode), "SET_GCODE_OFFSET Z_ADJUST=%.3f", delta);
        api_->execute_gcode(
            gcode, [delta]() { spdlog::debug("[PrintStatusPanel] Z adjusted {:+.3f}mm", delta); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Z-offset adjust failed: {}", err.message);
                NOTIFY_ERROR("Z-offset failed: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_save_z_offset() {
    // Show warning modal - SAVE_CONFIG restarts Klipper and cancels active prints!
    save_z_offset_modal_.set_on_confirm([this]() {
        if (api_) {
            api_->execute_gcode(
                "SAVE_CONFIG",
                []() {
                    spdlog::info("[PrintStatusPanel] Z-offset saved - Klipper restarting");
                    ui_toast_show(ToastSeverity::WARNING, "Z-offset saved - Klipper restarting...",
                                  5000);
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintStatusPanel] SAVE_CONFIG failed: {}", err.message);
                    NOTIFY_ERROR("Save failed: {}", err.user_message());
                });
        }
    });
    save_z_offset_modal_.show(lv_screen_active());
}

// ============================================================================
// XML EVENT CALLBACKS (free functions using global accessor)
// ============================================================================

static void on_tune_speed_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_global_print_status_panel().handle_tune_speed_changed(value);
    }
}

static void on_tune_flow_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_global_print_status_panel().handle_tune_flow_changed(value);
    }
}

static void on_tune_reset_clicked_cb(lv_event_t* /*e*/) {
    get_global_print_status_panel().handle_tune_reset();
}

/**
 * @brief Single callback for all Z-offset buttons
 *
 * Parses button name to determine direction and magnitude:
 *   - btn_z_closer_01  -> -0.1mm (closer = negative = more squish)
 *   - btn_z_closer_005 -> -0.05mm
 *   - btn_z_closer_001 -> -0.01mm
 *   - btn_z_farther_001 -> +0.01mm (farther = positive = less squish)
 *   - btn_z_farther_005 -> +0.05mm
 *   - btn_z_farther_01 -> +0.1mm
 */
static void on_tune_z_offset_cb(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!btn) {
        return;
    }

    // Get button name to determine delta
    const char* name = lv_obj_get_name(btn);
    if (!name) {
        spdlog::warn("[on_tune_z_offset_cb] Button has no name");
        return;
    }

    // Parse direction: "closer" = negative, "farther" = positive
    double delta = 0.0;
    bool is_closer = (strstr(name, "closer") != nullptr);
    bool is_farther = (strstr(name, "farther") != nullptr);

    if (!is_closer && !is_farther) {
        spdlog::warn("[on_tune_z_offset_cb] Unknown button name: {}", name);
        return;
    }

    // Parse magnitude from suffix: "_01" = 0.1, "_005" = 0.05, "_001" = 0.01
    if (strstr(name, "_01") && !strstr(name, "_001")) {
        delta = 0.1;
    } else if (strstr(name, "_005")) {
        delta = 0.05;
    } else if (strstr(name, "_001")) {
        delta = 0.01;
    } else {
        spdlog::warn("[on_tune_z_offset_cb] Unknown delta in button name: {}", name);
        return;
    }

    // Apply direction: closer = more squish = negative Z adjust
    if (is_closer) {
        delta = -delta;
    }

    spdlog::trace("[on_tune_z_offset_cb] Button '{}' -> delta {:+.3f}mm", name, delta);
    get_global_print_status_panel().handle_tune_z_offset_changed(delta);
}

static void on_tune_save_z_offset_cb(lv_event_t* /*e*/) {
    get_global_print_status_panel().handle_tune_save_z_offset();
}

// ============================================================================
// THUMBNAIL LOADING
// ============================================================================

void PrintStatusPanel::load_thumbnail_for_file(const std::string& filename) {
    // Increment generation to invalidate any in-flight async operations
    ++thumbnail_load_generation_;
    uint32_t current_gen = thumbnail_load_generation_;

    spdlog::debug("[{}] Loading thumbnail for: {} (gen={})", get_name(), filename, current_gen);

    // Skip if no API available (e.g., in mock mode)
    if (!api_) {
        spdlog::debug("[{}] No API available - skipping thumbnail load", get_name());
        return;
    }

    // Skip if no widget to display to
    if (!print_thumbnail_) {
        spdlog::warn("[{}] print_thumbnail_ widget not found - skipping thumbnail load",
                     get_name());
        return;
    }

    // Resolve to original filename if this is a modified temp file
    // (Moonraker only has metadata for original files, not modified copies)
    std::string metadata_filename = resolve_gcode_filename(filename);

    // First, get file metadata to find thumbnail path
    api_->get_file_metadata(
        metadata_filename,
        [this, current_gen](const FileMetadata& metadata) {
            // Check if this callback is still relevant
            if (current_gen != thumbnail_load_generation_) {
                spdlog::trace("[{}] Stale metadata callback (gen {} != {}), ignoring", get_name(),
                              current_gen, thumbnail_load_generation_);
                return;
            }

            // Get the largest thumbnail available
            std::string thumbnail_rel_path = metadata.get_largest_thumbnail();
            if (thumbnail_rel_path.empty()) {
                spdlog::debug("[{}] No thumbnail available in metadata", get_name());
                return;
            }

            spdlog::debug("[{}] Found thumbnail: {}", get_name(), thumbnail_rel_path);

            // Use centralized ThumbnailCache for download and LVGL path handling
            get_thumbnail_cache().fetch(
                api_, thumbnail_rel_path,
                [this, current_gen](const std::string& lvgl_path) {
                    // Check if this callback is still relevant
                    if (current_gen != thumbnail_load_generation_) {
                        spdlog::trace("[{}] Stale thumbnail callback (gen {} != {}), ignoring",
                                      get_name(), current_gen, thumbnail_load_generation_);
                        return;
                    }

                    // Store the cached path (without "A:" prefix for internal use)
                    cached_thumbnail_path_ = lvgl_path;

                    if (print_thumbnail_) {
                        lv_image_set_src(print_thumbnail_, lvgl_path.c_str());
                        spdlog::info("[{}] Thumbnail loaded: {}", get_name(), lvgl_path);
                    }
                },
                [this](const std::string& error) {
                    spdlog::warn("[{}] Failed to fetch thumbnail: {}", get_name(), error);
                });
        },
        [this](const MoonrakerError& err) {
            spdlog::debug("[{}] Failed to get file metadata: {}", get_name(), err.message);
        });
}

// ============================================================================
// G-CODE VIEWER LOADING
// ============================================================================

void PrintStatusPanel::load_gcode_for_viewing(const std::string& filename) {
    spdlog::debug("[{}] Loading G-code for viewing: {}", get_name(), filename);

    // Skip if no viewer widget
    if (!gcode_viewer_) {
        spdlog::debug("[{}] No gcode_viewer_ widget - skipping G-code load", get_name());
        return;
    }

    // Skip if no API available
    if (!api_) {
        spdlog::debug("[{}] No API available - skipping G-code load", get_name());
        return;
    }

    // Download the G-code file using the API
    // For mock mode, this reads from test_gcodes/ directory
    // For real mode, this downloads from Moonraker
    api_->download_file(
        "gcodes", filename,
        [this, filename](const std::string& content) {
            // Save to a temp file for the viewer to load
            std::string temp_path = "/tmp/helix_print_view_" +
                                    std::to_string(std::hash<std::string>{}(filename)) + ".gcode";

            std::ofstream file(temp_path, std::ios::binary);
            if (!file) {
                spdlog::error("[{}] Failed to create temp file for G-code viewing: {}", get_name(),
                              temp_path);
                return;
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.close();

            spdlog::info("[{}] Downloaded G-code ({} bytes), loading into viewer: {}", get_name(),
                         content.size(), temp_path);

            // Load into the viewer widget
            load_gcode_file(temp_path.c_str());
        },
        [this, filename](const MoonrakerError& err) {
            spdlog::warn("[{}] Failed to download G-code for viewing '{}': {}", get_name(),
                         filename, err.message);
        });
}

// ============================================================================
// PUBLIC API
// ============================================================================

void PrintStatusPanel::set_temp_control_panel(TempControlPanel* temp_panel) {
    temp_control_panel_ = temp_panel;
    spdlog::debug("[{}] TempControlPanel reference set", get_name());
}

void PrintStatusPanel::set_filename(const char* filename) {
    // Store the actual filename (may be a temp file path)
    current_print_filename_ = filename ? filename : "";

    // Use thumbnail_source_filename_ if set (for modified temp files)
    // This affects BOTH the display name AND the thumbnail lookup
    std::string effective_filename =
        thumbnail_source_filename_.empty() ? current_print_filename_ : thumbnail_source_filename_;

    // Strip path and .gcode extension for clean display
    std::string display_name = get_display_filename(effective_filename);
    std::snprintf(filename_buf_, sizeof(filename_buf_), "%s", display_name.c_str());
    lv_subject_copy_string(&filename_subject_, filename_buf_);

    // Load thumbnail and G-code ONLY if effective filename changed (makes this function idempotent)
    // This prevents redundant loads when observer fires repeatedly with same filename
    if (!effective_filename.empty() && effective_filename != loaded_thumbnail_filename_) {
        spdlog::debug("[{}] Thumbnail filename changed: '{}' -> '{}'", get_name(),
                      loaded_thumbnail_filename_, effective_filename);
        load_thumbnail_for_file(effective_filename);
        load_gcode_for_viewing(effective_filename);
        loaded_thumbnail_filename_ = effective_filename;
    }
}

void PrintStatusPanel::set_thumbnail_source(const std::string& filename) {
    thumbnail_source_filename_ = filename;
    spdlog::debug("[{}] Thumbnail source set to: {}", get_name(),
                  filename.empty() ? "(cleared)" : filename);
}

void PrintStatusPanel::set_progress(int percent) {
    current_progress_ = percent;
    if (current_progress_ < 0)
        current_progress_ = 0;
    if (current_progress_ > 100)
        current_progress_ = 100;
    update_all_displays();
}

void PrintStatusPanel::set_layer(int current, int total) {
    current_layer_ = current;
    total_layers_ = total;
    update_all_displays();
}

void PrintStatusPanel::set_times(int elapsed_secs, int remaining_secs) {
    elapsed_seconds_ = elapsed_secs;
    remaining_seconds_ = remaining_secs;
    update_all_displays();
}

void PrintStatusPanel::set_speeds(int speed_pct, int flow_pct) {
    speed_percent_ = speed_pct;
    flow_percent_ = flow_pct;
    update_all_displays();
}

void PrintStatusPanel::set_state(PrintState state) {
    current_state_ = state;
    update_all_displays();
    update_button_states();
    spdlog::debug("[{}] State changed to: {}", get_name(), static_cast<int>(state));
}

// ============================================================================
// PRE-PRINT PREPARATION STATE
// ============================================================================

void PrintStatusPanel::set_preparing(const std::string& operation_name, int current_step,
                                     int total_steps) {
    current_state_ = PrintState::Preparing;

    // Update operation name with step info: "Homing (1/3)"
    snprintf(preparing_operation_buf_, sizeof(preparing_operation_buf_), "%s (%d/%d)",
             operation_name.c_str(), current_step, total_steps);
    lv_subject_set_pointer(&preparing_operation_subject_, preparing_operation_buf_);

    // Calculate overall progress based on step position
    // Each step contributes equally to 100%
    int progress =
        (current_step > 0 && total_steps > 0) ? ((current_step - 1) * 100) / total_steps : 0;
    lv_subject_set_int(&preparing_progress_subject_, progress);

    // Animate bar directly for smooth visual feedback (300ms ease-out) if animations enabled
    if (preparing_progress_bar_) {
        lv_anim_enable_t anim_enable =
            SettingsManager::instance().get_animations_enabled() ? LV_ANIM_ON : LV_ANIM_OFF;
        lv_bar_set_value(preparing_progress_bar_, progress, anim_enable);
    }

    // Make preparing UI visible
    lv_subject_set_int(&preparing_visible_subject_, 1);

    spdlog::info("[{}] Preparing: {} (step {}/{})", get_name(), operation_name, current_step,
                 total_steps);
}

void PrintStatusPanel::set_preparing_progress(float progress) {
    // Clamp to valid range
    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;

    int pct = static_cast<int>(progress * 100.0f);
    lv_subject_set_int(&preparing_progress_subject_, pct);

    // Animate bar directly for smooth visual feedback (300ms ease-out) if animations enabled
    if (preparing_progress_bar_) {
        lv_anim_enable_t anim_enable =
            SettingsManager::instance().get_animations_enabled() ? LV_ANIM_ON : LV_ANIM_OFF;
        lv_bar_set_value(preparing_progress_bar_, pct, anim_enable);
    }

    spdlog::trace("[{}] Preparing progress: {}%", get_name(), pct);
}

void PrintStatusPanel::end_preparing(bool success) {
    // Hide preparing UI
    lv_subject_set_int(&preparing_visible_subject_, 0);
    lv_subject_set_int(&preparing_progress_subject_, 0);

    if (success) {
        // Transition to Printing state
        set_state(PrintState::Printing);
        spdlog::info("[{}] Preparation complete, starting print", get_name());
    } else {
        // Transition back to Idle
        set_state(PrintState::Idle);
        spdlog::warn("[{}] Preparation cancelled or failed", get_name());
    }
}

// ============================================================================
// EXCLUDE OBJECT FEATURE
// ============================================================================

constexpr uint32_t EXCLUDE_UNDO_WINDOW_MS = 5000; // 5 second undo window

void PrintStatusPanel::on_object_long_pressed(lv_obj_t* viewer, const char* object_name,
                                              void* user_data) {
    (void)viewer;
    auto* self = static_cast<PrintStatusPanel*>(user_data);
    if (self && object_name && object_name[0] != '\0') {
        self->handle_object_long_press(object_name);
    }
}

void PrintStatusPanel::handle_object_long_press(const char* object_name) {
    if (!object_name || object_name[0] == '\0') {
        spdlog::debug("[{}] Long-press on empty area (no object)", get_name());
        return;
    }

    // Check if already excluded
    if (excluded_objects_.count(object_name) > 0) {
        spdlog::info("[{}] Object '{}' already excluded - ignoring", get_name(), object_name);
        return;
    }

    // Check if there's already a pending exclusion
    if (!pending_exclude_object_.empty()) {
        spdlog::warn("[{}] Already have pending exclusion for '{}' - ignoring new request",
                     get_name(), pending_exclude_object_);
        return;
    }

    spdlog::info("[{}] Long-press on object: '{}' - showing confirmation", get_name(), object_name);

    // Store the object name for when confirmation happens
    pending_exclude_object_ = object_name;

    // Create confirmation dialog
    std::string title = "Exclude Object?";
    std::string message = "Stop printing \"" + std::string(object_name) +
                          "\"?\n\nThis cannot be undone after 5 seconds.";

    const char* attrs[] = {"title", title.c_str(), "message", message.c_str(), nullptr};

    lv_obj_t* screen = lv_screen_active();
    lv_xml_create(screen, "confirmation_dialog", attrs);

    // Find the created dialog (should be last child of screen)
    uint32_t child_cnt = lv_obj_get_child_count(screen);
    exclude_confirm_dialog_ =
        (child_cnt > 0) ? lv_obj_get_child(screen, static_cast<int32_t>(child_cnt - 1)) : nullptr;

    if (!exclude_confirm_dialog_) {
        spdlog::error("[{}] Failed to create exclude confirmation dialog", get_name());
        pending_exclude_object_.clear();
        return;
    }

    // Update button text - "Exclude" instead of default "Delete"
    lv_obj_t* confirm_btn = lv_obj_find_by_name(exclude_confirm_dialog_, "dialog_confirm_btn");
    if (confirm_btn) {
        lv_obj_t* btn_label = lv_obj_get_child(confirm_btn, 0);
        if (btn_label) {
            lv_label_set_text(btn_label, "Exclude");
        }
    }

    // Wire up button callbacks
    lv_obj_t* cancel_btn = lv_obj_find_by_name(exclude_confirm_dialog_, "dialog_cancel_btn");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_exclude_cancel_clicked, LV_EVENT_CLICKED, this);
    }
    if (confirm_btn) {
        lv_obj_add_event_cb(confirm_btn, on_exclude_confirm_clicked, LV_EVENT_CLICKED, this);
    }
}

void PrintStatusPanel::on_exclude_confirm_clicked(lv_event_t* e) {
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_exclude_confirmed();
    }
}

void PrintStatusPanel::on_exclude_cancel_clicked(lv_event_t* e) {
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_exclude_cancelled();
    }
}

void PrintStatusPanel::handle_exclude_confirmed() {
    spdlog::info("[{}] Exclusion confirmed for '{}'", get_name(), pending_exclude_object_);

    // Close the dialog
    if (exclude_confirm_dialog_) {
        lv_obj_delete(exclude_confirm_dialog_);
        exclude_confirm_dialog_ = nullptr;
    }

    if (pending_exclude_object_.empty()) {
        spdlog::error("[{}] No pending object for exclusion", get_name());
        return;
    }

    // Immediately update visual state in G-code viewer (red/semi-transparent)
    if (gcode_viewer_) {
        // For immediate visual feedback, we add to a "visually excluded" set
        // but don't send to Klipper yet - that happens after undo timer
        std::unordered_set<std::string> visual_excluded = excluded_objects_;
        visual_excluded.insert(pending_exclude_object_);
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, visual_excluded);
        spdlog::debug("[{}] Updated viewer with visual exclusion", get_name());
    }

    // Start undo timer - when it fires, we send EXCLUDE_OBJECT to Klipper
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
    }
    exclude_undo_timer_ = lv_timer_create(exclude_undo_timer_cb, EXCLUDE_UNDO_WINDOW_MS, this);
    lv_timer_set_repeat_count(exclude_undo_timer_, 1);

    // Show toast with "Undo" action button
    std::string toast_msg = "Excluding \"" + pending_exclude_object_ + "\"...";
    ui_toast_show_with_action(
        ToastSeverity::WARNING, toast_msg.c_str(), "Undo",
        [](void* user_data) {
            auto* self = static_cast<PrintStatusPanel*>(user_data);
            if (self) {
                self->handle_exclude_undo();
            }
        },
        this, EXCLUDE_UNDO_WINDOW_MS);

    spdlog::info("[{}] Started {}ms undo window for '{}'", get_name(), EXCLUDE_UNDO_WINDOW_MS,
                 pending_exclude_object_);
}

void PrintStatusPanel::handle_exclude_cancelled() {
    spdlog::info("[{}] Exclusion cancelled for '{}'", get_name(), pending_exclude_object_);

    // Close the dialog
    if (exclude_confirm_dialog_) {
        lv_obj_delete(exclude_confirm_dialog_);
        exclude_confirm_dialog_ = nullptr;
    }

    // Clear pending state
    pending_exclude_object_.clear();

    // Clear selection in viewer
    if (gcode_viewer_) {
        std::unordered_set<std::string> empty_set;
        ui_gcode_viewer_set_highlighted_objects(gcode_viewer_, empty_set);
    }
}

void PrintStatusPanel::handle_exclude_undo() {
    if (pending_exclude_object_.empty()) {
        spdlog::warn("[{}] Undo called but no pending exclusion", get_name());
        return;
    }

    spdlog::info("[{}] Undo pressed - cancelling exclusion of '{}'", get_name(),
                 pending_exclude_object_);

    // Cancel the timer
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
        exclude_undo_timer_ = nullptr;
    }

    // Restore visual state - remove from visual exclusion
    if (gcode_viewer_) {
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, excluded_objects_);
    }

    // Clear pending
    pending_exclude_object_.clear();

    // Show confirmation that undo succeeded
    ui_toast_show(ToastSeverity::SUCCESS, "Exclusion cancelled", 2000);
}

void PrintStatusPanel::exclude_undo_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<PrintStatusPanel*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    self->exclude_undo_timer_ = nullptr; // Timer auto-deletes after single shot

    if (self->pending_exclude_object_.empty()) {
        spdlog::warn("[PrintStatusPanel] Undo timer fired but no pending object");
        return;
    }

    std::string object_name = self->pending_exclude_object_;
    self->pending_exclude_object_.clear();

    spdlog::info("[PrintStatusPanel] Undo window expired - sending EXCLUDE_OBJECT for '{}'",
                 object_name);

    // Actually send the command to Klipper via MoonrakerAPI
    if (self->api_) {
        self->api_->exclude_object(
            object_name,
            [self, object_name]() {
                spdlog::info("[PrintStatusPanel] EXCLUDE_OBJECT '{}' sent successfully",
                             object_name);
                // Move to confirmed excluded set
                self->excluded_objects_.insert(object_name);
            },
            [self, object_name](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Failed to exclude '{}': {}", object_name,
                              err.message);
                NOTIFY_ERROR("Failed to exclude '{}': {}", object_name, err.user_message());

                // Revert visual state - refresh viewer with only confirmed exclusions
                if (self->gcode_viewer_) {
                    ui_gcode_viewer_set_excluded_objects(self->gcode_viewer_,
                                                         self->excluded_objects_);
                    spdlog::debug("[PrintStatusPanel] Reverted visual exclusion for '{}'",
                                  object_name);
                }
            });
    } else {
        spdlog::warn("[PrintStatusPanel] No API available - simulating exclusion");
        self->excluded_objects_.insert(object_name);
    }
}
