// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_console.h"

#include "ui_error_reporting.h"
#include "ui_update_queue.h"
#include "ui_event_safety.h"
#include "ui_keyboard_manager.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"

#include "app_globals.h"
#include "moonraker_client.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

// Forward declarations for callbacks (registered in init_subjects)
static void on_console_row_clicked(lv_event_t* e);
static void on_console_send_clicked(lv_event_t* e);
static void on_console_clear_clicked(lv_event_t* e);

ConsolePanel::ConsolePanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    std::snprintf(status_buf_, sizeof(status_buf_), "Loading history...");
}

void ConsolePanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize status subject for reactive binding
    UI_SUBJECT_INIT_AND_REGISTER_STRING(status_subject_, status_buf_, status_buf_,
                                        "console_status");

    // Register callbacks
    lv_xml_register_event_cb(nullptr, "on_console_row_clicked", on_console_row_clicked);
    lv_xml_register_event_cb(nullptr, "on_console_send_clicked", on_console_send_clicked);
    lv_xml_register_event_cb(nullptr, "on_console_clear_clicked", on_console_clear_clicked);

    subjects_initialized_ = true;
    spdlog::debug("[{}] init_subjects() - registered callbacks", get_name());
}

void ConsolePanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up...", get_name());

    // Use standard overlay panel setup (wires header, back button, handles responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Find widget references
    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
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
        return;
    }

    if (!gcode_input_) {
        spdlog::warn("[{}] gcode_input not found - input disabled", get_name());
    }

    // Fetch initial history
    fetch_history();

    spdlog::info("[{}] Setup complete!", get_name());
}

void ConsolePanel::on_activate() {
    spdlog::debug("[{}] Panel activated", get_name());
    // Refresh history when panel becomes visible
    fetch_history();
    // Subscribe to real-time updates
    subscribe_to_gcode_responses();
    // Reset scroll tracking
    user_scrolled_up_ = false;
}

