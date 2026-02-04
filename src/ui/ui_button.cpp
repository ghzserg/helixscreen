// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_button.h"

#include "ui_fonts.h"
#include "ui_icon_codepoints.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace {

// User data stored on button to track icon/label positions
// NOTE: Magic number required because Modal::wire_button overwrites user_data
// with a Modal* pointer. Without this check, button_delete_cb would try to
// delete a Modal* as if it were UiButtonData*, causing a crash on shutdown.
struct UiButtonData {
    static constexpr uint32_t MAGIC = 0x42544E31; // "BTN1"
    uint32_t magic{MAGIC};
    lv_obj_t* icon;     // Icon widget (or nullptr if none)
    lv_obj_t* label;    // Label widget (always present)
    bool icon_on_right; // true if icon is after text
};

/**
 * @brief Get icon font for button icons (24px MDI)
 * @return Pointer to the MDI 24px icon font
 */
static const lv_font_t* get_button_icon_font() {
    return &mdi_icons_24;
}

/**
 * @brief Update button label and icon text color based on button bg luminance
 *
 * Computes luminance using standard formula:
 *   L = (299*R + 587*G + 114*B) / 1000
 *
 * If L < 128 (dark bg): use light text color
 * If L >= 128 (light bg): use dark text color
 *
 * @param btn The button widget
 */
void update_button_text_contrast(lv_obj_t* btn) {
    // Get user data to find icon and label
    // Check magic to ensure user_data hasn't been overwritten (e.g., by Modal::wire_button)
    UiButtonData* data = static_cast<UiButtonData*>(lv_obj_get_user_data(btn));
    if (!data || data->magic != UiButtonData::MAGIC) {
        // Normal for buttons whose user_data was repurposed (e.g., Modal::wire_button)
        return;
    }

    // Need at least one of icon or label to update
    if (!data->label && !data->icon) {
        // Normal during creation sequence - label/icon not yet attached
        return;
    }

    bool is_disabled = lv_obj_has_state(btn, LV_STATE_DISABLED);
    lv_opa_t bg_opa = lv_obj_get_style_bg_opa(btn, LV_PART_MAIN);
    bool is_ghost = bg_opa < LV_OPA_50;

    // Determine text color based on button type
    lv_color_t text_color;
    if (is_ghost) {
        // Ghost/transparent button - use theme text color
        text_color = theme_manager_get_color("text");
    } else {
        // Solid button - calculate contrast against effective background
        lv_color_t bg = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);

        // Disabled buttons render at 50% opacity, so blend with screen bg
        // to get the effective color for contrast calculation
        if (is_disabled) {
            lv_color_t screen_bg = theme_manager_get_color("screen_bg");
            bg = lv_color_mix(bg, screen_bg, LV_OPA_50);
        }

        text_color = theme_manager_get_contrast_text(bg);
    }

    spdlog::trace("[ui_button] contrast: ghost={} disabled={} text=0x{:06X}", is_ghost, is_disabled,
                  lv_color_to_u32(text_color) & 0xFFFFFF);

    // Text opacity: slightly higher than button opacity (50%) for disabled state
    // to maintain readability while indicating disabled
    lv_opa_t text_opa = is_disabled ? LV_OPA_70 : LV_OPA_COVER;

    // Apply color and opacity to label and icon
    auto apply_text_style = [&](lv_obj_t* obj) {
        if (obj) {
            lv_obj_set_style_text_color(obj, text_color, LV_PART_MAIN);
            lv_obj_set_style_text_opa(obj, text_opa, LV_PART_MAIN);
        }
    };

    apply_text_style(data->label);
    apply_text_style(data->icon);
}

/**
 * @brief Event callback for LV_EVENT_STYLE_CHANGED and LV_EVENT_STATE_CHANGED
 *
 * Called when button style changes (e.g., theme update) or state changes
 * (e.g., disabled via bind_state_if_eq). Recalculates and applies text contrast.
 *
 * @param e Event object
 */
void button_style_changed_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target_obj(e);
    update_button_text_contrast(btn);
}

/**
 * @brief Event callback for LV_EVENT_DELETE
 *
 * Called when button is deleted. Frees the UiButtonData user data.
 *
 * @param e Event object
 */
