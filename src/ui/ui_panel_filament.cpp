// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_filament.h"

#include "ui_component_keypad.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_icon.h"
#include "ui_nav.h"
#include "ui_panel_ams.h"
#include "ui_panel_temp_control.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_theme.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "ams_state.h"
#include "app_constants.h"
#include "app_globals.h"
#include "filament_database.h"
#include "filament_sensor_manager.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "standard_macros.h"
#include "static_panel_registry.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

// Preset material names (indexed by material ID: 0=PLA, 1=PETG, 2=ABS, 3=TPU)
// Temperatures are now looked up from filament_database.h
static constexpr const char* PRESET_MATERIAL_NAMES[] = {"PLA", "PETG", "ABS", "TPU"};
static constexpr int PRESET_COUNT = 4;

// Format string for safety warning (used in constructor and set_limits)
static constexpr const char* SAFETY_WARNING_FMT = "Heat to at least %d°C to load/unload";

using helix::ui::observe_int_async;
using helix::ui::temperature::centi_to_degrees;
using helix::ui::temperature::format_target_or_off;
using helix::ui::temperature::get_heating_state_color;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

FilamentPanel::FilamentPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents with default values
    std::snprintf(temp_display_buf_, sizeof(temp_display_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    std::snprintf(status_buf_, sizeof(status_buf_), "%s", "Select material to begin");
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C | Target: %d°C",
                  nozzle_current_, nozzle_target_);
    std::snprintf(safety_warning_text_buf_, sizeof(safety_warning_text_buf_), SAFETY_WARNING_FMT,
                  min_extrude_temp_);
    format_target_or_off(0, material_nozzle_buf_, sizeof(material_nozzle_buf_));
    format_target_or_off(0, material_bed_buf_, sizeof(material_bed_buf_));
    std::snprintf(nozzle_current_buf_, sizeof(nozzle_current_buf_), "%d°C", nozzle_current_);
    format_target_or_off(0, nozzle_target_buf_, sizeof(nozzle_target_buf_));
    std::snprintf(bed_current_buf_, sizeof(bed_current_buf_), "%d°C", bed_current_);
    format_target_or_off(0, bed_target_buf_, sizeof(bed_target_buf_));

    // Register XML event callbacks
    lv_xml_register_event_cb(nullptr, "filament_manage_slots_cb", on_manage_slots_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_load", on_load_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_unload", on_unload_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge", on_purge_clicked);

    // Material preset buttons
    lv_xml_register_event_cb(nullptr, "on_filament_preset_pla", on_preset_pla_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_preset_petg", on_preset_petg_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_preset_abs", on_preset_abs_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_preset_tpu", on_preset_tpu_clicked);

    // Temperature tap targets
    lv_xml_register_event_cb(nullptr, "on_filament_nozzle_temp_tap", on_nozzle_temp_tap_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_bed_temp_tap", on_bed_temp_tap_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_nozzle_target_tap",
                             on_nozzle_target_tap_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_bed_target_tap", on_bed_target_tap_clicked);

    // Purge amount buttons
    lv_xml_register_event_cb(nullptr, "on_filament_purge_5mm", on_purge_5mm_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge_10mm", on_purge_10mm_clicked);
    lv_xml_register_event_cb(nullptr, "on_filament_purge_25mm", on_purge_25mm_clicked);

    // Cooldown button
    lv_xml_register_event_cb(nullptr, "on_filament_cooldown", on_cooldown_clicked);

    // Subscribe to PrinterState temperatures to show actual printer state
    // NOTE: Observers must defer UI updates via ui_async_call to avoid render-phase assertions
    // [L029]
    extruder_temp_observer_ = observe_int_async<FilamentPanel>(
        printer_state_.get_extruder_temp_subject(), this,
        [](FilamentPanel* self, int raw) { self->nozzle_current_ = centi_to_degrees(raw); },
        [](FilamentPanel* self) {
            self->update_left_card_temps();
            self->update_temp_display();
            self->update_warning_text();
            self->update_safety_state();
            self->update_status();
        });

    extruder_target_observer_ = observe_int_async<FilamentPanel>(
        printer_state_.get_extruder_target_subject(), this,
        [](FilamentPanel* self, int raw) { self->nozzle_target_ = centi_to_degrees(raw); },
        [](FilamentPanel* self) {
            self->update_left_card_temps();
            self->update_material_temp_display();
            self->update_warning_text();
            self->update_status();
            self->check_and_auto_select_preset();
            lv_subject_set_int(&self->nozzle_heating_subject_, self->nozzle_target_ > 0 ? 1 : 0);
        });

    bed_temp_observer_ = observe_int_async<FilamentPanel>(
        printer_state_.get_bed_temp_subject(), this,
        [](FilamentPanel* self, int raw) { self->bed_current_ = centi_to_degrees(raw); },
        [](FilamentPanel* self) { self->update_left_card_temps(); });

    bed_target_observer_ = observe_int_async<FilamentPanel>(
        printer_state_.get_bed_target_subject(), this,
        [](FilamentPanel* self, int raw) { self->bed_target_ = centi_to_degrees(raw); },
        [](FilamentPanel* self) {
            self->update_left_card_temps();
            self->update_material_temp_display();
            self->check_and_auto_select_preset();
        });
}

FilamentPanel::~FilamentPanel() {
    deinit_subjects();

    // Clean up warning dialogs if open (prevents memory leak and use-after-free)
    if (lv_is_initialized()) {
        if (load_warning_dialog_) {
            ui_modal_hide(load_warning_dialog_);
            load_warning_dialog_ = nullptr;
        }
        if (unload_warning_dialog_) {
            ui_modal_hide(unload_warning_dialog_);
            unload_warning_dialog_ = nullptr;
        }
    }
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void FilamentPanel::init_subjects() {
    init_subjects_guarded([this]() {
        // Initialize subjects with default values
        UI_MANAGED_SUBJECT_STRING(temp_display_subject_, temp_display_buf_, temp_display_buf_,
                                  "filament_temp_display", subjects_);
        UI_MANAGED_SUBJECT_STRING(status_subject_, status_buf_, status_buf_, "filament_status",
                                  subjects_);
        UI_MANAGED_SUBJECT_INT(material_selected_subject_, -1, "filament_material_selected",
                               subjects_);
        UI_MANAGED_SUBJECT_INT(extrusion_allowed_subject_, 0, "filament_extrusion_allowed",
                               subjects_); // false (cold at start)
        UI_MANAGED_SUBJECT_INT(safety_warning_visible_subject_, 1,
                               "filament_safety_warning_visible",
                               subjects_); // true (cold at start)
        UI_MANAGED_SUBJECT_STRING(warning_temps_subject_, warning_temps_buf_, warning_temps_buf_,
                                  "filament_warning_temps", subjects_);
        UI_MANAGED_SUBJECT_STRING(safety_warning_text_subject_, safety_warning_text_buf_,
                                  safety_warning_text_buf_, "filament_safety_warning_text",
                                  subjects_);

        // Material temperature display subjects (for right side preset displays)
        UI_MANAGED_SUBJECT_STRING(material_nozzle_temp_subject_, material_nozzle_buf_,
                                  material_nozzle_buf_, "filament_material_nozzle_temp", subjects_);
        UI_MANAGED_SUBJECT_STRING(material_bed_temp_subject_, material_bed_buf_, material_bed_buf_,
                                  "filament_material_bed_temp", subjects_);

        // Left card temperature subjects (current and target for nozzle/bed)
        UI_MANAGED_SUBJECT_STRING(nozzle_current_subject_, nozzle_current_buf_, nozzle_current_buf_,
                                  "filament_nozzle_current", subjects_);
        UI_MANAGED_SUBJECT_STRING(nozzle_target_subject_, nozzle_target_buf_, nozzle_target_buf_,
                                  "filament_nozzle_target", subjects_);
        UI_MANAGED_SUBJECT_STRING(bed_current_subject_, bed_current_buf_, bed_current_buf_,
                                  "filament_bed_current", subjects_);
        UI_MANAGED_SUBJECT_STRING(bed_target_subject_, bed_target_buf_, bed_target_buf_,
                                  "filament_bed_target", subjects_);

        // Operation in progress subject (for disabling buttons during filament ops)
        UI_MANAGED_SUBJECT_INT(operation_in_progress_subject_, 0, "filament_operation_in_progress",
                               subjects_);

        // Cooldown button visibility (1 when nozzle target > 0)
        UI_MANAGED_SUBJECT_INT(nozzle_heating_subject_, 0, "filament_nozzle_heating", subjects_);

        // Purge amount button active states (boolean: 0=inactive, 1=active)
        // Using separate subjects because bind_style doesn't work well with multiple ref_values
        UI_MANAGED_SUBJECT_INT(purge_5mm_active_subject_, 0, "filament_purge_5mm_active",
                               subjects_);
        UI_MANAGED_SUBJECT_INT(purge_10mm_active_subject_, 1, "filament_purge_10mm_active",
                               subjects_);
        UI_MANAGED_SUBJECT_INT(purge_25mm_active_subject_, 0, "filament_purge_25mm_active",
                               subjects_);

        spdlog::debug("[{}] temp={}/{}°C, material={}", get_name(), nozzle_current_, nozzle_target_,
                      selected_material_);
    });
}

void FilamentPanel::deinit_subjects() {
    deinit_subjects_base(subjects_);
}

void FilamentPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Filament macros now resolved via StandardMacros singleton (auto-detected or user-configured)
    spdlog::debug("[{}] Setting up (events handled declaratively via XML)", get_name());

    // Find preset buttons (for visual state updates)
    const char* preset_names[] = {"preset_pla", "preset_petg", "preset_abs", "preset_tpu"};
    for (int i = 0; i < 4; i++) {
        preset_buttons_[i] = lv_obj_find_by_name(panel_, preset_names[i]);
    }

    // Action buttons (btn_load, btn_unload, btn_purge) - disabled state managed by XML bindings

    // Find safety warning card
    safety_warning_ = lv_obj_find_by_name(panel_, "safety_warning");

    // Find status icon for dynamic updates
    status_icon_ = lv_obj_find_by_name(panel_, "status_icon");

    // Find temperature labels for color updates
    nozzle_current_label_ = lv_obj_find_by_name(panel_, "nozzle_current_temp");
    bed_current_label_ = lv_obj_find_by_name(panel_, "bed_current_temp");

    // Find temp layout widgets for dynamic sizing when AMS is hidden
    temp_group_ = lv_obj_find_by_name(panel_, "temp_group");
    temp_graph_card_ = lv_obj_find_by_name(panel_, "temp_graph_card");

    // Subscribe to AMS type to expand temp graph when no AMS present [L020] [L029]
    ams_type_observer_ = ObserverGuard(
        AmsState::instance().get_ams_type_subject(),
        [](lv_observer_t* observer, [[maybe_unused]] lv_subject_t* subject) {
            auto* self = static_cast<FilamentPanel*>(lv_observer_get_user_data(observer));
            if (!self || !self->temp_group_ || !self->temp_graph_card_)
                return;

            ui_async_call(
                [](void* ctx) {
                    auto* panel = static_cast<FilamentPanel*>(ctx);
                    bool has_ams =
                        (lv_subject_get_int(AmsState::instance().get_ams_type_subject()) != 0);

                    if (has_ams) {
                        // AMS visible: standard 120px graph
                        lv_obj_set_height(panel->temp_graph_card_, 120);
                        lv_obj_set_flex_grow(panel->temp_group_, 0);
                        lv_obj_set_flex_grow(panel->temp_graph_card_, 0);
                    } else {
                        // AMS hidden: expand graph to fill available space
                        lv_obj_set_flex_grow(panel->temp_group_, 1);
                        lv_obj_set_flex_grow(panel->temp_graph_card_, 1);
                    }
                },
                self);
        },
        this);

    // Initialize visual state
    update_preset_buttons_visual();
    update_temp_display();
    update_left_card_temps();
    update_material_temp_display();
    update_status();
    update_status_icon_for_state();
    update_warning_text();
    update_safety_state();

    // Trigger initial purge button selection (notifies bind_style observers)
    handle_purge_amount_select(purge_amount_);

    // Setup combined temperature graph if TempControlPanel is available
    if (temp_control_panel_) {
        lv_obj_t* graph_container = lv_obj_find_by_name(panel_, "temp_graph_container");
        if (graph_container) {
            temp_control_panel_->setup_mini_combined_graph(graph_container);
            spdlog::debug("[{}] Temperature graph initialized", get_name());
        } else {
            spdlog::warn("[{}] temp_graph_container not found in XML", get_name());
        }
    }

    // AMS mini status widget is now created declaratively via XML <ams_mini_status/>

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void FilamentPanel::update_temp_display() {
    std::snprintf(temp_display_buf_, sizeof(temp_display_buf_), "%d / %d°C", nozzle_current_,
                  nozzle_target_);
    lv_subject_copy_string(&temp_display_subject_, temp_display_buf_);
}

void FilamentPanel::update_status_icon(const char* icon_name, const char* variant) {
    if (!status_icon_)
        return;

    // Update icon imperatively using ui_icon API
    ui_icon_set_source(status_icon_, icon_name);
    ui_icon_set_variant(status_icon_, variant);
}

void FilamentPanel::update_status() {
    const char* status_msg;

    if (helix::ui::temperature::is_extrusion_safe(nozzle_current_, min_extrude_temp_)) {
        // Hot enough - ready to load
        status_msg = "Ready to load";
        update_status_icon("check", "success");
    } else if (nozzle_target_ >= min_extrude_temp_) {
        // Heating in progress
        std::snprintf(status_buf_, sizeof(status_buf_), "Heating to %d°C...", nozzle_target_);
        lv_subject_copy_string(&status_subject_, status_buf_);
        update_status_icon("flash", "warning");
        return; // Already updated, exit early
    } else {
        // Cold - needs material selection
        status_msg = "Select material to begin";
        update_status_icon("cooldown", "secondary");
    }

    lv_subject_copy_string(&status_subject_, status_msg);
}

void FilamentPanel::update_warning_text() {
    std::snprintf(warning_temps_buf_, sizeof(warning_temps_buf_), "Current: %d°C | Target: %d°C",
                  nozzle_current_, nozzle_target_);
    lv_subject_copy_string(&warning_temps_subject_, warning_temps_buf_);
}

void FilamentPanel::update_safety_state() {
    bool allowed = helix::ui::temperature::is_extrusion_safe(nozzle_current_, min_extrude_temp_);

    // Update reactive subjects - XML bindings handle button disabled state and safety warning
    // visibility
    lv_subject_set_int(&extrusion_allowed_subject_, allowed ? 1 : 0);
    lv_subject_set_int(&safety_warning_visible_subject_, allowed ? 0 : 1);

    spdlog::trace("[{}] Safety state updated: allowed={} (temp={}°C)", get_name(), allowed,
                  nozzle_current_);
}

void FilamentPanel::update_preset_buttons_visual() {
    for (int i = 0; i < 4; i++) {
        if (preset_buttons_[i]) {
            if (i == selected_material_) {
                // Selected state - theme handles colors
                lv_obj_add_state(preset_buttons_[i], LV_STATE_CHECKED);
            } else {
                // Unselected state - theme handles colors
                lv_obj_remove_state(preset_buttons_[i], LV_STATE_CHECKED);
            }
        }
    }
}

void FilamentPanel::check_and_auto_select_preset() {
    // Check if both nozzle and bed targets match any preset
    int matching_preset = -1;
    for (int i = 0; i < PRESET_COUNT; i++) {
        auto mat = filament::find_material(PRESET_MATERIAL_NAMES[i]);
        if (mat && nozzle_target_ == mat->nozzle_recommended() && bed_target_ == mat->bed_temp) {
            matching_preset = i;
            break;
        }
    }

    // Only update if selection changed
    if (matching_preset != selected_material_) {
        selected_material_ = matching_preset;
        lv_subject_set_int(&material_selected_subject_, selected_material_);
        update_preset_buttons_visual();

        if (matching_preset >= 0) {
            spdlog::debug("[{}] Auto-selected preset: {} (nozzle={}°C, bed={}°C)", get_name(),
                          PRESET_MATERIAL_NAMES[matching_preset], nozzle_target_, bed_target_);
        } else {
            spdlog::debug("[{}] No matching preset for nozzle={}°C, bed={}°C", get_name(),
                          nozzle_target_, bed_target_);
        }
    }
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void FilamentPanel::handle_preset_button(int material_id) {
    if (material_id < 0 || material_id >= PRESET_COUNT) {
        spdlog::error("[{}] Invalid preset ID {} (valid: 0-{})", get_name(), material_id,
                      PRESET_COUNT - 1);
        return;
    }

    auto mat = filament::find_material(PRESET_MATERIAL_NAMES[material_id]);
    if (!mat) {
        spdlog::error("[{}] Material '{}' not found in database", get_name(),
                      PRESET_MATERIAL_NAMES[material_id]);
        return;
    }

    selected_material_ = material_id;
    nozzle_target_ = mat->nozzle_recommended();
    bed_target_ = mat->bed_temp;

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_material_temp_display();
    update_status();

    spdlog::info("[{}] Material selected: {} (nozzle={}°C, bed={}°C)", get_name(),
                 PRESET_MATERIAL_NAMES[material_id], nozzle_target_, bed_target_);

    // Send temperature commands to printer (both nozzle and bed)
    if (api_) {
        api_->set_temperature(
            "extruder", static_cast<double>(nozzle_target_),
            [target = nozzle_target_]() { NOTIFY_SUCCESS("Nozzle target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
            });
        api_->set_temperature(
            "heater_bed", static_cast<double>(bed_target_),
            [target = bed_target_]() { NOTIFY_SUCCESS("Bed target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set bed temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_nozzle_temp_tap() {
    spdlog::debug("[{}] Opening custom nozzle temperature keypad", get_name());

    ui_keypad_config_t config = {.initial_value =
                                     static_cast<float>(nozzle_target_ > 0 ? nozzle_target_ : 200),
                                 .min_value = 0.0f,
                                 .max_value = static_cast<float>(nozzle_max_temp_),
                                 .title_label = "Nozzle Temperature",
                                 .unit_label = "°C",
                                 .allow_decimal = false,
                                 .allow_negative = false,
                                 .callback = custom_nozzle_keypad_cb,
                                 .user_data = this};

    ui_keypad_show(&config);
}

void FilamentPanel::handle_bed_temp_tap() {
    spdlog::debug("[{}] Opening custom bed temperature keypad", get_name());

    ui_keypad_config_t config = {.initial_value =
                                     static_cast<float>(bed_target_ > 0 ? bed_target_ : 60),
                                 .min_value = 0.0f,
                                 .max_value = static_cast<float>(bed_max_temp_),
                                 .title_label = "Bed Temperature",
                                 .unit_label = "°C",
                                 .allow_decimal = false,
                                 .allow_negative = false,
                                 .callback = custom_bed_keypad_cb,
                                 .user_data = this};

    ui_keypad_show(&config);
}

void FilamentPanel::handle_custom_nozzle_confirmed(float value) {
    spdlog::info("[{}] Custom nozzle temperature confirmed: {}°C", get_name(),
                 static_cast<int>(value));

    nozzle_target_ = static_cast<int>(value);
    // Deselect any preset since user set custom temp
    selected_material_ = -1;
    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_material_temp_display();
    update_status();

    // Send temperature command to printer
    if (api_) {
        api_->set_temperature(
            "extruder", static_cast<double>(nozzle_target_),
            [target = nozzle_target_]() { NOTIFY_SUCCESS("Nozzle target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set nozzle temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::handle_custom_bed_confirmed(float value) {
    spdlog::info("[{}] Custom bed temperature confirmed: {}°C", get_name(),
                 static_cast<int>(value));

    bed_target_ = static_cast<int>(value);
    // Deselect any preset since user set custom temp
    selected_material_ = -1;
    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_material_temp_display();

    // Send temperature command to printer
    if (api_) {
        api_->set_temperature(
            "heater_bed", static_cast<double>(bed_target_),
            [target = bed_target_]() { NOTIFY_SUCCESS("Bed target set to {}°C", target); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to set bed temp: {}", error.user_message());
            });
    }
}

void FilamentPanel::update_material_temp_display() {
    // Use centralized formatting with em dash for heater-off state
    format_target_or_off(nozzle_target_, material_nozzle_buf_, sizeof(material_nozzle_buf_));
    format_target_or_off(bed_target_, material_bed_buf_, sizeof(material_bed_buf_));
    lv_subject_copy_string(&material_nozzle_temp_subject_, material_nozzle_buf_);
    lv_subject_copy_string(&material_bed_temp_subject_, material_bed_buf_);
}

void FilamentPanel::update_left_card_temps() {
    // Update current temps
    std::snprintf(nozzle_current_buf_, sizeof(nozzle_current_buf_), "%d°C", nozzle_current_);
    std::snprintf(bed_current_buf_, sizeof(bed_current_buf_), "%d°C", bed_current_);
    lv_subject_copy_string(&nozzle_current_subject_, nozzle_current_buf_);
    lv_subject_copy_string(&bed_current_subject_, bed_current_buf_);

    // Update target temps using centralized formatting with em dash for heater-off state
    format_target_or_off(nozzle_target_, nozzle_target_buf_, sizeof(nozzle_target_buf_));
    format_target_or_off(bed_target_, bed_target_buf_, sizeof(bed_target_buf_));
    lv_subject_copy_string(&nozzle_target_subject_, nozzle_target_buf_);
    lv_subject_copy_string(&bed_target_subject_, bed_target_buf_);

    // Update temperature label colors using 4-state heating logic
    // (matches temp_display widget: gray=off, red=heating, green=at-temp, blue=cooling)
    if (nozzle_current_label_) {
        lv_color_t nozzle_color = get_heating_state_color(nozzle_current_, nozzle_target_);
        lv_obj_set_style_text_color(nozzle_current_label_, nozzle_color, LV_PART_MAIN);
    }
    if (bed_current_label_) {
        lv_color_t bed_color = get_heating_state_color(bed_current_, bed_target_);
        lv_obj_set_style_text_color(bed_current_label_, bed_color, LV_PART_MAIN);
    }
}

void FilamentPanel::update_status_icon_for_state() {
    // Determine icon and color based on current state
    if (nozzle_target_ == 0 && bed_target_ == 0) {
        // Idle - no target set
        update_status_icon("info", "secondary");
    } else if (nozzle_current_ < nozzle_target_ - 5 || bed_current_ < bed_target_ - 5) {
        // Heating
        update_status_icon("fire", "warning");
    } else if (nozzle_current_ > nozzle_target_ + 5 && nozzle_target_ > 0) {
        // Cooling down
        update_status_icon("cooldown", "info");
    } else {
        // At temperature
        update_status_icon("check", "success");
    }
}

void FilamentPanel::set_operation_in_progress(bool in_progress) {
    operation_in_progress_ = in_progress;
    lv_subject_set_int(&operation_in_progress_subject_, in_progress ? 1 : 0);
}

void FilamentPanel::handle_purge_amount_select(int amount) {
    purge_amount_ = amount;
    // Update boolean subjects for each button (only one active at a time)
    lv_subject_set_int(&purge_5mm_active_subject_, amount == 5 ? 1 : 0);
    lv_subject_set_int(&purge_10mm_active_subject_, amount == 10 ? 1 : 0);
    lv_subject_set_int(&purge_25mm_active_subject_, amount == 25 ? 1 : 0);
    spdlog::info("[{}] Purge amount set to {}mm", get_name(), amount);
}

void FilamentPanel::handle_load_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for filament load ({}°C, min: {}°C)", nozzle_current_,
                       min_extrude_temp_);
        return;
    }

    // Check if toolhead sensor shows filament already present
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::TOOLHEAD) &&
        sensor_mgr.is_filament_detected(helix::FilamentSensorRole::TOOLHEAD)) {
        // Filament appears to already be loaded - show warning
        spdlog::info("[{}] Toolhead sensor shows filament present - showing load warning",
                     get_name());
        show_load_warning();
        return;
    }

    // No sensor or no filament detected - proceed directly
    execute_load();
}

void FilamentPanel::handle_unload_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for filament unload ({}°C, min: {}°C)", nozzle_current_,
                       min_extrude_temp_);
        return;
    }

    // Check if toolhead sensor shows no filament (nothing to unload)
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::TOOLHEAD) &&
        !sensor_mgr.is_filament_detected(helix::FilamentSensorRole::TOOLHEAD)) {
        // No filament detected - show warning
        spdlog::info("[{}] Toolhead sensor shows no filament - showing unload warning", get_name());
        show_unload_warning();
        return;
    }

    // Sensor not available or filament detected - proceed directly
    execute_unload();
}

void FilamentPanel::handle_purge_button() {
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING("Nozzle too cold for purge ({}°C, min: {}°C)", nozzle_current_,
                       min_extrude_temp_);
        return;
    }

    if (operation_in_progress_) {
        NOTIFY_WARNING("Operation already in progress");
        return;
    }

    spdlog::info("[{}] Purging {}mm", get_name(), purge_amount_);

    if (!api_) {
        return;
    }

    // Try StandardMacros Purge slot first
    const auto& info = StandardMacros::instance().get(StandardMacroSlot::Purge);
    if (!info.is_empty()) {
        spdlog::info("[{}] Using StandardMacros purge: {}", get_name(), info.get_macro());
        NOTIFY_INFO("Purging...");

        // Stateless callbacks match load/unload pattern and avoid use-after-free [L012]
        StandardMacros::instance().execute(
            StandardMacroSlot::Purge, api_, []() { NOTIFY_SUCCESS("Purge complete"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Purge failed: {}", error.user_message());
            });
        return;
    }

    // Fallback: inline G-code (M83 = relative extrusion, G1 E{amount} F300)
    // Note: FilamentPanel is a global singleton, so `this` capture is safe [L012]
    set_operation_in_progress(true);
    spdlog::info("[{}] No purge macro configured, using inline G-code ({}mm)", get_name(),
                 purge_amount_);
    std::string gcode = fmt::format("M83\nG1 E{} F300", purge_amount_);
    NOTIFY_INFO("Purging {}mm...", purge_amount_);

    api_->execute_gcode(
        gcode,
        [this, amount = purge_amount_]() {
            ui_async_call(
                [](void* ud) {
                    auto* self = static_cast<FilamentPanel*>(ud);
                    self->set_operation_in_progress(false);
                },
                this);
            NOTIFY_SUCCESS("Purge complete ({}mm)", amount);
        },
        [this](const MoonrakerError& error) {
            ui_async_call(
                [](void* ud) {
                    auto* self = static_cast<FilamentPanel*>(ud);
                    self->set_operation_in_progress(false);
                },
                this);
            NOTIFY_ERROR("Purge failed: {}", error.user_message());
        });
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void FilamentPanel::on_manage_slots_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_manage_slots_clicked");
    LV_UNUSED(e);

    spdlog::info("[FilamentPanel] Opening AMS panel overlay");

    auto& ams_panel = get_global_ams_panel();
    if (!ams_panel.are_subjects_initialized()) {
        ams_panel.init_subjects();
    }
    lv_obj_t* panel_obj = ams_panel.get_panel();
    if (panel_obj) {
        ui_nav_push_overlay(panel_obj);
    }

    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_load_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_load_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_load_button();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_unload_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_unload_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_unload_button();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_purge_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_button();
    LVGL_SAFE_EVENT_CB_END();
}

// Material preset callbacks (XML event_cb - use global singleton)
void FilamentPanel::on_preset_pla_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_pla_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(0);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_petg_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_petg_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(1);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_abs_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_abs_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(2);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_preset_tpu_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_preset_tpu_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_preset_button(3);
    LVGL_SAFE_EVENT_CB_END();
}

// Temperature tap callbacks (XML event_cb - use global singleton)
void FilamentPanel::on_nozzle_temp_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_nozzle_temp_tap_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_nozzle_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_bed_temp_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_bed_temp_tap_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_bed_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::custom_nozzle_keypad_cb(float value, void* user_data) {
    auto* self = static_cast<FilamentPanel*>(user_data);
    if (self) {
        self->handle_custom_nozzle_confirmed(value);
    }
}

void FilamentPanel::custom_bed_keypad_cb(float value, void* user_data) {
    auto* self = static_cast<FilamentPanel*>(user_data);
    if (self) {
        self->handle_custom_bed_confirmed(value);
    }
}

void FilamentPanel::on_nozzle_target_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_nozzle_target_tap_clicked");
    LV_UNUSED(e);
    spdlog::info("[FilamentPanel] on_nozzle_target_tap_clicked TRIGGERED");
    get_global_filament_panel().handle_nozzle_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_bed_target_tap_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_bed_target_tap_clicked");
    LV_UNUSED(e);
    spdlog::info("[FilamentPanel] on_bed_target_tap_clicked TRIGGERED");
    get_global_filament_panel().handle_bed_temp_tap();
    LVGL_SAFE_EVENT_CB_END();
}

// Purge amount callbacks (XML event_cb - use global singleton)
void FilamentPanel::on_purge_5mm_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_5mm_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_amount_select(5);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_purge_10mm_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_10mm_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_amount_select(10);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_purge_25mm_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_purge_25mm_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_purge_amount_select(25);
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_cooldown_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_cooldown_clicked");
    LV_UNUSED(e);
    get_global_filament_panel().handle_cooldown();
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::handle_cooldown() {
    spdlog::info("[{}] Cooldown requested - turning off heaters", get_name());

    // Turn off nozzle heater
    if (api_) {
        api_->set_temperature(
            "extruder", 0.0, []() { NOTIFY_SUCCESS("Nozzle heater off"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to turn off nozzle: {}", error.user_message());
            });

        // Also turn off bed heater for full cooldown
        api_->set_temperature(
            "heater_bed", 0.0, []() { NOTIFY_SUCCESS("Bed heater off"); },
            [](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to turn off bed: {}", error.user_message());
            });
    }

    // Clear material selection since we're cooling down
    selected_material_ = -1;
    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
}

// ============================================================================
// PUBLIC API
// ============================================================================

void FilamentPanel::set_temp(int current, int target) {
    // Validate temperature ranges
    helix::ui::temperature::validate_and_clamp_pair(current, target, nozzle_min_temp_,
                                                    nozzle_max_temp_, "Filament");

    nozzle_current_ = current;
    nozzle_target_ = target;

    update_temp_display();
    update_status();
    update_warning_text();
    update_safety_state();
}

void FilamentPanel::get_temp(int* current, int* target) const {
    if (current)
        *current = nozzle_current_;
    if (target)
        *target = nozzle_target_;
}

void FilamentPanel::set_material(int material_id) {
    if (material_id < 0 || material_id >= PRESET_COUNT) {
        spdlog::error("[{}] Invalid material ID {} (valid: 0-{})", get_name(), material_id,
                      PRESET_COUNT - 1);
        return;
    }

    auto mat = filament::find_material(PRESET_MATERIAL_NAMES[material_id]);
    if (!mat) {
        spdlog::error("[{}] Material '{}' not found in database", get_name(),
                      PRESET_MATERIAL_NAMES[material_id]);
        return;
    }

    selected_material_ = material_id;
    nozzle_target_ = mat->nozzle_recommended();
    bed_target_ = mat->bed_temp;

    lv_subject_set_int(&material_selected_subject_, selected_material_);
    update_preset_buttons_visual();
    update_temp_display();
    update_material_temp_display();
    update_status();

    spdlog::info("[{}] Material set: {} (nozzle={}°C, bed={}°C)", get_name(),
                 PRESET_MATERIAL_NAMES[material_id], nozzle_target_, bed_target_);
}

bool FilamentPanel::is_extrusion_allowed() const {
    return helix::ui::temperature::is_extrusion_safe(nozzle_current_, min_extrude_temp_);
}

void FilamentPanel::set_limits(int min_temp, int max_temp, int min_extrude_temp) {
    nozzle_min_temp_ = min_temp;
    nozzle_max_temp_ = max_temp;

    // Update min_extrude_temp and safety warning text if changed
    if (min_extrude_temp_ != min_extrude_temp) {
        min_extrude_temp_ = min_extrude_temp;
        std::snprintf(safety_warning_text_buf_, sizeof(safety_warning_text_buf_),
                      SAFETY_WARNING_FMT, min_extrude_temp_);
        lv_subject_copy_string(&safety_warning_text_subject_, safety_warning_text_buf_);
        spdlog::info("[{}] Min extrusion temp updated: {}°C", get_name(), min_extrude_temp_);
    }

    spdlog::info("[{}] Nozzle temperature limits updated: {}-{}°C", get_name(), min_temp, max_temp);
}

// ============================================================================
// FILAMENT SENSOR WARNING HELPERS
// ============================================================================

void FilamentPanel::execute_load() {
    const auto& info = StandardMacros::instance().get(StandardMacroSlot::LoadFilament);
    if (info.is_empty()) {
        spdlog::warn("[{}] Load filament slot is empty", get_name());
        NOTIFY_WARNING("Load filament macro not configured");
        return;
    }

    spdlog::info("[{}] Loading filament via StandardMacros: {}", get_name(), info.get_macro());
    StandardMacros::instance().execute(
        StandardMacroSlot::LoadFilament, api_, []() { NOTIFY_SUCCESS("Loading filament..."); },
        [](const MoonrakerError& error) {
            NOTIFY_ERROR("Filament load failed: {}", error.user_message());
        });
}

void FilamentPanel::execute_unload() {
    const auto& info = StandardMacros::instance().get(StandardMacroSlot::UnloadFilament);
    if (info.is_empty()) {
        spdlog::warn("[{}] Unload filament slot is empty", get_name());
        NOTIFY_WARNING("Unload filament macro not configured");
        return;
    }

    spdlog::info("[{}] Unloading filament via StandardMacros: {}", get_name(), info.get_macro());
    StandardMacros::instance().execute(
        StandardMacroSlot::UnloadFilament, api_, []() { NOTIFY_SUCCESS("Unloading filament..."); },
        [](const MoonrakerError& error) {
            NOTIFY_ERROR("Filament unload failed: {}", error.user_message());
        });
}

void FilamentPanel::show_load_warning() {
    // Close any existing dialog first
    if (load_warning_dialog_) {
        ui_modal_hide(load_warning_dialog_);
        load_warning_dialog_ = nullptr;
    }

    load_warning_dialog_ = ui_modal_show_confirmation(
        "Filament Detected",
        "The toolhead sensor indicates filament is already loaded. "
        "Proceed with load anyway?",
        ModalSeverity::Warning, "Proceed", on_load_warning_proceed, on_load_warning_cancel, this);

    if (!load_warning_dialog_) {
        spdlog::error("[{}] Failed to create load warning dialog", get_name());
        return;
    }

    spdlog::debug("[{}] Load warning dialog shown", get_name());
}

void FilamentPanel::show_unload_warning() {
    // Close any existing dialog first
    if (unload_warning_dialog_) {
        ui_modal_hide(unload_warning_dialog_);
        unload_warning_dialog_ = nullptr;
    }

    unload_warning_dialog_ =
        ui_modal_show_confirmation("No Filament Detected",
                                   "The toolhead sensor indicates no filament is present. "
                                   "Proceed with unload anyway?",
                                   ModalSeverity::Warning, "Proceed", on_unload_warning_proceed,
                                   on_unload_warning_cancel, this);

    if (!unload_warning_dialog_) {
        spdlog::error("[{}] Failed to create unload warning dialog", get_name());
        return;
    }

    spdlog::debug("[{}] Unload warning dialog shown", get_name());
}

void FilamentPanel::on_load_warning_proceed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_load_warning_proceed");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->load_warning_dialog_) {
            ui_modal_hide(self->load_warning_dialog_);
            self->load_warning_dialog_ = nullptr;
        }
        // Execute load
        self->execute_load();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_load_warning_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_load_warning_cancel");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self && self->load_warning_dialog_) {
        ui_modal_hide(self->load_warning_dialog_);
        self->load_warning_dialog_ = nullptr;
        spdlog::debug("[FilamentPanel] Load cancelled by user");
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_unload_warning_proceed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_unload_warning_proceed");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->unload_warning_dialog_) {
            ui_modal_hide(self->unload_warning_dialog_);
            self->unload_warning_dialog_ = nullptr;
        }
        // Execute unload
        self->execute_unload();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentPanel::on_unload_warning_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentPanel] on_unload_warning_cancel");
    auto* self = static_cast<FilamentPanel*>(lv_event_get_user_data(e));
    if (self && self->unload_warning_dialog_) {
        ui_modal_hide(self->unload_warning_dialog_);
        self->unload_warning_dialog_ = nullptr;
        spdlog::debug("[FilamentPanel] Unload cancelled by user");
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<FilamentPanel> g_filament_panel;

FilamentPanel& get_global_filament_panel() {
    if (!g_filament_panel) {
        g_filament_panel = std::make_unique<FilamentPanel>(get_printer_state(), nullptr);
        StaticPanelRegistry::instance().register_destroy("FilamentPanel",
                                                         []() { g_filament_panel.reset(); });
    }
    return *g_filament_panel;
}
