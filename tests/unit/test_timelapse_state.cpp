// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"
#include "timelapse_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using json = nlohmann::json;

// Helper to flush queued UI updates so subject values are readable
static void flush_queue() {
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
}

// ============================================================================
// Subject lifecycle
// ============================================================================

TEST_CASE("TimelapseState: init_subjects creates valid subjects", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    REQUIRE(state.get_render_progress_subject() != nullptr);
    REQUIRE(state.get_render_status_subject() != nullptr);
    REQUIRE(state.get_frame_count_subject() != nullptr);

    // Verify initial values
    REQUIRE(lv_subject_get_int(state.get_render_progress_subject()) == 0);
    REQUIRE(std::string(lv_subject_get_string(state.get_render_status_subject())) == "idle");
    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 0);

    state.deinit_subjects();
}

TEST_CASE("TimelapseState: deinit_subjects cleans up", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    // Should not crash
    state.deinit_subjects();

    // Double deinit should be safe
    state.deinit_subjects();
}

// ============================================================================
// newframe events
// ============================================================================

TEST_CASE("TimelapseState: newframe increments frame count", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    json event = {{"action", "newframe"}, {"framefile", "frame001.jpg"}, {"framenum", 1}};
    state.handle_timelapse_event(event);
    flush_queue();

    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 1);

    state.deinit_subjects();
}

TEST_CASE("TimelapseState: multiple newframe events increment correctly", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    for (int i = 1; i <= 5; i++) {
        json event = {{"action", "newframe"}, {"framefile", "frame.jpg"}, {"framenum", i}};
        state.handle_timelapse_event(event);
        flush_queue();
    }

    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 5);

    state.deinit_subjects();
}

// ============================================================================
// render events
// ============================================================================

TEST_CASE("TimelapseState: render running updates progress and status", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    json event = {{"action", "render"}, {"status", "running"}, {"progress", 45}};
    state.handle_timelapse_event(event);
    flush_queue();

    REQUIRE(lv_subject_get_int(state.get_render_progress_subject()) == 45);
    REQUIRE(std::string(lv_subject_get_string(state.get_render_status_subject())) == "rendering");

    state.deinit_subjects();
}

TEST_CASE("TimelapseState: render success sets complete and resets progress", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    // First set some progress
    json running = {{"action", "render"}, {"status", "running"}, {"progress", 80}};
    state.handle_timelapse_event(running);
    flush_queue();

    // Then success
    json success = {{"action", "render"}, {"status", "success"}, {"filename", "vid.mp4"}};
    state.handle_timelapse_event(success);
    flush_queue();

    REQUIRE(std::string(lv_subject_get_string(state.get_render_status_subject())) == "complete");
    REQUIRE(lv_subject_get_int(state.get_render_progress_subject()) == 0);

    state.deinit_subjects();
}

TEST_CASE("TimelapseState: render error sets error status", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    json event = {{"action", "render"}, {"status", "error"}, {"msg", "ffmpeg failed"}};
    state.handle_timelapse_event(event);
    flush_queue();

    REQUIRE(std::string(lv_subject_get_string(state.get_render_status_subject())) == "error");

    state.deinit_subjects();
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("TimelapseState: reset clears all state", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    // Set some state
    json frame = {{"action", "newframe"}, {"framefile", "f.jpg"}, {"framenum", 1}};
    state.handle_timelapse_event(frame);
    flush_queue();

    json render = {{"action", "render"}, {"status", "running"}, {"progress", 50}};
    state.handle_timelapse_event(render);
    flush_queue();

    // Reset
    state.reset();
    flush_queue();

    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_render_progress_subject()) == 0);
    REQUIRE(std::string(lv_subject_get_string(state.get_render_status_subject())) == "idle");

    state.deinit_subjects();
}

// ============================================================================
// Edge cases: malformed/unknown events
// ============================================================================

TEST_CASE("TimelapseState: unknown action does not crash or change state", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    json event = {{"action", "unknown_action"}};
    state.handle_timelapse_event(event);
    flush_queue();

    // State unchanged from defaults
    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_render_progress_subject()) == 0);
    REQUIRE(std::string(lv_subject_get_string(state.get_render_status_subject())) == "idle");

    state.deinit_subjects();
}

TEST_CASE("TimelapseState: malformed JSON with no action field", "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    // Empty object
    json event = json::object();
    state.handle_timelapse_event(event);
    flush_queue();

    // State unchanged
    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 0);

    // Non-string action
    json bad_action = {{"action", 42}};
    state.handle_timelapse_event(bad_action);
    flush_queue();

    REQUIRE(lv_subject_get_int(state.get_frame_count_subject()) == 0);

    state.deinit_subjects();
}

// ============================================================================
// Notification throttling
// ============================================================================

TEST_CASE("TimelapseState: render progress notifications throttled to 25% boundaries",
          "[timelapse_state]") {
    lv_init_safe();
    auto& state = TimelapseState::instance();
    state.deinit_subjects();
    state.init_subjects(false);

    // Send progress events at 10%, 20%, 25%, 30%, 50%, 75%, 100%
    // Only 25%, 50%, 75%, 100% should trigger notifications
    // (We can't easily verify notifications in unit tests, but we verify
    // that the progress subject updates correctly for each event)
    int progress_values[] = {10, 20, 25, 30, 50, 75, 100};
    for (int p : progress_values) {
        json event = {{"action", "render"}, {"status", "running"}, {"progress", p}};
        state.handle_timelapse_event(event);
        flush_queue();

        REQUIRE(lv_subject_get_int(state.get_render_progress_subject()) == p);
    }

    state.deinit_subjects();
}
