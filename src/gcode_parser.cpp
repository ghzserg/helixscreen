// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 HelixScreen Contributors
 *
 * This file is part of HelixScreen, which is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * See <https://www.gnu.org/licenses/>.
 */

#include "gcode_parser.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace gcode {

// ============================================================================
// ParsedGCodeFile Methods
// ============================================================================

int ParsedGCodeFile::find_layer_at_z(float z) const {
    if (layers.empty()) {
        return -1;
    }

    // Binary search for closest Z height
    int left = 0;
    int right = static_cast<int>(layers.size()) - 1;
    int closest = 0;
    float min_diff = std::abs(layers[0].z_height - z);

    constexpr float epsilon = 0.0001f; // Tolerance for floating point comparison

    while (left <= right) {
        int mid = left + (right - left) / 2;
        float diff = std::abs(layers[mid].z_height - z);

        // Update closest if this is better, or if equal distance but prefer lower Z height
        if (diff < min_diff || (std::abs(diff - min_diff) < epsilon &&
                                layers[mid].z_height < layers[closest].z_height)) {
            min_diff = diff;
            closest = mid;
        }

        if (layers[mid].z_height < z) {
            left = mid + 1;
        } else if (layers[mid].z_height > z) {
            right = mid - 1;
        } else {
            return mid; // Exact match
        }
    }

    return closest;
}

// ============================================================================
// GCodeParser Implementation
// ============================================================================

GCodeParser::GCodeParser() {
    reset();
}

void GCodeParser::reset() {
    current_position_ = glm::vec3(0.0f, 0.0f, 0.0f);
    current_e_ = 0.0f;
    current_object_.clear();
    is_absolute_positioning_ = true;
    is_absolute_extrusion_ = true;
    layers_.clear();
    objects_.clear();
    global_bounds_ = AABB();
    lines_parsed_ = 0;

    // Layers will be created on-demand when segments are added
    // (see add_segment() which creates a layer if layers_ is empty)
}

void GCodeParser::parse_line(const std::string& line) {
    lines_parsed_++;

    // Extract and parse metadata comments before trimming
    size_t comment_pos = line.find(';');
    if (comment_pos != std::string::npos) {
        std::string comment = line.substr(comment_pos);
        parse_metadata_comment(comment);
        parse_wipe_tower_marker(comment);
    }

    std::string trimmed = trim_line(line);
    if (trimmed.empty()) {
        return;
    }

    // Check for tool changes (T0, T1, T2, etc.)
    if (!trimmed.empty() && trimmed[0] == 'T') {
        parse_tool_change_command(trimmed);
        // Continue processing - some G-code files have commands after tool changes
    }

    // Check for EXCLUDE_OBJECT commands first
    if (trimmed.find("EXCLUDE_OBJECT") == 0) {
        parse_exclude_object_command(trimmed);
        return;
    }

    // Parse positioning mode commands
    if (trimmed == "G90") {
        is_absolute_positioning_ = true;
        return;
    } else if (trimmed == "G91") {
        is_absolute_positioning_ = false;
        return;
    } else if (trimmed == "M82") {
        is_absolute_extrusion_ = true;
        return;
    } else if (trimmed == "M83") {
        is_absolute_extrusion_ = false;
        return;
    }

    // Parse movement commands (G0, G1)
    if (trimmed[0] == 'G' && (trimmed.find("G0 ") == 0 || trimmed.find("G1 ") == 0 ||
                              trimmed == "G0" || trimmed == "G1")) {
        parse_movement_command(trimmed);
    }
}

