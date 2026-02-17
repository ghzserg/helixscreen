// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_dryer_card.h"

#include "ui_error_reporting.h"
#include "ui_modal.h"

#include "ams_state.h"
#include "filament_database.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>

namespace helix::ui {

// Constants
static constexpr int DEFAULT_FAN_SPEED_PCT = 50;

// Static member initialization
bool AmsDryerCard::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsDryerCard::AmsDryerCard() {
    spdlog::debug("[AmsDryerCard] Constructed");
}

AmsDryerCard::~AmsDryerCard() {
    cleanup();
    spdlog::trace("[AmsDryerCard] Destroyed");
}

AmsDryerCard::AmsDryerCard(AmsDryerCard&& other) noexcept
    : dryer_card_(other.dryer_card_), dryer_modal_(other.dryer_modal_),
      progress_fill_(other.progress_fill_), progress_observer_(std::move(other.progress_observer_)),
      cached_presets_(std::move(other.cached_presets_)) {
    // Update modal's user_data so the LV_EVENT_DELETE callback points to new owner
    if (dryer_modal_) {
        lv_obj_set_user_data(dryer_modal_, this);
    }
    other.dryer_card_ = nullptr;
    other.dryer_modal_ = nullptr;
    other.progress_fill_ = nullptr;
}

AmsDryerCard& AmsDryerCard::operator=(AmsDryerCard&& other) noexcept {
    if (this != &other) {
        cleanup();

        dryer_card_ = other.dryer_card_;
        dryer_modal_ = other.dryer_modal_;
        progress_fill_ = other.progress_fill_;
        progress_observer_ = std::move(other.progress_observer_);
        cached_presets_ = std::move(other.cached_presets_);

        // Update modal's user_data so the LV_EVENT_DELETE callback points to new owner
        if (dryer_modal_) {
            lv_obj_set_user_data(dryer_modal_, this);
        }

        other.dryer_card_ = nullptr;
        other.dryer_modal_ = nullptr;
        other.progress_fill_ = nullptr;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

bool AmsDryerCard::setup(lv_obj_t* panel) {
    if (!panel) {
        return false;
    }

    // Register callbacks once (idempotent)
    register_callbacks();

    // Find dryer card in panel
    dryer_card_ = lv_obj_find_by_name(panel, "dryer_card");
    if (!dryer_card_) {
        spdlog::debug("[AmsDryerCard] dryer_card not found - dryer UI disabled");
        return false;
    }

    // Store 'this' in dryer_card's user_data for callback traversal
    lv_obj_set_user_data(dryer_card_, this);

    // Find progress bar fill element
    progress_fill_ = lv_obj_find_by_name(dryer_card_, "progress_fill");
    if (progress_fill_) {
        // Set up observer to update width when progress changes
        progress_observer_ = ObserverGuard(
            AmsState::instance().get_dryer_progress_pct_subject(),
            [](lv_observer_t* observer, lv_subject_t* subject) {
                auto* self = static_cast<AmsDryerCard*>(lv_observer_get_user_data(observer));
                if (self && self->progress_fill_) {
                    int progress = lv_subject_get_int(subject);
                    lv_obj_set_width(self->progress_fill_,
                                     lv_pct(std::max(0, std::min(100, progress))));
                }
            },
            this);

        spdlog::debug("[AmsDryerCard] Progress bar observer set up");
    }

    // Modal is created on-demand via helix::ui::modal_show() in on_open_modal_cb
    // Initial sync of dryer state
    AmsState::instance().sync_dryer_from_backend();
    spdlog::debug("[AmsDryerCard] Setup complete");

    return true;
}

void AmsDryerCard::cleanup() {
    // Remove observer first
    progress_observer_.reset();

    // Hide modal if visible (Modal system handles deletion via exit animation).
    // Clear dryer_modal_ unconditionally — even if modal_hide() returns early because
    // the modal is already exiting, we must not hold a pointer that will be freed
    // when the exit animation completes.
    if (dryer_modal_ && lv_is_initialized()) {
        // Clear user_data first so the LV_EVENT_DELETE callback won't try to write
        // to our (possibly destroyed) member
        lv_obj_set_user_data(dryer_modal_, nullptr);
        helix::ui::modal_hide(dryer_modal_);
    }
    dryer_modal_ = nullptr;

    // Clear widget references (dryer_card_ is owned by panel)
    dryer_card_ = nullptr;
    progress_fill_ = nullptr;
    spdlog::debug("[AmsDryerCard] cleanup()");
}

// ============================================================================
// Actions
// ============================================================================

void AmsDryerCard::start_drying(float temp_c, int duration_min, int fan_pct) {
    spdlog::info("[AmsDryerCard] Starting dryer: {}°C for {}min, fan {}%", temp_c, duration_min,
                 fan_pct);

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    DryerInfo dryer = backend->get_dryer_info();
    if (!dryer.supported) {
        NOTIFY_WARNING("Dryer not available");
        return;
    }

    AmsError error = backend->start_drying(temp_c, duration_min, fan_pct);
    if (error.result == AmsResult::SUCCESS) {
        NOTIFY_INFO("Drying started: {}°C", static_cast<int>(temp_c));
        AmsState::instance().sync_dryer_from_backend();
        // Close the presets modal
        if (dryer_modal_) {
            helix::ui::modal_hide(dryer_modal_);
            dryer_modal_ = nullptr;
        }
    } else {
        NOTIFY_ERROR("Failed to start drying: {}", error.user_msg);
    }
}

void AmsDryerCard::stop_drying() {
    spdlog::info("[AmsDryerCard] Stopping dryer");

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    AmsError error = backend->stop_drying();
    if (error.result == AmsResult::SUCCESS) {
        NOTIFY_INFO("Drying stopped");
        AmsState::instance().sync_dryer_from_backend();
    } else {
        NOTIFY_ERROR("Failed to stop drying: {}", error.user_msg);
    }
}

void AmsDryerCard::apply_preset(int temp_c, int duration_min) {
    // Update modal values via AmsState (reactive binding updates UI)
    AmsState::instance().set_modal_preset(temp_c, duration_min);

    // If dryer is already running, apply new settings immediately
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend && backend->get_dryer_info().active) {
        start_drying(static_cast<float>(temp_c), duration_min, DEFAULT_FAN_SPEED_PCT);
    }
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsDryerCard::register_callbacks_static() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "dryer_open_modal_cb", on_open_modal_cb);
    lv_xml_register_event_cb(nullptr, "dryer_modal_close_cb", on_close_modal_cb);
    lv_xml_register_event_cb(nullptr, "dryer_preset_changed_cb", on_preset_changed_cb);
    lv_xml_register_event_cb(nullptr, "dryer_stop_clicked_cb", on_stop_cb);
    lv_xml_register_event_cb(nullptr, "dryer_temp_minus_cb", on_temp_minus_cb);
    lv_xml_register_event_cb(nullptr, "dryer_temp_plus_cb", on_temp_plus_cb);
    lv_xml_register_event_cb(nullptr, "dryer_duration_minus_cb", on_duration_minus_cb);
    lv_xml_register_event_cb(nullptr, "dryer_duration_plus_cb", on_duration_plus_cb);
    lv_xml_register_event_cb(nullptr, "dryer_power_toggled_cb", on_power_toggled_cb);

    callbacks_registered_ = true;
    spdlog::debug("[AmsDryerCard] Static callbacks registered");
}

void AmsDryerCard::register_callbacks() {
    // Delegate to static method for backward compatibility
    register_callbacks_static();
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

AmsDryerCard* AmsDryerCard::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Traverse parent chain to find dryer_card or dryer_modal with user_data
    lv_obj_t* obj = target;
    while (obj) {
        void* user_data = lv_obj_get_user_data(obj);
        if (user_data) {
            return static_cast<AmsDryerCard*>(user_data);
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[AmsDryerCard] Could not find instance from event target");
    return nullptr;
}

void AmsDryerCard::on_open_modal_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self) {
        return;
    }

    spdlog::debug("[AmsDryerCard] Opening dryer modal");

    // Show modal via Modal system (creates backdrop programmatically)
    self->dryer_modal_ = helix::ui::modal_show("dryer_presets_modal");

    if (self->dryer_modal_) {
        // Store 'this' in modal's user_data for callback traversal
        lv_obj_set_user_data(self->dryer_modal_, self);

        // Auto-clear dryer_modal_ if the modal is destroyed externally (e.g., by the
        // modal system's exit animation deleting the backdrop+dialog). Without this,
        // dryer_modal_ becomes a dangling pointer and any subsequent access crashes
        // with LV_ASSERT_OBJ (SIGABRT). See GitHub issue #97.
        // Uses the modal's own user_data (set above) so move operations only need to
        // update user_data, not re-register the callback.
        lv_obj_add_event_cb(
            self->dryer_modal_,
            [](lv_event_t* e) {
                auto* card =
                    static_cast<AmsDryerCard*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
                if (card) {
                    spdlog::debug("[AmsDryerCard] Modal deleted externally, clearing pointer");
                    card->dryer_modal_ = nullptr;
                }
            },
            LV_EVENT_DELETE, nullptr);

        // Populate the preset dropdown with data from filament database
        self->populate_preset_dropdown();
    }
}

void AmsDryerCard::on_close_modal_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self) {
        return;
    }

    spdlog::debug("[AmsDryerCard] Closing dryer modal");

    if (self->dryer_modal_) {
        helix::ui::modal_hide(self->dryer_modal_);
        self->dryer_modal_ = nullptr;
    }
}

