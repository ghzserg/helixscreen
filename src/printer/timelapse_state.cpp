// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timelapse_state.h"

#include "ui_error_reporting.h"
#include "ui_toast_manager.h"
#include "ui_update_queue.h"

#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace helix {

TimelapseState& TimelapseState::instance() {
    static TimelapseState instance;
    return instance;
}

void TimelapseState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[TimelapseState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[TimelapseState] Initializing subjects (register_xml={})", register_xml);

    std::memset(timelapse_render_status_buf_, 0, sizeof(timelapse_render_status_buf_));
    std::strcpy(timelapse_render_status_buf_, "idle");

    INIT_SUBJECT_INT(timelapse_render_progress, 0, subjects_, register_xml);
    INIT_SUBJECT_STRING(timelapse_render_status, "idle", subjects_, register_xml);
    INIT_SUBJECT_INT(timelapse_frame_count, 0, subjects_, register_xml);

    subjects_initialized_ = true;
}

void TimelapseState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[TimelapseState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    last_notified_progress_ = -1;
}

void TimelapseState::handle_timelapse_event(const nlohmann::json& event) {
    if (!subjects_initialized_) {
        spdlog::warn("[TimelapseState] Event received before subjects initialized");
        return;
    }

    // Safely extract action
    auto action_it = event.find("action");
    if (action_it == event.end() || !action_it->is_string()) {
        spdlog::debug("[TimelapseState] Event missing or invalid 'action' field");
        return;
    }

    const std::string& action = action_it->get_ref<const std::string&>();

    if (action == "newframe") {
        // Increment frame count — read+write both inside ui_queue_update
        // since lv_subject_get_int must be called from the UI thread
        ui_queue_update([this]() {
            int current = lv_subject_get_int(&timelapse_frame_count_);
            lv_subject_set_int(&timelapse_frame_count_, current + 1);
        });

        spdlog::debug("[TimelapseState] New frame captured");

    } else if (action == "render") {
        // Extract render status and progress
        std::string status;
        int progress = 0;
        std::string error_msg;
        std::string filename;

        if (event.contains("status") && event["status"].is_string()) {
            status = event["status"].get<std::string>();
        }
        if (event.contains("progress") && event["progress"].is_number()) {
            progress = event["progress"].get<int>();
        }
        if (event.contains("msg") && event["msg"].is_string()) {
            error_msg = event["msg"].get<std::string>();
        }
        if (event.contains("filename") && event["filename"].is_string()) {
            filename = event["filename"].get<std::string>();
        }

        if (status == "running") {
            ui_queue_update([this, progress]() {
                lv_subject_set_int(&timelapse_render_progress_, progress);
                lv_subject_copy_string(&timelapse_render_status_, "rendering");
            });

            // Throttled notifications at 25% boundaries
            int boundary = (progress / 25) * 25;
            if (boundary > 0 && boundary != last_notified_progress_) {
                last_notified_progress_ = boundary;
                NOTIFY_INFO("Rendering timelapse... {}%", progress);
            }

            spdlog::debug("[TimelapseState] Render progress: {}%", progress);

        } else if (status == "success") {
            ui_queue_update([this]() {
                lv_subject_set_int(&timelapse_render_progress_, 0);
                lv_subject_copy_string(&timelapse_render_status_, "complete");
            });

            last_notified_progress_ = -1;
            ui_toast_show(ToastSeverity::SUCCESS, "Timelapse rendered successfully", 5000);

            spdlog::info("[TimelapseState] Render complete: {}", filename);

        } else if (status == "error") {
            ui_queue_update(
                [this]() { lv_subject_copy_string(&timelapse_render_status_, "error"); });

            last_notified_progress_ = -1;
            if (!error_msg.empty()) {
                NOTIFY_ERROR("Timelapse render failed: {}", error_msg);
            } else {
                NOTIFY_ERROR("Timelapse render failed");
            }

            spdlog::error("[TimelapseState] Render error: {}", error_msg);
        }

    } else {
        spdlog::debug("[TimelapseState] Unknown action: {}", action);
    }
}

void TimelapseState::reset() {
    if (!subjects_initialized_) {
        return;
    }

    ui_queue_update([this]() {
        lv_subject_set_int(&timelapse_frame_count_, 0);
        lv_subject_set_int(&timelapse_render_progress_, 0);
        lv_subject_copy_string(&timelapse_render_status_, "idle");
    });

    last_notified_progress_ = -1;
    spdlog::debug("[TimelapseState] State reset");
}

} // namespace helix