void ConsolePanel::on_deactivate() {
    spdlog::debug("[{}] Panel deactivated", get_name());
    // Unsubscribe from real-time updates
    unsubscribe_from_gcode_responses();
}

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

    // Create a simple label (not XML component for better performance with many entries)
    lv_obj_t* label = lv_label_create(console_container_);
    lv_label_set_text(label, entry.message.c_str());
    lv_obj_set_width(label, LV_PCT(100));

    // Apply color based on entry type (same pattern as ui_icon.cpp)
    lv_color_t color;
    if (entry.is_error) {
        color = ui_theme_get_color("error_color");
    } else if (entry.type == GcodeEntry::Type::RESPONSE) {
        color = ui_theme_get_color("success_color");
    } else {
        // Commands use primary text color
        color = UI_COLOR_TEXT_PRIMARY;
    }
    lv_obj_set_style_text_color(label, color, 0);

    // Use small font from theme (responsive)
    lv_obj_set_style_text_font(label, UI_FONT_SMALL, 0);
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
    if (console_container_) {
        if (has_entries) {
            lv_obj_remove_flag(console_container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(console_container_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (empty_state_) {
        if (has_entries) {
            lv_obj_add_flag(empty_state_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(empty_state_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update status message
    if (has_entries) {
        std::snprintf(status_buf_, sizeof(status_buf_), "%zu entries", entries_.size());
    } else {
        std::snprintf(status_buf_, sizeof(status_buf_), "");
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

    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        spdlog::debug("[{}] Cannot subscribe - no client", get_name());
        return;
    }

    // Generate unique handler name
    static std::atomic<uint64_t> s_handler_id{0};
    gcode_handler_name_ = "console_panel_" + std::to_string(++s_handler_id);

    // Register for notify_gcode_response notifications
    // Capture 'this' safely since we unregister in on_deactivate()
    client->register_method_callback("notify_gcode_response", gcode_handler_name_,
                                     [this](const nlohmann::json& msg) { on_gcode_response(msg); });

    is_subscribed_ = true;
    spdlog::debug("[{}] Subscribed to notify_gcode_response (handler: {})", get_name(),
                  gcode_handler_name_);
}

void ConsolePanel::unsubscribe_from_gcode_responses() {
    if (!is_subscribed_) {
        return;
    }

    MoonrakerClient* client = get_moonraker_client();
    if (client) {
        client->unregister_method_callback("notify_gcode_response", gcode_handler_name_);
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

    // CRITICAL: Defer LVGL operations to main thread via ui_async_call [L012]
    // WebSocket callbacks run on libhv thread - direct LVGL calls cause crashes
    struct Ctx {
        ConsolePanel* panel;
        GcodeEntry entry;
    };
    auto* ctx = new Ctx{this, std::move(entry)};
    ui_async_call(
        [](void* user_data) {
            auto* c = static_cast<Ctx*>(user_data);
            c->panel->add_entry(c->entry);
            delete c;
        },
        ctx);
}

void ConsolePanel::add_entry(const GcodeEntry& entry) {
    // Add to deque
    entries_.push_back(entry);

    // Enforce max size (remove oldest)
    while (entries_.size() > MAX_ENTRIES && console_container_) {
        entries_.pop_front();
        // Remove oldest widget (first child)
        lv_obj_t* first_child = lv_obj_get_child(console_container_, 0);
        if (first_child) {
            lv_obj_delete(first_child);
        }
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

    // Send via MoonrakerClient
    MoonrakerClient* client = get_moonraker_client();
    if (client) {
        int result = client->gcode_script(command);
        if (result < 0) {
            spdlog::error("[{}] Failed to send G-code command", get_name());
            // Add error response to console
            GcodeEntry err_entry;
            err_entry.message = "!! Failed to send command";
            err_entry.type = GcodeEntry::Type::RESPONSE;
            err_entry.is_error = true;
            add_entry(err_entry);
        }
    } else {
        spdlog::warn("[{}] No MoonrakerClient available", get_name());
    }
}

void ConsolePanel::clear_display() {
    spdlog::debug("[{}] Clearing console display", get_name());
    clear_entries();
    update_visibility();
}

// ============================================================================
// Global Instance and Row Click Handler
// ============================================================================

static std::unique_ptr<ConsolePanel> g_console_panel;
static lv_obj_t* g_console_panel_obj = nullptr;

ConsolePanel& get_global_console_panel() {
    if (!g_console_panel) {
        spdlog::error("[Console Panel] get_global_console_panel() called before initialization!");
        throw std::runtime_error("ConsolePanel not initialized");
    }
    return *g_console_panel;
}

void init_global_console_panel(PrinterState& printer_state, MoonrakerAPI* api) {
    if (g_console_panel) {
        spdlog::warn("[Console Panel] ConsolePanel already initialized, skipping");
        return;
    }
    g_console_panel = std::make_unique<ConsolePanel>(printer_state, api);
    spdlog::debug("[Console Panel] ConsolePanel initialized");
}

/**
 * @brief Row click handler for opening console from Advanced panel
 *
 * Registered via lv_xml_register_event_cb() in init_subjects().
 * Lazy-creates the console panel on first click.
 */
static void on_console_row_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Console Panel] Console row clicked");

    if (!g_console_panel) {
        spdlog::error("[Console Panel] Global instance not initialized!");
        return;
    }

    // Lazy-create the console panel
    if (!g_console_panel_obj) {
        spdlog::debug("[Console Panel] Creating console panel...");
        g_console_panel_obj = static_cast<lv_obj_t*>(
            lv_xml_create(lv_display_get_screen_active(NULL), "console_panel", nullptr));

        if (g_console_panel_obj) {
            g_console_panel->setup(g_console_panel_obj, lv_display_get_screen_active(NULL));
            lv_obj_add_flag(g_console_panel_obj, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[Console Panel] Panel created and setup complete");
        } else {
            spdlog::error("[Console Panel] Failed to create console_panel");
            return;
        }
    }

    // Show the overlay
    ui_nav_push_overlay(g_console_panel_obj);
}

/**
 * @brief Send button click handler
 *
 * Registered via lv_xml_register_event_cb() in init_subjects().
 * Sends the current G-code command from the input field.
 */
static void on_console_send_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Console Panel] Send button clicked");

    if (!g_console_panel) {
        spdlog::error("[Console Panel] Global instance not initialized!");
        return;
    }

    g_console_panel->send_gcode_command();
}

/**
 * @brief Clear button click handler
 *
 * Registered via lv_xml_register_event_cb() in init_subjects().
 * Clears all entries from the console display.
 */
static void on_console_clear_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Console Panel] Clear button clicked");

    if (!g_console_panel) {
        spdlog::error("[Console Panel] Global instance not initialized!");
        return;
    }

    g_console_panel->clear_display();
}
