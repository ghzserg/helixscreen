// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file gcode_parser.h
 * @brief Streaming G-code parser extracting toolpath, layers, and metadata
 *
 * @pattern Line-by-line streaming (no full buffer); layer-indexed geometry
 * @threading Main thread only
 * @gotchas clear_segments() frees 40-160MB after geometry build; layer detection via Z changes
 */

#pragma once

#include <glm/glm.hpp>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace helix {
namespace gcode {

/**
 * @brief Axis-aligned bounding box for spatial queries
 */
struct AABB {
    glm::vec3 min{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::infinity()};
    glm::vec3 max{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()};

    /**
     * @brief Get center point of bounding box
     * @return Center coordinate
     */
    glm::vec3 center() const {
        return (min + max) * 0.5f;
    }

    /**
     * @brief Get size (dimensions) of bounding box
     * @return Size vector (width, depth, height)
     */
    glm::vec3 size() const {
        return max - min;
    }

    /**
     * @brief Expand bounding box to include a point
     * @param point Point to include
     */
    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    /**
     * @brief Check if bounding box is empty (not initialized)
     * @return true if empty (min > max)
     */
    bool is_empty() const {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }
};

/**
 * @brief Single toolpath segment (line segment in 3D space)
 *
 * Represents movement from start to end point. Can be either:
 * - Extrusion move (is_extrusion=true): Plastic is deposited
 * - Travel move (is_extrusion=false): Nozzle moves without extruding
 */
struct ToolpathSegment {
    glm::vec3 start{0.0f, 0.0f, 0.0f}; ///< Start point (X, Y, Z)
    glm::vec3 end{0.0f, 0.0f, 0.0f};   ///< End point (X, Y, Z)
    bool is_extrusion{false};          ///< true if extruding, false if travel move
    std::string object_name;           ///< Object name (from EXCLUDE_OBJECT_START) or
                                       ///< empty
    float extrusion_amount{0.0f};      ///< E-axis delta (mm of filament)
    float width{0.0f};                 ///< Calculated extrusion width (mm) - 0 means use default
    int tool_index{0};                 ///< Which tool/extruder printed this (0-indexed)
};

/**
 * @brief Single layer of toolpath (constant Z-height)
 *
 * Contains all segments at a specific Z coordinate. Layers are indexed
 * sequentially from 0 (first layer) to N-1 (top layer).
 */
struct Layer {
    float z_height{0.0f};                  ///< Z coordinate of this layer
    std::vector<ToolpathSegment> segments; ///< All segments in layer
    AABB bounding_box;                     ///< Precomputed spatial bounds
    size_t segment_count_extrusion{0};     ///< Count of extrusion moves
    size_t segment_count_travel{0};        ///< Count of travel moves
};

/**
 * @brief Object metadata from EXCLUDE_OBJECT_DEFINE command
 *
 * Represents a named object in the print (e.g., "part_1", "support_3").
 * Used for Klipper's exclude objects feature.
 */
struct GCodeObject {
    std::string name;               ///< Object identifier
    glm::vec2 center{0.0f, 0.0f};   ///< Center point (X, Y)
    std::vector<glm::vec2> polygon; ///< Boundary polygon points
    AABB bounding_box;              ///< 3D bounding box
    bool is_excluded{false};        ///< User exclusion state (local UI state)
};

/**
 * @brief Parsed G-code file with layer-indexed toolpath data
 *
 * Final output of the parser. Contains all layers, objects, and metadata
 * needed for visualization and analysis.
 */
struct ParsedGCodeFile {
    std::string filename;                       ///< Source filename
    std::vector<Layer> layers;                  ///< Indexed by layer number
    std::map<std::string, GCodeObject> objects; ///< Object metadata (name → object)
    AABB global_bounding_box;                   ///< Bounds of entire model

    // Statistics
    size_t total_segments{0};                 ///< Total segment count
    float estimated_print_time_minutes{0.0f}; ///< From metadata (if available)
    float total_filament_mm{0.0f};            ///< From metadata (if available)

    // Slicer metadata (parsed from comments)
    std::string slicer_name;        ///< Slicer software name and version
    std::string filament_type;      ///< Filament material type (e.g., "PLA", "PETG")
    std::string filament_color_hex; ///< Filament color in hex format (e.g., "#26A69A")
    std::string printer_model;      ///< Printer model name
    float nozzle_diameter_mm{0.0f}; ///< Nozzle diameter in mm
    float filament_weight_g{0.0f};  ///< Total filament weight in grams
    float filament_cost{0.0f};      ///< Estimated filament cost
    int total_layer_count{0};       ///< Total layer count from metadata

