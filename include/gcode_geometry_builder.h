// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code Geometry Builder
// Converts parsed G-code toolpath segments into optimized 3D ribbon geometry
// with coordinate quantization and segment simplification.

#pragma once

#include "gcode_parser.h"

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace helix {
namespace gcode {

// ============================================================================
// Quantized Vertex Representation
// ============================================================================

/**
 * @brief 16-bit quantized vertex for memory efficiency
 *
 * Stores 3D coordinates as 16-bit signed integers instead of 32-bit floats.
 * Provides 4.6 micron resolution for 300mm build volume (far exceeds
 * typical printer precision of ~50 microns).
 *
 * Memory savings: 50% reduction (12 bytes → 6 bytes per vertex)
 */
struct QuantizedVertex {
    int16_t x; ///< X coordinate in quantized units
    int16_t y; ///< Y coordinate in quantized units
    int16_t z; ///< Z coordinate in quantized units
};

/**
 * @brief Quantization parameters for coordinate conversion
 */
struct QuantizationParams {
    glm::vec3 min_bounds; ///< Minimum XYZ of bounding box
    glm::vec3 max_bounds; ///< Maximum XYZ of bounding box
    float scale_factor;   ///< Units per quantized step

    /**
     * @brief Calculate scale factor from bounding box
     *
     * Determines optimal quantization to fit build volume into
     * 16-bit signed integer range (-32768 to +32767).
     */
    void calculate_scale(const AABB& bbox);

    /**
     * @brief Quantize floating-point coordinate to int16_t
     */
    int16_t quantize(float value, float min_bound) const;

    /**
     * @brief Dequantize int16_t back to floating-point
     */
    float dequantize(int16_t value, float min_bound) const;

    /**
     * @brief Quantize 3D vector
     */
    QuantizedVertex quantize_vec3(const glm::vec3& v) const;

    /**
     * @brief Dequantize to 3D vector
     */
    glm::vec3 dequantize_vec3(const QuantizedVertex& qv) const;
};

// ============================================================================
// Packed Vertex Layout (for GPU upload)
// ============================================================================

/**
 * @brief Interleaved vertex format for GPU upload: position(3f) + normal(3f) + color(3f)
 *
 * Centralizes the vertex attribute layout so that upload code (geometry builder)
 * and draw code (renderer) stay in sync. 36 bytes per vertex.
 */
struct PackedVertex {
    float position[3];
    float normal[3];
    float color[3];
    static constexpr size_t stride() {
        return sizeof(PackedVertex);
    }
    static constexpr size_t position_offset() {
        return offsetof(PackedVertex, position);
    }
    static constexpr size_t normal_offset() {
        return offsetof(PackedVertex, normal);
    }
    static constexpr size_t color_offset() {
        return offsetof(PackedVertex, color);
    }
};

// ============================================================================
// Ribbon Geometry
// ============================================================================

/**
 * @brief Single ribbon segment (flat quad, 4 vertices, 2 triangles)
 *
 * Represents one extruded line segment as a flat rectangular ribbon
 * oriented horizontally (parallel to build plate). Each ribbon has:
 * - 4 vertices (bottom-left, bottom-right, top-left, top-right)
 * - 2 triangles sharing the diagonal edge
 * - Horizontal normal (0, 0, 1) for lighting
 *
 * Uses palette indices for normals and colors to reduce memory (9 bytes per vertex)
 */
struct RibbonVertex {
    QuantizedVertex position; ///< Quantized 3D position (6 bytes)
    uint16_t normal_index;    ///< Index into normal palette (2 bytes, supports 65536 normals)
    uint8_t color_index;      ///< Index into color palette (1 byte)
};

/**
 * @brief Triangle indices (uses vertex sharing between adjacent ribbons)
 * Uses uint32_t to support large models (>65k vertices)
 */
using TriangleIndices = std::array<uint32_t, 3>;

/**
 * @brief Triangle strip (4 indices for rectangular face: 2 triangles)
 * Order: [bottom-left, bottom-right, top-left, top-right]
 * Renders as: Triangle 1 (BL-BR-TL), Triangle 2 (BR-TL-TR) with strip winding
 */
using TriangleStrip = std::array<uint32_t, 4>;

// ============================================================================
// Palette Cache Types
// ============================================================================

/**
 * @brief Hash function for quantized normals (use in unordered_map)
 */
struct Vec3Hash {
    std::size_t operator()(const glm::vec3& v) const {
        // Quantize to grid for hashing (same as QUANT_STEP = 0.001)
        int32_t x = static_cast<int32_t>(std::round(v.x * 1000.0f));
        int32_t y = static_cast<int32_t>(std::round(v.y * 1000.0f));
        int32_t z = static_cast<int32_t>(std::round(v.z * 1000.0f));

        // Combine hashes (boost::hash_combine pattern)
        std::size_t h = 0;
        h ^= std::hash<int32_t>{}(x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>{}(y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>{}(z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/**
 * @brief Equality operator for quantized normals (needed for unordered_map)
 */
struct Vec3Equal {
    bool operator()(const glm::vec3& a, const glm::vec3& b) const {
        constexpr float EPSILON = 0.0001f;
        return glm::length(a - b) < EPSILON;
    }
};

/// Type alias for normal palette cache (O(1) lookup)
using NormalCache = std::unordered_map<glm::vec3, uint16_t, Vec3Hash, Vec3Equal>;

/// Type alias for color palette cache (O(1) lookup)
using ColorCache = std::unordered_map<uint32_t, uint8_t>;

/**
 * @brief Complete ribbon geometry for rendering
 */
struct RibbonGeometry {
    std::vector<RibbonVertex> vertices;   ///< Vertex buffer (indexed)
    std::vector<TriangleIndices> indices; ///< Index buffer (triangles) - DEPRECATED, use strips
    std::vector<TriangleStrip> strips;    ///< Index buffer (triangle strips) - OPTIMIZED

    // Palette-based compression (normals and colors stored once, indexed from vertices)
    std::vector<glm::vec3> normal_palette; ///< Unique normals (max 256)
    std::vector<uint32_t> color_palette;   ///< Unique colors in RGB format (max 256)

    /// Maps tool_index → color_palette index. Allows recoloring VBOs by tool
    /// (e.g., AMS slot colors) without rebuilding geometry.
    std::unordered_map<uint8_t, uint8_t> tool_palette_map;

    // Layer tracking for two-pass ghost layer rendering
    std::vector<uint16_t> strip_layer_index; ///< Layer index per strip (parallel to strips vector)
    /// Layer strip ranges: [layer_idx] -> (first_strip_idx, strip_count)
    std::vector<std::pair<size_t, size_t>> layer_strip_ranges;
    uint16_t max_layer_index{0}; ///< Maximum layer index in geometry

    // Per-layer bounding boxes for frustum culling (indexed by layer)
    std::vector<AABB> layer_bboxes; ///< AABB per layer for frustum culling

    // Palette lookup caches (O(1) lookup instead of O(N) linear search)
    std::unique_ptr<NormalCache> normal_cache; ///< Cache for normal palette lookups
    std::unique_ptr<ColorCache> color_cache;   ///< Cache for color palette lookups

    size_t extrusion_triangle_count; ///< Triangles for extrusion moves
    size_t travel_triangle_count;    ///< Triangles for travel moves
    QuantizationParams quantization; ///< Quantization params for dequantization
    float layer_height_mm{0.2f};     ///< Layer height for Z-offset calculations during LOD

    /**
     * @brief Calculate total memory usage in bytes
     */
    size_t memory_usage() const {
        return vertices.size() * sizeof(RibbonVertex) + indices.size() * sizeof(TriangleIndices) +
               strips.size() * sizeof(TriangleStrip) + normal_palette.size() * sizeof(glm::vec3) +
               color_palette.size() * sizeof(uint32_t) +
               strip_layer_index.size() * sizeof(uint16_t) +
               layer_strip_ranges.size() * sizeof(std::pair<size_t, size_t>) +
               layer_bboxes.size() * sizeof(AABB);
    }

    /// Pre-computed interleaved vertex buffers for GPU upload (position+normal+color floats).
    /// Prepared on background thread to avoid blocking UI during VBO upload.
    struct PreparedLayerBuffer {
        std::vector<float> data;
        size_t vertex_count{0};
    };
    std::vector<PreparedLayerBuffer> prepared_buffers;

    /**
     * @brief Pre-compute interleaved vertex buffers for GPU upload.
     *
     * Call from background thread after build(). Expands strips into
     * position(3f)+normal(3f)+color(3f) interleaved format per layer.
     * The renderer can then upload directly to VBOs without CPU work.
     */
    void prepare_interleaved_buffers();

    /**
     * @brief Clear all geometry data
     */
    void clear();

    /**
     * @brief Validate geometry integrity (vertex data, layer ranges, palette indices)
     *
     * Spot-checks vertex positions for NaN/Inf, verifies layer strip ranges are
     * within bounds, and checks color palette indices. Logs warnings for any
     * issues found.
     */
    void validate() const;

    /**
     * @brief Destructor - clean up cache pointers
     */
    ~RibbonGeometry();

    // Copy/move operations need special handling for cache pointers
    RibbonGeometry();
    RibbonGeometry(const RibbonGeometry&) = delete;
    RibbonGeometry& operator=(const RibbonGeometry&) = delete;
    RibbonGeometry(RibbonGeometry&& other) noexcept;
    RibbonGeometry& operator=(RibbonGeometry&& other) noexcept;
};

// ============================================================================
// Simplification Options
// ============================================================================

/**
 * @brief Segment simplification configuration
 */
struct SimplificationOptions {
    bool enable_merging = true; ///< Enable collinear segment merging
    float tolerance_mm = 0.01f; ///< Merge tolerance (mm) - only merge truly collinear segments
    float min_segment_length_mm = 0.01f; ///< Minimum segment length to keep (filter micro-segments)
    float max_direction_change_deg = 15.0f; ///< Max angle (degrees) between segments to allow merge

    /**
     * @brief Validate and clamp tolerance to safe range
     *
     * Max tolerance of 5.0mm allows very aggressive simplification for LOD
     * during interaction. For final quality rendering, use 0.01mm or less.
     */
    void validate() {
        tolerance_mm = std::max(0.001f, std::min(5.0f, tolerance_mm));
        min_segment_length_mm = std::max(0.0001f, min_segment_length_mm);
        max_direction_change_deg = std::max(1.0f, std::min(90.0f, max_direction_change_deg));
    }
};

// ============================================================================
// Geometry Builder
// ============================================================================

/**
 * @brief Converts G-code toolpath segments into optimized 3D ribbon geometry
 *
 * Pipeline:
 * 1. Analyze bounding box and compute quantization parameters
 * 2. Simplify segments (merge collinear lines within tolerance)
 * 3. Generate ribbon geometry (quads from line segments)
 * 4. Assign colors (Z-height gradient or custom)
 * 5. Compute surface normals (horizontal for flat ribbons)
 * 6. Index vertices (share vertices between adjacent segments)
 */
class GeometryBuilder {
  public:
    // Default filament color (OrcaSlicer teal) - used when G-code doesn't specify color
    static constexpr const char* DEFAULT_FILAMENT_COLOR = "#26A69A";

    GeometryBuilder();

    /**
     * @brief Build ribbon geometry from parsed G-code
     *
     * @param gcode Parsed G-code file with toolpath segments
     * @param options Simplification configuration
     * @return Optimized ribbon geometry ready for 3D rendering
     */
    RibbonGeometry build(const ParsedGCodeFile& gcode, const SimplificationOptions& options);

    /**
     * @brief Get statistics about last build operation
     */
    struct BuildStats {
        size_t input_segments;      ///< Original segment count
        size_t output_segments;     ///< Simplified segment count
        size_t vertices_generated;  ///< Total vertices
        size_t triangles_generated; ///< Total triangles
        size_t memory_bytes;        ///< Total memory used
        float simplification_ratio; ///< Segments removed (0.0 - 1.0)

        void log() const; ///< Log statistics via spdlog
    };

    const BuildStats& last_stats() const {
        return stats_;
    }

    /**
     * @brief Set ribbon width for extrusion moves (default: 0.42mm)
     */
    void set_extrusion_width(float width_mm) {
        extrusion_width_mm_ = width_mm;
    }

    /**
     * @brief Set ribbon width for travel moves (default: 0.1mm)
     */
    void set_travel_width(float width_mm) {
        travel_width_mm_ = width_mm;
    }

    /**
     * @brief Enable/disable Z-height color gradient
     */
    void set_use_height_gradient(bool enable) {
        use_height_gradient_ = enable;
    }

    /**
     * @brief Set solid filament color (disables height gradient)
     * @param hex_color Color in hex format (e.g., "#26A69A" or "26A69A")
     */
    void set_filament_color(const std::string& hex_color);

    /**
     * @brief Enable/disable smooth shading (Gouraud)
     * @param enable true for smooth shading (averaged normals), false for flat shading (per-face
     * normals)
     */
    void set_smooth_shading(bool enable) {
        use_smooth_shading_ = enable;
    }

    /**
     * @brief Set layer height for tube geometry (default: 0.2mm)
     * @param height_mm Layer height in millimeters
     *
     * Controls the vertical dimension of extruded tubes. Should match the actual
     * layer height from the G-code for accurate proportions.
     */
    void set_layer_height(float height_mm) {
        layer_height_mm_ = height_mm;
    }

    /**
     * @brief Set highlighted object names for visual emphasis
     * @param object_names Names of objects to highlight (empty to clear)
     *
     * Highlighted segments will be rendered with brightened color (1.8x multiplier)
     * to make them stand out from the rest of the model.
     */
    void set_highlighted_objects(const std::unordered_set<std::string>& object_names) {
        highlighted_objects_ = object_names;
    }

    /**
     * @brief Enable/disable per-face debug coloring
     * @param enable true to assign distinct colors to each face for debugging
     *
     * When enabled, renders each face of the tube in a different bright color:
     * - Top face: Red (#FF0000)
     * - Bottom face: Blue (#0000FF)
     * - Left face: Green (#00FF00)
     * - Right face: Yellow (#FFFF00)
     * - Start end cap: Magenta (#FF00FF)
     * - End end cap: Cyan (#00FFFF)
     *
     * This overrides normal color computation and is useful for debugging
     * face orientation, winding order, and geometry issues.
     */
    void set_debug_face_colors(bool enable) {
        debug_face_colors_ = enable;
    }

    /// Set tube_sides override from budget manager (0 = use config default)
    void set_budget_tube_sides(int sides) {
        budget_tube_sides_ = sides;
    }

    /// Set memory ceiling for progressive budget checking (0 = unlimited)
    void set_budget_limit(size_t bytes) {
        budget_limit_bytes_ = bytes;
    }

    /// Whether the last build was aborted due to budget exceeded
    bool was_budget_exceeded() const {
        return budget_exceeded_;
    }

    /**
     * @brief Set tool color palette for multi-color prints
     * @param palette Vector of hex color strings (e.g., ["#ED1C24", "#00C1AE"])
     *
     * When set, colors will be assigned based on segment tool_index instead of
     * Z-height gradient. Empty palette disables multi-color mode.
     */
    void set_tool_color_palette(const std::vector<std::string>& palette) {
        tool_color_palette_ = palette;
    }

  private:
    // Palette management
    uint16_t add_to_normal_palette(RibbonGeometry& geometry, const glm::vec3& normal);
    uint8_t add_to_color_palette(RibbonGeometry& geometry, uint32_t color_rgb);

    // Simplification pipeline
    std::vector<ToolpathSegment> simplify_segments(const std::vector<ToolpathSegment>& segments,
                                                   const SimplificationOptions& options);

    bool are_collinear(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                       float tolerance) const;

    // Tube cross-section vertex indices (4 vertices: one per face)
    // OrcaSlicer approach - Order: [up, right, down, left]
    using TubeCap = std::vector<uint32_t>; // Size determined at runtime (4, 8, or 16)

    // Geometry generation with vertex sharing (OrcaSlicer approach)
    // prev_start_cap: Optional 4 vertex indices from previous segment's end cap (for reuse)
    // Returns: 4 vertex indices of this segment's end cap (for next segment to reuse)
    TubeCap generate_ribbon_vertices(const ToolpathSegment& segment, RibbonGeometry& geometry,
                                     const QuantizationParams& quant,
                                     std::optional<TubeCap> prev_start_cap = std::nullopt);

    glm::vec3 compute_perpendicular(const glm::vec3& direction, float width) const;

    // Color assignment
    uint32_t compute_color_rgb(float z_height, float z_min, float z_max) const;

    /**
     * @brief Parse hex color string to RGB integer
     * @param hex_color Hex color string (e.g., "#ED1C24" or "ED1C24")
     * @return RGB color as 32-bit integer (0xRRGGBB), or 0x808080 (gray) if invalid
     */
    uint32_t parse_hex_color(const std::string& hex_color) const;

    /**
     * @brief Compute color for a segment with multi-color support
     * @param segment Toolpath segment with tool_index
     * @param z_min Minimum Z height of model
     * @param z_max Maximum Z height of model
     * @return RGB color as 32-bit integer (0xRRGGBB)
     *
     * Priority:
     * 1. Tool-specific color from palette (if tool_index valid and palette not empty)
     * 2. Z-height gradient (if use_height_gradient_ enabled)
     * 3. Default filament color
     */
    uint32_t compute_segment_color(const ToolpathSegment& segment, float z_min, float z_max) const;

    // Configuration
    float extrusion_width_mm_ = 0.42f; ///< Default for 0.4mm nozzle
    float travel_width_mm_ = 0.1f;     ///< Thin for travels
    float layer_height_mm_ = 0.2f;     ///< Layer height for tube vertical dimension
    bool use_height_gradient_ = true;  ///< Rainbow Z-gradient
    bool use_smooth_shading_ = false;  ///< Smooth (Gouraud) vs flat shading
    uint8_t filament_r_ = 0x26;        ///< Filament color red component
    uint8_t filament_g_ = 0xA6;        ///< Filament color green component
    uint8_t filament_b_ = 0x9A;        ///< Filament color blue component
    std::unordered_set<std::string>
        highlighted_objects_;                     ///< Object names to highlight (empty = none)
    bool debug_face_colors_ = false;              ///< Enable per-face debug coloring
    std::vector<std::string> tool_color_palette_; ///< Hex colors per tool (multi-color prints)
    int tube_sides_ = 16;                         ///< Tube cross-section sides (valid: 4, 8, 16)

    int budget_tube_sides_ = 0;     ///< Override tube_sides from budget (0 = use config)
    size_t budget_limit_bytes_ = 0; ///< Memory ceiling (0 = unlimited)
    bool budget_exceeded_ = false;  ///< Set to true if build aborted due to budget

    // Build statistics
    BuildStats stats_;
    QuantizationParams quant_params_;
};

} // namespace gcode
} // namespace helix