bool GCodeParser::parse_movement_command(const std::string& line) {
    glm::vec3 new_position = current_position_;
    float new_e = current_e_;
    bool has_movement = false;
    bool has_extrusion = false;

    // Extract X, Y, Z parameters
    float value;
    if (extract_param(line, 'X', value)) {
        new_position.x = is_absolute_positioning_ ? value : current_position_.x + value;
        has_movement = true;
    }
    if (extract_param(line, 'Y', value)) {
        new_position.y = is_absolute_positioning_ ? value : current_position_.y + value;
        has_movement = true;
    }
    if (extract_param(line, 'Z', value)) {
        new_position.z = is_absolute_positioning_ ? value : current_position_.z + value;
        has_movement = true;

        // Layer change detected
        if (std::abs(new_position.z - current_position_.z) > 0.001f) {
            start_new_layer(new_position.z);
        }
    }

    // Extract E (extrusion) parameter
    if (extract_param(line, 'E', value)) {
        new_e = is_absolute_extrusion_ ? value : current_e_ + value;
        has_extrusion = true;
    }

    // Add segment if there's XY movement
    if (has_movement &&
        (new_position.x != current_position_.x || new_position.y != current_position_.y)) {
        // Determine if this is an extrusion move
        bool is_extruding = false;
        float e_delta = 0.0f;
        if (has_extrusion) {
            e_delta = new_e - current_e_;
            is_extruding = (e_delta > 0.00001f); // Small threshold for floating point
        }

        add_segment(current_position_, new_position, is_extruding, e_delta);
    }

    // Update state
    current_position_ = new_position;
    if (has_extrusion) {
        current_e_ = new_e;
    }

    return has_movement;
}

bool GCodeParser::parse_exclude_object_command(const std::string& line) {
    // EXCLUDE_OBJECT_DEFINE NAME=... CENTER=... POLYGON=...
    if (line.find("EXCLUDE_OBJECT_DEFINE") == 0) {
        std::string name;
        if (!extract_string_param(line, "NAME", name)) {
            return false;
        }

        GCodeObject obj;
        obj.name = name;

        // Extract CENTER (format: "X,Y")
        std::string center_str;
        if (extract_string_param(line, "CENTER", center_str)) {
            size_t comma = center_str.find(',');
            if (comma != std::string::npos) {
                try {
                    obj.center.x = std::stof(center_str.substr(0, comma));
                    obj.center.y = std::stof(center_str.substr(comma + 1));
                } catch (...) {
                    spdlog::warn("Failed to parse CENTER for object: {}", name);
                }
            }
        }

        // Extract POLYGON (format: "[[x1,y1],[x2,y2],...]")
        // For now, we'll do basic parsing - full JSON parsing would be better
        std::string polygon_str;
        if (extract_string_param(line, "POLYGON", polygon_str)) {
            // Simple extraction of number pairs
            // Remove all whitespace first for easier parsing
            polygon_str.erase(std::remove_if(polygon_str.begin(), polygon_str.end(), ::isspace),
                              polygon_str.end());

            // Skip outer opening bracket if present
            size_t pos = 0;
            if (!polygon_str.empty() && polygon_str[0] == '[') {
                pos = 1;
            }

            while (pos < polygon_str.length()) {
                // Find opening bracket for this point
                if (polygon_str[pos] == '[') {
                    pos++;
                    // Extract x coordinate (everything until comma)
                    size_t comma = polygon_str.find(',', pos);
                    if (comma != std::string::npos) {
                        try {
                            float x = std::stof(polygon_str.substr(pos, comma - pos));
                            pos = comma + 1;

                            // Extract y coordinate (everything until closing bracket)
                            size_t close = polygon_str.find(']', pos);
                            if (close != std::string::npos) {
                                float y = std::stof(polygon_str.substr(pos, close - pos));
                                obj.polygon.push_back(glm::vec2(x, y));
                                pos = close + 1;
                                spdlog::trace("Parsed polygon point: ({}, {})", x, y);
                            } else {
                                break;
                            }
                        } catch (...) {
                            break;
                        }
                    } else {
                        break;
                    }
                } else {
                    pos++;
                }
            }
        }

        objects_[name] = obj;
        spdlog::debug("Defined object: {} at ({}, {})", name, obj.center.x, obj.center.y);
        return true;
    }
    // EXCLUDE_OBJECT_START NAME=...
    else if (line.find("EXCLUDE_OBJECT_START") == 0) {
        if (!extract_string_param(line, "NAME", current_object_)) {
            current_object_.clear();
            return false;
        }
        spdlog::trace("Started object: {}", current_object_);
        return true;
    }
    // EXCLUDE_OBJECT_END NAME=...
    else if (line.find("EXCLUDE_OBJECT_END") == 0) {
        std::string name;
        if (extract_string_param(line, "NAME", name) && name == current_object_) {
            spdlog::trace("Ended object: {}", current_object_);
            current_object_.clear();
            return true;
        }
    }

    return false;
}

