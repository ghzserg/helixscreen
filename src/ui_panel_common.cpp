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

#include "ui_panel_common.h"

#include "ui_component_header_bar.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include <spdlog/spdlog.h>

// ============================================================================
// HEADER BAR SETUP
// ============================================================================

lv_obj_t* ui_panel_setup_header(lv_obj_t* panel, lv_obj_t* parent_screen, const char* header_name) {
    if (!panel || !parent_screen || !header_name) {
        spdlog::warn("[PanelCommon] Invalid parameters for header setup");
        return nullptr;
    }

    lv_obj_t* header = lv_obj_find_by_name(panel, header_name);
    if (header) {
        ui_component_header_bar_setup(header, parent_screen);
        spdlog::debug("[PanelCommon] Header '{}' configured for responsive height", header_name);
    } else {
        spdlog::warn("[PanelCommon] Header '{}' not found in panel", header_name);
    }

    return header;
}

// ============================================================================
// CONTENT PADDING SETUP
// ============================================================================

lv_obj_t* ui_panel_setup_content_padding(lv_obj_t* panel, lv_obj_t* parent_screen,
                                         const char* content_name) {
    if (!panel || !parent_screen || !content_name) {
        spdlog::warn("[PanelCommon] Invalid parameters for content padding setup");
        return nullptr;
    }

    lv_obj_t* content = lv_obj_find_by_name(panel, content_name);
    if (content) {
        lv_coord_t vertical_padding =
            ui_get_header_content_padding(lv_obj_get_height(parent_screen));

        // Set vertical padding (top/bottom) responsively, keep horizontal at medium (12px)
        lv_obj_set_style_pad_top(content, vertical_padding, 0);
        lv_obj_set_style_pad_bottom(content, vertical_padding, 0);
        lv_obj_set_style_pad_left(content, UI_PADDING_MEDIUM, 0);
        lv_obj_set_style_pad_right(content, UI_PADDING_MEDIUM, 0);

        spdlog::debug("[PanelCommon] Content '{}' padding: top/bottom={}px, left/right={}px",
                      content_name, vertical_padding, UI_PADDING_MEDIUM);
    } else {
        spdlog::warn("[PanelCommon] Content '{}' not found in panel", content_name);
    }

    return content;
}

// ============================================================================
// BACK BUTTON SETUP
// ============================================================================

