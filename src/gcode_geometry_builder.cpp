// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// G-Code Geometry Builder Implementation

#include "gcode_geometry_builder.h"
#include "config.h"

#include <spdlog/spdlog.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>
#include <limits>
#include <unordered_map>
#include <chrono>

namespace gcode {

// ============================================================================
// Hash Functions for Palette Caches
// ============================================================================

// Hash function for quantized normals (use in unordered_map)
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

// Equality operator for quantized normals (needed for unordered_map)
struct Vec3Equal {
    bool operator()(const glm::vec3& a, const glm::vec3& b) const {
        constexpr float EPSILON = 0.0001f;
        return glm::length(a - b) < EPSILON;
    }
};

// ============================================================================
// Debug Face Colors
// ============================================================================

namespace DebugColors {
constexpr uint32_t TOP = 0xFF0000;       // Bright Red
constexpr uint32_t BOTTOM = 0x0000FF;    // Bright Blue
constexpr uint32_t LEFT = 0x00FF00;      // Bright Green
constexpr uint32_t RIGHT = 0xFFFF00;     // Bright Yellow
constexpr uint32_t START_CAP = 0xFF00FF; // Bright Magenta
constexpr uint32_t END_CAP = 0x00FFFF;   // Bright Cyan
} // namespace DebugColors

// ============================================================================
// QuantizationParams Implementation
// ============================================================================

void QuantizationParams::calculate_scale(const AABB& bbox) {
    min_bounds = bbox.min;
    max_bounds = bbox.max;

    // Calculate maximum dimension to determine scale factor
    glm::vec3 extents = max_bounds - min_bounds;
    float max_extent = std::max({extents.x, extents.y, extents.z});

    // 16-bit signed int range: -32768 to +32767
    // Quantization formula: (value - min_bound) * scale
    // Maximum quantized value = (max_bound - min_bound) * scale = extent * scale
    // Constraint: extent * scale <= 32767
    // Reserve 10% headroom to avoid edge cases
    constexpr float INT16_MAX_WITH_HEADROOM = 32767.0f * 0.9f;

    if (max_extent > 0.0f) {
        scale_factor = INT16_MAX_WITH_HEADROOM / max_extent;
    } else {
        // Fallback for degenerate bounding box
        scale_factor = 1000.0f; // 1 unit = 1mm
    }

    spdlog::debug("Quantization: bounds=[{:.2f},{:.2f},{:.2f}] to [{:.2f},{:.2f},{:.2f}], "
                  "scale={:.2f} units/mm, resolution={:.4f}mm",
                  min_bounds.x, min_bounds.y, min_bounds.z, max_bounds.x, max_bounds.y,
                  max_bounds.z, scale_factor, 1.0f / scale_factor);
}

int16_t QuantizationParams::quantize(float value, float min_bound) const {
    float normalized = (value - min_bound) * scale_factor;

    // Clamp to int16 range to prevent overflow
    normalized = std::max(-32768.0f, std::min(32767.0f, normalized));

    return static_cast<int16_t>(std::round(normalized));
}

float QuantizationParams::dequantize(int16_t value, float min_bound) const {
    return static_cast<float>(value) / scale_factor + min_bound;
}

QuantizedVertex QuantizationParams::quantize_vec3(const glm::vec3& v) const {
    return QuantizedVertex{quantize(v.x, min_bounds.x), quantize(v.y, min_bounds.y),
                           quantize(v.z, min_bounds.z)};
}

glm::vec3 QuantizationParams::dequantize_vec3(const QuantizedVertex& qv) const {
    return glm::vec3(dequantize(qv.x, min_bounds.x), dequantize(qv.y, min_bounds.y),
                     dequantize(qv.z, min_bounds.z));
}

// ============================================================================
// RibbonGeometry Implementation
// ============================================================================

// Type aliases for cache maps
using NormalCache = std::unordered_map<glm::vec3, uint16_t, Vec3Hash, Vec3Equal>;
using ColorCache = std::unordered_map<uint32_t, uint8_t>;

RibbonGeometry::RibbonGeometry() {
    // Initialize caches
    normal_cache_ptr = new NormalCache();
    color_cache_ptr = new ColorCache();
    extrusion_triangle_count = 0;
    travel_triangle_count = 0;
}

RibbonGeometry::~RibbonGeometry() {
    // Clean up cache pointers
    delete static_cast<NormalCache*>(normal_cache_ptr);
    delete static_cast<ColorCache*>(color_cache_ptr);
}

RibbonGeometry::RibbonGeometry(RibbonGeometry&& other) noexcept
    : vertices(std::move(other.vertices)), indices(std::move(other.indices)),
      strips(std::move(other.strips)), normal_palette(std::move(other.normal_palette)),
      color_palette(std::move(other.color_palette)), normal_cache_ptr(other.normal_cache_ptr),
      color_cache_ptr(other.color_cache_ptr),
      extrusion_triangle_count(other.extrusion_triangle_count),
      travel_triangle_count(other.travel_triangle_count), quantization(other.quantization) {
    // Prevent double-delete
    other.normal_cache_ptr = nullptr;
    other.color_cache_ptr = nullptr;
}

RibbonGeometry& RibbonGeometry::operator=(RibbonGeometry&& other) noexcept {
    if (this != &other) {
        // Clean up existing caches
        delete static_cast<NormalCache*>(normal_cache_ptr);
        delete static_cast<ColorCache*>(color_cache_ptr);

        // Move data
        vertices = std::move(other.vertices);
        indices = std::move(other.indices);
        strips = std::move(other.strips);
        normal_palette = std::move(other.normal_palette);
        color_palette = std::move(other.color_palette);
        normal_cache_ptr = other.normal_cache_ptr;
        color_cache_ptr = other.color_cache_ptr;
        extrusion_triangle_count = other.extrusion_triangle_count;
        travel_triangle_count = other.travel_triangle_count;
        quantization = other.quantization;

        // Prevent double-delete
        other.normal_cache_ptr = nullptr;
        other.color_cache_ptr = nullptr;
    }
    return *this;
}

void RibbonGeometry::clear() {
    vertices.clear();
    indices.clear();
    strips.clear();
    normal_palette.clear();
    color_palette.clear();

    // Clear caches
    if (normal_cache_ptr) {
        static_cast<NormalCache*>(normal_cache_ptr)->clear();
    }
    if (color_cache_ptr) {
        static_cast<ColorCache*>(color_cache_ptr)->clear();
    }

    extrusion_triangle_count = 0;
    travel_triangle_count = 0;
}

// ============================================================================
// BuildStats Implementation
// ============================================================================

void GeometryBuilder::BuildStats::log() const {
    spdlog::info("Geometry Build Statistics:");
    spdlog::info("  Input segments:      {:>8}", input_segments);
    spdlog::info("  Simplified segments: {:>8} ({:.1f}% reduction)", output_segments,
                 simplification_ratio * 100.0f);
    spdlog::info("  Vertices generated:  {:>8}", vertices_generated);
    spdlog::info("  Triangles generated: {:>8}", triangles_generated);
    spdlog::info("  Memory usage:        {:>8} KB ({:.2f} MB)", memory_bytes / 1024,
                 memory_bytes / (1024.0 * 1024.0));

    if (input_segments > 0) {
        float bytes_per_segment = static_cast<float>(memory_bytes) / input_segments;
        spdlog::info("  Bytes per segment:   {:.1f}", bytes_per_segment);
    }
}

// ============================================================================
// GeometryBuilder Implementation
// ============================================================================

GeometryBuilder::GeometryBuilder() {
    stats_ = {};
}

// ============================================================================
// Palette Management
// ============================================================================

uint16_t GeometryBuilder::add_to_normal_palette(RibbonGeometry& geometry, const glm::vec3& normal) {
    // Very light quantization (0.001) to merge nearly-identical normals without visible banding
    constexpr float QUANT_STEP = 0.001f;
    glm::vec3 quantized;
    quantized.x = std::round(normal.x / QUANT_STEP) * QUANT_STEP;
    quantized.y = std::round(normal.y / QUANT_STEP) * QUANT_STEP;
    quantized.z = std::round(normal.z / QUANT_STEP) * QUANT_STEP;

    // Renormalize to ensure unit vector
    float length = glm::length(quantized);
    if (length > 0.0001f) {
        quantized /= length;
    } else {
        quantized = normal; // Fallback if quantization created zero vector
    }

    // Check cache first (O(1) lookup)
    auto* cache = static_cast<NormalCache*>(geometry.normal_cache_ptr);
    auto it = cache->find(quantized);
    if (it != cache->end()) {
        return it->second;  // Cache hit!
    }

    // Not in cache - add to palette
    if (geometry.normal_palette.size() >= 65536) {
        spdlog::warn("Normal palette full (65536 entries), reusing last entry");
        return 65535;
    }

    uint16_t index = static_cast<uint16_t>(geometry.normal_palette.size());
    geometry.normal_palette.push_back(quantized);
    (*cache)[quantized] = index;  // Add to cache

    // Log palette size periodically
    if (geometry.normal_palette.size() % 1000 == 0) {
        spdlog::debug("Normal palette: {} entries", geometry.normal_palette.size());
    }

    return index;
}

uint8_t GeometryBuilder::add_to_color_palette(RibbonGeometry& geometry, uint32_t color_rgb) {
    // Check cache first (O(1) lookup)
    auto* cache = static_cast<ColorCache*>(geometry.color_cache_ptr);
    auto it = cache->find(color_rgb);
    if (it != cache->end()) {
        return it->second;  // Cache hit!
    }

    // Not in cache - add to palette
    if (geometry.color_palette.size() >= 256) {
        spdlog::warn("Color palette full (256 entries), reusing last entry");
        return 255;
    }

    uint8_t index = static_cast<uint8_t>(geometry.color_palette.size());
    geometry.color_palette.push_back(color_rgb);
    (*cache)[color_rgb] = index;  // Add to cache

    return index;
}

RibbonGeometry GeometryBuilder::build(const ParsedGCodeFile& gcode,
                                      const SimplificationOptions& options) {
    // Start timing
    auto build_start = std::chrono::high_resolution_clock::now();

    RibbonGeometry geometry;
    stats_ = {}; // Reset statistics

    // Validate and apply options
    SimplificationOptions validated_opts = options;
    validated_opts.validate();

    spdlog::info("Building G-code geometry (tolerance={:.3f}mm, merging={})",
                 validated_opts.tolerance_mm, validated_opts.enable_merging);

    // Calculate quantization parameters from bounding box
    // IMPORTANT: Expand bounds to account for tube width (vertices extend beyond segment positions)
    // Use sqrt(2) safety factor because rectangular tubes on diagonal segments can expand
    // in multiple dimensions simultaneously (e.g., perp_horizontal + perp_vertical)
    float max_tube_width = std::max(extrusion_width_mm_, travel_width_mm_);
    float expansion_margin = max_tube_width * 1.5f; // Safety factor for diagonal expansion
    AABB expanded_bbox = gcode.global_bounding_box;
    expanded_bbox.min -= glm::vec3(expansion_margin, expansion_margin, expansion_margin);
    expanded_bbox.max += glm::vec3(expansion_margin, expansion_margin, expansion_margin);
    quant_params_.calculate_scale(expanded_bbox);

    spdlog::debug("Expanded quantization bounds by {:.1f}mm for tube width {:.1f}mm",
                  expansion_margin, max_tube_width);

    // Collect all segments from all layers
    std::vector<ToolpathSegment> all_segments;
    for (const auto& layer : gcode.layers) {
        all_segments.insert(all_segments.end(), layer.segments.begin(), layer.segments.end());
    }

    stats_.input_segments = all_segments.size();
    spdlog::debug("Collected {} total segments from {} layers", all_segments.size(),
                  gcode.layers.size());

    // Step 1: Simplify segments (merge collinear lines)
    std::vector<ToolpathSegment> simplified;
    if (validated_opts.enable_merging) {
        simplified = simplify_segments(all_segments, validated_opts);
        stats_.output_segments = simplified.size();
        stats_.simplification_ratio =
            1.0f - (static_cast<float>(simplified.size()) / all_segments.size());

        spdlog::info("Simplified {} → {} segments ({:.1f}% reduction)", all_segments.size(),
                     simplified.size(), stats_.simplification_ratio * 100.0f);
    } else {
        simplified = all_segments;
        stats_.output_segments = simplified.size();
        stats_.simplification_ratio = 0.0f;
        spdlog::info("Using RAW segments (simplification DISABLED): {} segments",
                     simplified.size());
    }

    // Find the maximum Z height (top layer) dynamically for debug filtering
    float max_z = -std::numeric_limits<float>::infinity();
    for (const auto& segment : simplified) {
        float z = std::round(segment.start.z * 100.0f) / 100.0f;
        if (z > max_z)
            max_z = z;
    }

    // Step 2: Generate ribbon geometry with vertex sharing
    // Track previous segment end vertices for reuse
    std::optional<TubeCap> prev_end_cap;
    glm::vec3 prev_end_pos{0.0f};

    // DEBUG: Track segment Y range
    float seg_y_min = FLT_MAX, seg_y_max = -FLT_MAX;
    size_t segments_skipped = 0;
    for (size_t i = 0; i < simplified.size(); ++i) {
        const auto& segment = simplified[i];

        // Skip degenerate segments (zero length)
        float segment_length = glm::distance(segment.start, segment.end);
        if (segment_length < 0.0001f) {
            segments_skipped++;
            continue;
        }

        // Skip travel moves (non-extrusion moves)
        // TODO: Make this configurable if we want to visualize travel paths
        if (!segment.is_extrusion) {
            segments_skipped++;
            continue;
        }

        // Track Y range
        seg_y_min = std::min({seg_y_min, segment.start.y, segment.end.y});
        seg_y_max = std::max({seg_y_max, segment.start.y, segment.end.y});

        // Check if we can share vertices with previous segment (OPTIMIZATION ENABLED!)
        bool can_share = false;
        float dist = 0.0f;
        float connection_tolerance = 0.0f;
        if (prev_end_cap.has_value()) {
            // Segments must connect spatially (within epsilon) and be same type
            dist = glm::distance(segment.start, prev_end_pos);
            // Use width-based tolerance: if gap is less than extrusion width, consider them
            // connected
            connection_tolerance = segment.width * 1.5f; //  50% overlap tolerance
            can_share = (dist < connection_tolerance) &&
                        (segment.is_extrusion == simplified[i - 1].is_extrusion);

            // Debug top layer connections
            float z = std::round(segment.start.z * 100.0f) / 100.0f;
            if (z == max_z) {
                spdlog::trace(
                    "  Seg {:3d}: dist={:.4f}mm, tol={:.4f}mm, width={:.4f}mm, can_share={}", i,
                    dist, connection_tolerance, segment.width, can_share);
            }
        }

        // Generate geometry, reusing previous end cap if segments connect
        TubeCap end_cap = generate_ribbon_vertices(segment, geometry, quant_params_,
                                                   can_share ? prev_end_cap : std::nullopt);

        // Store for next iteration
        prev_end_cap = end_cap;
        prev_end_pos = segment.end;
    }

    spdlog::trace("Segment Y range: [{:.1f}, {:.1f}]", seg_y_min, seg_y_max);

    // Categorize segments in top layer by angle and type (max_z already calculated above)
    size_t total_segs = 0, extrusion_segs = 0, travel_segs = 0;
    size_t diagonal_45_segs = 0, horizontal_segs = 0, vertical_segs = 0, other_angle_segs = 0;

    for (const auto& segment : simplified) {
        float z = std::round(segment.start.z * 100.0f) / 100.0f;
        if (std::abs(z - max_z) < 0.01f) {
            total_segs++;

            // Categorize by extrusion vs travel
            if (segment.is_extrusion) {
                extrusion_segs++;
            } else {
                travel_segs++;
            }

            // Calculate segment angle in XY plane
            glm::vec2 delta(segment.end.x - segment.start.x, segment.end.y - segment.start.y);
            float length_2d = glm::length(delta);

            if (length_2d > 0.01f) { // Skip near-zero length segments
                float angle_rad = std::atan2(delta.y, delta.x);
                float angle_deg = glm::degrees(angle_rad);

                // Normalize angle to [0, 180) for direction-independent classification
                if (angle_deg < 0)
                    angle_deg += 180.0f;

                // Categorize by angle (±5° tolerance)
                if (std::abs(angle_deg - 45.0f) < 5.0f || std::abs(angle_deg - 135.0f) < 5.0f) {
                    diagonal_45_segs++;
                } else if (std::abs(angle_deg - 0.0f) < 5.0f ||
                           std::abs(angle_deg - 180.0f) < 5.0f) {
                    horizontal_segs++;
                } else if (std::abs(angle_deg - 90.0f) < 5.0f) {
                    vertical_segs++;
                } else {
                    other_angle_segs++;
                }
            }
        }
    }

    if (total_segs > 0) {
        spdlog::debug("Top layer Z={:.2f}mm: {} segments ({} extrusion, {} travel)",
                      max_z, total_segs, extrusion_segs, travel_segs);
    }

    // Store quantization parameters for dequantization during rendering
    geometry.quantization = quant_params_;

    // Update final statistics
    stats_.vertices_generated = geometry.vertices.size();
    stats_.triangles_generated = geometry.indices.size();
    stats_.memory_bytes = geometry.memory_usage();

    // Log palette statistics
    spdlog::info("Palette stats: {} normals, {} colors (smooth_shading={})",
                 geometry.normal_palette.size(), geometry.color_palette.size(),
                 use_smooth_shading_);

    // Log cache statistics
    auto* normal_cache = static_cast<NormalCache*>(geometry.normal_cache_ptr);
    auto* color_cache = static_cast<ColorCache*>(geometry.color_cache_ptr);
    spdlog::debug("Cache stats: normal_cache={} entries, color_cache={} entries",
                 normal_cache->size(), color_cache->size());

    if (segments_skipped > 0) {
        spdlog::warn("Skipped {} degenerate segments (zero length)", segments_skipped);
    }

    stats_.log();

    // End timing
    auto build_end = std::chrono::high_resolution_clock::now();
    auto build_duration = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start);
    spdlog::info("Geometry build completed in {:.3f} seconds", build_duration.count() / 1000.0);

