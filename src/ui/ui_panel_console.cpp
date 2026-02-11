// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_console.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_global_panel_helper.h"
#include "ui_keyboard_manager.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

// ============================================================================
// Global Instance
// ============================================================================

DEFINE_GLOBAL_PANEL(ConsolePanel, g_console_panel, get_global_console_panel)

// ============================================================================
// HTML Span Parsing (for AFC/Happy Hare colored output)
// ============================================================================

namespace {

/**
 * @brief Parsed text segment with optional color class
 */
struct TextSegment {
    std::string text;
    std::string color_class; // empty = default, "success", "info", "warning", "error"
};

/**
 * @brief Check if a message contains HTML spans we can parse
 *
 * Looks for Mainsail-style spans from AFC/Happy Hare plugins:
 * <span class=success--text>LOADED</span>
 */
bool contains_html_spans(const std::string& message) {
    return message.find("<span class=") != std::string::npos &&
           (message.find("success--text") != std::string::npos ||
            message.find("info--text") != std::string::npos ||
            message.find("warning--text") != std::string::npos ||
            message.find("error--text") != std::string::npos);
}

/**
 * @brief Parse HTML span tags into text segments with color classes
 *
 * Parses Mainsail-style spans: <span class=XXX--text>content</span>
 * Returns vector of segments, each with text and optional color class.
 */
std::vector<TextSegment> parse_html_spans(const std::string& message) {
    std::vector<TextSegment> segments;

    size_t pos = 0;
    const size_t len = message.size();

    while (pos < len) {
        // Look for next <span class=
        size_t span_start = message.find("<span class=", pos);

        if (span_start == std::string::npos) {
            // No more spans - add remaining text as plain segment
            if (pos < len) {
                TextSegment seg;
                seg.text = message.substr(pos);
                if (!seg.text.empty()) {
                    segments.push_back(seg);
                }
            }
            break;
        }

        // Add any text before the span as a plain segment
        if (span_start > pos) {
            TextSegment seg;
            seg.text = message.substr(pos, span_start - pos);
            segments.push_back(seg);
        }

        // Parse the span: <span class=XXX--text>content</span>
        // Find the class value (ends at >)
        size_t class_start = span_start + 12; // strlen("<span class=")
        size_t class_end = message.find('>', class_start);

        if (class_end == std::string::npos) {
            // Malformed - add rest as plain text
            TextSegment seg;
            seg.text = message.substr(span_start);
            segments.push_back(seg);
            break;
        }

        // Extract color class from "success--text", "info--text", etc.
        std::string class_attr = message.substr(class_start, class_end - class_start);
        std::string color_class;

        if (class_attr.find("success--text") != std::string::npos) {
            color_class = "success";
        } else if (class_attr.find("info--text") != std::string::npos) {
            color_class = "info";
        } else if (class_attr.find("warning--text") != std::string::npos) {
            color_class = "warning";
        } else if (class_attr.find("error--text") != std::string::npos) {
            color_class = "error";
        }

        // Find the closing </span>
        size_t content_start = class_end + 1;
        size_t span_close = message.find("</span>", content_start);

        if (span_close == std::string::npos) {
            // No closing tag - add rest as plain text
            TextSegment seg;
            seg.text = message.substr(content_start);
            seg.color_class = color_class;
            segments.push_back(seg);
            break;
        }

        // Extract content between > and </span>
        TextSegment seg;
        seg.text = message.substr(content_start, span_close - content_start);
        seg.color_class = color_class;
        if (!seg.text.empty()) {
            segments.push_back(seg);
        }

        // Move past </span>
        pos = span_close + 7; // strlen("</span>")
    }

    return segments;
}

} // namespace

// ============================================================================
// Constructor
// ============================================================================

ConsolePanel::ConsolePanel() {
    spdlog::trace("[{}] Constructor", get_name());
    std::memset(status_buf_, 0, sizeof(status_buf_));
}

ConsolePanel::~ConsolePanel() {
    deinit_subjects();
}

// ============================================================================
// Subject Initialization
// ============================================================================

void ConsolePanel::init_subjects() {
    init_subjects_guarded([this]() {
        // Initialize status subject for reactive binding
        UI_MANAGED_SUBJECT_STRING(status_subject_, status_buf_, "Loading history...",
                                  "console_status", subjects_);
    });
}

void ConsolePanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[{}] Subjects deinitialized", get_name());
}

// ============================================================================
// Callback Registration
// ============================================================================

