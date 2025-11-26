// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_base.h"

#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PanelBase::PanelBase(PrinterState& printer_state, MoonrakerAPI* api)
    : printer_state_(printer_state), api_(api) {
    // Reserve reasonable capacity to avoid reallocations
    observers_.reserve(8);
}

PanelBase::~PanelBase() {
    cleanup_observers();
}

// ============================================================================
// MOVE SEMANTICS
// ============================================================================

PanelBase::PanelBase(PanelBase&& other) noexcept
    : printer_state_(other.printer_state_),
      api_(other.api_),
      panel_(other.panel_),
      parent_screen_(other.parent_screen_),
      subjects_initialized_(other.subjects_initialized_),
      observers_(std::move(other.observers_)) {

    // Clear source's state to prevent double-cleanup
    other.api_ = nullptr;
    other.panel_ = nullptr;
    other.parent_screen_ = nullptr;
    other.subjects_initialized_ = false;
    // other.observers_ is already moved (now empty)
}

PanelBase& PanelBase::operator=(PanelBase&& other) noexcept {
    if (this != &other) {
        // Clean up our observers first
        cleanup_observers();

        // Move state
        api_ = other.api_;
        panel_ = other.panel_;
        parent_screen_ = other.parent_screen_;
        subjects_initialized_ = other.subjects_initialized_;
        observers_ = std::move(other.observers_);

        // Clear source's state
        other.api_ = nullptr;
        other.panel_ = nullptr;
        other.parent_screen_ = nullptr;
        other.subjects_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// CORE LIFECYCLE
// ============================================================================

void PanelBase::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    panel_ = panel;
    parent_screen_ = parent_screen;

    if (!subjects_initialized_) {
        spdlog::warn("[{}] setup() called before init_subjects() - subjects may not bind",
                     get_name());
    }
}

// ============================================================================
// OBSERVER MANAGEMENT
// ============================================================================

void PanelBase::register_observer(lv_observer_t* observer) {
    if (observer) {
        observers_.push_back(observer);
    }
}

void PanelBase::cleanup_observers() {
    for (auto* observer : observers_) {
        if (observer) {
            lv_observer_remove(observer);
        }
    }
    observers_.clear();
}