    return geometry;
}

// ============================================================================
// Segment Simplification
// ============================================================================

std::vector<ToolpathSegment>
GeometryBuilder::simplify_segments(const std::vector<ToolpathSegment>& segments,
                                   const SimplificationOptions& options) {
    if (segments.empty()) {
        return {};
    }

    std::vector<ToolpathSegment> simplified;
    simplified.reserve(segments.size()); // Upper bound

    // Start with first segment
    ToolpathSegment current = segments[0];

    for (size_t i = 1; i < segments.size(); ++i) {
        const auto& next = segments[i];

        // Can only merge segments if:
        // 1. Same move type (both extrusion or both travel)
        // 2. Endpoints connect (current.end ≈ next.start)
        // 3. Same object (for per-object highlighting)
        // 4. Collinear within tolerance

        bool same_type = (current.is_extrusion == next.is_extrusion);
        bool endpoints_connect = glm::distance2(current.end, next.start) < 0.0001f;
        bool same_object = (current.object_name == next.object_name);

        if (same_type && endpoints_connect && same_object) {
            // Check if current.start, current.end, next.end are collinear
            bool collinear =
                are_collinear(current.start, current.end, next.end, options.tolerance_mm);

            if (collinear) {
                // Merge: extend current segment to end at next.end
                current.end = next.end;
                current.extrusion_amount += next.extrusion_amount;
                continue; // Skip adding next to simplified list
            }
        }

        // Cannot merge - save current and start new segment
        simplified.push_back(current);
        current = next;
    }

    // Add final segment
    simplified.push_back(current);

    return simplified;
}

