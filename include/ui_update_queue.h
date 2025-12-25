// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 HelixScreen Contributors
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
 * CRITICAL FIX (2025-12-25): When lv_async_call() is called DURING render (e.g.,
 * from a draw callback), LVGL's timer restart behavior causes the async callback
 * to fire IMMEDIATELY - still within the render phase. This triggers cascading
 * lv_inv_area() assertions. The fix: queue such callbacks and drain them AFTER
 * render completes via LV_EVENT_REFR_READY.
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
#include "lvgl/src/display/lv_display_private.h" // For rendering_in_progress

#include <spdlog/spdlog.h>

#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace helix::ui {

/**
 * @brief Callback type for queued updates
 */
using UpdateCallback = std::function<void()>;

/**
 * @brief Thread-safe UI update queue
 *
 * Singleton that manages pending UI updates. Call init() once at startup
 * to install a display event handler that processes updates BEFORE rendering.
 *
 * Key insight: Using a timer doesn't work because LVGL doesn't guarantee
 * timer order relative to display refresh. Instead, we use LV_EVENT_REFR_START
 * which fires RIGHT BEFORE rendering begins - the perfect safe point.
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
     * Registers an event handler on the default display that processes
     * pending updates at the START of each refresh cycle, BEFORE rendering.
     */
    void init() {
        if (initialized_)
            return;

        lv_display_t* disp = lv_display_get_default();
        if (!disp) {
            spdlog::warn("[UpdateQueue] init - no default display!");
            return;
        }

        // Register event handler for LV_EVENT_REFR_START
        // This fires RIGHT BEFORE rendering begins - the perfect safe point
        lv_display_add_event_cb(disp, refr_start_cb, LV_EVENT_REFR_START, this);
        display_ = disp;
        initialized_ = true;
        spdlog::info("[UpdateQueue] Initialized - REFR_START handler registered on display {:p}",
                     static_cast<void*>(disp));
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
     */
    void shutdown() {
        // Note: We don't remove the event callback because the display
        // may already be destroyed during lv_deinit(). Just mark as shutdown.
        initialized_ = false;
        display_ = nullptr;
    }

  private:
    UpdateQueue() = default;
    ~UpdateQueue() {
        shutdown();
    }

    // Non-copyable
    UpdateQueue(const UpdateQueue&) = delete;
    UpdateQueue& operator=(const UpdateQueue&) = delete;

    /**
     * @brief Process all pending updates before rendering starts
     *
     * Called by LVGL via LV_EVENT_REFR_START, guaranteed to run
     * BEFORE rendering_in_progress is set to true.
     */
    static void refr_start_cb(lv_event_t* e) {
        auto* self = static_cast<UpdateQueue*>(lv_event_get_user_data(e));
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
        // Wrap each callback in try-catch to prevent one bad callback from
        // blocking others and to avoid exceptions propagating through LVGL's C code
        while (!to_process.empty()) {
            auto& callback = to_process.front();
            try {
                callback();
            } catch (const std::exception& e) {
                spdlog::error("[UpdateQueue] Callback threw exception: {}", e.what());
            } catch (...) {
                spdlog::error("[UpdateQueue] Callback threw unknown exception");
            }
            to_process.pop();
        }
    }

    std::mutex mutex_;
    std::queue<UpdateCallback> pending_;
    lv_display_t* display_ = nullptr;
    bool initialized_ = false;
};

/**
 * @brief Queue for callbacks that must execute AFTER render completes
 *
 * When ui_async_call() is invoked during render phase, using lv_async_call()
 * directly causes the callback to fire immediately (due to LVGL's timer restart
 * behavior). This queue defers such callbacks until LV_EVENT_REFR_READY fires.
 *
 * Key insight: LVGL's lv_timer_handler() restarts from the head of the timer
 * list when a new timer is created. If this happens during _lv_display_refr_timer
 * (the render phase), newly created period-0 timers fire WITHIN render context,
 * causing lv_inv_area() assertions when their callbacks trigger invalidation.
 */
class DeferredRenderQueue {
  public:
    /**
     * @brief Callback info stored for deferred execution
     */
    struct DeferredCallback {
        lv_async_cb_t callback;
        void* user_data;
    };

    static DeferredRenderQueue& instance() {
        static DeferredRenderQueue instance;
        return instance;
    }

    /**
     * @brief Initialize the deferred queue (call once at startup)
     *
     * Registers LV_EVENT_REFR_READY handler to drain the queue after each render.
     */
    void init() {
        if (initialized_)
            return;

        lv_display_t* disp = lv_display_get_default();
        if (!disp) {
            spdlog::warn("[DeferredRenderQueue] init - no default display!");
            return;
        }

        // LV_EVENT_REFR_READY fires AFTER rendering completes - perfect drain point
        lv_display_add_event_cb(disp, refr_ready_cb, LV_EVENT_REFR_READY, this);
        display_ = disp;
        initialized_ = true;
        spdlog::info("[DeferredRenderQueue] Initialized - REFR_READY handler registered");
    }

