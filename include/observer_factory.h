// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file observer_factory.h
 * @brief Factory functions for creating type-safe LVGL observers with RAII cleanup
 *
 * Provides template-based observer creation that eliminates boilerplate callback code.
 * All observers return ObserverGuard for automatic cleanup.
 *
 * Three observer patterns supported:
 * 1. Value observer - stores value directly, calls update method via ui_async_call
 * 2. Transform observer - applies transformation before storing, then async update
 * 3. Raw observer - stores raw value without async call (for timer-based updates)
 *
 * @pattern Factory pattern with member-pointers for type-safe field access
 * @threading Observers with update methods use helix::ui::async_call() for thread safety
 */

#pragma once

#include "ui_observer_guard.h"
#include "ui_update_queue.h"

#include "lvgl/lvgl.h"
#include "moonraker_client.h" // ConnectionState
#include "printer_state.h"    // PrintJobState

#include <string>
#include <type_traits>

namespace helix::ui {

namespace detail {

/**
 * @brief Extract typed value from LVGL subject based on type T
 *
 * Template specializations handle int, float, bool, and pointer types.
 */
template <typename T> T get_subject_value(lv_subject_t* subject);

template <> inline int get_subject_value<int>(lv_subject_t* subject) {
    return lv_subject_get_int(subject);
}

template <> inline float get_subject_value<float>(lv_subject_t* subject) {
    // Float subjects store centidegrees; convert to degrees
    return static_cast<float>(lv_subject_get_int(subject)) / 10.0f;
}

template <> inline bool get_subject_value<bool>(lv_subject_t* subject) {
    return lv_subject_get_int(subject) != 0;
}

/**
 * @brief Pointer specialization for get_subject_value
 */
template <typename T> inline T* get_subject_value_ptr(lv_subject_t* subject) {
    return static_cast<T*>(lv_subject_get_pointer(subject));
}

/**
 * @brief Context for value observer callbacks
 */
template <typename T, typename Panel> struct ValueObserverContext {
    Panel* panel;
    T Panel::* member;
    void (Panel::*on_update)();
};

/**
 * @brief Context for transform observer callbacks
 */
template <typename T, typename Panel, typename Transform> struct TransformObserverContext {
    Panel* panel;
    Transform transform;
    T Panel::* member;
    void (Panel::*on_update)();
};

/**
 * @brief Context for raw cache observer callbacks
 */
template <typename T, typename Panel> struct RawObserverContext {
    Panel* panel;
    T Panel::* member;
};

/**
 * @brief C-style callback for value observers [L029]
 *
 * Extracts value from subject, stores in member, schedules async update.
 */
template <typename T, typename Panel>
void value_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* ctx = static_cast<ValueObserverContext<T, Panel>*>(lv_observer_get_user_data(observer));

    // [L049] Null panel check
    if (!ctx || !ctx->panel) {
        return;
    }

    T value = get_subject_value<T>(subject);
    ctx->panel->*(ctx->member) = value;

    // Schedule async UI update via captured member function pointer
    auto* async_ctx = new ValueObserverContext<T, Panel>(*ctx);
    helix::ui::async_call(
        [](void* user_data) {
            auto* c = static_cast<ValueObserverContext<T, Panel>*>(user_data);
            if (c && c->panel && c->on_update) {
                (c->panel->*(c->on_update))();
            }
            delete c;
        },
        async_ctx);
}

/**
 * @brief C-style callback for transform observers [L029]
 *
 * Applies transform to subject value, stores result, schedules async update.
 */
template <typename T, typename Panel, typename Transform>
void transform_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* ctx = static_cast<TransformObserverContext<T, Panel, Transform>*>(
        lv_observer_get_user_data(observer));

    // [L049] Null panel check
    if (!ctx || !ctx->panel) {
        return;
    }

    int raw_value = lv_subject_get_int(subject);
    T transformed = ctx->transform(raw_value);
    ctx->panel->*(ctx->member) = transformed;

