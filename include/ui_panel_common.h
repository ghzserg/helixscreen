// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

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

#pragma once

#include "lvgl/lvgl.h"

/**
 * @file ui_panel_common.h
 * @brief Common helper utilities for panel setup to reduce boilerplate
 *
 * Provides reusable functions for standard panel setup patterns:
 * - Header bar configuration with responsive height
 * - Content area padding (responsive vertical, fixed horizontal)
 * - Resize callback registration
 * - Standard back button event handlers
 *
 * These helpers eliminate 50-100 lines of repetitive code per panel.
 */

// ============================================================================
// HEADER BAR SETUP
// ============================================================================

/**
 * @brief Setup header bar with responsive height
 *
 * Finds the header bar widget by name within the panel and configures it
 * for responsive height based on screen size.
 *
 * @param panel Panel object containing the header bar
 * @param parent_screen Parent screen object for measuring screen height
 * @param header_name Name of the header bar widget (e.g., "motion_header")
 * @return Pointer to header bar widget if found, nullptr otherwise
 */
lv_obj_t* ui_panel_setup_header(lv_obj_t* panel, lv_obj_t* parent_screen, const char* header_name);

// ============================================================================
// CONTENT PADDING SETUP
// ============================================================================

/**
 * @brief Setup responsive padding for content area
 *
 * Configures content area with responsive vertical padding (varies by screen size)
 * and fixed horizontal padding (UI_PADDING_MEDIUM = 12px).
 *
 * Pattern used across all panels:
 * - Vertical (top/bottom): 20px large, 10px small, 6px tiny
 * - Horizontal (left/right): 12px (UI_PADDING_MEDIUM)
 *
 * @param panel Panel object containing the content area
 * @param parent_screen Parent screen object for measuring screen height
 * @param content_name Name of the content area widget (e.g., "motion_content")
 * @return Pointer to content area widget if found, nullptr otherwise
 */
lv_obj_t* ui_panel_setup_content_padding(lv_obj_t* panel, lv_obj_t* parent_screen,
                                         const char* content_name);

// ============================================================================
// BACK BUTTON SETUP
// ============================================================================

/**
 * @brief Standard back button event handler
 *
 * Implements the standard back button behavior used across all panels:
 * 1. Attempt to use navigation history via ui_nav_go_back()
 * 2. Fallback: Hide current panel, show controls launcher
 *
 * Usage:
 *   lv_obj_add_event_cb(back_btn, ui_panel_back_button_cb,
 *                       LV_EVENT_CLICKED, panel);
 *
 * @param e LVGL event object (user_data should be the panel object)
 */
void ui_panel_back_button_cb(lv_event_t* e);

/**
 * @brief Setup standard back button
 *
 * Finds the back button by name and attaches the standard event handler.
 * The handler uses navigation history or falls back to hiding the panel.
 *
 * @param panel Panel object containing the back button
 * @param button_name Name of the back button widget (default: "back_button")
 * @return Pointer to back button widget if found, nullptr otherwise
 */
lv_obj_t* ui_panel_setup_back_button(lv_obj_t* panel, const char* button_name = "back_button");

// ============================================================================
// RESIZE CALLBACK SETUP
// ============================================================================

/**
 * @brief Context for panel resize callbacks
 *
 * Stores panel state needed for resize operations. Pass this to
 * ui_panel_setup_resize_callback() to automatically handle content padding
 * updates on window resize.
 */
struct ui_panel_resize_context_t {
    lv_obj_t* panel;          ///< Panel object
    lv_obj_t* parent_screen;  ///< Parent screen object
    const char* content_name; ///< Name of content area widget
};

/**
 * @brief Setup standard resize callback for content padding
 *
 * Registers a resize callback that automatically updates content padding
 * when the window is resized. The context object must remain valid for
 * the lifetime of the panel.
 *
 * Pattern: Each panel has a static resize context and callback that updates
 * vertical padding responsively while keeping horizontal padding constant.
 *
 * @param context Pointer to resize context (must be static/persistent)
 */
void ui_panel_setup_resize_callback(ui_panel_resize_context_t* context);

// ============================================================================
// COMBINED SETUP
// ============================================================================

/**
 * @brief Standard panel layout setup (header + content + resize + back button)
 *
 * Combines all common setup operations in a single call:
 * 1. Setup header bar with responsive height
 * 2. Setup content padding (responsive vertical, fixed horizontal)
 * 3. Register resize callback for content padding
 * 4. Setup back button with standard handler
 *
 * This is the recommended function for most panels, reducing setup from
 * 50-100 lines to a single function call.
 *
 * @param panel Panel object
 * @param parent_screen Parent screen object
 * @param header_name Name of header bar widget
 * @param content_name Name of content area widget
 * @param resize_context Pointer to static resize context (must be persistent)
 * @param back_button_name Name of back button (default: "back_button")
 */
void ui_panel_setup_standard_layout(lv_obj_t* panel, lv_obj_t* parent_screen,
                                    const char* header_name, const char* content_name,
                                    ui_panel_resize_context_t* resize_context,
                                    const char* back_button_name = "back_button");

// ============================================================================
// OVERLAY PANEL SETUP (For panels using overlay_panel.xml wrapper)
// ============================================================================

/**
 * @brief Standard setup for overlay panels using overlay_panel.xml wrapper
 *
 * Overlay panels use the new overlay_panel.xml component which provides:
 * - Integrated header_bar with back button
 * - Right-aligned positioning
 * - Content area with responsive padding
 *
 * This function wires the header_bar back button to use ui_nav_go_back()
 * and sets up responsive padding for the content area.
 *
 * @param panel Overlay panel root object
 * @param parent_screen Parent screen for measuring dimensions
 * @param header_name Name of header_bar widget (default: "overlay_header")
 * @param content_name Name of content area (default: "overlay_content")
 */
void ui_overlay_panel_setup_standard(lv_obj_t* panel, lv_obj_t* parent_screen,
                                     const char* header_name = "overlay_header",
                                     const char* content_name = "overlay_content");

/**
 * @brief Wire back button in overlay panel header_bar
 *
 * Finds the back button within the header_bar and wires it to call
 * ui_nav_go_back() when clicked. This is the standard behavior for all
 * overlay panels.
 *
 * @param panel Overlay panel root object
 * @param header_name Name of header_bar widget (default: "overlay_header")
 * @return Pointer to back button if found, nullptr otherwise
 */
lv_obj_t* ui_overlay_panel_wire_back_button(lv_obj_t* panel,
                                            const char* header_name = "overlay_header");

/**
 * @brief Wire right action button in overlay panel header_bar
 *
 * Finds the right button within the header_bar and wires it to the provided
 * callback. Used for confirm/save/action buttons in overlay panels.
 *
 * @param panel Overlay panel root object
 * @param callback Event callback for button click
 * @param header_name Name of header_bar widget (default: "overlay_header")
 * @param user_data Optional user data to pass to callback (default: nullptr)
 * @return Pointer to right button if found, nullptr otherwise
 */
lv_obj_t* ui_overlay_panel_wire_right_button(lv_obj_t* panel,
                                             lv_event_cb_t callback,
                                             const char* header_name = "overlay_header",
                                             void* user_data = nullptr);
