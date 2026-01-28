// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_icon.h"

#include "ui_fonts.h"
#include "ui_icon_codepoints.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_component.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_style.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_core.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>

/**
 * Icon size enum - maps to MDI font sizes
 */
enum class IconSize { XS, SM, MD, LG, XL };

/**
 * Variant mapping: semantic name -> color styling
 *
 * NEW semantic names (Phase 2.2):
 *   TEXT     - Primary text color (was PRIMARY)
 *   MUTED    - De-emphasized text (was SECONDARY)
 *   PRIMARY  - Accent/brand color (was ACCENT)
 *   SECONDARY- Secondary accent color (NEW)
 *   TERTIARY - Tertiary accent color (NEW)
 *   DANGER   - Error/danger state (was ERROR)
 *   INFO     - Info state (NEW)
 */
enum class IconVariant {
    NONE,      // Uses text style (default)
    TEXT,      // Primary text color
    MUTED,     // De-emphasized text
    PRIMARY,   // Accent/brand color
    SECONDARY, // Secondary accent
    TERTIARY,  // Tertiary accent
    DISABLED,  // Text @ 50% opacity
    SUCCESS,
    WARNING,
    DANGER, // Was ERROR
    INFO    // NEW
};

/**
 * Parse size string to IconSize enum
 */
static IconSize parse_size(const char* size_str) {
    if (!size_str || strlen(size_str) == 0) {
        return IconSize::XL; // Default
    }

    if (strcmp(size_str, "xs") == 0) {
        return IconSize::XS;
    } else if (strcmp(size_str, "sm") == 0) {
        return IconSize::SM;
    } else if (strcmp(size_str, "md") == 0) {
        return IconSize::MD;
    } else if (strcmp(size_str, "lg") == 0) {
        return IconSize::LG;
    } else if (strcmp(size_str, "xl") == 0) {
        return IconSize::XL;
    }

    spdlog::warn("[Icon] Invalid size '{}', using default 'xl'", size_str);
    return IconSize::XL;
}

/**
 * Get the MDI font for a given size
 */
static const lv_font_t* get_font_for_size(IconSize size) {
    switch (size) {
    case IconSize::XS:
        return &mdi_icons_16;
    case IconSize::SM:
        return &mdi_icons_24;
    case IconSize::MD:
        return &mdi_icons_32;
    case IconSize::LG:
        return &mdi_icons_48;
    case IconSize::XL:
    default:
        return &mdi_icons_64;
    }
}

/**
 * Parse variant string to IconVariant enum
 *
 * Semantic variant names:
 *   "text"      -> TEXT (primary text color)
 *   "muted"     -> MUTED (de-emphasized text)
 *   "primary"   -> PRIMARY (accent/brand color)
 *   "secondary" -> SECONDARY (secondary accent color)
 *   "tertiary"  -> TERTIARY (tertiary accent color)
 *   "disabled"  -> DISABLED (text @ 50% opacity)
 *   "success"   -> SUCCESS (green)
 *   "warning"   -> WARNING (amber)
 *   "danger"    -> DANGER (red)
 *   "info"      -> INFO (blue)
 *   "none"      -> NONE (uses text style)
 */
static IconVariant parse_variant(const char* variant_str) {
    if (!variant_str || strlen(variant_str) == 0) {
        return IconVariant::NONE;
    } else if (strcmp(variant_str, "text") == 0) {
        return IconVariant::TEXT;
    } else if (strcmp(variant_str, "muted") == 0) {
        return IconVariant::MUTED;
    } else if (strcmp(variant_str, "primary") == 0) {
        return IconVariant::PRIMARY;
    } else if (strcmp(variant_str, "secondary") == 0) {
        return IconVariant::SECONDARY;
    } else if (strcmp(variant_str, "tertiary") == 0) {
        return IconVariant::TERTIARY;
    } else if (strcmp(variant_str, "disabled") == 0) {
        return IconVariant::DISABLED;
    } else if (strcmp(variant_str, "success") == 0) {
        return IconVariant::SUCCESS;
    } else if (strcmp(variant_str, "warning") == 0) {
        return IconVariant::WARNING;
    } else if (strcmp(variant_str, "danger") == 0) {
        return IconVariant::DANGER;
    } else if (strcmp(variant_str, "info") == 0) {
        return IconVariant::INFO;
    } else if (strcmp(variant_str, "none") == 0) {
        return IconVariant::NONE;
    }

    spdlog::warn("[Icon] Invalid variant '{}', using default 'none'", variant_str);
    return IconVariant::NONE;
}