    // Extrusion width metadata (from OrcaSlicer/PrusaSlicer headers)
    float extrusion_width_mm{0.0f}; ///< Default extrusion width (0 = use nozzle-based default)
    float perimeter_extrusion_width_mm{0.0f};   ///< Perimeter width
    float infill_extrusion_width_mm{0.0f};      ///< Infill width
    float first_layer_extrusion_width_mm{0.0f}; ///< First layer width
    float filament_diameter_mm{1.75f};          ///< Filament diameter (default: 1.75mm)
    float layer_height_mm{0.2f};                ///< Layer height (default: 0.2mm)

    // Multi-color support
    std::vector<std::string> tool_color_palette; ///< Hex colors per tool (e.g., ["#ED1C24", ...])

    /**
     * @brief Get layer at specific index
     * @param index Layer index (0-based)
     * @return Pointer to layer or nullptr if out of range
     */
    const Layer* get_layer(size_t index) const {
        return (index < layers.size()) ? &layers[index] : nullptr;
    }

    /**
     * @brief Find layer closest to Z height
     * @param z Z coordinate to search for
     * @return Layer index or -1 if no layers
     */
    int find_layer_at_z(float z) const;

    /**
     * @brief Clear segment data to free memory
     *
     * After geometry is built, the raw segment data is no longer needed.
     * This frees the segment vectors while preserving metadata (bounding box,
     * statistics, slicer info, etc.). Call this after geometry building to
     * reduce memory usage by 40-160MB on large files.
     *
     * @return Bytes freed (approximate)
     */
    size_t clear_segments() {
        size_t freed = 0;
        for (auto& layer : layers) {
            // Estimate ~80 bytes per segment
            freed += layer.segments.size() * 80;
            layer.segments.clear();
            layer.segments.shrink_to_fit();
        }
        // Also clear the objects polygon data (rarely used after geometry build)
        for (auto& [name, obj] : objects) {
            freed += obj.polygon.size() * sizeof(glm::vec2);
            obj.polygon.clear();
            obj.polygon.shrink_to_fit();
        }
        return freed;
    }
};

/**
 * @brief Streaming G-code parser
 *
 * Usage pattern:
 * @code
 *   GCodeParser parser;
 *   std::ifstream file("model.gcode");
 *   std::string line;
 *   while (std::getline(file, line)) {
 *       parser.parse_line(line);
 *   }
 *   ParsedGCodeFile result = parser.finalize();
 * @endcode
 *
 * The parser maintains state across parse_line() calls and accumulates
 * data. Call finalize() once when complete to get the final result.
 */
class GCodeParser {
  public:
    GCodeParser();
    ~GCodeParser() = default;

    /**
     * @brief Parse single line of G-code
     * @param line Raw G-code line (may include comments)
     *
     * Extracts movement commands, coordinate changes, and object metadata.
     * Automatically detects layer changes (Z-axis movement).
     */
    void parse_line(const std::string& line);

    /**
     * @brief Finalize parsing and return complete data structure
     * @return Parsed file with all layers and objects
     *
     * Call this after all lines have been parsed. Clears internal state.
     */
    ParsedGCodeFile finalize();

    /**
     * @brief Reset parser state for new file
     *
     * Clears all accumulated data. Use when parsing multiple files
     * with the same parser instance.
     */
    void reset();

    // Progress tracking

    /**
     * @brief Get number of lines parsed so far
     * @return Line count
     */
    size_t lines_parsed() const {
        return lines_parsed_;
    }

    /**
     * @brief Get current Z coordinate
     * @return Current Z position in mm
     */
    float current_z() const {
        return current_position_.z;
    }

    /**
     * @brief Get current layer index
     * @return Layer number (0-based)
     */
    size_t current_layer() const {
        return layers_.size() - 1;
    }

    /**
     * @brief Get tool color palette parsed from metadata
     * @return Vector of hex color strings (e.g., ["#ED1C24", "#00C1AE"])
     *
     * Returns empty vector if no color metadata was found.
     */
    const std::vector<std::string>& get_tool_color_palette() const {
        return tool_color_palette_;
    }

  private:
    // Parsing helpers

    /**
     * @brief Parse movement command (G0, G1)
     * @param line Trimmed G-code line
     * @return true if parsed successfully
     */
    bool parse_movement_command(const std::string& line);

    /**
     * @brief Parse EXCLUDE_OBJECT_* command
     * @param line Trimmed G-code line
     * @return true if parsed successfully
     */
    bool parse_exclude_object_command(const std::string& line);

