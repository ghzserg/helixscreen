// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_discovery_sequence.h"

#include "ui_update_queue.h"

#include "app_globals.h"
#include "helix_version.h"
#include "led/led_controller.h"
#include "moonraker_client.h"
#include "printer_state.h"

#include <algorithm>

namespace helix {

MoonrakerDiscoverySequence::MoonrakerDiscoverySequence(MoonrakerClient& client) : client_(client) {}

void MoonrakerDiscoverySequence::clear_cache() {
    heaters_.clear();
    sensors_.clear();
    fans_.clear();
    leds_.clear();
    steppers_.clear();
    afc_objects_.clear();
    filament_sensors_.clear();
    hardware_ = PrinterDiscovery{};
}

bool MoonrakerDiscoverySequence::is_stale() const {
    return client_.connection_generation() != discovery_generation_;
}

void MoonrakerDiscoverySequence::start(std::function<void()> on_complete,
                                       std::function<void(const std::string& reason)> on_error) {
    spdlog::debug("[Moonraker Client] Starting printer auto-discovery");

    // Store callbacks and snapshot the connection generation for stale detection
    on_complete_discovery_ = std::move(on_complete);
    on_error_discovery_ = std::move(on_error);
    discovery_generation_ = client_.connection_generation();

    // Step 0: Identify ourselves to Moonraker to enable receiving notifications
    // Skip if we've already identified on this connection (e.g., wizard tested, then completed)
    if (identified_.load()) {
        spdlog::debug("[Moonraker Client] Already identified, skipping identify step");
        continue_discovery();
        return;
    }

    json identify_params = {{"client_name", "HelixScreen"},
                            {"version", HELIX_VERSION},
                            {"type", "display"},
                            {"url", "https://github.com/helixscreen/helixscreen"}};

    client_.send_jsonrpc(
        "server.connection.identify", identify_params,
        [this](json identify_response) {
            if (is_stale())
                return;

            if (identify_response.contains("result")) {
                auto conn_id = identify_response["result"].value("connection_id", 0);
                spdlog::info("[Moonraker Client] Identified to Moonraker (connection_id: {})",
                             conn_id);
                identified_.store(true);
            } else if (identify_response.contains("error")) {
                // Log but continue - older Moonraker versions may not support this
                spdlog::warn("[Moonraker Client] Failed to identify: {}",
                             identify_response["error"].dump());
            }

            // Continue with discovery regardless of identify result
            continue_discovery();
        },
        [this](const MoonrakerError& err) {
            if (is_stale())
                return;

            // Log but continue - identify is not strictly required
            spdlog::warn("[Moonraker Client] Identify request failed: {}", err.message);
            continue_discovery();
        });
}

void MoonrakerDiscoverySequence::continue_discovery() {
    // Step 1: Check Klippy readiness via server.info before querying printer objects.
    // When Klippy is in STARTUP state, printer.objects.list returns JSON-RPC error
    // -32601 "Method not found", causing confusing error toasts. Gate here instead.
    client_.send_jsonrpc(
        "server.info", json(),
        [this](json server_info_response) {
            if (is_stale())
                return;

            // Extract klippy_state from response
            std::string klippy_state = "unknown";
            if (server_info_response.contains("result") &&
                server_info_response["result"].contains("klippy_state")) {
                klippy_state = server_info_response["result"]["klippy_state"].get<std::string>();
            }

            spdlog::debug("[Moonraker Client] Klippy state gate check: {}", klippy_state);

            // Allow "ready" and "shutdown" — both have valid Klipper objects.
            // Abort on "startup", "error", or unknown states.
            if (klippy_state != "ready" && klippy_state != "shutdown") {
                std::string reason = fmt::format("Klippy not ready (state: {})", klippy_state);
                spdlog::warn("[Moonraker Client] {}", reason);
                client_.emit_event(MoonrakerEventType::DISCOVERY_FAILED, reason, true);
                if (on_error_discovery_) {
                    auto cb = std::move(on_error_discovery_);
                    on_complete_discovery_ = nullptr;
                    cb(reason);
                }
                return;
            }

            // Klippy is ready/shutdown — proceed to query printer objects
            continue_discovery_objects();
        },
        [this](const MoonrakerError& err) {
            if (is_stale())
                return;
            // server.info failed — cannot determine Klippy state, abort discovery
            spdlog::error("[Moonraker Client] server.info request failed: {}", err.message);
            client_.emit_event(MoonrakerEventType::DISCOVERY_FAILED, err.message, true);
            if (on_error_discovery_) {
                auto cb = std::move(on_error_discovery_);
                on_complete_discovery_ = nullptr;
                cb(err.message);
            }
        });
}

void MoonrakerDiscoverySequence::continue_discovery_objects() {
    // Step 2: Query available printer objects (no params required)
    // Silent=true to suppress error toast if Klippy goes away between gate and this call
    client_.send_jsonrpc(
        "printer.objects.list", json(),
        [this](json response) {
            if (is_stale())
                return;
            // Debug: Log raw response
            spdlog::debug("[Moonraker Client] printer.objects.list response: {}", response.dump());

            // Validate response
            if (!response.contains("result") || !response["result"].contains("objects")) {
                // Extract error message from response if available
                std::string error_reason = "Failed to query printer objects from Moonraker";
                if (response.contains("error") && response["error"].contains("message")) {
                    error_reason = response["error"]["message"].get<std::string>();
                    spdlog::error("[Moonraker Client] printer.objects.list failed: {}",
                                  error_reason);
                } else {
                    spdlog::error(
                        "[Moonraker Client] printer.objects.list failed: invalid response");
                    if (response.contains("error")) {
                        spdlog::error("[Moonraker Client]   Error details: {}",
                                      response["error"].dump());
                    }
                }

                // Emit discovery failed event
                client_.emit_event(MoonrakerEventType::DISCOVERY_FAILED, error_reason, true);

                // Invoke error callback if provided
                spdlog::debug(
                    "[Moonraker Client] Invoking discovery on_error callback, on_error={}",
                    on_error_discovery_ ? "valid" : "null");
                if (on_error_discovery_) {
                    auto cb = std::move(on_error_discovery_);
                    on_complete_discovery_ = nullptr;
                    cb(error_reason);
                }
                return;
            }

            // Parse discovered objects into typed arrays
            const json& objects = response["result"]["objects"];
            parse_objects(objects);

            // Early hardware discovery callback - allows AMS/MMU backends to initialize
            // BEFORE the subscription response arrives, so they can receive initial state naturally
            if (on_hardware_discovered_) {
                spdlog::debug("[Moonraker Client] Invoking early hardware discovery callback");
                on_hardware_discovered_(hardware_);
            }

            // Step 2: Get server information
            client_.send_jsonrpc("server.info", {}, [this](json info_response) {
                if (is_stale())
                    return;
                if (info_response.contains("result")) {
                    const json& result = info_response["result"];
                    std::string klippy_version = result.value("klippy_version", "unknown");
                    auto moonraker_version = result.value("moonraker_version", "unknown");
                    hardware_.set_moonraker_version(moonraker_version);

                    spdlog::debug("[Moonraker Client] Moonraker version: {}", moonraker_version);
                    spdlog::debug("[Moonraker Client] Klippy version: {}", klippy_version);

                    if (result.contains("components") && result["components"].is_array()) {
                        std::vector<std::string> components =
                            result["components"].get<std::vector<std::string>>();
                        spdlog::debug("[Moonraker Client] Server components: {}",
                                      json(components).dump());

                        // Check for Spoolman component and verify connection
                        bool has_spoolman_component =
                            std::find(components.begin(), components.end(), "spoolman") !=
                            components.end();
                        if (has_spoolman_component) {
                            spdlog::info("[Moonraker Client] Spoolman component detected, "
                                         "checking status...");
                            // Fire-and-forget status check - updates PrinterState async
                            client_.send_jsonrpc(
                                "server.spoolman.status", json::object(),
                                [](json response) {
                                    bool connected = false;
                                    if (response.contains("result")) {
                                        connected =
                                            response["result"].value("spoolman_connected", false);
                                    }
                                    spdlog::info("[Moonraker Client] Spoolman status: connected={}",
                                                 connected);
                                    get_printer_state().set_spoolman_available(connected);
                                },
                                [](const MoonrakerError& err) {
                                    spdlog::warn(
                                        "[Moonraker Client] Spoolman status check failed: {}",
                                        err.message);
                                    get_printer_state().set_spoolman_available(false);
                                });
                        }
                    }
                }

                // Fire-and-forget webcam detection - independent of components list
                client_.send_jsonrpc(
                    "server.webcams.list", json::object(),
                    [](json response) {
                        bool has_webcam = false;
                        if (response.contains("result") && response["result"].contains("webcams")) {
                            for (const auto& cam : response["result"]["webcams"]) {
                                if (cam.value("enabled", true)) {
                                    has_webcam = true;
                                    break;
                                }
                            }
                        }
                        spdlog::info("[Moonraker Client] Webcam detection: {}",
                                     has_webcam ? "found" : "none");
                        get_printer_state().set_webcam_available(has_webcam);
                    },
                    [](const MoonrakerError& err) {
                        spdlog::warn("[Moonraker Client] Webcam detection failed: {}", err.message);
                        get_printer_state().set_webcam_available(false);
                    });

                // Fire-and-forget power device detection (silent — not all printers
                // have the power component, and "Method not found" is expected)
                client_.send_jsonrpc(
                    "machine.device_power.devices", json::object(),
                    [](json response) {
                        int device_count = 0;
                        if (response.contains("result") && response["result"].contains("devices")) {
                            device_count = static_cast<int>(response["result"]["devices"].size());
                        }
                        spdlog::info("[Moonraker Client] Power device detection: {} devices",
                                     device_count);
                        get_printer_state().set_power_device_count(device_count);
                    },
                    [](const MoonrakerError& err) {
                        spdlog::debug("[Moonraker Client] Power device detection failed: {}",
                                      err.message);
                        get_printer_state().set_power_device_count(0);
                    },
                    0,     // default timeout
                    true); // silent — suppress error toast

                // Step 3: Get printer information
                client_.send_jsonrpc("printer.info", {}, [this](json printer_response) {
                    if (is_stale())
                        return;
                    if (printer_response.contains("result")) {
                        const json& result = printer_response["result"];
                        auto hostname = result.value("hostname", "unknown");
                        auto software_version = result.value("software_version", "unknown");
                        hardware_.set_hostname(hostname);
                        hardware_.set_software_version(software_version);
                        std::string state = result.value("state", "");
                        std::string state_message = result.value("state_message", "");

                        spdlog::debug("[Moonraker Client] Printer hostname: {}", hostname);
                        spdlog::debug("[Moonraker Client] Klipper software version: {}",
                                      software_version);
                        if (!state_message.empty()) {
                            spdlog::info("[Moonraker Client] Printer state: {}", state_message);
                        }

                        // Set klippy state based on printer.info response
                        // This ensures we recognize shutdown/error states at startup
                        if (state == "shutdown") {
                            spdlog::warn(
                                "[Moonraker Client] Printer is in SHUTDOWN state at startup");
                            get_printer_state().set_klippy_state(KlippyState::SHUTDOWN);
                        } else if (state == "error") {
                            spdlog::warn("[Moonraker Client] Printer is in ERROR state at startup");
                            get_printer_state().set_klippy_state(KlippyState::ERROR);
                        } else if (state == "startup") {
                            spdlog::info("[Moonraker Client] Printer is starting up");
                            get_printer_state().set_klippy_state(KlippyState::STARTUP);
                        } else if (state == "ready") {
                            get_printer_state().set_klippy_state(KlippyState::READY);
                        }
                    }

                    // Step 4: Query configfile for accelerometer detection
                    // Klipper's objects/list only returns objects with get_status() methods.
                    // Accelerometers (adxl345, lis2dw, mpu9250, resonance_tester) don't have
                    // get_status() since they're on-demand calibration tools.
                    // Must check configfile.config keys instead.
                    client_.send_jsonrpc(
                        "printer.objects.query",
                        {{"objects", json::object({{"configfile", json::array({"config"})}})}},
                        [this](json config_response) {
                            if (config_response.contains("result") &&
                                config_response["result"].contains("status") &&
                                config_response["result"]["status"].contains("configfile") &&
                                config_response["result"]["status"]["configfile"].contains(
                                    "config")) {
                                const auto& cfg =
                                    config_response["result"]["status"]["configfile"]["config"];
                                hardware_.parse_config_keys(cfg);

                                // Update LED controller with configfile data (effect targets +
                                // output_pin PWM)
                                nlohmann::json cfg_copy = cfg;
                                helix::ui::queue_update([cfg_copy]() {
                                    auto& led_ctrl = helix::led::LedController::instance();
                                    if (led_ctrl.is_initialized()) {
                                        led_ctrl.update_effect_targets(cfg_copy);
                                        led_ctrl.update_output_pin_config(cfg_copy);
                                    }
                                });
                            }
                        },
                        [](const MoonrakerError& err) {
                            // Configfile query failed - not critical, continue with discovery
                            spdlog::debug(
                                "[Moonraker Client] Configfile query failed, continuing: {}",
                                err.message);
                        });

                    // Step 4b: Query OS version from machine.system_info (parallel)
                    client_.send_jsonrpc(
                        "machine.system_info", json::object(),
                        [this](json sys_response) {
                            // Extract distribution name: result.system_info.distribution.name
                            if (sys_response.contains("result") &&
                                sys_response["result"].contains("system_info") &&
                                sys_response["result"]["system_info"].contains("distribution") &&
                                sys_response["result"]["system_info"]["distribution"].contains(
                                    "name")) {
                                std::string os_name =
                                    sys_response["result"]["system_info"]["distribution"]["name"]
                                        .get<std::string>();
                                hardware_.set_os_version(os_name);
                                spdlog::debug("[Moonraker Client] OS version: {}", os_name);
                            }
                        },
                        [](const MoonrakerError& err) {
                            spdlog::debug("[Moonraker Client] machine.system_info query "
                                          "failed, continuing: "
                                          "{}",
                                          err.message);
                        });

                    // Step 5: Query MCU information for printer detection
                    // Find all MCU objects (e.g., "mcu", "mcu EBBCan", "mcu rpi")
                    std::vector<std::string> mcu_objects;
                    for (const auto& obj : hardware_.printer_objects()) {
                        // Match "mcu" or "mcu <name>" pattern
                        if (obj == "mcu" || obj.rfind("mcu ", 0) == 0) {
                            mcu_objects.push_back(obj);
                        }
                    }

                    if (mcu_objects.empty()) {
                        spdlog::debug(
                            "[Moonraker Client] No MCU objects found, skipping MCU query");
                        // Continue to subscription step
                        complete_discovery_subscription();
                        return;
                    }

                    // Query all MCU objects in parallel using a shared counter
                    auto pending_mcu_queries =
                        std::make_shared<std::atomic<size_t>>(mcu_objects.size());
                    auto mcu_results =
                        std::make_shared<std::vector<std::pair<std::string, std::string>>>();
                    auto mcu_version_results =
                        std::make_shared<std::vector<std::pair<std::string, std::string>>>();
                    auto mcu_results_mutex = std::make_shared<std::mutex>();

                    for (const auto& mcu_obj : mcu_objects) {
                        json mcu_query = {{mcu_obj, nullptr}};
                        client_.send_jsonrpc(
                            "printer.objects.query", {{"objects", mcu_query}},
                            [this, mcu_obj, pending_mcu_queries, mcu_results, mcu_version_results,
                             mcu_results_mutex](json mcu_response) {
                                if (is_stale())
                                    return;
                                std::string chip_type;
                                std::string mcu_version;

                                // Extract MCU chip type and version from response
                                if (mcu_response.contains("result") &&
                                    mcu_response["result"].contains("status") &&
                                    mcu_response["result"]["status"].contains(mcu_obj)) {
                                    const json& mcu_data =
                                        mcu_response["result"]["status"][mcu_obj];

                                    if (mcu_data.contains("mcu_constants") &&
                                        mcu_data["mcu_constants"].is_object() &&
                                        mcu_data["mcu_constants"].contains("MCU") &&
                                        mcu_data["mcu_constants"]["MCU"].is_string()) {
                                        chip_type =
                                            mcu_data["mcu_constants"]["MCU"].get<std::string>();
                                        spdlog::debug("[Moonraker Client] Detected MCU '{}': {}",
                                                      mcu_obj, chip_type);
                                    }

                                    // Extract mcu_version for About section
                                    if (mcu_data.contains("mcu_version") &&
                                        mcu_data["mcu_version"].is_string()) {
                                        mcu_version = mcu_data["mcu_version"].get<std::string>();
                                        spdlog::debug("[Moonraker Client] MCU '{}' version: {}",
                                                      mcu_obj, mcu_version);
                                    }
                                }

                                // Store results thread-safely
                                {
                                    std::lock_guard<std::mutex> lock(*mcu_results_mutex);
                                    if (!chip_type.empty()) {
                                        mcu_results->push_back({mcu_obj, chip_type});
                                    }
                                    if (!mcu_version.empty()) {
                                        mcu_version_results->push_back({mcu_obj, mcu_version});
                                    }
                                }

                                // Check if all queries complete
                                if (pending_mcu_queries->fetch_sub(1) == 1) {
                                    // All MCU queries complete - populate mcu and mcu_list
                                    std::vector<std::string> mcu_list;
                                    std::string primary_mcu;

                                    // Sort results to ensure consistent ordering (primary "mcu"
                                    // first)
                                    std::lock_guard<std::mutex> lock(*mcu_results_mutex);
                                    auto sort_mcu_first = [](const auto& a, const auto& b) {
                                        // "mcu" comes first, then alphabetical
                                        if (a.first == "mcu")
                                            return true;
                                        if (b.first == "mcu")
                                            return false;
                                        return a.first < b.first;
                                    };
                                    std::sort(mcu_results->begin(), mcu_results->end(),
                                              sort_mcu_first);
                                    std::sort(mcu_version_results->begin(),
                                              mcu_version_results->end(), sort_mcu_first);

                                    for (const auto& [obj_name, chip] : *mcu_results) {
                                        mcu_list.push_back(chip);
                                        if (obj_name == "mcu" && primary_mcu.empty()) {
                                            primary_mcu = chip;
                                        }
                                    }

                                    // Update hardware discovery with MCU info
                                    hardware_.set_mcu(primary_mcu);
                                    hardware_.set_mcu_list(mcu_list);
                                    hardware_.set_mcu_versions(*mcu_version_results);

                                    if (!primary_mcu.empty()) {
                                        spdlog::info("[Moonraker Client] Primary MCU: {}",
                                                     primary_mcu);
                                    }
                                    if (mcu_list.size() > 1) {
                                        spdlog::info("[Moonraker Client] All MCUs: {}",
                                                     json(mcu_list).dump());
                                    }

                                    // Continue to subscription step
                                    complete_discovery_subscription();
                                }
                            },
                            [this, mcu_obj, pending_mcu_queries](const MoonrakerError& err) {
                                if (is_stale())
                                    return;

                                spdlog::warn("[Moonraker Client] MCU query for '{}' failed: {}",
                                             mcu_obj, err.message);

                                // Check if all queries complete (even on error)
                                if (pending_mcu_queries->fetch_sub(1) == 1) {
                                    // Continue to subscription step even if some MCU queries
                                    // failed
                                    complete_discovery_subscription();
                                }
                            });
                    }
                });
            });
        },
        [this](const MoonrakerError& err) {
            if (is_stale())
                return;

            spdlog::error("[Moonraker Client] printer.objects.list request failed: {}",
                          err.message);
            client_.emit_event(MoonrakerEventType::DISCOVERY_FAILED, err.message, true);
            spdlog::debug("[Moonraker Client] Invoking discovery on_error callback, on_error={}",
                          on_error_discovery_ ? "valid" : "null");
            if (on_error_discovery_) {
                auto cb = std::move(on_error_discovery_);
                on_complete_discovery_ = nullptr;
                cb(err.message);
            }
        },
        0,   // default timeout
        true // silent — suppress error toast (Klippy gate already checked)
    );
}

void MoonrakerDiscoverySequence::complete_discovery_subscription() {
    // Step 5: Subscribe to all discovered objects + core objects
    json subscription_objects;

    // Core non-optional objects
    subscription_objects["print_stats"] = nullptr;
    subscription_objects["virtual_sdcard"] = nullptr;
    subscription_objects["toolhead"] = nullptr;
    subscription_objects["gcode_move"] = nullptr;
    subscription_objects["motion_report"] = nullptr;
    subscription_objects["system_stats"] = nullptr;
    subscription_objects["display_status"] = nullptr;

    // All discovered heaters (extruders, beds, generic heaters)
    for (const auto& heater : heaters_) {
        subscription_objects[heater] = nullptr;
    }

    // All discovered sensors
    for (const auto& sensor : sensors_) {
        subscription_objects[sensor] = nullptr;
    }

    // All discovered fans
    spdlog::info("[Moonraker Client] Subscribing to {} fans: {}", fans_.size(), json(fans_).dump());
    for (const auto& fan : fans_) {
        subscription_objects[fan] = nullptr;
    }

    // All discovered LEDs
    for (const auto& led : leds_) {
        subscription_objects[led] = nullptr;
    }

    // All discovered LED effects (for tracking active/enabled state)
    for (const auto& effect : hardware_.led_effects()) {
        subscription_objects[effect] = nullptr;
    }

    // Bed mesh (for 3D visualization)
    subscription_objects["bed_mesh"] = nullptr;

    // Exclude object (for mid-print object exclusion)
    subscription_objects["exclude_object"] = nullptr;

    // Manual probe (for Z-offset calibration - PROBE_CALIBRATE, Z_ENDSTOP_CALIBRATE)
    subscription_objects["manual_probe"] = nullptr;

    // Stepper enable state (for motor enabled/disabled detection - updates immediately on M84)
    subscription_objects["stepper_enable"] = nullptr;

    // Idle timeout (for printer activity state - Ready/Printing/Idle)
    subscription_objects["idle_timeout"] = nullptr;

    // Happy Hare MMU object (gate status, colors, materials, filament info)
    if (hardware_.has_mmu()) {
        subscription_objects["mmu"] = nullptr;
        spdlog::info("[Moonraker Client] Subscribing to MMU object (Happy Hare)");
    }

    // All discovered AFC objects (AFC, AFC_stepper, AFC_hub, AFC_extruder)
    // These provide lane status, sensor states, and filament info for MMU support
    for (const auto& afc_obj : afc_objects_) {
        subscription_objects[afc_obj] = nullptr;
    }

    // All discovered filament sensors (filament_switch_sensor, filament_motion_sensor)
    // These provide runout detection and encoder motion data
    for (const auto& sensor : filament_sensors_) {
        subscription_objects[sensor] = nullptr;
    }

    // All discovered tool objects (for toolchanger support)
    if (hardware_.has_tool_changer()) {
        subscription_objects["toolchanger"] = nullptr;
        for (const auto& tool_name : hardware_.tool_names()) {
            subscription_objects["tool " + tool_name] = nullptr;
        }
        spdlog::info("[Moonraker Client] Subscribing to toolchanger + {} tool objects",
                     hardware_.tool_names().size());
    }

    // Firmware retraction settings (if printer has firmware_retraction module)
    if (hardware_.has_firmware_retraction()) {
        subscription_objects["firmware_retraction"] = nullptr;
    }

    // Print start macros (for detecting when prep phase completes)
    // These are optional - printers without these macros will silently not receive updates
    // AD5M/KAMP macros:
    subscription_objects["gcode_macro _START_PRINT"] = nullptr;
    subscription_objects["gcode_macro START_PRINT"] = nullptr;
    // HelixScreen custom macro:
    subscription_objects["gcode_macro _HELIX_STATE"] = nullptr;

    json subscribe_params = {{"objects", subscription_objects}};

    client_.send_jsonrpc(
        "printer.objects.subscribe", subscribe_params,
        [this, subscription_objects](json sub_response) {
            if (is_stale())
                return;
            if (sub_response.contains("result")) {
                spdlog::info("[Moonraker Client] Subscription complete: {} objects subscribed",
                             subscription_objects.size());

                // Process initial state from subscription response
                // Moonraker returns current values in result.status
                if (sub_response["result"].contains("status")) {
                    const auto& status = sub_response["result"]["status"];
                    spdlog::info(
                        "[Moonraker Client] Processing initial printer state from subscription");

                    // DEBUG: Log print_stats specifically to diagnose startup sync issues
                    if (status.contains("print_stats")) {
                        spdlog::info("[Moonraker Client] INITIAL print_stats: {}",
                                     status["print_stats"].dump());
                    } else {
                        spdlog::warn("[Moonraker Client] INITIAL status has NO print_stats!");
                    }

                    client_.dispatch_status_update(status);
                }
            } else if (sub_response.contains("error")) {
                spdlog::error("[Moonraker Client] Subscription failed: {}",
                              sub_response["error"].dump());

                // Emit discovery failed event (subscription is part of discovery)
                std::string error_msg = sub_response["error"].dump();
                client_.emit_event(
                    MoonrakerEventType::DISCOVERY_FAILED,
                    fmt::format("Failed to subscribe to printer updates: {}", error_msg),
                    false); // Warning, not error - discovery still completes
            }

            // Discovery complete - notify observers
            if (on_discovery_complete_) {
                on_discovery_complete_(hardware_);
            }
            if (on_complete_discovery_) {
                auto cb = std::move(on_complete_discovery_);
                on_error_discovery_ = nullptr;
                cb();
            }
        });
}

void MoonrakerDiscoverySequence::parse_objects(const json& objects) {
    // Populate unified hardware discovery (Phase 2)
    hardware_.parse_objects(objects);

    heaters_.clear();
    sensors_.clear();
    fans_.clear();
    leds_.clear();
    steppers_.clear();
    afc_objects_.clear();
    filament_sensors_.clear();

    // Collect printer_objects for hardware_ as we iterate
    std::vector<std::string> all_objects;
    all_objects.reserve(objects.size());

    for (const auto& obj : objects) {
        std::string name = obj.template get<std::string>();

        // Store all objects for detection heuristics (object_exists, macro_match)
        all_objects.push_back(name);

        // Steppers (stepper_x, stepper_y, stepper_z, stepper_z1, etc.)
        if (name.rfind("stepper_", 0) == 0) {
            steppers_.push_back(name);
        }
        // Extruders (controllable heaters)
        // Match "extruder", "extruder1", etc., but NOT "extruder_stepper"
        else if (name.rfind("extruder", 0) == 0 && name.rfind("extruder_stepper", 0) != 0) {
            heaters_.push_back(name);
        }
        // Heated bed
        else if (name == "heater_bed") {
            heaters_.push_back(name);
        }
        // Generic heaters (e.g., "heater_generic chamber")
        else if (name.rfind("heater_generic ", 0) == 0) {
            heaters_.push_back(name);
        }
        // Read-only temperature sensors
        else if (name.rfind("temperature_sensor ", 0) == 0) {
            sensors_.push_back(name);
        }
        // Temperature-controlled fans (also act as sensors)
        else if (name.rfind("temperature_fan ", 0) == 0) {
            sensors_.push_back(name);
            fans_.push_back(name); // Also add to fans for control
        }
        // Part cooling fan
        else if (name == "fan") {
            fans_.push_back(name);
        }
        // Heater fans (e.g., "heater_fan hotend_fan")
        else if (name.rfind("heater_fan ", 0) == 0) {
            fans_.push_back(name);
        }
        // Generic fans
        else if (name.rfind("fan_generic ", 0) == 0) {
            fans_.push_back(name);
        }
        // Controller fans
        else if (name.rfind("controller_fan ", 0) == 0) {
            fans_.push_back(name);
        }
        // Output pins - classify as fan or LED based on name keywords
        else if (name.rfind("output_pin ", 0) == 0) {
            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (lower_name.find("fan") != std::string::npos) {
                fans_.push_back(name);
            } else if (lower_name.find("light") != std::string::npos ||
                       lower_name.find("led") != std::string::npos ||
                       lower_name.find("lamp") != std::string::npos) {
                leds_.push_back(name);
            }
        }
        // LED outputs
        else if (name.rfind("led ", 0) == 0 || name.rfind("neopixel ", 0) == 0 ||
                 name.rfind("dotstar ", 0) == 0) {
            leds_.push_back(name);
        }
        // AFC MMU objects (AFC_stepper, AFC_hub, AFC_extruder, AFC, AFC_lane, AFC_BoxTurtle,
        // AFC_OpenAMS, AFC_buffer) These need subscription for lane state, sensor data, and
        // filament info
        else if (name == "AFC" || name.rfind("AFC_stepper ", 0) == 0 ||
                 name.rfind("AFC_hub ", 0) == 0 || name.rfind("AFC_extruder ", 0) == 0 ||
                 name.rfind("AFC_lane ", 0) == 0 || name.rfind("AFC_BoxTurtle ", 0) == 0 ||
                 name.rfind("AFC_OpenAMS ", 0) == 0 || name.rfind("AFC_buffer ", 0) == 0) {
            afc_objects_.push_back(name);
        }
        // Filament sensors (switch or motion type)
        // These provide runout detection and encoder motion data
        else if (name.rfind("filament_switch_sensor ", 0) == 0 ||
                 name.rfind("filament_motion_sensor ", 0) == 0) {
            filament_sensors_.push_back(name);
        }
    }

    spdlog::debug("[Moonraker Client] Discovered: {} heaters, {} sensors, {} fans, {} LEDs, {} "
                  "steppers, {} AFC objects, {} filament sensors",
                  heaters_.size(), sensors_.size(), fans_.size(), leds_.size(), steppers_.size(),
                  afc_objects_.size(), filament_sensors_.size());

    // Debug output of discovered objects
    if (!heaters_.empty()) {
        spdlog::debug("[Moonraker Client] Heaters: {}", json(heaters_).dump());
    }
    if (!sensors_.empty()) {
        spdlog::debug("[Moonraker Client] Sensors: {}", json(sensors_).dump());
    }
    if (!fans_.empty()) {
        spdlog::debug("[Moonraker Client] Fans: {}", json(fans_).dump());
    }
    if (!leds_.empty()) {
        spdlog::debug("[Moonraker Client] LEDs: {}", json(leds_).dump());
    }
    if (!steppers_.empty()) {
        spdlog::debug("[Moonraker Client] Steppers: {}", json(steppers_).dump());
    }
    if (!afc_objects_.empty()) {
        spdlog::info("[Moonraker Client] AFC objects: {}", json(afc_objects_).dump());
    }
    if (!filament_sensors_.empty()) {
        spdlog::info("[Moonraker Client] Filament sensors: {}", json(filament_sensors_).dump());
    }

    // Store printer objects in hardware discovery (handles all capability parsing)
    hardware_.set_printer_objects(all_objects);
}

void MoonrakerDiscoverySequence::parse_bed_mesh(const json& bed_mesh) {
    // Invoke bed mesh callback for API layer
    // The API layer (MoonrakerAPI) owns the bed mesh data; Client is just the transport
    std::function<void(const json&)> callback_copy;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callback_copy = bed_mesh_callback_;
    }
    if (callback_copy) {
        try {
            callback_copy(bed_mesh);
        } catch (const std::exception& e) {
            spdlog::error("[Moonraker Client] Bed mesh callback threw exception: {}", e.what());
        }
    }
}

} // namespace helix
