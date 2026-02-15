// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <json.hpp> // nlohmann/json from libhv
#include <string>
#include <vector>

using json = nlohmann::json;

/**
 * @file moonraker_types.h
 * @brief Data structures for Moonraker API operations
 *
 * Contains all request/response types used by MoonrakerAPI, separated
 * from the API class for cleaner dependencies. Code that only needs
 * to work with these types (e.g., ValgACE backend) can include just
 * this header without pulling in the full API interface.
 */

// ============================================================================
// Safety Configuration
// ============================================================================

/**
 * @brief Safety limits for G-code generation and validation
 *
 * These limits protect against dangerous operations:
 * - Temperature limits prevent heater damage or fire hazards
 * - Position/distance limits prevent mechanical collisions
 * - Feedrate limits prevent motor stalling or mechanical stress
 *
 * Priority order:
 * 1. Explicitly configured values (via set_safety_limits())
 * 2. Auto-detected from printer.cfg (via update_safety_limits_from_printer())
 * 3. Conservative fallback defaults
 */
struct SafetyLimits {
    double max_temperature_celsius = 400.0;
    double min_temperature_celsius = 0.0;
    double min_extrude_temp_celsius = 170.0; ///< Minimum temp for extrusion (Klipper default)
    double max_fan_speed_percent = 100.0;
    double min_fan_speed_percent = 0.0;
    double max_feedrate_mm_min = 50000.0;
    double min_feedrate_mm_min = 0.0;
    double max_relative_distance_mm = 1000.0;
    double min_relative_distance_mm = -1000.0;
    double max_absolute_position_mm = 1000.0;
    double min_absolute_position_mm = 0.0;
};

// ============================================================================
// File Management Types
// ============================================================================

/**
 * @brief File information structure
 */
struct FileInfo {
    std::string filename;
    std::string path; // Relative to root
    uint64_t size = 0;
    double modified = 0.0;
    std::string permissions;
    bool is_dir = false;
};

/**
 * @brief Thumbnail info with dimensions
 */
struct ThumbnailInfo {
    std::string relative_path;
    int width = 0;
    int height = 0;

    /// Calculate pixel count for comparison (int64 to avoid overflow on large images)
    [[nodiscard]] int64_t pixel_count() const {
        return static_cast<int64_t>(width) * height;
    }
};

/**
 * @brief File metadata structure (detailed file info)
 */
struct FileMetadata {
    std::string filename;
    uint64_t size = 0;
    double modified = 0.0;
    std::string slicer;
    std::string slicer_version;
    double print_start_time = 0.0;
    std::string job_id; // Moonraker returns hex string like "00000D"
    uint32_t layer_count = 0;
    double object_height = 0.0;               // mm
    double estimated_time = 0.0;              // seconds
    double filament_total = 0.0;              // mm
    double filament_weight_total = 0.0;       // grams
    std::string filament_type;                // e.g., "PLA", "PETG", "ABS", "TPU", "ASA"
    std::string filament_name;                // Full filament name (e.g., "PolyMaker PolyLite ABS")
    double layer_height = 0.0;                // mm (per-layer height)
    double first_layer_height = 0.0;          // mm (first layer height, may differ)
    std::vector<std::string> filament_colors; // Hex colors per tool (e.g., ["#ED1C24", "#00C1AE"])
    double first_layer_bed_temp = 0.0;
    double first_layer_extr_temp = 0.0;
    uint64_t gcode_start_byte = 0;
    uint64_t gcode_end_byte = 0;
    std::string uuid;                      // Slicer-generated UUID (for history matching)
    std::vector<ThumbnailInfo> thumbnails; // Thumbnails with dimensions

    /**
     * @brief Get the largest thumbnail path
     * @return Path to largest thumbnail, or empty string if none available
     */
    [[nodiscard]] std::string get_largest_thumbnail() const {
        if (thumbnails.empty())
            return "";
        const ThumbnailInfo* best = &thumbnails[0];
        for (const auto& t : thumbnails) {
            if (t.pixel_count() > best->pixel_count()) {
                best = &t;
            }
        }
        return best->relative_path;
    }

    /**
     * @brief Get the best thumbnail for a target display size
     *
     * Selects the smallest thumbnail that meets or exceeds the target dimensions.
     * This minimizes download size while ensuring sufficient resolution for display.
     *
     * Selection priority:
     * 1. Smallest thumbnail where width >= target_w AND height >= target_h
     * 2. Fallback: largest available thumbnail (better to upscale slightly than use tiny)
     *
     * @param target_w Minimum acceptable width in pixels
     * @param target_h Minimum acceptable height in pixels
     * @return Pointer to best thumbnail, or nullptr if no thumbnails available
     *
     * Example usage:
     * @code
     *   // For a 160x160 display card
     *   const ThumbnailInfo* best = metadata.get_best_thumbnail(160, 160);
     *   if (best) {
     *       // 300x300 slicer thumb chosen over 32x32 icon
     *       download(best->relative_path);
     *   }
     * @endcode
     */
    [[nodiscard]] const ThumbnailInfo* get_best_thumbnail(int target_w, int target_h) const {
        if (thumbnails.empty()) {
            return nullptr;
        }

        const ThumbnailInfo* best_adequate = nullptr;  // Smallest that meets target
        const ThumbnailInfo* largest = &thumbnails[0]; // Fallback

        for (const auto& t : thumbnails) {
            // Track largest for fallback
            if (t.pixel_count() > largest->pixel_count()) {
                largest = &t;
            }

            // Check if this thumbnail meets minimum requirements
            if (t.width >= target_w && t.height >= target_h) {
                // Prefer smaller adequate thumbnails (less to download/process)
                if (!best_adequate || t.pixel_count() < best_adequate->pixel_count()) {
                    best_adequate = &t;
                }
            }
        }

        // Return adequate thumbnail if found, otherwise largest available
        return best_adequate ? best_adequate : largest;
    }
};

