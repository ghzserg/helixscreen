// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "led/ui_led_control_overlay.h"

#include "ui_color_picker.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_global_panel_helper.h"
#include "ui_led_chip_factory.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "led/led_controller.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>

using namespace helix::led;

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

DEFINE_GLOBAL_OVERLAY_STORAGE(LedControlOverlay, g_led_control_overlay, get_led_control_overlay)

void init_led_control_overlay(PrinterState& printer_state) {
    INIT_GLOBAL_OVERLAY(LedControlOverlay, g_led_control_overlay, printer_state);
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

LedControlOverlay::LedControlOverlay(PrinterState& printer_state) : printer_state_(printer_state) {
    spdlog::trace("[{}] Constructor", get_name());
}

LedControlOverlay::~LedControlOverlay() {
    if (!lv_is_initialized()) {
        spdlog::trace("[LedControlOverlay] Destroyed (LVGL already deinit)");
        return;
    }
    spdlog::trace("[LedControlOverlay] Destroyed");
}

// ============================================================================
// OVERLAYBASE IMPLEMENTATION
// ============================================================================

void LedControlOverlay::init_subjects() {
    init_subjects_guarded([this]() {
        UI_MANAGED_SUBJECT_INT(brightness_subject_, 100, "led_brightness", subjects_);
        UI_MANAGED_SUBJECT_STRING(brightness_text_subject_, brightness_text_buf_, "100%",
                                  "led_brightness_text", subjects_);
        UI_MANAGED_SUBJECT_STRING(strip_name_subject_, strip_name_buf_, "LED",
                                  "led_active_strip_name", subjects_);
        UI_MANAGED_SUBJECT_INT(wled_brightness_subject_, 100, "led_wled_brightness", subjects_);
        UI_MANAGED_SUBJECT_STRING(wled_brightness_text_subject_, wled_brightness_text_buf_, "100%",
                                  "led_wled_brightness_text", subjects_);

        // WLED toggle state (0=off, 1=on)
        UI_MANAGED_SUBJECT_INT(wled_is_on_, 0, "led_wled_is_on", subjects_);

        // Section visibility subjects (0=hidden, 1=visible)
        UI_MANAGED_SUBJECT_INT(native_visible_, 0, "led_native_visible", subjects_);
        UI_MANAGED_SUBJECT_INT(effects_visible_, 0, "led_effects_visible", subjects_);
        UI_MANAGED_SUBJECT_INT(wled_visible_, 0, "led_wled_visible", subjects_);
        UI_MANAGED_SUBJECT_INT(macro_visible_, 0, "led_macro_visible", subjects_);
        UI_MANAGED_SUBJECT_INT(strip_selector_visible_, 0, "led_strip_selector_visible", subjects_);
    });
}

lv_obj_t* LedControlOverlay::create(lv_obj_t* parent) {
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "led_control_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Find widget containers needed for dynamic population (lv_obj_clean + repopulate)
    // Section visibility is handled declaratively via bind_flag_if_eq subjects
    strip_selector_section_ = lv_obj_find_by_name(overlay_root_, "strip_selector_section");
    current_color_swatch_ = lv_obj_find_by_name(overlay_root_, "current_color_swatch");
    color_presets_container_ = lv_obj_find_by_name(overlay_root_, "color_presets_container");
    effects_container_ = lv_obj_find_by_name(overlay_root_, "effects_container");
    wled_presets_container_ = lv_obj_find_by_name(overlay_root_, "wled_presets_container");
    macro_buttons_container_ = lv_obj_find_by_name(overlay_root_, "macro_buttons_container");

    // Populate based on available backends
    populate_sections();

    spdlog::trace("[{}] Created overlay", get_name());
    return overlay_root_;
}

void LedControlOverlay::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "led_custom_color_cb", on_custom_color_cb);
    lv_xml_register_event_cb(nullptr, "led_brightness_changed_cb", on_brightness_changed_cb);
    lv_xml_register_event_cb(nullptr, "led_native_turn_off_cb", on_native_turn_off_cb);
    lv_xml_register_event_cb(nullptr, "led_wled_toggle_cb", on_wled_toggle_cb);
    lv_xml_register_event_cb(nullptr, "led_color_preset_cb", on_color_preset_cb);
    spdlog::trace("[{}] Callbacks registered", get_name());
}

