// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_nav_manager.h"

#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_fonts.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#include "app_globals.h"

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

void NavigationManager::clear_overlay_stack() {
    // Hide all overlay panels immediately (no animation for connection loss)
    while (panel_stack_.size() > 1) {
        lv_obj_t* overlay = panel_stack_.back();
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_x(overlay, 0, LV_PART_MAIN);
        panel_stack_.pop_back();
        spdlog::trace("[NavigationManager] Cleared overlay {} from stack", (void*)overlay);
    }

    // Hide backdrop
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
    lv_obj_set_style_translate_x(panel, 0, LV_PART_MAIN);
    spdlog::debug("[NavigationManager] Overlay slide-out complete, panel {} hidden", (void*)panel);
}

void NavigationManager::overlay_animate_slide_in(lv_obj_t* panel) {
    int32_t panel_width = lv_obj_get_width(panel);
    if (panel_width <= 0) {
        panel_width = OVERLAY_SLIDE_OFFSET;
    }

    lv_obj_set_style_translate_x(panel, panel_width, LV_PART_MAIN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, panel);
    lv_anim_set_values(&anim, panel_width, 0);
    lv_anim_set_duration(&anim, OVERLAY_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&anim);

    spdlog::debug("[NavigationManager] Started slide-in animation for panel {} (width={})",
                  (void*)panel, panel_width);
}