    // Schedule async UI update - must copy context for async call
    auto* async_ctx = new TransformObserverContext<T, Panel, Transform>(*ctx);
    helix::ui::async_call(
        [](void* user_data) {
            auto* c = static_cast<TransformObserverContext<T, Panel, Transform>*>(user_data);
            if (c && c->panel && c->on_update) {
                (c->panel->*(c->on_update))();
            }
            delete c;
        },
        async_ctx);
}

/**
 * @brief C-style callback for raw cache observers [L029]
 *
 * Stores raw value directly in member, no async update.
 */
template <typename T, typename Panel>
void raw_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* ctx = static_cast<RawObserverContext<T, Panel>*>(lv_observer_get_user_data(observer));

    // [L049] Null panel check
    if (!ctx || !ctx->panel) {
        return;
    }

    T value = get_subject_value<T>(subject);
    ctx->panel->*(ctx->member) = value;
}

} // namespace detail

/**
 * @brief Create observer that stores value directly and calls update method
 *
 * Use for simple value caching where the raw subject value is stored
 * and an async UI update is triggered.
 *
 * @tparam T Member variable type (int, float, bool)
 * @tparam Panel Panel class type
 * @param subject LVGL subject to observe
 * @param member Pointer-to-member for value storage
 * @param on_update Member function called via ui_async_call after value update
 * @param panel Panel instance
 * @return ObserverGuard for RAII cleanup
 *
 * @code{.cpp}
 * observer_ = create_value_observer(
 *     state.get_temp_subject(),
 *     &MyPanel::cached_temp_,
 *     &MyPanel::update_display,
 *     this);
 * @endcode
 */
template <typename T, typename Panel>
ObserverGuard create_value_observer(lv_subject_t* subject, T Panel::* member,
                                    void (Panel::*on_update)(), Panel* panel) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    // Allocate context on heap - lives with observer lifetime
    auto* ctx = new detail::ValueObserverContext<T, Panel>{panel, member, on_update};

    return ObserverGuard(subject, detail::value_observer_cb<T, Panel>, ctx);
}

/**
 * @brief Create observer that transforms value before storing
 *
 * Use when subject value needs transformation (e.g., centidegrees to degrees)
 * before storing in member variable.
 *
 * @tparam T Member variable type after transformation
 * @tparam Panel Panel class type
 * @tparam Transform Callable type: T(int raw_value)
 * @param subject LVGL subject to observe
 * @param transform Function to transform raw int value to T
 * @param member Pointer-to-member for transformed value storage
 * @param on_update Member function called via ui_async_call after value update
 * @param panel Panel instance
 * @return ObserverGuard for RAII cleanup
 *
 * @code{.cpp}
 * observer_ = create_transform_observer(
 *     state.get_temp_subject(),
 *     [](int centi) { return centi_to_degrees(centi); },
 *     &FilamentPanel::nozzle_temp_,
 *     &FilamentPanel::update_temps,
 *     this);
 * @endcode
 */
template <typename T, typename Panel, typename Transform>
ObserverGuard create_transform_observer(lv_subject_t* subject, Transform&& transform,
                                        T Panel::* member, void (Panel::*on_update)(),
                                        Panel* panel) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    // Allocate context on heap with decayed transform type
    using DecayedTransform = std::decay_t<Transform>;
    auto* ctx = new detail::TransformObserverContext<T, Panel, DecayedTransform>{
        panel, std::forward<Transform>(transform), member, on_update};

    return ObserverGuard(subject, detail::transform_observer_cb<T, Panel, DecayedTransform>, ctx);
}

/**
 * @brief Create observer that stores raw value without async update
 *
 * Use for caching values that will be transformed later during display,
 * or when UI update is handled by a timer or other mechanism.
 *
 * @tparam T Member variable type
 * @tparam Panel Panel class type
 * @param subject LVGL subject to observe
 * @param member Pointer-to-member for value storage
 * @param panel Panel instance
 * @return ObserverGuard for RAII cleanup
 *
 * @code{.cpp}
 * observer_ = create_raw_observer(
 *     state.get_temp_subject(),
 *     &ControlsPanel::cached_temp_centi_,
 *     this);
 * @endcode
 */
