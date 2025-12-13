// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_temp_display.h"

#include "ui_fonts.h"
#include "ui_theme.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

// ============================================================================
// Constants
// ============================================================================

/** Magic number to identify temp_display widgets ("TMP1" as ASCII) */
static constexpr uint32_t TEMP_DISPLAY_MAGIC = 0x544D5031;

// ============================================================================
// Per-widget user data
// ============================================================================

/**
 * @brief User data stored on each temp_display widget
 */
struct TempDisplayData {
    uint32_t magic = TEMP_DISPLAY_MAGIC;
    int current_temp = 0;
    int target_temp = 0;
    bool show_target = true;

    // Child label pointers for efficient updates
    lv_obj_t* current_label = nullptr;
    lv_obj_t* separator_label = nullptr;
    lv_obj_t* target_label = nullptr;
    lv_obj_t* unit_label = nullptr;
};

// Static registry for safe cleanup
static std::unordered_map<lv_obj_t*, TempDisplayData*> s_registry;

static TempDisplayData* get_data(lv_obj_t* obj) {
    auto it = s_registry.find(obj);
    return (it != s_registry.end()) ? it->second : nullptr;
}

// ============================================================================
// Internal helpers
// ============================================================================

/** Get font based on size string */
static const lv_font_t* get_font_for_size(const char* size) {
    const char* font_const = "font_body"; // default md

    if (size) {
        if (strcmp(size, "sm") == 0) {
            font_const = "font_small";
        } else if (strcmp(size, "lg") == 0) {
            font_const = "font_heading";
        }
        // "md" uses font_body (default)
    }

    const char* font_name = lv_xml_get_const(nullptr, font_const);
    if (!font_name) {
        spdlog::warn("[temp_display] Font constant '{}' not found, using default", font_const);
        return &noto_sans_18;
    }

    const lv_font_t* font = lv_xml_get_font(nullptr, font_name);
    if (!font) {
        spdlog::warn("[temp_display] Font '{}' not available, using default", font_name);
        return &noto_sans_18;
    }

    return font;
}

/**
 * @brief Update current temp label color based on heating state
 *
 * When the heater is ON (target > 0), the current temperature is displayed
 * in primary_color to provide visual feedback that heating is active.
 * When OFF, it reverts to text_primary.
 */
static void update_heating_color(TempDisplayData* data) {
    if (!data || !data->current_label)
        return;

    lv_color_t color = (data->target_temp > 0) ? ui_theme_get_color("primary_color")
                                               : ui_theme_get_color("text_primary");
    lv_obj_set_style_text_color(data->current_label, color, LV_PART_MAIN);
}

/**
 * @brief Update visibility of separator and target based on heater state
 *
 * When show_target is true but target temp is 0 (heater off), we hide the
 * separator and target to show just "XX°C" instead of "XX / --°C".
 */
