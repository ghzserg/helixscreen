// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_slot.h"

#include "ui_fonts.h"
#include "ui_icon_codepoints.h"
#include "ui_observer_guard.h"
#include "ui_spool_canvas.h"
#include "ui_theme.h"

#include "ams_state.h"
#include "ams_types.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <unordered_map>

// ============================================================================
// Per-widget user data (managed via static registry for safe shutdown)
// ============================================================================

/**
 * @brief Check if 3D spool visualization is enabled in config
 * @return true if "3d" style, false for "flat" style
 */
static bool is_3d_spool_style() {
    Config* cfg = Config::get_instance();
    std::string style = cfg->get<std::string>("/ams/spool_style", "3d");
    return (style == "3d");
}

/**
 * @brief User data stored on each ams_slot widget
 *
 * Contains the slot index and observer handles. Managed via static registry
 * rather than lv_obj user_data to ensure safe cleanup during lv_deinit().
 */
struct AmsSlotData {
    int slot_index = -1;
    bool use_3d_style = true; // Cached style setting

    // RAII observer handles - automatically removed when this struct is destroyed
    ObserverGuard color_observer;
    ObserverGuard status_observer;
    ObserverGuard current_gate_observer;
    ObserverGuard filament_loaded_observer;

    // Skeuomorphic spool visualization layers (flat style)
    lv_obj_t* spool_container = nullptr; // Container for all spool elements
    lv_obj_t* spool_outer = nullptr;     // Outer ring (flange - darker shade)
    lv_obj_t* color_swatch = nullptr;    // Main filament color ring (flat) or spool_canvas (3D)
    lv_obj_t* spool_hub = nullptr;       // Center hub (dark) - only for flat style

    // 3D spool canvas widget (when use_3d_style is true)
    lv_obj_t* spool_canvas = nullptr;

    // Other UI elements
    lv_obj_t* material_label = nullptr;
    lv_obj_t* status_badge_bg = nullptr; // Status badge background (colored circle)
    lv_obj_t* status_icon = nullptr;     // Icon inside status badge
    lv_obj_t* slot_badge = nullptr;      // Slot number badge
    lv_obj_t* container = nullptr;       // The ams_slot widget itself

    // Material text buffer (for subject-driven updates if we add material subjects later)
    char material_buf[16] = {0};

    // Fill level for Spoolman integration (0.0 = empty, 1.0 = full)
    float fill_level = 1.0f;
};

// Note: Icons are accessed via ui_icon::lookup_codepoint() from ui_icon_codepoints.h

// Static registry mapping lv_obj_t* -> AmsSlotData*
// Used for safe cleanup during lv_deinit() when user_data may be unreliable
static std::unordered_map<lv_obj_t*, AmsSlotData*> s_slot_registry;

/**
 * @brief Get AmsSlotData for an object from the registry
 */
static AmsSlotData* get_slot_data(lv_obj_t* obj) {
    auto it = s_slot_registry.find(obj);
    return (it != s_slot_registry.end()) ? it->second : nullptr;
}

/**
 * @brief Register slot data in the registry
 */
static void register_slot_data(lv_obj_t* obj, AmsSlotData* data) {
    s_slot_registry[obj] = data;
}

/**
 * @brief Unregister and cleanup slot data
 */
static void unregister_slot_data(lv_obj_t* obj) {
    auto it = s_slot_registry.find(obj);
    if (it != s_slot_registry.end()) {
        AmsSlotData* data = it->second;
        if (data) {
            // Release observers before delete to prevent destructors
            // from calling lv_observer_remove() on destroyed subjects
            data->color_observer.release();
            data->status_observer.release();
            data->current_gate_observer.release();
            data->filament_loaded_observer.release();
            delete data;
        }
        s_slot_registry.erase(it);
    }
}

// ============================================================================
// Color Helpers (for skeuomorphic shading)
// ============================================================================

/**
 * @brief Darken a color by reducing RGB values
 * Uses direct struct member access since lv_color_t has .red, .green, .blue
 */
static lv_color_t darken_color(lv_color_t color, uint8_t amount) {
    uint8_t r = (color.red > amount) ? (color.red - amount) : 0;
    uint8_t g = (color.green > amount) ? (color.green - amount) : 0;
    uint8_t b = (color.blue > amount) ? (color.blue - amount) : 0;
    return lv_color_make(r, g, b);
}