template <typename T, typename Panel>
ObserverGuard create_raw_observer(lv_subject_t* subject, T Panel::* member, Panel* panel) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    // Allocate context on heap - lives with observer lifetime
    auto* ctx = new detail::RawObserverContext<T, Panel>{panel, member};

    return ObserverGuard(subject, detail::raw_observer_cb<T, Panel>, ctx);
}

// ============================================================================
// Lambda-based API (more flexible than member-pointer API)
// ============================================================================

namespace detail {

/**
 * @brief Context for lambda-based observers
 */
template <typename Panel, typename Handler> struct LambdaObserverContext {
    Panel* panel;
    Handler handler;
};

/**
 * @brief Context for async lambda observers with update handler
 */
template <typename Panel, typename ValueHandler, typename UpdateHandler>
struct AsyncLambdaObserverContext {
    Panel* panel;
    ValueHandler value_handler;
    UpdateHandler update_handler;
};

} // namespace detail

/**
 * @brief Create deferred int observer with custom lambda handler
 *
 * The handler is deferred via helix::ui::queue_update() to run after the current
 * subject notification completes. This prevents re-entrant observer
 * destruction crashes (issue #82). Safe default for all observer callbacks.
 *
 * @tparam Panel Panel class type
 * @tparam Handler Callable type: void(Panel*, int)
 * @param subject LVGL subject to observe
 * @param panel Panel instance
 * @param handler Lambda called with panel and int value
 * @return ObserverGuard for RAII cleanup
 */
template <typename Panel, typename Handler>
ObserverGuard observe_int_sync(lv_subject_t* subject, Panel* panel, Handler&& handler) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    using DecayedHandler = std::decay_t<Handler>;
    auto* ctx = new detail::LambdaObserverContext<Panel, DecayedHandler>{
        panel, std::forward<Handler>(handler)};

    return ObserverGuard(
        subject,
        [](lv_observer_t* obs, lv_subject_t* subj) {
            auto* c = static_cast<detail::LambdaObserverContext<Panel, DecayedHandler>*>(
                lv_observer_get_user_data(obs));
            if (c && c->panel) {
                int value = lv_subject_get_int(subj);
                // Copy handler and panel pointer so the deferred lambda is
                // self-contained and safe even if the observer context is
                // destroyed before execution (the exact crash in issue #82).
                auto handler_copy = c->handler;
                auto* panel_ptr = c->panel;
                helix::ui::queue_update(
                    [handler_copy, panel_ptr, value]() { handler_copy(panel_ptr, value); });
            }
        },
        ctx);
}

/**
 * @brief Create immediate (non-deferred) int observer with custom lambda handler
 *
 * The handler is called directly in the observer callback with no deferral.
 * Use ONLY when you are certain the callback will NOT modify observer lifecycle
 * (no observer reassignment, no widget destruction, no ObserverGuard mutation).
 * Prefer observe_int_sync() in all other cases.
 *
 * @tparam Panel Panel class type
 * @tparam Handler Callable type: void(Panel*, int)
 */
template <typename Panel, typename Handler>
ObserverGuard observe_int_immediate(lv_subject_t* subject, Panel* panel, Handler&& handler) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    using DecayedHandler = std::decay_t<Handler>;
    auto* ctx = new detail::LambdaObserverContext<Panel, DecayedHandler>{
        panel, std::forward<Handler>(handler)};

    return ObserverGuard(
        subject,
        [](lv_observer_t* obs, lv_subject_t* subj) {
            auto* c = static_cast<detail::LambdaObserverContext<Panel, DecayedHandler>*>(
                lv_observer_get_user_data(obs));
            if (c && c->panel) {
                int value = lv_subject_get_int(subj);
                c->handler(c->panel, value);
            }
        },
        ctx);
}

