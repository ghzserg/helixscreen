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

#include "ui_text.h"

#include "ui_theme.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_label_parser.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"

#include <spdlog/spdlog.h>

#include <cstdlib>

/**
 * Helper function to apply semantic font and color to a label
 *
 * IMPORTANT: This function will CRASH the application if a font is not found.
 * This is intentional - silent font fallbacks cause visual bugs that are
 * extremely hard to debug. If a font is missing, fix lv_conf.h immediately.
 *
 * @param label Label widget to style
 * @param font_const_name Name of font constant in globals.xml (e.g., "font_heading")
 * @param color_const_name Name of color constant in globals.xml (e.g., "header_text")
 */
static void apply_semantic_style(lv_obj_t* label, const char* font_const_name,
                                 const char* color_const_name) {
    // Apply font - FAIL FAST if font is not available
    const char* font_name = lv_xml_get_const(NULL, font_const_name);
    if (!font_name) {
        spdlog::critical("[ui_text] FATAL: Font constant '{}' not found in globals.xml",
                         font_const_name);
        spdlog::critical("[ui_text] Check that globals.xml defines this constant");
        std::exit(EXIT_FAILURE);
    }

    const lv_font_t* font = lv_xml_get_font(NULL, font_name);
    if (!font) {
        // Extract font size from name like "montserrat_26" -> "26"
        std::string font_str(font_name);
        std::string size_hint;
        size_t underscore = font_str.rfind('_');
        if (underscore != std::string::npos) {
            size_hint = font_str.substr(underscore + 1);
        }

        spdlog::critical("[ui_text] FATAL: Font '{}' (from constant '{}') is not compiled!",
                         font_name, font_const_name);
        if (!size_hint.empty()) {
            spdlog::critical("[ui_text] FIX: Enable LV_FONT_MONTSERRAT_{} in lv_conf.h", size_hint);
        }
        std::exit(EXIT_FAILURE);
    }

    lv_obj_set_style_text_font(label, font, 0);

    // Debug: log the actual font being applied
    spdlog::trace("[ui_text] Applied font '{}' (from '{}') - line_height={}px", font_name,
                  font_const_name, lv_font_get_line_height(font));

    // Apply color
    const char* color_str = lv_xml_get_const(NULL, color_const_name);
    if (color_str && color_str[0] == '#') {
        uint32_t hex = static_cast<uint32_t>(strtoul(color_str + 1, NULL, 16));
        lv_color_t color = lv_color_hex(hex);
        lv_obj_set_style_text_color(label, color, 0);
    } else {
        spdlog::warn("[ui_text] Color constant '{}' not found or invalid in globals.xml",
                     color_const_name);
    }
}

/**
 * XML create callback for <text_heading> widget
 * Creates a label with heading font (montserrat_20) and header color
 */
static void* ui_text_heading_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = (lv_obj_t*)lv_xml_state_get_parent(state);
    lv_obj_t* label = lv_label_create(parent);
    apply_semantic_style(label, "font_heading", "header_text");
    return label;
}

/**
 * XML apply callback for <text_heading> widget
 * Delegates to standard label parser (inherits all label attributes)
 */
static void ui_text_heading_apply(lv_xml_parser_state_t* state, const char** attrs) {
    // Apply label properties (text, long_mode, etc.) and base object properties
    lv_xml_label_apply(state, attrs);
}

/**
 * XML create callback for <text_body> widget
 * Creates a label with body font (montserrat_16) and primary text color
 */
static void* ui_text_body_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = (lv_obj_t*)lv_xml_state_get_parent(state);
    lv_obj_t* label = lv_label_create(parent);
    apply_semantic_style(label, "font_body", "text_primary");
    return label;
}

/**
 * XML apply callback for <text_body> widget
 * Delegates to standard label parser (inherits all label attributes)
 */
static void ui_text_body_apply(lv_xml_parser_state_t* state, const char** attrs) {
    // Apply label properties (text, long_mode, etc.) and base object properties
    lv_xml_label_apply(state, attrs);
}

/**
 * XML create callback for <text_small> widget
 * Creates a label with small font (montserrat_10) and secondary text color
 */
static void* ui_text_small_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = (lv_obj_t*)lv_xml_state_get_parent(state);
    lv_obj_t* label = lv_label_create(parent);
    apply_semantic_style(label, "font_small", "text_secondary");
    return label;
}

/**
 * XML apply callback for <text_small> widget
 * Delegates to standard label parser (inherits all label attributes)
 */
static void ui_text_small_apply(lv_xml_parser_state_t* state, const char** attrs) {
    // Apply label properties (text, long_mode, etc.) and base object properties
    lv_xml_label_apply(state, attrs);
}

void ui_text_init() {
    // Register custom text widgets for XML usage
    lv_xml_register_widget("text_heading", ui_text_heading_create, ui_text_heading_apply);
    lv_xml_register_widget("text_body", ui_text_body_create, ui_text_body_apply);
    lv_xml_register_widget("text_small", ui_text_small_create, ui_text_small_apply);

    spdlog::debug(
        "[ui_text] Registered semantic text widgets: text_heading, text_body, text_small");
}