void LedControlOverlay::on_activate() {
    OverlayBase::on_activate();

    auto& controller = LedController::instance();
    if (controller.is_initialized()) {
        // Read current color from the selected strip's cached state
        std::string active_strip = controller.first_available_strip();

        // Determine the backend type of the active strip
        if (!active_strip.empty()) {
            selected_backend_type_ = LedBackendType::NATIVE;

            // Check if it's a macro strip
            if (active_strip.rfind("macro:", 0) == 0) {
                selected_backend_type_ = LedBackendType::MACRO;
            } else {
                for (const auto& s : controller.wled().strips()) {
                    if (s.id == active_strip) {
                        selected_backend_type_ = LedBackendType::WLED;
                        break;
                    }
                }
            }
        }

        if (selected_backend_type_ != LedBackendType::WLED &&
            selected_backend_type_ != LedBackendType::MACRO && !active_strip.empty() &&
            controller.native().has_strip_color(active_strip)) {
            auto color = controller.native().get_strip_color(active_strip);
            color.decompose(current_color_, current_brightness_);
        } else if (selected_backend_type_ != LedBackendType::WLED &&
                   selected_backend_type_ != LedBackendType::MACRO) {
            current_brightness_ = controller.last_brightness();
            current_color_ = controller.last_color();
        }

        // Update section visibility based on strip type
        update_section_visibility();

        // Poll WLED status on overlay activation for live state
        if (selected_backend_type_ == LedBackendType::WLED) {
            // Sync WLED brightness slider to active strip's brightness
            std::string wled_strip_id = active_strip;
            if (!wled_strip_id.empty()) {
                auto strip_state = controller.wled().get_strip_state(wled_strip_id);
                int pct = (strip_state.brightness * 100) / 255;
                lv_subject_set_int(&wled_brightness_subject_, pct);
                update_wled_brightness_text(pct);
            }
            update_wled_toggle_button();
            refresh_wled_status();
        }
    }

    // Update visual state — brightness slider syncs via bind_value="led_brightness"
    update_brightness_text(current_brightness_);
    update_current_color_swatch();

    // Sync slider position via subject (bind_value handles the visual update)
    lv_subject_set_int(&brightness_subject_, current_brightness_);

    // Subscribe to WLED brightness slider changes
    wled_brightness_observer_ = helix::ui::observe_int_sync<LedControlOverlay>(
        &wled_brightness_subject_, this, [](LedControlOverlay* self, int value) {
            if (self->is_visible()) {
                self->handle_wled_brightness(value);
            }
        });

    // Sync effect highlight to current Moonraker state
    if (effects_container_ && controller.is_initialized()) {
        const auto& all_effects = controller.effects().effects();
        std::string active_effect;
        for (const auto& eff : all_effects) {
            if (eff.enabled) {
                active_effect = eff.name;
                break;
            }
        }
        highlight_active_effect(active_effect);
    }

    // Register for live color updates from Moonraker subscription
    controller.native().set_color_change_callback(
        [this](const std::string& strip_id, const NativeBackend::StripColor& color) {
            if (!is_visible())
                return;

            // Only update for the currently active strip
            auto& ctrl = LedController::instance();
            const auto& selected = ctrl.selected_strips();
            std::string active_strip;
            if (!selected.empty())
                active_strip = selected[0];
            else if (!ctrl.native().strips().empty())
                active_strip = ctrl.native().strips()[0].id;

            if (strip_id != active_strip)
                return;

            // Queue UI update to main thread — this callback runs on background thread
            uint8_t r = static_cast<uint8_t>(color.r * 255.0);
            uint8_t g = static_cast<uint8_t>(color.g * 255.0);
            uint8_t b = static_cast<uint8_t>(color.b * 255.0);
            lv_obj_t* swatch = current_color_swatch_;
            ui_queue_update([swatch, r, g, b]() {
                if (swatch)
                    lv_obj_set_style_bg_color(swatch, lv_color_make(r, g, b), 0);
            });
        });

    spdlog::debug("[{}] Activated (brightness={}, color=0x{:06X})", get_name(), current_brightness_,
                  current_color_);
}

void LedControlOverlay::on_deactivate() {
    OverlayBase::on_deactivate();

    // Stop live color updates + persist state
    auto& controller = LedController::instance();
    if (controller.is_initialized()) {
        controller.native().clear_color_change_callback();
    }

    wled_brightness_observer_.reset();

    // Persist state
    if (controller.is_initialized()) {
        controller.set_last_brightness(current_brightness_);
        controller.set_last_color(current_color_);
        controller.save_config();
    }

    spdlog::debug("[{}] Deactivated", get_name());
}

void LedControlOverlay::cleanup() {
    spdlog::debug("[{}] Cleanup", get_name());
    wled_brightness_observer_.reset();
    deinit_subjects_base(subjects_);
    OverlayBase::cleanup();
}

// ============================================================================
// SECTION POPULATION
// ============================================================================

void LedControlOverlay::populate_sections() {
    auto& controller = LedController::instance();
    if (!controller.is_initialized()) {
        spdlog::warn("[{}] LedController not initialized - hiding all sections", get_name());
        update_section_visibility();
        return;
    }

    populate_strip_selector();
    populate_color_presets();
    populate_effects();
    populate_wled();
    populate_macros();
    update_section_visibility();
}

