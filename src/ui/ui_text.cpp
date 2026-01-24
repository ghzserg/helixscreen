// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_text.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_label_parser.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>

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

    // Apply color from theme constant (not hardcoded - parsed from globals.xml)
    const char* color_str = lv_xml_get_const(NULL, color_const_name);
    if (color_str && color_str[0] == '#') {
        uint32_t hex = static_cast<uint32_t>(strtoul(color_str + 1, NULL, 16));
        lv_color_t color = lv_color_hex(hex); // theme color parsed from XML
        lv_obj_set_style_text_color(label, color, 0);
    } else {
        spdlog::warn("[ui_text] Color constant '{}' not found or invalid in globals.xml",
                     color_const_name);
    }
}

/**
 * Helper function to apply text stroke attributes from XML
 *
 * Parses and applies stroke_width, stroke_color, and stroke_opa attributes
 * to enable text outline effects on labels.
 *
 * @param label Label widget to apply stroke styling to
 * @param attrs XML attribute array (name/value pairs, NULL terminated)
 *
 * Usage in XML:
 *   <text_heading text="Title" stroke_width="2" stroke_color="0x000000" stroke_opa="255"/>
 *   <text_body text="Body" stroke_width="1" stroke_color="#000000"/>
 */
static void apply_stroke_attrs(lv_obj_t* label, const char** attrs) {
    if (!attrs)
        return;

    const char* stroke_width = lv_xml_get_value_of(attrs, "stroke_width");
    const char* stroke_color = lv_xml_get_value_of(attrs, "stroke_color");
    const char* stroke_opa = lv_xml_get_value_of(attrs, "stroke_opa");

    // Apply stroke width (required for stroke to be visible)
    if (stroke_width) {
        int32_t width = lv_xml_atoi(stroke_width);
        lv_obj_set_style_text_outline_stroke_width(label, width, 0);

        // Default to full opacity if width is set but opacity is not
        if (!stroke_opa) {
            lv_obj_set_style_text_outline_stroke_opa(label, LV_OPA_COVER, 0);
        }

        // Default to black stroke if width is set but color is not
        if (!stroke_color) {
            lv_obj_set_style_text_outline_stroke_color(label, lv_color_black(), 0);
        }

        spdlog::trace("[ui_text] Applied text stroke: width={}", width);
    }

    // Apply stroke color
    if (stroke_color) {
        lv_color_t color = lv_xml_to_color(stroke_color);
        lv_obj_set_style_text_outline_stroke_color(label, color, 0);
    }

    // Apply stroke opacity
    if (stroke_opa) {
        lv_opa_t opa = lv_xml_to_opa(stroke_opa);
        lv_obj_set_style_text_outline_stroke_opa(label, opa, 0);
    }
}

/**
 * Shared XML apply callback for all text_* widgets
 *
 * Applies standard label properties plus custom stroke attributes.
 * All semantic text widgets use this same apply function.
 */
static void ui_text_apply(lv_xml_parser_state_t* state, const char** attrs) {
    // Apply label properties (text, long_mode, etc.) and base object properties
    lv_xml_label_apply(state, attrs);

    // Apply stroke attributes (stroke_width, stroke_color, stroke_opa)
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_xml_state_get_item(state));
    apply_stroke_attrs(label, attrs);
}

/**
 * Helper to create a semantic text label with specified font and color
 */
static lv_obj_t* create_semantic_label(lv_xml_parser_state_t* state, const char** attrs,
                                       const char* font_const, const char* color_const) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));
    lv_obj_t* label = lv_label_create(parent);
    apply_semantic_style(label, font_const, color_const);
    return label;
}

// XML create callbacks - each variant just specifies font/color constants

static void* ui_text_heading_create(lv_xml_parser_state_t* state, const char** attrs) {
    return create_semantic_label(state, attrs, "font_heading", "header_text");
}

static void* ui_text_body_create(lv_xml_parser_state_t* state, const char** attrs) {
    return create_semantic_label(state, attrs, "font_body", "text_primary");
}

static void* ui_text_small_create(lv_xml_parser_state_t* state, const char** attrs) {
    return create_semantic_label(state, attrs, "font_small", "text_secondary");
}

static void* ui_text_xs_create(lv_xml_parser_state_t* state, const char** attrs) {
    return create_semantic_label(state, attrs, "font_xs", "text_secondary");
}

/**
 * Create callback for text_button widget
 *
 * Button text with body font, primary text color, and center alignment default.
 * Used inside lv_button elements to provide consistent button label styling.
 */
static void* ui_text_button_create(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* label = create_semantic_label(state, attrs, "font_body", "text_primary");
    if (label) {
        // Center the label within its parent (the button)
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        // Also set text alignment for multi-line button labels
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    return label;
}

void ui_text_init() {
    // Register custom text widgets for XML usage
    // All widgets share the same apply function (ui_text_apply) which handles
    // standard label attributes plus custom stroke_* attributes
    lv_xml_register_widget("text_heading", ui_text_heading_create, ui_text_apply);
    lv_xml_register_widget("text_body", ui_text_body_create, ui_text_apply);
    lv_xml_register_widget("text_small", ui_text_small_create, ui_text_apply);
    lv_xml_register_widget("text_xs", ui_text_xs_create, ui_text_apply);
    // text_tiny is an alias for text_xs (same size, just a more intuitive name)
    lv_xml_register_widget("text_tiny", ui_text_xs_create, ui_text_apply);
    // text_button: centered body text for button labels (eliminates manual align/color overrides)
    lv_xml_register_widget("text_button", ui_text_button_create, ui_text_apply);

    spdlog::debug(
        "[ui_text] Registered semantic text widgets: text_heading, text_body, text_small, "
        "text_xs, text_tiny, text_button");
}

void ui_text_set_stroke(lv_obj_t* label, int32_t width, lv_color_t color, lv_opa_t opa) {
    if (!label) {
        spdlog::warn("[ui_text] ui_text_set_stroke called with NULL label");
        return;
    }

    lv_obj_set_style_text_outline_stroke_width(label, width, 0);
    lv_obj_set_style_text_outline_stroke_color(label, color, 0);
    lv_obj_set_style_text_outline_stroke_opa(label, opa, 0);

    spdlog::trace("[ui_text] Applied text stroke: width={}, opa={}", width, opa);
}