// ============================================================================
// Fill Level Helpers
// ============================================================================

/**
 * @brief Update the filament visualization based on fill level
 *
 * Simulates remaining filament on spool:
 * - 3D style: Updates spool_canvas fill_level
 * - Flat style: Resizes concentric ring
 */
static void update_filament_ring_size(AmsSlotData* data) {
    if (!data) {
        return;
    }

    // Clamp fill level to valid range
    float fill = data->fill_level;
    if (fill < 0.0f)
        fill = 0.0f;
    if (fill > 1.0f)
        fill = 1.0f;

    if (data->use_3d_style && data->spool_canvas) {
        // 3D style: Use spool_canvas fill level
        ui_spool_canvas_set_fill_level(data->spool_canvas, fill);
        spdlog::debug("[AmsSlot] Slot {} 3D fill={:.0f}%", data->slot_index, fill * 100.0f);
    } else if (data->color_swatch && data->spool_container && data->spool_hub) {
        // Flat style: Resize the concentric ring
        lv_obj_update_layout(data->spool_container);

        int32_t spool_size = lv_obj_get_width(data->spool_container);
        int32_t hub_size = lv_obj_get_width(data->spool_hub);

        int32_t min_ring = hub_size + 4;   // Minimum: slightly larger than hub
        int32_t max_ring = spool_size - 8; // Maximum: smaller than outer flange

        int32_t ring_size = min_ring + static_cast<int32_t>((max_ring - min_ring) * fill);

        lv_obj_set_size(data->color_swatch, ring_size, ring_size);
        lv_obj_align(data->color_swatch, LV_ALIGN_CENTER, 0, 0);

        spdlog::debug("[AmsSlot] Slot {} flat fill={:.0f}% → ring_size={}px", data->slot_index,
                      fill * 100.0f, ring_size);
    }
}

// ============================================================================
// Observer Callbacks
// ============================================================================

/**
 * @brief Observer callback for gate color changes
 *
 * Updates the spool visualization color based on current style:
 * - 3D style: Updates spool_canvas widget color
 * - Flat style: Updates the concentric ring colors
 */
static void on_color_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* data = static_cast<AmsSlotData*>(lv_observer_get_user_data(observer));
    if (!data) {
        return;
    }

    int color_int = lv_subject_get_int(subject);
    lv_color_t filament_color = lv_color_hex(static_cast<uint32_t>(color_int));

    if (data->use_3d_style && data->spool_canvas) {
        // 3D style: Update spool_canvas color
        ui_spool_canvas_set_color(data->spool_canvas, filament_color);
    } else if (data->color_swatch) {
        // Flat style: Update concentric rings
        lv_obj_set_style_bg_color(data->color_swatch, filament_color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(data->color_swatch, LV_OPA_COVER, LV_PART_MAIN);

        // Outer ring (flange) - darker shade for depth effect
        if (data->spool_outer) {
            lv_color_t darker = darken_color(filament_color, 50);
            lv_obj_set_style_bg_color(data->spool_outer, darker, LV_PART_MAIN);
        }
    }

    spdlog::trace("[AmsSlot] Slot {} color updated to 0x{:06X}", data->slot_index,
                  static_cast<uint32_t>(color_int));
}

/**
 * @brief Observer callback for gate status changes
 *
 * Updates the status badge overlay with appropriate color and icon.
 * Badge is positioned on the spool for a cleaner look.
 */
