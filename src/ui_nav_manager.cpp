// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_nav_manager.h"

#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_fonts.h"
#include "ui_panel_base.h"
#include "ui_status_bar.h"
#include "ui_theme.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "moonraker_client.h" // For ConnectionState enum
#include "overlay_base.h"
#include "printer_state.h" // For KlippyState enum
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

NavigationManager& NavigationManager::instance() {
    static NavigationManager instance;
    return instance;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

const char* NavigationManager::panel_id_to_name(ui_panel_id_t id) {
    static const char* names[] = {"home_panel",     "print_select_panel", "controls_panel",
                                  "filament_panel", "settings_panel",     "advanced_panel"};
    if (id < UI_PANEL_COUNT) {
        return names[id];
    }
    return "unknown_panel";
}

bool NavigationManager::panel_requires_connection(ui_panel_id_t panel) {
    return panel == UI_PANEL_CONTROLS || panel == UI_PANEL_FILAMENT;
}

bool NavigationManager::is_printer_connected() const {
    auto* subject = get_printer_state().get_printer_connection_state_subject();
    return lv_subject_get_int(subject) == 2;
}

bool NavigationManager::is_klippy_ready() const {
    auto* subject = get_printer_state().get_klippy_state_subject();
    return lv_subject_get_int(subject) == 0; // KlippyState::READY
}

void NavigationManager::clear_overlay_stack() {
    // Hide all overlay panels immediately (no animation for connection loss)
    while (panel_stack_.size() > 1) {
        lv_obj_t* overlay = panel_stack_.back();
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        // Reset transform and opacity for potential reuse
        lv_obj_set_style_translate_x(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);

        // Clean up dynamic backdrop for this overlay (if one was created)
        auto backdrop_it = overlay_backdrops_.find(overlay);
        if (backdrop_it != overlay_backdrops_.end()) {
            lv_obj_del(backdrop_it->second);
            overlay_backdrops_.erase(backdrop_it);
        }

        panel_stack_.pop_back();
        spdlog::trace("[NavigationManager] Cleared overlay {} from stack", (void*)overlay);
    }

    // Hide primary backdrop
    if (overlay_backdrop_) {
        ui_status_bar_set_backdrop_visible(false);
    }

    spdlog::debug("[NavigationManager] Overlay stack cleared (connection gating)");
}

// ============================================================================
// ANIMATION HELPERS
// ============================================================================

void NavigationManager::overlay_slide_out_complete_cb(lv_anim_t* anim) {
    lv_obj_t* panel = static_cast<lv_obj_t*>(anim->var);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    // Reset transform and opacity for potential reuse
    lv_obj_set_style_translate_x(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    spdlog::debug("[NavigationManager] Overlay slide+fade-out complete, panel {} hidden",
                  (void*)panel);

    // Invoke close callback if registered (AFTER animation completes, before any deletion)
    auto& mgr = NavigationManager::instance();
    auto it = mgr.overlay_close_callbacks_.find(panel);
    if (it != mgr.overlay_close_callbacks_.end()) {
        spdlog::debug("[NavigationManager] Invoking close callback for overlay {}", (void*)panel);
        auto callback = std::move(it->second);
        mgr.overlay_close_callbacks_.erase(it);
        callback(); // Call after erasing to allow re-registration
    }

    // Lifecycle: Activate what's now visible after animation completes
    // Stack was already modified in go_back(), so check what's now at top
    if (mgr.panel_stack_.size() == 1) {
        // Back to main panel - activate it
        if (mgr.panel_instances_[mgr.active_panel_]) {
            spdlog::trace("[NavigationManager] Activating main panel {} after overlay closed",
                          static_cast<int>(mgr.active_panel_));
            mgr.panel_instances_[mgr.active_panel_]->on_activate();
        }
    } else if (mgr.panel_stack_.size() > 1) {
        // Back to previous overlay - activate it
        lv_obj_t* now_visible = mgr.panel_stack_.back();
        auto overlay_it = mgr.overlay_instances_.find(now_visible);
        if (overlay_it != mgr.overlay_instances_.end() && overlay_it->second) {
            spdlog::trace("[NavigationManager] Activating previous overlay {}",
                          overlay_it->second->get_name());
            overlay_it->second->on_activate();
        }
    }
}

void NavigationManager::overlay_animate_slide_in(lv_obj_t* panel) {
    int32_t panel_width = lv_obj_get_width(panel);
    if (panel_width <= 0) {
        panel_width = OVERLAY_SLIDE_OFFSET;
    }

    // Skip animation if disabled - show panel in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_set_style_translate_x(panel, 0, LV_PART_MAIN);
        lv_obj_set_style_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[NavigationManager] Animations disabled - showing overlay instantly");
        return;
    }

    // Set initial state: off-screen and transparent
    lv_obj_set_style_translate_x(panel, panel_width, LV_PART_MAIN);
    lv_obj_set_style_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);

    // Slide animation: translate from right to final position
    lv_anim_t slide_anim;
    lv_anim_init(&slide_anim);
    lv_anim_set_var(&slide_anim, panel);
    lv_anim_set_values(&slide_anim, panel_width, 0);
    lv_anim_set_duration(&slide_anim, OVERLAY_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&slide_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&slide_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&slide_anim);

    // Fade animation: opacity from transparent to opaque (runs simultaneously)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, panel);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, OVERLAY_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    spdlog::debug("[NavigationManager] Started slide+fade-in animation for panel {} (width={})",
                  (void*)panel, panel_width);
}

