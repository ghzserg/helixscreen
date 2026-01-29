// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_fan_dial.h"

#include "ui_utils.h"

#include "format_utils.h"
#include "lvgl/src/xml/lv_xml.h"
#include "ui/ui_event_trampoline.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <utility>

// ============================================================================
// FanDial Implementation
// ============================================================================

FanDial::FanDial(lv_obj_t* parent, const std::string& name, const std::string& fan_id,
                 int initial_speed)
    : name_(name), fan_id_(fan_id), current_speed_(initial_speed) {
    // Build attributes array for XML creation
    char initial_value_str[16];
    snprintf(initial_value_str, sizeof(initial_value_str), "%d", initial_speed);

    const char* attrs[] = {"fan_name",      name.c_str(),      "fan_id", fan_id.c_str(),
                           "initial_value", initial_value_str, nullptr};

    // Create widget from XML component
    root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "fan_dial", attrs));
    if (!root_) {
        spdlog::error("[FanDial] Failed to create fan_dial component for '{}'", name);
        return;
    }

    // Find child widgets by name
    arc_ = lv_obj_find_by_name(root_, "dial_arc");
    speed_label_ = lv_obj_find_by_name(root_, "speed_label");
    btn_off_ = lv_obj_find_by_name(root_, "btn_off");
    btn_on_ = lv_obj_find_by_name(root_, "btn_on");

    if (!arc_ || !speed_label_ || !btn_off_ || !btn_on_) {
        spdlog::error("[FanDial] Failed to find child widgets for '{}': arc={} label={} "
                      "off={} on={}",
                      name, arc_ != nullptr, speed_label_ != nullptr, btn_off_ != nullptr,
                      btn_on_ != nullptr);
        return;
    }

    // Add event callbacks with this pointer as user data
    lv_obj_add_event_cb(arc_, on_arc_value_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(btn_off_, on_off_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_on_, on_on_clicked, LV_EVENT_CLICKED, this);

    // Set initial speed display
    update_speed_label(initial_speed);

    spdlog::debug("[FanDial] Created '{}' (id={}) with initial speed {}%", name, fan_id,
                  initial_speed);
}

FanDial::~FanDial() {
    // LVGL will clean up child widgets when root is deleted
    // No need to manually remove event callbacks - they are cleaned up with the widget
    spdlog::trace("[FanDial] Destroyed '{}'", name_);
}

FanDial::FanDial(FanDial&& other) noexcept
    : root_(other.root_), arc_(other.arc_), speed_label_(other.speed_label_),
      btn_off_(other.btn_off_), btn_on_(other.btn_on_), name_(std::move(other.name_)),
      fan_id_(std::move(other.fan_id_)), current_speed_(other.current_speed_),
      on_speed_changed_(std::move(other.on_speed_changed_)), syncing_(other.syncing_) {
    // Clear the source pointers
    other.root_ = nullptr;
    other.arc_ = nullptr;
    other.speed_label_ = nullptr;
    other.btn_off_ = nullptr;
    other.btn_on_ = nullptr;

    // Update event callback user_data to point to this instance
    if (arc_) {
        // Remove old callbacks and add new ones with updated user_data
        lv_obj_remove_event_cb(arc_, on_arc_value_changed);
        lv_obj_add_event_cb(arc_, on_arc_value_changed, LV_EVENT_VALUE_CHANGED, this);
    }
    if (btn_off_) {
        lv_obj_remove_event_cb(btn_off_, on_off_clicked);
        lv_obj_add_event_cb(btn_off_, on_off_clicked, LV_EVENT_CLICKED, this);
    }
    if (btn_on_) {
        lv_obj_remove_event_cb(btn_on_, on_on_clicked);
        lv_obj_add_event_cb(btn_on_, on_on_clicked, LV_EVENT_CLICKED, this);
    }
}