void LedControlOverlay::update_section_visibility() {
    // Section visibility driven by subjects — XML bind_flag_if_eq handles the UI
    auto& controller = LedController::instance();
    bool ctrl_init = controller.is_initialized();

    bool has_native = ctrl_init && controller.native().is_available();
    bool has_effects = ctrl_init && controller.effects().is_available();
    bool has_wled = ctrl_init && controller.wled().is_available();

    bool native_vis = false;
    bool effects_vis = false;
    bool wled_vis = false;
    bool macro_vis = false;

    switch (selected_backend_type_) {
    case LedBackendType::WLED:
        wled_vis = has_wled;
        break;
    case LedBackendType::MACRO:
        macro_vis = true;
        break;
    case LedBackendType::NATIVE:
    case LedBackendType::LED_EFFECT:
    default:
        native_vis = has_native;
        effects_vis = has_effects;
        break;
    }

    lv_subject_set_int(&native_visible_, native_vis ? 1 : 0);
    lv_subject_set_int(&effects_visible_, effects_vis ? 1 : 0);
    lv_subject_set_int(&wled_visible_, wled_vis ? 1 : 0);
    lv_subject_set_int(&macro_visible_, macro_vis ? 1 : 0);

    // Strip selector visible when there are 2+ strips total
    size_t total_strips = 0;
    if (ctrl_init) {
        total_strips = controller.native().strips().size() + controller.wled().strips().size() +
                       controller.macro().macros().size();
    }
    lv_subject_set_int(&strip_selector_visible_, total_strips > 1 ? 1 : 0);

    spdlog::debug(
        "[{}] Section visibility: native={}, effects={}, wled={}, macros={}, backend_type={}",
        get_name(), native_vis, effects_vis, wled_vis, macro_vis,
        static_cast<int>(selected_backend_type_));
}

void LedControlOverlay::populate_strip_selector() {
    if (!strip_selector_section_)
        return;

    auto& controller = LedController::instance();

    // Build combined strip list (native + WLED + macro)
    std::vector<LedStripInfo> all_strips;
    for (const auto& s : controller.native().strips())
        all_strips.push_back(s);
    for (const auto& s : controller.wled().strips())
        all_strips.push_back(s);
    for (const auto& m : controller.macro().macros()) {
        LedStripInfo macro_strip;
        macro_strip.name = m.display_name;
        macro_strip.id = "macro:" + m.display_name;
        macro_strip.backend = LedBackendType::MACRO;
        macro_strip.supports_color = false;
        macro_strip.supports_white = false;
        all_strips.push_back(macro_strip);
    }

    if (all_strips.empty())
        return;

    const auto& selected = controller.selected_strips();

    // Determine active strip name for the header
    std::string active_name = all_strips[0].name;
    if (!selected.empty()) {
        for (const auto& s : all_strips) {
            if (s.id == selected[0]) {
                active_name = s.name;
                break;
            }
        }
    }
    snprintf(strip_name_buf_, sizeof(strip_name_buf_), "%s", active_name.c_str());
    lv_subject_copy_string(&strip_name_subject_, strip_name_buf_);

    // Only show selector chips if multiple strips total
    if (all_strips.size() <= 1)
        return;

    for (const auto& strip : all_strips) {
        bool is_selected =
            selected.empty()
                ? (&strip == &all_strips[0])
                : (std::find(selected.begin(), selected.end(), strip.id) != selected.end());

        // Add suffix for non-native strips to visually distinguish them
        std::string display_name = strip.name;
        if (strip.backend == LedBackendType::WLED)
            display_name += " (WLED)";
        else if (strip.backend == LedBackendType::MACRO)
            display_name += " (Macro)";

        helix::ui::create_led_chip(
            strip_selector_section_, strip.id, display_name, is_selected,
            [this](const std::string& strip_id) { handle_strip_selected(strip_id); });
    }

    spdlog::trace("[{}] Populated strip selector with {} strips ({} native + {} WLED + {} macro)",
                  get_name(), all_strips.size(), controller.native().strips().size(),
                  controller.wled().strips().size(), controller.macro().macros().size());
}

void LedControlOverlay::populate_color_presets() {
    if (!color_presets_container_)
        return;

    // Swatches are defined in XML with event_cb; just set user_data with color values
    // Click handling via led_color_preset_cb registered in register_callbacks()
    static const struct {
        const char* name;
        uint32_t color;
    } swatches[] = {
        {"swatch_white", 0xFFFFFF},  {"swatch_warm", 0xFFD700}, {"swatch_orange", 0xFF6B35},
        {"swatch_blue", 0x4FC3F7},   {"swatch_red", 0xFF4444},  {"swatch_green", 0x66BB6A},
        {"swatch_purple", 0x9C27B0}, {"swatch_cyan", 0x00BCD4},
    };

    int count = 0;
    for (const auto& s : swatches) {
        auto* swatch = lv_obj_find_by_name(overlay_root_, s.name);
        if (!swatch)
            continue;

        // Store color as static data — no heap allocation needed
        static uint32_t color_values[8];
        color_values[count] = s.color;
        lv_obj_set_user_data(swatch, &color_values[count]);
        count++;
    }

    spdlog::trace("[{}] Attached color data to {} presets", get_name(), count);
}

