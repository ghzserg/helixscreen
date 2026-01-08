// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file lvgl_log_handler.h
 * @brief Custom LVGL log handler that routes through spdlog
 *
 * Routes all LVGL log messages through spdlog for consistent logging.
 * Provides enhanced debugging for subject type mismatch warnings by
 * looking up subject metadata in the SubjectDebugRegistry.
 */

#pragma once

namespace helix {
namespace logging {

/**
 * @brief Register the custom LVGL log handler
 *
 * Call this after spdlog initialization to route LVGL logs through spdlog.
 * Replaces printf-based LVGL logging with spdlog-based logging.
 *
 * Features:
 * - Routes LVGL log levels to corresponding spdlog levels
 * - Detects subject type mismatch warnings and enriches with debug info
 * - Looks up subjects in SubjectDebugRegistry for enhanced debugging
 */
void register_lvgl_log_handler();

} // namespace logging
} // namespace helix