void GCodeParser::parse_metadata_comment(const std::string& line) {
    // OrcaSlicer/PrusaSlicer format: "; key = value"
    // Use fuzzy matching to handle variations across slicers

    if (line.length() < 3 || line[0] != ';') {
        return;
    }

    // Skip '; ' to get key=value part
    std::string content = line.substr(1);

    // Trim leading whitespace
    size_t start = 0;
    while (start < content.length() && std::isspace(content[start])) {
        start++;
    }
    content = content.substr(start);

    // Look for '=' separator
    size_t eq_pos = content.find('=');
    if (eq_pos == std::string::npos) {
        return;
    }

    // Extract key and value
    std::string key = content.substr(0, eq_pos);
    std::string value = content.substr(eq_pos + 1);

    // Trim whitespace from key and value
    auto trim = [](std::string& s) {
        size_t start = 0;
        while (start < s.length() && std::isspace(s[start]))
            start++;
        size_t end = s.length();
        while (end > start && std::isspace(s[end - 1]))
            end--;
        s = s.substr(start, end - start);
    };
    trim(key);
    trim(value);

    // Convert key to lowercase for case-insensitive matching
    std::string key_lower = key;
    std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);

    // Helper to check if key contains all substrings (fuzzy match)
    auto contains_all = [&key_lower](std::initializer_list<const char*> terms) {
        for (const char* term : terms) {
            if (key_lower.find(term) == std::string::npos) {
                return false;
            }
        }
        return true;
    };

    // Parse specific metadata fields with fuzzy matching
    // Multi-color: Check for extruder_colour first (priority over single filament_colour)
    if (key_lower.find("extruder_colour") != std::string::npos ||
        key_lower.find("extruder_color") != std::string::npos) {
        parse_extruder_color_metadata(line);
    }
    // Fallback: Parse single filament_colour if extruder_colour not yet found
    else if (contains_all({"filament", "col"}) && tool_color_palette_.empty()) {
        // Check if it's a semicolon-separated list (multi-color)
        if (value.find(';') != std::string::npos) {
            parse_extruder_color_metadata(line);
        } else {
            // Single color metadata
            metadata_filament_color_ = value;
            spdlog::trace("Parsed single filament color: {}", value);
        }
    } else if (contains_all({"filament", "type"})) {
        metadata_filament_type_ = value;
        spdlog::trace("Parsed filament type: {}", value);
    } else if (contains_all({"printer", "model"}) || contains_all({"printer", "name"})) {
        metadata_printer_model_ = value;
        spdlog::trace("Parsed printer model: {}", value);
    } else if (contains_all({"nozzle", "diameter"})) {
        try {
            metadata_nozzle_diameter_ = std::stof(value);
            spdlog::trace("Parsed nozzle diameter: {}mm", metadata_nozzle_diameter_);
        } catch (...) {
        }
    } else if (contains_all({"filament"}) &&
               (key_lower.find("[mm]") != std::string::npos || contains_all({"length"}))) {
        try {
            metadata_filament_length_ = std::stof(value);
            spdlog::trace("Parsed filament length: {}mm", metadata_filament_length_);
        } catch (...) {
        }
    } else if (contains_all({"filament"}) &&
               (key_lower.find("[g]") != std::string::npos || contains_all({"weight"}))) {
        try {
            metadata_filament_weight_ = std::stof(value);
            spdlog::trace("Parsed filament weight: {}g", metadata_filament_weight_);
        } catch (...) {
        }
    } else if (contains_all({"filament", "cost"}) || contains_all({"material", "cost"})) {
        try {
            metadata_filament_cost_ = std::stof(value);
            spdlog::trace("Parsed filament cost: ${}", metadata_filament_cost_);
        } catch (...) {
        }
    } else if (contains_all({"layer"}) &&
               (contains_all({"total"}) || contains_all({"number"}) || contains_all({"count"}))) {
        try {
            metadata_layer_count_ = std::stoi(value);
            spdlog::trace("Parsed total layer count: {}", metadata_layer_count_);
        } catch (...) {
        }
    } else if ((contains_all({"time"}) &&
                (contains_all({"print"}) || contains_all({"estimated"}))) ||
               contains_all({"print", "time"})) {
        // Parse various time formats: "29m 25s", "1h 23m", "45s", etc.
        float minutes = 0.0f;

        // Try to find hours
        size_t h_pos = value.find('h');
        if (h_pos != std::string::npos) {
            try {
                float hours = std::stof(value.substr(0, h_pos));
                minutes += hours * 60.0f;
            } catch (...) {
            }
        }

        // Try to find minutes
        size_t m_pos = value.find('m');
        if (m_pos != std::string::npos) {
            try {
                size_t start_pos = (h_pos != std::string::npos) ? h_pos + 1 : 0;
                std::string min_str = value.substr(start_pos, m_pos - start_pos);
                // Trim spaces
                size_t min_start = 0;
                while (min_start < min_str.length() && std::isspace(min_str[min_start]))
                    min_start++;
                if (min_start < min_str.length()) {
                    minutes += std::stof(min_str.substr(min_start));
                }
            } catch (...) {
            }
        }

        // Try to find seconds
        size_t s_pos = value.find('s');
        if (s_pos != std::string::npos) {
            try {
                size_t start_pos = (m_pos != std::string::npos)   ? m_pos + 1
                                   : (h_pos != std::string::npos) ? h_pos + 1
                                                                  : 0;
                std::string sec_str = value.substr(start_pos, s_pos - start_pos);
                // Trim spaces
                size_t sec_start = 0;
                while (sec_start < sec_str.length() && std::isspace(sec_str[sec_start]))
                    sec_start++;
                if (sec_start < sec_str.length()) {
                    float seconds = std::stof(sec_str.substr(sec_start));
                    minutes += seconds / 60.0f;
                }
            } catch (...) {
            }
        }

        if (minutes > 0.0f) {
            metadata_print_time_ = minutes;
            spdlog::trace("Parsed estimated time: {:.2f} minutes", minutes);
        }
    } else if (contains_all({"generated"}) || contains_all({"slicer"})) {
        metadata_slicer_name_ = value;
        spdlog::trace("Parsed slicer: {}", value);
    }
    // Parse extrusion width metadata
    // OrcaSlicer/PrusaSlicer/SuperSlicer: "; perimeters extrusion width = 0.45mm"
    // Cura: ";SETTING_3 line_width = 0.4" or ";SETTING_3 wall_line_width_0 = 0.4"
    else if (contains_all({"extrusion", "width"}) ||
             (key_lower.find("line_width") != std::string::npos) ||
             (key_lower.find("linewidth") != std::string::npos)) {
        // Extract numeric value (handle "0.45mm" format and plain "0.4")
        std::string numeric_value = value;
        // Remove "mm" suffix if present
        size_t mm_pos = numeric_value.find("mm");
        if (mm_pos != std::string::npos) {
            numeric_value = numeric_value.substr(0, mm_pos);
        }

        try {
            float width = std::stof(numeric_value);

            // Categorize by feature type
            if (contains_all({"first", "layer"}) || contains_all({"initial", "layer"})) {
                metadata_first_layer_extrusion_width_ = width;
                spdlog::trace("Parsed first layer extrusion width: {}mm", width);
            } else if (contains_all({"perimeter"}) || key_lower.find("wall") != std::string::npos) {
                // Handles "perimeter" (Prusa/Orca) and "wall" (Cura)
                metadata_perimeter_extrusion_width_ = width;
                spdlog::trace("Parsed perimeter/wall extrusion width: {}mm", width);
            } else if (contains_all({"infill"})) {
                metadata_infill_extrusion_width_ = width;
                spdlog::trace("Parsed infill extrusion width: {}mm", width);
            } else {
                // General extrusion width (fallback for "line_width", etc.)
                if (metadata_extrusion_width_ == 0.0f) {
                    metadata_extrusion_width_ = width;
                    spdlog::trace("Parsed default extrusion width: {}mm", width);
                }
            }
        } catch (...) {
            // Failed to parse width value
        }
    }
}