void AmsDryerCard::on_preset_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self) {
        return;
    }

    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    uint32_t selected = lv_dropdown_get_selected(dropdown);

    // Look up preset from cached data
    if (selected < self->cached_presets_.size()) {
        const auto& preset = self->cached_presets_[selected];
        spdlog::debug("[AmsDryerCard] Preset selected: {} ({}°C, {}min)", preset.name,
                      preset.temp_c, preset.time_min);
        self->apply_preset(preset.temp_c, preset.time_min);
    }
}

void AmsDryerCard::on_stop_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->stop_drying();
    }
}

void AmsDryerCard::on_temp_minus_cb(lv_event_t* e) {
    LV_UNUSED(e);
    AmsState::instance().adjust_modal_temp(-5);
}

void AmsDryerCard::on_temp_plus_cb(lv_event_t* e) {
    LV_UNUSED(e);
    AmsState::instance().adjust_modal_temp(+5);
}

void AmsDryerCard::on_duration_minus_cb(lv_event_t* e) {
    LV_UNUSED(e);
    AmsState::instance().adjust_modal_duration(-30);
}

void AmsDryerCard::on_duration_plus_cb(lv_event_t* e) {
    LV_UNUSED(e);
    AmsState::instance().adjust_modal_duration(+30);
}

void AmsDryerCard::on_power_toggled_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    DryerInfo dryer = backend->get_dryer_info();
    if (dryer.active) {
        // Currently on - stop it
        self->stop_drying();
    } else {
        // Currently off - start with modal settings
        int temp = AmsState::instance().get_modal_target_temp();
        int duration = AmsState::instance().get_modal_duration_min();
        self->start_drying(static_cast<float>(temp), duration, DEFAULT_FAN_SPEED_PCT);
    }
}