bool GeometryBuilder::are_collinear(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                    float tolerance) const {
    // Vector from p1 to p2
    glm::vec3 v1 = p2 - p1;

    // Vector from p1 to p3
    glm::vec3 v2 = p3 - p1;

    // If either vector is nearly zero-length, points are effectively same point
    float len1_sq = glm::length2(v1);
    float len2_sq = glm::length2(v2);

    if (len1_sq < 1e-8f || len2_sq < 1e-8f) {
        return true; // Degenerate case - treat as collinear
    }

    // Cross product gives vector perpendicular to both v1 and v2
    // If v1 and v2 are collinear, cross product magnitude will be zero
    glm::vec3 cross = glm::cross(v1, v2);
    float cross_mag = glm::length(cross);

    // Distance from p3 to line defined by p1-p2 is:
    // distance = |cross(v1, v2)| / |v1|
    float distance = cross_mag / std::sqrt(len1_sq);

    return distance <= tolerance;
}

// ============================================================================
// Ribbon Geometry Generation
// ============================================================================

GeometryBuilder::TubeCap
GeometryBuilder::generate_ribbon_vertices(const ToolpathSegment& segment, RibbonGeometry& geometry,
                                          const QuantizationParams& quant,
                                          std::optional<TubeCap> prev_start_cap) {

    // Read tube cross-section configuration
    static int tube_sides = -1;  // Cache config value (read once)
    if (tube_sides == -1) {
        tube_sides = Config::get_instance()->get<int>("/gcode_viewer/tube_sides", 16);

        // Validate: only 4, 8, or 16 sides supported
        if (tube_sides != 4 && tube_sides != 8 && tube_sides != 16) {
            spdlog::warn("Invalid tube_sides={} (must be 4, 8, or 16), defaulting to 16", tube_sides);
            tube_sides = 16;
        }

        spdlog::info("G-code tube geometry: N={} sides (elliptical cross-section) [NOTE: Only N=4 currently supported, N=8/16 coming soon]", tube_sides);
    }

    // Phase 2: Force N=4 until vertex generation is refactored (Phase 3)
    // Config may request 8 or 16, but Phase 2 only supports 4
    const int N = 4;  // Override tube_sides during Phase 2
    if (tube_sides != 4) {
        static bool warned_once = false;
        if (!warned_once) {
            spdlog::warn("Phase 2: Overriding tube_sides={} to N=4 (N=8/16 support coming in Phase 3)", tube_sides);
            warned_once = true;
        }
    }

    // Determine tube dimensions
    float width;
    if (segment.is_extrusion && segment.width >= 0.1f && segment.width <= 2.0f) {
        width = segment.width;
    } else {
        width = segment.is_extrusion ? extrusion_width_mm_ : travel_width_mm_;
    }
    width = width * 1.1f; // 10% safety margin

    const float half_width = width * 0.5f;
    const float half_height = layer_height_mm_ * 0.5f;

    // Calculate direction and perpendicular vectors
    const glm::vec3 dir = glm::normalize(segment.end - segment.start);
    const glm::vec3 up(0.0f, 0.0f, 1.0f);
    glm::vec3 right = glm::cross(dir, up);

    if (glm::length2(right) < 1e-6f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = glm::normalize(right);
    }

    // OrcaSlicer: up = right.cross(dir), NOT cross(dir, up)!
    const glm::vec3 perp_up = glm::normalize(glm::cross(right, dir));

    // Compute color
    uint32_t rgb = compute_segment_color(segment, quant.min_bounds.z, quant.max_bounds.z);
    if (!highlighted_objects_.empty() && !segment.object_name.empty() &&
        highlighted_objects_.count(segment.object_name) > 0) {
        constexpr float HIGHLIGHT_BRIGHTNESS = 1.8f;
        uint8_t r = static_cast<uint8_t>(std::min(255.0f, ((rgb >> 16) & 0xFF) * HIGHLIGHT_BRIGHTNESS));
        uint8_t g = static_cast<uint8_t>(std::min(255.0f, ((rgb >> 8) & 0xFF) * HIGHLIGHT_BRIGHTNESS));
        uint8_t b = static_cast<uint8_t>(std::min(255.0f, (rgb & 0xFF) * HIGHLIGHT_BRIGHTNESS));
        rgb = (r << 16) | (g << 8) | b;
    }

    uint8_t color_idx = add_to_color_palette(geometry, rgb);

    // Face colors: one color per face (N faces total)
    std::vector<uint8_t> face_colors(N, color_idx);

    if (debug_face_colors_) {
        // Cycle through 4 debug colors for N faces
        constexpr uint32_t DEBUG_COLORS[] = {
            DebugColors::TOP,    // Red
            DebugColors::RIGHT,  // Yellow
            DebugColors::BOTTOM, // Blue
            DebugColors::LEFT    // Green
        };

        for (int i = 0; i < N; i++) {
            uint32_t color = DEBUG_COLORS[i % 4];  // Cycle: R,Y,B,G,R,Y,B,G,...
            face_colors[i] = add_to_color_palette(geometry, color);
        }

        static bool logged_once = false;
        if (!logged_once) {
            spdlog::debug("DEBUG FACE COLORS ACTIVE: N={} faces, colors cycle through Red/Yellow/Blue/Green", N);
            logged_once = true;
        }
    }

    // OrcaSlicer approach: Apply vertical offset to BOTH prev and curr positions
    // This makes the TOP edge sit at the path Z-coordinate
    const glm::vec3 prev_pos = segment.start - half_height * perp_up;
    const glm::vec3 curr_pos = segment.end - half_height * perp_up;

    // Generate N vertex offsets in elliptical arrangement
    // Vertex i is at angle (i * 2π/N) around the ellipse
    // Position offset = cos(angle)*half_width*right + sin(angle)*half_height*perp_up
    const float angle_step = 2.0f * M_PI / N;
    std::vector<glm::vec3> vertex_offsets(N);

    for (int i = 0; i < N; i++) {
        float angle = i * angle_step;  // 0°, 360°/N, 2×360°/N, ...
        vertex_offsets[i] = half_width * std::cos(angle) * right +
                           half_height * std::sin(angle) * perp_up;
    }

    // Generate N face normals (one per face)
    // Face normal points outward from face center (midpoint between adjacent vertices)
    // Face i connects vertex i to vertex (i+1)%N, so face center is at angle (i+0.5)*angle_step
    std::vector<glm::vec3> face_normals(N);

    for (int i = 0; i < N; i++) {
        float face_angle = (i + 0.5f) * angle_step;  // Midpoint between vertices i and i+1
        glm::vec3 face_center_offset =
            half_width * std::cos(face_angle) * right +
            half_height * std::sin(face_angle) * perp_up;
        face_normals[i] = glm::normalize(face_center_offset);
    }

    // Phase 3: N-based vertex generation (replaces hardcoded N=4 logic)
    uint32_t idx_start = geometry.vertices.size();
    bool is_first_segment = !prev_start_cap.has_value();

    // ========== START CAP VERTICES (first segment only) ==========
    if (is_first_segment) {
        // START CAP: All normals point BACKWARD along segment (-dir)
        glm::vec3 cap_normal_start = -dir;
        uint16_t cap_normal_idx = add_to_normal_palette(geometry, cap_normal_start);

        // Use unique START_CAP color for debug visualization
        uint8_t start_cap_color_idx = debug_face_colors_
            ? add_to_color_palette(geometry, DebugColors::START_CAP)
            : face_colors[0];  // Use first face color if not debugging

        // Generate N start cap vertices
        for (int i = 0; i < N; i++) {
            glm::vec3 pos = prev_pos + vertex_offsets[i];
            geometry.vertices.push_back({
                quant.quantize_vec3(pos),
                cap_normal_idx,          // Axial normal pointing backward
                start_cap_color_idx      // MAGENTA for start cap in debug mode
            });
        }
        idx_start += N;
    }

    // ========== PREV SIDE FACE VERTICES ==========
    // Generate 2N prev vertices (2 vertices per face, N faces)
    // Each face connects vertex i to vertex (i+1)%N
    for (int i = 0; i < N; i++) {
        int next_i = (i + 1) % N;
        glm::vec3 pos_v1 = prev_pos + vertex_offsets[i];
        glm::vec3 pos_v2 = prev_pos + vertex_offsets[next_i];
        uint16_t normal_idx = add_to_normal_palette(geometry, face_normals[i]);

        geometry.vertices.push_back({
            quant.quantize_vec3(pos_v1),
            normal_idx,
            face_colors[i]
        });
        geometry.vertices.push_back({
            quant.quantize_vec3(pos_v2),
            normal_idx,
            face_colors[i]
        });
    }
    idx_start += 2*N;

    // ========== CURR SIDE FACE VERTICES ==========
    // Generate 2N curr vertices (2 vertices per face, N faces)
    for (int i = 0; i < N; i++) {
        int next_i = (i + 1) % N;
        glm::vec3 pos_v1 = curr_pos + vertex_offsets[i];
        glm::vec3 pos_v2 = curr_pos + vertex_offsets[next_i];
        uint16_t normal_idx = add_to_normal_palette(geometry, face_normals[i]);

        geometry.vertices.push_back({
            quant.quantize_vec3(pos_v1),
            normal_idx,
            face_colors[i]
        });
        geometry.vertices.push_back({
            quant.quantize_vec3(pos_v2),
            normal_idx,
            face_colors[i]
        });
    }
    idx_start += 2*N;

    // ========== END CAP TRACKING ==========
    // Track end cap edge positions (first vertex of each face in curr ring)
    TubeCap end_cap(N);
    uint32_t curr_faces_base = idx_start - 2*N;
    for (int i = 0; i < N; i++) {
        // Track first vertex of each face (vertex i)
        end_cap[i] = curr_faces_base + 2*i;
    }

    static int debug_count = 0;
    if (debug_count < 2 && debug_face_colors_) {
        spdlog::info("=== Segment {} | N={} | is_first={} ===", debug_count, N, is_first_segment);
        spdlog::info("  Segment: start=({:.3f},{:.3f},{:.3f}) end=({:.3f},{:.3f},{:.3f})",
                     segment.start.x, segment.start.y, segment.start.z,
                     segment.end.x, segment.end.y, segment.end.z);
        spdlog::info("  Direction: dir=({:.3f},{:.3f},{:.3f}) right=({:.3f},{:.3f},{:.3f}) perp_up=({:.3f},{:.3f},{:.3f})",
                     dir.x, dir.y, dir.z, right.x, right.y, right.z, perp_up.x, perp_up.y, perp_up.z);
        spdlog::info("  Cross-section center: prev_pos=({:.3f},{:.3f},{:.3f}) curr_pos=({:.3f},{:.3f},{:.3f})",
                     prev_pos.x, prev_pos.y, prev_pos.z, curr_pos.x, curr_pos.y, curr_pos.z);
        spdlog::info("  Curr vertices ({} total):", N);
        for (int i = 0; i < N; i++) {
            glm::vec3 pos = curr_pos + vertex_offsets[i];
            spdlog::info("    v{}[{}]: ({:.3f},{:.3f},{:.3f})", i, end_cap[i], pos.x, pos.y, pos.z);
        }
        debug_count++;
    }

    // Generate triangle strips for the 4 faces
    // Need to compute vertex indices based on whether this is first segment or not
    if (is_first_segment) {
        // First segment vertex layout:
        //   Start cap: 0-3
        //   Face1 prev: 4-5 (UP, RIGHT)
        //   Face2 prev: 6-7 (RIGHT, DOWN)
        //   Face3 prev: 8-9 (DOWN, LEFT)
        //   Face4 prev: 10-11 (LEFT, UP)
        //   Face1 curr: 12-13 (UP, RIGHT)
        //   Face2 curr: 14-15 (RIGHT, DOWN)
        //   Face3 curr: 16-17 (DOWN, LEFT)
        //   Face4 curr: 18-19 (LEFT, UP)
        uint32_t base = idx_start - 20;  // Total vertices: 4 (cap) + 8 (prev) + 8 (curr)
        uint32_t start_cap_up = base + 0;
        uint32_t start_cap_right = base + 1;
        uint32_t start_cap_down = base + 2;
        uint32_t start_cap_left = base + 3;

        // Face strips: each face uses 2 prev + 2 curr vertices
        uint32_t face1_prev_start = base + 4;
        uint32_t face2_prev_start = base + 6;
        uint32_t face3_prev_start = base + 8;
        uint32_t face4_prev_start = base + 10;
        uint32_t face1_curr_start = base + 12;
        uint32_t face2_curr_start = base + 14;
        uint32_t face3_curr_start = base + 16;
        uint32_t face4_curr_start = base + 18;

        // Four side faces - each face has 4 vertices in strip order [prev1, prev2, curr1, curr2]
        geometry.strips.push_back({face1_prev_start, face1_prev_start+1, face1_curr_start, face1_curr_start+1});  // TOP-RIGHT (RED)
        geometry.strips.push_back({face2_prev_start, face2_prev_start+1, face2_curr_start, face2_curr_start+1});  // RIGHT-BOTTOM (YELLOW)
        geometry.strips.push_back({face3_prev_start, face3_prev_start+1, face3_curr_start, face3_curr_start+1});  // BOTTOM-LEFT (BLUE)
        geometry.strips.push_back({face4_prev_start, face4_prev_start+1, face4_curr_start, face4_curr_start+1});  // LEFT-TOP (GREEN)

        // Start cap - REAL 4-vertex strip creating 2 valid triangles
        // Diamond quad with CCW winding (for GL_CULL_FACE)
        // Strip [A,B,C,D] creates: Triangle1(A,B,C) Triangle2(B,D,C)
        // From front (+X), we want CCW: UP → LEFT → RIGHT → DOWN
        // So: [UP, LEFT, RIGHT, DOWN] creates triangles (UP,LEFT,RIGHT) and (LEFT,DOWN,RIGHT)
        geometry.strips.push_back({start_cap_up, start_cap_left, start_cap_right, start_cap_down});

        if (debug_face_colors_) {
            spdlog::info("START CAP: Strip indices = [{}, {}, {}, {}]", start_cap_up, start_cap_left, start_cap_right, start_cap_down);
            spdlog::info("  Triangle 1: ({},{},{}) = (UP,LEFT,RIGHT) - CCW from front", start_cap_up, start_cap_left, start_cap_right);
            spdlog::info("  Triangle 2: ({},{},{}) = (LEFT,DOWN,RIGHT) - CCW from front", start_cap_left, start_cap_down, start_cap_right);
        }

    } else {
        // Subsequent segments: For now,  continuing to generate all  prev vertices
        // TODO: Implement true vertex sharing once we have a proper tracking mechanism
        // This would require tracking the previous segment's 8 curr vertex indices

        // Current approach: Same as first segment but without start cap
        uint32_t base = idx_start - 16;  // 8 prev + 8 curr
        uint32_t face1_prev_start = base + 0;
        uint32_t face2_prev_start = base + 2;
        uint32_t face3_prev_start = base + 4;
        uint32_t face4_prev_start = base + 6;
        uint32_t face1_curr_start = base + 8;
        uint32_t face2_curr_start = base + 10;
        uint32_t face3_curr_start = base + 12;
        uint32_t face4_curr_start = base + 14;

        // Four side faces
        geometry.strips.push_back({face1_prev_start, face1_prev_start+1, face1_curr_start, face1_curr_start+1});  // TOP-RIGHT
        geometry.strips.push_back({face2_prev_start, face2_prev_start+1, face2_curr_start, face2_curr_start+1});  // RIGHT-BOTTOM
        geometry.strips.push_back({face3_prev_start, face3_prev_start+1, face3_curr_start, face3_curr_start+1});  // BOTTOM-LEFT
        geometry.strips.push_back({face4_prev_start, face4_prev_start+1, face4_curr_start, face4_curr_start+1});  // LEFT-TOP
    }

    // End cap - Use the SAME positions as end_cap array but with axial normals
    uint8_t end_cap_color_idx = debug_face_colors_
        ? add_to_color_palette(geometry, DebugColors::END_CAP)
        : face_colors[0];

    glm::vec3 cap_normal_end = -dir;  // Same as start cap
    uint16_t end_cap_normal_idx = add_to_normal_palette(geometry, cap_normal_end);

    // Create 4 new vertices at the SAME POSITIONS as end_cap but with axial normals
    // end_cap[0] is UP, end_cap[1] is RIGHT, end_cap[2] is DOWN, end_cap[3] is LEFT
    uint32_t idx_end_cap_start = idx_start;

    if (debug_face_colors_) {
        spdlog::info("END CAP SOURCE INDICES: end_cap[0]={}, end_cap[1]={}, end_cap[2]={}, end_cap[3]={}",
                     end_cap[0], end_cap[1], end_cap[2], end_cap[3]);
        spdlog::info("  geometry.vertices.size() = {}", geometry.vertices.size());
        auto pos_up_dequant = quant.dequantize_vec3(geometry.vertices[end_cap[0]].position);
        spdlog::info("  Source vertex[{}] position: ({:.3f},{:.3f},{:.3f})",
                     end_cap[0], pos_up_dequant.x, pos_up_dequant.y, pos_up_dequant.z);
    }

    // Reuse the POSITIONS from the end_cap vertices (which are at curr_pos + d_*)
    geometry.vertices.push_back({geometry.vertices[end_cap[0]].position, end_cap_normal_idx, end_cap_color_idx});  // UP
    geometry.vertices.push_back({geometry.vertices[end_cap[1]].position, end_cap_normal_idx, end_cap_color_idx});  // RIGHT
    geometry.vertices.push_back({geometry.vertices[end_cap[2]].position, end_cap_normal_idx, end_cap_color_idx});  // DOWN
    geometry.vertices.push_back({geometry.vertices[end_cap[3]].position, end_cap_normal_idx, end_cap_color_idx});  // LEFT
    idx_start += 4;

    if (debug_face_colors_) {
        // Verify the newly added end cap vertices
        spdlog::info("END CAP VERTICES ADDED:");
        for (int i = 0; i < 4; i++) {
            auto pos = quant.dequantize_vec3(geometry.vertices[idx_end_cap_start + i].position);
            spdlog::info("  Vertex[{}]: ({:.3f},{:.3f},{:.3f})", idx_end_cap_start + i, pos.x, pos.y, pos.z);
        }
    }

    // OPPOSITE winding from start cap (caps face opposite directions)
    // Start cap: [UP, LEFT, RIGHT, DOWN] for normal pointing backward (-X)
    // End cap: [UP, RIGHT, LEFT, DOWN] for normal pointing backward (-X) but at far end
    geometry.strips.push_back({idx_end_cap_start + 0, idx_end_cap_start + 1, idx_end_cap_start + 3, idx_end_cap_start + 2});

    if (debug_face_colors_) {
        spdlog::info("END CAP: Strip indices = [{}, {}, {}, {}]",
                     idx_end_cap_start + 0, idx_end_cap_start + 1, idx_end_cap_start + 3, idx_end_cap_start + 2);
        spdlog::info("  Total geometry.strips.size() = {}", geometry.strips.size());
    }

    // Update counters
    int triangle_count = 8 + 4; // 4 side faces (8 tri) + start cap (2 tri) + end cap (2 tri)
    if (!is_first_segment) {
        triangle_count -= 2; // No start cap for subsequent segments
    }
    if (segment.is_extrusion) {
        geometry.extrusion_triangle_count += triangle_count;
    } else {
        geometry.travel_triangle_count += triangle_count;
    }

    return end_cap;
}

