// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

/**
 * @file ui_busy_overlay.h
 * @brief Reusable busy/progress overlay for long-running operations
 *
 * Shows a semi-transparent overlay with spinner and progress text during
 * operations like file downloads/uploads. Features a grace period to avoid
 * flicker for fast operations.
 *
 * Usage:
 *   // Start operation - overlay appears after 300ms if still running
 *   BusyOverlay::show("Preparing...");
 *
 *   // Update progress during operation (call from main thread or via ui_queue_update)
 *   BusyOverlay::set_progress("Downloading", 45.0f);  // "Downloading... 45%"
 *
 *   // Operation complete
 *   BusyOverlay::hide();
 */

namespace helix {

class BusyOverlay {
  public:
    /**
     * @brief Request overlay display after grace period
     *
     * Overlay will appear after grace_period_ms if hide() hasn't been called.
     * Safe to call multiple times - subsequent calls update initial_text.
     *
     * @param initial_text Text to display (e.g., "Please wait...")
     * @param grace_period_ms Delay before showing overlay (default 300ms)
     */
    static void show(const std::string& initial_text = "Please wait...",
                     uint32_t grace_period_ms = 300);

    /**
     * @brief Update progress display
     *
     * Formats as "Operation... XX%" (e.g., "Downloading... 45%")
     * Only updates if overlay is visible or pending.
     *
     * @note NOT thread-safe! Must be called from the main LVGL thread.
     *       When calling from HTTP/background threads, use helix::ui::async_call():
     *       @code
     *       helix::ui::async_call([](void* data) {
     *           BusyOverlay::set_progress("Downloading", pct);
     *       }, nullptr);
     *       @endcode
     *
     * @param operation Operation name (e.g., "Downloading", "Uploading")
     * @param percent Progress percentage (0-100)
     */
    static void set_progress(const std::string& operation, float percent);

    /**
     * @brief Hide overlay immediately
     *
     * Cancels grace timer if pending, or removes overlay if visible.
     * Safe to call even if overlay was never shown.
     */
    static void hide();

    /**
     * @brief Check if overlay is currently visible
     *
     * @return true if overlay is on screen (not just pending)
     */
    static bool is_visible();

    /**
     * @brief Check if show was called but grace period hasn't elapsed
     *
     * @return true if waiting for grace period
     */
    static bool is_pending();

  private:
    BusyOverlay() = delete; // Static-only class
};

} // namespace helix