void GCodeParser::parse_extruder_color_metadata(const std::string& line) {
    // Format: "; extruder_colour = #ED1C24;#00C1AE;#F4E2C1;#000000"
    //     OR: "; filament_colour = ..." (fallback)
    //     OR: ";extruder_colour=#AA0000 ; #00BB00 ;#0000CC" (with variations)

    // Find '=' character (with or without spaces)
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
        return;
    }

    std::string colors_str = line.substr(eq_pos + 1);

    // Trim leading whitespace from colors_str
    size_t start = colors_str.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) {
        colors_str = colors_str.substr(start);
    }

    // Split by semicolons
    std::stringstream ss(colors_str);
    std::string color;
    while (std::getline(ss, color, ';')) {
        // Trim whitespace
        color.erase(0, color.find_first_not_of(" \t\r\n"));
        color.erase(color.find_last_not_of(" \t\r\n") + 1);

        if (!color.empty() && color[0] == '#') {
            tool_color_palette_.push_back(color);
        } else if (!color.empty()) {
            // Non-empty but invalid format - use placeholder
            tool_color_palette_.push_back("");
        }
    }

    // Log color palette (manual join since fmt::join may not be available)
    std::string palette_str;
    for (size_t i = 0; i < tool_color_palette_.size(); ++i) {
        if (i > 0)
            palette_str += ", ";
        palette_str += tool_color_palette_[i];
    }
    spdlog::debug("Parsed {} extruder colors from metadata: [{}]", tool_color_palette_.size(),
                  palette_str);

    // Set metadata_filament_color_ to the first valid color (for single-color rendering fallback)
    if (!tool_color_palette_.empty() && !tool_color_palette_[0].empty()) {
        metadata_filament_color_ = tool_color_palette_[0];
    }
}

