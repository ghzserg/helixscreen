// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file theme_manager.h
 * @brief Responsive design token system with breakpoints, spacing, and theme colors
 *
 * @pattern Singleton with breakpoint suffixes (_small/_medium/_large) and light/dark variants
 * @threading Main thread only
 * @gotchas theme_manager_get_color() looks up tokens; theme_manager_parse_color() parses hex
 * literals only
 */

#pragma once

#include "ui_fonts.h"

#include "lvgl/lvgl.h"

// Include theme_loader for ModePalette and ThemeData definitions
#include "theme_loader.h"

// ============================================================================
// Table-Driven Style System Types (ThemeManager refactor Phase 1)
// ============================================================================

/// Style roles - each represents a semantic style in the theme system.
/// Used to index into the style table for O(1) lookups.
enum class StyleRole {
    Card,
    Dialog,
    ObjBase,
    InputBg,
    Disabled,
    Pressed,
    Focused,
    TextPrimary,
    TextMuted,
    TextSubtle,
    IconText,
    IconPrimary,
    IconSecondary,
    IconTertiary,
    IconInfo,
    IconSuccess,
    IconWarning,
    IconDanger,
    Button,
    ButtonPrimary,
    ButtonSecondary,
    ButtonTertiary,
    ButtonDanger,
    ButtonGhost,
    ButtonSuccess,
    ButtonWarning,
    ButtonDisabled,
    ButtonPressed,
    SeverityInfo,
    SeveritySuccess,
    SeverityWarning,
    SeverityDanger,
    Dropdown,
    Checkbox,
    Switch,
    Slider,
    Spinner,
    Arc,
    COUNT
};

/// Theme palette - holds all semantic colors for a theme mode.
/// Used by style configure functions to read colors without string lookups.
struct ThemePalette {
    lv_color_t screen_bg{};
    lv_color_t overlay_bg{};
    lv_color_t card_bg{};
    lv_color_t elevated_bg{};
    lv_color_t border{};
    lv_color_t text{};
    lv_color_t text_muted{};
    lv_color_t text_subtle{};
    lv_color_t primary{};
    lv_color_t secondary{};
    lv_color_t tertiary{};
    lv_color_t info{};
    lv_color_t success{};
    lv_color_t warning{};
    lv_color_t danger{};
    lv_color_t focus{};
    int border_radius = 8;
    int border_width = 1;
    int border_opacity = 40;
};

/// Style configure function type - applies palette colors to a style.
using StyleConfigureFn = void (*)(lv_style_t* style, const ThemePalette& palette);

/// Style entry - binds a role to its style and configure function.
struct StyleEntry {
    StyleRole role;
    lv_style_t style{};
    StyleConfigureFn configure = nullptr;
};

#include <array>
#include <string_view>

/// Unified theme manager - singleton managing all styles and colors.
/// Replaces theme_core.c + old theme_manager.cpp with table-driven approach.
class ThemeManager {
  public:
    /// Get singleton instance
    static ThemeManager& instance();

    /// Initialize the theme system. Must be called once at startup.
    void init();

    /// Shutdown and cleanup
    void shutdown();

    /// Get style for a role. Returns pointer to internal style (never null after init).
    lv_style_t* get_style(StyleRole role);

    /// Get current palette
    const ThemePalette& current_palette() const {
        return current_palette_;
    }

    /// Check if dark mode is currently active
    bool is_dark_mode() const {
        return dark_mode_;
    }

    /// Set dark mode on/off and update all styles
    void set_dark_mode(bool dark);

    /// Toggle between dark and light mode
    void toggle_dark_mode() {
        set_dark_mode(!dark_mode_);
    }

    /// Set both light and dark palettes (for theme loading)
    void set_palettes(const ThemePalette& light, const ThemePalette& dark);

    /// Get color from current palette by name
    /// Supports: "primary", "danger", "card_bg", "text", etc.
    lv_color_t get_color(std::string_view name) const;