void ConsolePanel::register_callbacks() {
    if (callbacks_registered_) {
        spdlog::debug("[{}] Callbacks already registered", get_name());
        return;
    }

    spdlog::debug("[{}] Registering event callbacks", get_name());

    // Register XML event callbacks for send and clear buttons
    lv_xml_register_event_cb(nullptr, "on_console_send_clicked", [](lv_event_t* /*e*/) {
        spdlog::debug("[Console] Send button clicked");
        get_global_console_panel().send_gcode_command();
    });
    lv_xml_register_event_cb(nullptr, "on_console_clear_clicked", [](lv_event_t* /*e*/) {
        spdlog::debug("[Console] Clear button clicked");
        get_global_console_panel().clear_display();
    });

    callbacks_registered_ = true;
    spdlog::debug("[{}] Event callbacks registered", get_name());
}

// ============================================================================
// Create
// ============================================================================

lv_obj_t* ConsolePanel::create(lv_obj_t* parent) {
    if (!create_overlay_from_xml(parent, "console_panel")) {
        return nullptr;
    }

    // Find widget references
    lv_obj_t* overlay_content = lv_obj_find_by_name(overlay_root_, "overlay_content");
    if (overlay_content) {
        console_container_ = lv_obj_find_by_name(overlay_content, "console_container");
        empty_state_ = lv_obj_find_by_name(overlay_content, "empty_state");
        status_label_ = lv_obj_find_by_name(overlay_content, "status_message");

        // Find the input row and get the text input
        lv_obj_t* input_row = lv_obj_find_by_name(overlay_content, "input_row");
        if (input_row) {
            gcode_input_ = lv_obj_find_by_name(input_row, "gcode_input");
            if (gcode_input_) {
                // Register textarea for keyboard integration
                ui_keyboard_register_textarea(gcode_input_);
                spdlog::debug("[{}] Registered gcode_input for keyboard", get_name());
            }
        }
    }

    if (!console_container_) {
        spdlog::error("[{}] console_container not found!", get_name());
        return nullptr;
    }

    if (!gcode_input_) {
        spdlog::warn("[{}] gcode_input not found - input disabled", get_name());
    }

    spdlog::info("[{}] Overlay created successfully", get_name());
    return overlay_root_;
}

// ============================================================================
// Lifecycle Hooks
// ============================================================================

void ConsolePanel::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[{}] on_activate()", get_name());

    // Refresh history when panel becomes visible
    fetch_history();
    // Subscribe to real-time updates
    subscribe_to_gcode_responses();
    // Reset scroll tracking
    user_scrolled_up_ = false;
}

void ConsolePanel::on_deactivate() {
    spdlog::debug("[{}] on_deactivate()", get_name());

    // Unsubscribe from real-time updates
    unsubscribe_from_gcode_responses();

    // Call base class
    OverlayBase::on_deactivate();
}

// ============================================================================
// Data Loading
// ============================================================================

void ConsolePanel::fetch_history() {
    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        spdlog::warn("[{}] No MoonrakerClient available", get_name());
        std::snprintf(status_buf_, sizeof(status_buf_), "Not connected to printer");
        lv_subject_copy_string(&status_subject_, status_buf_);
        update_visibility();
        return;
    }

    // Update status while loading
    std::snprintf(status_buf_, sizeof(status_buf_), "Loading...");
    lv_subject_copy_string(&status_subject_, status_buf_);

    // Request gcode history from Moonraker
    client->get_gcode_store(
        FETCH_COUNT,
        [this](const std::vector<MoonrakerClient::GcodeStoreEntry>& entries) {
            spdlog::info("[{}] Received {} gcode entries", get_name(), entries.size());

            // Convert to our entry format
            std::vector<GcodeEntry> converted;
            converted.reserve(entries.size());

            for (const auto& entry : entries) {
                GcodeEntry e;
                e.message = entry.message;
                e.timestamp = entry.time;
                e.type = (entry.type == "command") ? GcodeEntry::Type::COMMAND
                                                   : GcodeEntry::Type::RESPONSE;
                e.is_error = is_error_message(entry.message);
                converted.push_back(e);
            }

            populate_entries(converted);
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to fetch gcode store: {}", get_name(), err.message);
            std::snprintf(status_buf_, sizeof(status_buf_), "Failed to load history");
            lv_subject_copy_string(&status_subject_, status_buf_);
            update_visibility();
        });
}

