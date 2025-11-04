/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_keyboard.h"
#include <spdlog/spdlog.h>

// Global keyboard instance
static lv_obj_t* g_keyboard = NULL;

/**
 * @brief Textarea focus event callback - handles auto show/hide
 */
static void textarea_focus_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* textarea = lv_event_get_target_obj(e);

    if (code == LV_EVENT_FOCUSED) {
        spdlog::debug("[Keyboard] Textarea focused: {}", (void*)textarea);
        ui_keyboard_show(textarea);
    }
    else if (code == LV_EVENT_DEFOCUSED) {
        spdlog::debug("[Keyboard] Textarea defocused: {}", (void*)textarea);
        ui_keyboard_hide();
    }
}

/**
 * @brief Keyboard event callback - handles ready/cancel events
 */
static void keyboard_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        spdlog::info("[Keyboard] OK pressed - input confirmed");
        ui_keyboard_hide();
    }
    else if (code == LV_EVENT_CANCEL) {
        spdlog::info("[Keyboard] Cancel pressed");
        ui_keyboard_hide();
    }
}

void ui_keyboard_init(lv_obj_t* parent)
{
    if (g_keyboard != NULL) {
        spdlog::warn("[Keyboard] Already initialized, skipping");
        return;
    }

    spdlog::info("[Keyboard] Initializing global keyboard");

    // Create keyboard at bottom of screen
    g_keyboard = lv_keyboard_create(parent);

    // Set initial mode (lowercase text)
    lv_keyboard_set_mode(g_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

    // Enable pop-overs (iOS/Android-style key feedback)
    lv_keyboard_set_popovers(g_keyboard, true);

    // Apply styling - theme handles colors, set opacity for transparency
    lv_obj_set_style_bg_opa(g_keyboard, LV_OPA_80, LV_PART_MAIN);  // 80% opacity = 20% transparent
    lv_obj_set_style_bg_opa(g_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(g_keyboard, 8, LV_PART_ITEMS);  // Rounded key corners

    // Position at bottom-middle (default)
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Start hidden
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Add event handlers for ready and cancel events
    lv_obj_add_event_cb(g_keyboard, keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(g_keyboard, keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    spdlog::info("[Keyboard] Initialization complete");
}

void ui_keyboard_register_textarea(lv_obj_t* textarea)
{
    if (g_keyboard == NULL) {
        spdlog::error("[Keyboard] Not initialized - call ui_keyboard_init() first");
        return;
    }

    if (textarea == NULL) {
        spdlog::error("[Keyboard] Cannot register NULL textarea");
        return;
    }

    spdlog::debug("[Keyboard] Registering textarea: {}", (void*)textarea);

    // Add event handler to catch focus/defocus events (not ALL events to avoid cleanup issues)
    lv_obj_add_event_cb(textarea, textarea_focus_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(textarea, textarea_focus_event_cb, LV_EVENT_DEFOCUSED, NULL);

    // Add textarea to default input group for physical keyboard input
    lv_group_t* default_group = lv_group_get_default();
    if (default_group) {
        lv_group_add_obj(default_group, textarea);
        spdlog::debug("[Keyboard] Added textarea to input group for physical keyboard");
    }
}

void ui_keyboard_show(lv_obj_t* textarea)
{
    if (g_keyboard == NULL) {
        spdlog::error("[Keyboard] Not initialized - call ui_keyboard_init() first");
        return;
    }

    // Safety: if keyboard's parent is NULL, we're in cleanup - bail out
    if (lv_obj_get_parent(g_keyboard) == NULL) {
        spdlog::debug("[Keyboard] Skipping show - keyboard is being cleaned up");
        return;
    }

    // Safety: check if screen is valid before layout operations
    lv_obj_t* screen = lv_screen_active();
    if (screen == NULL || lv_obj_get_parent(screen) == NULL) {
        spdlog::debug("[Keyboard] Skipping show - screen is being cleaned up");
        return;
    }

    spdlog::debug("[Keyboard] Showing keyboard for textarea: {}", (void*)textarea);

    // Assign textarea to keyboard (standard LVGL API)
    lv_keyboard_set_textarea(g_keyboard, textarea);

    // Show keyboard
    lv_obj_remove_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Force layout update to get accurate positions
    lv_obj_update_layout(screen);

    if (!textarea) {
        return;
    }

    // Get absolute coordinates
    lv_area_t kb_coords, ta_coords;
    lv_obj_get_coords(g_keyboard, &kb_coords);
    lv_obj_get_coords(textarea, &ta_coords);

    int32_t kb_top = kb_coords.y1;
    int32_t ta_bottom = ta_coords.y2;

    // Add padding above textarea (20px breathing room)
    const int32_t padding = 20;
    int32_t desired_bottom = kb_top - padding;

    // Calculate if we need to shift the screen up
    if (ta_bottom > desired_bottom) {
        int32_t shift_up = ta_bottom - desired_bottom;

        spdlog::debug("[Keyboard] Shifting screen UP by {} px (ta_bottom={}, kb_top={})",
                     shift_up, ta_bottom, kb_top);

        // Move all screen children (except keyboard) up with animation
        lv_obj_t* screen = lv_screen_active();
        uint32_t child_count = lv_obj_get_child_count(screen);

        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* child = lv_obj_get_child(screen, i);
            if (child == g_keyboard) continue;

            int32_t current_y = lv_obj_get_y(child);
            int32_t target_y = current_y - shift_up;

            // Animate the Y position change (200ms, fast and smooth)
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, child);
            lv_anim_set_values(&a, current_y, target_y);
            lv_anim_set_time(&a, 200);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
        }
    } else {
        spdlog::debug("[Keyboard] Textarea already visible (ta_bottom={}, kb_top={})",
                     ta_bottom, kb_top);
    }
}

void ui_keyboard_hide()
{
    if (g_keyboard == NULL) {
        spdlog::error("[Keyboard] Not initialized - call ui_keyboard_init() first");
        return;
    }

    // Safety: if keyboard's parent is NULL, we're in cleanup - bail out
    if (lv_obj_get_parent(g_keyboard) == NULL) {
        spdlog::debug("[Keyboard] Skipping hide - keyboard is being cleaned up");
        return;
    }

    // Safety: check if screen is valid before layout operations
    lv_obj_t* screen = lv_screen_active();
    if (screen == NULL || lv_obj_get_parent(screen) == NULL) {
        spdlog::debug("[Keyboard] Skipping hide - screen is being cleaned up");
        return;
    }

    spdlog::debug("[Keyboard] Hiding keyboard");

    // Clear keyboard assignment
    lv_keyboard_set_textarea(g_keyboard, NULL);

    // Hide keyboard
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Move all screen children (except keyboard) back to y=0 with animation
    uint32_t child_count = lv_obj_get_child_count(screen);

    spdlog::debug("[Keyboard] Restoring screen children to y=0");

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(screen, i);
        if (child == g_keyboard) continue;

        int32_t current_y = lv_obj_get_y(child);
        if (current_y != 0) {
            // Animate back to y=0 (200ms, fast and smooth)
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, child);
            lv_anim_set_values(&a, current_y, 0);
            lv_anim_set_time(&a, 200);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_start(&a);
        }
    }
}

bool ui_keyboard_is_visible()
{
    if (g_keyboard == NULL) {
        return false;
    }

    return !lv_obj_has_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ui_keyboard_get_instance()
{
    return g_keyboard;
}

void ui_keyboard_set_mode(lv_keyboard_mode_t mode)
{
    if (g_keyboard == NULL) {
        spdlog::error("[Keyboard] Not initialized - call ui_keyboard_init() first");
        return;
    }

    spdlog::debug("[Keyboard] Setting mode: {}", (int)mode);
    lv_keyboard_set_mode(g_keyboard, mode);
}

void ui_keyboard_set_position(lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    if (g_keyboard == NULL) {
        spdlog::error("[Keyboard] Not initialized - call ui_keyboard_init() first");
        return;
    }

    spdlog::debug("[Keyboard] Setting position: align={}, x={}, y={}",
                  (int)align, x_ofs, y_ofs);
    lv_obj_align(g_keyboard, align, x_ofs, y_ofs);
}