void button_delete_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target_obj(e);
    UiButtonData* data = static_cast<UiButtonData*>(lv_obj_get_user_data(btn));
    // Only delete if magic matches - user_data may have been overwritten
    // by Modal::wire_button with a Modal* pointer
    if (data && data->magic == UiButtonData::MAGIC) {
        delete data;
        lv_obj_set_user_data(btn, nullptr);
    }
}

/**
 * @brief Create icon widget for button
 *
 * Creates a font-based MDI icon label inside the button.
 *
 * @param btn Parent button widget
 * @param icon_name Icon name (e.g., "settings", "heat_wave")
 * @return Created icon widget or nullptr on failure
 */
lv_obj_t* create_button_icon(lv_obj_t* btn, const char* icon_name) {
    if (!icon_name || strlen(icon_name) == 0) {
        return nullptr;
    }

    // Lookup icon codepoint
    const char* codepoint = ui_icon::lookup_codepoint(icon_name);
    if (!codepoint) {
        // Try stripping legacy prefix
        const char* stripped = ui_icon::strip_legacy_prefix(icon_name);
        if (stripped != icon_name) {
            codepoint = ui_icon::lookup_codepoint(stripped);
        }
    }

    if (!codepoint) {
        spdlog::warn("[ui_button] Icon '{}' not found", icon_name);
        return nullptr;
    }

    // Create icon as lv_label with MDI font
    lv_obj_t* icon = lv_label_create(btn);
    lv_label_set_text(icon, codepoint);
    lv_obj_set_style_text_font(icon, get_button_icon_font(), LV_PART_MAIN);

    spdlog::trace("[ui_button] Created icon '{}' -> codepoint", icon_name);
    return icon;
}

/**
 * @brief XML create callback for <ui_button> widget
 *
 * Creates a semantic button with:
 * - lv_button as base widget
 * - Shared style based on variant (primary/secondary/danger/ghost)
 * - Optional icon with auto-contrast
 * - Child lv_label with text attribute
 * - LV_EVENT_STYLE_CHANGED handler for auto-contrast updates
 *
 * Attributes:
 * - variant: Button style (primary/secondary/danger/success/tertiary/warning/ghost)
 * - text: Button label text
 * - icon: Optional icon name (e.g., "settings", "heat_wave")
 * - icon_position: "left" (default) or "right"
 *
 * @param state XML parser state
 * @param attrs XML attributes
 * @return Created button object
 */
