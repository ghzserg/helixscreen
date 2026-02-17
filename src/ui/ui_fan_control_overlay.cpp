// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_fan_control_overlay.h"

#include "ui_error_reporting.h"
#include "ui_fan_arc_resize.h"
#include "ui_global_panel_helper.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_notification.h"

#include "app_globals.h"
#include "format_utils.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "observer_factory.h"

#include <spdlog/spdlog.h>

#include <cstdio>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

DEFINE_GLOBAL_OVERLAY_STORAGE(FanControlOverlay, g_fan_control_overlay, get_fan_control_overlay)

void init_fan_control_overlay(PrinterState& printer_state) {
    INIT_GLOBAL_OVERLAY(FanControlOverlay, g_fan_control_overlay, printer_state);
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

FanControlOverlay::FanControlOverlay(PrinterState& printer_state) : printer_state_(printer_state) {
    spdlog::trace("[{}] Constructor", get_name());
}

FanControlOverlay::~FanControlOverlay() {
    // LVGL may already be destroyed during static destruction
    if (!lv_is_initialized()) {
        spdlog::trace("[FanControlOverlay] Destroyed (LVGL already deinit)");
        return;
    }

    // Clear vectors to destroy FanDial instances before LVGL objects are deleted
    animated_fan_dials_.clear();
    auto_fan_cards_.clear();

    spdlog::trace("[FanControlOverlay] Destroyed");
}

// ============================================================================
// OVERLAYBASE IMPLEMENTATION
// ============================================================================

void FanControlOverlay::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // No local subjects needed - we use PrinterState's fans_version subject

    subjects_initialized_ = true;
    spdlog::trace("[{}] Subjects initialized", get_name());
}

lv_obj_t* FanControlOverlay::create(lv_obj_t* parent) {
    // Create overlay root from XML
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "fan_control_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Find container widget
    fans_container_ = lv_obj_find_by_name(overlay_root_, "fans_container");

    if (!fans_container_) {
        spdlog::error("[{}] Failed to find fans_container widget", get_name());
    }

    // Populate fans from current PrinterState
    populate_fans();

    spdlog::trace("[{}] Created overlay with {} animated dials and {} auto fans", get_name(),
                  animated_fan_dials_.size(), auto_fan_cards_.size());

    return overlay_root_;
}

void FanControlOverlay::register_callbacks() {
    // Back button is handled by overlay_panel base component
    spdlog::trace("[{}] Callbacks registered", get_name());
}

void FanControlOverlay::on_activate() {
    OverlayBase::on_activate();

    // Subscribe to fans_version subject for structural changes (fan discovery)
    // Using observer factory for type-safe lambda observer
    using helix::ui::observe_int_sync;
    if (auto* fans_ver = printer_state_.get_fans_version_subject()) {
        fans_observer_ = observe_int_sync<FanControlOverlay>(
            fans_ver, this, [](FanControlOverlay* self, int /* version */) {
                if (self->is_visible()) {
                    // Structural change - unsubscribe before rebuild to avoid dangling observers
                    self->unsubscribe_from_fan_speeds();
                    self->populate_fans();
                    self->subscribe_to_fan_speeds();
                }
            });
    }

    // Subscribe to per-fan speed subjects for reactive updates
    subscribe_to_fan_speeds();

    // Refresh fan speeds from current state
    update_fan_speeds();

    spdlog::trace("[{}] Activated", get_name());
}

void FanControlOverlay::on_deactivate() {
    OverlayBase::on_deactivate();

    // Unsubscribe from all observers
    fans_observer_.reset();
    unsubscribe_from_fan_speeds();

    spdlog::debug("[{}] Deactivated", get_name());
}