    /// Preview a palette without permanently applying it
    void preview_palette(const ThemePalette& palette);

    /// Cancel preview and revert to current theme palette
    void cancel_preview();

    /// Check if currently previewing
    bool is_previewing() const {
        return previewing_;
    }

    // Delete copy/move
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

  private:
    ThemeManager() = default;

    std::array<StyleEntry, static_cast<size_t>(StyleRole::COUNT)> styles_{};
    ThemePalette current_palette_{};
    ThemePalette light_palette_{};
    ThemePalette dark_palette_{};
    bool initialized_ = false;
    bool dark_mode_ = true;
    bool previewing_ = false;

    void register_style_configs();
    void apply_palette(const ThemePalette& palette);
};

// ============================================================================
// End Table-Driven Style System Types
// ============================================================================

// Theme colors: Use theme_manager_get_color() to retrieve from globals.xml
// Available tokens: primary_color, text_primary, text_secondary, success_color, etc.

// Layout constants
#define UI_NAV_WIDTH_PERCENT 10 // Nav bar is 1/10th of screen width
#define UI_NAV_ICON_SIZE 64     // Base icon size for 1024x800
#define UI_NAV_PADDING 16       // Padding between elements

// Calculate nav width based on actual screen
#define UI_NAV_WIDTH(screen_w) ((screen_w) / 10)

// Responsive breakpoints (based on max(width, height))
// Optimized for our target hardware: 480x320, 800x480, 1024x600, 1280x720
#define UI_BREAKPOINT_SMALL_MAX 480 // 480x320 and below → SMALL
#define UI_BREAKPOINT_MEDIUM_MAX                                                                   \
    800 // 481-800: 800x480, up to 800x600 → MEDIUM
        // >800: 1024x600, 1280x720+ → LARGE

// Screen size targets (reference only, use breakpoints above for logic)
#define UI_SCREEN_LARGE_W 1280
#define UI_SCREEN_LARGE_H 720
#define UI_SCREEN_MEDIUM_W 1024
#define UI_SCREEN_MEDIUM_H 600
#define UI_SCREEN_SMALL_W 800
#define UI_SCREEN_SMALL_H 480
#define UI_SCREEN_TINY_W 480
#define UI_SCREEN_TINY_H 320

// Spacing tokens available (use theme_manager_get_spacing() to read values):
//   space_xxs: 2/3/4px  (small/medium/large breakpoints)
//   space_xs:  4/5/6px
//   space_sm:  6/7/8px
//   space_md:  8/10/12px
//   space_lg:  12/16/20px
//   space_xl:  16/20/24px

// Opacity constants (matching globals.xml values)
#define UI_DISABLED_OPA 128 // disabled_opa - 50% opacity for disabled/dimmed elements

// Responsive navigation bar width (applied in C++ based on screen size)
#define UI_NAV_WIDTH_TINY 64   // Tiny screens: 42px button + margins
#define UI_NAV_WIDTH_SMALL 76  // Small screens: 60px button + 8px padding each side
#define UI_NAV_WIDTH_MEDIUM 94 // Medium screens: 70px button + 12px padding each side
#define UI_NAV_WIDTH_LARGE 102 // Large screens: 70px button + 16px padding each side

// Semantic fonts: Use theme_manager_get_font() to retrieve responsive fonts from globals.xml
// Available tokens: font_heading, font_body, font_small

/**
 * @brief Initialize LVGL theme system
 *
 * Creates and applies LVGL theme with light or dark mode.
 * Must be called before creating any widgets.
 *
 * @param display LVGL display instance
 * @param use_dark_mode true for dark theme, false for light theme
 */
void theme_manager_init(lv_display_t* display, bool use_dark_mode);

/**
 * @brief Get breakpoint suffix for a given resolution
 *
 * Returns the suffix string used to select responsive variants from globals.xml.
 * Useful for testing and debugging responsive behavior.
 *
 * @param max_resolution Maximum of horizontal and vertical resolution
 * @return "_small" (≤480), "_medium" (481-800), or "_large" (>800)
 */