FanDial& FanDial::operator=(FanDial&& other) noexcept {
    if (this != &other) {
        // Clean up current resources (child widgets are destroyed with root_)
        lv_obj_safe_delete(root_);

        // Move resources
        root_ = other.root_;
        arc_ = other.arc_;
        speed_label_ = other.speed_label_;
        btn_off_ = other.btn_off_;
        btn_on_ = other.btn_on_;
        name_ = std::move(other.name_);
        fan_id_ = std::move(other.fan_id_);
        current_speed_ = other.current_speed_;
        on_speed_changed_ = std::move(other.on_speed_changed_);
        syncing_ = other.syncing_;

        // Clear source pointers
        other.root_ = nullptr;
        other.arc_ = nullptr;
        other.speed_label_ = nullptr;
        other.btn_off_ = nullptr;
        other.btn_on_ = nullptr;

        // Update event callback user_data
        if (arc_) {
            lv_obj_remove_event_cb(arc_, on_arc_value_changed);
            lv_obj_add_event_cb(arc_, on_arc_value_changed, LV_EVENT_VALUE_CHANGED, this);
        }
        if (btn_off_) {
            lv_obj_remove_event_cb(btn_off_, on_off_clicked);
            lv_obj_add_event_cb(btn_off_, on_off_clicked, LV_EVENT_CLICKED, this);
        }
        if (btn_on_) {
            lv_obj_remove_event_cb(btn_on_, on_on_clicked);
            lv_obj_add_event_cb(btn_on_, on_on_clicked, LV_EVENT_CLICKED, this);
        }
    }
    return *this;
}

void FanDial::set_speed(int percent) {
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    current_speed_ = percent;

    // Set syncing flag to prevent callback loop
    syncing_ = true;

    // Update arc value
    if (arc_) {
        lv_arc_set_value(arc_, percent);
    }

    // Update label
    update_speed_label(percent);

    syncing_ = false;

    spdlog::trace("[FanDial] '{}' set_speed({}%)", name_, percent);
}

int FanDial::get_speed() const {
    return current_speed_;
}

void FanDial::set_on_speed_changed(SpeedCallback callback) {
    on_speed_changed_ = std::move(callback);
}

void FanDial::update_speed_label(int percent) {
    if (!speed_label_)
        return;

    if (percent == 0) {
        lv_label_set_text(speed_label_, "Off");
    } else {
        char buf[8];
        helix::fmt::format_percent(percent, buf, sizeof(buf));
        lv_label_set_text(speed_label_, buf);
    }
}

void FanDial::handle_arc_changed() {
    if (syncing_)
        return;

    if (!arc_)
        return;

    int value = lv_arc_get_value(arc_);
    current_speed_ = value;
    update_speed_label(value);

    if (on_speed_changed_) {
        on_speed_changed_(fan_id_, value);
    }

    spdlog::trace("[FanDial] '{}' arc changed to {}%", name_, value);
}

void FanDial::handle_off_clicked() {
    set_speed(0);

    if (on_speed_changed_) {
        on_speed_changed_(fan_id_, 0);
    }

    spdlog::debug("[FanDial] '{}' Off button clicked", name_);
}

void FanDial::handle_on_clicked() {
    set_speed(100);

    if (on_speed_changed_) {
        on_speed_changed_(fan_id_, 100);
    }

    spdlog::debug("[FanDial] '{}' On button clicked", name_);
}

// ============================================================================
// Static Event Trampolines
// ============================================================================

DEFINE_EVENT_TRAMPOLINE_SIMPLE(FanDial, on_arc_value_changed, handle_arc_changed)
DEFINE_EVENT_TRAMPOLINE_SIMPLE(FanDial, on_off_clicked, handle_off_clicked)
DEFINE_EVENT_TRAMPOLINE_SIMPLE(FanDial, on_on_clicked, handle_on_clicked)

// ============================================================================
// XML Callback Registration
// ============================================================================

// These are no-op placeholders for XML event callbacks
// The actual event handling is done via lv_obj_add_event_cb in the constructor
// with user_data pointing to the FanDial instance

static void xml_fan_dial_value_changed(lv_event_t* /*e*/) {
    // No-op: actual handling is via C++ event callbacks with user_data
}

static void xml_fan_dial_off_clicked(lv_event_t* /*e*/) {
    // No-op: actual handling is via C++ event callbacks with user_data
}

static void xml_fan_dial_on_clicked(lv_event_t* /*e*/) {
    // No-op: actual handling is via C++ event callbacks with user_data
}

void register_fan_dial_callbacks() {
    // Register XML callbacks (required for XML parsing, but we use C++ callbacks)
    lv_xml_register_event_cb(nullptr, "on_fan_dial_value_changed", xml_fan_dial_value_changed);
    lv_xml_register_event_cb(nullptr, "on_fan_dial_off_clicked", xml_fan_dial_off_clicked);
    lv_xml_register_event_cb(nullptr, "on_fan_dial_on_clicked", xml_fan_dial_on_clicked);

    spdlog::debug("[FanDial] Registered XML event callbacks");
}
