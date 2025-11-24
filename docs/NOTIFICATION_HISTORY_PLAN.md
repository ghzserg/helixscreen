# Notification History & Error Reporting System - Implementation Plan

**Status:** ✅ Phase 1 COMPLETE - Ready for Phase 2
**Priority:** High
**Complexity:** Medium
**Actual Effort:** 2 sessions (2025-01-23, 2025-11-23)
**Last Updated:** 2025-11-23

---

## Quick Start Todo List

For the developer implementing this system, here's a sequential task list:

### Phase 1: Core Infrastructure ✅ COMPLETE (2025-11-23)
- [x] Design notification history/log data structure
- [x] Implement notification history manager with circular buffer (`NotificationHistory` class)
- [x] Create notification history panel XML component (`notification_history_panel.xml`)
- [x] Create notification history item XML component (`notification_history_item.xml`)
- [x] Implement notification history panel manager (`ui_panel_notification_history.cpp`)
- [x] Add unread badge to status bar
- [x] Wire up history panel navigation
- [x] Create helper macros/functions for error reporting (`ui_error_reporting.h`)
- [x] Integrate history tracking with existing notification functions
- [x] ~~Fix linker errors~~ (RESOLVED 2025-01-23)
- [x] Integrate into main.cpp (XML registration, init calls, app layout)
- [x] Fix XML symbol issues (replaced LV_SYMBOL constants with Unicode entities)
- [x] Fix keyboard LV_KB_BTN macro errors
- [x] Add test notifications to verify system triggers
- [x] Debug status bar initialization (fixed duplicate XML attribute)
- [x] Fix XML duplicate attribute error (notification_history_item.xml line 21)
- [x] Verify app_layout structure matches main.cpp expectations
- [x] Optimize status bar for tiny screens (padding_tiny, gap_small)
- [x] Remove test notifications from main.cpp (commented out for future testing)

### Phase 2: Error Reporting Migration (IN PROGRESS)
- [x] Test notification system triggers (4 test messages working)
- [x] Fix status bar initialization (network icon widget lookup fixed - 2025-11-23)
- [x] Implement reactive network status icon (observer pattern, Font Awesome sitemap - 2025-11-23)
- [ ] Verify UI appearance (toasts, history panel)
- [ ] Audit Moonraker error sites (~20 calls)
- [ ] Convert Moonraker errors to use `NOTIFY_ERROR()` / `NOTIFY_WARNING()`
- [ ] Audit WiFi error sites (~15 calls)
- [ ] Convert WiFi errors to use new macros
- [ ] Audit file I/O error sites (~25 calls)
- [ ] Convert file I/O errors to use new macros
- [ ] Test all conversions

### Phase 3: Comprehensive Migration (FUTURE)
- [ ] Audit all remaining spdlog::error and spdlog::warn usage (~445 calls)
- [ ] Categorize each: internal vs user-facing vs critical
- [ ] Convert user-facing errors (estimated ~185 remaining)
- [ ] Add user-friendly messages where needed
- [ ] Comprehensive testing
- [ ] (Optional) Add spdlog sink/interceptor to auto-capture tagged errors

### Phase 4: Unit Testing & Quality Assurance (HIGH PRIORITY)
- [ ] **NotificationHistory Unit Tests:**
  - [ ] Test circular buffer behavior (overflow, wraparound)
  - [ ] Test thread safety (concurrent add/get operations)
  - [ ] Test unread count tracking
  - [ ] Test filter by severity
  - [ ] Test mark all as read
  - [ ] Test clear functionality
  - [ ] Test persistence (save/load from disk)
  - [ ] Test timestamp formatting

- [ ] **Notification System Unit Tests:**
  - [ ] Test toast creation and auto-dismiss timers
  - [ ] Test modal error/warning dialogs
  - [ ] Test notification subject observer pattern
  - [ ] Test deduplication logic (same message within time window)
  - [ ] Test notification severity icon mapping
  - [ ] Test toast stacking (multiple simultaneous toasts)

- [ ] **Status Bar Unit Tests:**
  - [ ] Test icon widget lookup and initialization
  - [ ] Test notification badge visibility toggle
  - [ ] Test badge count updates
  - [ ] Test icon click handlers
  - [ ] Test status icon updates (network, printer, notification)

- [ ] **History Panel Unit Tests:**
  - [ ] Test panel creation and layout
  - [ ] Test dynamic list population from history
  - [ ] Test severity filtering (All, Errors, Warnings, Info)
  - [ ] Test empty state display
  - [ ] Test "Clear All" button
  - [ ] Test mark-as-read on panel open
  - [ ] Test history item click handlers

- [ ] **Error Reporting Macro Tests:**
  - [ ] Test NOTIFY_ERROR() creates toast and logs
  - [ ] Test NOTIFY_WARNING() creates warning toast
  - [ ] Test NOTIFY_INFO() creates info toast
  - [ ] Test NOTIFY_SUCCESS() creates success toast
  - [ ] Test NOTIFY_ERROR_MODAL() creates modal dialog
  - [ ] Test LOG_ERROR_INTERNAL() logs only (no UI)
  - [ ] Test ErrorContext RAII class

- [ ] **Integration Tests:**
  - [ ] Test full notification flow: trigger → history → display → clear
  - [ ] Test bell icon click opens history panel
  - [ ] Test unread badge appears/disappears correctly
  - [ ] Test history survives app restart (if persistence enabled)
  - [ ] Test concurrent notifications from multiple threads
  - [ ] Test notification during wizard/overlay states

- [ ] **Test Infrastructure:**
  - [ ] Create mock notification observer for testing
  - [ ] Create test fixtures for NotificationHistory
  - [ ] Create helper functions for generating test notifications
  - [ ] Add test coverage reports
  - [ ] Integrate tests into CI/CD pipeline

**Test Framework:** Use existing Catch2 infrastructure (see `docs/TESTING.md`)