    /**
     * @brief Queue a callback for post-render execution
     *
     * Thread-safe. Called when ui_async_call() is invoked during render phase.
     *
     * @param cb Callback function
     * @param user_data User data for callback
     */
    void queue(lv_async_cb_t cb, void* user_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        deferred_.push_back({cb, user_data});
    }

    /**
     * @brief Check if there are deferred callbacks (under lock for thread safety)
     */
    bool has_pending() {
        std::lock_guard<std::mutex> lock(mutex_);
        return !deferred_.empty();
    }

    void shutdown() {
        initialized_ = false;
        display_ = nullptr;
    }

  private:
    DeferredRenderQueue() = default;
    ~DeferredRenderQueue() {
        shutdown();
    }

    DeferredRenderQueue(const DeferredRenderQueue&) = delete;
    DeferredRenderQueue& operator=(const DeferredRenderQueue&) = delete;

    /**
     * @brief Drain deferred callbacks after render completes
     */
    static void refr_ready_cb(lv_event_t* e) {
        auto* self = static_cast<DeferredRenderQueue*>(lv_event_get_user_data(e));
        if (self && self->initialized_ && self->has_pending()) {
            self->drain();
        }
    }

    void drain() {
        // Move to local vector to minimize lock time
        std::vector<DeferredCallback> to_execute;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::swap(to_execute, deferred_);
        }

        if (!to_execute.empty()) {
            spdlog::debug("[DeferredRenderQueue] Draining {} callbacks", to_execute.size());
        }

        // Execute all deferred callbacks - now safe because render has finished
        // Wrap each callback in try-catch to prevent exceptions from propagating
        // through LVGL's C code (undefined behavior)
        for (const auto& cb_info : to_execute) {
            if (cb_info.callback) {
                try {
                    cb_info.callback(cb_info.user_data);
                } catch (const std::exception& e) {
                    spdlog::error("[DeferredRenderQueue] Callback threw exception: {}", e.what());
                } catch (...) {
                    spdlog::error("[DeferredRenderQueue] Callback threw unknown exception");
                }
            }
        }
    }

    std::mutex mutex_;
    std::vector<DeferredCallback> deferred_;
    lv_display_t* display_ = nullptr;
    bool initialized_ = false;
};

} // namespace helix::ui

/**
 * @brief Initialize the UI update queue
 *
 * Call this once during application startup, AFTER lv_init() but BEFORE
 * creating any UI elements. This ensures the processing timer has highest
 * priority and runs before other timers.
 */
inline void ui_update_queue_init() {
    helix::ui::UpdateQueue::instance().init();
    helix::ui::DeferredRenderQueue::instance().init();
}

/**
 * @brief Shutdown the UI update queue
 *
 * Call this during application shutdown, BEFORE lv_deinit().
 */
inline void ui_update_queue_shutdown() {
    helix::ui::UpdateQueue::instance().shutdown();
    helix::ui::DeferredRenderQueue::instance().shutdown();
}

/**
 * @brief Thread-safe async call wrapper with render-phase protection
 *
 * This wrapper handles a critical LVGL edge case: when lv_async_call() is invoked
 * DURING render (e.g., from a draw callback), LVGL's timer restart behavior causes
 * the async callback to fire immediately within the render phase. This triggers
 * cascading lv_inv_area() assertions when the callback modifies UI state.
 *
 * Solution: When called during render, callbacks are queued and executed AFTER
 * render completes (via LV_EVENT_REFR_READY). When called outside render, we
 * delegate directly to lv_async_call() for immediate timer scheduling.
 *
 * @param async_xcb Callback function (same signature as lv_async_call)
 * @param user_data User data passed to callback
 * @return LV_RESULT_OK on success
 */
inline lv_result_t ui_async_call(lv_async_cb_t async_xcb, void* user_data) {
    lv_display_t* disp = lv_display_get_default();

    // CRITICAL: If we're inside render phase, DO NOT use lv_async_call() directly!
    // LVGL's timer restart behavior would cause the callback to fire immediately,
    // leading to cascading lv_inv_area() assertions when observers trigger invalidation.
    if (disp && disp->rendering_in_progress) {
        spdlog::debug("[UI ASYNC] Deferring callback - render in progress");
        helix::ui::DeferredRenderQueue::instance().queue(async_xcb, user_data);
        return LV_RESULT_OK;
    }

    // Outside render phase - safe to use LVGL's native async call
    return lv_async_call(async_xcb, user_data);
}

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

    // Trigger a screen refresh so REFR_START fires and processes the queue
    // Without this, callbacks wait until something else triggers a refresh
    // Use ui_async_call (not lv_async_call) to handle render-phase safety
    ui_async_call(
        [](void*) {
            lv_obj_t* scr = lv_screen_active();
            if (scr) {
                lv_obj_invalidate(scr);
            }
        },
        nullptr);
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