/**
 * @brief Create async int observer with value and update handlers
 *
 * Value handler is called synchronously, update handler via ui_async_call.
 *
 * @tparam Panel Panel class type
 * @tparam ValueHandler Callable: void(Panel*, int)
 * @tparam UpdateHandler Callable: void(Panel*)
 */
template <typename Panel, typename ValueHandler, typename UpdateHandler>
ObserverGuard observe_int_async(lv_subject_t* subject, Panel* panel, ValueHandler&& value_handler,
                                UpdateHandler&& update_handler) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    using DecayedValueHandler = std::decay_t<ValueHandler>;
    using DecayedUpdateHandler = std::decay_t<UpdateHandler>;
    auto* ctx =
        new detail::AsyncLambdaObserverContext<Panel, DecayedValueHandler, DecayedUpdateHandler>{
            panel, std::forward<ValueHandler>(value_handler),
            std::forward<UpdateHandler>(update_handler)};

    return ObserverGuard(
        subject,
        [](lv_observer_t* obs, lv_subject_t* subj) {
            auto* c = static_cast<detail::AsyncLambdaObserverContext<Panel, DecayedValueHandler,
                                                                     DecayedUpdateHandler>*>(
                lv_observer_get_user_data(obs));
            if (c && c->panel) {
                int value = lv_subject_get_int(subj);
                c->value_handler(c->panel, value);

                // Schedule async update
                helix::ui::async_call(
                    [](void* user_data) {
                        auto* ctx = static_cast<detail::AsyncLambdaObserverContext<
                            Panel, DecayedValueHandler, DecayedUpdateHandler>*>(user_data);
                        if (ctx && ctx->panel) {
                            ctx->update_handler(ctx->panel);
                        }
                    },
                    c);
            }
        },
        ctx);
}

/**
 * @brief Create deferred string observer with custom lambda handler
 *
 * The handler is deferred via helix::ui::queue_update() to run after the current
 * subject notification completes. String value is copied to ensure validity.
 *
 * @tparam Panel Panel class type
 * @tparam Handler Callable: void(Panel*, const char*)
 */
template <typename Panel, typename Handler>
ObserverGuard observe_string(lv_subject_t* subject, Panel* panel, Handler&& handler) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    using DecayedHandler = std::decay_t<Handler>;
    auto* ctx = new detail::LambdaObserverContext<Panel, DecayedHandler>{
        panel, std::forward<Handler>(handler)};

    return ObserverGuard(
        subject,
        [](lv_observer_t* obs, lv_subject_t* subj) {
            auto* c = static_cast<detail::LambdaObserverContext<Panel, DecayedHandler>*>(
                lv_observer_get_user_data(obs));
            if (c && c->panel) {
                const char* str = lv_subject_get_string(subj);
                std::string str_copy = str ? str : "";
                auto handler_copy = c->handler;
                auto* panel_ptr = c->panel;
                helix::ui::queue_update([handler_copy, panel_ptr, str_copy]() {
                    handler_copy(panel_ptr, str_copy.c_str());
                });
            }
        },
        ctx);
}

/**
 * @brief Create immediate (non-deferred) string observer
 *
 * Use ONLY when the callback will NOT modify observer lifecycle.
 * Prefer observe_string() in all other cases.
 *
 * @tparam Panel Panel class type
 * @tparam Handler Callable: void(Panel*, const char*)
 */
template <typename Panel, typename Handler>
ObserverGuard observe_string_immediate(lv_subject_t* subject, Panel* panel, Handler&& handler) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    using DecayedHandler = std::decay_t<Handler>;
    auto* ctx = new detail::LambdaObserverContext<Panel, DecayedHandler>{
        panel, std::forward<Handler>(handler)};

    return ObserverGuard(
        subject,
        [](lv_observer_t* obs, lv_subject_t* subj) {
            auto* c = static_cast<detail::LambdaObserverContext<Panel, DecayedHandler>*>(
                lv_observer_get_user_data(obs));
            if (c && c->panel) {
                const char* str = lv_subject_get_string(subj);
                if (!str)
                    str = "";
                c->handler(c->panel, str);
            }
        },
        ctx);
}

