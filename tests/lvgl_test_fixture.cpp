// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lvgl_test_fixture.h"

#include "ui_test_utils.h"

#include "test_helpers/update_queue_test_access.h"

#include <chrono>
#include <thread>

// Static member definitions
std::once_flag LVGLTestFixture::s_init_flag;
bool LVGLTestFixture::s_initialized = false;
lv_display_t* LVGLTestFixture::s_display = nullptr;
bool LVGLTestFixture::s_queue_initialized = false;

// Display buffer - static to persist across test cases
// Size: width * 10 lines for partial rendering mode
// IMPORTANT: Must be aligned to LV_DRAW_BUF_ALIGN (typically 4 or 8 bytes)
// Using alignas(64) for maximum compatibility with all platforms
alignas(64) static lv_color_t s_display_buf[TEST_DISPLAY_WIDTH * 10];

/**
 * @brief Flush callback for virtual display (no-op for testing)
 */
static void test_display_flush_cb(lv_display_t* disp, const lv_area_t* /*area*/,
                                  uint8_t* /*px_map*/) {
    // No actual rendering needed for tests
    lv_display_flush_ready(disp);
}

LVGLTestFixture::LVGLTestFixture() : m_test_screen(nullptr) {
    ensure_lvgl_initialized();

    // Initialize update queue once (static guard) - CRITICAL for ui_queue_update()
    // Per L053/L054: Tests using UpdateQueue need proper lifecycle
    if (!s_queue_initialized) {
        ui_update_queue_init();
        s_queue_initialized = true;
    }

    m_test_screen = create_test_screen();
}

LVGLTestFixture::~LVGLTestFixture() {
    // Clean up the test screen
    if (m_test_screen != nullptr) {
        // Switch to a different screen before deleting if this is active
        lv_obj_t* active = lv_screen_active();
        if (active == m_test_screen) {
            // Create a temporary screen to switch to
            lv_obj_t* temp = lv_obj_create(nullptr);
            lv_screen_load(temp);
        }
        lv_obj_delete(m_test_screen);
        m_test_screen = nullptr;
    }

    // Per L053/L054: Drain pending callbacks before shutdown
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    // Shutdown queue
    ui_update_queue_shutdown();

    // Reset static flag for next test
    s_queue_initialized = false;
}

void LVGLTestFixture::ensure_lvgl_initialized() {
    std::call_once(s_init_flag, []() {
        // Initialize LVGL library (safe version avoids "already initialized" warnings)
        lv_init_safe();

        // Create virtual display for headless testing
        s_display = lv_display_create(TEST_DISPLAY_WIDTH, TEST_DISPLAY_HEIGHT);
        if (s_display != nullptr) {
            lv_display_set_buffers(s_display, s_display_buf, nullptr, sizeof(s_display_buf),
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
            lv_display_set_flush_cb(s_display, test_display_flush_cb);
        }

        s_initialized = true;
    });
}

lv_obj_t* LVGLTestFixture::create_test_screen() {
    // Clean up existing screen if any
    if (m_test_screen != nullptr) {
        lv_obj_delete(m_test_screen);
    }

    // Create new screen and make it active
    m_test_screen = lv_obj_create(nullptr);
    if (m_test_screen != nullptr) {
        lv_screen_load(m_test_screen);
    }

    return m_test_screen;
}

void LVGLTestFixture::process_lvgl(int ms) {
    if (ms <= 0) {
        return;
    }

    // Process in small increments for more accurate timing
    constexpr int tick_interval_ms = 5;
    int elapsed = 0;

    while (elapsed < ms) {
        // Advance LVGL tick (needed for animations and time-based logic)
        lv_tick_inc(tick_interval_ms);

        // Use the safe timer handler which drains the UpdateQueue,
        // normalizes timer timestamps, and pauses the queue timer
        // during lv_timer_handler() to prevent infinite loops.
        lv_timer_handler_safe();

        elapsed += tick_interval_ms;

        // Small sleep to avoid busy-waiting in longer waits
        if (ms > 50) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