void* ui_button_create(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));

    // Create button with default height from theme system
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_height(btn, theme_manager_get_spacing("button_height"));

    // Parse variant attribute (default: primary)
    const char* variant_str = lv_xml_get_value_of(attrs, "variant");
    if (!variant_str) {
        variant_str = "primary";
    }

    // Apply shared style based on variant
    auto& tm = ThemeManager::instance();
    lv_style_t* style = nullptr;
    if (strcmp(variant_str, "primary") == 0) {
        style = tm.get_style(StyleRole::ButtonPrimary);
    } else if (strcmp(variant_str, "secondary") == 0) {
        style = tm.get_style(StyleRole::ButtonSecondary);
    } else if (strcmp(variant_str, "danger") == 0) {
        style = tm.get_style(StyleRole::ButtonDanger);
    } else if (strcmp(variant_str, "success") == 0) {
        style = tm.get_style(StyleRole::ButtonSuccess);
    } else if (strcmp(variant_str, "tertiary") == 0) {
        style = tm.get_style(StyleRole::ButtonTertiary);
    } else if (strcmp(variant_str, "warning") == 0) {
        style = tm.get_style(StyleRole::ButtonWarning);
    } else if (strcmp(variant_str, "ghost") == 0) {
        style = tm.get_style(StyleRole::ButtonGhost);
    } else {
        spdlog::warn("[ui_button] Unknown variant '{}', defaulting to primary", variant_str);
        style = tm.get_style(StyleRole::ButtonPrimary);
    }

    if (style) {
        lv_obj_add_style(btn, style, LV_PART_MAIN);
    }

    // Parse text attribute
    const char* text = lv_xml_get_value_of(attrs, "text");
    if (!text) {
        text = "";
    }

    // Parse icon attribute
    const char* icon_name = lv_xml_get_value_of(attrs, "icon");

    // Parse icon_position attribute (default: left)
    // Supported values: "left" (default), "right", "top", "bottom"
    const char* icon_pos_str = lv_xml_get_value_of(attrs, "icon_position");
    bool icon_on_right = (icon_pos_str && strcmp(icon_pos_str, "right") == 0);
    bool icon_on_top = (icon_pos_str && strcmp(icon_pos_str, "top") == 0);
    bool icon_on_bottom = (icon_pos_str && strcmp(icon_pos_str, "bottom") == 0);
    bool vertical_layout = icon_on_top || icon_on_bottom;

    // Parse layout attribute for explicit flex direction on button children
    // Supported values: "row", "column"
    // This enables stacking text + XML children properly
    const char* layout_str = lv_xml_get_value_of(attrs, "layout");
    bool explicit_column = (layout_str && strcmp(layout_str, "column") == 0);
    bool explicit_row = (layout_str && strcmp(layout_str, "row") == 0);
    if (explicit_column) {
        vertical_layout = true;
    }

    // Allocate user data to track icon/label
    UiButtonData* data = new UiButtonData{.magic = UiButtonData::MAGIC,
                                          .icon = nullptr,
                                          .label = nullptr,
                                          .icon_on_right = icon_on_right};

    bool has_icon = (icon_name && strlen(icon_name) > 0);
    bool has_text = (text && strlen(text) > 0);

    if (has_icon && has_text) {
        if (vertical_layout) {
            // Icon + text: use vertical flex layout (column)
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            // No pad_row - use pad_top on label to match Motors Off button style
            lv_obj_set_style_pad_row(btn, 0, LV_PART_MAIN);

            if (icon_on_bottom) {
                // Text first, then icon
                data->label = lv_label_create(btn);
                lv_label_set_text(data->label, text);
                data->icon = create_button_icon(btn, icon_name);
            } else {
                // Icon first (top), then text
                data->icon = create_button_icon(btn, icon_name);
                data->label = lv_label_create(btn);
                lv_label_set_text(data->label, text);
            }
            // Use small font for vertical layout labels (matches text_small)
            if (data->label) {
                lv_obj_set_style_text_font(data->label, theme_manager_get_font("font_small"),
                                           LV_PART_MAIN);
                lv_obj_set_style_pad_top(data->label, theme_manager_get_spacing("space_xxs"),
                                         LV_PART_MAIN);
            }
        } else {
            // Icon + text: use horizontal flex layout (row)
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);

            if (icon_on_right) {
                // Text first, then icon
                data->label = lv_label_create(btn);
                lv_label_set_text(data->label, text);
                data->icon = create_button_icon(btn, icon_name);
            } else {
                // Icon first (left), then text
                data->icon = create_button_icon(btn, icon_name);
                data->label = lv_label_create(btn);
                lv_label_set_text(data->label, text);
            }
        }
    } else if (has_icon) {
        // Icon only: center the icon, no label needed
        data->icon = create_button_icon(btn, icon_name);
        if (data->icon) {
            lv_obj_center(data->icon);
        }
    } else if (has_text) {
        if (explicit_column) {
            // Text with column layout: set up flex for XML children below
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            data->label = lv_label_create(btn);
            lv_label_set_text(data->label, text);
        } else if (explicit_row) {
            // Text with row layout: set up flex for XML children beside
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);
            data->label = lv_label_create(btn);
            lv_label_set_text(data->label, text);
        } else {
            // Text only without explicit layout: center the label
            data->label = lv_label_create(btn);
            lv_label_set_text(data->label, text);
            lv_obj_center(data->label);
        }
    } else if (explicit_column || explicit_row) {
        // No icon, no text, but explicit layout: set up flex for XML children
        if (explicit_column) {
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
        } else {
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);
        }
    }
    // else: No icon, no text, no layout - leave button empty for XML children

    // Store user data on button
    lv_obj_set_user_data(btn, data);

    // Register event handlers
    lv_obj_add_event_cb(btn, button_style_changed_cb, LV_EVENT_STYLE_CHANGED, nullptr);
    lv_obj_add_event_cb(btn, button_style_changed_cb, LV_EVENT_STATE_CHANGED, nullptr);
    lv_obj_add_event_cb(btn, button_delete_cb, LV_EVENT_DELETE, nullptr);

    // Apply initial text contrast
    update_button_text_contrast(btn);

    const char* pos_name = icon_on_top      ? "top"
                           : icon_on_bottom ? "bottom"
                           : icon_on_right  ? "right"
                                            : "left";
    const char* layout_name = explicit_column ? "column" : (explicit_row ? "row" : "auto");
    spdlog::trace(
        "[ui_button] Created button variant='{}' text='{}' icon='{}' icon_pos='{}' layout='{}'",
        variant_str, text, icon_name ? icon_name : "", pos_name, layout_name);

    return btn;
}

