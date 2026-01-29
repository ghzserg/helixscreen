// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_utils.h"

#include "format_utils.h"
#include "settings_manager.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

// ============================================================================
// Filename Utilities
// ============================================================================

std::string get_filename_basename(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    // Find last path separator
    size_t last_sep = path.find_last_of("/\\");
    if (last_sep == std::string::npos) {
        return path; // No separator, already just a filename
    }

    return path.substr(last_sep + 1);
}

std::string strip_gcode_extension(const std::string& filename) {
    // Common G-code extensions (case-insensitive check)
    static const std::vector<std::string> extensions = {".gcode", ".gco", ".g"};

    for (const auto& ext : extensions) {
        if (filename.size() > ext.size()) {
            size_t pos = filename.size() - ext.size();
            // Case-insensitive suffix comparison
            std::string suffix = filename.substr(pos);
            std::string suffix_lower;
            suffix_lower.reserve(suffix.size());
            for (char c : suffix) {
                suffix_lower.push_back(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (suffix_lower == ext) {
                return filename.substr(0, pos);
            }
        }
    }

    return filename;
}

std::string get_display_filename(const std::string& path) {
    return strip_gcode_extension(get_filename_basename(path));
}

std::string resolve_gcode_filename(const std::string& path) {
    // Pattern: .helix_temp/modified_123456789_OriginalName.gcode (Moonraker plugin)
    // Also handles: */gcode_mod/mod_XXXXXX_filename.gcode (local temp files)
    // Legacy: /tmp/helixscreen_mod_XXXXXX_filename.gcode
    static const std::string helix_temp_prefix = ".helix_temp/modified_";
    static const std::string gcode_mod_prefix = "/gcode_mod/mod_";
    static const std::string legacy_prefix = "/tmp/helixscreen_mod_";

    size_t underscore_pos = std::string::npos;

    if (path.find(helix_temp_prefix) != std::string::npos) {
        // Extract original: .helix_temp/modified_123456789_OriginalName.gcode -> OriginalName.gcode
        size_t prefix_end = path.find(helix_temp_prefix) + helix_temp_prefix.size();
        underscore_pos = path.find('_', prefix_end);
    } else if (path.find(gcode_mod_prefix) != std::string::npos) {
        // Extract original: */gcode_mod/mod_123456_OriginalName.gcode -> OriginalName.gcode
        size_t prefix_end = path.find(gcode_mod_prefix) + gcode_mod_prefix.size();
        underscore_pos = path.find('_', prefix_end);
    } else if (path.find(legacy_prefix) != std::string::npos) {
        // Legacy: /tmp/helixscreen_mod_123456_OriginalName.gcode -> OriginalName.gcode
        size_t prefix_end = path.find(legacy_prefix) + legacy_prefix.size();
        underscore_pos = path.find('_', prefix_end);
    }

    if (underscore_pos != std::string::npos && underscore_pos + 1 < path.size()) {
        std::string original = path.substr(underscore_pos + 1);
        spdlog::debug("[resolve_gcode_filename] '{}' -> '{}'", path, original);
        return original;
    }

    return path;
}

// ============================================================================
// Time Formatting
// ============================================================================

std::string format_print_time(int minutes) {
    return helix::fmt::duration_from_minutes(minutes);
}

std::string format_filament_weight(float grams) {
    char buf[32];
    if (grams < 1.0f) {
        snprintf(buf, sizeof(buf), "%.1f g", grams);
    } else if (grams < 10.0f) {
        snprintf(buf, sizeof(buf), "%.1f g", grams);
    } else {
        snprintf(buf, sizeof(buf), "%.0f g", grams);
    }
    return std::string(buf);
}

std::string format_layer_count(uint32_t layer_count) {
    if (layer_count == 0) {
        return helix::fmt::UNAVAILABLE;
    }
    char buf[32];
    if (layer_count == 1) {
        snprintf(buf, sizeof(buf), "1 layer");
    } else {
        snprintf(buf, sizeof(buf), "%u layers", layer_count);
    }
    return std::string(buf);
}

std::string format_print_height(double height_mm) {
    if (height_mm <= 0.0) {
        return helix::fmt::UNAVAILABLE;
    }
    char buf[32];
    if (height_mm < 1.0) {
        snprintf(buf, sizeof(buf), "%.2f mm", height_mm);
    } else if (height_mm < 10.0) {
        snprintf(buf, sizeof(buf), "%.1f mm", height_mm);
    } else {
        snprintf(buf, sizeof(buf), "%.0f mm", height_mm);
    }
    return std::string(buf);
}

std::string format_file_size(size_t bytes) {
    char buf[32];
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        double kb = bytes / 1024.0;
        snprintf(buf, sizeof(buf), "%.1f KB", kb);
    } else if (bytes < 1024 * 1024 * 1024) {
        double mb = bytes / (1024.0 * 1024.0);
        snprintf(buf, sizeof(buf), "%.1f MB", mb);
    } else {
        double gb = bytes / (1024.0 * 1024.0 * 1024.0);
        snprintf(buf, sizeof(buf), "%.2f GB", gb);
    }
    return std::string(buf);
}

const char* get_time_format_string() {
    TimeFormat format = SettingsManager::instance().get_time_format();
    // %l = hour (1-12, space-padded), %I = hour (01-12, zero-padded)
    // Using %l for cleaner display without leading zero
    return (format == TimeFormat::HOUR_12) ? "%l:%M %p" : "%H:%M";
}

std::string format_time(const struct tm* tm_info) {
    if (!tm_info) {
        return helix::fmt::UNAVAILABLE;
    }

    char buf[16];
    strftime(buf, sizeof(buf), get_time_format_string(), tm_info);

    // Trim leading space from %l if present (space-padded hour)
    std::string result(buf);
    if (!result.empty() && result[0] == ' ') {
        result.erase(0, 1);
    }
    return result;
}

std::string format_modified_date(time_t timestamp) {
    char buf[64];
    struct tm* timeinfo = localtime(&timestamp);
    if (timeinfo) {
        // Format: "Jan 15 2:30 PM" (12H) or "Jan 15 14:30" (24H)
        TimeFormat format = SettingsManager::instance().get_time_format();
        if (format == TimeFormat::HOUR_12) {
            strftime(buf, sizeof(buf), "%b %d %l:%M %p", timeinfo);
            // Trim double spaces from %l (space-padded hour)
            std::string result(buf);
            size_t pos;
            while ((pos = result.find("  ")) != std::string::npos) {
                result.erase(pos, 1);
            }
            return result;
        } else {
            strftime(buf, sizeof(buf), "%b %d %H:%M", timeinfo);
        }
    } else {
        snprintf(buf, sizeof(buf), "Unknown");
    }
    return std::string(buf);
}

lv_coord_t ui_get_header_content_padding(lv_coord_t screen_height) {
    (void)screen_height; // Parameter kept for API stability

    // Use unified space_* system - values are already responsive based on breakpoint
    // set during theme initialization (space_lg = 12/16/20px at small/medium/large)
    int32_t spacing = theme_manager_get_spacing("space_lg");

    // Fallback if theme not initialized (e.g., in unit tests)
    constexpr int32_t DEFAULT_SPACE_LG = 16; // Medium breakpoint value
    if (spacing == 0) {
        spacing = DEFAULT_SPACE_LG;
    }

    return spacing;
}

lv_coord_t ui_get_responsive_header_height(lv_coord_t screen_height) {
    // Responsive header heights for space efficiency:
    // Large/Medium (≥600px): 60px (comfortable)
    // Small (480-599px): 48px (compact)
    // Tiny (≤479px): 40px (minimal)

    if (screen_height >= UI_SCREEN_MEDIUM_H) {
        return 60;
    } else if (screen_height >= UI_SCREEN_SMALL_H) {
        return 48;
    } else {
        return 40;
    }
}

// ============================================================================
// LED Icon Utilities
// ============================================================================

const char* ui_brightness_to_lightbulb_icon(int brightness) {
    // Clamp to valid range
    if (brightness <= 0) {
        return "lightbulb_outline"; // OFF state
    }
    if (brightness < 15) {
        return "lightbulb_on_10";
    }
    if (brightness < 25) {
        return "lightbulb_on_20";
    }
    if (brightness < 35) {
        return "lightbulb_on_30";
    }
    if (brightness < 45) {
        return "lightbulb_on_40";
    }
    if (brightness < 55) {
        return "lightbulb_on_50";
    }
    if (brightness < 65) {
        return "lightbulb_on_60";
    }
    if (brightness < 75) {
        return "lightbulb_on_70";
    }
    if (brightness < 85) {
        return "lightbulb_on_80";
    }
    if (brightness < 95) {
        return "lightbulb_on_90";
    }
    return "lightbulb_on"; // 100%
}

// ============================================================================
// Color Utilities
// ============================================================================

std::optional<uint32_t> ui_parse_hex_color(const std::string& hex_str) {
    if (hex_str.empty()) {
        return std::nullopt;
    }

    std::string hex = hex_str;
    if (hex[0] == '#') {
        hex = hex.substr(1);
    }

    if (hex.length() != 6) {
        return std::nullopt;
    }

    try {
        return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    } catch (...) {
        return std::nullopt;
    }
}

int ui_color_distance(uint32_t color1, uint32_t color2) {
    int r1 = (color1 >> 16) & 0xFF;
    int g1 = (color1 >> 8) & 0xFF;
    int b1 = color1 & 0xFF;

    int r2 = (color2 >> 16) & 0xFF;
    int g2 = (color2 >> 8) & 0xFF;
    int b2 = color2 & 0xFF;

    // Weighted distance - green is most perceptible to human eye
    int dr = r1 - r2;
    int dg = g1 - g2;
    int db = b1 - b2;

    // Weights: R=0.30, G=0.59, B=0.11 (standard luminance)
    // Squared for distance calculation, then sqrt
    int dist_sq = (dr * dr * 30 + dg * dg * 59 + db * db * 11) / 100;
    return static_cast<int>(std::sqrt(static_cast<double>(dist_sq)));
}

// ============================================================================
// Image Scaling Utilities
// ============================================================================

bool ui_image_scale_to_cover(lv_obj_t* image_widget, lv_coord_t target_width,
                             lv_coord_t target_height) {
    if (!image_widget) {
        spdlog::error("[UI Utils] Cannot scale image: widget is null");
        return false;
    }

    // Get source image dimensions
    lv_image_header_t header;
    lv_result_t res = lv_image_decoder_get_info(lv_image_get_src(image_widget), &header);

    if (res != LV_RESULT_OK || header.w == 0 || header.h == 0) {
        int w = header.w, h = header.h; // Copy bitfields for formatting
        spdlog::warn("[UI Utils] Cannot get image info for scaling (res={}, w={}, h={})",
                     static_cast<int>(res), w, h);
        return false;
    }

    // Calculate scale to cover the target area (like CSS object-fit: cover)
    // Use larger scale factor so image fills entire area (may crop)
    float scale_w = (float)target_width / header.w;
    float scale_h = (float)target_height / header.h;
    float scale = (scale_w > scale_h) ? scale_w : scale_h; // Use larger scale to cover

    // LVGL uses zoom as fixed-point: 256 = 1.0x, 512 = 2.0x, etc.
    uint16_t zoom = (uint16_t)(scale * 256);
    lv_image_set_scale(image_widget, zoom);
    lv_image_set_inner_align(image_widget, LV_IMAGE_ALIGN_CENTER);

    int img_w = header.w, img_h = header.h; // Copy bitfields for formatting
    spdlog::debug("[UI Utils] Image scale (cover): img={}x{}, target={}x{}, zoom={} ({:.1f}%)",
                  img_w, img_h, target_width, target_height, zoom, scale * 100);

    return true;
}

bool ui_image_scale_to_contain(lv_obj_t* image_widget, lv_coord_t target_width,
                               lv_coord_t target_height, lv_image_align_t align) {
    if (!image_widget) {
        spdlog::error("[UI Utils] Cannot scale image: widget is null");
        return false;
    }

    // Get source image dimensions
    lv_image_header_t header;
    lv_result_t res = lv_image_decoder_get_info(lv_image_get_src(image_widget), &header);

    if (res != LV_RESULT_OK || header.w == 0 || header.h == 0) {
        int w = header.w, h = header.h; // Copy bitfields for formatting
        spdlog::warn("[UI Utils] Cannot get image info for scaling (res={}, w={}, h={})",
                     static_cast<int>(res), w, h);
        return false;
    }

    // Calculate scale to contain the image (like CSS object-fit: contain)
    // Use smaller scale factor so entire image fits within area (no crop)
    float scale_w = (float)target_width / header.w;
    float scale_h = (float)target_height / header.h;
    float scale = (scale_w < scale_h) ? scale_w : scale_h; // Use smaller scale to contain

    // LVGL uses zoom as fixed-point: 256 = 1.0x, 512 = 2.0x, etc.
    uint16_t zoom = (uint16_t)(scale * 256);
    lv_image_set_scale(image_widget, zoom);
    lv_image_set_inner_align(image_widget, align);

    int img_w = header.w, img_h = header.h; // Copy bitfields for formatting
    spdlog::debug("[UI Utils] Image scale (contain): img={}x{}, target={}x{}, zoom={} ({:.1f}%)",
                  img_w, img_h, target_width, target_height, zoom, scale * 100);

    return true;
}

// ============================================================================
// Touch Feedback Utilities
// ============================================================================

void ui_create_ripple(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, int start_size, int end_size,
                      int32_t duration_ms) {
    // Skip animation if disabled
    if (!SettingsManager::instance().get_animations_enabled()) {
        spdlog::trace("[UI Utils] Animations disabled - skipping ripple");
        return;
    }

    if (!parent) {
        return;
    }

    // Create circle object for ripple effect
    lv_obj_t* ripple = lv_obj_create(parent);
    lv_obj_remove_style_all(ripple);

    // Initial size (small circle)
    lv_obj_set_size(ripple, start_size, start_size);
    lv_obj_set_style_radius(ripple, LV_RADIUS_CIRCLE, 0);

    // Style: primary color, semi-transparent
    lv_obj_set_style_bg_color(ripple, theme_manager_get_color("primary"), 0);
    lv_obj_set_style_bg_opa(ripple, LV_OPA_50, 0);
    lv_obj_set_style_border_width(ripple, 0, 0);

    // Take out of flex layout so position works, and make non-clickable
    lv_obj_add_flag(ripple, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(ripple, LV_OBJ_FLAG_CLICKABLE);

    // Position centered on touch point
    lv_obj_set_pos(ripple, x - start_size / 2, y - start_size / 2);

    // Animation 1: Scale (grow)
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, ripple);
    lv_anim_set_values(&scale_anim, start_size, end_size);
    lv_anim_set_duration(&scale_anim, duration_ms);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&scale_anim, [](void* var, int32_t size) {
        auto* obj = static_cast<lv_obj_t*>(var);
        lv_coord_t old_size = lv_obj_get_width(obj);
        lv_coord_t delta = (size - old_size) / 2;
        lv_obj_set_size(obj, size, size);
        // Use style values (not coords) - coords aren't updated until layout refresh
        int32_t current_x = lv_obj_get_style_x(obj, LV_PART_MAIN);
        int32_t current_y = lv_obj_get_style_y(obj, LV_PART_MAIN);
        lv_obj_set_pos(obj, current_x - delta, current_y - delta);
    });
    lv_anim_start(&scale_anim);

    // Animation 2: Fade out
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, ripple);
    lv_anim_set_values(&fade_anim, LV_OPA_50, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_anim, duration_ms);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* var, int32_t opa) {
        lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(opa), 0);
    });
    lv_anim_set_completed_cb(&fade_anim, [](lv_anim_t* a) {
        // Delete ripple object when animation completes
        lv_obj_t* widget = static_cast<lv_obj_t*>(a->var);
        lv_obj_safe_delete(widget);
    });
    lv_anim_start(&fade_anim);
}

// ============================================================================
// Backdrop Utilities
// ============================================================================

lv_obj_t* ui_create_fullscreen_backdrop(lv_obj_t* parent, lv_opa_t opacity) {
    if (!parent) {
        spdlog::error("[UI Utils] Cannot create backdrop: parent is null");
        return nullptr;
    }

    lv_obj_t* backdrop = lv_obj_create(parent);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, opacity, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(backdrop, 0, LV_PART_MAIN);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

    spdlog::trace("[UI Utils] Created fullscreen backdrop with opacity {}", opacity);
    return backdrop;
}