const char* theme_manager_get_breakpoint_suffix(int32_t max_resolution);

/**
 * @brief Register responsive spacing tokens (space_* system)
 *
 * Registers the unified spacing scale (space_xxs through space_xl) based on
 * current display resolution. This is the preferred system - use space_*
 * tokens instead of the deprecated padding_* and gap_* tokens.
 *
 * Called automatically by theme_manager_init().
 *
 * @param display LVGL display instance
 */
void theme_manager_register_responsive_spacing(lv_display_t* display);

/**
 * @brief Register responsive font constants
 *
 * Selects font sizes based on screen size breakpoints.
 * Called automatically by theme_manager_init().
 *
 * @param display LVGL display instance
 */
void theme_manager_register_responsive_fonts(lv_display_t* display);

/**
 * @brief Toggle between light and dark themes
 *
 * Switches theme mode, re-registers XML color constants, updates theme
 * styles in-place, and forces a widget tree refresh. All existing widgets
 * will update to the new color scheme without recreation.
 */
void theme_manager_toggle_dark_mode();

/**
 * @brief Force style refresh on widget tree
 *
 * Walks the widget tree starting from root and forces style recalculation
 * on each widget. This is called automatically by theme_manager_toggle_dark_mode()
 * but can be used independently for custom refresh scenarios.
 *
 * @param root Root widget to start refresh from (typically lv_screen_active())
 */
void theme_manager_refresh_widget_tree(lv_obj_t* root);

/**
 * @brief Check if dark mode is currently active
 *
 * @return true if dark mode enabled, false if light mode
 */
bool theme_manager_is_dark_mode();

/**
 * @brief Get currently active theme data
 * @return Reference to active theme (valid after theme_manager_init)
 */
const helix::ThemeData& theme_manager_get_active_theme();

/**
 * @brief Get mode support for currently loaded theme
 * @return ThemeModeSupport enum value (DUAL_MODE, DARK_ONLY, or LIGHT_ONLY)
 */
helix::ThemeModeSupport theme_manager_get_mode_support();

/**
 * @brief Check if current theme supports dark mode
 * @return true if dark palette is available
 */
bool theme_manager_supports_dark_mode();

/**
 * @brief Check if current theme supports light mode
 * @return true if light palette is available
 */
bool theme_manager_supports_light_mode();

/**
 * @brief Preview theme colors without restart
 *
 * Applies theme colors for live preview. Call theme_manager_revert_preview()
 * to restore original colors, or restart to apply permanently.
 *
 * @param theme Theme data to preview
 */
void theme_manager_preview(const helix::ThemeData& theme);

/**
 * @brief Preview theme colors with explicit dark mode
 *
 * Like theme_manager_preview() but allows UI to control dark/light mode
 * independently from the global setting (for preview toggle).
 *
 * @param theme Theme data to preview
 * @param is_dark Whether to preview in dark mode
 */
void theme_manager_preview(const helix::ThemeData& theme, bool is_dark);

/**
 * @brief Revert to active theme (cancel preview)
 */
void theme_manager_revert_preview();

/**
 * @brief Refresh example preview elements after theme color change
 *
 * Updates named preview widgets (buttons, aurora dots, etc.) to reflect
 * current theme colors. This is needed because XML inline styles like
 * `style_bg_color="#primary"` are resolved at parse time and don't
 * automatically update when theme colors change.
 *
 * Called automatically by theme_manager_preview() to update any visible
 * preview overlays on the active screen.
 *
 * Named widgets updated:
 * - example_btn_primary, example_btn_success, example_btn_warning, example_btn_danger
 * - aurora_0, aurora_1 (danger), aurora_2 (warning), aurora_3 (success), aurora_4 (info)
 * - preview_typography_card, preview_actions_card, preview_background (card_bg, screen_bg)
 *
 * @param root Widget tree root to search (typically lv_screen_active())
 * @param theme Theme data with colors to apply
 */