/**
 * Apply size to icon widget (font only - let content determine dimensions)
 *
 * Uses LV_SIZE_CONTENT so the widget automatically sizes to fit the font glyph.
 * This prevents clipping when font line_height differs from nominal size
 * (e.g., 32px font may have 33px line_height due to glyph bounding boxes).
 */
static void apply_size(lv_obj_t* obj, IconSize size) {
    const lv_font_t* font = get_font_for_size(size);

    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
}

/**
 * Remove any previously applied icon variant style from the object.
 *
 * This prevents style accumulation when changing variants multiple times.
 * We remove all known icon styles - only one will be present at a time.
 */
static void remove_icon_styles(lv_obj_t* obj) {
    // Remove all possible icon styles - only one should be attached
    lv_style_t* styles_to_remove[] = {
        theme_core_get_icon_text_style(),     theme_core_get_icon_muted_style(),
        theme_core_get_icon_primary_style(),  theme_core_get_icon_secondary_style(),
        theme_core_get_icon_tertiary_style(), theme_core_get_icon_success_style(),
        theme_core_get_icon_warning_style(),  theme_core_get_icon_danger_style(),
        theme_core_get_icon_info_style(),
    };

    for (lv_style_t* style : styles_to_remove) {
        if (style) {
            lv_obj_remove_style(obj, style, LV_PART_MAIN);
        }
    }
}

/**
 * Apply variant color styling to icon widget
 *
 * Uses shared styles from theme_core instead of inline colors. This enables
 * reactive theming - when theme_core_update_colors() is called, icons
 * automatically update because they reference the shared style objects.
 *
 * Removes any previously applied icon style first to prevent accumulation.
 */
static void apply_variant(lv_obj_t* obj, IconVariant variant) {
    // Remove any existing icon style first to prevent accumulation
    remove_icon_styles(obj);

    lv_style_t* style = nullptr;
    lv_opa_t opa = LV_OPA_COVER;

    switch (variant) {
    case IconVariant::TEXT:
    case IconVariant::NONE:
        style = theme_core_get_icon_text_style();
        break;
    case IconVariant::MUTED:
        style = theme_core_get_icon_muted_style();
        break;
    case IconVariant::PRIMARY:
        style = theme_core_get_icon_primary_style();
        break;
    case IconVariant::SECONDARY:
        style = theme_core_get_icon_secondary_style();
        break;
    case IconVariant::TERTIARY:
        style = theme_core_get_icon_tertiary_style();
        break;
    case IconVariant::DISABLED:
        style = theme_core_get_icon_text_style();
        opa = LV_OPA_50;
        break;
    case IconVariant::SUCCESS:
        style = theme_core_get_icon_success_style();
        break;
    case IconVariant::WARNING:
        style = theme_core_get_icon_warning_style();
        break;
    case IconVariant::DANGER:
        style = theme_core_get_icon_danger_style();
        break;
    case IconVariant::INFO:
        style = theme_core_get_icon_info_style();
        break;
    }

    if (style) {
        lv_obj_add_style(obj, style, LV_PART_MAIN);
        lv_obj_set_style_text_opa(obj, opa, LV_PART_MAIN);
    } else {
        spdlog::error("[Icon] Failed to get style for variant - theme not initialized?");
    }
}

/**
 * Apply icon source (lookup codepoint and set label text)
 */
static void apply_source(lv_obj_t* obj, const char* src) {
    if (!src || strlen(src) == 0) {
        src = "image_broken_variant"; // Default icon (broken image)
    }

    // Try direct lookup first
    const char* codepoint = ui_icon::lookup_codepoint(src);

    // If not found, try stripping legacy "mat_" prefix and "_img" suffix
    if (!codepoint) {
        const char* stripped = ui_icon::strip_legacy_prefix(src);
        if (stripped != src) {
            codepoint = ui_icon::lookup_codepoint(stripped);
        }
    }

    if (codepoint) {
        lv_label_set_text(obj, codepoint);
        spdlog::trace("[Icon] Set icon '{}' -> codepoint", src);
    } else {
        // Fallback to broken image icon
        const char* fallback = ui_icon::lookup_codepoint("image_broken_variant");
        if (fallback) {
            lv_label_set_text(obj, fallback);
        }
        spdlog::warn("[Icon] Icon '{}' not found, using 'image_broken_variant' fallback", src);
    }
}