void LedControlOverlay::populate_effects() {
    if (!effects_container_)
        return;

    auto& controller = LedController::instance();

    // Filter effects by the currently selected strip
    const auto& selected = controller.selected_strips();
    std::vector<LedEffectInfo> effects;
    if (!selected.empty()) {
        effects = controller.effects().effects_for_strip(selected[0]);
    } else if (!controller.native().strips().empty()) {
        effects = controller.effects().effects_for_strip(controller.native().strips()[0].id);
    } else {
        effects = controller.effects().effects();
    }

    for (const auto& effect : effects) {
        const char* attrs[] = {"label", effect.display_name.c_str(), nullptr};
        auto* chip =
            static_cast<lv_obj_t*>(lv_xml_create(effects_container_, "led_action_chip", attrs));
        if (!chip)
            continue;

        auto* name_data = new std::string(effect.name);
        lv_obj_set_user_data(chip, name_data);

        lv_obj_add_event_cb(
            chip,
            [](lv_event_t* e) {
                LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] effect_cb");
                auto* data = static_cast<std::string*>(lv_event_get_user_data(e));
                if (data)
                    get_led_control_overlay().handle_effect_activate(*data);
                LVGL_SAFE_EVENT_CB_END();
            },
            LV_EVENT_CLICKED, name_data);

        lv_obj_add_event_cb(
            chip,
            [](lv_event_t* e) { delete static_cast<std::string*>(lv_event_get_user_data(e)); },
            LV_EVENT_DELETE, name_data);
    }

    // Highlight whichever effect is currently enabled (from Moonraker subscription)
    std::string active_effect;
    for (const auto& effect : effects) {
        if (effect.enabled) {
            active_effect = effect.name;
            break;
        }
    }
    if (!active_effect.empty()) {
        highlight_active_effect(active_effect);
    }

    spdlog::trace("[{}] Populated {} effects", get_name(), effects.size());
}

void LedControlOverlay::populate_wled() {
    if (!wled_presets_container_)
        return;

    auto& controller = LedController::instance();
    if (!controller.wled().is_available())
        return;

    // Determine active WLED strip
    const auto& selected = controller.selected_strips();
    std::string active_strip_id;
    if (!selected.empty() && selected_backend_type_ == LedBackendType::WLED) {
        active_strip_id = selected[0];
    } else if (!controller.wled().strips().empty()) {
        active_strip_id = controller.wled().strips()[0].id;
    }

    if (active_strip_id.empty())
        return;

    // Get current state for highlighting
    auto state = controller.wled().get_strip_state(active_strip_id);

    // Get presets for this strip (real names from device or mock data)
    const auto& presets = controller.wled().get_strip_presets(active_strip_id);

    // Determine which presets to show
    struct PresetEntry {
        int id;
        std::string name;
    };
    std::vector<PresetEntry> entries;

    if (presets.empty()) {
        // Fallback to numbered presets
        for (int i = 1; i <= 5; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "Preset %d", i);
            entries.push_back({i, buf});
        }
    } else {
        for (const auto& p : presets) {
            entries.push_back({p.id, p.name});
        }
    }

    auto accent = theme_manager_get_color("primary");
    auto on_accent = theme_manager_get_color("screen_bg");

    for (const auto& entry : entries) {
        const char* attrs[] = {"label", entry.name.c_str(), nullptr};
        auto* chip = static_cast<lv_obj_t*>(
            lv_xml_create(wled_presets_container_, "led_action_chip", attrs));
        if (!chip)
            continue;

        auto* id_data = new int(entry.id);
        lv_obj_set_user_data(chip, id_data);

        // Highlight active preset
        if (entry.id == state.active_preset) {
            lv_obj_set_style_bg_color(chip, accent, LV_PART_MAIN);
            auto* label = lv_obj_get_child(chip, 0);
            if (label)
                lv_obj_set_style_text_color(label, on_accent, LV_PART_MAIN);
        }

        lv_obj_add_event_cb(
            chip,
            [](lv_event_t* e) {
                LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] wled_preset_cb");
                auto* data = static_cast<int*>(lv_event_get_user_data(e));
                if (data)
                    get_led_control_overlay().handle_wled_preset(*data);
                LVGL_SAFE_EVENT_CB_END();
            },
            LV_EVENT_CLICKED, id_data);

        lv_obj_add_event_cb(
            chip, [](lv_event_t* e) { delete static_cast<int*>(lv_event_get_user_data(e)); },
            LV_EVENT_DELETE, id_data);
    }

    spdlog::trace("[{}] Populated {} WLED presets for '{}'", get_name(), entries.size(),
                  active_strip_id);
}