void GCodeParser::parse_tool_change_command(const std::string& line) {
    // Format: "T0", "T1", "T2", etc. (standalone line)
    if (line.empty() || line[0] != 'T') {
        return;
    }

    // Check if it's JUST "T" + digits (no other commands on line)
    if (line.length() < 2) {
        return;
    }

    // Extract tool number
    size_t i = 1;
    while (i < line.length() && std::isdigit(line[i])) {
        i++;
    }

    if (i == 1) {
        return; // No digits after T
    }
    if (i < line.length() && !std::isspace(line[i])) {
        return; // Not standalone
    }

    std::string tool_str = line.substr(1, i - 1);
    int tool_num = std::stoi(tool_str);

    current_tool_index_ = tool_num;
    spdlog::debug("Tool change: T{}", tool_num);
}

void GCodeParser::parse_wipe_tower_marker(const std::string& comment) {
    if (comment.find("WIPE_TOWER_START") != std::string::npos ||
        comment.find("WIPE_TOWER_BRIM_START") != std::string::npos) {
        in_wipe_tower_ = true;
        spdlog::debug("Entering wipe tower section");
    } else if (comment.find("WIPE_TOWER_END") != std::string::npos ||
               comment.find("WIPE_TOWER_BRIM_END") != std::string::npos) {
        in_wipe_tower_ = false;
        spdlog::debug("Exiting wipe tower section");
    }
}

bool GCodeParser::extract_param(const std::string& line, char param, float& out_value) {
    size_t pos = line.find(param);
    if (pos == std::string::npos) {
        return false;
    }

    // Make sure it's a parameter (preceded by space or at start after command)
    if (pos > 0 && line[pos - 1] != ' ' && line[pos - 1] != '\t') {
        return false;
    }

    // Extract number after parameter letter
    size_t start = pos + 1;
    if (start >= line.length()) {
        return false;
    }

    // Find end of number (space, end of string, or another letter)
    size_t end = start;
    while (end < line.length() &&
           (std::isdigit(line[end]) || line[end] == '.' || line[end] == '-' || line[end] == '+')) {
        end++;
    }

    if (end == start) {
        return false;
    }

    try {
        out_value = std::stof(line.substr(start, end - start));
        return true;
    } catch (...) {
        return false;
    }
}

