// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_telemetry_data.cpp
 * @brief Implementation of TelemetryDataOverlay
 */

#include "ui_settings_telemetry_data.h"

#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_toast_manager.h"

#include "lvgl/src/others/translation/lv_translation.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "system/telemetry_manager.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <memory>

namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<TelemetryDataOverlay> g_telemetry_data_overlay;

TelemetryDataOverlay& get_telemetry_data_overlay() {
    if (!g_telemetry_data_overlay) {
        g_telemetry_data_overlay = std::make_unique<TelemetryDataOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "TelemetryDataOverlay", []() { g_telemetry_data_overlay.reset(); });
    }
    return *g_telemetry_data_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

TelemetryDataOverlay::TelemetryDataOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

TelemetryDataOverlay::~TelemetryDataOverlay() {
    if (subjects_initialized_) {
        deinit_subjects_base(subjects_);
    }
    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void TelemetryDataOverlay::init_subjects() {
    init_subjects_guarded([this]() {
        // Status text subject: "Telemetry Enabled" / "Telemetry Disabled"
        UI_MANAGED_SUBJECT_STRING(status_subject_, status_buf_, "Telemetry",
                                  "telemetry_data_status", subjects_);

        // Detail text subject: "N events queued"
        UI_MANAGED_SUBJECT_STRING(detail_subject_, detail_buf_, "0 events queued",
                                  "telemetry_data_detail", subjects_);

        // Count subject for show/hide empty state vs event list
        UI_MANAGED_SUBJECT_INT(count_subject_, 0, "telemetry_data_count", subjects_);
    });
}

void TelemetryDataOverlay::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_telemetry_clear_queue", on_telemetry_clear_queue);

    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* TelemetryDataOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "telemetry_data_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Initially hidden until show() pushes it
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void TelemetryDataOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    // Ensure subjects and callbacks are initialized
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    // Lazy create overlay
    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Register for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack (on_activate will populate events)
    NavigationManager::instance().push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void TelemetryDataOverlay::on_activate() {
    OverlayBase::on_activate();

    update_status();
    populate_events();
}

void TelemetryDataOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void TelemetryDataOverlay::update_status() {
    if (!subjects_initialized_) {
        return;
    }

    auto& telemetry = TelemetryManager::instance();
    bool enabled = telemetry.is_enabled();
    size_t count = telemetry.queue_size();

    // Update status text
    const char* status_text = enabled ? "Telemetry Enabled" : "Telemetry Disabled";
    lv_subject_copy_string(&status_subject_, status_text);

    // Update detail text with event count
    if (count == 0) {
        lv_subject_copy_string(&detail_subject_, "No events queued");
    } else if (count == 1) {
        lv_subject_copy_string(&detail_subject_, "1 event queued");
    } else {
        snprintf(detail_buf_, sizeof(detail_buf_), "%zu events queued", count);
        lv_subject_copy_string(&detail_subject_, detail_buf_);
    }

    // Update count subject for show/hide logic
    lv_subject_set_int(&count_subject_, static_cast<int32_t>(count));

    spdlog::debug("[{}] Status updated: {} events, enabled={}", get_name(), count, enabled);
}

void TelemetryDataOverlay::populate_events() {
    if (!overlay_root_) {
        return;
    }

    lv_obj_t* event_list = lv_obj_find_by_name(overlay_root_, "event_list");
    if (!event_list) {
        spdlog::warn("[{}] Could not find event_list widget", get_name());
        return;
    }

    // Clear existing children
    lv_obj_clean(event_list);

    auto& telemetry = TelemetryManager::instance();
    auto snapshot = telemetry.get_queue_snapshot();

    if (!snapshot.is_array() || snapshot.empty()) {
        spdlog::debug("[{}] No events to display", get_name());
        return;
    }

    for (const auto& event : snapshot) {
        // Create a card for each event
        lv_obj_t* card = lv_obj_create(event_list);
        if (!card) {
            continue;
        }

        // Style the card
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, theme_manager_get_color("card_bg"), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_gap(card, 4, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // Helper: create a label that wraps within the card
        auto make_label = [&](lv_obj_t* parent, const std::string& text,
                              const char* color_token) -> lv_obj_t* {
            lv_obj_t* label = lv_label_create(parent);
            lv_label_set_text(label, text.c_str());
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(label, lv_pct(100));
            lv_obj_set_style_text_color(label, theme_manager_get_color(color_token), 0);
            lv_obj_set_style_text_font(label, lv_font_get_default(), 0);
            return label;
        };

        // Event type (heading) — JSON key is "event", not "type"
        std::string event_type = "Unknown Event";
        if (event.contains("event") && event["event"].is_string()) {
            event_type = event["event"].get<std::string>();
            if (event_type == "session") {
                event_type = "Session Start";
            } else if (event_type == "print_outcome") {
                event_type = "Print Outcome";
            } else if (event_type == "crash") {
                event_type = "Crash Report";
            }
        }

        make_label(card, event_type, "text");

        // Timestamp
        if (event.contains("timestamp") && event["timestamp"].is_string()) {
            make_label(card, event["timestamp"].get<std::string>(), "text_muted");
        }

        // Key fields based on event type
        std::string type_str;
        if (event.contains("event") && event["event"].is_string()) {
            type_str = event["event"].get<std::string>();
        }

        if (type_str == "session") {
            // Session fields are nested under "app"
            auto add_app_field = [&](const char* key, const char* display_name) {
                if (event.contains("app") && event["app"].contains(key) &&
                    event["app"][key].is_string()) {
                    std::string text =
                        std::string(display_name) + ": " + event["app"][key].get<std::string>();
                    make_label(card, text, "text_subtle");
                }
            };

            // App info - combine platform and display on one line
            add_app_field("version", "Version");
            if (event.contains("app")) {
                const auto& app = event["app"];
                std::string platform_display;
                if (app.contains("platform") && app["platform"].is_string()) {
                    platform_display = "Platform: " + app["platform"].get<std::string>();
                }
                if (app.contains("display") && app["display"].is_string()) {
                    if (!platform_display.empty())
                        platform_display += " | ";
                    platform_display += "Display: " + app["display"].get<std::string>();
                    if (app.contains("display_backend") && app["display_backend"].is_string()) {
                        platform_display += " (" + app["display_backend"].get<std::string>() + ")";
                    }
                }
                if (!platform_display.empty()) {
                    make_label(card, platform_display, "text_subtle");
                }

                // Theme, locale, and input type
                std::string settings_line;
                if (app.contains("theme") && app["theme"].is_string()) {
                    settings_line = "Theme: " + app["theme"].get<std::string>();
                }
                if (app.contains("locale") && app["locale"].is_string()) {
                    if (!settings_line.empty())
                        settings_line += " | ";
                    settings_line += "Locale: " + app["locale"].get<std::string>();
                }
                if (app.contains("input_type") && app["input_type"].is_string()) {
                    if (!settings_line.empty())
                        settings_line += " | ";
                    settings_line += "Input: " + app["input_type"].get<std::string>();
                }
                if (!settings_line.empty()) {
                    make_label(card, settings_line, "text_subtle");
                }
            }

            // Printer section
            if (event.contains("printer") && event["printer"].is_object()) {
                const auto& p = event["printer"];

                // "Printer: corexy, 350x350x300"
                std::string printer_line = "Printer:";
                if (p.contains("kinematics") && p["kinematics"].is_string()) {
                    printer_line += " " + p["kinematics"].get<std::string>();
                }
                if (p.contains("build_volume") && p["build_volume"].is_string()) {
                    printer_line += ", " + p["build_volume"].get<std::string>();
                }
                if (printer_line != "Printer:") {
                    make_label(card, printer_line, "text_subtle");
                }

                // "MCU: stm32f446 (x2) | 1 extruder"
                std::string mcu_line;
                if (p.contains("mcu") && p["mcu"].is_string()) {
                    mcu_line = "MCU: " + p["mcu"].get<std::string>();
                    if (p.contains("mcu_count") && p["mcu_count"].is_number_integer() &&
                        p["mcu_count"].get<int>() > 1) {
                        mcu_line += " (x" + std::to_string(p["mcu_count"].get<int>()) + ")";
                    }
                }
                if (p.contains("extruder_count") && p["extruder_count"].is_number_integer()) {
                    int ext = p["extruder_count"].get<int>();
                    if (!mcu_line.empty())
                        mcu_line += " | ";
                    mcu_line += std::to_string(ext) + " extruder" + (ext != 1 ? "s" : "");
                }
                if (!mcu_line.empty()) {
                    make_label(card, mcu_line, "text_subtle");
                }

                // Klipper and Moonraker versions
                if (p.contains("klipper_version") && p["klipper_version"].is_string()) {
                    make_label(card, "Klipper: " + p["klipper_version"].get<std::string>(),
                               "text_subtle");
                }
                if (p.contains("moonraker_version") && p["moonraker_version"].is_string()) {
                    make_label(card, "Moonraker: " + p["moonraker_version"].get<std::string>(),
                               "text_subtle");
                }
            }

            // Features array
            if (event.contains("features") && event["features"].is_array() &&
                !event["features"].empty()) {
                std::string features_str = "Features: ";
                bool first = true;
                for (const auto& f : event["features"]) {
                    if (f.is_string()) {
                        if (!first)
                            features_str += ", ";
                        features_str += f.get<std::string>();
                        first = false;
                    }
                }
                make_label(card, features_str, "text_subtle");
            }

            // Host info
            if (event.contains("host") && event["host"].is_object()) {
                const auto& h = event["host"];

                // "Host: aarch64, 4 cores, 1024 MB RAM"
                std::string host_line;
                if (h.contains("arch") && h["arch"].is_string()) {
                    host_line = h["arch"].get<std::string>();
                }
                if (h.contains("cpu_cores") && h["cpu_cores"].is_number_integer()) {
                    if (!host_line.empty())
                        host_line += ", ";
                    host_line += std::to_string(h["cpu_cores"].get<int>()) + " cores";
                }
                if (h.contains("ram_total_mb") && h["ram_total_mb"].is_number_integer()) {
                    if (!host_line.empty())
                        host_line += ", ";
                    host_line += std::to_string(h["ram_total_mb"].get<int>()) + " MB RAM";
                }
                if (!host_line.empty()) {
                    make_label(card, "Host: " + host_line, "text_subtle");
                }

                if (h.contains("cpu_model") && h["cpu_model"].is_string()) {
                    make_label(card, "CPU: " + h["cpu_model"].get<std::string>(), "text_subtle");
                }

                if (h.contains("os") && h["os"].is_string()) {
                    make_label(card, "OS: " + h["os"].get<std::string>(), "text_subtle");
                }
            }
        } else if (type_str == "print_outcome") {
            // Print outcome fields are at top level
            auto add_field_str = [&](const char* key, const char* display_name) {
                if (event.contains(key) && event[key].is_string()) {
                    make_label(card,
                               std::string(display_name) + ": " + event[key].get<std::string>(),
                               "text_subtle");
                }
            };
            auto add_field_num = [&](const char* key, const char* display_name,
                                     const char* suffix) {
                if (event.contains(key) && event[key].is_number()) {
                    char buf[64];
                    if (event[key].is_number_integer()) {
                        snprintf(buf, sizeof(buf), "%s: %d%s", display_name, event[key].get<int>(),
                                 suffix);
                    } else {
                        snprintf(buf, sizeof(buf), "%s: %.1f%s", display_name,
                                 event[key].get<double>(), suffix);
                    }
                    make_label(card, buf, "text_subtle");
                }
            };
            add_field_str("outcome", "Outcome");
            add_field_num("duration_sec", "Duration", "s");
            add_field_str("filament_type", "Filament");
            add_field_num("nozzle_temp", "Nozzle",
                          "\xC2\xB0"
                          "C");
            add_field_num("bed_temp", "Bed",
                          "\xC2\xB0"
                          "C");
        }

        // Show the full hashed device ID (no truncation)
        if (event.contains("device_id") && event["device_id"].is_string()) {
            std::string text = "Device: " + event["device_id"].get<std::string>();
            make_label(card, text, "text_subtle");
        }
    }

    spdlog::debug("[{}] Populated {} event cards", get_name(), snapshot.size());
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void TelemetryDataOverlay::handle_clear_queue() {
    auto& telemetry = TelemetryManager::instance();
    telemetry.clear_queue();

    ToastManager::instance().show(ToastSeverity::SUCCESS, lv_tr("Telemetry queue cleared"), 2000);
    spdlog::info("[{}] Queue cleared by user", get_name());

    // Refresh display
    update_status();
    populate_events();
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void TelemetryDataOverlay::on_telemetry_clear_queue(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[TelemetryDataOverlay] on_telemetry_clear_queue");
    get_telemetry_data_overlay().handle_clear_queue();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings
