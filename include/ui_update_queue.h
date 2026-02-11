// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file ui_update_queue.h
 * @brief Thread-safe UI update queue for LVGL
 *
 * This module provides a safe mechanism for scheduling UI updates from any thread.
 * Updates are queued and processed at the START of each lv_timer_handler cycle,
 * BEFORE rendering begins. This guarantees that widget modifications never
 * happen during the render phase.
 *
 * Architecture:
 * 1. Any thread can queue updates via ui_queue_update()
 * 2. Updates accumulate in a thread-safe queue
 * 3. At the start of each frame (via LVGL timer), all pending updates are processed
 * 4. Rendering happens AFTER all updates are applied
 *
 * This is similar to React's batched state updates - changes are queued and
 * applied together at a safe point.
 *
 * Usage:
 * @code
 * // From any thread (WebSocket callback, async operation, etc.):
 * ui_queue_update([](void*) {
 *     lv_subject_set_int(&my_subject, new_value);
 *     lv_label_set_text(label, "Updated!");
 * });
 *
 * // With captured data:
 * auto* data = new MyData{value, text};
 * ui_queue_update([](void* user_data) {
 *     auto* d = static_cast<MyData*>(user_data);
 *     lv_subject_set_int(&my_subject, d->value);
 *     delete d;
 * }, data);
 * @endcode
 */

#pragma once

#include "lvgl/lvgl.h"

#include <spdlog/spdlog.h>

#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace helix::ui {

/**
 * @brief Callback type for queued updates
 */
using UpdateCallback = std::function<void()>;

/**
 * @brief Thread-safe UI update queue
 *
 * Singleton that manages pending UI updates. Call init() once at startup
 * to install a high-priority timer that processes updates every lv_timer_handler() cycle.
 *
 * Key insight: Using LV_EVENT_REFR_START doesn't work because it only fires when
 * LVGL decides to render. If nothing invalidates the display, the queue never drains.
 * Instead, we use a highest-priority timer that fires every lv_timer_handler() call,
 * ensuring callbacks execute promptly regardless of render state.
 */
class UpdateQueue {
  public:
    /**
     * @brief Get singleton instance
     */
    static UpdateQueue& instance() {
        static UpdateQueue instance;
        return instance;
    }

    /**
     * @brief Initialize the update queue (call once at startup)
     *
     * Creates a highest-priority timer that processes pending updates
     * every lv_timer_handler() cycle, BEFORE the render timer runs.
     */
    void init() {
        if (initialized_)
            return;

        // Create a timer that fires every lv_timer_handler() cycle
        // Period of 1ms ensures it runs frequently (LVGL processes all ready timers)
        // Created early at init, so it's near the head of the timer list
        timer_ = lv_timer_create(timer_cb, 1, this);
        if (!timer_) {
            spdlog::error("[UpdateQueue] Failed to create timer!");
            return;
        }

        initialized_ = true;
        spdlog::debug("[UpdateQueue] Initialized - timer created for queue drain");
    }

    /**
     * @brief Queue an update for processing
     *
     * Thread-safe. Can be called from any thread.
     * The callback will be executed on the main LVGL thread before rendering.
     *
     * @param callback Function to execute
     */
    void queue(UpdateCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push(std::move(callback));
    }

    /**
     * @brief Shutdown and cleanup
     *
     * Nullifies the timer pointer and clears the pending queue to prevent
     * stale callbacks from executing after objects they reference are
     * destroyed. The actual LVGL timer is freed by lv_deinit().
     */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::queue<UpdateCallback>().swap(pending_); // Clear pending queue
        }
        timer_ = nullptr;
        initialized_ = false;
    }

    /**
     * @brief Pause the update queue timer
     *
     * Prevents the timer from firing during lv_timer_handler() calls.
     * Used by test infrastructure to break the infinite restart chain
     * where UpdateQueue callbacks trigger subject changes that create
     * new period-0 timers.
     */
    void pause_timer() {
        if (timer_) {
            lv_timer_pause(timer_);
        }
    }

    /**
     * @brief Resume the update queue timer
     *
     * Re-enables the timer after it was paused.
     */
    void resume_timer() {
        if (timer_) {
            lv_timer_resume(timer_);
        }
    }

  private:
    friend class UpdateQueueTestAccess;
    UpdateQueue() = default;
    ~UpdateQueue() {
        shutdown();
    }

    // Non-copyable
    UpdateQueue(const UpdateQueue&) = delete;
    UpdateQueue& operator=(const UpdateQueue&) = delete;

    /**
     * @brief Timer callback - processes all pending updates
     *
     * Called by LVGL on every lv_timer_handler() cycle due to highest priority.
     * Runs BEFORE the render timer, ensuring updates are applied before drawing.
     */
    static void timer_cb(lv_timer_t* timer) {
        auto* self = static_cast<UpdateQueue*>(lv_timer_get_user_data(timer));
        if (self && self->initialized_) {
            self->process_pending();
        }
    }

    void process_pending() {
        // Move pending updates to local queue to minimize lock time
        std::queue<UpdateCallback> to_process;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::swap(to_process, pending_);
        }

        // Execute all pending updates - safe because render hasn't started yet
        while (!to_process.empty()) {
            auto& callback = to_process.front();
            callback();
            to_process.pop();
        }
    }

    std::mutex mutex_;
    std::queue<UpdateCallback> pending_;
    lv_timer_t* timer_ = nullptr;
    bool initialized_ = false;
};

} // namespace helix::ui

/**
 * @brief Queue a UI update for safe execution
 *
 * This is the primary API for scheduling UI updates from any thread.
 * Updates are guaranteed to execute BEFORE rendering, avoiding the
 * "Invalidate area is not allowed during rendering" assertion.
 *
 * @param callback Function to execute on the main thread
 */
inline void ui_queue_update(helix::ui::UpdateCallback callback) {
    helix::ui::UpdateQueue::instance().queue(std::move(callback));
}

/**
 * @brief Queue a UI update with data
 *
 * Convenience wrapper for updates that need to pass data.
 * The data is captured and passed to the callback.
 *
 * @tparam T Type of data to pass
 * @param data Data to pass to callback (moved into queue)
 * @param callback Function to execute with data
 */
template <typename T>
void ui_queue_update(std::unique_ptr<T> data, std::function<void(T*)> callback) {
    // Capture data and callback in a lambda
    T* raw_ptr = data.release(); // Transfer ownership
    ui_queue_update([raw_ptr, callback = std::move(callback)]() {
        std::unique_ptr<T> owned(raw_ptr); // Reclaim ownership for RAII
        callback(owned.get());
    });
}

/**
 * @brief Initialize the UI update queue
 *
 * Call this once during application startup, AFTER lv_init() but BEFORE
 * creating any UI elements. This ensures the processing timer has highest
 * priority and runs before other timers.
 */
inline void ui_update_queue_init() {
    helix::ui::UpdateQueue::instance().init();
}

/**
 * @brief Shutdown the UI update queue
 *
 * Call this during application shutdown, BEFORE lv_deinit().
 */
inline void ui_update_queue_shutdown() {
    helix::ui::UpdateQueue::instance().shutdown();
}

/**
 * @brief Drop-in replacement for lv_async_call
 *
 * Has the EXACT same signature as lv_async_call() but uses the UI update queue
 * to ensure callbacks run BEFORE rendering, not during.
 *
 * Migration: Simply replace `lv_async_call(` with `ui_async_call(`
 *
 * @param async_xcb Callback function (same signature as lv_async_call)
 * @param user_data User data passed to callback
 * @return LV_RESULT_OK always (queue never fails)
 */
inline lv_result_t ui_async_call(lv_async_cb_t async_xcb, void* user_data) {
    ui_queue_update([async_xcb, user_data]() {
        if (async_xcb) {
            async_xcb(user_data);
        }
    });
    return LV_RESULT_OK;
}