void LedControlOverlay::populate_macros() {
    if (!macro_buttons_container_)
        return;

    auto& controller = LedController::instance();

    // Find the currently selected macro device
    const auto& selected = controller.selected_strips();
    std::string macro_key;
    if (!selected.empty() && selected[0].rfind("macro:", 0) == 0) {
        macro_key = selected[0].substr(6);
    }

    // If a specific macro is selected, show controls for that device
    if (!macro_key.empty()) {
        for (const auto& m : controller.macro().macros()) {
            if (m.display_name == macro_key) {
                populate_macro_controls(m);
                return;
            }
        }
    }

    // Default: show all macro controls (initial state before selection)
    for (const auto& macro : controller.macro().macros()) {
        populate_macro_controls(macro);
    }

    spdlog::trace("[{}] Populated macros section", get_name());
}

void LedControlOverlay::populate_macro_controls(const LedMacroInfo& macro) {
    switch (macro.type) {
    case MacroLedType::ON_OFF:
        add_macro_chip("Turn On", macro.display_name, &LedControlOverlay::handle_macro_on);
        add_macro_chip("Turn Off", macro.display_name, &LedControlOverlay::handle_macro_off);
        break;

    case MacroLedType::TOGGLE:
        add_macro_chip("Toggle", macro.display_name, &LedControlOverlay::handle_macro_toggle);
        break;

    case MacroLedType::PRESET:
        for (const auto& [preset_name, preset_gcode] : macro.presets) {
            add_macro_chip(preset_name, preset_gcode, &LedControlOverlay::handle_macro_custom);
        }
        break;
    }
}

// ============================================================================
// ACTION HANDLERS
// ============================================================================

void LedControlOverlay::handle_color_preset(uint32_t color) {
    current_color_ = color;

    // Presets are defined at full brightness — reset brightness to 100%
    current_brightness_ = 100;
    update_brightness_text(current_brightness_);
    lv_subject_set_int(&brightness_subject_, current_brightness_);

    apply_current_color();
    spdlog::info("[{}] Color preset applied: 0x{:06X}", get_name(), color);
}

void LedControlOverlay::handle_brightness_change(int brightness) {
    if (brightness == current_brightness_)
        return;

    current_brightness_ = brightness;
    update_brightness_text(brightness);

    // Re-apply current color at new brightness
    apply_current_color();

    spdlog::debug("[{}] Brightness changed to {}%", get_name(), brightness);
}

void LedControlOverlay::handle_custom_color() {
    spdlog::info("[{}] Opening custom color picker", get_name());

    // Use the ColorPicker modal
    static helix::ui::ColorPicker color_picker;
    color_picker.set_color_callback([this](uint32_t rgb, const std::string& name) {
        spdlog::info("[{}] Custom color selected: 0x{:06X} ({})", get_name(), rgb, name);

        // Decompose picked color into HSV to extract brightness (V)
        // and store the full-brightness base color
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;
        uint8_t max_c = std::max({r, g, b});
        uint8_t min_c = std::min({r, g, b});

        // V = max component (0-255), map to brightness 0-100
        int brightness = (max_c * 100 + 127) / 255;
        if (brightness < 1)
            brightness = 1; // Avoid zero brightness from very dark picks

        // Reconstruct full-brightness color (scale RGB so max component = 255)
        uint32_t full_color = rgb;
        if (max_c > 0 && max_c < 255) {
            uint8_t fr = static_cast<uint8_t>(std::min(255, r * 255 / max_c));
            uint8_t fg = static_cast<uint8_t>(std::min(255, g * 255 / max_c));
            uint8_t fb = static_cast<uint8_t>(std::min(255, b * 255 / max_c));
            full_color = (fr << 16) | (fg << 8) | fb;
        }

        // Apply the full-brightness base color first, then sync brightness
        spdlog::debug("[{}] Custom color decomposed: base=0x{:06X} brightness={}%", get_name(),
                      full_color, brightness);

        // Set brightness BEFORE handle_color_preset so it uses the new value
        current_brightness_ = brightness;
        update_brightness_text(brightness);
        handle_color_preset(full_color);

        // Sync slider via subject (bind_value handles the visual update)
        lv_subject_set_int(&brightness_subject_, brightness);
    });

    if (overlay_root_) {
        color_picker.show_with_color(lv_obj_get_parent(overlay_root_), current_color_);
    }
}