    /**
     * @brief Parse slicer metadata from comment line
     * @param line Comment line (starts with ';')
     *
     * Extracts key-value pairs from slicer comments in OrcaSlicer/PrusaSlicer format.
     * Examples:
     * - "; filament_colour = #26A69A"
     * - "; estimated printing time (normal mode) = 29m 25s"
     * - "; printer_model = Flashforge Adventurer 5M Pro"
     */
    void parse_metadata_comment(const std::string& line);

    /**
     * @brief Parse extruder color palette from header metadata
     * @param line Comment line containing extruder_colour or filament_colour
     *
     * Extracts semicolon-separated hex color values for multi-color prints.
     * Format: "; extruder_colour = #ED1C24;#00C1AE;#F4E2C1;#000000"
     */
    void parse_extruder_color_metadata(const std::string& line);

    /**
     * @brief Parse tool change command (T0, T1, T2, etc.)
     * @param line Trimmed G-code line
     *
     * Updates current_tool_index_ when tool change commands are encountered.
     */
    void parse_tool_change_command(const std::string& line);

    /**
     * @brief Parse wipe tower markers from comments
     * @param comment Comment string
     *
     * Detects WIPE_TOWER_START/END markers for optional wipe tower filtering.
     */
    void parse_wipe_tower_marker(const std::string& comment);

    /**
     * @brief Extract parameter value from G-code line
     * @param line G-code line
     * @param param Parameter letter (e.g., 'X', 'Y', 'Z')
     * @param out_value Output value
     * @return true if parameter found
     */
    bool extract_param(const std::string& line, char param, float& out_value);

    /**
     * @brief Extract string parameter value
     * @param line G-code line
     * @param param Parameter name (e.g., "NAME")
     * @param out_value Output string
     * @return true if parameter found
     */
    bool extract_string_param(const std::string& line, const std::string& param,
                              std::string& out_value);

    /**
     * @brief Add toolpath segment to current layer
     * @param start Start point
     * @param end End point
     * @param is_extrusion true if extruding
     * @param e_delta Extruder delta (mm of filament), used to calculate width
     */
    void add_segment(const glm::vec3& start, const glm::vec3& end, bool is_extrusion,
                     float e_delta = 0.0f);

    /**
     * @brief Start new layer at given Z height
     * @param z Z coordinate
     */
    void start_new_layer(float z);

    /**
     * @brief Trim whitespace and comments from line
     * @param line Raw line
     * @return Trimmed line
     */
    std::string trim_line(const std::string& line);

    // Parser state
    glm::vec3 current_position_{0.0f, 0.0f, 0.0f}; ///< Current XYZ position
    float current_e_{0.0f};                        ///< Current E (extruder) position
    std::string current_object_;         ///< Current object name (from EXCLUDE_OBJECT_START)
    bool is_absolute_positioning_{true}; ///< G90 (absolute) vs G91 (relative)
    bool is_absolute_extrusion_{true};   ///< M82 (absolute E) vs M83 (relative E)

    // Multi-color tool tracking
    int current_tool_index_{0};                   ///< Active extruder/tool (0-indexed)
    std::vector<std::string> tool_color_palette_; ///< Hex colors per tool: ["#ED1C24", ...]
    bool in_wipe_tower_{false};                   ///< True when inside wipe tower section

    // Accumulated data
    std::vector<Layer> layers_;                  ///< All parsed layers
    std::map<std::string, GCodeObject> objects_; ///< Object metadata
    AABB global_bounds_;                         ///< Global bounding box

    // Parsed metadata (transferred to ParsedGCodeFile on finalize())
    std::string metadata_slicer_name_;
    std::string metadata_filament_type_;
    std::string metadata_filament_color_;
    std::string metadata_printer_model_;
    float metadata_nozzle_diameter_{0.0f};
    float metadata_filament_length_{0.0f};
    float metadata_filament_weight_{0.0f};
    float metadata_filament_cost_{0.0f};
    float metadata_print_time_{0.0f};
    int metadata_layer_count_{0};

    // Extrusion width metadata
    float metadata_extrusion_width_{0.0f};
    float metadata_perimeter_extrusion_width_{0.0f};
    float metadata_infill_extrusion_width_{0.0f};
    float metadata_first_layer_extrusion_width_{0.0f};
    float metadata_filament_diameter_{1.75f}; ///< Filament diameter (default: 1.75mm)
    float metadata_layer_height_{0.2f};       ///< Layer height (default: 0.2mm)

    // Progress tracking
    size_t lines_parsed_{0};           ///< Line counter
    bool use_layer_markers_{false};    ///< True if ;LAYER_CHANGE markers found
    bool pending_layer_marker_{false}; ///< Layer change marker seen, layer not yet started