glm::vec3 GeometryBuilder::compute_perpendicular(const glm::vec3& direction, float width) const {
    // Define "up" vector (Z-axis)
    glm::vec3 up(0.0f, 0.0f, 1.0f);

    // Compute perpendicular in XY plane
    // perpendicular = cross(direction, up)
    glm::vec3 perp = glm::cross(direction, up);

    // If direction is vertical (parallel to up), cross product will be zero
    // Fall back to using X-axis as perpendicular
    if (glm::length2(perp) < 1e-6f) {
        perp = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        perp = glm::normalize(perp);
    }

    return perp * width;
}

uint32_t GeometryBuilder::compute_color_rgb(float z_height, float z_min, float z_max) const {
    if (!use_height_gradient_) {
        // Use solid filament color
        uint32_t color = (static_cast<uint32_t>(filament_r_) << 16) |
                         (static_cast<uint32_t>(filament_g_) << 8) |
                         static_cast<uint32_t>(filament_b_);
        static bool logged_once = false;
        if (!logged_once) {
            spdlog::debug("compute_color_rgb: R={}, G={}, B={} -> 0x{:06X}", filament_r_,
                          filament_g_, filament_b_, color);
            logged_once = true;
        }
        return color;
    }

    // Rainbow gradient from blue (bottom) to red (top)
    // Normalize Z to [0, 1]
    float range = z_max - z_min;
    float t = (range > 0.0f) ? (z_height - z_min) / range : 0.5f;
    t = std::max(0.0f, std::min(1.0f, t)); // Clamp to [0, 1]

    // Rainbow spectrum: Blue → Cyan → Green → Yellow → Red
    // Using HSV color space converted to RGB
    float hue = (1.0f - t) * 240.0f; // 240° (blue) to 0° (red)

    // Simple HSV to RGB conversion (assuming S=1.0, V=1.0)
    float c = 1.0f; // Chroma (full saturation)
    float h_prime = hue / 60.0f;
    float x = c * (1.0f - std::abs(std::fmod(h_prime, 2.0f) - 1.0f));

    float r, g, b;
    if (h_prime < 1.0f) {
        r = c;
        g = x;
        b = 0.0f;
    } else if (h_prime < 2.0f) {
        r = x;
        g = c;
        b = 0.0f;
    } else if (h_prime < 3.0f) {
        r = 0.0f;
        g = c;
        b = x;
    } else if (h_prime < 4.0f) {
        r = 0.0f;
        g = x;
        b = c;
    } else if (h_prime < 5.0f) {
        r = x;
        g = 0.0f;
        b = c;
    } else {
        r = c;
        g = 0.0f;
        b = x;
    }

    uint8_t r8 = static_cast<uint8_t>(r * 255.0f);
    uint8_t g8 = static_cast<uint8_t>(g * 255.0f);
    uint8_t b8 = static_cast<uint8_t>(b * 255.0f);

    return (static_cast<uint32_t>(r8) << 16) | (static_cast<uint32_t>(g8) << 8) |
           static_cast<uint32_t>(b8);
}