static void on_status_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* data = static_cast<AmsSlotData*>(lv_observer_get_user_data(observer));
    if (!data || !data->status_icon) {
        return;
    }

    int status_int = lv_subject_get_int(subject);
    auto status = static_cast<GateStatus>(status_int);

    // Select icon and badge background color based on status
    // Use ui_icon::lookup_codepoint() for icon font glyphs
    const char* icon = ui_icon::lookup_codepoint("help_circle");
    lv_color_t badge_bg = ui_theme_get_color("text_secondary");

    switch (status) {
    case GateStatus::AVAILABLE:
        icon = ui_icon::lookup_codepoint("check");
        badge_bg = ui_theme_get_color("success_color");
        break;
    case GateStatus::LOADED:
        icon = ui_icon::lookup_codepoint("arrow_up");
        badge_bg = ui_theme_get_color("info_color");
        break;
    case GateStatus::FROM_BUFFER:
        icon = ui_icon::lookup_codepoint("check");
        badge_bg = ui_theme_get_color("warning_color");
        break;
    case GateStatus::EMPTY:
        icon = ui_icon::lookup_codepoint("close");
        badge_bg = ui_theme_get_color("ams_badge_bg");
        break;
    case GateStatus::BLOCKED:
        icon = ui_icon::lookup_codepoint("alert");
        badge_bg = ui_theme_get_color("error_color");
        break;
    case GateStatus::UNKNOWN:
    default:
        icon = ui_icon::lookup_codepoint("help_circle");
        badge_bg = ui_theme_get_color("ams_badge_bg");
        break;
    }

    // Update status badge
    lv_label_set_text(data->status_icon, icon);
    lv_obj_set_style_text_color(data->status_icon, lv_color_white(), LV_PART_MAIN);

    // Update badge background color
    if (data->status_badge_bg) {
        lv_obj_set_style_bg_color(data->status_badge_bg, badge_bg, LV_PART_MAIN);
    }

    // Handle empty slot visual treatment - fade the spool
    if (data->color_swatch) {
        lv_opa_t swatch_opa = (status == GateStatus::EMPTY) ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_bg_opa(data->color_swatch, swatch_opa, LV_PART_MAIN);
    }
    if (data->spool_outer) {
        lv_opa_t outer_opa = (status == GateStatus::EMPTY) ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_bg_opa(data->spool_outer, outer_opa, LV_PART_MAIN);
    }

    spdlog::trace("[AmsSlot] Slot {} status updated to {}", data->slot_index,
                  gate_status_to_string(status));
}

/**
 * @brief Observer callback for current gate changes (highlight active slot)
 *
 * Active slots get a glowing border effect using shadows for visual emphasis.
 */