void theme_manager_refresh_preview_elements(lv_obj_t* root, const helix::ThemeData& theme);

/**
 * @brief Parse hex color string to lv_color_t
 *
 * Supports both "#RRGGBB" and "RRGGBB" formats.
 *
 * @param hex_str Hex color string (e.g., "#FF0000" or "FF0000")
 * @return LVGL color object
 */
lv_color_t theme_manager_parse_hex_color(const char* hex_str);

/**
 * @brief Compute perceived brightness of a color
 *
 * Uses standard luminance formula: 0.299*R + 0.587*G + 0.114*B
 *
 * @param color LVGL color to analyze
 * @return Brightness value 0-255
 */
int theme_compute_brightness(lv_color_t color);

/**
 * @brief Return the brighter of two colors
 *
 * Compares perceived brightness using theme_compute_brightness().
 * Used for computing switch/slider knob colors.
 *
 * @param a First color
 * @param b Second color
 * @return Whichever color has higher perceived brightness
 */
lv_color_t theme_compute_brighter_color(lv_color_t a, lv_color_t b);

/**
 * @brief Compute saturation of a color (0-255)
 *
 * Uses HSV saturation formula. Returns 0 for grayscale, higher for vivid colors.
 */
int theme_compute_saturation(lv_color_t c);

/**
 * @brief Return the more saturated of two colors
 *
 * Useful for accent colors where you want the more vivid/colorful option.
 */
lv_color_t theme_compute_more_saturated(lv_color_t a, lv_color_t b);

/**
 * @brief Get the computed knob color for switches/sliders
 *
 * Returns the more saturated of primary vs tertiary colors from the current theme.
 * Used for switch and slider handle styling to ensure consistent appearance.
 *
 * @return Knob color (more saturated of primary/tertiary)
 */
lv_color_t theme_get_knob_color();

/**
 * @brief Get the computed accent color for icons
 *
 * Returns the more saturated of primary vs secondary colors from the current theme.
 * Used for icon accent variant styling to ensure consistent appearance.
 *
 * @return Accent color (more saturated of primary/secondary)
 */
lv_color_t theme_get_accent_color();

/**
 * @brief Apply palette colors to a single widget based on its type
 *
 * Styles the widget appropriately for its class type using colors from the
 * provided ModePalette. This enables preview panels to show colors from a
 * different palette than the app's current active theme.
 *
 * Widget types handled:
 * - Labels: text color
 * - Buttons: background color (accent), text contrast
 * - Switches: track (border), indicator (secondary), knob (brighter of primary/tertiary)
 * - Sliders: track (border), indicator (secondary), knob (brighter of primary/tertiary)
 * - Dropdowns: background (elevated_bg), border, text
 * - Textareas: background (elevated_bg), text
 *
 * @param obj Widget to style
 * @param palette Colors to apply
 * @param text_light Light text color (for contrast on dark backgrounds)
 * @param text_dark Dark text color (for contrast on light backgrounds)
 */
void theme_apply_palette_to_widget(lv_obj_t* obj, const helix::ModePalette& palette,
                                   lv_color_t text_light, lv_color_t text_dark);

/**
 * @brief Apply palette colors to all widgets in a tree
 *
 * Recursively walks the widget tree and applies palette colors to each
 * widget based on its type. Handles backgrounds, text, and input widgets.
 *
 * @param root Root widget of the tree to style
 * @param palette Colors to apply
 * @param text_light Light text color (for button contrast)
 * @param text_dark Dark text color (for button contrast)
 */
void theme_apply_palette_to_tree(lv_obj_t* root, const helix::ModePalette& palette,
                                 lv_color_t text_light, lv_color_t text_dark);