/**
 * @brief Observer callback for bind_icon subject changes
 *
 * Updates the icon label text when the bound string subject changes.
 * The icon label is stored in user_data, and the button is in user_data of observer.
 *
 * @param observer The LVGL observer
 * @param subject The subject that changed (string subject with icon name)
 */
void icon_subject_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    lv_obj_t* icon = static_cast<lv_obj_t*>(lv_observer_get_target_obj(observer));
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_observer_get_user_data(observer));

    if (!icon || !lv_obj_is_valid(icon)) {
        return;
    }

    const char* icon_value = lv_subject_get_string(subject);
    if (!icon_value || strlen(icon_value) == 0) {
        spdlog::trace("[ui_button] bind_icon: empty icon value, keeping current");
        return;
    }

    // Check if value is already a UTF-8 codepoint (starts with high byte 0xF0+)
    // MDI icons are in the Unicode Private Use Area, encoded as 4-byte UTF-8
    // starting with 0xF3 (U+F0000..U+FFFFF range)
    const unsigned char first_byte = static_cast<unsigned char>(icon_value[0]);
    if (first_byte >= 0xF0) {
        // Value is already a codepoint - use directly
        lv_label_set_text(icon, icon_value);
        spdlog::trace("[ui_button] bind_icon: updated to codepoint");
    } else {
        // Value is an icon name - look up the codepoint
        const char* codepoint = ui_icon::lookup_codepoint(icon_value);
        if (!codepoint) {
            // Try stripping legacy prefix
            const char* stripped = ui_icon::strip_legacy_prefix(icon_value);
            if (stripped != icon_value) {
                codepoint = ui_icon::lookup_codepoint(stripped);
            }
        }

        if (codepoint) {
            lv_label_set_text(icon, codepoint);
            spdlog::trace("[ui_button] bind_icon: updated to '{}'", icon_value);
        } else {
            spdlog::warn("[ui_button] Icon '{}' not found during bind_icon update", icon_value);
            return;
        }
    }

    // Re-apply text contrast if we have the button reference
    if (btn && lv_obj_is_valid(btn)) {
        update_button_text_contrast(btn);
    }
}

/**
 * @brief XML apply callback for <ui_button> widget
 *
 * Delegates to standard object parser for base properties (align, hidden, etc.)
 * Also sets derived names for icon/label children if the button has a name.
 *
 * @param state XML parser state
 * @param attrs XML attributes
 */