    // Warning counters (logged as summary in finalize() instead of per-segment)
    size_t out_of_range_width_count_{
        0}; ///< Count of segments with calculated width outside 0.1-2.0mm
};

// ============================================================================
// Thumbnail Extraction (Standalone Functions)
// ============================================================================

/**
 * @brief Thumbnail extracted from G-code file header
 *
 * G-code files embed thumbnails as base64-encoded PNG in comment blocks.
 * Multiple sizes may be present (e.g., 48x48 for printer LCD, 300x300 for web).
 */
struct GCodeThumbnail {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> png_data; ///< Decoded PNG binary data

    int pixel_count() const {
        return width * height;
    }
};

/**
 * @brief Extract all thumbnails from G-code file header
 *
 * Parses thumbnail blocks in the format:
 *   ; thumbnail begin WIDTHxHEIGHT SIZE
 *   ; <base64 data line 1>
 *   ; <base64 data line 2>
 *   ; ...
 *   ; thumbnail end
 *
 * @param filepath Path to the G-code file
 * @return Vector of thumbnails sorted largest-first. Empty if none found.
 */
std::vector<GCodeThumbnail> extract_thumbnails(const std::string& filepath);

/**
 * @brief Extract all thumbnails from G-code content string
 *
 * Same as extract_thumbnails() but works on string content instead of file.
 * Useful for processing downloaded gcode without writing to disk.
 *
 * @param content G-code content (typically first ~100KB of file header)
 * @return Vector of thumbnails sorted largest-first. Empty if none found.
 */
std::vector<GCodeThumbnail> extract_thumbnails_from_content(const std::string& content);

/**
 * @brief Get the largest thumbnail from a G-code file
 *
 * @param filepath Path to the G-code file
 * @return Largest thumbnail, or empty thumbnail if none found
 */
GCodeThumbnail get_best_thumbnail(const std::string& filepath);

/**
 * @brief Extract thumbnail and save to PNG file
 *
 * Extracts the largest thumbnail and writes raw PNG bytes to file.
 *
 * @param gcode_path Path to the G-code file
 * @param output_path Path for the output PNG file
 * @return true if successful, false if no thumbnail or write failed
 */
bool save_thumbnail_to_file(const std::string& gcode_path, const std::string& output_path);

/**
 * @brief Get or create cached thumbnail for a G-code file
 *
 * If cached thumbnail exists and is newer than G-code file, returns cache path.
 * Otherwise extracts thumbnail and saves to cache directory.
 *
 * @param gcode_path Path to the G-code file
 * @param cache_dir Directory for cached thumbnails
 * @return Path to cached PNG, or empty string if no thumbnail available
 */
std::string get_cached_thumbnail(const std::string& gcode_path, const std::string& cache_dir);

/**
 * @brief Decode base64 string to binary data
 *
 * @param encoded Base64 encoded string (may contain whitespace)
 * @return Decoded binary data
 */
std::vector<uint8_t> base64_decode(const std::string& encoded);

/**
 * @brief Basic metadata extracted from G-code header
 *
 * Lightweight struct for quick file listings without full toolpath parsing.
 */
struct GCodeHeaderMetadata {
    std::string filename;
    uint64_t file_size = 0;
    double modified_time = 0.0; ///< Unix timestamp
    std::string slicer;
    std::string slicer_version;
    double estimated_time_seconds = 0.0;
    double filament_used_mm = 0.0;
    double filament_used_g = 0.0;
    std::string filament_type; ///< e.g., "PLA", "PETG", "ABS", "TPU", "ASA"
    uint32_t layer_count = 0;
    double layer_height = 0.0;       ///< mm per layer (e.g. 0.2)
    double first_layer_height = 0.0; ///< mm first layer (e.g. 0.3)
    double object_height = 0.0;      ///< mm max Z height of the print
    double first_layer_bed_temp = 0.0;
    double first_layer_nozzle_temp = 0.0;
    std::vector<std::string> tool_colors; ///< Hex colors per tool (e.g., ["#ED1C24", "#00C1AE"])
};

/**
 * @brief Quick metadata extraction from G-code header only
 *
 * Extracts just the header metadata (slicer info, print time, filament)
 * without parsing the full toolpath. Much faster for file listings.
 *
 * @param filepath Path to the G-code file
 * @return GCodeHeaderMetadata with basic info populated
 */
GCodeHeaderMetadata extract_header_metadata(const std::string& filepath);

} // namespace gcode
} // namespace helix
