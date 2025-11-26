// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_glyphs.h"

#include "app_globals.h"
#include "printer_state.h"
#include "ui_theme.h"

#include <lvgl/lvgl.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

/**
 * @brief Structure to hold glyph information
 */
struct GlyphInfo {
    const char* symbol; ///< The actual symbol string (e.g., LV_SYMBOL_AUDIO)
    const char* name;   ///< The symbolic name (e.g., "LV_SYMBOL_AUDIO")
};

/**
 * @brief Complete list of LVGL 9.4 symbols
 *
 * All symbols from lv_symbol_def.h
 */
static const std::vector<GlyphInfo> LVGL_SYMBOLS = {
    {LV_SYMBOL_AUDIO, "LV_SYMBOL_AUDIO"},
    {LV_SYMBOL_VIDEO, "LV_SYMBOL_VIDEO"},
    {LV_SYMBOL_LIST, "LV_SYMBOL_LIST"},
    {LV_SYMBOL_OK, "LV_SYMBOL_OK"},
    {LV_SYMBOL_CLOSE, "LV_SYMBOL_CLOSE"},
    {LV_SYMBOL_POWER, "LV_SYMBOL_POWER"},
    {LV_SYMBOL_SETTINGS, "LV_SYMBOL_SETTINGS"},
    {LV_SYMBOL_HOME, "LV_SYMBOL_HOME"},
    {LV_SYMBOL_DOWNLOAD, "LV_SYMBOL_DOWNLOAD"},
    {LV_SYMBOL_DRIVE, "LV_SYMBOL_DRIVE"},
    {LV_SYMBOL_REFRESH, "LV_SYMBOL_REFRESH"},
    {LV_SYMBOL_MUTE, "LV_SYMBOL_MUTE"},
    {LV_SYMBOL_VOLUME_MID, "LV_SYMBOL_VOLUME_MID"},
    {LV_SYMBOL_VOLUME_MAX, "LV_SYMBOL_VOLUME_MAX"},
    {LV_SYMBOL_IMAGE, "LV_SYMBOL_IMAGE"},
    {LV_SYMBOL_TINT, "LV_SYMBOL_TINT"},
    {LV_SYMBOL_PREV, "LV_SYMBOL_PREV"},
    {LV_SYMBOL_PLAY, "LV_SYMBOL_PLAY"},
    {LV_SYMBOL_PAUSE, "LV_SYMBOL_PAUSE"},
    {LV_SYMBOL_STOP, "LV_SYMBOL_STOP"},
    {LV_SYMBOL_NEXT, "LV_SYMBOL_NEXT"},
    {LV_SYMBOL_EJECT, "LV_SYMBOL_EJECT"},
    {LV_SYMBOL_LEFT, "LV_SYMBOL_LEFT"},
    {LV_SYMBOL_RIGHT, "LV_SYMBOL_RIGHT"},
    {LV_SYMBOL_PLUS, "LV_SYMBOL_PLUS"},
    {LV_SYMBOL_MINUS, "LV_SYMBOL_MINUS"},
    {LV_SYMBOL_EYE_OPEN, "LV_SYMBOL_EYE_OPEN"},
    {LV_SYMBOL_EYE_CLOSE, "LV_SYMBOL_EYE_CLOSE"},
    {LV_SYMBOL_WARNING, "LV_SYMBOL_WARNING"},
    {LV_SYMBOL_SHUFFLE, "LV_SYMBOL_SHUFFLE"},
    {LV_SYMBOL_UP, "LV_SYMBOL_UP"},
    {LV_SYMBOL_DOWN, "LV_SYMBOL_DOWN"},
    {LV_SYMBOL_LOOP, "LV_SYMBOL_LOOP"},
    {LV_SYMBOL_DIRECTORY, "LV_SYMBOL_DIRECTORY"},
    {LV_SYMBOL_UPLOAD, "LV_SYMBOL_UPLOAD"},
    {LV_SYMBOL_CALL, "LV_SYMBOL_CALL"},
    {LV_SYMBOL_CUT, "LV_SYMBOL_CUT"},
    {LV_SYMBOL_COPY, "LV_SYMBOL_COPY"},
    {LV_SYMBOL_SAVE, "LV_SYMBOL_SAVE"},
    {LV_SYMBOL_CHARGE, "LV_SYMBOL_CHARGE"},
    {LV_SYMBOL_PASTE, "LV_SYMBOL_PASTE"},
    {LV_SYMBOL_BELL, "LV_SYMBOL_BELL"},
    {LV_SYMBOL_KEYBOARD, "LV_SYMBOL_KEYBOARD"},
    {LV_SYMBOL_GPS, "LV_SYMBOL_GPS"},
    {LV_SYMBOL_FILE, "LV_SYMBOL_FILE"},
    {LV_SYMBOL_WIFI, "LV_SYMBOL_WIFI"},
    {LV_SYMBOL_BATTERY_FULL, "LV_SYMBOL_BATTERY_FULL"},
    {LV_SYMBOL_BATTERY_3, "LV_SYMBOL_BATTERY_3"},
    {LV_SYMBOL_BATTERY_2, "LV_SYMBOL_BATTERY_2"},
    {LV_SYMBOL_BATTERY_1, "LV_SYMBOL_BATTERY_1"},
    {LV_SYMBOL_BATTERY_EMPTY, "LV_SYMBOL_BATTERY_EMPTY"},
    {LV_SYMBOL_USB, "LV_SYMBOL_USB"},
    {LV_SYMBOL_BLUETOOTH, "LV_SYMBOL_BLUETOOTH"},
    {LV_SYMBOL_TRASH, "LV_SYMBOL_TRASH"},
    {LV_SYMBOL_EDIT, "LV_SYMBOL_EDIT"},
    {LV_SYMBOL_BACKSPACE, "LV_SYMBOL_BACKSPACE"},
    {LV_SYMBOL_SD_CARD, "LV_SYMBOL_SD_CARD"},
    {LV_SYMBOL_NEW_LINE, "LV_SYMBOL_NEW_LINE"},
    {LV_SYMBOL_DUMMY, "LV_SYMBOL_DUMMY"},
    {LV_SYMBOL_BULLET, "LV_SYMBOL_BULLET"}};