void LedControlOverlay::handle_effect_activate(const std::string& effect_name) {
    spdlog::info("[{}] Activating effect: {}", get_name(), effect_name);
    auto& controller = LedController::instance();
    controller.effects().activate_effect(
        effect_name, []() { spdlog::debug("[LedControlOverlay] Effect activated successfully"); },
        [](const std::string& err) {
            spdlog::error("[LedControlOverlay] Effect activation failed: {}", err);
        });

    // Highlight active chip, unhighlight others
    highlight_active_effect(effect_name);
}

void LedControlOverlay::handle_native_turn_off() {
    spdlog::info("[{}] Turn off: stopping effects + turning off LED", get_name());
    auto& controller = LedController::instance();

    // Stop led_effects if any are available
    if (controller.effects().is_available()) {
        controller.effects().stop_all_effects(
            []() { spdlog::debug("[LedControlOverlay] All effects stopped"); },
            [](const std::string& err) {
                spdlog::error("[LedControlOverlay] Stop effects failed: {}", err);
            });
        highlight_active_effect("");
    }

    // Turn off all selected native strips (set color to black)
    const auto& selected = controller.selected_strips();
    if (selected.empty()) {
        const auto& strips = controller.native().strips();
        if (!strips.empty()) {
            controller.native().turn_off(strips[0].id);
        }
    } else {
        for (const auto& strip_id : selected) {
            controller.native().turn_off(strip_id);
        }
    }
}

void LedControlOverlay::handle_wled_toggle() {
    auto& controller = LedController::instance();
    const auto& selected = controller.selected_strips();
    if (!selected.empty() && selected_backend_type_ == LedBackendType::WLED) {
        spdlog::info("[{}] WLED toggle: {}", get_name(), selected[0]);
        controller.wled().toggle(
            selected[0],
            [this]() {
                update_wled_toggle_button();
                refresh_wled_status();
            },
            nullptr);
    }
}

void LedControlOverlay::update_wled_toggle_button() {
    // Button text, colors, and styling driven declaratively via led_wled_is_on subject
    auto& controller = LedController::instance();
    const auto& selected = controller.selected_strips();
    std::string strip_id;
    if (!selected.empty() && selected_backend_type_ == LedBackendType::WLED) {
        strip_id = selected[0];
    } else if (!controller.wled().strips().empty()) {
        strip_id = controller.wled().strips()[0].id;
    }

    if (strip_id.empty())
        return;

    auto state = controller.wled().get_strip_state(strip_id);
    lv_subject_set_int(&wled_is_on_, state.is_on ? 1 : 0);
}

void LedControlOverlay::highlight_active_effect(const std::string& active_name) {
    if (!effects_container_)
        return;

    auto accent = theme_manager_get_color("primary");
    auto card_bg = theme_manager_get_color("card_bg");
    auto text_color = theme_manager_get_color("text");
    auto on_accent = theme_manager_get_color("screen_bg");

    uint32_t count = lv_obj_get_child_count(effects_container_);
    for (uint32_t i = 0; i < count; i++) {
        auto* child = lv_obj_get_child(effects_container_, i);
        auto* data = static_cast<std::string*>(lv_obj_get_user_data(child));
        if (!data)
            continue; // skip stop button (has no user data)

        bool is_active = (*data == active_name);
        lv_obj_set_style_bg_color(child, is_active ? accent : card_bg, LV_PART_MAIN);
        auto* label = lv_obj_get_child(child, 0);
        if (label)
            lv_obj_set_style_text_color(label, is_active ? on_accent : text_color, LV_PART_MAIN);
    }
}

void LedControlOverlay::handle_wled_preset(int preset_id) {
    spdlog::info("[{}] Activating WLED preset {}", get_name(), preset_id);
    auto& controller = LedController::instance();
    const auto& selected = controller.selected_strips();
    if (!selected.empty() && selected_backend_type_ == LedBackendType::WLED) {
        controller.wled().set_preset(
            selected[0], preset_id, []() { get_led_control_overlay().refresh_wled_status(); },
            nullptr);
    }
}

void LedControlOverlay::handle_wled_brightness(int brightness) {
    spdlog::debug("[{}] WLED brightness: {}%", get_name(), brightness);
    update_wled_brightness_text(brightness);

    auto& controller = LedController::instance();
    const auto& selected = controller.selected_strips();
    if (!selected.empty() && selected_backend_type_ == LedBackendType::WLED) {
        controller.wled().set_brightness(selected[0], brightness);
    }
}

void LedControlOverlay::handle_macro_on(const std::string& macro_name) {
    spdlog::info("[{}] Executing macro ON: {}", get_name(), macro_name);
    auto& controller = LedController::instance();
    controller.macro().execute_on(macro_name);
}

void LedControlOverlay::handle_macro_off(const std::string& macro_name) {
    spdlog::info("[{}] Executing macro OFF: {}", get_name(), macro_name);
    auto& controller = LedController::instance();
    controller.macro().execute_off(macro_name);
}