void NavigationManager::overlay_animate_slide_out(lv_obj_t* panel) {
    int32_t panel_width = lv_obj_get_width(panel);
    if (panel_width <= 0) {
        panel_width = OVERLAY_SLIDE_OFFSET;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, panel);
    lv_anim_set_values(&anim, 0, panel_width);
    lv_anim_set_duration(&anim, OVERLAY_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_set_completed_cb(&anim, overlay_slide_out_complete_cb);
    lv_anim_start(&anim);

    spdlog::debug("[NavigationManager] Started slide-out animation for panel {} (width={})",
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
    bool was_connected = (mgr.previous_connection_state_ == 2);
    bool is_connected = (state == 2);

    // Only redirect if we were previously connected and are now disconnected
    if (was_connected && !is_connected && panel_requires_connection(mgr.active_panel_)) {
        spdlog::info("[NavigationManager] Connection lost on panel {} - navigating to home",
                     static_cast<int>(mgr.active_panel_));

        mgr.clear_overlay_stack();
        mgr.set_active(UI_PANEL_HOME);
    }

    mgr.previous_connection_state_ = state;
}

// ============================================================================
// EVENT CALLBACK
// ============================================================================

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

        // Block navigation to connection-required panels when disconnected
        if (panel_requires_connection(static_cast<ui_panel_id_t>(panel_id)) &&
            !mgr.is_printer_connected()) {
            spdlog::info("[NavigationManager] Navigation to panel {} blocked - not connected",
                         panel_id);
            return;
        }

        // Hide ALL visible overlay panels
        lv_obj_t* screen = lv_screen_active();
        if (screen) {
            for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
                lv_obj_t* child = lv_obj_get_child(screen, static_cast<int32_t>(i));
                if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
                    continue;
                }

                if (child == mgr.app_layout_widget_) {
                    continue;
                }

                bool is_main_panel = false;
                for (int j = 0; j < UI_PANEL_COUNT; j++) {
                    if (mgr.panel_widgets_[j] == child) {
                        is_main_panel = true;
                        break;
                    }
                }

                if (!is_main_panel) {
                    lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                    spdlog::trace(
                        "[NavigationManager] Hiding overlay panel {} (nav button clicked)",
                        (void*)child);
                }
            }
        }

        // Hide all main panels
        for (int i = 0; i < UI_PANEL_COUNT; i++) {
            if (mgr.panel_widgets_[i]) {
                lv_obj_add_flag(mgr.panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Clear panel stack
        mgr.panel_stack_.clear();
        spdlog::trace("[NavigationManager] Panel stack cleared (nav button clicked)");

        // Show the clicked panel
        lv_obj_t* new_panel = mgr.panel_widgets_[panel_id];
        if (new_panel) {
            lv_obj_remove_flag(new_panel, LV_OBJ_FLAG_HIDDEN);
            mgr.panel_stack_.push_back(new_panel);
            spdlog::trace("[NavigationManager] Showing panel {} (stack depth: {})",
                          (void*)new_panel, mgr.panel_stack_.size());
        }

        spdlog::info("[NavigationManager] Switching to panel {}", panel_id);
        mgr.set_active((ui_panel_id_t)panel_id);
    }

    LVGL_SAFE_EVENT_CB_END();
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

    // Register connection state observer
    connection_state_observer_ =
        ObserverGuard(get_printer_state().get_printer_connection_state_subject(),
                      connection_state_observer_cb, nullptr);

    spdlog::debug("[NavigationManager] Navigation button events wired (with connection gating)");
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

    // Update panel stack
    if (panel_widgets_[panel_id]) {
        panel_stack_.clear();
        panel_stack_.push_back(panel_widgets_[panel_id]);
        spdlog::trace("[NavigationManager] Panel stack updated to panel {} (set_active)",
                      static_cast<int>(panel_id));
    }

    lv_subject_set_int(&active_panel_subject_, panel_id);
    active_panel_ = panel_id;

    // Notify E-Stop overlay of panel change
    EmergencyStopOverlay::instance().on_panel_changed(panel_id_to_name(panel_id));
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

void NavigationManager::push_overlay(lv_obj_t* overlay_panel) {
    if (!overlay_panel) {
        spdlog::error("[NavigationManager] Cannot push NULL overlay panel");
        return;
    }

    // Notify E-Stop overlay of panel change
    const char* panel_name = lv_obj_get_name(overlay_panel);
    if (panel_name) {
        EmergencyStopOverlay::instance().on_panel_changed(panel_name);
    }

    bool is_first_overlay = (panel_stack_.size() == 1);

    // Hide current top panel
    if (!panel_stack_.empty()) {
        lv_obj_t* current_top = panel_stack_.back();
        lv_obj_add_flag(current_top, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[NavigationManager] Hiding current top panel {} (pushing overlay)",
                      (void*)current_top);
    }

    // Show the new overlay
    lv_obj_remove_flag(overlay_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_panel);
    panel_stack_.push_back(overlay_panel);

    // Show backdrop if first overlay
    if (is_first_overlay && overlay_backdrop_) {
        ui_status_bar_set_backdrop_visible(true);
        lv_obj_move_foreground(overlay_backdrop_);
        lv_obj_move_foreground(overlay_panel);

        spdlog::debug(
            "[NavigationManager] Showing overlay backdrop behind panel (stack was size 1, now {})",
            panel_stack_.size());
    }

    // Animate slide-in
    overlay_animate_slide_in(overlay_panel);

    spdlog::debug("[NavigationManager] Showing overlay panel {} (stack depth: {})",
                  (void*)overlay_panel, panel_stack_.size());
}

bool NavigationManager::go_back() {
    spdlog::debug("[NavigationManager] === go_back() called, stack depth: {} ===",
                  panel_stack_.size());

    lv_obj_t* current_top = panel_stack_.empty() ? nullptr : panel_stack_.back();

    // Check if current top is an overlay
    bool is_overlay = false;
    if (current_top) {
        is_overlay = true;
        for (int j = 0; j < UI_PANEL_COUNT; j++) {
            if (panel_widgets_[j] == current_top) {
                is_overlay = false;
                break;
            }
        }
    }

    // Animate slide-out if overlay
    if (is_overlay && current_top) {
        overlay_animate_slide_out(current_top);
    }

    // Hide any OTHER overlay panels
    lv_obj_t* screen = lv_screen_active();
    if (screen) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
            lv_obj_t* child = lv_obj_get_child(screen, static_cast<int32_t>(i));

            if (child == app_layout_widget_ || child == overlay_backdrop_ || child == current_top) {
                continue;
            }

            bool is_main_panel = false;
            for (int j = 0; j < UI_PANEL_COUNT; j++) {
                if (panel_widgets_[j] == child) {
                    is_main_panel = true;
                    break;
                }
            }

            if (!is_main_panel && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                spdlog::trace("[NavigationManager] Child {}: {} - HIDING stale overlay", i,
                              (void*)child);
            }
        }
    }

    // Pop current panel
    if (!panel_stack_.empty()) {
        panel_stack_.pop_back();
        spdlog::debug("[NavigationManager] Popped panel from stack (remaining depth: {})",
                      panel_stack_.size());
    }

    // Hide backdrop if no more overlays
    if (panel_stack_.size() <= 1 && overlay_backdrop_) {
        ui_status_bar_set_backdrop_visible(false);
        spdlog::debug("[NavigationManager] Hiding overlay backdrop (no more overlays)");
    }

    // Fallback to home if stack empty
    if (panel_stack_.empty()) {
        spdlog::debug(
            "[NavigationManager] Panel stack empty after pop, falling back to home panel");

        for (int i = 0; i < UI_PANEL_COUNT; i++) {
            if (panel_widgets_[i]) {
                lv_obj_add_flag(panel_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (panel_widgets_[UI_PANEL_HOME]) {
            lv_obj_remove_flag(panel_widgets_[UI_PANEL_HOME], LV_OBJ_FLAG_HIDDEN);
            panel_stack_.push_back(panel_widgets_[UI_PANEL_HOME]);
            active_panel_ = UI_PANEL_HOME;
            lv_subject_set_int(&active_panel_subject_, UI_PANEL_HOME);
            spdlog::debug("[NavigationManager] Fallback: showing home panel");
            return true;
        }

        spdlog::error("[NavigationManager] Cannot show home panel - widget not found!");
        return false;
    }

    // Show previous panel
    lv_obj_t* previous_panel = panel_stack_.back();

    bool is_main_panel = false;
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        if (panel_widgets_[i] == previous_panel) {
            is_main_panel = true;
            for (int j = 0; j < UI_PANEL_COUNT; j++) {
                if (j != i && panel_widgets_[j]) {
                    lv_obj_add_flag(panel_widgets_[j], LV_OBJ_FLAG_HIDDEN);
                }
            }
            active_panel_ = (ui_panel_id_t)i;
            lv_subject_set_int(&active_panel_subject_, i);
            spdlog::debug("[NavigationManager] Updated active panel to {}", i);
            break;
        }
    }

    lv_obj_remove_flag(previous_panel, LV_OBJ_FLAG_HIDDEN);
    spdlog::debug("[NavigationManager] Showing previous panel {} (stack depth: {}, is_main={})",
                  (void*)previous_panel, panel_stack_.size(), is_main_panel);

    // Notify E-Stop overlay
    if (is_main_panel) {
        EmergencyStopOverlay::instance().on_panel_changed(panel_id_to_name(active_panel_));
    } else {
        const char* panel_name = lv_obj_get_name(previous_panel);
        if (panel_name) {
            EmergencyStopOverlay::instance().on_panel_changed(panel_name);
        }
    }

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

void ui_nav_push_overlay(lv_obj_t* overlay_panel) {
    NavigationManager::instance().push_overlay(overlay_panel);
}

bool ui_nav_go_back() {
    return NavigationManager::instance().go_back();
}