/**
 * @brief Create a single glyph display item
 *
 * @param parent Parent container for the item
 * @param glyph Glyph information
 * @return lv_obj_t* The created item container
 */
static lv_obj_t* create_glyph_item(lv_obj_t* parent, const GlyphInfo& glyph) {
    // Container for this glyph item
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(item, ui_theme_get_color("card_bg"), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(item, 8, 0);
    lv_obj_set_style_radius(item, 8, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, ui_theme_get_color("grey_color"), 0);
    lv_obj_set_style_border_opa(item, LV_OPA_50, 0);

    // Flex row layout: [Icon] Name
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(item, 12, 0);

    // Icon label (larger font for visibility)
    lv_obj_t* icon_label = lv_label_create(item);
    lv_label_set_text(icon_label, glyph.symbol);
    lv_obj_set_style_text_color(icon_label, ui_theme_get_color("text_primary"), 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
    lv_obj_set_width(icon_label, LV_SIZE_CONTENT);

    // Name label
    lv_obj_t* name_label = lv_label_create(item);
    lv_label_set_text(name_label, glyph.name);
    lv_obj_set_style_text_color(name_label, ui_theme_get_color("text_primary"), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, 0);
    lv_obj_set_flex_grow(name_label, 1);

    return item;
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

GlyphsPanel::GlyphsPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // GlyphsPanel doesn't use PrinterState or MoonrakerAPI, but we accept
    // them for interface consistency with other panels
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void GlyphsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // GlyphsPanel has no subjects to initialize
    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized (none required)", get_name());
}

void GlyphsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Populate the glyphs content
    populate_glyphs();
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void GlyphsPanel::populate_glyphs() {
    // Update glyph count in header
    lv_obj_t* count_label = lv_obj_find_by_name(panel_, "glyph_count_label");
    if (count_label) {
        char count_text[32];
        snprintf(count_text, sizeof(count_text), "%zu symbols", LVGL_SYMBOLS.size());
        lv_label_set_text(count_label, count_text);
    }

    // Find the scrollable content container
    // It's the second child of the main container (after header)
    lv_obj_t* main_container = lv_obj_get_child(panel_, 0);
    if (!main_container) {
        spdlog::error("[{}] Failed to find main container", get_name());
        return;
    }

    lv_obj_t* content_area = lv_obj_get_child(main_container, 1); // Second child (index 1)
    if (!content_area) {
        spdlog::error("[{}] Failed to find content area", get_name());
        return;
    }

    // Add all glyph items to the content area
    spdlog::debug("[{}] Adding {} glyph items to content area", get_name(), LVGL_SYMBOLS.size());
    for (const auto& glyph : LVGL_SYMBOLS) {
        create_glyph_item(content_area, glyph);
    }

    // Force layout update to ensure scrolling works correctly
    lv_obj_update_layout(panel_);

    spdlog::info("[{}] Setup complete with {} symbols", get_name(), LVGL_SYMBOLS.size());
}

// ============================================================================
// GLOBAL INSTANCE (needed by main.cpp)
// ============================================================================

static std::unique_ptr<GlyphsPanel> g_glyphs_panel;

GlyphsPanel& get_global_glyphs_panel() {
    if (!g_glyphs_panel) {
        g_glyphs_panel = std::make_unique<GlyphsPanel>(get_printer_state(), nullptr);
    }
    return *g_glyphs_panel;
}

// Legacy create wrapper (test panel - still used by main.cpp)
lv_obj_t* ui_panel_glyphs_create(lv_obj_t* parent) {
    auto& panel = get_global_glyphs_panel();
    if (!panel.are_subjects_initialized()) {
        panel.init_subjects();
    }
    
    lv_obj_t* glyphs_panel = static_cast<lv_obj_t*>(
        lv_xml_create(parent, panel.get_xml_component_name(), nullptr));
    if (glyphs_panel) {
        panel.setup(glyphs_panel, nullptr);
    }
    return glyphs_panel;
}