void NavigationManager::overlay_animate_slide_out(lv_obj_t* panel) {
    // Disable clicks immediately to prevent interaction during animation
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Skip animation if disabled - hide panel immediately and invoke callback
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        // Reset transform and opacity for potential reuse
        lv_obj_set_style_translate_x(panel, 0, LV_PART_MAIN);
        lv_obj_set_style_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[NavigationManager] Animations disabled - hiding overlay instantly");

        // Invoke close callback if registered
        auto& mgr = NavigationManager::instance();
        auto it = mgr.overlay_close_callbacks_.find(panel);
        if (it != mgr.overlay_close_callbacks_.end()) {
            spdlog::debug("[NavigationManager] Invoking close callback for overlay {}",
                          (void*)panel);
            auto callback = std::move(it->second);
            mgr.overlay_close_callbacks_.erase(it);
            callback();
        }

        // Lifecycle: Activate what's now visible (same logic as animation callback)
        if (mgr.panel_stack_.size() == 1) {
            if (mgr.panel_instances_[mgr.active_panel_]) {
                spdlog::trace("[NavigationManager] Activating main panel {} after overlay closed",
                              static_cast<int>(mgr.active_panel_));
                mgr.panel_instances_[mgr.active_panel_]->on_activate();
            }
        } else if (mgr.panel_stack_.size() > 1) {
            lv_obj_t* now_visible = mgr.panel_stack_.back();
            auto overlay_it = mgr.overlay_instances_.find(now_visible);
            if (overlay_it != mgr.overlay_instances_.end() && overlay_it->second) {
                spdlog::trace("[NavigationManager] Activating previous overlay {}",
                              overlay_it->second->get_name());
                overlay_it->second->on_activate();
            }
        }
        return;
    }

    int32_t panel_width = lv_obj_get_width(panel);
    if (panel_width <= 0) {
        panel_width = OVERLAY_SLIDE_OFFSET;
    }

    // Slide animation: translate to off-screen right
    lv_anim_t slide_anim;
    lv_anim_init(&slide_anim);
    lv_anim_set_var(&slide_anim, panel);
    lv_anim_set_values(&slide_anim, 0, panel_width);
    lv_anim_set_duration(&slide_anim, OVERLAY_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&slide_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&slide_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_set_completed_cb(&slide_anim, overlay_slide_out_complete_cb);
    lv_anim_start(&slide_anim);

    // Fade animation: opacity from opaque to transparent (runs simultaneously)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, panel);
    lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_anim, OVERLAY_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    spdlog::debug("[NavigationManager] Started slide+fade-out animation for panel {} (width={})",
                  (void*)panel, panel_width);
}

// ============================================================================
// OBSERVER CALLBACKS
// ============================================================================