void ConsolePanel::populate_entries(const std::vector<GcodeEntry>& entries) {
    clear_entries();

    // Store entries (already oldest-first from API)
    for (const auto& entry : entries) {
        entries_.push_back(entry);

        // Enforce max size (remove oldest)
        if (entries_.size() > MAX_ENTRIES) {
            entries_.pop_front();
        }
    }

    // Create widgets for each entry
    for (const auto& entry : entries_) {
        create_entry_widget(entry);
    }

    // Update visibility and scroll to bottom
    update_visibility();
    scroll_to_bottom();
}

void ConsolePanel::create_entry_widget(const GcodeEntry& entry) {
    if (!console_container_) {
        return;
    }

    const lv_font_t* font = theme_manager_get_font("font_small");

    if (contains_html_spans(entry.message)) {
        // Create spangroup for rich text with colored segments
        lv_obj_t* spangroup = lv_spangroup_create(console_container_);
        lv_obj_set_width(spangroup, LV_PCT(100));
        lv_obj_set_style_text_font(spangroup, font, 0);

        auto segments = parse_html_spans(entry.message);
        for (const auto& seg : segments) {
            lv_span_t* span = lv_spangroup_add_span(spangroup);
            lv_span_set_text(span, seg.text.c_str());

            // Determine color based on segment's color class
            lv_color_t color;
            if (seg.color_class == "success") {
                color = theme_manager_get_color("success");
            } else if (seg.color_class == "info") {
                color = theme_manager_get_color("info");
            } else if (seg.color_class == "warning") {
                color = theme_manager_get_color("warning");
            } else if (seg.color_class == "error") {
                color = theme_manager_get_color("danger");
            } else {
                // Default color based on entry type
                color = entry.is_error ? theme_manager_get_color("danger")
                        : entry.type == GcodeEntry::Type::RESPONSE
                            ? theme_manager_get_color("success")
                            : theme_manager_get_color("text");
            }
            lv_style_set_text_color(lv_span_get_style(span), color);
        }
        lv_spangroup_refresh(spangroup);
    } else {
        // Plain label for non-HTML messages (faster, simpler)
        lv_obj_t* label = lv_label_create(console_container_);
        lv_label_set_text(label, entry.message.c_str());
        lv_obj_set_width(label, LV_PCT(100));

        // Apply color based on entry type
        lv_color_t color;
        if (entry.is_error) {
            color = theme_manager_get_color("danger");
        } else if (entry.type == GcodeEntry::Type::RESPONSE) {
            color = theme_manager_get_color("success");
        } else {
            // Commands use primary text color
            color = theme_manager_get_color("text");
        }
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_set_style_text_font(label, font, 0);
    }
}

void ConsolePanel::clear_entries() {
    entries_.clear();

    if (console_container_) {
        lv_obj_clean(console_container_);
    }
}