/**
 * @brief Create async string observer with value and update handlers
 *
 * @tparam Panel Panel class type
 * @tparam ValueHandler Callable: void(Panel*, const char*)
 * @tparam UpdateHandler Callable: void(Panel*)
 */
template <typename Panel, typename ValueHandler, typename UpdateHandler>
ObserverGuard observe_string_async(lv_subject_t* subject, Panel* panel,
                                   ValueHandler&& value_handler, UpdateHandler&& update_handler) {
    if (!subject || !panel) {
        return ObserverGuard();
    }

    using DecayedValueHandler = std::decay_t<ValueHandler>;
    using DecayedUpdateHandler = std::decay_t<UpdateHandler>;
    auto* ctx =
        new detail::AsyncLambdaObserverContext<Panel, DecayedValueHandler, DecayedUpdateHandler>{
            panel, std::forward<ValueHandler>(value_handler),
            std::forward<UpdateHandler>(update_handler)};

    return ObserverGuard(
        subject,
        [](lv_observer_t* obs, lv_subject_t* subj) {
            auto* c = static_cast<detail::AsyncLambdaObserverContext<Panel, DecayedValueHandler,
                                                                     DecayedUpdateHandler>*>(
                lv_observer_get_user_data(obs));
            if (c && c->panel) {
                const char* str = lv_subject_get_string(subj);
                if (!str)
                    str = "";
                c->value_handler(c->panel, str);

                // Schedule async update
                helix::ui::async_call(
                    [](void* user_data) {
                        auto* ctx = static_cast<detail::AsyncLambdaObserverContext<
                            Panel, DecayedValueHandler, DecayedUpdateHandler>*>(user_data);
                        if (ctx && ctx->panel) {
                            ctx->update_handler(ctx->panel);
                        }
                    },
                    c);
            }
        },
        ctx);
}

// ============================================================================
// Domain-Specific Observer Helpers
// ============================================================================

/**
 * @brief Create connection state observer that triggers on CONNECTED
 *
 * Common pattern used in 6+ files to perform actions when connection is established.
 * Only calls the handler when ConnectionState::CONNECTED is reached.
 *
 * @tparam Panel Panel class type
 * @tparam OnConnected Callable: void(Panel*)
 * @param subject Connection state subject (printer_connection_state)
 * @param panel Panel instance
 * @param on_connected Lambda called when state becomes CONNECTED
 * @return ObserverGuard for RAII cleanup
 */
template <typename Panel, typename OnConnected>
ObserverGuard observe_connection_state(lv_subject_t* subject, Panel* panel,
                                       OnConnected&& on_connected) {
    return observe_int_sync<Panel>(
        subject, panel,
        [on_connected = std::forward<OnConnected>(on_connected)](Panel* p, int state) {
            if (state == static_cast<int>(ConnectionState::CONNECTED)) {
                on_connected(p);
            }
        });
}

/**
 * @brief Create print state observer with typed PrintJobState
 *
 * Common pattern used in 4+ files to react to print state changes.
 * Automatically casts the int subject value to PrintJobState enum.
 *
 * @tparam Panel Panel class type
 * @tparam Handler Callable: void(Panel*, PrintJobState)
 * @param subject Print state enum subject (print_state_enum)
 * @param panel Panel instance
 * @param handler Lambda called with panel and typed PrintJobState
 * @return ObserverGuard for RAII cleanup
 */
template <typename Panel, typename Handler>
ObserverGuard observe_print_state(lv_subject_t* subject, Panel* panel, Handler&& handler) {
    return observe_int_sync<Panel>(
        subject, panel, [handler = std::forward<Handler>(handler)](Panel* p, int state_int) {
            handler(p, static_cast<PrintJobState>(state_int));
        });
}

} // namespace helix::ui