**Test File Locations:**
```
tests/unit/test_notification_history.cpp
tests/unit/test_notification_system.cpp
tests/unit/test_status_bar.cpp
tests/unit/test_error_reporting_macros.cpp
tests/integration/test_notification_flow.cpp
```

**Coverage Goals:**
- NotificationHistory: 100% (critical infrastructure)
- Notification system: 95%
- Status bar: 90%
- Error reporting macros: 100%
- Integration tests: Cover all user-facing workflows

---

## Table of Contents

1. [Overview](#overview)
2. [Current State Analysis](#current-state-analysis)
3. [Proposed Architecture](#proposed-architecture)
4. [Implementation Details](#implementation-details)
5. [Migration Strategy](#migration-strategy)
6. [Testing Plan](#testing-plan)
7. [Future Enhancements](#future-enhancements)
8. [Warnings & Gotchas](#warnings--gotchas)

---

## Overview

### Problem Statement

Currently, HelixScreen has **505 spdlog::error/warn calls** scattered throughout the codebase. These errors are:
- ❌ Only visible in terminal/logs (users don't see them)
- ❌ Not surfaced in the UI
- ❌ No persistent history for users to review
- ❌ No way to troubleshoot issues after they occur

### Solution Goals

1. **User Visibility** - Important errors should create UI notifications
2. **Persistent History** - Users can review past errors/warnings
3. **Smart Filtering** - Internal errors don't spam users
4. **Context Preservation** - Enough detail to diagnose issues
5. **Minimal Disruption** - Non-critical errors don't block workflow

### Success Criteria

- ✅ Users see notifications for user-facing errors
- ✅ Users can access notification history panel
- ✅ Internal/debug errors remain log-only
- ✅ Critical errors require user acknowledgment
- ✅ History persists across app sessions (optional)
- ✅ Duplicate notifications are suppressed

---

## Current State Analysis

### Existing Notification System

**Already Implemented (as of 2025-01):**
- ✅ Toast notifications (auto-dismiss, non-blocking)
- ✅ Modal error/warning dialogs (blocking)
- ✅ Status bar with network/printer/notification icons
- ✅ Reactive subject system for decoupled notifications
- ✅ Unified notification API (`ui_notification.h`)

**Missing:**
- ❌ Notification history/log storage
- ❌ History review panel UI
- ❌ Integration with spdlog
- ❌ Error classification/categorization
- ❌ Helper macros for error reporting

### Error Distribution (Audit Results)

```bash
# Total error/warn calls
grep -r "spdlog::error\|spdlog::warn" src/ | wc -l
# Result: 505 calls

# Sample categories:
# - Widget/XML failures: ~150 calls (internal, log-only)
# - Network/connection errors: ~80 calls (user-facing)
# - File I/O errors: ~40 calls (user-facing)
# - Moonraker API errors: ~60 calls (user-facing)
# - Temperature/hardware warnings: ~30 calls (user-facing)
# - Misc debug/internal: ~145 calls (log-only)
```

**Categorization Strategy:**
- **Internal (~295 calls):** Widget creation, XML parsing, render issues → Log only
- **User-Facing (~210 calls):** Connection errors, save failures, API errors → Show notification

---

## Proposed Architecture

### 3-Tier Notification Strategy

```
┌─────────────────────────────────────────────────────────────┐
│                    User Error Occurs                         │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
         ┌────────────────────────────┐
         │   Error Reporting Layer    │
         │  (Macros/Helper Functions) │
         └────────────┬───────────────┘
                      │
         ┌────────────┴────────────┐
         │                         │
         ▼                         ▼
  ┌─────────────┐         ┌──────────────┐
  │   spdlog    │         │ Notification │
  │   Logging   │         │   System     │
  └──────┬──────┘         └──────┬───────┘
         │                       │
         ▼                       ▼
  ┌─────────────┐         ┌──────────────┐
  │ Custom Sink │         │   UI Toast/  │
  │  (Optional) │         │    Modal     │
  └──────┬──────┘         └──────┬───────┘
         │                       │
         └───────────┬───────────┘
                     │
                     ▼
          ┌──────────────────────┐
          │  Notification History │
          │    (Circular Buffer)  │
          └──────────────────────┘
                     │
                     ▼
          ┌──────────────────────┐
          │   History Panel UI   │
          │  (User Can Review)   │
          └──────────────────────┘
```

---

## Implementation Details

### Part 1: Notification History System

#### 1.1 Data Structure

**File:** `include/ui_notification_history.h`

```cpp
#pragma once

#include "ui_toast.h"
#include <cstdint>
#include <vector>
#include <mutex>

/**
 * @brief Single notification history entry
 */
struct NotificationHistoryEntry {
    uint64_t timestamp_ms;     ///< LVGL tick time when notification occurred
    ToastSeverity severity;    ///< INFO, SUCCESS, WARNING, ERROR
    char title[64];            ///< Title (empty for toasts)
    char message[256];         ///< Notification message
    bool was_modal;            ///< true if shown as modal dialog
    bool was_read;             ///< true if user viewed in history panel
};

/**
 * @brief Notification history manager
 *
 * Maintains a circular buffer of the last N notifications for user review.
 * Thread-safe for concurrent access from UI and background threads.
 */
class NotificationHistory {
public:
    static constexpr size_t MAX_ENTRIES = 100;  ///< Circular buffer size

    /**
     * @brief Get singleton instance
     */
    static NotificationHistory& instance();

    /**
     * @brief Add notification to history
     *
     * @param entry Notification entry to add
     */
    void add(const NotificationHistoryEntry& entry);

    /**
     * @brief Get all history entries (newest first)
     *
     * @return Vector of history entries
     */
    std::vector<NotificationHistoryEntry> get_all() const;

    /**
     * @brief Get entries filtered by severity
     *
     * @param severity Filter by this severity (or pass -1 for all)
     * @return Vector of filtered entries
     */
    std::vector<NotificationHistoryEntry> get_filtered(int severity) const;

    /**
     * @brief Get count of unread notifications
     *
     * @return Number of unread entries
     */
    size_t get_unread_count() const;

    /**
     * @brief Mark all notifications as read
     */
    void mark_all_read();

    /**
     * @brief Clear all history
     */
    void clear();

    /**
     * @brief Get total notification count
     *
     * @return Number of entries in history
     */
    size_t count() const;

    /**
     * @brief Save history to disk (optional)
     *
     * @param path File path to save to
     * @return true on success, false on failure
     */
    bool save_to_disk(const char* path) const;

    /**
     * @brief Load history from disk (optional)
     *
     * @param path File path to load from
     * @return true on success, false on failure
     */
    bool load_from_disk(const char* path);

private:
    NotificationHistory() = default;
    ~NotificationHistory() = default;
    NotificationHistory(const NotificationHistory&) = delete;
    NotificationHistory& operator=(const NotificationHistory&) = delete;

    mutable std::mutex mutex_;
    std::vector<NotificationHistoryEntry> entries_;
    size_t head_index_ = 0;  ///< Circular buffer write position
};
```

#### 1.2 Implementation Notes

**File:** `src/ui_notification_history.cpp`

**Key Implementation Details:**

1. **Thread Safety:**
   - Use `std::lock_guard<std::mutex>` for all public methods
   - Background threads (Moonraker, WiFi) may add notifications concurrently

2. **Circular Buffer:**
   - When buffer is full, oldest entry is overwritten
   - Use modulo arithmetic: `head_index_ = (head_index_ + 1) % MAX_ENTRIES`

3. **Timestamp Handling:**
   - Use `lv_tick_get()` for timestamps (milliseconds since boot)
   - Display relative time for recent entries ("2 min ago")
   - Display absolute time for older entries ("Jan 15, 14:32")

4. **Memory Management:**
   - Fixed-size circular buffer (no dynamic allocation in hot path)
   - Pre-allocate vector to MAX_ENTRIES capacity

5. **Persistence (Optional):**
   - Save to JSON file in config directory
   - Format: `~/.config/helixscreen/notification_history.json`
   - Truncate to last 50 entries when saving (keep file size reasonable)

**Sample JSON Format:**
```json
{
  "version": 1,
  "entries": [
    {
      "timestamp": 1234567890,
      "severity": "ERROR",
      "title": "Connection Failed",
      "message": "Unable to connect to printer at 192.168.1.100",
      "was_modal": true,
      "was_read": false
    }
  ]
}
```

#### 1.3 Integration with Notification System

**Modify:** `src/ui_notification.cpp`

Add to `ui_notification_init()`:
```cpp
void ui_notification_init() {
    // Existing observer setup...
    lv_subject_add_observer(&get_notification_subject(), notification_observer_cb, nullptr);

    // NEW: Load notification history from disk
    NotificationHistory::instance().load_from_disk(
        RuntimeConfig::get_config_path("notification_history.json").c_str()
    );

    spdlog::debug("Notification system initialized");
}
```

Add to notification display functions:
```cpp
void ui_notification_error(const char* title, const char* message, bool modal) {
    if (!message) return;

    // Show notification (existing code)...

    // NEW: Add to history
    NotificationHistoryEntry entry = {
        .timestamp_ms = lv_tick_get(),
        .severity = ToastSeverity::ERROR,
        .title = {0},
        .message = {0},
        .was_modal = modal,
        .was_read = false
    };
    if (title) {
        strncpy(entry.title, title, sizeof(entry.title) - 1);
    }
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    NotificationHistory::instance().add(entry);

    // Update status bar unread count
    ui_status_bar_update_notification_count(
        NotificationHistory::instance().get_unread_count()
    );
}
```

---

### Part 2: History Panel UI

#### 2.1 XML Component

**File:** `ui_xml/notification_history_panel.xml`

```xml
<?xml version="1.0"?>
<!-- Notification History Panel - Review past notifications -->
<component>
  <view extends="lv_obj" name="notification_history_panel"
        width="100%" height="100%"
        style_bg_opa="255"
        style_border_width="0"
        style_pad_all="0"
        flex_flow="column">

    <!-- Header -->
    <lv_obj width="100%" height="#header_height"
            style_bg_opa="0" style_border_width="0"
            style_pad_left="#padding_normal" style_pad_right="#padding_normal"
            flex_flow="row" style_flex_main_place="space_between" style_flex_cross_place="center">

      <!-- Back button -->
      <lv_button name="history_back_btn"
                 width="#header_back_button_size" height="#header_back_button_size"
                 style_bg_opa="0" style_border_width="0" clickable="true">
        <lv_label text="&#xF053" align="center" style_text_font="#font_heading"/>
      </lv_button>

      <!-- Title -->
      <text_heading text="Notifications" flex_grow="1" style_text_align="center"/>

      <!-- Clear All button -->
      <lv_button name="history_clear_btn"
                 width="#header_right_button_width" height="#header_right_button_height"
                 style_radius="#border_radius" style_border_width="0" clickable="true">
        <text_body text="Clear All" align="center"/>
      </lv_button>
    </lv_obj>

    <!-- Filter buttons -->
    <lv_obj width="100%" height="60"
            style_bg_opa="0" style_border_width="0"
            style_pad_all="#padding_small" style_pad_gap="#gap_small"
            flex_flow="row" style_flex_main_place="center">

      <lv_button name="filter_all_btn" width="80" height="#button_height_small"
                 style_radius="#border_radius" clickable="true">
        <text_body text="All" align="center"/>
      </lv_button>

      <lv_button name="filter_errors_btn" width="80" height="#button_height_small"
                 style_radius="#border_radius" clickable="true">
        <text_body text="Errors" align="center"/>
      </lv_button>

      <lv_button name="filter_warnings_btn" width="80" height="#button_height_small"
                 style_radius="#border_radius" clickable="true">
        <text_body text="Warnings" align="center"/>
      </lv_button>

      <lv_button name="filter_info_btn" width="80" height="#button_height_small"
                 style_radius="#border_radius" clickable="true">
        <text_body text="Info" align="center"/>
      </lv_button>
    </lv_obj>

    <!-- Notification list (populated dynamically) -->
    <lv_obj name="notification_list_container" width="100%" flex_grow="1"
            style_border_width="0" style_pad_all="0"
            flex_flow="column" scrollbar_mode="active">
      <!-- List items added dynamically via C++ -->
    </lv_obj>

    <!-- Empty state (shown when no notifications) -->
    <lv_obj name="empty_state" width="100%" height="100%"
            style_bg_opa="0" style_border_width="0"
            flex_flow="column" style_flex_main_place="center" style_flex_cross_place="center"
            hidden="false">
      <lv_label text="&#xF0F3" style_text_font="montserrat_48" style_text_color="#text_secondary"/>
      <text_body text="No notifications" style_text_color="#text_secondary" style_pad_top="#gap_normal"/>
    </lv_obj>
  </view>
</component>
```

**File:** `ui_xml/notification_history_item.xml`

```xml
<?xml version="1.0"?>
<!-- Single notification history item -->
<component>
  <api>
    <prop type="string" name="severity"/>      <!-- "error", "warning", "info", "success" -->
    <prop type="string" name="title"/>
    <prop type="string" name="message"/>
    <prop type="string" name="timestamp"/>
    <prop type="string" name="border_color"/>
  </api>

  <view extends="lv_obj" name="notification_item"
        width="100%" style_min_height="80"
        style_pad_all="#padding_normal"
        style_pad_gap="#gap_small"
        style_border_width="0"
        style_border_color="$border_color"
        style_border_side="left"
        style_border_width="4"
        clickable="true"
        flex_flow="column">

    <!-- Header: severity icon + timestamp -->
    <lv_obj width="100%" style_bg_opa="0" style_border_width="0" style_pad_all="0"
            flex_flow="row" style_flex_main_place="space_between">

      <!-- Severity icon + title -->
      <lv_obj style_bg_opa="0" style_border_width="0" style_pad_all="0" style_pad_gap="#gap_small"
              flex_flow="row" style_flex_cross_place="center" flex_grow="1">
        <lv_label name="severity_icon" text="&#xF071"
                  style_text_font="#font_heading" style_text_color="$border_color"/>
        <text_body name="item_title" text="$title" style_text_font="#font_body"/>
      </lv_obj>

      <!-- Timestamp -->
      <text_small name="item_timestamp" text="$timestamp" style_text_color="#text_secondary"/>
    </lv_obj>

    <!-- Message -->
    <text_body name="item_message" text="$message" width="100%"
               style_text_color="#text_secondary" long_mode="wrap"/>
  </view>
</component>
```

#### 2.2 Panel Manager

**File:** `include/ui_panel_notification_history.h`

```cpp
#pragma once

#include "lvgl.h"

/**
 * @brief Initialize notification history panel subjects
 */
void ui_panel_notification_history_init_subjects();

/**
 * @brief Create notification history panel
 *
 * @param parent Parent object
 * @return Created panel object
 */
lv_obj_t* ui_panel_notification_history_create(lv_obj_t* parent);

/**
 * @brief Refresh notification list from history
 *
 * Called when panel is shown or filter changes.
 */
void ui_panel_notification_history_refresh();
```

**File:** `src/ui_panel_notification_history.cpp`

**Key Implementation:**

1. **Populate List Dynamically:**
   ```cpp
   void ui_panel_notification_history_refresh() {
       auto entries = NotificationHistory::instance().get_all();

       lv_obj_t* list_container = lv_obj_find_by_name(panel, "notification_list_container");
       lv_obj_clean(list_container);  // Clear existing items

       for (const auto& entry : entries) {
           // Create list item from XML
           const char* attrs[] = {
               "severity", severity_to_string(entry.severity),
               "title", entry.title[0] ? entry.title : "Notification",
               "message", entry.message,
               "timestamp", format_timestamp(entry.timestamp_ms),
               "border_color", severity_to_color(entry.severity),
               nullptr
           };

           lv_obj_t* item = lv_xml_create(list_container, "notification_history_item", attrs);
           // Wire up click handler for details view
       }

       // Show/hide empty state
       bool has_entries = !entries.empty();
       lv_obj_t* empty_state = lv_obj_find_by_name(panel, "empty_state");
       if (has_entries) {
           lv_obj_add_flag(empty_state, LV_OBJ_FLAG_HIDDEN);
       } else {
           lv_obj_remove_flag(empty_state, LV_OBJ_FLAG_HIDDEN);
       }

       // Mark as read
       NotificationHistory::instance().mark_all_read();
       ui_status_bar_update_notification_count(0);
   }
   ```

2. **Timestamp Formatting:**
   ```cpp
   std::string format_timestamp(uint64_t timestamp_ms) {
       uint64_t now = lv_tick_get();
       uint64_t diff_ms = now - timestamp_ms;

       if (diff_ms < 60000) {  // < 1 min
           return "Just now";
       } else if (diff_ms < 3600000) {  // < 1 hour
           return fmt::format("{} min ago", diff_ms / 60000);
       } else if (diff_ms < 86400000) {  // < 1 day
           return fmt::format("{} hours ago", diff_ms / 3600000);
       } else {
           // Convert to absolute time (needs RTC or system time)
           return "Recently";  // Or format as "Jan 15, 14:32"
       }
   }
   ```

3. **Filter Implementation:**
   - Store current filter in panel state
   - Refresh list when filter button clicked
   - Use `NotificationHistory::get_filtered(severity)`

#### 2.3 Status Bar Integration

**Modify:** `ui_xml/status_bar.xml`

Add notification history button:
```xml
<!-- Notification history button (always visible, shows unread count) -->
<lv_obj style_bg_opa="0" style_border_width="0" style_pad_all="0"
        clickable="true">
  <event_cb trigger="clicked" callback="status_notification_history_clicked"/>

  <lv_label name="status_notification_history_icon" text="&#xF0F3"
            style_text_font="#font_heading"
            style_text_color="#text_secondary"/>

  <!-- Unread badge (hidden when count is 0) -->
  <lv_obj name="notification_badge"
          width="20" height="20"
          align="top_right"
          style_radius="10"
          style_bg_color="#error_color"
          hidden="true">
    <lv_label name="notification_badge_count" text="0"
              align="center"
              style_text_font="#font_small"
              style_text_color="#FFFFFF"/>
  </lv_obj>
</lv_obj>
```

**Modify:** `include/ui_status_bar.h`

Add:
```cpp
/**
 * @brief Update notification unread count badge
 *
 * @param count Number of unread notifications (0 hides badge)
 */
void ui_status_bar_update_notification_count(size_t count);
```

**Modify:** `src/ui_status_bar.cpp`

Implement:
```cpp
void ui_status_bar_update_notification_count(size_t count) {
    lv_obj_t* badge = lv_obj_find_by_name(lv_screen_active(), "notification_badge");
    lv_obj_t* badge_count = lv_obj_find_by_name(lv_screen_active(), "notification_badge_count");

    if (!badge || !badge_count) return;

    if (count == 0) {
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(badge_count, "%zu", count);
    }
}
```

---

### Part 3: spdlog Integration (Optional)

#### 3.1 Custom spdlog Sink

**File:** `include/notification_sink.h`

```cpp
#pragma once

#include <spdlog/sinks/base_sink.h>
#include <mutex>

/**
 * @brief Custom spdlog sink that creates UI notifications
 *
 * Intercepts spdlog::error and spdlog::warn calls and optionally
 * creates user-visible notifications based on message tags.
 *
 * Tag-based filtering:
 * - [INTERNAL] → Log only, no UI notification
 * - [USER] → Log + toast notification
 * - [CRITICAL] → Log + modal dialog
 * - No tag → Log only (safe default)
 */
class NotificationSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    NotificationSink() = default;
    ~NotificationSink() override = default;

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}

private:
    /**
     * @brief Extract tag from message (e.g., "[USER]", "[CRITICAL]")
     *
     * @param msg Log message payload
     * @return Tag string or empty string if no tag
     */
    std::string extract_tag(const spdlog::details::log_msg& msg);

    /**
     * @brief Strip tag from message for display
     */
    std::string strip_tag(const spdlog::details::log_msg& msg);
};
```

**File:** `src/notification_sink.cpp`

```cpp
#include "notification_sink.h"
#include "ui_notification.h"
#include <spdlog/pattern_formatter.h>

void NotificationSink::sink_it_(const spdlog::details::log_msg& msg) {
    // Extract tag and clean message
    std::string tag = extract_tag(msg);
    std::string clean_msg = strip_tag(msg);

    // Only process errors and warnings
    if (msg.level != spdlog::level::err && msg.level != spdlog::level::warn) {
        return;
    }

    // Determine notification action based on tag
    if (tag == "INTERNAL" || tag.empty()) {
        // Log only, no UI notification
        return;
    } else if (tag == "USER") {
        // Show toast notification
        if (msg.level == spdlog::level::err) {
            ui_notification_error(nullptr, clean_msg.c_str(), false);
        } else {
            ui_notification_warning(clean_msg.c_str());
        }
    } else if (tag == "CRITICAL") {
        // Show modal dialog
        ui_notification_error("Critical Error", clean_msg.c_str(), true);
    }
}

std::string NotificationSink::extract_tag(const spdlog::details::log_msg& msg) {
    std::string_view payload(msg.payload.data(), msg.payload.size());

    // Look for [TAG] at start of message
    if (payload.size() > 2 && payload[0] == '[') {
        size_t end = payload.find(']');
        if (end != std::string_view::npos && end < 20) {  // Reasonable tag length
            return std::string(payload.substr(1, end - 1));
        }
    }

    return "";
}

std::string NotificationSink::strip_tag(const spdlog::details::log_msg& msg) {
    std::string_view payload(msg.payload.data(), msg.payload.size());

    // Remove [TAG] prefix if present
    if (payload.size() > 2 && payload[0] == '[') {
        size_t end = payload.find(']');
        if (end != std::string_view::npos) {
            // Skip tag and any following whitespace
            size_t start = end + 1;
            while (start < payload.size() && std::isspace(payload[start])) {
                start++;
            }
            return std::string(payload.substr(start));
        }
    }

    return std::string(payload);
}
```

**Integration in main.cpp:**

```cpp
#include "notification_sink.h"

// During logger setup:
auto notification_sink = std::make_shared<NotificationSink>();
auto logger = std::make_shared<spdlog::logger>("multi_sink",
    spdlog::sinks_init_list{console_sink, file_sink, notification_sink});
spdlog::set_default_logger(logger);
```

#### 3.2 Deduplication Logic

Add to `NotificationSink` to prevent spam:

```cpp
class NotificationSink : public spdlog::sinks::base_sink<std::mutex> {
private:
    static constexpr uint64_t DEDUPE_WINDOW_MS = 5000;  // 5 seconds

    struct RecentMessage {
        std::string msg;
        uint64_t timestamp_ms;
    };

    std::vector<RecentMessage> recent_messages_;

    bool is_duplicate(const std::string& msg) {
        uint64_t now = lv_tick_get();

        // Remove old messages outside dedupe window
        recent_messages_.erase(
            std::remove_if(recent_messages_.begin(), recent_messages_.end(),
                [now](const RecentMessage& rm) {
                    return (now - rm.timestamp_ms) > DEDUPE_WINDOW_MS;
                }),
            recent_messages_.end()
        );

        // Check for duplicate
        for (const auto& rm : recent_messages_) {
            if (rm.msg == msg) {
                return true;  // Duplicate found
            }
        }

        // Add to recent messages
        recent_messages_.push_back({msg, now});
        return false;
    }
};
```

---

### Part 4: Helper Macros & Functions

#### 4.1 Error Reporting Macros

**File:** `include/ui_error_reporting.h` (header-only)

```cpp
#pragma once

#include "ui_notification.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

/**
 * @file ui_error_reporting.h
 * @brief Convenience macros for error reporting with automatic UI notifications
 *
 * These macros combine spdlog logging with UI notifications for better user experience.
 *
 * Usage Examples:
 * ```cpp
 * // Internal error (logged but not shown to user)
 * LOG_ERROR_INTERNAL("Failed to create widget: {}", widget_name);
 *
 * // User-facing error (logged + toast notification)
 * NOTIFY_ERROR("Failed to save configuration");
 *
 * // User-facing warning (logged + warning toast)
 * NOTIFY_WARNING("Printer temperature approaching {}°C limit", temp);
 *
 * // Critical error (logged + modal dialog)
 * NOTIFY_ERROR_MODAL("Connection Failed", "Unable to reach printer at {}", ip_addr);
 * ```
 */

// ============================================================================
// Internal Errors (Log Only)
// ============================================================================

/**
 * @brief Log internal error (not shown to user)
 *
 * Use for widget creation failures, XML parsing errors, and other internal
 * issues that don't require user action.
 */
#define LOG_ERROR_INTERNAL(msg, ...) \
    spdlog::error("[INTERNAL] " msg, ##__VA_ARGS__)

/**
 * @brief Log internal warning (not shown to user)
 */
#define LOG_WARN_INTERNAL(msg, ...) \
    spdlog::warn("[INTERNAL] " msg, ##__VA_ARGS__)

// ============================================================================
// User-Facing Errors (Log + Toast Notification)
// ============================================================================

/**
 * @brief Report error with toast notification
 *
 * Logs error and shows non-blocking toast. Use for recoverable errors
 * that don't require immediate user action.
 */
#define NOTIFY_ERROR(msg, ...) do { \
    spdlog::error("[USER] " msg, ##__VA_ARGS__); \
    ui_notification_error(nullptr, fmt::format(msg, ##__VA_ARGS__).c_str(), false); \
} while(0)

/**
 * @brief Report warning with toast notification
 *
 * Logs warning and shows non-blocking toast. Use for potential issues
 * that user should be aware of.
 */
#define NOTIFY_WARNING(msg, ...) do { \
    spdlog::warn("[USER] " msg, ##__VA_ARGS__); \
    ui_notification_warning(fmt::format(msg, ##__VA_ARGS__).c_str()); \
} while(0)

/**
 * @brief Report info with toast notification
 */
#define NOTIFY_INFO(msg, ...) do { \
    spdlog::info("[USER] " msg, ##__VA_ARGS__); \
    ui_notification_info(fmt::format(msg, ##__VA_ARGS__).c_str()); \
} while(0)

/**
 * @brief Report success with toast notification
 */
#define NOTIFY_SUCCESS(msg, ...) do { \
    spdlog::info("[USER] " msg, ##__VA_ARGS__); \
    ui_notification_success(fmt::format(msg, ##__VA_ARGS__).c_str()); \
} while(0)

// ============================================================================
// Critical Errors (Log + Modal Dialog)
// ============================================================================

/**
 * @brief Report critical error with modal dialog
 *
 * Logs error and shows blocking modal dialog. Use for critical errors
 * that require user acknowledgment (connection failures, hardware errors).
 */
#define NOTIFY_ERROR_MODAL(title, msg, ...) do { \
    spdlog::error("[CRITICAL] {}: " msg, title, ##__VA_ARGS__); \
    ui_notification_error(title, fmt::format(msg, ##__VA_ARGS__).c_str(), true); \
} while(0)

// ============================================================================
// Context-Aware Error Reporting
// ============================================================================

/**
 * @brief RAII error context for operations that might fail
 *
 * Usage:
 * ```cpp
 * ErrorContext ctx("Save Configuration");
 * if (!save_to_disk()) {
 *     ctx.error("Disk write failed");  // Shows toast
 * }
 * if (hardware_fault) {
 *     ctx.critical("Hardware disconnected");  // Shows modal
 * }
 * ```
 */
class ErrorContext {
public:
    explicit ErrorContext(const char* operation)
        : operation_(operation) {}

    /**
     * @brief Report non-critical error in this context
     */
    void error(const char* details) {
        spdlog::error("[{}] {}", operation_, details);
        ui_notification_error(operation_, details, false);
    }

    /**
     * @brief Report critical error in this context
     */
    void critical(const char* details) {
        spdlog::error("[{}] CRITICAL: {}", operation_, details);
        ui_notification_error(operation_, details, true);
    }

    /**
     * @brief Report warning in this context
     */
    void warning(const char* details) {
        spdlog::warn("[{}] {}", operation_, details);
        ui_notification_warning(fmt::format("{}: {}", operation_, details).c_str());
    }

private:
    const char* operation_;
};
```

#### 4.2 Migration Examples

**Before:**
```cpp
if (!wifi_manager->connect(ssid, password)) {
    spdlog::error("Failed to connect to WiFi network: {}", ssid);
}
```

**After (Option 1 - Macro):**
```cpp
if (!wifi_manager->connect(ssid, password)) {
    NOTIFY_ERROR("Failed to connect to WiFi network: {}", ssid);
}
```

**After (Option 2 - Context):**
```cpp
ErrorContext ctx("WiFi Connection");
if (!wifi_manager->connect(ssid, password)) {
    ctx.error(fmt::format("Unable to connect to network '{}'", ssid).c_str());
}
```

---

## Migration Strategy

### Phase 1: Infrastructure (This Session)

**Goal:** Set up notification history and helper functions

**Tasks:**
1. ✅ Implement `NotificationHistory` class
2. ✅ Create history panel XML components
3. ✅ Implement history panel manager
4. ✅ Integrate with status bar
5. ✅ Create error reporting macros
6. ✅ (Optional) Implement custom spdlog sink

**Estimated Time:** 2-3 hours

### Phase 2: High-Priority Conversions (Next Session)

**Goal:** Convert critical user-facing errors

**Priority List:**
1. **Moonraker Connection** (~20 calls)
   - Connection failures
   - API errors
   - WebSocket disconnections

2. **WiFi Management** (~15 calls)
   - Connection failures
   - Scan errors
   - Authentication failures

3. **File Operations** (~25 calls)
   - Save configuration failures
   - Load errors
   - G-code file access errors

4. **Print Job Errors** (~10 calls)
   - Print start failures
   - Emergency stop events
   - Filament runout

**Approach:**
- Use `NOTIFY_ERROR` for recoverable errors
- Use `NOTIFY_ERROR_MODAL` for critical errors
- Use `NOTIFY_WARNING` for warnings

**Estimated Time:** 3-4 hours

### Phase 3: Comprehensive Audit (Future)

**Goal:** Review and convert all 505 spdlog calls

**Process:**
1. Generate list of all error/warn calls with context
2. Categorize each call:
   - **Internal** (leave as `spdlog::error`)
   - **User-Facing** (convert to `NOTIFY_*`)
   - **Critical** (convert to `NOTIFY_ERROR_MODAL`)
3. Add user-friendly messages where needed
4. Test notification flow for each conversion

**Estimated Time:** 8-10 hours (spread across multiple sessions)

---

## Testing Plan

### Unit Tests

**File:** `tests/unit/test_notification_history.cpp`

```cpp
TEST_CASE("NotificationHistory circular buffer", "[notification]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    // Add MAX_ENTRIES + 10 notifications
    for (size_t i = 0; i < NotificationHistory::MAX_ENTRIES + 10; i++) {
        NotificationHistoryEntry entry = {
            .timestamp_ms = i * 1000,
            .severity = ToastSeverity::INFO,
            .title = "",
            .message = "",
            .was_modal = false,
            .was_read = false
        };
        snprintf(entry.message, sizeof(entry.message), "Message %zu", i);
        history.add(entry);
    }

    // Should only have MAX_ENTRIES
    REQUIRE(history.count() == NotificationHistory::MAX_ENTRIES);

    // Oldest entries should be overwritten
    auto entries = history.get_all();
    REQUIRE(std::string(entries.back().message) == "Message 10");
}

TEST_CASE("NotificationHistory unread count", "[notification]") {
    NotificationHistory& history = NotificationHistory::instance();
    history.clear();

    // Add 5 notifications
    for (int i = 0; i < 5; i++) {
        NotificationHistoryEntry entry = {/* ... */};
        history.add(entry);
    }

    REQUIRE(history.get_unread_count() == 5);

    history.mark_all_read();
    REQUIRE(history.get_unread_count() == 0);
}
```

### Integration Tests

**Manual Testing Checklist:**
- [ ] Add notification → Appears in history panel
- [ ] Unread badge shows correct count
- [ ] Click notification history icon → Opens panel
- [ ] Filter by severity → Shows correct entries
- [ ] Clear all → Removes all entries
- [ ] Empty state shown when no notifications
- [ ] Timestamps display correctly
- [ ] Modal notifications marked as modal in history
- [ ] History persists across app restart (if enabled)
- [ ] Duplicate notifications suppressed (if enabled)

### Error Injection Tests

**Simulate Real Errors:**
```cpp
// Test WiFi error
NOTIFY_ERROR("Failed to connect to WiFi network: TestNetwork");

// Test critical error
NOTIFY_ERROR_MODAL("Hardware Failure", "Extruder temperature sensor disconnected");

// Test warning
NOTIFY_WARNING("Print bed temperature variance: ±2°C");

// Test multiple rapid errors (deduplication)
for (int i = 0; i < 5; i++) {
    NOTIFY_ERROR("Connection timeout");
}
// Should only show 1 notification
```

---

## Future Enhancements

### 1. Persistent Storage
- Save history to JSON on app exit
- Load on startup
- Configurable retention period (e.g., keep 7 days)

### 2. Notification Grouping
- Group similar notifications (e.g., "Connection lost (x3)")
- Expandable groups to see individual entries

### 3. Search & Export
- Search bar in history panel
- Export history as text/CSV for support tickets

### 4. Notification Channels
- Categorize by source (WiFi, Moonraker, Print Job, System)
- Per-channel notification settings (enable/disable, toast vs modal)

### 5. Smart Suggestions
- Suggest fixes based on error patterns
- "Connection lost → Troubleshoot Network"
- "Save failed → Check Disk Space"

### 6. Integration with Remote Monitoring
- Send critical notifications to mobile app (future)
- Email/SMS alerts for hardware failures

### 7. User Preferences
- Enable/disable notification history
- Adjust history size (50/100/200 entries)
- Auto-clear old entries after N days
- Notification sound/vibration (for devices with speakers)

---

## Warnings & Gotchas

### ⚠️ Performance Considerations

1. **Circular Buffer Lock Contention:**
   - History buffer uses mutex for thread safety
   - Avoid adding notifications in tight loops
   - Consider rate limiting (max 10/second)

2. **UI Refresh Performance:**
   - Refreshing 100+ list items can be slow
   - Use LVGL's virtual scrolling for large lists (future optimization)
   - Consider pagination (show 20 at a time)

3. **String Formatting Overhead:**
   - `fmt::format()` in macros allocates strings
   - For hot paths, check log level first:
     ```cpp
     if (spdlog::should_log(spdlog::level::err)) {
         NOTIFY_ERROR("Expensive format: {}", complex_object);
     }
     ```

### ⚠️ Thread Safety

1. **Background Thread Notifications:**
   - Moonraker callbacks run on background thread
   - WiFi scan callbacks run on background thread
   - LVGL is NOT thread-safe!

   **Solution:** Queue notifications for main thread
   ```cpp
   // In background thread:
   lv_async_call([](void* msg) {
       ui_notification_error(nullptr, (const char*)msg, false);
       free(msg);
   }, strdup(error_message));
   ```

2. **Notification Subject Access:**
   - `lv_subject_set_pointer()` should only be called from LVGL thread
   - Use `lv_async_call()` from background threads

### ⚠️ Memory Management

1. **Fixed-Size Buffers:**
   - History entries have fixed 256-char message limit
   - Truncate long messages gracefully:
     ```cpp
     strncpy(entry.message, message, sizeof(entry.message) - 1);
     entry.message[sizeof(entry.message) - 1] = '\0';
     ```

2. **XML String Attributes:**
   - Ensure strings passed to XML components are valid for component lifetime
   - Use static strings or carefully managed heap strings

### ⚠️ User Experience

1. **Notification Fatigue:**
   - Too many notifications → Users ignore them
   - Use deduplication aggressively
   - Consider rate limiting (max 5 toasts/minute)

2. **Message Quality:**
   - Avoid technical jargon: "ECONNREFUSED" → "Connection refused"
   - Provide actionable information: "Check printer power and network"
   - Be specific: "Failed to save" → "Failed to save printer configuration to disk"

3. **Modal Dialog Abuse:**
   - Use modals sparingly (only for critical errors)
   - Prefer toasts for most errors
   - Allow dismissing modals with ESC/backdrop click

### ⚠️ Backwards Compatibility

1. **Migration Path:**
   - Don't convert all 505 calls at once
   - Phase in conversions over multiple sessions
   - Test each conversion thoroughly

2. **Existing Error Handlers:**
   - Some code may catch errors and show custom dialogs
   - Avoid double-notifications
   - Consolidate on new notification system

### ⚠️ spdlog Sink Caveats

1. **Recursion Risk:**
   - If notification system logs internally, could cause recursion
   - Solution: Use `LOG_ERROR_INTERNAL` in notification code
   - Or disable sink for notification system's logger

2. **Performance Impact:**
   - Every log call goes through sink
   - Keep `sink_it_()` fast
   - Consider async logging for high-volume scenarios

### ⚠️ Testing Challenges

1. **Asynchronous Behavior:**
   - Toast auto-dismiss timers
   - Background thread notifications
   - Use LVGL test runner or manual verification

2. **State Cleanup:**
   - Clear history between tests
   - Reset unread count
   - Delete persistent storage file

---

## Additional Ideas & Notes

### Accessibility Considerations

1. **Screen Readers:**
   - Ensure notifications are accessible
   - Use ARIA-like attributes (future LVGL feature?)
   - Audible alerts for critical errors

2. **High Contrast Mode:**
   - Ensure severity colors are distinguishable
   - Test with different theme variants

### Localization

1. **Translatable Messages:**
   - Use translation keys instead of hardcoded strings
   - Example: `NOTIFY_ERROR(tr("error.wifi.connection_failed"), ssid);`
   - Translation system not yet implemented

### Analytics & Telemetry

1. **Error Tracking:**
   - Count frequency of each error type
   - Identify most common user issues
   - Send anonymized error reports (opt-in)

2. **User Behavior:**
   - Track notification dismissal rate
   - Identify ignored notifications
   - Optimize notification strategy

### Developer Tools

1. **Error Injection UI:**
   - Hidden debug panel to trigger test notifications
   - Test all severity levels
   - Test rapid notification scenarios

2. **Notification Replay:**
   - Save notification history to file
   - Replay sequence for debugging
   - Useful for reproducing user-reported issues

---

## Summary Checklist

### Implementation Checklist

**Phase 1 - Infrastructure:**
- [ ] Create `NotificationHistory` class
- [ ] Implement circular buffer with thread safety
- [ ] Add persistence (optional)
- [ ] Create `notification_history_panel.xml`
- [ ] Create `notification_history_item.xml`
- [ ] Implement `ui_panel_notification_history.cpp`
- [ ] Add unread badge to status bar
- [ ] Wire up history panel navigation
- [ ] Create error reporting macros (`ui_error_reporting.h`)
- [ ] (Optional) Implement custom spdlog sink

**Phase 2 - High-Priority Conversions:**
- [ ] Audit Moonraker error sites
- [ ] Convert Moonraker errors to `NOTIFY_*`
- [ ] Audit WiFi error sites
- [ ] Convert WiFi errors to `NOTIFY_*`
- [ ] Audit file I/O error sites
- [ ] Convert file errors to `NOTIFY_*`
- [ ] Test all conversions

**Phase 3 - Comprehensive Audit:**
- [ ] Generate full list of error/warn calls
- [ ] Categorize all 505 calls
- [ ] Convert user-facing errors
- [ ] Add user-friendly messages
- [ ] Final testing

### Testing Checklist

- [ ] Unit tests for `NotificationHistory`
- [ ] Unit tests for circular buffer
- [ ] Unit tests for unread count
- [ ] Integration test for history panel
- [ ] Integration test for status bar badge
- [ ] Error injection tests
- [ ] Deduplication tests
- [ ] Thread safety tests
- [ ] Performance profiling (100+ entries)
- [ ] Memory leak checks

### Documentation Checklist

- [x] Implementation plan (this document)
- [ ] API documentation (Doxygen)
- [ ] User guide (how to view history)
- [ ] Developer guide (how to add notifications)
- [ ] Migration guide (converting old errors)

---

## References

- **Existing Notification System:** `docs/ARCHITECTURE.md` (notification subject pattern)
- **LVGL Subject API:** `lib/lvgl/src/misc/lv_subject.h`
- **spdlog Documentation:** https://github.com/gabime/spdlog
- **Error Handling Best Practices:** https://isocpp.org/wiki/faq/exceptions

---

## Revision History

- **2025-01-XX:** Initial plan created based on notification system implementation
- **Future:** Update after Phase 1 completion
- **Future:** Update after comprehensive audit

---

**Next Steps:**
1. Review this plan with team/user
2. Get approval for Phase 1 scope
3. Begin implementation of `NotificationHistory` class
4. Create history panel UI
5. Test thoroughly before Phase 2

**Questions to Resolve:**
- Should history persist across restarts?
- Max history size (50/100/200 entries)?
- Use spdlog sink or manual conversion approach?
- Auto-notification for all errors or opt-in?
