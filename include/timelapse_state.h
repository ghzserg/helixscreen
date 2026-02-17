// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Manages timelapse recording state and render progress
 *
 * Tracks frame captures during printing and render progress from
 * the Moonraker-Timelapse plugin. Provides subjects for reactive
 * UI updates (frame count, render progress, render status).
 *
 * Events arrive via WebSocket notify_timelapse_event and are
 * dispatched through handle_timelapse_event().
 *
 * @note Thread-safe: handle_timelapse_event() uses helix::ui::queue_update()
 *       for subject updates from WebSocket callbacks.
 */
class TimelapseState {
  public:
    static TimelapseState& instance();

    /**
     * @brief Initialize subjects for XML binding
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    /**
     * @brief Handle a timelapse event from Moonraker
     *
     * Dispatches based on event["action"]:
     * - "newframe": Increments frame count
     * - "render": Updates render progress/status, triggers notifications
     *
     * Thread-safe: Uses helix::ui::queue_update() for subject updates.
     *
     * @param event JSON event from notify_timelapse_event
     */
    void handle_timelapse_event(const nlohmann::json& event);

    /**
     * @brief Reset all state (on disconnect or new print)
     *
     * Thread-safe: Uses helix::ui::queue_update() for subject updates.
     */
    void reset();

    /// Render progress as 0-100 percent
    lv_subject_t* get_render_progress_subject() {
        return &timelapse_render_progress_;
    }

    /// Render status: "idle", "rendering", "complete", "error"
    lv_subject_t* get_render_status_subject() {
        return &timelapse_render_status_;
    }

    /// Frame count captured this print
    lv_subject_t* get_frame_count_subject() {
        return &timelapse_frame_count_;
    }

  private:
    TimelapseState() = default;

    // Allow tests to call handle_timelapse_event directly
    friend class TimelapseStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Subjects
    lv_subject_t timelapse_render_progress_{};
    lv_subject_t timelapse_render_status_{};
    lv_subject_t timelapse_frame_count_{};

    // String buffer for render status
    char timelapse_render_status_buf_[32]{};

    // Notification throttling: last progress value that triggered a notification
    int last_notified_progress_ = -1;
};

} // namespace helix
