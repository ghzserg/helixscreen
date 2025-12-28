// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 HelixScreen Authors

#pragma once

#include "ui_update_queue.h"

#include "spdlog/spdlog.h"

#include <functional>
#include <memory>
#include <type_traits>

/**
 * @file async_helpers.h
 * @brief Thread-safe async callback helpers for LVGL main-thread execution
 *
 * This header provides utilities to safely defer function calls to the LVGL
 * main thread from background threads (e.g., WebSocket callbacks).
 *
 * @section problem Problem Solved
 * WebSocket callbacks run on libhv's event loop thread. Calling lv_subject_set_*()
 * directly from a background thread triggers lv_obj_invalidate() which asserts
 * if called during LVGL rendering. The traditional solution requires:
 *   1. Defining a context struct with instance pointer and values
 *   2. Defining a static callback function that casts, invokes, and deletes
 *   3. Calling ui_async_call with heap-allocated context
 *
 * This pattern is repeated 7+ times in printer_state.cpp alone, totaling ~150
 * lines of boilerplate. These utilities reduce it to single function calls.
 *
 * @section usage Usage
 * @code
 * // OLD: 15 lines of boilerplate
 * struct AsyncBoolContext {
 *     PrinterState* state;
 *     bool value;
 * };
 * void async_bool_callback(void* user_data) {
 *     auto* ctx = static_cast<AsyncBoolContext*>(user_data);
 *     if (ctx && ctx->state) {
 *         ctx->state->set_value_internal(ctx->value);
 *     }
 *     delete ctx;
 * }
 * void PrinterState::set_value(bool v) {
 *     ui_async_call(async_bool_callback, new AsyncBoolContext{this, v});
 * }
 *
 * // NEW: Single call
 * void PrinterState::set_value(bool v) {
 *     helix::async::invoke([this, v]() {
 *         set_value_internal(v);
 *     });
 * }
 * @endcode
 *
 * @section safety Thread Safety
 * WARNING: Like the existing pattern, these helpers capture `this` pointers
 * by value. If the object is destroyed before the async callback runs,
 * use-after-free occurs. Callers must ensure object lifetime exceeds callback
 * execution. For long-lived singletons like PrinterState, this is typically safe.
 *
 * For short-lived objects, consider:
 *   - Using std::weak_ptr with async::invoke_weak()
 *   - Checking a destruction flag in the callback
 *   - Cancelling pending callbacks in the destructor
 */

namespace helix::async {

/**
 * @brief Invoke a callable on the LVGL main thread
 *
 * Type-erases any callable (lambda, std::function, function pointer) into
 * a heap-allocated std::function<void()> and schedules it via ui_async_call.
 *
 * @tparam Callable Any invocable type with signature void()
 * @param callable The function/lambda to invoke on the main thread
 *
 * @code
 * helix::async::invoke([this, value]() {
 *     lv_subject_set_int(&my_subject_, value);
 * });
 * @endcode
 */
template <typename Callable> void invoke(Callable&& callable) {
    // Type-erase into std::function on the heap
    auto* fn = new std::function<void()>(std::forward<Callable>(callable));

    ui_async_call(
        [](void* data) {
            auto* func = static_cast<std::function<void()>*>(data);
            if (func) {
                try {
                    (*func)();
                } catch (const std::exception& e) {
                    spdlog::error("[async::invoke] Exception in callback: {}", e.what());
                } catch (...) {
                    spdlog::error("[async::invoke] Unknown exception in callback");
                }
            }
            delete func;
        },
        fn);
}

/**
 * @brief Invoke a member function with one parameter on the LVGL main thread
 *
 * Convenience overload for the common pattern of calling an internal setter.
 * Copies the value to ensure it survives across threads.
 *
 * @tparam T Instance type (e.g., PrinterState)
 * @tparam V Value type (copied, not referenced)
 * @param instance Pointer to the object (must outlive the async call)
 * @param method Member function pointer with signature void(V)
 * @param value Value to pass (copied into the async context)
 *
 * @code
 * helix::async::call_method(this, &PrinterState::set_temp_internal, temp);
 * @endcode
 */
template <typename T, typename V> void call_method(T* instance, void (T::*method)(V), V value) {
    if (!instance) {
        return;
    }

    invoke([instance, method, val = std::move(value)]() mutable { (instance->*method)(val); });
}

/**
 * @brief Invoke a member function with const reference parameter
 *
 * @tparam T Instance type
 * @tparam V Value type (copied into context)
 * @param instance Pointer to the object
 * @param method Member function pointer with signature void(const V&)
 * @param value Value to pass (copied, then passed as const ref)
 *
 * @code
 * helix::async::call_method_ref(this, &PrinterState::set_version_internal, version_string);
 * @endcode
 */
template <typename T, typename V>
void call_method_ref(T* instance, void (T::*method)(const V&), const V& value) {
    if (!instance) {
        return;
    }

    invoke([instance, method, val = value]() { (instance->*method)(val); });
}

/**
 * @brief Invoke a member function with two parameters
 *
 * Handles cases like set_connection_state_internal(int, const char*).
 *
 * @tparam T Instance type
 * @tparam V1 First parameter type
 * @tparam V2 Second parameter type
 * @param instance Pointer to the object
 * @param method Member function pointer
 * @param v1 First value (copied)
 * @param v2 Second value (copied)
 *
 * @code
 * helix::async::call_method2(this, &PrinterState::set_connection_internal, state, message);
 * @endcode
 */
template <typename T, typename V1, typename V2>
void call_method2(T* instance, void (T::*method)(V1, V2), V1 v1, V2 v2) {
    if (!instance) {
        return;
    }

    invoke([instance, method, val1 = std::move(v1), val2 = std::move(v2)]() mutable {
        (instance->*method)(val1, val2);
    });
}

/**
 * @brief Safely invoke a callable if a weak_ptr is still valid
 *
 * This is the recommended pattern for objects with uncertain lifetime.
 * The callback is only invoked if the weak_ptr can be locked.
 *
 * @tparam T Object type
 * @tparam Callable Function taking T& as parameter
 * @param weak Weak pointer to the object
 * @param callable Function to invoke with the object
 *
 * @code
 * auto weak = std::weak_ptr<MyObject>(shared_obj);
 * helix::async::invoke_weak(weak, [value](MyObject& obj) {
 *     obj.set_value(value);
 * });
 * @endcode
 */
template <typename T, typename Callable>
void invoke_weak(std::weak_ptr<T> weak, Callable&& callable) {
    invoke([w = std::move(weak), fn = std::forward<Callable>(callable)]() {
        if (auto shared = w.lock()) {
            fn(*shared);
        }
    });
}

} // namespace helix::async