// ============================================================================
// Webcam Types
// ============================================================================

/// Webcam information from Moonraker
struct WebcamInfo {
    std::string name;         ///< Webcam name/identifier
    std::string service;      ///< Service type (e.g., "mjpegstreamer")
    std::string snapshot_url; ///< URL for snapshot image
    std::string stream_url;   ///< URL for MJPEG stream
    std::string uid;          ///< Unique identifier
    bool enabled = true;      ///< Whether the webcam is enabled
};

// ============================================================================
// Timelapse Types
// ============================================================================

/**
 * @brief Moonraker-Timelapse plugin settings
 *
 * Represents the configurable options for the Moonraker-Timelapse plugin.
 * Used by get_timelapse_settings() and set_timelapse_settings().
 */
struct TimelapseSettings {
    bool enabled = false;             ///< Whether timelapse recording is enabled
    std::string mode = "layermacro";  ///< "layermacro" (per-layer) or "hyperlapse" (time-based)
    int output_framerate = 30;        ///< Output video framerate (15/24/30/60)
    bool autorender = true;           ///< Auto-render video when print completes
    int park_retract_distance = 1;    ///< Retract distance before parking (mm)
    double park_extrude_speed = 15.0; ///< Extrude speed after unpark (mm/s)

    // Hyperlapse-specific
    int hyperlapse_cycle = 30; ///< Seconds between frames in hyperlapse mode
};

/**
 * @brief Information about the last captured timelapse frame
 */
struct LastFrameInfo {
    int frame_count = 0;         ///< Total frames captured
    std::string last_frame_file; ///< Filename of the last captured frame
};

// ============================================================================
// Power Device Types
// ============================================================================

/**
 * @brief Power device information
 */
struct PowerDevice {
    std::string device;                 ///< Device name (e.g., "printer", "led_strip")
    std::string type;                   ///< Device type (e.g., "gpio", "klipper_device")
    std::string status;                 ///< Current status ("on", "off", "error")
    bool locked_while_printing = false; ///< Cannot be toggled during prints
};

// ============================================================================
// Print Control Types
// ============================================================================

/**
 * @brief Result from start_modified_print() API call
 */
struct ModifiedPrintResult {
    std::string original_filename; ///< Original file path
    std::string print_filename;    ///< Symlink path used for printing
    std::string temp_filename;     ///< Temp file with modifications
    std::string status;            ///< "printing" on success
};

// ============================================================================
// REST API Types (for Moonraker extensions like ValgACE)
// ============================================================================

/**
 * @brief Response from a generic REST API call
 *
 * Used for communicating with Moonraker extension plugins that expose
 * REST endpoints (e.g., ValgACE at /server/ace/). Encapsulates both
 * success and error cases in a single structure.
 */
struct RestResponse {
    bool success = false; ///< true if HTTP 2xx response
    int status_code = 0;  ///< HTTP status code
    json data; ///< Parsed JSON response. If response isn't JSON, contains {"_raw_body": "..."}
    std::string error; ///< Error message (empty on success)
};

// ============================================================================
// G-code Store Types
// ============================================================================

/**
 * @brief Entry from Moonraker's gcode_store endpoint
 *
 * Represents a single G-code command or response from the
 * server.gcode_store history buffer.
 */
struct GcodeStoreEntry {
    std::string message; ///< G-code command or response text
    double time = 0.0;   ///< Unix timestamp
    std::string type;    ///< "command" or "response"
};

// ============================================================================
// Bed Mesh Types
// ============================================================================

/**
 * @brief Bed mesh profile data from Klipper
 *
 * Contains the probed Z-height matrix and associated metadata for bed mesh
 * visualization and compensation.
 */
struct BedMeshProfile {
    std::string name;                              ///< Profile name (e.g., "default", "adaptive")
    std::vector<std::vector<float>> probed_matrix; ///< Z height grid (row-major order)
    float mesh_min[2];                             ///< Min X,Y coordinates
    float mesh_max[2];                             ///< Max X,Y coordinates
    int x_count;                                   ///< Probes per row
    int y_count;                                   ///< Number of rows
    std::string algo;                              ///< Interpolation algorithm

    BedMeshProfile() : mesh_min{0, 0}, mesh_max{0, 0}, x_count(0), y_count(0) {}
};
