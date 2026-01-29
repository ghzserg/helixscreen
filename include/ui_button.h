// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file ui_button.h
 * @brief Semantic button widget with variant styles and auto-contrast text
 *
 * Provides a <ui_button> XML widget with:
 * - Semantic variants: primary, secondary, danger, ghost
 * - Auto-contrast text: automatically picks light/dark text based on bg luminance
 * - Reactive styling: updates automatically when theme changes
 *
 * Usage in XML:
 *   <ui_button variant="primary" text="Save"/>
 *   <ui_button variant="danger" text="Delete"/>
 *   <ui_button variant="secondary" text="Cancel"/>
 *   <ui_button variant="ghost" text="Skip"/>
 *
 * Variant styles are defined in theme_core:
 * - primary: theme_core_get_button_primary_style() (primary/accent color bg)
 * - secondary: theme_core_get_button_secondary_style() (surface color bg)
 * - danger: theme_core_get_button_danger_style() (danger/red color bg)
 * - ghost: theme_core_get_button_ghost_style() (transparent bg)
 *
 * Text contrast is computed using luminance formula:
 *   L = (299*R + 587*G + 114*B) / 1000
 * If L < 128 (dark bg): light text (theme_core_get_text_for_dark_bg)
 * If L >= 128 (light bg): dark text (theme_core_get_text_for_light_bg)
 */

/**
 * @brief Initialize the ui_button custom widget
 *
 * Registers the <ui_button> XML widget with LVGL's XML parser.
 * Must be called after lv_xml_init() and after theme is initialized.
 */
void ui_button_init();