bool GCodeParser::extract_string_param(const std::string& line, const std::string& param,
                                       std::string& out_value) {
    size_t pos = line.find(param + "=");
    if (pos == std::string::npos) {
        return false;
    }

    size_t start = pos + param.length() + 1; // Skip "PARAM="
    if (start >= line.length()) {
        return false;
    }

    // Find end of value (space or end of line)
    size_t end = line.find(' ', start);
    if (end == std::string::npos) {
        end = line.length();
    }

    out_value = line.substr(start, end - start);
    return true;
}

void GCodeParser::add_segment(const glm::vec3& start, const glm::vec3& end, bool is_extrusion,
                              float e_delta) {
    if (layers_.empty()) {
        start_new_layer(start.z);
    }

    ToolpathSegment segment;
    segment.start = start;
    segment.end = end;
    segment.is_extrusion = is_extrusion;
    segment.object_name = current_object_;
    segment.extrusion_amount = e_delta;

    // Multi-color support: Tag segment with current tool
    segment.tool_index = current_tool_index_;

    // Wipe tower support: Tag wipe tower segments with special object name
    if (in_wipe_tower_) {
        segment.object_name = "__WIPE_TOWER__";
    }

    // Calculate actual extrusion width from E-delta and XY distance
    if (is_extrusion && e_delta > 0.00001f) {
        // Calculate XY distance
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float xy_distance = std::sqrt(dx * dx + dy * dy);

        if (xy_distance > 0.00001f) {
            // Calculate filament cross-sectional area
            float filament_radius = metadata_filament_diameter_ / 2.0f;
            float filament_area = M_PI * filament_radius * filament_radius;

            // Calculate extruded volume: volume = e_delta * filament_area
            float volume = e_delta * filament_area;

            // Calculate width using Slic3r's oval cross-section formula with empirical correction
            // Extruded plastic forms an oval/rounded shape, not a rectangle
            // Cross-sectional area: A = (w - h) × h + π × (h/2)²
            // Where: A = volume / distance, h = layer_height, w = width
            // Solving for w: w = (A - π × (h/2)²) / h + h
            //
            // Empirical 2x correction factor accounts for slicer-specific settings:
            // - First-layer extrusion width multipliers (Slic3r defaults to 200%)
            // - Flow rate compensation and extrusion multipliers
            // - Perimeter overlap calculations
            // Testing shows this produces widths matching G-code metadata (0.42mm)
            float h = metadata_layer_height_;
            float cross_section_area = volume / xy_distance;
            float h_radius = h / 2.0f;
            float circular_area = M_PI * h_radius * h_radius;
            float calculated_width = (cross_section_area - circular_area) / h + h;
            segment.width = calculated_width * 2.0f; // Empirical correction factor

            // Sanity check: width should be reasonable (0.1mm to 2.0mm)
            if (segment.width < 0.1f || segment.width > 2.0f) {
                spdlog::debug("Calculated out-of-range extrusion width: {:.3f}mm (e={:.3f}, "
                              "dist={:.3f}, layer_h={:.3f}) - using default",
                              segment.width, e_delta, xy_distance, metadata_layer_height_);
                segment.width = 0.0f; // Use default
            }
        }
    }

    // Update layer data
    Layer& current_layer = layers_.back();
    current_layer.segments.push_back(segment);

    // For bounding box: skip start position if this is the first segment ever
    // (avoids including implicit (0,0,0) starting position in print bounds)
    bool is_first_segment = (layers_.size() == 1 && current_layer.segments.size() == 1);

    if (!is_first_segment) {
        current_layer.bounding_box.expand(start);
        global_bounds_.expand(start);
    }
    current_layer.bounding_box.expand(end);
    global_bounds_.expand(end);

    if (is_extrusion) {
        current_layer.segment_count_extrusion++;
    } else {
        current_layer.segment_count_travel++;
    }

    // Update object bounding box (only for extrusion moves, not travels)
    if (!current_object_.empty() && objects_.count(current_object_) > 0 && is_extrusion) {
        objects_[current_object_].bounding_box.expand(start);
        objects_[current_object_].bounding_box.expand(end);

        // Debug: Log first few extrusion segments per object
        static std::map<std::string, int> segment_counts;
        segment_counts[current_object_]++;
        if (segment_counts[current_object_] <= 3) {
            spdlog::trace("Object '{}' extrusion segment: start=({:.2f},{:.2f},{:.2f}) "
                          "end=({:.2f},{:.2f},{:.2f})",
                          current_object_, start.x, start.y, start.z, end.x, end.y, end.z);
        }
    }
}

