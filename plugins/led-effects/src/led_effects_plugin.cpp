// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen
//
// LED Effects Plugin - Proof-of-concept for HelixScreen plugin system
// Demonstrates: init/deinit, event subscription, subject registration,
// XML widget injection, and gcode execution.

#include "injection_point_manager.h"
#include "moonraker_api.h"
#include "plugin_api.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

using namespace helix::plugin;

// ============================================================================
// Plugin State
// ============================================================================

static PluginAPI* g_api = nullptr;
static std::string g_plugin_dir;
static lv_subject_t s_led_mode; // 0=off, 1=on, 2=rainbow
static bool s_led_on = false;   // Simple toggle state

// ============================================================================
// Event Callbacks
// ============================================================================

static void led_toggle_cb(lv_event_t* e) {
    (void)e; // Unused

    if (!g_api) {
        spdlog::warn("[LED Effects] Toggle called but API is null");
        return;
    }

    // Toggle LED state
    s_led_on = !s_led_on;

    // Update subject for UI binding
    lv_subject_set_int(&s_led_mode, s_led_on ? 1 : 0);

    // Send gcode to control LED via Moonraker
    auto* moonraker = g_api->moonraker_api();
    if (moonraker) {
        // TODO: Get LED name from PrinterState capabilities or printer_database.json
        // Currently hardcoded to "chamber_light" - works for Adventurer 5M but not all printers.
        // Future: Query available LEDs from PrinterState and use first available,
        // or allow user configuration via plugin settings.
        const char* led_name = "chamber_light";

        std::string gcode;
        if (s_led_on) {
            // Turn on - white at full brightness
            gcode = fmt::format("SET_LED LED={} RED=1.0 GREEN=1.0 BLUE=1.0", led_name);
        } else {
            // Turn off
            gcode = fmt::format("SET_LED LED={} RED=0 GREEN=0 BLUE=0", led_name);
        }

        moonraker->execute_gcode(
            gcode, []() { spdlog::info("[LED Effects] LED command executed successfully"); },
            [](const MoonrakerError& err) {
                spdlog::warn("[LED Effects] LED command failed: {}", err.message);
            });

        g_api->log_info(s_led_on ? "LED turned ON" : "LED turned OFF");
    } else {
        g_api->log_warn("Moonraker not connected - cannot control LED");
    }
}

// ============================================================================
// Plugin Entry Points
// ============================================================================

extern "C" bool helix_plugin_init(PluginAPI* api, const char* plugin_dir) {
    if (!api) {
        spdlog::error("[LED Effects] Plugin init called with null API");
        return false;
    }

    g_api = api;
    g_plugin_dir = plugin_dir ? plugin_dir : "";

    api->log_info("LED Effects plugin initializing...");
    spdlog::debug("[LED Effects] Plugin directory: {}", g_plugin_dir);

    // Initialize subject for LED mode (0=off, 1=on, 2=rainbow)
    lv_subject_init_int(&s_led_mode, 0);
    api->register_subject("led_effects.mode", &s_led_mode);
    spdlog::debug("[LED Effects] Registered subject: led_effects.mode");

    // Register XML event callback BEFORE registering the component
    // This is critical - callbacks must be available when XML is parsed
    lv_xml_register_event_cb(nullptr, "led_toggle_cb", led_toggle_cb);
    spdlog::debug("[LED Effects] Registered event callback: led_toggle_cb");

    // Register XML component from plugin directory
    if (!g_plugin_dir.empty()) {
        if (!api->register_xml_component(g_plugin_dir, "ui_xml/led_widget.xml")) {
            api->log_error("Failed to register led_widget.xml component");
            // Continue anyway - the widget just won't appear
        } else {
            spdlog::debug("[LED Effects] Registered XML component: led_widget");
        }
    } else {
        api->log_warn("Plugin directory not provided - cannot register XML component");
    }

    // Inject widget into home panel's widget area
    WidgetCallbacks callbacks;
    callbacks.on_create = [](lv_obj_t* widget) {
        spdlog::info("[LED Effects] Widget created at {:p}", static_cast<void*>(widget));
    };
    callbacks.on_destroy = [](lv_obj_t* widget) {
        spdlog::info("[LED Effects] Widget destroyed at {:p}", static_cast<void*>(widget));
    };

    // Check if injection point is available and inject
    if (api->has_injection_point("home_widget_area")) {
        if (api->inject_widget("home_widget_area", "led_widget", callbacks)) {
            api->log_info("Widget injected into home_widget_area");
        } else {
            api->log_warn("Failed to inject widget into home_widget_area");
        }
    } else {
        // Injection point not yet registered - this is normal if home panel
        // hasn't loaded yet. The widget won't appear until home panel loads.
        spdlog::debug("[LED Effects] home_widget_area not yet available");
    }

    // Subscribe to printer connection events
    api->on_event(events::PRINTER_CONNECTED, [](const EventData& e) {
        (void)e;
        if (g_api) {
            g_api->log_info("Printer connected - LED control available");
        }
    });

    // Subscribe to print events (example of event handling)
    api->on_event(events::PRINT_STARTED, [](const EventData& e) {
        if (g_api) {
            std::string filename = "unknown";
            if (e.payload.contains("filename")) {
                filename = e.payload["filename"].get<std::string>();
            }
            g_api->log_info("Print started: " + filename + " - LED effect could trigger here");
        }
    });

    api->on_event(events::PRINT_COMPLETED, [](const EventData& e) {
        (void)e;
        if (g_api) {
            g_api->log_info("Print completed - could flash LEDs for celebration");
        }
    });

    api->log_info("LED Effects plugin initialized successfully");
    return true;
}

extern "C" void helix_plugin_deinit() {
    if (g_api) {
        g_api->log_info("LED Effects plugin shutting down");
    }

    // Deinitialize subject (cleanup observer notifications)
    // Note: registered subjects are automatically unregistered by PluginAPI cleanup
    lv_subject_deinit(&s_led_mode);

    g_api = nullptr;
    g_plugin_dir.clear();
    s_led_on = false;

    spdlog::debug("[LED Effects] Plugin deinitialized");
}

extern "C" const char* helix_plugin_api_version() {
    return PLUGIN_API_VERSION; // "1.0" from plugin_api.h
}