// ============================================================================
// Dropdown Population
// ============================================================================

void AmsDryerCard::populate_preset_dropdown() {
    if (!dryer_modal_) {
        return;
    }

    lv_obj_t* dropdown = lv_obj_find_by_name(dryer_modal_, "preset_dropdown");
    if (!dropdown) {
        spdlog::warn("[AmsDryerCard] preset_dropdown not found in modal");
        return;
    }

    // Get presets from filament database
    cached_presets_ = filament::get_drying_presets_by_group();

    if (cached_presets_.empty()) {
        spdlog::warn("[AmsDryerCard] No drying presets available");
        lv_dropdown_set_options(dropdown, lv_tr("No presets"));
        return;
    }

    // Build options string: "PLA (45°C, 4h)\nPETG (55°C, 6h)\n..."
    std::ostringstream options;
    for (size_t i = 0; i < cached_presets_.size(); ++i) {
        const auto& preset = cached_presets_[i];
        int hours = preset.time_min / 60;
        int mins = preset.time_min % 60;

        options << preset.name << " (" << preset.temp_c << "°C, ";
        if (hours > 0) {
            options << hours << "h";
            if (mins > 0) {
                options << mins << "m";
            }
        } else {
            options << mins << "m";
        }
        options << ")";

        if (i < cached_presets_.size() - 1) {
            options << "\n";
        }
    }

    lv_dropdown_set_options(dropdown, options.str().c_str());
    spdlog::debug("[AmsDryerCard] Populated preset dropdown with {} presets",
                  cached_presets_.size());
}

} // namespace helix::ui