/**
 * XML create function for icon widget
 * Called by LVGL XML parser when <icon> is encountered
 */
static void* ui_icon_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    (void)attrs;
    lv_obj_t* parent = (lv_obj_t*)lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_label_create(parent);

    // Apply default size (xl = 64px)
    apply_size(obj, IconSize::XL);

    // Apply default source (broken image icon)
    apply_source(obj, "image_broken_variant");

    // Default variant (primary text color)
    apply_variant(obj, IconVariant::NONE);

    return obj;
}

/**
 * XML apply function for icon widget
 * Called by LVGL XML parser to process attributes
 */
static void ui_icon_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* obj = (lv_obj_t*)lv_xml_state_get_item(state);

    // First apply common lv_obj properties (width, height, align, etc.)
    lv_xml_obj_apply(state, attrs);

    // Then process icon-specific properties
    IconSize size = IconSize::XL;
    IconVariant variant = IconVariant::NONE;
    const char* src = nullptr;
    bool size_set = false;
    bool variant_set = false;
    bool custom_color_set = false;
    lv_color_t custom_color;

    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (strcmp(name, "src") == 0) {
            src = value;
        } else if (strcmp(name, "size") == 0) {
            size = parse_size(value);
            size_set = true;
        } else if (strcmp(name, "variant") == 0) {
            variant = parse_variant(value);
            variant_set = true;
        } else if (strcmp(name, "color") == 0) {
            custom_color = lv_xml_to_color(value);
            custom_color_set = true;
        }
    }

    // Apply size first (sets font)
    if (size_set) {
        apply_size(obj, size);
    }

    // Apply source (sets label text to codepoint)
    if (src) {
        apply_source(obj, src);
    }

    // Custom color overrides variant
    if (custom_color_set) {
        lv_obj_set_style_text_color(obj, custom_color, LV_PART_MAIN);
        lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    } else if (variant_set) {
        apply_variant(obj, variant);
    }
}

/**
 * Register the icon widget with LVGL's XML system
 */
void ui_icon_register_widget() {
    lv_xml_register_widget("icon", ui_icon_xml_create, ui_icon_xml_apply);
    spdlog::trace("[Icon] Font-based icon widget registered with XML system");
}

// Public API implementations

void ui_icon_set_source(lv_obj_t* icon, const char* icon_name) {
    if (!icon || !icon_name) {
        spdlog::error("[Icon] Invalid parameters to ui_icon_set_source");
        return;
    }

    apply_source(icon, icon_name);
    spdlog::trace("[Icon] Changed icon source to '{}'", icon_name);
}

void ui_icon_set_size(lv_obj_t* icon, const char* size_str) {
    if (!icon || !size_str) {
        spdlog::error("[Icon] Invalid parameters to ui_icon_set_size");
        return;
    }

    IconSize size = parse_size(size_str);
    apply_size(icon, size);
    spdlog::trace("[Icon] Changed icon size to '{}'", size_str);
}

void ui_icon_set_variant(lv_obj_t* icon, const char* variant_str) {
    if (!icon || !variant_str) {
        spdlog::error("[Icon] Invalid parameters to ui_icon_set_variant");
        return;
    }

    IconVariant variant = parse_variant(variant_str);
    apply_variant(icon, variant);
    spdlog::trace("[Icon] Changed icon variant to '{}'", variant_str);
}

void ui_icon_set_color(lv_obj_t* icon, lv_color_t color, lv_opa_t opa) {
    if (!icon) {
        spdlog::error("[Icon] Invalid icon parameter to ui_icon_set_color");
        return;
    }

    lv_obj_set_style_text_color(icon, color, LV_PART_MAIN);
    lv_obj_set_style_text_opa(icon, opa, LV_PART_MAIN);
    spdlog::trace("[Icon] Set custom color (opa: {})", opa);
}

void ui_icon_set_clickable(lv_obj_t* icon, bool clickable) {
    if (!icon) {
        spdlog::error("[Icon] Invalid icon parameter to ui_icon_set_clickable");
        return;
    }

    if (clickable) {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
    spdlog::trace("[Icon] Set clickable: {}", clickable);
}