void ui_panel_back_button_cb(lv_event_t* e) {
    lv_obj_t* panel = (lv_obj_t*)lv_event_get_user_data(e);

    // Use navigation history to go back to previous panel
    if (!ui_nav_go_back()) {
        // Fallback: If navigation history is empty, manually hide panel and show controls launcher
        if (panel) {
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

            // Try to find and show controls_panel as fallback
            lv_obj_t* parent = lv_obj_get_parent(panel);
            if (parent) {
                lv_obj_t* controls_launcher = lv_obj_find_by_name(parent, "controls_panel");
                if (controls_launcher) {
                    lv_obj_clear_flag(controls_launcher, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}

lv_obj_t* ui_panel_setup_back_button(lv_obj_t* panel, const char* button_name) {
    if (!panel || !button_name) {
        spdlog::warn("[PanelCommon] Invalid parameters for back button setup");
        return nullptr;
    }

    lv_obj_t* back_btn = lv_obj_find_by_name(panel, button_name);
    if (back_btn) {
        lv_obj_add_event_cb(back_btn, ui_panel_back_button_cb, LV_EVENT_CLICKED, panel);
        spdlog::debug("[PanelCommon] Back button '{}' configured", button_name);
    } else {
        spdlog::warn("[PanelCommon] Back button '{}' not found in panel", button_name);
    }

    return back_btn;
}

// ============================================================================
// RESIZE CALLBACK SETUP
// ============================================================================

// Generic resize callback that uses context to update content padding
static void panel_resize_callback_wrapper(ui_panel_resize_context_t* context) {
    if (!context || !context->panel || !context->parent_screen || !context->content_name) {
        return;
    }

    lv_obj_t* content = lv_obj_find_by_name(context->panel, context->content_name);
    if (content) {
        lv_coord_t vertical_padding =
            ui_get_header_content_padding(lv_obj_get_height(context->parent_screen));

        // Update vertical padding (top/bottom) responsively, keep horizontal at medium (12px)
        lv_obj_set_style_pad_top(content, vertical_padding, 0);
        lv_obj_set_style_pad_bottom(content, vertical_padding, 0);
        lv_obj_set_style_pad_left(content, UI_PADDING_MEDIUM, 0);
        lv_obj_set_style_pad_right(content, UI_PADDING_MEDIUM, 0);
    }
}

// Context storage for trampolines (max 8 panels using common resize)
static ui_panel_resize_context_t* trampoline_contexts[8] = {nullptr};
static int trampoline_count = 0;

// Actual trampoline implementations
static void resize_trampoline_impl_0(void) {
    panel_resize_callback_wrapper(trampoline_contexts[0]);
}
static void resize_trampoline_impl_1(void) {
    panel_resize_callback_wrapper(trampoline_contexts[1]);
}
static void resize_trampoline_impl_2(void) {
    panel_resize_callback_wrapper(trampoline_contexts[2]);
}
static void resize_trampoline_impl_3(void) {
    panel_resize_callback_wrapper(trampoline_contexts[3]);
}
static void resize_trampoline_impl_4(void) {
    panel_resize_callback_wrapper(trampoline_contexts[4]);
}
static void resize_trampoline_impl_5(void) {
    panel_resize_callback_wrapper(trampoline_contexts[5]);
}
static void resize_trampoline_impl_6(void) {
    panel_resize_callback_wrapper(trampoline_contexts[6]);
}
static void resize_trampoline_impl_7(void) {
    panel_resize_callback_wrapper(trampoline_contexts[7]);
}

static ui_resize_callback_t trampoline_funcs[8] = {
    resize_trampoline_impl_0, resize_trampoline_impl_1, resize_trampoline_impl_2,
    resize_trampoline_impl_3, resize_trampoline_impl_4, resize_trampoline_impl_5,
    resize_trampoline_impl_6, resize_trampoline_impl_7,
};

void ui_panel_setup_resize_callback(ui_panel_resize_context_t* context) {
    if (!context) {
        spdlog::warn("[PanelCommon] Invalid resize context");
        return;
    }

    if (trampoline_count >= 8) {
        spdlog::error("[PanelCommon] Too many panels using common resize (max 8)");
        return;
    }

    // Store context and register corresponding trampoline
    trampoline_contexts[trampoline_count] = context;
    ui_resize_handler_register(trampoline_funcs[trampoline_count]);
    trampoline_count++;

    spdlog::debug("[PanelCommon] Resize callback registered for content '{}'",
                  context->content_name);
}

// ============================================================================
// COMBINED SETUP
// ============================================================================

void ui_panel_setup_standard_layout(lv_obj_t* panel, lv_obj_t* parent_screen,
                                    const char* header_name, const char* content_name,
                                    ui_panel_resize_context_t* resize_context,
                                    const char* back_button_name) {
    if (!panel || !parent_screen) {
        spdlog::error("[PanelCommon] Invalid parameters for standard layout setup");
        return;
    }

    spdlog::debug("[PanelCommon] Setting up standard panel layout");

    // 1. Setup header bar
    ui_panel_setup_header(panel, parent_screen, header_name);

    // 2. Setup content padding
    ui_panel_setup_content_padding(panel, parent_screen, content_name);

    // 3. Register resize callback
    if (resize_context) {
        resize_context->panel = panel;
        resize_context->parent_screen = parent_screen;
        resize_context->content_name = content_name;
        ui_panel_setup_resize_callback(resize_context);
    } else {
        spdlog::warn("[PanelCommon] No resize context provided, skipping resize callback");
    }

    // 4. Setup back button
    ui_panel_setup_back_button(panel, back_button_name);

    spdlog::debug("[PanelCommon] Standard panel layout setup complete");
}

// ============================================================================
// OVERLAY PANEL SETUP (For panels using overlay_panel.xml wrapper)
// ============================================================================

void ui_overlay_panel_setup_standard(lv_obj_t* panel, lv_obj_t* parent_screen,
                                     const char* header_name,
                                     const char* content_name) {
    if (!panel || !parent_screen) {
        spdlog::error("[PanelCommon] Invalid parameters for overlay panel setup");
        return;
    }

    spdlog::debug("[PanelCommon] Setting up overlay panel with header='{}', content='{}'",
                  header_name, content_name);

    // 1. Setup header responsive height (if needed - usually handled by XML)
    lv_obj_t* header = lv_obj_find_by_name(panel, header_name);
    if (header) {
        // Header height is typically set via #header_height constant in XML
        // Just wire the back button here
        ui_overlay_panel_wire_back_button(panel, header_name);
    } else {
        spdlog::warn("[PanelCommon] Header '{}' not found in overlay panel", header_name);
    }

    // 2. Setup content padding (responsive vertical, fixed horizontal)
    // Note: overlay_panel.xml already sets style_pad_all="#padding_normal"
    // This is a no-op unless we need to override for specific screen sizes
    lv_obj_t* content = lv_obj_find_by_name(panel, content_name);
    if (content) {
        spdlog::debug("[PanelCommon] Content area '{}' found, padding already set by XML",
                      content_name);
    } else {
        spdlog::warn("[PanelCommon] Content area '{}' not found in overlay panel", content_name);
    }

    spdlog::debug("[PanelCommon] Overlay panel setup complete");
}

lv_obj_t* ui_overlay_panel_wire_back_button(lv_obj_t* panel, const char* header_name) {
    if (!panel || !header_name) {
        spdlog::warn("[PanelCommon] Invalid parameters for overlay back button wiring");
        return nullptr;
    }

    // Find header_bar widget
    lv_obj_t* header = lv_obj_find_by_name(panel, header_name);
    if (!header) {
        spdlog::warn("[PanelCommon] Header '{}' not found in overlay panel", header_name);
        return nullptr;
    }

    // Find back_button within header_bar
    lv_obj_t* back_btn = lv_obj_find_by_name(header, "back_button");
    if (!back_btn) {
        spdlog::warn("[PanelCommon] Back button not found in header '{}'", header_name);
        return nullptr;
    }

    // Wire to standard back button handler (uses ui_nav_go_back)
    lv_obj_add_event_cb(back_btn, ui_panel_back_button_cb, LV_EVENT_CLICKED, panel);
    spdlog::debug("[PanelCommon] Back button wired in header '{}'", header_name);

    return back_btn;
}

lv_obj_t* ui_overlay_panel_wire_right_button(lv_obj_t* panel,
                                             lv_event_cb_t callback,
                                             const char* header_name,
                                             void* user_data) {
    if (!panel || !callback || !header_name) {
        spdlog::warn("[PanelCommon] Invalid parameters for overlay right button wiring");
        return nullptr;
    }

    // Find header_bar widget
    lv_obj_t* header = lv_obj_find_by_name(panel, header_name);
    if (!header) {
        spdlog::warn("[PanelCommon] Header '{}' not found in overlay panel", header_name);
        return nullptr;
    }

    // Find right_button within header_bar
    lv_obj_t* right_btn = lv_obj_find_by_name(header, "right_button");
    if (!right_btn) {
        spdlog::warn("[PanelCommon] Right button not found in header '{}'", header_name);
        return nullptr;
    }

    // Wire to provided callback with user_data
    lv_obj_add_event_cb(right_btn, callback, LV_EVENT_CLICKED, user_data);
    spdlog::debug("[PanelCommon] Right button wired in header '{}'", header_name);

    return right_btn;
}