void LedControlOverlay::handle_macro_toggle(const std::string& macro_name) {
    spdlog::info("[{}] Executing macro TOGGLE: {}", get_name(), macro_name);
    auto& controller = LedController::instance();
    controller.macro().execute_toggle(macro_name);
}

void LedControlOverlay::handle_macro_custom(const std::string& gcode) {
    spdlog::info("[{}] Executing custom macro: {}", get_name(), gcode);
    auto& controller = LedController::instance();
    controller.macro().execute_custom_action(gcode);
}

void LedControlOverlay::handle_strip_selected(const std::string& strip_id) {
    spdlog::info("[{}] Strip selected: {}", get_name(), strip_id);

    auto& controller = LedController::instance();

    // Toggle selection: if already the only selected strip, do nothing
    auto selected = controller.selected_strips();
    auto it = std::find(selected.begin(), selected.end(), strip_id);

    if (it != selected.end()) {
        // Already selected - if it's the only one, keep it; otherwise deselect
        if (selected.size() > 1) {
            selected.erase(it);
        }
    } else {
        // Not selected - replace selection with this strip (single-select behavior)
        selected.clear();
        selected.push_back(strip_id);
    }

    controller.set_selected_strips(selected);

    // Determine the backend type and display name for the selected strip
    selected_backend_type_ = LedBackendType::NATIVE;
    std::string display_name = strip_id;

    if (strip_id.rfind("macro:", 0) == 0) {
        // Macro strip: extract display name from the ID
        selected_backend_type_ = LedBackendType::MACRO;
        std::string macro_key = strip_id.substr(6); // Strip "macro:" prefix
        display_name = macro_key;
        for (const auto& m : controller.macro().macros()) {
            if (m.display_name == macro_key) {
                display_name = m.display_name;
                break;
            }
        }
    } else {
        for (const auto& s : controller.wled().strips()) {
            if (s.id == strip_id) {
                selected_backend_type_ = LedBackendType::WLED;
                display_name = s.name;
                break;
            }
        }
        if (selected_backend_type_ != LedBackendType::WLED) {
            for (const auto& s : controller.native().strips()) {
                if (s.id == strip_id) {
                    display_name = s.name;
                    break;
                }
            }
        }
    }

    // Update strip name display
    snprintf(strip_name_buf_, sizeof(strip_name_buf_), "%s", display_name.c_str());
    lv_subject_copy_string(&strip_name_subject_, strip_name_buf_);

    if (selected_backend_type_ == LedBackendType::WLED) {
        // WLED strip selected: rebuild WLED section, update visibility
        if (wled_presets_container_) {
            lv_obj_clean(wled_presets_container_);
            populate_wled();
        }

        // Sync WLED brightness slider to the selected strip's brightness
        auto& ctrl_ref = LedController::instance();
        auto strip_state = ctrl_ref.wled().get_strip_state(strip_id);
        int pct = (strip_state.brightness * 100) / 255;
        lv_subject_set_int(&wled_brightness_subject_, pct);
        update_wled_brightness_text(pct);
        update_wled_toggle_button();
    } else if (selected_backend_type_ == LedBackendType::MACRO) {
        // Macro strip selected: rebuild macro controls for this specific macro
        if (macro_buttons_container_) {
            lv_obj_clean(macro_buttons_container_);
            std::string macro_key = strip_id.substr(6);
            for (const auto& m : controller.macro().macros()) {
                if (m.display_name == macro_key) {
                    populate_macro_controls(m);
                    break;
                }
            }
        }
    } else {
        // Native strip selected: update color/brightness from cache
        auto strip_color = controller.native().get_strip_color(strip_id);
        strip_color.decompose(current_color_, current_brightness_);
        update_brightness_text(current_brightness_);
        update_current_color_swatch();
        lv_subject_set_int(&brightness_subject_, current_brightness_);

        // Rebuild effects for the newly selected strip
        if (effects_container_) {
            lv_obj_clean(effects_container_);
            populate_effects();
        }
    }

    // Rebuild strip selector to update visual states
    if (strip_selector_section_) {
        lv_obj_clean(strip_selector_section_);
        populate_strip_selector();
    }

    update_section_visibility();
}

// ============================================================================
// HELPERS
// ============================================================================

void LedControlOverlay::apply_current_color() {
    // Stop any running LED effects before applying a manual color
    auto& controller = LedController::instance();
    if (controller.effects().is_available()) {
        controller.effects().stop_all_effects();
        highlight_active_effect("");
    }

    double r = static_cast<double>((current_color_ >> 16) & 0xFF) / 255.0;
    double g = static_cast<double>((current_color_ >> 8) & 0xFF) / 255.0;
    double b = static_cast<double>(current_color_ & 0xFF) / 255.0;

    double bf = static_cast<double>(current_brightness_) / 100.0;
    send_color_to_strips(r * bf, g * bf, b * bf, 0.0);
    update_current_color_swatch();
}