void GeometryBuilder::set_filament_color(const std::string& hex_color) {
    use_height_gradient_ = false; // Disable gradient

    // Remove '#' prefix if present
    const char* hex_str = hex_color.c_str();
    if (hex_str[0] == '#')
        hex_str++;

    // Parse RGB hex (e.g., "26A69A")
    uint32_t rgb = std::strtol(hex_str, nullptr, 16);
    filament_r_ = (rgb >> 16) & 0xFF;
    filament_g_ = (rgb >> 8) & 0xFF;
    filament_b_ = rgb & 0xFF;

    spdlog::info("Filament color set to #{:02X}{:02X}{:02X} (R={}, G={}, B={})", filament_r_,
                 filament_g_, filament_b_, filament_r_, filament_g_, filament_b_);
}

uint32_t GeometryBuilder::parse_hex_color(const std::string& hex_color) const {
    if (hex_color.length() < 6) {
        return 0x808080; // Default gray for invalid input
    }

    // Skip '#' prefix if present
    const char* hex_str = hex_color.c_str();
    if (hex_str[0] == '#') {
        hex_str++;
    }

    // Parse #RRGGBB format
    unsigned long value = std::strtoul(hex_str, nullptr, 16);

    uint8_t r = (value >> 16) & 0xFF;
    uint8_t g = (value >> 8) & 0xFF;
    uint8_t b = value & 0xFF;

    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

uint32_t GeometryBuilder::compute_segment_color(const ToolpathSegment& segment, float z_min,
                                                float z_max) const {
    // Priority 1: Tool-specific color from palette (multi-color prints)
    if (!tool_color_palette_.empty() && segment.tool_index >= 0 &&
        segment.tool_index < static_cast<int>(tool_color_palette_.size())) {
        const std::string& hex_color = tool_color_palette_[segment.tool_index];
        if (!hex_color.empty()) {
            return parse_hex_color(hex_color);
        }
    }

    // Priority 2: Z-height gradient (if enabled)
    if (use_height_gradient_) {
        float mid_z = (segment.start.z + segment.end.z) * 0.5f;
        return compute_color_rgb(mid_z, z_min, z_max);
    }

    // Priority 3: Default filament color
    return (static_cast<uint32_t>(filament_r_) << 16) |
           (static_cast<uint32_t>(filament_g_) << 8) | static_cast<uint32_t>(filament_b_);
}

} // namespace gcode