void NavigationManager::active_panel_observer_cb([[maybe_unused]] lv_observer_t* observer,
                                                 lv_subject_t* subject) {
    auto& mgr = NavigationManager::instance();
    int32_t new_active_panel = lv_subject_get_int(subject);

    // Show/hide panels if widgets are set
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (mgr.panel_widgets_[i]) {
            if (i == new_active_panel) {
                lv_obj_remove_flag(mgr.panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(mgr.panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void NavigationManager::connection_state_observer_cb([[maybe_unused]] lv_observer_t* observer,
                                                     lv_subject_t* subject) {
    auto& mgr = NavigationManager::instance();
    int state = lv_subject_get_int(subject);
    bool was_connected =
        (mgr.previous_connection_state_ == static_cast<int>(ConnectionState::CONNECTED));
    bool is_connected = (state == static_cast<int>(ConnectionState::CONNECTED));

    // Only redirect if we were previously connected and are now disconnected
    if (was_connected && !is_connected && panel_requires_connection(mgr.active_panel_)) {
        spdlog::info("[NavigationManager] Connection lost on panel {} - navigating to home",
                     static_cast<int>(mgr.active_panel_));

        mgr.clear_overlay_stack();
        mgr.set_active(UI_PANEL_HOME);
    }

    mgr.previous_connection_state_ = state;
}

void NavigationManager::klippy_state_observer_cb([[maybe_unused]] lv_observer_t* observer,
                                                 lv_subject_t* subject) {
    auto& mgr = NavigationManager::instance();
    int state = lv_subject_get_int(subject);
    bool was_ready = (mgr.previous_klippy_state_ == static_cast<int>(KlippyState::READY));
    bool is_ready = (state == static_cast<int>(KlippyState::READY));

    // Redirect to home if klippy enters non-READY state (SHUTDOWN/ERROR) while on restricted panel
    if (was_ready && !is_ready && panel_requires_connection(mgr.active_panel_)) {
        const char* state_name = (state == static_cast<int>(KlippyState::SHUTDOWN)) ? "SHUTDOWN"
                                 : (state == static_cast<int>(KlippyState::ERROR))  ? "ERROR"
                                                                                    : "non-READY";
        spdlog::info("[NavigationManager] Klippy {} on panel {} - navigating to home", state_name,
                     static_cast<int>(mgr.active_panel_));

        mgr.clear_overlay_stack();
        mgr.set_active(UI_PANEL_HOME);
    }

    mgr.previous_klippy_state_ = state;
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================

void NavigationManager::backdrop_click_event_cb(lv_event_t* e) {
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* current = static_cast<lv_obj_t*>(lv_event_get_current_target(e));

    // Only respond if click was directly on backdrop (not bubbled from child)
    if (target != current) {
        return;
    }

    auto& mgr = NavigationManager::instance();

    // Only process if there's an overlay to close (stack > 1 means overlays exist)
    if (mgr.panel_stack_.size() <= 1) {
        return;
    }

    // Get click position to check if it's in the navbar area
    lv_point_t click_point;
    lv_indev_get_point(lv_indev_active(), &click_point);

    // Check if click is in navbar area and find which button was clicked
    if (mgr.navbar_widget_) {
        int32_t navbar_width = lv_obj_get_width(mgr.navbar_widget_);

        if (click_point.x < navbar_width) {
            // Click is in navbar area - find which button and trigger navigation
            const char* button_names[] = {"nav_btn_home",     "nav_btn_print_select",
                                          "nav_btn_controls", "nav_btn_filament",
                                          "nav_btn_settings", "nav_btn_advanced"};

            for (int i = 0; i < UI_PANEL_COUNT; i++) {
                lv_obj_t* btn = lv_obj_find_by_name(mgr.navbar_widget_, button_names[i]);
                if (!btn) {
                    continue;
                }

                // Check if click point is inside this button
                lv_area_t btn_area;
                lv_obj_get_coords(btn, &btn_area);

                // Simple bounds check (point in rectangle)
                if (click_point.x >= btn_area.x1 && click_point.x <= btn_area.x2 &&
                    click_point.y >= btn_area.y1 && click_point.y <= btn_area.y2) {
                    spdlog::debug(
                        "[NavigationManager] Backdrop click forwarded to navbar button {}", i);
                    // Simulate the navbar button click by sending a clicked event
                    lv_obj_send_event(btn, LV_EVENT_CLICKED, nullptr);
                    return;
                }
            }

            // Click was in navbar area but not on a button - just close overlay
            spdlog::debug("[NavigationManager] Backdrop clicked in navbar area (no button hit)");
        }
    }

    // Regular backdrop click - close topmost overlay
    spdlog::debug("[NavigationManager] Backdrop clicked, closing topmost overlay");
    mgr.go_back();
}

void NavigationManager::nav_button_clicked_cb(lv_event_t* event) {
    LVGL_SAFE_EVENT_CB_BEGIN("nav_button_clicked_cb");

    auto& mgr = NavigationManager::instance();
    lv_event_code_t code = lv_event_get_code(event);
    int panel_id = (int)(uintptr_t)lv_event_get_user_data(event);

    spdlog::info("[NavigationManager] nav_button_clicked_cb fired: code={}, panel_id={}, "
                 "active_panel={}",
                 static_cast<int>(code), panel_id, static_cast<int>(mgr.active_panel_));

    if (code == LV_EVENT_CLICKED) {
        // Skip if already on this panel
        if (panel_id == static_cast<int>(mgr.active_panel_)) {
            spdlog::info("[NavigationManager] Skipping - already on panel {}", panel_id);
            return;
        }

        // Block navigation to connection-required panels when disconnected or klippy not ready
        if (panel_requires_connection(static_cast<ui_panel_id_t>(panel_id))) {
            if (!mgr.is_printer_connected()) {
                spdlog::info("[NavigationManager] Navigation to panel {} blocked - not connected",
                             panel_id);
                return;
            }
            if (!mgr.is_klippy_ready()) {
                spdlog::info(
                    "[NavigationManager] Navigation to panel {} blocked - klippy not ready",
                    panel_id);
                return;
            }
        }

        // Queue for REFR_START - guarantees we never modify widgets during render phase
        spdlog::info("[NavigationManager] Queuing switch to panel {}", panel_id);
        ui_queue_update(
            [panel_id]() { NavigationManager::instance().switch_to_panel_impl(panel_id); });
    }

    LVGL_SAFE_EVENT_CB_END();
}

void NavigationManager::switch_to_panel_impl(int panel_id) {
    spdlog::debug("[NavigationManager] switch_to_panel_impl executing for panel {}", panel_id);

    // Hide ALL visible overlay panels
    lv_obj_t* screen = lv_screen_active();
    if (screen) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
            lv_obj_t* child = lv_obj_get_child(screen, static_cast<int32_t>(i));
            if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
                continue;
            }

            if (child == app_layout_widget_) {
                continue;
            }

            bool is_main_panel = false;
            for (int j = 0; j < UI_PANEL_COUNT; j++) {
                if (panel_widgets_[j] == child) {
                    is_main_panel = true;
                    break;
                }
            }

            if (!is_main_panel) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                // Reset transform and opacity for potential reuse
                lv_obj_set_style_translate_x(child, 0, LV_PART_MAIN);
                lv_obj_set_style_opa(child, LV_OPA_COVER, LV_PART_MAIN);
                spdlog::trace("[NavigationManager] Hiding overlay panel {} (nav button clicked)",
                              (void*)child);
            }
        }
    }

    // Hide all main panels
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (panel_widgets_[i]) {
            lv_obj_add_flag(panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Invoke close callbacks and clean up dynamic backdrops for any overlays being cleared
    // (e.g., AMS panel needs to destroy its UI to free memory)
    for (lv_obj_t* panel : panel_stack_) {
        // Invoke close callback if registered
        auto it = overlay_close_callbacks_.find(panel);
        if (it != overlay_close_callbacks_.end()) {
            auto callback = std::move(it->second);
            overlay_close_callbacks_.erase(it);
            spdlog::debug("[NavigationManager] Invoking close callback for panel {} (navbar)",
                          (void*)panel);
            callback();
        }

        // Clean up dynamic backdrop for this overlay (if one was created)
        auto backdrop_it = overlay_backdrops_.find(panel);
        if (backdrop_it != overlay_backdrops_.end()) {
            lv_obj_del(backdrop_it->second);
            overlay_backdrops_.erase(backdrop_it);
        }
    }

    // Clear panel stack
    panel_stack_.clear();
    spdlog::trace("[NavigationManager] Panel stack cleared (nav button clicked)");

    // Hide primary backdrop since all overlays are being cleared
    if (overlay_backdrop_) {
        ui_status_bar_set_backdrop_visible(false);
    }

    // Show the clicked panel
    lv_obj_t* new_panel = panel_widgets_[panel_id];
    if (new_panel) {
        lv_obj_remove_flag(new_panel, LV_OBJ_FLAG_HIDDEN);
        panel_stack_.push_back(new_panel);
        spdlog::trace("[NavigationManager] Showing panel {} (stack depth: {})", (void*)new_panel,
                      panel_stack_.size());
    }

    spdlog::info("[NavigationManager] Switched to panel {}", panel_id);
    set_active((ui_panel_id_t)panel_id);
}

// ============================================================================
// NAVIGATION MANAGER IMPLEMENTATION
// ============================================================================

void NavigationManager::init() {
    if (subjects_initialized_) {
        spdlog::warn("[NavigationManager] Subjects already initialized");
        return;
    }

    spdlog::debug("[NavigationManager] Initializing navigation reactive subjects...");

    lv_subject_init_int(&active_panel_subject_, UI_PANEL_HOME);
    lv_xml_register_subject(NULL, "active_panel", &active_panel_subject_);

    active_panel_observer_ =
        ObserverGuard(&active_panel_subject_, active_panel_observer_cb, nullptr);

    subjects_initialized_ = true;
    spdlog::debug("[NavigationManager] Navigation subjects initialized successfully");
}

void NavigationManager::init_overlay_backdrop(lv_obj_t* screen) {
    if (!screen) {
        spdlog::error("[NavigationManager] NULL screen provided to init_overlay_backdrop");
        return;
    }

    if (overlay_backdrop_) {
        spdlog::warn("[NavigationManager] Overlay backdrop already initialized");
        return;
    }

    overlay_backdrop_ = static_cast<lv_obj_t*>(lv_xml_create(screen, "overlay_backdrop", nullptr));
    if (!overlay_backdrop_) {
        spdlog::error("[NavigationManager] Failed to create overlay_backdrop from XML");
        return;
    }

    // Wire up click handler to close topmost overlay when backdrop is clicked
    lv_obj_add_event_cb(overlay_backdrop_, backdrop_click_event_cb, LV_EVENT_CLICKED, nullptr);

    spdlog::debug("[NavigationManager] Overlay backdrop created from XML successfully");
}

void NavigationManager::set_app_layout(lv_obj_t* app_layout) {
    app_layout_widget_ = app_layout;
    spdlog::debug("[NavigationManager] App layout widget registered");
}

void NavigationManager::wire_events(lv_obj_t* navbar) {
    if (!navbar) {
        spdlog::error("[NavigationManager] NULL navbar provided to wire_events");
        return;
    }

    if (!subjects_initialized_) {
        spdlog::error("[NavigationManager] Subjects not initialized! Call init() first!");
        return;
    }

    // Store navbar reference for z-order management when showing overlays
    navbar_widget_ = navbar;

    lv_obj_remove_flag(navbar, LV_OBJ_FLAG_CLICKABLE);

    const char* button_names[] = {"nav_btn_home",     "nav_btn_print_select", "nav_btn_controls",
                                  "nav_btn_filament", "nav_btn_settings",     "nav_btn_advanced"};

    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        lv_obj_t* btn = lv_obj_find_by_name(navbar, button_names[i]);

        if (!btn) {
            spdlog::debug("[NavigationManager] Nav button {} not found (may be intentional)", i);
            continue;
        }

        lv_obj_add_event_cb(btn, nav_button_clicked_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }

    // Register connection state observer for redirect on disconnect
    connection_state_observer_ =
        ObserverGuard(get_printer_state().get_printer_connection_state_subject(),
                      connection_state_observer_cb, nullptr);

    // Register klippy state observer for redirect on SHUTDOWN/ERROR
    klippy_state_observer_ = ObserverGuard(get_printer_state().get_klippy_state_subject(),
                                           klippy_state_observer_cb, nullptr);

    spdlog::debug(
        "[NavigationManager] Navigation button events wired (with connection/klippy gating)");
}

void NavigationManager::wire_status_icons(lv_obj_t* navbar) {
    if (!navbar) {
        spdlog::error("[NavigationManager] NULL navbar provided to wire_status_icons");
        return;
    }

    const char* button_names[] = {"status_btn_printer", "status_btn_network",
                                  "status_notification_icon"};
    const char* icon_names[] = {"status_printer_icon", "status_network_icon",
                                "status_notification_icon"};
    const int status_icon_count = 3;

    for (int i = 0; i < status_icon_count; i++) {
        lv_obj_t* btn = lv_obj_find_by_name(navbar, button_names[i]);
        lv_obj_t* icon_widget = lv_obj_find_by_name(navbar, icon_names[i]);

        if (!btn || !icon_widget) {
            spdlog::warn("[NavigationManager] Status icon {}: btn={}, icon={} (may not exist yet)",
                         button_names[i], (void*)btn, (void*)icon_widget);
            continue;
        }

        lv_obj_add_flag(icon_widget, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(icon_widget, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        spdlog::trace("[NavigationManager] Status icon {} wired", button_names[i]);
    }
}

void NavigationManager::set_active(ui_panel_id_t panel_id) {
    if (panel_id >= UI_PANEL_COUNT) {
        spdlog::error("[NavigationManager] Invalid panel ID: {}", (int)panel_id);
        return;
    }

    if (panel_id == active_panel_) {
        return;
    }

    ui_panel_id_t old_panel = active_panel_;

    // Update panel stack
    if (panel_widgets_[panel_id]) {
        panel_stack_.clear();
        panel_stack_.push_back(panel_widgets_[panel_id]);
        spdlog::trace("[NavigationManager] Panel stack updated to panel {} (set_active)",
                      static_cast<int>(panel_id));
    }

    // Call on_deactivate() BEFORE state update
    if (panel_instances_[old_panel]) {
        spdlog::trace("[NavigationManager] Calling on_deactivate() for panel {}",
                      static_cast<int>(old_panel));
        panel_instances_[old_panel]->on_deactivate();
    }

    // Update state
    lv_subject_set_int(&active_panel_subject_, panel_id);
    active_panel_ = panel_id;

    // Call on_activate() AFTER state update
    if (panel_instances_[panel_id]) {
        spdlog::trace("[NavigationManager] Calling on_activate() for panel {}",
                      static_cast<int>(panel_id));
        panel_instances_[panel_id]->on_activate();
    }
}

ui_panel_id_t NavigationManager::get_active() const {
    return active_panel_;
}

void NavigationManager::set_panels(lv_obj_t** panels) {
    if (!panels) {
        spdlog::error("[NavigationManager] NULL panels array provided");
        return;
    }

    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        panel_widgets_[i] = panels[i];
    }

    // Hide all panels except active one
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (panel_widgets_[i]) {
            if (i == active_panel_) {
                lv_obj_remove_flag(panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Initialize panel stack
    panel_stack_.clear();
    if (panel_widgets_[active_panel_]) {
        panel_stack_.push_back(panel_widgets_[active_panel_]);
        spdlog::debug("[NavigationManager] Panel stack initialized with active panel {}",
                      (void*)panel_widgets_[active_panel_]);
    }

    spdlog::debug("[NavigationManager] Panel widgets registered for show/hide management");
}

void NavigationManager::register_panel_instance(ui_panel_id_t id, PanelBase* panel) {
    if (id >= UI_PANEL_COUNT) {
        spdlog::error("[NavigationManager] Invalid panel ID for registration: {}",
                      static_cast<int>(id));
        return;
    }
    panel_instances_[id] = panel;
    spdlog::debug("[NavigationManager] Registered panel instance for ID {}", static_cast<int>(id));
}

void NavigationManager::register_overlay_instance(lv_obj_t* widget, OverlayBase* overlay) {
    if (!widget || !overlay) {
        spdlog::error("[NavigationManager] Cannot register NULL overlay instance");
        return;
    }
    overlay_instances_[widget] = overlay;
    spdlog::debug("[NavigationManager] Registered overlay instance {} for widget {}",
                  overlay->get_name(), (void*)widget);
}

void NavigationManager::unregister_overlay_instance(lv_obj_t* widget) {
    auto it = overlay_instances_.find(widget);
    if (it != overlay_instances_.end()) {
        spdlog::debug("[NavigationManager] Unregistered overlay instance for widget {}",
                      (void*)widget);
        overlay_instances_.erase(it);
    }
}

void NavigationManager::push_overlay(lv_obj_t* overlay_panel, bool hide_previous) {
    if (!overlay_panel) {
        spdlog::error("[NavigationManager] Cannot push NULL overlay panel");
        return;
    }

    // Always queue - this is the safest pattern for overlay operations
    // which can be triggered from various contexts (events, observers, etc.)
    ui_queue_update([overlay_panel, hide_previous]() {
        auto& mgr = NavigationManager::instance();

        // Check for duplicate push
        if (std::find(mgr.panel_stack_.begin(), mgr.panel_stack_.end(), overlay_panel) !=
            mgr.panel_stack_.end()) {
            spdlog::warn("[NavigationManager] Overlay {} already in stack, ignoring duplicate push",
                         (void*)overlay_panel);
            return;
        }

        bool is_first_overlay = (mgr.panel_stack_.size() == 1);

        // Lifecycle: Deactivate what's currently visible before showing new overlay
        if (is_first_overlay) {
            // Deactivate main panel when first overlay covers it
            if (mgr.panel_instances_[mgr.active_panel_]) {
                spdlog::trace("[NavigationManager] Deactivating main panel {} for overlay",
                              static_cast<int>(mgr.active_panel_));
                mgr.panel_instances_[mgr.active_panel_]->on_deactivate();
            }
        } else {
            // Deactivate previous overlay if stacking
            lv_obj_t* prev_overlay = mgr.panel_stack_.back();
            auto it = mgr.overlay_instances_.find(prev_overlay);
            if (it != mgr.overlay_instances_.end() && it->second) {
                spdlog::trace("[NavigationManager] Deactivating previous overlay {}",
                              it->second->get_name());
                it->second->on_deactivate();
            }
        }

        // Optionally hide current top panel
        if (hide_previous && !mgr.panel_stack_.empty()) {
            lv_obj_t* current_top = mgr.panel_stack_.back();
            lv_obj_add_flag(current_top, LV_OBJ_FLAG_HIDDEN);
        }

        // Create backdrop
        lv_obj_t* screen = lv_obj_get_screen(overlay_panel);
        if (screen) {
            if (is_first_overlay && mgr.overlay_backdrop_) {
                ui_status_bar_set_backdrop_visible(true);
                lv_obj_move_foreground(mgr.overlay_backdrop_);
            } else if (!is_first_overlay) {
                lv_obj_t* backdrop =
                    static_cast<lv_obj_t*>(lv_xml_create(screen, "overlay_backdrop", nullptr));
                if (backdrop) {
                    mgr.overlay_backdrops_[overlay_panel] = backdrop;
                    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_move_foreground(backdrop);
                    lv_obj_add_event_cb(backdrop, backdrop_click_event_cb, LV_EVENT_CLICKED,
                                        nullptr);
                }
            }
        }

        // Show overlay
        lv_obj_remove_flag(overlay_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(overlay_panel);
        mgr.panel_stack_.push_back(overlay_panel);
        mgr.overlay_animate_slide_in(overlay_panel);

        // Lifecycle: Activate new overlay
        auto it = mgr.overlay_instances_.find(overlay_panel);
        if (it == mgr.overlay_instances_.end()) {
            spdlog::warn("[NavigationManager] Overlay {} pushed without lifecycle registration",
                         (void*)overlay_panel);
        } else if (it->second) {
            spdlog::trace("[NavigationManager] Activating overlay {}", it->second->get_name());
            it->second->on_activate();
        }

        spdlog::debug("[NavigationManager] Pushed overlay {} (stack: {})", (void*)overlay_panel,
                      mgr.panel_stack_.size());
    });
}

void NavigationManager::register_overlay_close_callback(lv_obj_t* overlay_panel,
                                                        OverlayCloseCallback callback) {
    if (!overlay_panel || !callback) {
        return;
    }
    overlay_close_callbacks_[overlay_panel] = std::move(callback);
    spdlog::debug("[NavigationManager] Registered close callback for overlay {}",
                  (void*)overlay_panel);
}

void NavigationManager::unregister_overlay_close_callback(lv_obj_t* overlay_panel) {
    auto it = overlay_close_callbacks_.find(overlay_panel);
    if (it != overlay_close_callbacks_.end()) {
        overlay_close_callbacks_.erase(it);
        spdlog::debug("[NavigationManager] Unregistered close callback for overlay {}",
                      (void*)overlay_panel);
    }
}

bool NavigationManager::go_back() {
    ui_queue_update([]() {
        auto& mgr = NavigationManager::instance();
        spdlog::debug("[NavigationManager] go_back executing, stack depth: {}",
                      mgr.panel_stack_.size());

        lv_obj_t* current_top = mgr.panel_stack_.empty() ? nullptr : mgr.panel_stack_.back();

        // Check if current top is an overlay
        bool is_overlay = false;
        if (current_top) {
            is_overlay = true;
            for (int j = 0; j < UI_PANEL_COUNT; j++) {
                if (mgr.panel_widgets_[j] == current_top) {
                    is_overlay = false;
                    break;
                }
            }
        }

        // Lifecycle: Deactivate the closing overlay before animation
        if (is_overlay && current_top) {
            auto it = mgr.overlay_instances_.find(current_top);
            if (it != mgr.overlay_instances_.end() && it->second) {
                spdlog::trace("[NavigationManager] Deactivating closing overlay {}",
                              it->second->get_name());
                it->second->on_deactivate();
            }
        }

        // Animate slide-out if overlay
        if (is_overlay && current_top) {
            mgr.overlay_animate_slide_out(current_top);
        }

        // Hide stale overlays
        lv_obj_t* screen = lv_screen_active();
        if (screen) {
            for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
                lv_obj_t* child = lv_obj_get_child(screen, static_cast<int32_t>(i));
                if (child == mgr.app_layout_widget_ || child == mgr.overlay_backdrop_ ||
                    child == current_top) {
                    continue;
                }
                bool is_main = false;
                for (int j = 0; j < UI_PANEL_COUNT; j++) {
                    if (mgr.panel_widgets_[j] == child) {
                        is_main = true;
                        break;
                    }
                }
                if (!is_main && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_style_translate_x(child, 0, LV_PART_MAIN);
                    lv_obj_set_style_opa(child, LV_OPA_COVER, LV_PART_MAIN);
                }
            }
        }

        // Pop and clean up backdrop
        if (!mgr.panel_stack_.empty()) {
            lv_obj_t* popped = mgr.panel_stack_.back();
            mgr.panel_stack_.pop_back();
            auto it = mgr.overlay_backdrops_.find(popped);
            if (it != mgr.overlay_backdrops_.end()) {
                lv_obj_del(it->second);
                mgr.overlay_backdrops_.erase(it);
            }
        }

        // Hide backdrop if no more overlays
        if (mgr.panel_stack_.size() <= 1 && mgr.overlay_backdrop_) {
            ui_status_bar_set_backdrop_visible(false);
        }

        // Fallback to home if empty
        if (mgr.panel_stack_.empty()) {
            for (int i = 0; i < UI_PANEL_COUNT; i++) {
                if (mgr.panel_widgets_[i])
                    lv_obj_add_flag(mgr.panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            }
            if (mgr.panel_widgets_[UI_PANEL_HOME]) {
                lv_obj_remove_flag(mgr.panel_widgets_[UI_PANEL_HOME], LV_OBJ_FLAG_HIDDEN);
                mgr.panel_stack_.push_back(mgr.panel_widgets_[UI_PANEL_HOME]);
                mgr.active_panel_ = UI_PANEL_HOME;
                lv_subject_set_int(&mgr.active_panel_subject_, UI_PANEL_HOME);
            }
            return;
        }

        // Show previous panel
        lv_obj_t* prev = mgr.panel_stack_.back();
        for (int i = 0; i < UI_PANEL_COUNT; i++) {
            if (mgr.panel_widgets_[i] == prev) {
                for (int j = 0; j < UI_PANEL_COUNT; j++) {
                    if (j != i && mgr.panel_widgets_[j])
                        lv_obj_add_flag(mgr.panel_widgets_[j], LV_OBJ_FLAG_HIDDEN);
                }
                mgr.active_panel_ = static_cast<ui_panel_id_t>(i);
                lv_subject_set_int(&mgr.active_panel_subject_, i);
                break;
            }
        }
        lv_obj_remove_flag(prev, LV_OBJ_FLAG_HIDDEN);
    });
    return true;
}

bool NavigationManager::is_panel_in_stack(lv_obj_t* panel) const {
    if (!panel) {
        return false;
    }
    return std::find(panel_stack_.begin(), panel_stack_.end(), panel) != panel_stack_.end();
}

// ============================================================================
// LEGACY API (forwards to NavigationManager)
// ============================================================================

void ui_nav_init() {
    NavigationManager::instance().init();
}

void ui_nav_init_overlay_backdrop(lv_obj_t* screen) {
    NavigationManager::instance().init_overlay_backdrop(screen);
}

void ui_nav_set_app_layout(lv_obj_t* app_layout) {
    NavigationManager::instance().set_app_layout(app_layout);
}

void ui_nav_wire_events(lv_obj_t* navbar) {
    NavigationManager::instance().wire_events(navbar);
}

void ui_nav_wire_status_icons(lv_obj_t* navbar) {
    NavigationManager::instance().wire_status_icons(navbar);
}

void ui_nav_set_active(ui_panel_id_t panel_id) {
    NavigationManager::instance().set_active(panel_id);
}

ui_panel_id_t ui_nav_get_active() {
    return NavigationManager::instance().get_active();
}

void ui_nav_set_panels(lv_obj_t** panels) {
    NavigationManager::instance().set_panels(panels);
}

void ui_nav_push_overlay(lv_obj_t* overlay_panel, bool hide_previous) {
    NavigationManager::instance().push_overlay(overlay_panel, hide_previous);
}

bool ui_nav_go_back() {
    return NavigationManager::instance().go_back();
}