void GCodeParser::start_new_layer(float z) {
    // Don't create duplicate layers at same Z
    if (!layers_.empty() && std::abs(layers_.back().z_height - z) < 0.001f) {
        return;
    }

    Layer layer;
    layer.z_height = z;
    layers_.push_back(layer);

    spdlog::trace("Started layer {} at Z={:.3f}", layers_.size() - 1, z);
}

std::string GCodeParser::trim_line(const std::string& line) {
    if (line.empty()) {
        return line;
    }

    // Remove comments (everything after ';')
    size_t comment_pos = line.find(';');
    std::string without_comment =
        (comment_pos != std::string::npos) ? line.substr(0, comment_pos) : line;

    // Trim leading/trailing whitespace
    size_t start = 0;
    while (start < without_comment.length() && std::isspace(without_comment[start])) {
        start++;
    }

    if (start == without_comment.length()) {
        return "";
    }

    size_t end = without_comment.length();
    while (end > start && std::isspace(without_comment[end - 1])) {
        end--;
    }

    return without_comment.substr(start, end - start);
}

ParsedGCodeFile GCodeParser::finalize() {
    ParsedGCodeFile result;
    result.filename = "";
    result.layers = std::move(layers_);
    result.objects = std::move(objects_);
    result.global_bounding_box = global_bounds_;

    // Calculate statistics
    for (const auto& layer : result.layers) {
        result.total_segments += layer.segments.size();
    }

    // Transfer metadata
    result.slicer_name = metadata_slicer_name_;
    result.filament_type = metadata_filament_type_;
    result.filament_color_hex = metadata_filament_color_;
    result.printer_model = metadata_printer_model_;
    result.nozzle_diameter_mm = metadata_nozzle_diameter_;
    result.total_filament_mm = metadata_filament_length_;
    result.filament_weight_g = metadata_filament_weight_;
    result.filament_cost = metadata_filament_cost_;

    // Transfer extrusion width metadata
    result.extrusion_width_mm = metadata_extrusion_width_;
    result.perimeter_extrusion_width_mm = metadata_perimeter_extrusion_width_;
    result.infill_extrusion_width_mm = metadata_infill_extrusion_width_;
    result.first_layer_extrusion_width_mm = metadata_first_layer_extrusion_width_;
    result.estimated_print_time_minutes = metadata_print_time_;
    result.total_layer_count = metadata_layer_count_;

    // Transfer multi-color tool palette
    result.tool_color_palette = tool_color_palette_;

    spdlog::info("Parsed G-code: {} layers, {} segments, {} objects", result.layers.size(),
                 result.total_segments, result.objects.size());

    // Debug: Log object bounding boxes
    for (const auto& [name, obj] : result.objects) {
        spdlog::debug("Object '{}' AABB: min=({:.2f},{:.2f},{:.2f}) max=({:.2f},{:.2f},{:.2f}) "
                      "center=({:.2f},{:.2f},{:.2f})",
                      name, obj.bounding_box.min.x, obj.bounding_box.min.y, obj.bounding_box.min.z,
                      obj.bounding_box.max.x, obj.bounding_box.max.y, obj.bounding_box.max.z,
                      obj.bounding_box.center().x, obj.bounding_box.center().y,
                      obj.bounding_box.center().z);
    }

    // Reset state for potential reuse
    reset();

    return result;
}

} // namespace gcode