void ui_button_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_xml_obj_apply(state, attrs);

    void* item = lv_xml_state_get_item(state);
    lv_obj_t* btn = static_cast<lv_obj_t*>(item);
    UiButtonData* data = static_cast<UiButtonData*>(lv_obj_get_user_data(btn));

    // Handle bind_text - bind the internal label to a subject
    const char* bind_text = lv_xml_get_value_of(attrs, "bind_text");
    if (bind_text && data && data->magic == UiButtonData::MAGIC) {
        lv_subject_t* subject = lv_xml_get_subject(&state->scope, bind_text);
        if (subject) {
            // If button has no label yet (text was empty), create one now
            if (!data->label) {
                data->label = lv_label_create(btn);
                lv_obj_center(data->label);
            }
            // Get optional format string
            const char* fmt = lv_xml_get_value_of(attrs, "bind_text-fmt");
            if (fmt) {
                fmt = lv_strdup(fmt);
                lv_obj_add_event_cb(data->label, lv_event_free_user_data_cb, LV_EVENT_DELETE,
                                    const_cast<char*>(fmt));
            }
            lv_label_bind_text(data->label, subject, fmt);
            // Re-apply contrast after binding updates text
            update_button_text_contrast(btn);
            spdlog::trace("[ui_button] Bound label to subject '{}'", bind_text);
        } else {
            spdlog::warn("[ui_button] Subject '{}' not found for bind_text", bind_text);
        }
    }

    // Handle bind_icon - bind the internal icon to a string subject
    const char* bind_icon = lv_xml_get_value_of(attrs, "bind_icon");
    if (bind_icon && data && data->magic == UiButtonData::MAGIC) {
        lv_subject_t* subject = lv_xml_get_subject(&state->scope, bind_icon);
        if (subject) {
            // If button has no icon yet, create one now
            if (!data->icon) {
                data->icon = lv_label_create(btn);
                lv_obj_set_style_text_font(data->icon, get_button_icon_font(), LV_PART_MAIN);

                // Position icon appropriately
                if (data->label) {
                    // Icon + text: set up flex layout
                    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                          LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_column(btn, theme_manager_get_spacing("space_xs"),
                                                LV_PART_MAIN);
                    // Clear any centering on the label (it was centered when text-only)
                    lv_obj_set_align(data->label, LV_ALIGN_DEFAULT);
                    // Position icon before or after text based on icon_on_right
                    lv_obj_move_to_index(data->icon, data->icon_on_right ? -1 : 0);
                    spdlog::trace("[ui_button] bind_icon: created icon with flex layout");
                } else {
                    // Icon only: center it
                    lv_obj_center(data->icon);
                    spdlog::trace("[ui_button] bind_icon: created centered icon-only");
                }
            }

            // Get initial icon value from subject and set icon text
            // Value can be either an icon name ("light") or raw codepoint ("\xF3\xB0\x8C\xB6")
            const char* icon_value = lv_subject_get_string(subject);
            if (icon_value && strlen(icon_value) > 0) {
                const unsigned char first_byte = static_cast<unsigned char>(icon_value[0]);
                if (first_byte >= 0xF0) {
                    // Value is already a codepoint - use directly
                    lv_label_set_text(data->icon, icon_value);
                } else {
                    // Value is an icon name - look up the codepoint
                    const char* codepoint = ui_icon::lookup_codepoint(icon_value);
                    if (!codepoint) {
                        // Try stripping legacy prefix
                        const char* stripped = ui_icon::strip_legacy_prefix(icon_value);
                        if (stripped != icon_value) {
                            codepoint = ui_icon::lookup_codepoint(stripped);
                        }
                    }
                    if (codepoint) {
                        lv_label_set_text(data->icon, codepoint);
                    } else {
                        spdlog::warn("[ui_button] Icon '{}' not found for bind_icon", icon_value);
                    }
                }
            }

            // Add observer to update icon when subject changes
            lv_subject_add_observer_obj(subject, icon_subject_observer_cb, data->icon, btn);

            // Re-apply contrast after adding icon
            update_button_text_contrast(btn);
            spdlog::trace("[ui_button] Bound icon to subject '{}'", bind_icon);
        } else {
            spdlog::warn("[ui_button] Subject '{}' not found for bind_icon", bind_icon);
        }
    }

    // If button has a name, give icon a derived name so it can be found
    const char* btn_name = lv_obj_get_name(btn);
    if (btn_name && strlen(btn_name) > 0) {
        if (data && data->magic == UiButtonData::MAGIC && data->icon) {
            // Set icon name as "{button_name}_icon"
            char icon_name[128];
            snprintf(icon_name, sizeof(icon_name), "%s_icon", btn_name);
            lv_obj_set_name(data->icon, icon_name);
            spdlog::trace("[ui_button] Set icon name to '{}'", icon_name);
        }
    }
}

} // namespace

void ui_button_init() {
    lv_xml_register_widget("ui_button", ui_button_create, ui_button_apply);
    spdlog::debug("[ui_button] Registered semantic button widget");
}