/**
 * @brief Apply palette to any open dropdown lists on the screen
 *
 * Dropdown lists are created as screen-level popups, not children of their
 * dropdown widget. This function finds and styles any open dropdown lists.
 *
 * @param palette Color palette to apply
 */
void theme_apply_palette_to_screen_dropdowns(const helix::ModePalette& palette);

/**
 * @brief Apply current theme palette to a widget tree
 *
 * Convenience wrapper that applies the currently active theme palette
 * to a widget tree. Use this when showing new UI elements (like modals)
 * that need context-aware styling.
 *
 * @param root Root object to apply palette to (recurses into children)
 */
void theme_apply_current_palette_to_tree(lv_obj_t* root);

/**
 * @brief Get themed color by base name
 *
 * Retrieves color from globals.xml with automatic _light/_dark
 * variant selection based on current theme mode.
 *
 * Example: base_name="card_bg" → "card_bg_light" or "card_bg_dark"
 *
 * @param base_name Base color name (without _light/_dark suffix)
 * @return Themed color for current mode
 */
lv_color_t theme_manager_get_color(const char* base_name);

/**
 * @brief Apply themed background color to widget
 *
 * Sets widget background color using theme-aware color lookup.
 * Automatically selects _light or _dark variant.
 *
 * @param obj Widget to style
 * @param base_name Base color name (without _light/_dark suffix)
 * @param part Widget part to style (default: LV_PART_MAIN)
 */
void theme_manager_apply_bg_color(lv_obj_t* obj, const char* base_name,
                                  lv_part_t part = LV_PART_MAIN);

/**
 * @brief Get font height in pixels
 *
 * Returns the line height of the font, useful for layout calculations.
 *
 * @param font LVGL font pointer
 * @return Font height in pixels
 */
int32_t theme_manager_get_font_height(const lv_font_t* font);

/**
 * @brief Set overlay widget width to fill space after nav bar
 *
 * Utility for overlay panels/widgets that use x="#nav_width" positioning.
 * Sets width to (screen_width - nav_width).
 *
 * @param obj Widget to resize (typically an overlay panel or detail view)
 * @param screen Parent screen to calculate width from
 */
void ui_set_overlay_width(lv_obj_t* obj, lv_obj_t* screen);

/**
 * @brief Get spacing value from unified space_* system
 *
 * Reads the registered space_* constant value from LVGL's XML constant registry.
 * The value returned is responsive - it depends on what breakpoint was used
 * during theme initialization (small/medium/large).
 *
 * This function is the C++ interface to the unified spacing system. All spacing
 * in C++ code should use this function to stay consistent with XML layouts.
 *
 * Available tokens:
 *   space_xxs: 2/3/4px  (small/medium/large)
 *   space_xs:  4/5/6px
 *   space_sm:  6/7/8px
 *   space_md:  8/10/12px
 *   space_lg:  12/16/20px
 *   space_xl:  16/20/24px
 *
 * @param token Spacing token name (e.g., "space_lg", "space_md", "space_xs")
 * @return Spacing value in pixels, or 0 if token not found
 */
int32_t theme_manager_get_spacing(const char* token);

/**
 * @brief Get responsive font by token name
 *
 * Retrieves font pointer from globals.xml based on current display breakpoint.
 * The font returned is responsive - it depends on the breakpoint used during
 * theme initialization (small/medium/large).
 *
 * Available tokens:
 *   font_heading: 20/26/28px (small/medium/large breakpoints)
 *   font_body:    14/18/20px
 *   font_small:   12/16/18px
 *
 * @param token Font token name (e.g., "font_small", "font_body", "font_heading")
 * @return Font pointer, or nullptr if token not found
 */
const lv_font_t* theme_manager_get_font(const char* token);