void LedControlOverlay::send_color_to_strips(double r, double g, double b, double w) {
    auto& controller = LedController::instance();
    if (!controller.native().is_available())
        return;

    const auto& selected = controller.selected_strips();
    if (selected.empty()) {
        // Default to first strip if none selected
        const auto& strips = controller.native().strips();
        if (!strips.empty()) {
            controller.native().set_color(strips[0].id, r, g, b, w);
        }
    } else {
        for (const auto& strip_id : selected) {
            controller.native().set_color(strip_id, r, g, b, w);
        }
    }
}

void LedControlOverlay::update_brightness_text(int brightness) {
    snprintf(brightness_text_buf_, sizeof(brightness_text_buf_), "%d%%", brightness);
    lv_subject_copy_string(&brightness_text_subject_, brightness_text_buf_);
}

void LedControlOverlay::update_current_color_swatch() {
    if (!current_color_swatch_)
        return;

    // Show the actual output color (base color × brightness)
    double bf = static_cast<double>(current_brightness_) / 100.0;
    uint8_t r = static_cast<uint8_t>(((current_color_ >> 16) & 0xFF) * bf);
    uint8_t g = static_cast<uint8_t>(((current_color_ >> 8) & 0xFF) * bf);
    uint8_t b = static_cast<uint8_t>((current_color_ & 0xFF) * bf);
    lv_obj_set_style_bg_color(current_color_swatch_, lv_color_make(r, g, b), 0);
}

void LedControlOverlay::update_wled_brightness_text(int brightness) {
    snprintf(wled_brightness_text_buf_, sizeof(wled_brightness_text_buf_), "%d%%", brightness);
    lv_subject_copy_string(&wled_brightness_text_subject_, wled_brightness_text_buf_);
}

void LedControlOverlay::add_macro_chip(const std::string& label, const std::string& data,
                                       MacroClickHandler handler) {
    const char* attrs[] = {"label", label.c_str(), nullptr};
    auto* chip =
        static_cast<lv_obj_t*>(lv_xml_create(macro_buttons_container_, "led_action_chip", attrs));
    if (!chip)
        return;

    // Pack handler + data together for the callback
    struct ChipCallbackData {
        std::string value;
        MacroClickHandler handler;
    };
    auto* cb_data = new ChipCallbackData{data, handler};
    lv_obj_set_user_data(chip, cb_data);

    lv_obj_add_event_cb(
        chip,
        [](lv_event_t* e) {
            LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] macro_cb");
            auto* d = static_cast<ChipCallbackData*>(lv_event_get_user_data(e));
            if (d)
                (get_led_control_overlay().*(d->handler))(d->value);
            LVGL_SAFE_EVENT_CB_END();
        },
        LV_EVENT_CLICKED, cb_data);

    lv_obj_add_event_cb(
        chip,
        [](lv_event_t* e) { delete static_cast<ChipCallbackData*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, cb_data);
}

void LedControlOverlay::refresh_wled_status() {
    auto& controller = LedController::instance();
    if (!controller.is_initialized() || selected_backend_type_ != LedBackendType::WLED)
        return;

    controller.wled().poll_status([this]() {
        if (wled_presets_container_) {
            lv_obj_clean(wled_presets_container_);
            populate_wled();
        }
        update_wled_toggle_button();
    });
}

// ============================================================================
// STATIC CALLBACKS
// ============================================================================

void LedControlOverlay::on_custom_color_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] custom_color_cb");
    (void)e;
    get_led_control_overlay().handle_custom_color();
    LVGL_SAFE_EVENT_CB_END();
}

void LedControlOverlay::on_native_turn_off_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] native_turn_off_cb");
    (void)e;
    get_led_control_overlay().handle_native_turn_off();
    LVGL_SAFE_EVENT_CB_END();
}

void LedControlOverlay::on_wled_toggle_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] wled_toggle_cb");
    (void)e;
    get_led_control_overlay().handle_wled_toggle();
    LVGL_SAFE_EVENT_CB_END();
}

void LedControlOverlay::on_color_preset_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] color_preset_cb");
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* data = static_cast<uint32_t*>(lv_obj_get_user_data(target));
    if (data)
        get_led_control_overlay().handle_color_preset(*data);
    LVGL_SAFE_EVENT_CB_END();
}

void LedControlOverlay::on_brightness_changed_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[LedControlOverlay] brightness_changed_cb");
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int value = lv_slider_get_value(slider);
    auto& overlay = get_led_control_overlay();
    overlay.handle_brightness_change(value);
    LVGL_SAFE_EVENT_CB_END();
}