static void update_target_visibility(TempDisplayData* data) {
    if (!data)
        return;

    // If show_target is false, separator and target are always hidden (set at create time)
    if (!data->show_target)
        return;

    // When show_target is true, hide separator/target dynamically if heater is off
    bool should_show = (data->target_temp > 0);

    if (data->separator_label) {
        if (should_show) {
            lv_obj_remove_flag(data->separator_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(data->separator_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (data->target_label) {
        if (should_show) {
            lv_obj_remove_flag(data->target_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(data->target_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/** Update the display text based on current values */
static void update_display(TempDisplayData* data) {
    if (!data)
        return;

    char buf[16];

    // Update current temp
    if (data->current_label) {
        snprintf(buf, sizeof(buf), "%d", data->current_temp);
        lv_label_set_text(data->current_label, buf);
    }

    // Update target temp
    if (data->target_label) {
        snprintf(buf, sizeof(buf), "%d", data->target_temp);
        lv_label_set_text(data->target_label, buf);
    }

    // Show/hide separator and target based on show_target
    if (data->separator_label) {
        if (data->show_target) {
            lv_obj_remove_flag(data->separator_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(data->separator_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (data->target_label) {
        if (data->show_target) {
            lv_obj_remove_flag(data->target_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(data->target_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update heating accent color
    update_heating_color(data);
}

/** Cleanup callback when widget is deleted */
static void on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto it = s_registry.find(obj);
    if (it != s_registry.end()) {
        delete it->second;
        s_registry.erase(it);
    }
}

// ============================================================================
// Subject observer callbacks for reactive binding
// ============================================================================

/** Observer callback for current temperature subject */
static void current_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_observer_get_target(observer));
    if (!label)
        return;

    // Get the parent container and its data
    lv_obj_t* container = lv_obj_get_parent(label);
    auto* data = get_data(container);

    // PrinterState stores temps in centidegrees (×10), convert to degrees for display
    int temp_centi = lv_subject_get_int(subject);
    int temp_deg = temp_centi / 10;

    if (data) {
        data->current_temp = temp_deg;
    }

    // Update just this label
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", temp_deg);
    lv_label_set_text(label, buf);
}

/** Observer callback for target temperature subject */
static void target_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_observer_get_target(observer));
    if (!label)
        return;

    // Get the parent container and its data
    lv_obj_t* container = lv_obj_get_parent(label);
    auto* data = get_data(container);

    // PrinterState stores temps in centidegrees (×10), convert to degrees for display
    int temp_centi = lv_subject_get_int(subject);
    int temp_deg = temp_centi / 10;

    if (data) {
        data->target_temp = temp_deg;
        // Update heating accent: primary_color when target > 0 (heater ON)
        update_heating_color(data);
        // Hide separator/target when heater is off
        update_target_visibility(data);
    }

    // Update label text (only matters when visible)
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", temp_deg);
    lv_label_set_text(label, buf);
}

// ============================================================================
// XML widget callbacks
// ============================================================================

/**
 * XML create callback for <temp_display> widget
 */
static void* ui_temp_display_create_cb(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));

    // Create main container (row layout)
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Flex row layout
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, 0, LV_PART_MAIN); // No gap between labels

    // Create user data
    auto* data = new TempDisplayData();

    // Parse size attribute for font selection
    const char* size = lv_xml_get_value_of(attrs, "size");
    const lv_font_t* font = get_font_for_size(size);
    lv_color_t text_color = ui_theme_get_color("text_primary");

    // Parse show_target attribute
    const char* show_target_str = lv_xml_get_value_of(attrs, "show_target");
    if (show_target_str && strcmp(show_target_str, "false") == 0) {
        data->show_target = false;
    }

    // Create current temp label
    data->current_label = lv_label_create(container);
    lv_label_set_text(data->current_label, "--");
    lv_obj_set_style_text_font(data->current_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(data->current_label, text_color, LV_PART_MAIN);

    // Create separator label " / "
    data->separator_label = lv_label_create(container);
    lv_label_set_text(data->separator_label, " / ");
    lv_obj_set_style_text_font(data->separator_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(data->separator_label, ui_theme_get_color("text_secondary"),
                                LV_PART_MAIN);
    if (!data->show_target) {
        lv_obj_add_flag(data->separator_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Create target temp label
    data->target_label = lv_label_create(container);
    lv_label_set_text(data->target_label, "--");
    lv_obj_set_style_text_font(data->target_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(data->target_label, text_color, LV_PART_MAIN);
    if (!data->show_target) {
        lv_obj_add_flag(data->target_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Create unit label "°C"
    data->unit_label = lv_label_create(container);
    lv_label_set_text(data->unit_label, "°C");
    lv_obj_set_style_text_font(data->unit_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(data->unit_label, ui_theme_get_color("text_secondary"),
                                LV_PART_MAIN);

    // Register data and cleanup
    s_registry[container] = data;
    lv_obj_add_event_cb(container, on_delete, LV_EVENT_DELETE, nullptr);

    spdlog::trace("[temp_display] Created widget (size={}, show_target={})", size ? size : "md",
                  data->show_target);

    return container;
}

/**
 * XML apply callback for <temp_display> widget
 * Handles bind_current and bind_target for reactive binding
 */
static void ui_temp_display_apply_cb(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* container = static_cast<lv_obj_t*>(lv_xml_state_get_item(state));
    auto* data = get_data(container);

    // Process custom binding attributes
    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (strcmp(name, "bind_current") == 0) {
            // Bind current temperature to a subject (NULL = global scope)
            lv_subject_t* subject = lv_xml_get_subject(NULL, value);
            if (subject && data && data->current_label) {
                lv_subject_add_observer_obj(subject, current_temp_observer_cb, data->current_label,
                                            nullptr);
                // Set initial value (convert centidegrees to degrees)
                int temp_centi = lv_subject_get_int(subject);
                data->current_temp = temp_centi / 10;
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", data->current_temp);
                lv_label_set_text(data->current_label, buf);
                spdlog::trace("[temp_display] Bound current to subject '{}' ({}°C)", value,
                              data->current_temp);
            } else if (!subject) {
                spdlog::warn("[temp_display] Subject '{}' not found for bind_current", value);
            }
        } else if (strcmp(name, "bind_target") == 0) {
            // Bind target temperature to a subject (NULL = global scope)
            lv_subject_t* subject = lv_xml_get_subject(NULL, value);
            if (subject && data && data->target_label) {
                lv_subject_add_observer_obj(subject, target_temp_observer_cb, data->target_label,
                                            nullptr);
                // Set initial value (convert centidegrees to degrees)
                int temp_centi = lv_subject_get_int(subject);
                data->target_temp = temp_centi / 10;
                // Set label text
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", data->target_temp);
                lv_label_set_text(data->target_label, buf);
                // Apply initial heating color (primary_color if target > 0)
                update_heating_color(data);
                // Hide separator/target when heater is off
                update_target_visibility(data);
                spdlog::trace("[temp_display] Bound target to subject '{}' ({}°C)", value,
                              data->target_temp);
            } else if (!subject) {
                spdlog::warn("[temp_display] Subject '{}' not found for bind_target", value);
            }
        }
    }

    // Apply base object properties (width, height, align, style_* etc.)
    lv_xml_obj_apply(state, attrs);
}

// ============================================================================
// Public API
// ============================================================================

void ui_temp_display_init(void) {
    lv_xml_register_widget("temp_display", ui_temp_display_create_cb, ui_temp_display_apply_cb);
    spdlog::debug("[temp_display] Registered temp_display widget");
}

void ui_temp_display_set(lv_obj_t* obj, int current, int target) {
    auto* data = get_data(obj);
    if (!data) {
        spdlog::warn("[temp_display] ui_temp_display_set called on non-temp_display widget");
        return;
    }

    data->current_temp = current;
    data->target_temp = target;
    update_display(data);
}

void ui_temp_display_set_current(lv_obj_t* obj, int current) {
    auto* data = get_data(obj);
    if (!data) {
        return;
    }

    data->current_temp = current;

    // Only update current label for efficiency
    if (data->current_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current);
        lv_label_set_text(data->current_label, buf);
    }
}

int ui_temp_display_get_current(lv_obj_t* obj) {
    auto* data = get_data(obj);
    return data ? data->current_temp : -1;
}

int ui_temp_display_get_target(lv_obj_t* obj) {
    auto* data = get_data(obj);
    return data ? data->target_temp : -1;
}

bool ui_temp_display_is_valid(lv_obj_t* obj) {
    auto* data = get_data(obj);
    return data && data->magic == TEMP_DISPLAY_MAGIC;
}