void ConsolePanel::scroll_to_bottom() {
    if (console_container_) {
        lv_obj_scroll_to_y(console_container_, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

bool ConsolePanel::is_error_message(const std::string& message) {
    if (message.empty()) {
        return false;
    }

    // Klipper errors typically start with "!!" or "Error:"
    if (message.size() >= 2 && message[0] == '!' && message[1] == '!') {
        return true;
    }

    // Case-insensitive check for "error" at start
    if (message.size() >= 5) {
        std::string lower = message.substr(0, 5);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower == "error") {
            return true;
        }
    }

    return false;
}

bool ConsolePanel::is_temp_message(const std::string& message) {
    if (message.empty()) {
        return false;
    }

    // Temperature status messages look like:
    // "ok T:210.5 /210.0 B:60.2 /60.0"
    // "T:210.5 /210.0 B:60.2 /60.0"
    // "ok B:60.0 /60.0 T0:210.0 /210.0"

    // Look for temperature patterns: T: or B: followed by numbers
    // Simple heuristic: contains "T:" or "B:" with "/" nearby
    size_t t_pos = message.find("T:");
    size_t b_pos = message.find("B:");

    if (t_pos != std::string::npos || b_pos != std::string::npos) {
        // Check for temperature format: number / number
        size_t slash_pos = message.find('/');
        if (slash_pos != std::string::npos) {
            // Very likely a temperature status message
            return true;
        }
    }

    return false;
}

void ConsolePanel::update_visibility() {
    bool has_entries = !entries_.empty();

    // Toggle visibility: show console OR empty state
    ui_toggle_list_empty_state(console_container_, empty_state_, has_entries);

    // Update status message
    if (has_entries) {
        std::snprintf(status_buf_, sizeof(status_buf_), "%zu entries", entries_.size());
    } else {
        status_buf_[0] = '\0'; // Clear status text
    }
    lv_subject_copy_string(&status_subject_, status_buf_);
}

// ============================================================================
// Real-time G-code Response Streaming
// ============================================================================

void ConsolePanel::subscribe_to_gcode_responses() {
    if (is_subscribed_) {
        return;
    }

    MoonrakerAPI* api = get_moonraker_api();
    if (!api) {
        spdlog::debug("[{}] Cannot subscribe - no API", get_name());
        return;
    }

    // Generate unique handler name
    static std::atomic<uint64_t> s_handler_id{0};
    gcode_handler_name_ = "console_panel_" + std::to_string(++s_handler_id);

    // Register for notify_gcode_response notifications
    // Capture 'this' safely since we unregister in on_deactivate()
    api->register_method_callback("notify_gcode_response", gcode_handler_name_,
                                  [this](const nlohmann::json& msg) { on_gcode_response(msg); });

    is_subscribed_ = true;
    spdlog::debug("[{}] Subscribed to notify_gcode_response (handler: {})", get_name(),
                  gcode_handler_name_);
}

void ConsolePanel::unsubscribe_from_gcode_responses() {
    if (!is_subscribed_) {
        return;
    }

    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        api->unregister_method_callback("notify_gcode_response", gcode_handler_name_);
        spdlog::debug("[{}] Unsubscribed from notify_gcode_response", get_name());
    }

    is_subscribed_ = false;
    gcode_handler_name_.clear();
}

void ConsolePanel::on_gcode_response(const nlohmann::json& msg) {
    // Parse notify_gcode_response format: {"method": "...", "params": ["line"]}
    if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
        return;
    }

    const std::string& line = msg["params"][0].get_ref<const std::string&>();

    // Skip empty lines and common noise
    if (line.empty() || line == "ok") {
        return;
    }

    // Filter temperature status messages if enabled
    if (filter_temps_ && is_temp_message(line)) {
        return;
    }

    // Create entry for this response
    GcodeEntry entry;
    entry.message = line;
    entry.timestamp = 0.0; // Real-time entries don't have timestamps
    entry.type = GcodeEntry::Type::RESPONSE;
    entry.is_error = is_error_message(line);

    // CRITICAL: Defer LVGL operations to main thread via ui_queue_update [L012]
    // WebSocket callbacks run on libhv thread - direct LVGL calls cause crashes
    struct Ctx {
        ConsolePanel* panel;
        GcodeEntry entry;
    };
    auto ctx = std::make_unique<Ctx>(Ctx{this, std::move(entry)});
    ui_queue_update<Ctx>(std::move(ctx), [](Ctx* c) { c->panel->add_entry(c->entry); });
}

void ConsolePanel::add_entry(const GcodeEntry& entry) {
    // Add to deque
    entries_.push_back(entry);

    // Enforce max size (remove oldest)
    while (entries_.size() > MAX_ENTRIES && console_container_) {
        entries_.pop_front();
        // Remove oldest widget (first child)
        lv_obj_t* first_child = lv_obj_get_child(console_container_, 0);
        lv_obj_safe_delete(first_child);
    }

    // Create widget for new entry
    create_entry_widget(entry);

    // Update visibility state
    update_visibility();

    // Smart auto-scroll: only scroll if user hasn't scrolled up manually
    if (!user_scrolled_up_) {
        scroll_to_bottom();
    }
}

void ConsolePanel::send_gcode_command() {
    if (!gcode_input_) {
        spdlog::warn("[{}] Cannot send - no input field", get_name());
        return;
    }

    // Get text from input
    const char* text = lv_textarea_get_text(gcode_input_);
    if (!text || text[0] == '\0') {
        spdlog::debug("[{}] Empty command, ignoring", get_name());
        return;
    }

    std::string command(text);
    spdlog::info("[{}] Sending G-code: {}", get_name(), command);

    // Clear the input field immediately
    lv_textarea_set_text(gcode_input_, "");

    // Add command to console display
    GcodeEntry cmd_entry;
    cmd_entry.message = command;
    cmd_entry.timestamp = 0.0;
    cmd_entry.type = GcodeEntry::Type::COMMAND;
    cmd_entry.is_error = false;
    add_entry(cmd_entry);

    // Send via MoonrakerAPI (fire-and-forget for console commands)
    MoonrakerAPI* api = get_moonraker_api();
    if (api) {
        api->execute_gcode(command, nullptr, nullptr);
    } else {
        spdlog::warn("[{}] No MoonrakerAPI available", get_name());
    }
}

void ConsolePanel::clear_display() {
    spdlog::debug("[{}] Clearing console display", get_name());
    clear_entries();
    update_visibility();
}