/**
 * @brief Convert semantic size name to font token
 *
 * Maps semantic size names (xs, sm, md, lg) to font tokens (font_xs, font_small, etc.)
 * This provides a consistent size vocabulary across widgets.
 *
 * Mapping:
 *   "xs" → "font_xs"
 *   "sm" → "font_small"
 *   "md" → "font_body"
 *   "lg" → "font_heading"
 *
 * @param size Size name (xs, sm, md, lg), or NULL for default
 * @param default_size Default size to use if size is NULL (defaults to "sm")
 * @return Font token string (e.g., "font_small"). Never returns NULL.
 */
const char* theme_manager_size_to_font_token(const char* size, const char* default_size = "sm");

// ============================================================================
// Multi-File Responsive Constants API
// ============================================================================
// These functions extend the responsive constant system to work with ALL XML
// files, not just globals.xml. Component-local constants are aggregated with
// last-wins precedence (alphabetical order, so component files can override
// globals.xml values).

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Parse an XML file and extract constants with a specific suffix
 *
 * Extracts name→value pairs for elements of the given type that end with
 * the specified suffix. The base name (suffix stripped) is used as the key.
 *
 * Example: For suffix="_small", element <px name="button_height_small" value="32"/>
 * produces entry {"button_height", "32"} in the results map.
 *
 * @param filepath Path to the XML file to parse
 * @param element_type Element type to match ("px", "color", "string")
 * @param suffix Suffix to match ("_small", "_medium", "_large", "_light", "_dark")
 * @param token_values Output map: base_name → value (last-wins if duplicate)
 */
void theme_manager_parse_xml_file_for_suffix(
    const char* filepath, const char* element_type, const char* suffix,
    std::unordered_map<std::string, std::string>& token_values);

/**
 * @brief Find all XML files in a directory
 *
 * Returns a sorted list of *.xml file paths in the given directory.
 * Files are sorted alphabetically to ensure deterministic processing order
 * (important for last-wins precedence).
 *
 * Does NOT recurse into subdirectories.
 *
 * @param directory Directory path to search
 * @return Sorted vector of full file paths, empty if directory doesn't exist
 */
std::vector<std::string> theme_manager_find_xml_files(const char* directory);

/**
 * @brief Parse all XML files in a directory for constants with a specific suffix
 *
 * Aggregates constants from all XML files in the directory. Files are processed
 * in alphabetical order, so later files (by name) override earlier ones.
 * This allows component files to override globals.xml values.
 *
 * @param directory Directory containing XML files
 * @param element_type Element type to match ("px", "color", "string")
 * @param suffix Suffix to match ("_small", "_medium", "_large", "_light", "_dark")
 * @return Map of base_name → value for all matching constants
 */
std::unordered_map<std::string, std::string>
theme_manager_parse_all_xml_for_suffix(const char* directory, const char* element_type,
                                       const char* suffix);

/**
 * @brief Parse all XML files in a directory for ALL elements of a given type
 *
 * Aggregates constants from all XML files in the directory. Files are processed
 * in alphabetical order, so later files (by name) override earlier ones.
 * Unlike theme_manager_parse_all_xml_for_suffix(), this returns ALL elements regardless
 * of suffix, with the full name as the key.
 *
 * @param directory Directory containing XML files
 * @param element_type Element type to match ("px", "color", "string")
 * @return Map of name → value for all matching constants
 */
std::unordered_map<std::string, std::string>
theme_manager_parse_all_xml_for_element(const char* directory, const char* element_type);

/**
 * @brief Validate that responsive/themed constant sets are complete
 *
 * Checks for incomplete sets:
 * - Responsive px: If ANY of foo_small, foo_medium, foo_large exist but NOT ALL -> warn
 * - Themed colors: If ONLY bar_light OR ONLY bar_dark exists -> warn
 *
 * This function is useful for:
 * - Unit tests to catch incomplete constant sets
 * - Pre-commit hooks to validate XML files before committing
 *
 * @param directory Directory containing XML files to validate
 * @return Vector of warning messages (empty if all valid)
 */
std::vector<std::string> theme_manager_validate_constant_sets(const char* directory);