static void on_current_gate_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* data = static_cast<AmsSlotData*>(lv_observer_get_user_data(observer));
    if (!data || !data->container) {
        return;
    }

    int current_gate = lv_subject_get_int(subject);

    // Also check filament_loaded to only highlight when actually loaded
    lv_subject_t* loaded_subject = AmsState::instance().get_filament_loaded_subject();
    bool filament_loaded = loaded_subject ? (lv_subject_get_int(loaded_subject) != 0) : false;

    bool is_active = (current_gate == data->slot_index) && filament_loaded;

    if (is_active) {
        // Active slot: glowing border effect
        lv_color_t primary = ui_theme_parse_color(lv_xml_get_const(NULL, "primary_color"));

        // Border highlight
        lv_obj_set_style_border_color(data->container, primary, LV_PART_MAIN);
        lv_obj_set_style_border_opa(data->container, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(data->container, 3, LV_PART_MAIN);

        // Outer glow using shadow
        lv_obj_set_style_shadow_width(data->container, 16, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(data->container, primary, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(data->container, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_shadow_spread(data->container, 2, LV_PART_MAIN);
    } else {
        // Inactive: no border or glow
        lv_obj_set_style_border_opa(data->container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(data->container, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(data->container, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(data->container, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    spdlog::trace("[AmsSlot] Slot {} active={} (current_gate={}, loaded={})", data->slot_index,
                  is_active, current_gate, filament_loaded);
}

/**
 * @brief Observer callback for filament loaded changes (affects highlight)
 */
static void on_filament_loaded_changed(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject); // We re-evaluate using current_gate, not the loaded value directly

    auto* data = static_cast<AmsSlotData*>(lv_observer_get_user_data(observer));
    if (!data || !data->container) {
        return;
    }

    // Re-evaluate highlight - delegate to current_gate logic
    lv_subject_t* gate_subject = AmsState::instance().get_current_gate_subject();
    if (gate_subject) {
        // Trigger the same logic as current_gate observer
        on_current_gate_changed(data->current_gate_observer.get(), gate_subject);
    }
}

// ============================================================================
// Widget Event Handler (for cleanup)
// ============================================================================

/**
 * @brief Event handler for widget lifecycle (DELETE event for cleanup)
 */
static void ams_slot_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DELETE) {
        lv_obj_t* obj = lv_event_get_target_obj(e);
        if (!obj) {
            return;
        }

        // Use the registry for cleanup - more reliable than user_data during lv_deinit()
        unregister_slot_data(obj);
    }
}

// ============================================================================
// Widget Creation (Internal)
// ============================================================================

/**
 * @brief Create all child widgets inside the ams_slot container
 *
 * Creates a skeuomorphic filament spool visualization with:
 * - Circular spool shape with outer flange, filament ring, and center hub
 * - Material label below the spool
 * - Status badge overlaid on the spool
 * - Slot number badge in corner
 */
static void create_slot_children(lv_obj_t* container, AmsSlotData* data) {
    // Get responsive spacing values
    int32_t space_xs = ui_theme_get_spacing("space_xs");

    // Responsive sizing: use flex_grow so slots share space equally
    // This ensures 4 slots fit in one row and align with path canvas gates
    // The parent slot_grid uses flex_flow="row" - slots expand to fill their portion
    lv_obj_set_flex_grow(container, 1); // Share horizontal space equally
    lv_obj_set_height(container, LV_SIZE_CONTENT);

    // Container styling: transparent, no border, minimal padding
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 2, LV_PART_MAIN);
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Use flex layout: column, center items
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, space_xs, LV_PART_MAIN);

    // ========================================================================
    // MATERIAL LABEL (above spool - leaves room for filament paths below)
    // ========================================================================
    lv_obj_t* material = lv_label_create(container);
    lv_label_set_text(material, "--");
    const char* font_small_name = lv_xml_get_const(NULL, "font_small");
    const lv_font_t* font_small =
        font_small_name ? lv_xml_get_font(NULL, font_small_name) : &noto_sans_16;
    lv_obj_set_style_text_font(material, font_small, LV_PART_MAIN);
    lv_obj_set_style_text_color(material, ui_theme_get_color("text_primary"), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(material, 1, LV_PART_MAIN);
    data->material_label = material;

    // ========================================================================
    // SPOOL VISUALIZATION (style-dependent: 3D canvas or flat rings)
    // ========================================================================
    // Check config for visualization style
    data->use_3d_style = is_3d_spool_style();

    // Spool size adapts to available space - scales with screen size
    // Must fit within max_width=90px constraint: spool_size + 8 (container padding) < 90
    int32_t space_lg = ui_theme_get_spacing("space_lg");
    int32_t spool_size = (space_lg * 4); // Responsive: 64px at 16px, 80px at 20px

    if (data->use_3d_style) {
        // ====================================================================
        // 3D SPOOL CANVAS (Bambu-style pseudo-3D with gradients + AA)
        // ====================================================================
        // Container is larger than canvas to accommodate badge overflow
        int32_t container_size = spool_size + 8; // Extra room for badge

        // Create a container to hold both the canvas and overlay badges
        lv_obj_t* spool_container = lv_obj_create(container);
        lv_obj_set_size(spool_container, container_size, container_size);
        lv_obj_set_style_bg_opa(spool_container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(spool_container, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(spool_container, 0, LV_PART_MAIN);
        lv_obj_remove_flag(spool_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(spool_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        data->spool_container = spool_container;

        // Create the 3D spool canvas inside the container (centered)
        lv_obj_t* canvas = ui_spool_canvas_create(spool_container, spool_size);
        if (canvas) {
            lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
            // Prevent flex layout from resizing the canvas
            lv_obj_set_style_min_width(canvas, spool_size, LV_PART_MAIN);
            lv_obj_set_style_min_height(canvas, spool_size, LV_PART_MAIN);
            lv_obj_set_style_max_width(canvas, spool_size, LV_PART_MAIN);
            lv_obj_set_style_max_height(canvas, spool_size, LV_PART_MAIN);
            ui_spool_canvas_set_color(canvas, lv_color_hex(AMS_DEFAULT_GATE_COLOR));
            ui_spool_canvas_set_fill_level(canvas, data->fill_level);
            data->spool_canvas = canvas;

            spdlog::debug("[AmsSlot] Created 3D spool_canvas ({}x{})", spool_size, spool_size);
        }
    } else {
        // ====================================================================
        // FLAT STYLE (skeuomorphic concentric rings)
        // ====================================================================
        int32_t filament_ring_size = spool_size - 8; // 8px smaller (4px margin each side)
        int32_t hub_size = spool_size / 3;           // Center hole proportional to spool

        // Spool container (holds all spool layers, provides shadow)
        lv_obj_t* spool_container = lv_obj_create(container);
        lv_obj_set_size(spool_container, spool_size, spool_size);
        lv_obj_set_style_radius(spool_container, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(spool_container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(spool_container, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(spool_container, 0, LV_PART_MAIN);
        lv_obj_remove_flag(spool_container, LV_OBJ_FLAG_SCROLLABLE);
        // Shadow for 3D depth effect
        lv_obj_set_style_shadow_width(spool_container, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(spool_container, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_shadow_offset_y(spool_container, 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(spool_container, lv_color_black(), LV_PART_MAIN);
        data->spool_container = spool_container;

        // Layer 1: Outer ring (flange - darker shade of filament color)
        lv_obj_t* outer_ring = lv_obj_create(spool_container);
        lv_obj_set_size(outer_ring, spool_size, spool_size);
        lv_obj_align(outer_ring, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(outer_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_color_t default_darker = darken_color(lv_color_hex(AMS_DEFAULT_GATE_COLOR), 50);
        lv_obj_set_style_bg_color(outer_ring, default_darker, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(outer_ring, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(outer_ring, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(outer_ring, ui_theme_get_color("ams_hub_dark"), LV_PART_MAIN);
        lv_obj_set_style_border_opa(outer_ring, LV_OPA_50, LV_PART_MAIN);
        lv_obj_remove_flag(outer_ring, LV_OBJ_FLAG_SCROLLABLE);
        data->spool_outer = outer_ring;

        // Layer 2: Main filament color ring (the actual vibrant filament color)
        lv_obj_t* filament_ring = lv_obj_create(spool_container);
        lv_obj_set_size(filament_ring, filament_ring_size, filament_ring_size);
        lv_obj_align(filament_ring, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(filament_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(filament_ring, lv_color_hex(AMS_DEFAULT_GATE_COLOR),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(filament_ring, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(filament_ring, 0, LV_PART_MAIN);
        lv_obj_remove_flag(filament_ring, LV_OBJ_FLAG_SCROLLABLE);
        data->color_swatch = filament_ring;

        // Layer 3: Center hub (the dark hole where filament feeds from)
        lv_obj_t* hub = lv_obj_create(spool_container);
        lv_obj_set_size(hub, hub_size, hub_size);
        lv_obj_align(hub, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(hub, ui_theme_get_color("ams_hub_dark"), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(hub, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(hub, ui_theme_get_color("ams_hub_border"), LV_PART_MAIN);
        lv_obj_remove_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
        data->spool_hub = hub;

        spdlog::debug("[AmsSlot] Created flat spool rings ({}x{})", spool_size, spool_size);
    }

    // ========================================================================
    // STATUS BADGE (overlaid on bottom-right of spool)
    // ========================================================================
    lv_obj_t* status_badge = lv_obj_create(data->spool_container);
    lv_obj_set_size(status_badge, 20, 20);
    // Position badge at bottom-right of the canvas area (offset accounts for larger container)
    lv_obj_align(status_badge, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
    lv_obj_set_style_radius(status_badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_badge, ui_theme_get_color("ams_badge_bg"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(status_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_badge, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(status_badge, ui_theme_get_color("card_bg"), LV_PART_MAIN);
    lv_obj_remove_flag(status_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(status_badge, 0, LV_PART_MAIN);
    data->status_badge_bg = status_badge;

    // Status icon inside badge
    lv_obj_t* status_icon = lv_label_create(status_badge);
    lv_label_set_text(status_icon, ui_icon::lookup_codepoint("help_circle"));
    lv_obj_set_style_text_font(status_icon, &mdi_icons_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(status_icon);
    data->status_icon = status_icon;

    // ========================================================================
    // SLOT NUMBER BADGE (bottom-left of spool, mirrors status badge)
    // ========================================================================
    lv_obj_t* badge_container = lv_obj_create(data->spool_container);
    lv_obj_set_size(badge_container, 20, 20);
    lv_obj_align(badge_container, LV_ALIGN_BOTTOM_LEFT, -2, -2);
    lv_obj_set_style_radius(badge_container, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge_container, ui_theme_get_color("ams_badge_bg"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge_container, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge_container, ui_theme_get_color("card_bg"), LV_PART_MAIN);
    lv_obj_remove_flag(badge_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(badge_container, 0, LV_PART_MAIN);

    lv_obj_t* badge_label = lv_label_create(badge_container);
    lv_label_set_text(badge_label, "?");
    const char* font_xs_name = lv_xml_get_const(NULL, "font_xs");
    const lv_font_t* font_xs = font_xs_name ? lv_xml_get_font(NULL, font_xs_name) : &noto_sans_12;
    lv_obj_set_style_text_font(badge_label, font_xs, LV_PART_MAIN);
    lv_obj_set_style_text_color(badge_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(badge_label);
    data->slot_badge = badge_label;

    data->container = container;
}

/**
 * @brief Setup observers for a given slot index
 */
static void setup_slot_observers(AmsSlotData* data) {
    if (data->slot_index < 0 || data->slot_index >= AmsState::MAX_GATES) {
        spdlog::warn("[AmsSlot] Invalid slot index {}, skipping observers", data->slot_index);
        return;
    }

    AmsState& state = AmsState::instance();

    // Get per-gate subjects
    lv_subject_t* color_subject = state.get_gate_color_subject(data->slot_index);
    lv_subject_t* status_subject = state.get_gate_status_subject(data->slot_index);
    lv_subject_t* current_gate_subject = state.get_current_gate_subject();
    lv_subject_t* filament_loaded_subject = state.get_filament_loaded_subject();

    // Create observers with ObserverGuard (RAII cleanup)
    if (color_subject) {
        data->color_observer = ObserverGuard(color_subject, on_color_changed, data);
    }
    if (status_subject) {
        data->status_observer = ObserverGuard(status_subject, on_status_changed, data);
    }
    if (current_gate_subject) {
        data->current_gate_observer =
            ObserverGuard(current_gate_subject, on_current_gate_changed, data);
    }
    if (filament_loaded_subject) {
        data->filament_loaded_observer =
            ObserverGuard(filament_loaded_subject, on_filament_loaded_changed, data);
    }

    // Update slot badge with 1-based display number
    if (data->slot_badge) {
        char badge_text[8];
        snprintf(badge_text, sizeof(badge_text), "%d", data->slot_index + 1);
        lv_label_set_text(data->slot_badge, badge_text);
    }

    // Trigger initial updates from current subject values
    if (color_subject && data->color_observer) {
        on_color_changed(data->color_observer.get(), color_subject);
    }
    if (status_subject && data->status_observer) {
        on_status_changed(data->status_observer.get(), status_subject);
    }
    if (current_gate_subject && data->current_gate_observer) {
        on_current_gate_changed(data->current_gate_observer.get(), current_gate_subject);
    }

    // Update material label from backend if available
    AmsBackend* backend = state.get_backend();
    if (backend) {
        GateInfo gate = backend->get_gate_info(data->slot_index);
        if (!gate.material.empty()) {
            lv_label_set_text(data->material_label, gate.material.c_str());
        }
    }

    spdlog::trace("[AmsSlot] Created observers for slot {}", data->slot_index);
}

// ============================================================================
// XML Handlers
// ============================================================================

/**
 * @brief XML create handler for ams_slot
 */
static void* ams_slot_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create(static_cast<lv_obj_t*>(parent));

    if (!obj) {
        spdlog::error("[AmsSlot] Failed to create container object");
        return nullptr;
    }

    // Allocate and register user data
    auto* data = new AmsSlotData();
    data->slot_index = -1; // Will be set by xml_apply when slot_index attr is parsed
    register_slot_data(obj, data);

    // Register event handler for cleanup
    lv_obj_add_event_cb(obj, ams_slot_event_cb, LV_EVENT_DELETE, nullptr);

    // Create child widgets
    create_slot_children(obj, data);

    spdlog::debug("[AmsSlot] Created widget");

    return obj;
}

/**
 * @brief XML apply handler for ams_slot
 */
static void ams_slot_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    void* item = lv_xml_state_get_item(state);
    lv_obj_t* obj = static_cast<lv_obj_t*>(item);

    if (!obj) {
        spdlog::error("[AmsSlot] NULL object in xml_apply");
        return;
    }

    // Apply standard lv_obj properties first
    lv_xml_obj_apply(state, attrs);

    // Get user data
    auto* data = get_slot_data(obj);
    if (!data) {
        spdlog::error("[AmsSlot] No user data in xml_apply");
        return;
    }

    // Parse custom attributes
    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (strcmp(name, "slot_index") == 0) {
            int new_index = atoi(value);
            if (new_index != data->slot_index) {
                // Clear existing observers
                data->color_observer.reset();
                data->status_observer.reset();
                data->current_gate_observer.reset();
                data->filament_loaded_observer.reset();

                data->slot_index = new_index;

                // Setup new observers
                setup_slot_observers(data);

                spdlog::debug("[AmsSlot] Set slot_index={}", data->slot_index);
            }
        } else if (strcmp(name, "fill_level") == 0) {
            // Parse fill level (0.0 = empty, 1.0 = full)
            float fill = strtof(value, nullptr);
            if (fill < 0.0f)
                fill = 0.0f;
            if (fill > 1.0f)
                fill = 1.0f;
            data->fill_level = fill;
            update_filament_ring_size(data);
            spdlog::debug("[AmsSlot] Set fill_level={:.2f}", data->fill_level);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void ui_ams_slot_register(void) {
    lv_xml_register_widget("ams_slot", ams_slot_xml_create, ams_slot_xml_apply);
    spdlog::info("[AmsSlot] Registered ams_slot widget with XML system");
}

int ui_ams_slot_get_index(lv_obj_t* obj) {
    if (!obj) {
        return -1;
    }

    auto* data = get_slot_data(obj);
    if (!data) {
        return -1;
    }

    return data->slot_index;
}

void ui_ams_slot_set_index(lv_obj_t* obj, int slot_index) {
    if (!obj) {
        return;
    }

    auto* data = get_slot_data(obj);
    if (!data) {
        return;
    }

    if (slot_index == data->slot_index) {
        return; // No change
    }

    // Clear existing observers
    data->color_observer.reset();
    data->status_observer.reset();
    data->current_gate_observer.reset();
    data->filament_loaded_observer.reset();

    data->slot_index = slot_index;

    // Setup new observers
    setup_slot_observers(data);
}

void ui_ams_slot_refresh(lv_obj_t* obj) {
    if (!obj) {
        return;
    }

    auto* data = get_slot_data(obj);
    if (!data || data->slot_index < 0) {
        return;
    }

    AmsState& state = AmsState::instance();

    // Trigger observer callbacks with current values
    lv_subject_t* color_subject = state.get_gate_color_subject(data->slot_index);
    if (color_subject && data->color_observer) {
        on_color_changed(data->color_observer.get(), color_subject);
    }

    lv_subject_t* status_subject = state.get_gate_status_subject(data->slot_index);
    if (status_subject && data->status_observer) {
        on_status_changed(data->status_observer.get(), status_subject);
    }

    lv_subject_t* current_gate_subject = state.get_current_gate_subject();
    if (current_gate_subject && data->current_gate_observer) {
        on_current_gate_changed(data->current_gate_observer.get(), current_gate_subject);
    }

    // Update material from backend
    AmsBackend* backend = state.get_backend();
    if (backend && data->material_label) {
        GateInfo gate = backend->get_gate_info(data->slot_index);
        if (!gate.material.empty()) {
            lv_label_set_text(data->material_label, gate.material.c_str());
        } else {
            lv_label_set_text(data->material_label, "--");
        }
    }

    spdlog::debug("[AmsSlot] Refreshed slot {}", data->slot_index);
}

void ui_ams_slot_set_fill_level(lv_obj_t* obj, float fill_level) {
    if (!obj) {
        return;
    }

    auto* data = get_slot_data(obj);
    if (!data) {
        return;
    }

    // Clamp to valid range
    if (fill_level < 0.0f)
        fill_level = 0.0f;
    if (fill_level > 1.0f)
        fill_level = 1.0f;

    data->fill_level = fill_level;
    update_filament_ring_size(data);

    spdlog::debug("[AmsSlot] Slot {} fill_level set to {:.2f}", data->slot_index, fill_level);
}

float ui_ams_slot_get_fill_level(lv_obj_t* obj) {
    if (!obj) {
        return 1.0f; // Default to full
    }

    auto* data = get_slot_data(obj);
    if (!data) {
        return 1.0f;
    }

    return data->fill_level;
}