void FanControlOverlay::cleanup() {
    spdlog::debug("[{}] Cleanup", get_name());
    // Clear observers first (they reference this object)
    fans_observer_.reset();
    unsubscribe_from_fan_speeds();
    // Clear widget tracking vectors (widgets will be destroyed by OverlayBase::cleanup)
    animated_fan_dials_.clear();
    auto_fan_cards_.clear();
    OverlayBase::cleanup();
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void FanControlOverlay::populate_fans() {
    if (!fans_container_) {
        spdlog::warn("[{}] Cannot populate fans - container not found", get_name());
        return;
    }

    // Clear existing widgets
    lv_obj_clean(fans_container_);

    // Clear tracking vectors
    animated_fan_dials_.clear();
    auto_fan_cards_.clear();

    const auto& fans = printer_state_.get_fans();

    // First pass: create controllable fans (FanDial widgets with animation)
    for (const auto& fan : fans) {
        if (fan.is_controllable) {
            AnimatedFanDial afd;
            afd.object_name = fan.object_name;
            afd.dial = std::make_unique<FanDial>(fans_container_, fan.display_name, fan.object_name,
                                                 fan.speed_percent);

            // Set callback for user-initiated speed changes (dial interaction)
            afd.dial->set_on_speed_changed([this](const std::string& fan_id, int speed_percent) {
                send_fan_speed(fan_id, speed_percent);
            });

            animated_fan_dials_.push_back(std::move(afd));

            spdlog::trace("[{}] Created AnimatedFanDial for '{}' ({}%)", get_name(),
                          fan.display_name, fan.speed_percent);
        }
    }

    // Second pass: create auto-controlled fans (fan_status_card widgets)
    for (const auto& fan : fans) {
        if (!fan.is_controllable) {
            // Pass numeric value for arc, then format label with % suffix
            char speed_num_str[16];
            std::snprintf(speed_num_str, sizeof(speed_num_str), "%d", fan.speed_percent);

            const char* attrs[] = {"fan_name", fan.display_name.c_str(), "speed_percent",
                                   speed_num_str, nullptr};

            lv_obj_t* card =
                static_cast<lv_obj_t*>(lv_xml_create(fans_container_, "fan_status_card", attrs));

            if (card) {
                // Find speed label and format with % suffix
                lv_obj_t* speed_label = lv_obj_find_by_name(card, "speed_label");
                if (speed_label) {
                    char speed_str[16];
                    helix::format::format_percent(fan.speed_percent, speed_str, sizeof(speed_str));
                    lv_label_set_text(speed_label, speed_str);
                }

                // Find arc for live updates
                lv_obj_t* arc = lv_obj_find_by_name(card, "dial_arc");

                // Attach auto-resize for dynamic arc scaling
                helix::ui::fan_arc_attach_auto_resize(card);

                auto_fan_cards_.push_back({fan.object_name, card, speed_label, arc});

                spdlog::trace("[{}] Created fan_status_card for '{}' ({}%)", get_name(),
                              fan.display_name, fan.speed_percent);
            } else {
                spdlog::error("[{}] Failed to create fan_status_card for '{}'", get_name(),
                              fan.display_name);
            }
        }
    }

    spdlog::trace("[{}] Populated {} animated fan dials and {} auto fan cards", get_name(),
                  animated_fan_dials_.size(), auto_fan_cards_.size());
}

void FanControlOverlay::update_fan_speeds() {
    const auto& fans = printer_state_.get_fans();

    // Note: FanDial widgets are updated via AnimatedValue bindings in subscribe_to_fan_speeds()
    // This method only updates auto fan cards which don't need animation

    // Update auto fan card labels and arcs (immediate, no animation)
    for (auto& card_info : auto_fan_cards_) {
        for (const auto& fan : fans) {
            if (fan.object_name == card_info.object_name) {
                // Update speed label
                if (card_info.speed_label) {
                    char speed_str[16];
                    helix::format::format_percent(fan.speed_percent, speed_str, sizeof(speed_str));
                    lv_label_set_text(card_info.speed_label, speed_str);
                }
                // Update arc indicator
                if (card_info.arc) {
                    lv_arc_set_value(card_info.arc, fan.speed_percent);
                }
                break;
            }
        }
    }

    spdlog::trace("[{}] Updated auto fan card speeds", get_name());
}

void FanControlOverlay::send_fan_speed(const std::string& object_name, int speed_percent) {
    if (!api_) {
        spdlog::warn("[{}] Cannot send fan speed - no API connection", get_name());
        NOTIFY_WARNING("No printer connection");
        return;
    }

    spdlog::trace("[{}] Setting '{}' to {}%", get_name(), object_name, speed_percent);

    // Optimistic update: immediately reflect the new speed in PrinterState so
    // other UI (e.g. controls card secondary fan rows) updates without waiting
    // for the Moonraker round-trip confirmation.
    printer_state_.update_fan_speed(object_name, static_cast<double>(speed_percent) / 100.0);

    // MoonrakerAPI::set_fan_speed expects:
    // - "fan" for part cooling fan (uses M106)
    // - Fan name for generic fans (uses SET_FAN_SPEED)
    api_->set_fan_speed(
        object_name, static_cast<double>(speed_percent),
        []() {
            // Silent success
        },
        [object_name](const MoonrakerError& err) {
            NOTIFY_ERROR("Fan control failed: {}", err.user_message());
        });
}

// ============================================================================
// OBSERVER CALLBACK
// ============================================================================

void FanControlOverlay::on_fan_speed_changed(lv_observer_t* obs, lv_subject_t* /* subject */) {
    auto* self = static_cast<FanControlOverlay*>(lv_observer_get_user_data(obs));
    if (self && self->is_visible()) {
        self->update_fan_speeds();
    }
}

void FanControlOverlay::subscribe_to_fan_speeds() {
    // Bind AnimatedValue for each FanDial - provides smooth animation when speed changes
    for (auto& afd : animated_fan_dials_) {
        if (auto* subject = printer_state_.get_fan_speed_subject(afd.object_name)) {
            FanDial* dial_ptr = afd.dial.get();
            // 2% threshold to avoid micro-updates
            helix::ui::AnimatedValueConfig anim_config;
            anim_config.duration_ms = 300;
            anim_config.threshold = 2;
            afd.animation.bind(
                subject, [dial_ptr](int percent) { dial_ptr->set_speed(percent); }, anim_config);
            spdlog::trace("[{}] Bound AnimatedValue for '{}'", get_name(), afd.object_name);
        }
    }

    // Subscribe to auto fan subjects using traditional observers (immediate update, no animation)
    fan_speed_observers_.reserve(auto_fan_cards_.size());
    for (const auto& card : auto_fan_cards_) {
        if (auto* subject = printer_state_.get_fan_speed_subject(card.object_name)) {
            fan_speed_observers_.emplace_back(subject, on_fan_speed_changed, this);
            spdlog::trace("[{}] Subscribed to auto fan subject for '{}'", get_name(),
                          card.object_name);
        }
    }

    spdlog::trace("[{}] Bound {} animated fan dials, subscribed to {} auto fan subjects",
                  get_name(), animated_fan_dials_.size(), fan_speed_observers_.size());
}

void FanControlOverlay::unsubscribe_from_fan_speeds() {
    // Unbind AnimatedValue instances
    for (auto& afd : animated_fan_dials_) {
        afd.animation.unbind();
    }

    // Clear auto fan observers
    fan_speed_observers_.clear();

    spdlog::trace("[{}] Unsubscribed from fan speed subjects", get_name());
}
