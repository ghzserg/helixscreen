// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_status.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_event_safety.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_utils.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <memory>

// Global instance for legacy API and resize callback
static std::unique_ptr<PrintStatusPanel> g_print_status_panel;

// Helper to get or create the global instance
PrintStatusPanel& get_global_print_status_panel() {
    if (!g_print_status_panel) {
        g_print_status_panel = std::make_unique<PrintStatusPanel>(get_printer_state(), nullptr);
    }
    return *g_print_status_panel;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PrintStatusPanel::PrintStatusPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Buffers are initialized with default values in header
}

PrintStatusPanel::~PrintStatusPanel() {
    // Note: Do NOT call ui_resize_handler_unregister here!
    // During static destruction order, the resize handler may already be destroyed.
    // The resize handler uses a weak reference pattern - if the panel is gone,
    // it simply won't call the callback.
    resize_registered_ = false;
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void PrintStatusPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize all 10 subjects with default values
    UI_SUBJECT_INIT_AND_REGISTER_STRING(filename_subject_, filename_buf_, "No print active",
                                        "print_filename");
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
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pause_button_subject_, pause_button_buf_, "Pause",
                                        "pause_button_text");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized (10 subjects)", get_name());
}

void PrintStatusPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up panel...", get_name());

    // Calculate width to fill remaining space after navigation bar
    lv_coord_t screen_width = lv_obj_get_width(parent_screen_);
    lv_coord_t nav_width = screen_width / 10;
    lv_obj_set_width(panel_, screen_width - nav_width);

    // Use standard overlay panel setup for header/content/back button
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return;
    }

    // Force layout calculation before scaling images
    lv_obj_update_layout(panel_);

    // Perform initial image scaling
    scale_thumbnail_images();

    // Register resize callback
    ui_resize_handler_register(on_resize_static);
    resize_registered_ = true;

    // Wire up event handlers
    spdlog::debug("[{}] Wiring event handlers...", get_name());

    // Nozzle temperature card
    lv_obj_t* nozzle_card = lv_obj_find_by_name(overlay_content, "nozzle_temp_card");
    if (nozzle_card) {
        lv_obj_add_event_cb(nozzle_card, on_nozzle_card_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}]   ✓ Nozzle temp card", get_name());
    } else {
        spdlog::error("[{}]   ✗ Nozzle temp card NOT FOUND", get_name());
    }

    // Bed temperature card
    lv_obj_t* bed_card = lv_obj_find_by_name(overlay_content, "bed_temp_card");
    if (bed_card) {
        lv_obj_add_event_cb(bed_card, on_bed_card_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}]   ✓ Bed temp card", get_name());
    } else {
        spdlog::error("[{}]   ✗ Bed temp card NOT FOUND", get_name());
    }

    // Light button
    lv_obj_t* light_btn = lv_obj_find_by_name(overlay_content, "btn_light");
    if (light_btn) {
        lv_obj_add_event_cb(light_btn, on_light_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}]   ✓ Light button", get_name());
    } else {
        spdlog::error("[{}]   ✗ Light button NOT FOUND", get_name());
    }

    // Pause button
    lv_obj_t* pause_btn = lv_obj_find_by_name(overlay_content, "btn_pause");
    if (pause_btn) {
        lv_obj_add_event_cb(pause_btn, on_pause_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}]   ✓ Pause button", get_name());
    } else {
        spdlog::error("[{}]   ✗ Pause button NOT FOUND", get_name());
    }

    // Tune button
    lv_obj_t* tune_btn = lv_obj_find_by_name(overlay_content, "btn_tune");
    if (tune_btn) {
        lv_obj_add_event_cb(tune_btn, on_tune_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}]   ✓ Tune button", get_name());
    } else {
        spdlog::error("[{}]   ✗ Tune button NOT FOUND", get_name());
    }

    // Cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(overlay_content, "btn_cancel");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_cancel_clicked, LV_EVENT_CLICKED, this);
        spdlog::debug("[{}]   ✓ Cancel button", get_name());
    } else {
        spdlog::error("[{}]   ✗ Cancel button NOT FOUND", get_name());
    }

    // Progress bar widget
    progress_bar_ = lv_obj_find_by_name(overlay_content, "print_progress");
    if (progress_bar_) {
        lv_bar_set_range(progress_bar_, 0, 100);
        lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
        spdlog::debug("[{}]   ✓ Progress bar", get_name());
    } else {
        spdlog::error("[{}]   ✗ Progress bar NOT FOUND", get_name());
    }

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void PrintStatusPanel::format_time(int seconds, char* buf, size_t buf_size) {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    std::snprintf(buf, buf_size, "%dh %02dm", hours, minutes);
}

void PrintStatusPanel::update_all_displays() {
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

    // Temperatures
    std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    lv_subject_copy_string(&nozzle_temp_subject_, nozzle_temp_buf_);

    std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%d / %d°C", bed_current_, bed_target_);
    lv_subject_copy_string(&bed_temp_subject_, bed_temp_buf_);

    // Speeds
    std::snprintf(speed_buf_, sizeof(speed_buf_), "%d%%", speed_percent_);
    lv_subject_copy_string(&speed_subject_, speed_buf_);

    std::snprintf(flow_buf_, sizeof(flow_buf_), "%d%%", flow_percent_);
    lv_subject_copy_string(&flow_subject_, flow_buf_);

    // Update progress bar widget directly
    if (progress_bar_) {
        lv_bar_set_value(progress_bar_, current_progress_, LV_ANIM_OFF);
    }

    // Update pause button text based on state
    if (current_state_ == PrintState::Paused) {
        std::snprintf(pause_button_buf_, sizeof(pause_button_buf_), "Resume");
    } else {
        std::snprintf(pause_button_buf_, sizeof(pause_button_buf_), "Pause");
    }
    lv_subject_copy_string(&pause_button_subject_, pause_button_buf_);
}

void PrintStatusPanel::scale_thumbnail_images() {
    if (!panel_) return;

    // Find thumbnail section to get target dimensions
    lv_obj_t* thumbnail_section = lv_obj_find_by_name(panel_, "thumbnail_section");
    if (!thumbnail_section) {
        spdlog::warn("[{}] Thumbnail section not found, cannot scale images", get_name());
        return;
    }

    lv_coord_t section_width = lv_obj_get_width(thumbnail_section);
    lv_coord_t section_height = lv_obj_get_height(thumbnail_section);

    // Scale gradient background to cover the entire section
    lv_obj_t* gradient_bg = lv_obj_find_by_name(panel_, "gradient_background");
    if (gradient_bg) {
        ui_image_scale_to_cover(gradient_bg, section_width, section_height);
    }

    // Scale thumbnail to contain within section (no cropping)
    lv_obj_t* thumbnail = lv_obj_find_by_name(panel_, "print_thumbnail");
    if (thumbnail) {
        ui_image_scale_to_contain(thumbnail, section_width, section_height, LV_IMAGE_ALIGN_TOP_MID);
    }
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void PrintStatusPanel::handle_nozzle_card_click() {
    spdlog::debug("[{}] Nozzle temp card clicked", get_name());
    // TODO: Show nozzle temperature adjustment panel
}

void PrintStatusPanel::handle_bed_card_click() {
    spdlog::debug("[{}] Bed temp card clicked", get_name());
    // TODO: Show bed temperature adjustment panel
}

void PrintStatusPanel::handle_light_button() {
    spdlog::debug("[{}] Light button clicked", get_name());
    // TODO: Toggle printer LED/light on/off
}

void PrintStatusPanel::handle_pause_button() {
    if (current_state_ == PrintState::Printing) {
        spdlog::info("[{}] Pausing print...", get_name());
        set_state(PrintState::Paused);
        // TODO: Send pause command to printer via api_
    } else if (current_state_ == PrintState::Paused) {
        spdlog::info("[{}] Resuming print...", get_name());
        set_state(PrintState::Printing);
        // TODO: Send resume command to printer via api_
    }
}

void PrintStatusPanel::handle_tune_button() {
    spdlog::info("[{}] Tune button clicked (not yet implemented)", get_name());
    // TODO: Open tuning overlay with speed/flow/temp adjustments
}

void PrintStatusPanel::handle_cancel_button() {
    spdlog::info("[{}] Cancel button clicked", get_name());
    // TODO: Show confirmation dialog, then cancel print
    set_state(PrintState::Cancelled);
    stop_mock_print();
}

void PrintStatusPanel::handle_resize() {
    spdlog::debug("[{}] Handling resize event", get_name());
    scale_thumbnail_images();
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void PrintStatusPanel::on_nozzle_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_nozzle_card_clicked");
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_nozzle_card_click();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_bed_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_bed_card_clicked");
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_bed_card_click();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_light_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_light_clicked");
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_light_button();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_pause_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_pause_clicked");
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_pause_button();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_tune_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_tune_clicked");
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_tune_button();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_cancel_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_cancel_clicked");
    auto* self = static_cast<PrintStatusPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_cancel_button();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_resize_static() {
    // Use global instance for resize callback (registered without user_data)
    if (g_print_status_panel) {
        g_print_status_panel->handle_resize();
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void PrintStatusPanel::set_filename(const char* filename) {
    std::snprintf(filename_buf_, sizeof(filename_buf_), "%s", filename);
    lv_subject_copy_string(&filename_subject_, filename_buf_);
}

void PrintStatusPanel::set_progress(int percent) {
    current_progress_ = percent;
    if (current_progress_ < 0) current_progress_ = 0;
    if (current_progress_ > 100) current_progress_ = 100;
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

void PrintStatusPanel::set_temperatures(int nozzle_cur, int nozzle_tgt, int bed_cur, int bed_tgt) {
    nozzle_current_ = nozzle_cur;
    nozzle_target_ = nozzle_tgt;
    bed_current_ = bed_cur;
    bed_target_ = bed_tgt;
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
    spdlog::debug("[{}] State changed to: {}", get_name(), static_cast<int>(state));
}

// ============================================================================
// MOCK PRINT SIMULATION
// ============================================================================

void PrintStatusPanel::start_mock_print(const char* filename, int layers, int duration_secs) {
    mock_active_ = true;
    mock_total_seconds_ = duration_secs;
    mock_elapsed_seconds_ = 0;
    mock_total_layers_ = layers;

    set_filename(filename);
    set_progress(0);
    set_layer(0, layers);
    set_times(0, duration_secs);
    set_temperatures(215, 215, 60, 60);
    set_speeds(100, 100);
    set_state(PrintState::Printing);

    spdlog::info("[{}] Mock print started: {} ({} layers, {} seconds)", get_name(), filename,
                 layers, duration_secs);
}

void PrintStatusPanel::stop_mock_print() {
    mock_active_ = false;
    spdlog::info("[{}] Mock print stopped", get_name());
}

void PrintStatusPanel::tick_mock_print() {
    if (!mock_active_) return;
    if (current_state_ != PrintState::Printing) return;
    if (mock_elapsed_seconds_ >= mock_total_seconds_) {
        // Print complete
        set_state(PrintState::Complete);
        stop_mock_print();
        spdlog::info("[{}] Mock print complete!", get_name());
        return;
    }

    // Advance simulation by 1 second
    mock_elapsed_seconds_++;

    // Calculate progress
    int progress = (mock_elapsed_seconds_ * 100) / mock_total_seconds_;
    int remaining = mock_total_seconds_ - mock_elapsed_seconds_;
    int layer = (mock_elapsed_seconds_ * mock_total_layers_) / mock_total_seconds_;

    // Update displays
    set_progress(progress);
    set_layer(layer, mock_total_layers_);
    set_times(mock_elapsed_seconds_, remaining);

    // Simulate temperature fluctuations (±2°C)
    int nozzle_var = (mock_elapsed_seconds_ % 4) - 2;
    int bed_var = (mock_elapsed_seconds_ % 6) - 3;
    set_temperatures(215 + nozzle_var, 215, 60 + bed_var, 60);
}

// Temporary wrapper for tick function (still called by main.cpp)
void ui_panel_print_status_tick_mock_print() {
    auto& panel = get_global_print_status_panel();
    panel.tick_mock_print();
}
