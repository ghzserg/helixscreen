// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_client_mock.h"

#include "ui_update_queue.h"

#include "../tests/mocks/mock_printer_state.h"
#include "app_globals.h"
#include "gcode_parser.h"
#include "moonraker_client_mock_internal.h"
#include "printer_state.h"
#include "runtime_config.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>

using namespace helix;

// Delegating constructor - uses default speedup of 1.0
MoonrakerClientMock::MoonrakerClientMock(PrinterType type) : MoonrakerClientMock(type, 1.0) {}

MoonrakerClientMock::MoonrakerClientMock(PrinterType type, double speedup_factor)
    : printer_type_(type) {
    // Initialize idle timeout tracking
    last_activity_time_ = std::chrono::steady_clock::now();

    // Set speedup factor (clamped)
    speedup_factor_.store(std::clamp(speedup_factor, 0.1, 10000.0));

    spdlog::debug("[MoonrakerClientMock] Created with printer type: {}, speedup: {}x",
                  static_cast<int>(type), speedup_factor_.load());

    // Register method handlers for all RPC domains
    mock_internal::register_file_handlers(method_handlers_);
    mock_internal::register_print_handlers(method_handlers_);
    mock_internal::register_object_handlers(method_handlers_);
    mock_internal::register_history_handlers(method_handlers_);
    mock_internal::register_server_handlers(method_handlers_);
    spdlog::debug("[MoonrakerClientMock] Registered {} RPC method handlers",
                  method_handlers_.size());

    // Populate hardware immediately (available for wizard without calling discover_printer())
    populate_hardware();
    spdlog::debug(
        "[MoonrakerClientMock] Hardware populated: {} heaters, {} sensors, {} fans, {} LEDs",
        heaters_.size(), sensors_.size(), fans_.size(), leds_.size());

    // Generate synthetic bed mesh data
    generate_mock_bed_mesh();

    // Pre-populate capabilities so they're available immediately for UI testing
    // (without waiting for connect() -> discover_printer() to be called)
    populate_capabilities();

    // Rebuild hardware_ from mock data (ensures hardware() accessors return complete data)
    rebuild_hardware();

    // Check HELIX_MOCK_SPOOLMAN env var for Spoolman availability
    const char* spoolman_env = std::getenv("HELIX_MOCK_SPOOLMAN");
    if (spoolman_env && (std::string(spoolman_env) == "0" || std::string(spoolman_env) == "off")) {
        mock_spoolman_enabled_ = false;
        spdlog::info("[MoonrakerClientMock] Mock Spoolman disabled via HELIX_MOCK_SPOOLMAN=0");
    }

    // Set up bed mesh callback to handle incoming status updates
    // This ensures dispatch_status_update updates the mock's internal bed mesh state
    set_bed_mesh_callback([this](const json& bed_mesh) { parse_incoming_bed_mesh(bed_mesh); });
}

void MoonrakerClientMock::set_simulation_speedup(double factor) {
    double clamped = std::clamp(factor, 0.1, 10000.0);
    speedup_factor_.store(clamped);
    spdlog::info("[MoonrakerClientMock] Simulation speedup set to {}x", clamped);
}

double MoonrakerClientMock::get_simulation_speedup() const {
    return speedup_factor_.load();
}

void MoonrakerClientMock::reset_idle_timeout() {
    last_activity_time_ = std::chrono::steady_clock::now();
    if (idle_timeout_triggered_.load()) {
        idle_timeout_triggered_.store(false);
        spdlog::debug("[MoonrakerClientMock] Idle timeout reset");
    }
}

int MoonrakerClientMock::get_current_layer() const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    if (print_metadata_.layer_count == 0) {
        return 0;
    }
    return static_cast<int>(print_progress_.load() * print_metadata_.layer_count);
}

int MoonrakerClientMock::get_total_layers() const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return static_cast<int>(print_metadata_.layer_count);
}

bool MoonrakerClientMock::has_chamber_sensor() const {
    for (const auto& s : sensors_) {
        if (s == "temperature_sensor chamber") {
            return true;
        }
    }
    return false;
}

std::set<std::string> MoonrakerClientMock::get_excluded_objects() const {
    // If shared state is set, use that for consistency with MoonrakerAPIMock
    if (mock_state_) {
        return mock_state_->get_excluded_objects();
    }
    // Fallback to local state for backward compatibility
    std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
    return excluded_objects_;
}

void MoonrakerClientMock::set_mock_state(std::shared_ptr<MockPrinterState> state) {
    mock_state_ = state;
    if (state) {
        spdlog::debug("[MoonrakerClientMock] Shared mock state attached");
    } else {
        spdlog::debug("[MoonrakerClientMock] Shared mock state detached");
    }
}

MoonrakerClientMock::~MoonrakerClientMock() {
    // Signal restart thread to stop and wait for it (under lock to prevent race)
    {
        std::lock_guard<std::mutex> lock(restart_mutex_);
        restart_pending_.store(false);
        if (restart_thread_.joinable()) {
            restart_thread_.join();
        }
    }

    // Pass true to skip logging during destruction - spdlog may already be destroyed
    stop_temperature_simulation(true);
}

int MoonrakerClientMock::connect(const char* url, std::function<void()> on_connected,
                                 [[maybe_unused]] std::function<void()> on_disconnected) {
    spdlog::debug("[MoonrakerClientMock] Simulating connection to: {}", url ? url : "(null)");

    // Simulate connection state change (same as real client)
    set_connection_state(ConnectionState::CONNECTING);

    // Small delay to simulate realistic connection (250ms / speedup)
    double speedup = speedup_factor_.load();
    auto delay_ms = static_cast<int>(250.0 / speedup);
    if (delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    // Check if we should simulate disconnected state for testing
    if (get_runtime_config()->simulate_disconnect) {
        spdlog::warn(
            "[MoonrakerClientMock] --disconnected flag set, simulating connection failure");
        set_connection_state(ConnectionState::DISCONNECTED);
        // Don't invoke on_connected callback or dispatch any state
        return 0;
    }

    set_connection_state(ConnectionState::CONNECTED);

    // Dispatch historical temperature data first (fills graph with 2-3 min of data)
    dispatch_historical_temperatures();

    // Start live temperature simulation
    start_temperature_simulation();

    // Dispatch initial state BEFORE calling on_connected (matches real Moonraker behavior)
    // Real client sends initial state from subscription response - mock does it here
    dispatch_initial_state();

    // Auto-start a print if configured (e.g., when testing print-status panel)
    if (get_runtime_config()->mock_auto_start_print) {
        // Use --gcode-file if specified, otherwise fall back to default test file
        const char* print_file = get_runtime_config()->gcode_test_file
                                     ? get_runtime_config()->gcode_test_file
                                     : RuntimeConfig::DEFAULT_TEST_FILE;
        spdlog::info("[MoonrakerClientMock] Auto-starting print simulation with '{}'", print_file);
        start_print_internal(print_file);
    }

    // Immediately invoke connection callback
    if (on_connected) {
        spdlog::debug("[MoonrakerClientMock] Simulated connection successful");
        on_connected();
    }

    // Store disconnect callback (never invoked in mock, but stored for consistency)
    // Note: Not needed for this simple mock implementation

    return 0; // Success
}

void MoonrakerClientMock::populate_capabilities() {
    // Create mock Klipper object list for capabilities parsing
    json mock_objects = json::array();

    // Add common objects
    mock_objects.push_back("heater_bed");
    mock_objects.push_back("extruder");
    mock_objects.push_back("bed_mesh");
    mock_objects.push_back("probe"); // Most printers have a probe for bed mesh/leveling

    // Add capabilities for UI testing (speaker for M300, firmware retraction for G10/G11)
    mock_objects.push_back("output_pin beeper");   // Triggers has_speaker_ capability
    mock_objects.push_back("firmware_retraction"); // Triggers has_firmware_retraction_ capability

    // Add hardware objects from populated lists
    for (const auto& heater : heaters_) {
        // Skip if already added (heater_bed, extruder)
        if (heater != "heater_bed" && heater != "extruder") {
            mock_objects.push_back(heater);
        }
    }
    for (const auto& fan : fans_) {
        mock_objects.push_back(fan);
    }
    for (const auto& sensor : sensors_) {
        mock_objects.push_back(sensor);
    }
    for (const auto& led : leds_) {
        mock_objects.push_back(led);
    }

    // Add printer-specific objects
    switch (printer_type_) {
    case PrinterType::VORON_24:
        mock_objects.push_back("quad_gantry_level");
        mock_objects.push_back("gcode_macro CLEAN_NOZZLE");
        mock_objects.push_back("gcode_macro PRINT_START");
        break;
    case PrinterType::VORON_TRIDENT:
        mock_objects.push_back("z_tilt");
        mock_objects.push_back("gcode_macro CLEAN_NOZZLE");
        mock_objects.push_back("gcode_macro PRINT_START");
        break;
    default:
        // Other printers may not have these features
        break;
    }

    // Add LED effects (klipper-led_effect plugin objects)
    mock_objects.push_back("led_effect breathing");
    mock_objects.push_back("led_effect fire_comet");
    mock_objects.push_back("led_effect rainbow");
    mock_objects.push_back("led_effect static_white");

    // Add common macros for all printer types (for testing macro panel)
    mock_objects.push_back("gcode_macro START_PRINT");
    mock_objects.push_back("gcode_macro END_PRINT");
    mock_objects.push_back("gcode_macro PAUSE");
    mock_objects.push_back("gcode_macro RESUME");
    mock_objects.push_back("gcode_macro CANCEL_PRINT");
    mock_objects.push_back("gcode_macro LOAD_FILAMENT");
    mock_objects.push_back("gcode_macro UNLOAD_FILAMENT");
    mock_objects.push_back("gcode_macro BED_MESH_CALIBRATE");
    mock_objects.push_back("gcode_macro G28");           // Home all
    mock_objects.push_back("gcode_macro M600");          // Filament change
    mock_objects.push_back("gcode_macro _SYSTEM_MACRO"); // System macro (hidden by default)

    // Add LED-related macros (auto-detected by printer_discovery via LED keywords)
    mock_objects.push_back("gcode_macro LIGHTS_ON");
    mock_objects.push_back("gcode_macro LIGHTS_OFF");
    mock_objects.push_back("gcode_macro LIGHTS_TOGGLE");
    mock_objects.push_back("gcode_macro LED_PARTY");
    mock_objects.push_back("gcode_macro LED_NIGHTLIGHT");

    // Moonraker plugins
    mock_objects.push_back("timelapse"); // Moonraker-Timelapse plugin

    // MMU/AMS system - Happy Hare uses "mmu" object name
    mock_objects.push_back("mmu");

    // Probe sensor (HELIX_MOCK_PROBE_TYPE: cartographer, tap, bltouch, beacon, klicky, standard,
    // none)
    const char* probe_env = std::getenv("HELIX_MOCK_PROBE_TYPE");
    std::string mock_probe_type = (probe_env && probe_env[0]) ? probe_env : "cartographer";
    if (mock_probe_type == "none") {
        spdlog::debug("[MoonrakerClientMock] Probe disabled via env var");
    } else if (mock_probe_type == "cartographer") {
        mock_objects.push_back("cartographer");
        spdlog::debug("[MoonrakerClientMock] Mock probe: cartographer");
    } else if (mock_probe_type == "bltouch") {
        mock_objects.push_back("bltouch");
        spdlog::debug("[MoonrakerClientMock] Mock probe: bltouch");
    } else if (mock_probe_type == "beacon") {
        mock_objects.push_back("beacon");
        spdlog::debug("[MoonrakerClientMock] Mock probe: beacon");
    } else {
        // tap, klicky, standard, etc. → generic "probe" object
        mock_objects.push_back("probe");
        spdlog::debug("[MoonrakerClientMock] Mock probe: {} (as generic probe)", mock_probe_type);
    }

    // Filament sensors (common setup: runout sensor at spool holder)
    // Check HELIX_MOCK_FILAMENT_SENSORS env var for custom sensor names
    // Default: single switch sensor named "runout_sensor"
    const char* sensor_env = std::getenv("HELIX_MOCK_FILAMENT_SENSORS");
    if (sensor_env && std::string(sensor_env) == "none") {
        // Explicitly disabled
        spdlog::debug("[MoonrakerClientMock] Filament sensors disabled via env var");
    } else if (sensor_env) {
        // Custom sensor list (comma-separated, e.g., "switch:fsensor,motion:encoder")
        std::string sensors_str(sensor_env);
        size_t pos = 0;
        while ((pos = sensors_str.find(',')) != std::string::npos || !sensors_str.empty()) {
            std::string token =
                (pos != std::string::npos) ? sensors_str.substr(0, pos) : sensors_str;
            size_t colon = token.find(':');
            if (colon != std::string::npos) {
                std::string type = token.substr(0, colon);
                std::string name = token.substr(colon + 1);
                if (type == "switch") {
                    mock_objects.push_back("filament_switch_sensor " + name);
                } else if (type == "motion") {
                    mock_objects.push_back("filament_motion_sensor " + name);
                }
            }
            if (pos == std::string::npos)
                break;
            sensors_str.erase(0, pos + 1);
        }
        spdlog::debug("[MoonrakerClientMock] Custom filament sensors from env: {}", sensor_env);
    } else {
        // Default: one switch sensor (typical Voron setup)
        mock_objects.push_back("filament_switch_sensor runout_sensor");
        spdlog::debug(
            "[MoonrakerClientMock] Default filament sensor: filament_switch_sensor runout_sensor");
    }

    // Parse objects into hardware discovery (unified hardware access)
    hardware_.parse_objects(mock_objects);

    // Mock accelerometer configuration for input shaper wizard testing
    // Real Klipper doesn't expose accelerometers in objects list (no get_status()),
    // so we simulate what parse_config_keys() would find from configfile.config
    json mock_config;
    mock_config["adxl345"] = json::object();
    mock_config["resonance_tester"] = json::object();
    // Provide kinematics so bed_moves detection works
    // HELIX_MOCK_KINEMATICS overrides; otherwise default matches printer type
    const char* kin_env = std::getenv("HELIX_MOCK_KINEMATICS");
    std::string default_kinematics;
    switch (printer_type_) {
    case PrinterType::VORON_24:
    case PrinterType::VORON_TRIDENT:
        default_kinematics = "corexy";
        break;
    case PrinterType::CREALITY_K1:
        default_kinematics = "corexy";
        break;
    default:
        default_kinematics = "cartesian";
        break;
    }
    std::string mock_kinematics = (kin_env && kin_env[0]) ? kin_env : default_kinematics;
    mock_config["printer"] = {{"kinematics", mock_kinematics}};
    hardware_.parse_config_keys(mock_config);
    spdlog::debug("[MoonrakerClientMock] Mock config: adxl345, resonance_tester, kinematics={}",
                  mock_kinematics);

    // Populate printer objects for hardware discovery
    std::vector<std::string> all_objects;
    for (const auto& obj : mock_objects) {
        std::string name = obj.get<std::string>();
        all_objects.push_back(name);
    }
    hardware_.set_printer_objects(all_objects);

    // Also populate filament_sensors_ member for subscription (same as real parse_objects)
    filament_sensors_.clear();
    for (const auto& obj : mock_objects) {
        std::string name = obj.get<std::string>();
        if (name.rfind("filament_switch_sensor ", 0) == 0 ||
            name.rfind("filament_motion_sensor ", 0) == 0) {
            filament_sensors_.push_back(name);
        }
    }

    spdlog::debug("[MoonrakerClientMock] Hardware populated: {} macros, {} filament sensors",
                  hardware_.macros().size(), filament_sensors_.size());
}

void MoonrakerClientMock::rebuild_hardware() {
    json objects = json::array();

    // Add hardware components
    for (const auto& h : heaters_) {
        objects.push_back(h);
    }
    for (const auto& f : fans_) {
        objects.push_back(f);
    }
    for (const auto& s : sensors_) {
        // Only include sensors that have proper sensor prefixes (temperature_sensor, etc.)
        // Skip bare heater names like "extruder", "heater_bed" - those are handled via heaters_
        // In Klipper's object list, heaters appear once (as "extruder" or "heater_bed"),
        // and their thermistors are accessed via the heater's temperature property.
        if (s.rfind("temperature_sensor ", 0) == 0 || s.rfind("temperature_fan ", 0) == 0) {
            objects.push_back(s);
        }
    }
    for (const auto& l : leds_) {
        objects.push_back(l);
    }
    for (const auto& fs : filament_sensors_) {
        objects.push_back(fs);
    }
    // Include additional objects set via set_additional_objects() for capability testing
    // (e.g., "mmu", "AFC", "toolchanger" for MMU/tool changer detection)
    for (const auto& obj : additional_objects_) {
        objects.push_back(obj);
    }

    // Add capability objects (must be included since parse_objects clears everything)
    objects.push_back("bed_mesh");
    objects.push_back("probe");
    objects.push_back("output_pin beeper");
    objects.push_back("firmware_retraction");
    if (mmu_enabled_) {
        objects.push_back("mmu");
    }
    objects.push_back("timelapse");

    // Add printer-specific capability objects
    switch (printer_type_) {
    case PrinterType::VORON_24:
        objects.push_back("quad_gantry_level");
        objects.push_back("gcode_macro CLEAN_NOZZLE");
        objects.push_back("gcode_macro PRINT_START");
        break;
    case PrinterType::VORON_TRIDENT:
        objects.push_back("z_tilt");
        objects.push_back("gcode_macro CLEAN_NOZZLE");
        objects.push_back("gcode_macro PRINT_START");
        break;
    default:
        break;
    }

    // Add LED effects (klipper-led_effect plugin objects)
    objects.push_back("led_effect breathing");
    objects.push_back("led_effect fire_comet");
    objects.push_back("led_effect rainbow");
    objects.push_back("led_effect static_white");

    // Add common macros
    objects.push_back("gcode_macro START_PRINT");
    objects.push_back("gcode_macro END_PRINT");
    objects.push_back("gcode_macro PAUSE");
    objects.push_back("gcode_macro RESUME");
    objects.push_back("gcode_macro CANCEL_PRINT");
    objects.push_back("gcode_macro LOAD_FILAMENT");
    objects.push_back("gcode_macro UNLOAD_FILAMENT");
    objects.push_back("gcode_macro BED_MESH_CALIBRATE");
    objects.push_back("gcode_macro G28");
    objects.push_back("gcode_macro M600");
    objects.push_back("gcode_macro _SYSTEM_MACRO");

    // Add LED-related macros (auto-detected by printer_discovery via LED keywords)
    objects.push_back("gcode_macro LIGHTS_ON");
    objects.push_back("gcode_macro LIGHTS_OFF");
    objects.push_back("gcode_macro LIGHTS_TOGGLE");
    objects.push_back("gcode_macro LED_PARTY");
    objects.push_back("gcode_macro LED_NIGHTLIGHT");

    // Add default filament sensor
    objects.push_back("filament_switch_sensor runout_sensor");

    // Add MCU objects for discovery
    objects.push_back("mcu");
    objects.push_back("mcu EBBCan");

    hardware_.parse_objects(objects);

    // Set mock MCU version data (after parse_objects which clears everything)
    hardware_.set_mcu("stm32f446xx");
    hardware_.set_mcu_list({"stm32f446xx", "stm32g0b1xx"});
    hardware_.set_mcu_versions(
        {{"mcu", "v0.12.0-155-g4cfa273e"}, {"mcu EBBCan", "v0.12.0-155-g4cfa273e"}});
}

void MoonrakerClientMock::discover_printer(
    std::function<void()> on_complete, std::function<void(const std::string& reason)> on_error) {
    spdlog::debug("[MoonrakerClientMock] Simulating hardware discovery");

    // Check Klippy state - discovery fails if Klippy not connected
    KlippyState state = klippy_state_.load();
    if (state == KlippyState::STARTUP || state == KlippyState::ERROR) {
        std::string reason = "Klippy Host not connected";
        spdlog::warn("[MoonrakerClientMock] Discovery failed: {}", reason);

        // Emit discovery failed event (matches real client behavior)
        emit_event(MoonrakerEventType::DISCOVERY_FAILED, reason, true);

        // Invoke error callback if provided
        if (on_error) {
            on_error(reason);
        }
        return;
    }

    // Populate hardware based on printer type (may have already been done in constructor)
    populate_hardware();

    // Generate synthetic bed mesh data (may have already been done in constructor)
    generate_mock_bed_mesh();

    // Query server.info to get moonraker_version (uses registered RPC handler)
    send_jsonrpc("server.info", json::object(), [this, on_complete](json response) {
        if (response.contains("result")) {
            auto moonraker_version = response["result"].value("moonraker_version", "unknown");
            hardware_.set_moonraker_version(moonraker_version);
            spdlog::debug("[MoonrakerClientMock] Moonraker version: {}", moonraker_version);
        }

        // Chain to printer.info to get hostname and software_version
        send_jsonrpc("printer.info", json::object(), [this, on_complete](json response) {
            spdlog::debug("[MoonrakerClientMock] printer.info response received");

            // Populate capabilities - builds complete hardware state from mock data
            // (heaters_, fans_, sensors_, leds_, plus all standard mock objects like
            // mmu, timelapse, macros, etc.) via hardware_.parse_objects()
            populate_capabilities();

            // Now set the metadata AFTER parse_objects() has run
            if (response.contains("result")) {
                auto hostname = response["result"].value("hostname", "unknown");
                auto software_version = response["result"].value("software_version", "unknown");
                hardware_.set_hostname(hostname);
                hardware_.set_software_version(software_version);
                spdlog::debug("[MoonrakerClientMock] Printer hostname: {}", hostname);
                spdlog::debug("[MoonrakerClientMock] Klipper software version: {}",
                              software_version);
            }

            // Query machine.system_info for OS version (uses registered RPC handler)
            send_jsonrpc(
                "machine.system_info", json::object(),
                [this](json sys_response) {
                    if (sys_response.contains("result") &&
                        sys_response["result"].contains("system_info") &&
                        sys_response["result"]["system_info"].contains("distribution") &&
                        sys_response["result"]["system_info"]["distribution"].contains("name")) {
                        std::string os_name =
                            sys_response["result"]["system_info"]["distribution"]["name"]
                                .get<std::string>();
                        hardware_.set_os_version(os_name);
                        spdlog::debug("[MoonrakerClientMock] OS version: {}", os_name);
                    }
                },
                [](const MoonrakerError& err) {
                    spdlog::debug("[MoonrakerClientMock] machine.system_info failed: {}",
                                  err.message);
                });

            // Set Spoolman availability during discovery (matches real Moonraker behavior)
            // Real client queries server.spoolman.status during discovery - see
            // moonraker_client.cpp:1047
            get_printer_state().set_spoolman_available(mock_spoolman_enabled_);
            spdlog::debug("[MoonrakerClientMock] Spoolman available: {}", mock_spoolman_enabled_);

            // Set webcam availability during discovery (matches real Moonraker behavior)
            // Real client queries server.webcams.list during discovery
            get_printer_state().set_webcam_available(true);
            spdlog::debug("[MoonrakerClientMock] Webcam available: true (mock always has webcam)");

            // Set power device count during discovery (matches real Moonraker behavior)
            // Real client queries machine.device_power.devices during discovery
            if (std::getenv("MOCK_EMPTY_POWER")) {
                get_printer_state().set_power_device_count(0);
                spdlog::debug("[MoonrakerClientMock] Power devices: 0 (MOCK_EMPTY_POWER set)");
            } else {
                get_printer_state().set_power_device_count(4);
                spdlog::debug("[MoonrakerClientMock] Power devices: 4 (mock default)");
            }

            // Log discovered hardware
            spdlog::debug("[MoonrakerClientMock] Discovered: {} heaters, {} sensors, {} fans, {} "
                          "LEDs",
                          heaters_.size(), sensors_.size(), fans_.size(), leds_.size());

            // Early hardware discovery callback (for AMS/MMU initialization)
            // Must be called BEFORE on_discovery_complete_ to match real implementation timing
            if (on_hardware_discovered_) {
                spdlog::debug("[MoonrakerClientMock] Invoking early hardware discovery callback");
                on_hardware_discovered_(hardware_);
            }

            // Invoke discovery complete callback with hardware (for PrinterState binding)
            if (on_discovery_complete_) {
                on_discovery_complete_(hardware_);
            }

            // Invoke completion callback immediately (no async delay in mock)
            if (on_complete) {
                on_complete();
            }
        });
    });
}

void MoonrakerClientMock::populate_hardware() {
    // Clear existing data (inherited from MoonrakerClient)
    heaters_.clear();
    sensors_.clear();
    fans_.clear();
    leds_.clear();

    // Populate based on printer type
    switch (printer_type_) {
    case PrinterType::VORON_24:
        // Voron 2.4 configuration
        heaters_ = {"heater_bed", "extruder"};
        sensors_ = {"heater_bed", // Bed thermistor (Klipper naming: bare heater name)
                    "extruder",   // Hotend thermistor (Klipper naming: bare heater name)
                    "temperature_sensor chamber", "temperature_sensor raspberry_pi",
                    "temperature_sensor mcu_temp"};
        fans_ = {"heater_fan hotend_fan",
                 "fan", // Part cooling fan
                 "fan_generic nevermore", "controller_fan controller_fan"};
        leds_ = {"neopixel chamber_light", "neopixel status_led", "led caselight",
                 "output_pin Enclosure_LEDs"};
        break;

    case PrinterType::VORON_TRIDENT:
        // Voron Trident configuration
        heaters_ = {"heater_bed", "extruder"};
        sensors_ = {"heater_bed", // Bed thermistor (Klipper naming: bare heater name)
                    "extruder",   // Hotend thermistor (Klipper naming: bare heater name)
                    "temperature_sensor chamber",
                    "temperature_sensor raspberry_pi",
                    "temperature_sensor mcu_temp",
                    "temperature_sensor z_thermal_adjust"};
        fans_ = {"heater_fan hotend_fan", "fan", "fan_generic exhaust_fan",
                 "controller_fan electronics_fan"};
        leds_ = {"neopixel sb_leds", "neopixel chamber_leds"};
        break;

    case PrinterType::CREALITY_K1:
        // Creality K1/K1 Max configuration
        heaters_ = {"heater_bed", "extruder"};
        sensors_ = {"heater_bed", // Bed thermistor (Klipper naming: bare heater name)
                    "extruder",   // Hotend thermistor (Klipper naming: bare heater name)
                    "temperature_sensor mcu_temp", "temperature_sensor host_temp"};
        fans_ = {"heater_fan hotend_fan", "fan", "fan_generic auxiliary_fan"};
        leds_ = {"neopixel logo_led"};
        break;

    case PrinterType::FLASHFORGE_AD5M:
        // FlashForge Adventurer 5M configuration
        heaters_ = {"heater_bed", "extruder"};
        sensors_ = {"heater_bed", // Bed thermistor (Klipper naming: bare heater name)
                    "extruder",   // Hotend thermistor (Klipper naming: bare heater name)
                    "temperature_sensor chamber", "temperature_sensor mcu_temp"};
        fans_ = {"heater_fan hotend_fan", "fan", "fan_generic chamber_fan"};
        leds_ = {"led chamber_led"};
        break;

    case PrinterType::GENERIC_COREXY:
        // Generic CoreXY printer
        heaters_ = {"heater_bed", "extruder"};
        sensors_ = {"heater_bed", // Bed thermistor (Klipper naming: bare heater name)
                    "extruder",   // Hotend thermistor (Klipper naming: bare heater name)
                    "temperature_sensor raspberry_pi"};
        fans_ = {"heater_fan hotend_fan", "fan"};
        leds_ = {"neopixel chamber_led"};
        break;

    case PrinterType::GENERIC_BEDSLINGER:
        // Generic i3-style bedslinger
        heaters_ = {"heater_bed", "extruder"};
        sensors_ = {
            "heater_bed", // Bed thermistor (Klipper naming: bare heater name)
            "extruder"    // Hotend thermistor (Klipper naming: bare heater name)
        };
        fans_ = {"heater_fan hotend_fan", "fan"};
        leds_ = {};
        break;

    case PrinterType::MULTI_EXTRUDER:
        // Multi-extruder test case
        heaters_ = {"heater_bed", "extruder", "extruder1"};
        sensors_ = {"heater_bed", // Bed thermistor (Klipper naming: bare heater name)
                    "extruder",   // Hotend thermistor primary (Klipper naming: bare heater name)
                    "extruder1",  // Hotend thermistor secondary (Klipper naming: bare heater name)
                    "temperature_sensor chamber", "temperature_sensor mcu_temp"};
        fans_ = {"heater_fan hotend_fan", "heater_fan hotend_fan1", "fan",
                 "fan_generic exhaust_fan"};
        leds_ = {"neopixel chamber_light"};
        break;
    }

    // Initialize LED states (all off by default)
    {
        std::lock_guard<std::mutex> lock(led_mutex_);
        led_states_.clear();
        for (const auto& led : leds_) {
            if (led.rfind("output_pin ", 0) == 0)
                continue; // output_pin uses {value:} not color_data
            led_states_[led] = LedColor{0.0, 0.0, 0.0, 0.0};
        }
    }

    spdlog::trace("[MoonrakerClientMock] Populated hardware:");
    for (const auto& h : heaters_)
        spdlog::trace("  Heater: {}", h);
    for (const auto& s : sensors_)
        spdlog::trace("  Sensor: {}", s);
    for (const auto& f : fans_)
        spdlog::trace("  Fan: {}", f);
    for (const auto& l : leds_)
        spdlog::trace("  LED: {}", l);
}

void MoonrakerClientMock::parse_incoming_bed_mesh(const json& bed_mesh) {
    // Parse bed mesh JSON from dispatch_status_update into active_bed_mesh_
    // This mirrors the JSON format sent by real Moonraker

    // Parse profile name
    if (bed_mesh.contains("profile_name")) {
        if (bed_mesh["profile_name"].is_string()) {
            active_bed_mesh_.name = bed_mesh["profile_name"].get<std::string>();
        } else if (bed_mesh["profile_name"].is_null()) {
            active_bed_mesh_.name = "";
        }
    }

    // Parse probed_matrix (2D array of Z heights)
    if (bed_mesh.contains("probed_matrix") && bed_mesh["probed_matrix"].is_array()) {
        active_bed_mesh_.probed_matrix.clear();
        const auto& matrix = bed_mesh["probed_matrix"];

        for (const auto& row : matrix) {
            if (!row.is_array()) {
                continue;
            }
            std::vector<float> row_vec;
            for (const auto& val : row) {
                if (val.is_number()) {
                    row_vec.push_back(val.get<float>());
                }
                // Skip non-numeric values (strings, nulls)
            }
            active_bed_mesh_.probed_matrix.push_back(row_vec);
        }

        // Update counts based on parsed matrix
        if (!active_bed_mesh_.probed_matrix.empty()) {
            active_bed_mesh_.y_count = static_cast<int>(active_bed_mesh_.probed_matrix.size());
            active_bed_mesh_.x_count = static_cast<int>(active_bed_mesh_.probed_matrix[0].size());
        } else {
            active_bed_mesh_.x_count = 0;
            active_bed_mesh_.y_count = 0;
        }
    }

    // Parse mesh_min (array [x, y])
    if (bed_mesh.contains("mesh_min") && bed_mesh["mesh_min"].is_array() &&
        bed_mesh["mesh_min"].size() >= 2) {
        if (bed_mesh["mesh_min"][0].is_number()) {
            active_bed_mesh_.mesh_min[0] = bed_mesh["mesh_min"][0].get<float>();
        }
        if (bed_mesh["mesh_min"][1].is_number()) {
            active_bed_mesh_.mesh_min[1] = bed_mesh["mesh_min"][1].get<float>();
        }
    }

    // Parse mesh_max (array [x, y])
    if (bed_mesh.contains("mesh_max") && bed_mesh["mesh_max"].is_array() &&
        bed_mesh["mesh_max"].size() >= 2) {
        if (bed_mesh["mesh_max"][0].is_number()) {
            active_bed_mesh_.mesh_max[0] = bed_mesh["mesh_max"][0].get<float>();
        }
        if (bed_mesh["mesh_max"][1].is_number()) {
            active_bed_mesh_.mesh_max[1] = bed_mesh["mesh_max"][1].get<float>();
        }
    }

    // Parse algorithm from mesh_params
    if (bed_mesh.contains("mesh_params") && bed_mesh["mesh_params"].is_object()) {
        const auto& params = bed_mesh["mesh_params"];
        if (params.contains("algo") && params["algo"].is_string()) {
            active_bed_mesh_.algo = params["algo"].get<std::string>();
        }
    }

    // Parse profiles list
    if (bed_mesh.contains("profiles") && bed_mesh["profiles"].is_object()) {
        bed_mesh_profiles_.clear();
        for (const auto& [key, value] : bed_mesh["profiles"].items()) {
            bed_mesh_profiles_.push_back(key);
        }
    }

    spdlog::debug("[MoonrakerClientMock] Parsed incoming bed mesh: profile='{}', size={}x{}",
                  active_bed_mesh_.name, active_bed_mesh_.x_count, active_bed_mesh_.y_count);
}

void MoonrakerClientMock::generate_mock_bed_mesh() {
    // Helper lambda to generate a mesh with given shape parameters
    auto generate_mesh = [](const std::string& name, float amplitude, float x_tilt,
                            float y_tilt) -> BedMeshProfile {
        BedMeshProfile mesh;
        mesh.name = name;
        mesh.mesh_min[0] = static_cast<float>(mock_internal::MOCK_MESH_X_MIN);
        mesh.mesh_min[1] = static_cast<float>(mock_internal::MOCK_MESH_Y_MIN);
        mesh.mesh_max[0] = static_cast<float>(mock_internal::MOCK_MESH_X_MAX);
        mesh.mesh_max[1] = static_cast<float>(mock_internal::MOCK_MESH_Y_MAX);
        mesh.x_count = 7;
        mesh.y_count = 7;
        mesh.algo = "lagrange";

        float center_x = mesh.x_count / 2.0f;
        float center_y = mesh.y_count / 2.0f;
        float max_radius = std::min(center_x, center_y);

        for (int row = 0; row < mesh.y_count; row++) {
            std::vector<float> row_vec;
            for (int col = 0; col < mesh.x_count; col++) {
                float dx = col - center_x;
                float dy = row - center_y;
                float dist = std::sqrt(dx * dx + dy * dy);

                // Dome shape + optional tilt
                float normalized_dist = dist / max_radius;
                float height = amplitude * (1.0f - normalized_dist * normalized_dist);
                height += x_tilt * (col - center_x) / center_x * 0.1f;
                height += y_tilt * (row - center_y) / center_y * 0.1f;

                row_vec.push_back(height);
            }
            mesh.probed_matrix.push_back(row_vec);
        }
        return mesh;
    };

    // Generate "default" profile: centered dome, 0.3mm amplitude
    stored_bed_mesh_profiles_["default"] = generate_mesh("default", 0.3f, 0.0f, 0.0f);

    // Generate "adaptive" profile: dome with slight tilt, different amplitude
    stored_bed_mesh_profiles_["adaptive"] = generate_mesh("adaptive", 0.25f, 0.5f, -0.3f);

    // Set profile name list
    bed_mesh_profiles_ = {"default", "adaptive"};

    // Load "default" as active
    active_bed_mesh_ = stored_bed_mesh_profiles_["default"];

    spdlog::debug("[MoonrakerClientMock] Generated {} bed mesh profiles, active='{}'",
                  stored_bed_mesh_profiles_.size(), active_bed_mesh_.name);
}

void MoonrakerClientMock::generate_mock_bed_mesh_with_variation() {
    // Generate a realistic bed mesh with true randomness
    // Simulates re-probing with measurement noise and slight bed changes

    // Keep existing configuration using centralized mock printer constants
    active_bed_mesh_.mesh_min[0] = static_cast<float>(mock_internal::MOCK_MESH_X_MIN);
    active_bed_mesh_.mesh_min[1] = static_cast<float>(mock_internal::MOCK_MESH_Y_MIN);
    active_bed_mesh_.mesh_max[0] = static_cast<float>(mock_internal::MOCK_MESH_X_MAX);
    active_bed_mesh_.mesh_max[1] = static_cast<float>(mock_internal::MOCK_MESH_Y_MAX);
    active_bed_mesh_.x_count = 7;
    active_bed_mesh_.y_count = 7;
    active_bed_mesh_.algo = "lagrange";

    // Seed with both random_device and a monotonic counter to guarantee
    // distinct meshes even if random_device is deterministic (some Linux/embedded platforms)
    static std::atomic<uint32_t> calibration_counter{0};
    std::random_device rd;
    std::seed_seq seed{
        rd(), rd(), calibration_counter.fetch_add(1),
        static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> noise(-0.03f, 0.03f);      // ±0.03mm probe noise
    std::uniform_real_distribution<float> amplitude(0.15f, 0.35f);   // Overall dome height
    std::uniform_real_distribution<float> tilt(-0.08f, 0.08f);       // Bed tilt per axis
    std::uniform_real_distribution<float> center_shift(-0.5f, 0.5f); // Dome center offset

    // Random parameters for this calibration
    float dome_amp = amplitude(gen);
    float x_tilt = tilt(gen);
    float y_tilt = tilt(gen);
    float cx_shift = center_shift(gen);
    float cy_shift = center_shift(gen);

    active_bed_mesh_.probed_matrix.clear();
    float center_x = active_bed_mesh_.x_count / 2.0f + cx_shift;
    float center_y = active_bed_mesh_.y_count / 2.0f + cy_shift;
    float max_radius = std::min(active_bed_mesh_.x_count, active_bed_mesh_.y_count) / 2.0f;

    for (int row = 0; row < active_bed_mesh_.y_count; row++) {
        std::vector<float> row_vec;
        for (int col = 0; col < active_bed_mesh_.x_count; col++) {
            float dx = col - center_x;
            float dy = row - center_y;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Base dome shape
            float normalized_dist = dist / max_radius;
            float height = dome_amp * (1.0f - normalized_dist * normalized_dist);

            // Add bed tilt (simulates unlevel bed)
            float norm_x = static_cast<float>(col) / (active_bed_mesh_.x_count - 1) - 0.5f;
            float norm_y = static_cast<float>(row) / (active_bed_mesh_.y_count - 1) - 0.5f;
            height += x_tilt * norm_x + y_tilt * norm_y;

            // Add per-point probe noise (simulates measurement uncertainty)
            height += noise(gen);

            row_vec.push_back(height);
        }
        active_bed_mesh_.probed_matrix.push_back(row_vec);
    }

    spdlog::debug("[MoonrakerClientMock] Regenerated bed mesh: amp={:.3f}, tilt=({:.3f},{:.3f})",
                  dome_amp, x_tilt, y_tilt);
}

void MoonrakerClientMock::dispatch_bed_mesh_update() {
    // Build bed mesh JSON in Moonraker format
    json probed_matrix_json = json::array();
    for (const auto& row : active_bed_mesh_.probed_matrix) {
        json row_json = json::array();
        for (float val : row) {
            row_json.push_back(val);
        }
        probed_matrix_json.push_back(row_json);
    }

    json profiles_json = json::object();
    for (const auto& [name, profile] : stored_bed_mesh_profiles_) {
        // Build points array for this profile
        json points_json = json::array();
        for (const auto& row : profile.probed_matrix) {
            json row_json = json::array();
            for (float val : row) {
                row_json.push_back(val);
            }
            points_json.push_back(row_json);
        }

        profiles_json[name] = {{"points", points_json},
                               {"mesh_params",
                                {{"min_x", profile.mesh_min[0]},
                                 {"min_y", profile.mesh_min[1]},
                                 {"max_x", profile.mesh_max[0]},
                                 {"max_y", profile.mesh_max[1]},
                                 {"x_count", profile.x_count},
                                 {"y_count", profile.y_count}}}};
    }

    json bed_mesh_status = {
        {"bed_mesh",
         {{"profile_name", active_bed_mesh_.name},
          {"probed_matrix", probed_matrix_json},
          {"mesh_min", {active_bed_mesh_.mesh_min[0], active_bed_mesh_.mesh_min[1]}},
          {"mesh_max", {active_bed_mesh_.mesh_max[0], active_bed_mesh_.mesh_max[1]}},
          {"profiles", profiles_json},
          {"mesh_params", {{"algo", active_bed_mesh_.algo}}}}}};

    // Dispatch via base class method
    dispatch_status_update(bed_mesh_status);
}

void MoonrakerClientMock::disconnect() {
    spdlog::info("[MoonrakerClientMock] Simulating disconnection");
    stop_temperature_simulation(false);
    set_connection_state(ConnectionState::DISCONNECTED);
}

int MoonrakerClientMock::send_jsonrpc(const std::string& method) {
    spdlog::trace("[MoonrakerClientMock] Mock send_jsonrpc: {}", method);
    return 0; // Success
}

int MoonrakerClientMock::send_jsonrpc(const std::string& method,
                                      [[maybe_unused]] const json& params) {
    spdlog::trace("[MoonrakerClientMock] Mock send_jsonrpc: {} (with params)", method);
    return 0; // Success
}

RequestId MoonrakerClientMock::send_jsonrpc(const std::string& method, const json& params,
                                            std::function<void(json)> cb) {
    spdlog::trace("[MoonrakerClientMock] Mock send_jsonrpc: {} (with callback)", method);

    // Dispatch to handler registry (wrap callback to match error_cb signature)
    auto noop_error_cb = [](const MoonrakerError&) {};
    return send_jsonrpc(method, params, cb, noop_error_cb);
}

RequestId MoonrakerClientMock::send_jsonrpc(const std::string& method, const json& params,
                                            std::function<void(json)> success_cb,
                                            std::function<void(const MoonrakerError&)> error_cb,
                                            [[maybe_unused]] uint32_t timeout_ms,
                                            [[maybe_unused]] bool silent) {
    spdlog::trace("[MoonrakerClientMock] Mock send_jsonrpc: {} (with success/error callbacks)",
                  method);

    // Dispatch to method handler registry
    auto it = method_handlers_.find(method);
    if (it != method_handlers_.end()) {
        it->second(this, params, success_cb, error_cb);
        return next_mock_request_id();
    }

    // Unimplemented methods - log warning
    spdlog::debug("[MoonrakerClientMock] Method '{}' not implemented - callbacks not invoked",
                  method);
    return next_mock_request_id();
}

// Removed old implementation - now handled by method_handlers_ registry:
// Lines 527-916 deleted (file/print/objects/history handlers moved to separate modules)
// See: moonraker_client_mock_files.cpp, moonraker_client_mock_print.cpp,
//      moonraker_client_mock_objects.cpp, moonraker_client_mock_history.cpp
//
// Old logic was:
//   - server.files.* handlers (list, metadata, delete, move, copy, post_directory,
//   delete_directory)
//   - printer.gcode.script handler
//   - printer.print.* handlers (start, pause, resume, cancel)
//   - printer.objects.query handler
//   - server.history.* handlers (list, totals, delete_job)
//

std::string MoonrakerClientMock::get_last_gcode_error() const {
    std::lock_guard<std::mutex> lock(gcode_error_mutex_);
    return last_gcode_error_;
}

int MoonrakerClientMock::gcode_script(const std::string& gcode) {
    spdlog::trace("[MoonrakerClientMock] Mock gcode_script: {}", gcode);

    // Clear previous error at start
    {
        std::lock_guard<std::mutex> lock(gcode_error_mutex_);
        last_gcode_error_.clear();
    }

    // Parse temperature commands to update simulation targets
    // M104 Sxxx - Set extruder temp (no wait)
    // M109 Sxxx - Set extruder temp (wait)
    // M140 Sxxx - Set bed temp (no wait)
    // M190 Sxxx - Set bed temp (wait)
    // SET_HEATER_TEMPERATURE HEATER=extruder TARGET=xxx
    // SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=xxx

    // Check for Klipper-style SET_HEATER_TEMPERATURE commands
    if (gcode.find("SET_HEATER_TEMPERATURE") != std::string::npos) {
        double target = 0.0;
        size_t target_pos = gcode.find("TARGET=");
        if (target_pos != std::string::npos) {
            target = std::stod(gcode.substr(target_pos + 7));
        }

        if (gcode.find("HEATER=extruder") != std::string::npos) {
            set_extruder_target(target);
            reset_idle_timeout();
            spdlog::info("[MoonrakerClientMock] Extruder target set to {}°C", target);
        } else if (gcode.find("HEATER=heater_bed") != std::string::npos) {
            set_bed_target(target);
            reset_idle_timeout();
            spdlog::info("[MoonrakerClientMock] Bed target set to {}°C", target);
        }
    }
    // Check for M-code style temperature commands
    else if (gcode.find("M104") != std::string::npos || gcode.find("M109") != std::string::npos) {
        size_t s_pos = gcode.find('S');
        if (s_pos != std::string::npos) {
            double target = std::stod(gcode.substr(s_pos + 1));
            set_extruder_target(target);
            reset_idle_timeout();
            spdlog::info("[MoonrakerClientMock] Extruder target set to {}°C (M-code)", target);
        }
    } else if (gcode.find("M140") != std::string::npos || gcode.find("M190") != std::string::npos) {
        size_t s_pos = gcode.find('S');
        if (s_pos != std::string::npos) {
            double target = std::stod(gcode.substr(s_pos + 1));
            set_bed_target(target);
            reset_idle_timeout();
            spdlog::info("[MoonrakerClientMock] Bed target set to {}°C (M-code)", target);
        }
    }

    // Parse motion mode commands (G90/G91)
    // G90 - Absolute positioning mode
    // G91 - Relative positioning mode
    if (gcode.find("G90") != std::string::npos) {
        relative_mode_.store(false);
        spdlog::info("[MoonrakerClientMock] Set absolute positioning mode (G90)");
    } else if (gcode.find("G91") != std::string::npos) {
        relative_mode_.store(true);
        spdlog::info("[MoonrakerClientMock] Set relative positioning mode (G91)");
    }

    // M84 - Disable stepper motors (clears homed_axes + updates stepper_enable)
    if (gcode.find("M84") != std::string::npos || gcode.find("M18") != std::string::npos) {
        motors_enabled_.store(false);
        {
            std::lock_guard<std::mutex> lock(homed_axes_mutex_);
            homed_axes_.clear();
        }
        spdlog::info("[MoonrakerClientMock] Motors disabled (M84/M18), homed_axes cleared");
        // Dispatch toolhead with cleared homed_axes (primary motor state indicator)
        // and stepper_enable state change (fallback for printers that report it)
        json status = {{"toolhead", {{"homed_axes", ""}}},
                       {"stepper_enable",
                        {{"steppers",
                          {{"stepper_x", false},
                           {"stepper_y", false},
                           {"stepper_z", false},
                           {"extruder", false}}}}}};
        dispatch_status_update(status);
    }

    // Parse homing command (G28)
    // G28 - Home all axes
    // G28 X - Home X axis only
    // G28 Y - Home Y axis only
    // G28 Z - Home Z axis only
    // G28 X Y - Home X and Y axes
    if (gcode.find("G28") != std::string::npos) {
        // Re-enable motors when homing
        motors_enabled_.store(true);
        // Check if specific axes are mentioned after G28
        // Need to look after the G28 to avoid false matches
        size_t g28_pos = gcode.find("G28");
        std::string after_g28 = gcode.substr(g28_pos + 3);

        // Check for specific axis letters (case insensitive search)
        bool has_x =
            after_g28.find('X') != std::string::npos || after_g28.find('x') != std::string::npos;
        bool has_y =
            after_g28.find('Y') != std::string::npos || after_g28.find('y') != std::string::npos;
        bool has_z =
            after_g28.find('Z') != std::string::npos || after_g28.find('z') != std::string::npos;

        // If no specific axis mentioned, home all
        bool home_all = !has_x && !has_y && !has_z;

        {
            std::lock_guard<std::mutex> lock(homed_axes_mutex_);

            if (home_all) {
                // Home all axes
                homed_axes_ = "xyz";
                pos_x_.store(0.0);
                pos_y_.store(0.0);
                pos_z_.store(0.0);
                spdlog::info("[MoonrakerClientMock] Homed all axes (G28), homed_axes='xyz'");
            } else {
                // Home specific axes and update position
                if (has_x) {
                    if (homed_axes_.find('x') == std::string::npos) {
                        homed_axes_ += 'x';
                    }
                    pos_x_.store(0.0);
                }
                if (has_y) {
                    if (homed_axes_.find('y') == std::string::npos) {
                        homed_axes_ += 'y';
                    }
                    pos_y_.store(0.0);
                }
                if (has_z) {
                    if (homed_axes_.find('z') == std::string::npos) {
                        homed_axes_ += 'z';
                    }
                    pos_z_.store(0.0);
                }
                spdlog::info("[MoonrakerClientMock] Homed axes: X={} Y={} Z={}, homed_axes='{}'",
                             has_x, has_y, has_z, homed_axes_);
            }
        }
        // Reset idle timeout when homing
        reset_idle_timeout();
    }

    // Parse movement commands (G0/G1)
    // G0 X100 Y50 Z10 - Rapid move
    // G1 X100 Y50 Z10 E5 F3000 - Linear move (E and F ignored for now)
    if (gcode.find("G0") != std::string::npos || gcode.find("G1") != std::string::npos) {
        // Re-enable motors when moving
        motors_enabled_.store(true);
        bool is_relative = relative_mode_.load();

        // Position limits (typical Voron 2.4 350mm config)
        // Z allows slight negative for probe calibration
        constexpr double X_MIN = 0.0, X_MAX = 350.0;
        constexpr double Y_MIN = 0.0, Y_MAX = 350.0;
        constexpr double Z_MIN = -0.5, Z_MAX = 340.0;

        // Helper lambda to parse axis value from gcode string
        auto parse_axis = [&gcode](char axis) -> std::pair<bool, double> {
            // Look for the axis letter followed by a number
            size_t pos = gcode.find(axis);
            if (pos == std::string::npos) {
                // Try lowercase
                pos = gcode.find(static_cast<char>(axis + 32));
            }
            if (pos != std::string::npos && pos + 1 < gcode.length()) {
                // Skip any spaces after the axis letter
                size_t value_start = pos + 1;
                while (value_start < gcode.length() && gcode[value_start] == ' ') {
                    value_start++;
                }
                if (value_start < gcode.length()) {
                    try {
                        double value = std::stod(gcode.substr(value_start));
                        return {true, value};
                    } catch (...) {
                        // Parse error, ignore this axis
                    }
                }
            }
            return {false, 0.0};
        };

        auto [has_x, x_val] = parse_axis('X');
        auto [has_y, y_val] = parse_axis('Y');
        auto [has_z, z_val] = parse_axis('Z');

        // Calculate target positions
        double target_x = has_x ? (is_relative ? pos_x_.load() + x_val : x_val) : pos_x_.load();
        double target_y = has_y ? (is_relative ? pos_y_.load() + y_val : y_val) : pos_y_.load();
        double target_z = has_z ? (is_relative ? pos_z_.load() + z_val : z_val) : pos_z_.load();

        // Check limits (like real Klipper)
        bool out_of_range = false;
        std::string error_msg;
        if (target_x < X_MIN || target_x > X_MAX) {
            error_msg = "!! Move out of range: X=" + std::to_string(target_x);
            out_of_range = true;
        } else if (target_y < Y_MIN || target_y > Y_MAX) {
            error_msg = "!! Move out of range: Y=" + std::to_string(target_y);
            out_of_range = true;
        } else if (target_z < Z_MIN || target_z > Z_MAX) {
            error_msg = "!! Move out of range: Z=" + std::to_string(target_z);
            out_of_range = true;
        }

        if (out_of_range) {
            dispatch_gcode_response(error_msg);
            spdlog::warn("[MoonrakerClientMock] Move rejected - {}", error_msg);
            // Store error for RPC handler to return proper error response (like real Moonraker)
            {
                std::lock_guard<std::mutex> lock(gcode_error_mutex_);
                last_gcode_error_ = error_msg;
            }
        } else {
            // Apply the move
            if (has_x)
                pos_x_.store(target_x);
            if (has_y)
                pos_y_.store(target_y);
            if (has_z)
                pos_z_.store(target_z);

            if (has_x || has_y || has_z) {
                spdlog::debug("[MoonrakerClientMock] Move {} X={} Y={} Z={} (mode={})",
                              gcode.find("G0") != std::string::npos ? "G0" : "G1", pos_x_.load(),
                              pos_y_.load(), pos_z_.load(), is_relative ? "relative" : "absolute");
                // Reset idle timeout when moving
                reset_idle_timeout();
            }
        }
    }

    // Parse print job commands (delegate to unified internal handlers)
    // SDCARD_PRINT_FILE FILENAME=xxx - Start printing a file
    if (gcode.find("SDCARD_PRINT_FILE") != std::string::npos) {
        size_t filename_pos = gcode.find("FILENAME=");
        if (filename_pos != std::string::npos) {
            // Extract filename (ends at space or end of string)
            size_t start = filename_pos + 9;
            size_t end = gcode.find(' ', start);
            std::string filename =
                (end != std::string::npos) ? gcode.substr(start, end - start) : gcode.substr(start);

            // Use unified internal handler
            start_print_internal(filename);
        }
    }
    // PAUSE - Pause current print
    else if (gcode == "PAUSE" || gcode.find("PAUSE ") == 0) {
        pause_print_internal();
    }
    // RESUME - Resume paused print
    else if (gcode == "RESUME" || gcode.find("RESUME ") == 0) {
        resume_print_internal();
    }
    // CANCEL_PRINT - Cancel current print
    else if (gcode == "CANCEL_PRINT" || gcode.find("CANCEL_PRINT ") == 0) {
        cancel_print_internal();
    }
    // M112 - Emergency stop
    else if (gcode.find("M112") != std::string::npos) {
        emergency_stop_internal();
    }

    // ========================================================================
    // UNIMPLEMENTED G-CODE STUBS - Log warnings for missing features
    // ========================================================================

    // Fan control - M106/M107/SET_FAN_SPEED
    // M106 P0 S128 - Set fan index 0 to 50% (S is 0-255, P is fan index)
    if (gcode.find("M106") != std::string::npos) {
        int fan_index = 0;
        int speed_value = 0;

        // Parse P parameter (fan index)
        auto p_pos = gcode.find('P');
        if (p_pos != std::string::npos && p_pos + 1 < gcode.length()) {
            try {
                fan_index = std::stoi(gcode.substr(p_pos + 1));
            } catch (...) {
            }
        }

        // Parse S parameter (speed 0-255)
        auto s_pos = gcode.find('S');
        if (s_pos != std::string::npos && s_pos + 1 < gcode.length()) {
            try {
                speed_value = std::stoi(gcode.substr(s_pos + 1));
                speed_value = std::clamp(speed_value, 0, 255);
            } catch (...) {
            }
        }

        // Convert to normalized speed (0.0-1.0)
        double normalized_speed = speed_value / 255.0;

        // Fan index 0 = "fan", index 1+ = "fan1", "fan2", etc.
        std::string fan_name = (fan_index == 0) ? "fan" : ("fan" + std::to_string(fan_index));
        set_fan_speed_internal(fan_name, normalized_speed);

        spdlog::trace("[MoonrakerClientMock] M106 P{} S{} -> {} speed={:.2f}", fan_index,
                      speed_value, fan_name, normalized_speed);
    }
    // M107 - Turn off fan
    else if (gcode.find("M107") != std::string::npos) {
        int fan_index = 0;

        auto p_pos = gcode.find('P');
        if (p_pos != std::string::npos && p_pos + 1 < gcode.length()) {
            try {
                fan_index = std::stoi(gcode.substr(p_pos + 1));
            } catch (...) {
            }
        }

        std::string fan_name = (fan_index == 0) ? "fan" : ("fan" + std::to_string(fan_index));
        set_fan_speed_internal(fan_name, 0.0);

        spdlog::info("[MoonrakerClientMock] M107 P{} -> {} off", fan_index, fan_name);
    }
    // SET_FAN_SPEED - Klipper extended fan control
    // SET_FAN_SPEED FAN=nevermore SPEED=0.5
    else if (gcode.find("SET_FAN_SPEED") != std::string::npos) {
        std::string fan_name;
        double speed = 0.0;

        // Parse FAN parameter
        auto fan_pos = gcode.find("FAN=");
        if (fan_pos != std::string::npos) {
            size_t start = fan_pos + 4;
            size_t end = gcode.find_first_of(" \t\n", start);
            fan_name = gcode.substr(start, end == std::string::npos ? end : end - start);
        }

        // Parse SPEED parameter (0.0-1.0)
        auto speed_pos = gcode.find("SPEED=");
        if (speed_pos != std::string::npos) {
            try {
                speed = std::stod(gcode.substr(speed_pos + 6));
                speed = std::clamp(speed, 0.0, 1.0);
            } catch (...) {
            }
        }

        if (!fan_name.empty()) {
            // Try to find matching fan in discovered fans_ list
            std::string full_fan_name = find_fan_by_suffix(fan_name);
            if (!full_fan_name.empty()) {
                set_fan_speed_internal(full_fan_name, speed);
                spdlog::info("[MoonrakerClientMock] SET_FAN_SPEED FAN={} SPEED={:.2f}",
                             full_fan_name, speed);
            } else {
                // Use short name if no match found
                set_fan_speed_internal(fan_name, speed);
                spdlog::info(
                    "[MoonrakerClientMock] SET_FAN_SPEED FAN={} SPEED={:.2f} (unmatched fan)",
                    fan_name, speed);
            }
        }
    }

    // Extrusion control (NOT IMPLEMENTED)
    if (gcode.find("G92") != std::string::npos && gcode.find('E') != std::string::npos) {
        spdlog::warn("[MoonrakerClientMock] STUB: G92 E (set extruder position) NOT IMPLEMENTED");
    }
    if ((gcode.find("G0") != std::string::npos || gcode.find("G1") != std::string::npos) &&
        gcode.find('E') != std::string::npos) {
        spdlog::debug("[MoonrakerClientMock] Note: Extrusion (E parameter) ignored in G0/G1");
    }

    // PID Calibration simulation
    if (gcode.find("PID_CALIBRATE") != std::string::npos) {
        // Parse HEATER= parameter
        std::string heater = "extruder";
        auto heater_pos = gcode.find("HEATER=");
        if (heater_pos != std::string::npos) {
            size_t start = heater_pos + 7;
            size_t end = gcode.find(' ', start);
            heater =
                gcode.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }

        // Parse TARGET= parameter
        int target = 200;
        auto target_pos = gcode.find("TARGET=");
        if (target_pos != std::string::npos) {
            target = std::stoi(gcode.substr(target_pos + 7));
        }

        spdlog::info("[MoonrakerClientMock] PID_CALIBRATE: heater={} target={}°C", heater, target);

        // Simulate PID calibration with a background timer
        struct PIDSimState {
            MoonrakerClientMock* mock;
            std::string heater;
            int target;
            int cycle;
        };

        auto* sim = new PIDSimState{this, heater, target, 0};

        lv_timer_t* timer = lv_timer_create(
            [](lv_timer_t* t) {
                auto* s = static_cast<PIDSimState*>(lv_timer_get_user_data(t));
                s->cycle++;

                if (s->cycle <= 5) {
                    // Simulate Kalico PID sample output (matches pid_calibrate.py format)
                    char buf[128];
                    float pwm = 0.5f - (s->cycle * 0.02f);
                    float asymmetry = 0.3f - (s->cycle * 0.05f);
                    // First two samples have n/a tolerance, then converging values
                    if (s->cycle <= 2) {
                        snprintf(buf, sizeof(buf),
                                 "sample:%d pwm:%.3f asymmetry:%.3f tolerance:n/a", s->cycle, pwm,
                                 asymmetry);
                    } else {
                        float tolerance = 0.1f / s->cycle;
                        snprintf(buf, sizeof(buf),
                                 "sample:%d pwm:%.3f asymmetry:%.3f tolerance:%.4f", s->cycle, pwm,
                                 asymmetry, tolerance);
                    }
                    s->mock->dispatch_gcode_response(buf);
                } else {
                    // Emit final PID result matching real Klipper format
                    float kp, ki, kd;
                    if (s->heater == "heater_bed") {
                        kp = 73.517f;
                        ki = 1.132f;
                        kd = 1194.093f;
                    } else {
                        kp = 22.865f;
                        ki = 1.292f;
                        kd = 101.178f;
                    }

                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "PID parameters: pid_Kp=%.3f pid_Ki=%.3f pid_Kd=%.3f", kp, ki, kd);
                    s->mock->dispatch_gcode_response(buf);

                    delete s;
                    lv_timer_delete(t);
                    return;
                }
            },
            500, sim);                       // 500ms between cycles for quick mock
        lv_timer_set_repeat_count(timer, 6); // 5 progress + 1 result

        return 0; // Success - results come asynchronously via gcode_response
    }

    // SAVE_CONFIG simulation
    if (gcode.find("SAVE_CONFIG") != std::string::npos) {
        spdlog::info("[MoonrakerClientMock] SAVE_CONFIG - simulating config save + restart");
        dispatch_gcode_response("ok");
        return 1;
    }

    // Bed mesh commands
    if (gcode.find("BED_MESH_CALIBRATE") != std::string::npos) {
        // Parse optional PROFILE= parameter
        std::string profile_name = "default";
        auto profile_pos = gcode.find("PROFILE=");
        if (profile_pos != std::string::npos) {
            size_t start = profile_pos + 8; // Length of "PROFILE="
            size_t end = gcode.find_first_of(" \t\n", start);
            profile_name = gcode.substr(start, end == std::string::npos ? end : end - start);
        }

        // Regenerate mesh with slight random variation
        active_bed_mesh_.name = profile_name;
        generate_mock_bed_mesh_with_variation();

        // Add new profile to list if not already present
        if (std::find(bed_mesh_profiles_.begin(), bed_mesh_profiles_.end(), profile_name) ==
            bed_mesh_profiles_.end()) {
            bed_mesh_profiles_.push_back(profile_name);
        }
        // Store the calibrated mesh
        stored_bed_mesh_profiles_[profile_name] = active_bed_mesh_;

        spdlog::info(
            "[MoonrakerClientMock] BED_MESH_CALIBRATE: generated new mesh for profile '{}'",
            profile_name);

        // Dispatch bed mesh update notification
        dispatch_bed_mesh_update();

    } else if (gcode.find("BED_MESH_PROFILE") != std::string::npos) {
        // Parse LOAD= or SAVE= or REMOVE= parameter
        if (gcode.find("LOAD=") != std::string::npos) {
            auto load_pos = gcode.find("LOAD=");
            size_t start = load_pos + 5; // Length of "LOAD="
            size_t end = gcode.find_first_of(" \t\n", start);
            std::string profile_name =
                gcode.substr(start, end == std::string::npos ? end : end - start);

            // Check if profile exists in stored data
            auto it = stored_bed_mesh_profiles_.find(profile_name);
            if (it != stored_bed_mesh_profiles_.end()) {
                // Load stored mesh data
                active_bed_mesh_ = it->second;
                spdlog::info("[MoonrakerClientMock] BED_MESH_PROFILE LOAD: loaded profile '{}'",
                             profile_name);
                dispatch_bed_mesh_update();
            } else {
                spdlog::warn("[MoonrakerClientMock] BED_MESH_PROFILE LOAD: profile '{}' not found",
                             profile_name);
            }
        } else if (gcode.find("SAVE=") != std::string::npos) {
            auto save_pos = gcode.find("SAVE=");
            size_t start = save_pos + 5; // Length of "SAVE="
            size_t end = gcode.find_first_of(" \t\n", start);
            std::string profile_name =
                gcode.substr(start, end == std::string::npos ? end : end - start);

            // Add new profile to list if not already present
            if (std::find(bed_mesh_profiles_.begin(), bed_mesh_profiles_.end(), profile_name) ==
                bed_mesh_profiles_.end()) {
                bed_mesh_profiles_.push_back(profile_name);
            }
            // Store current mesh data under new name
            active_bed_mesh_.name = profile_name;
            stored_bed_mesh_profiles_[profile_name] = active_bed_mesh_;
            spdlog::info("[MoonrakerClientMock] BED_MESH_PROFILE SAVE: saved profile '{}'",
                         profile_name);
            dispatch_bed_mesh_update();
        } else if (gcode.find("REMOVE=") != std::string::npos) {
            auto remove_pos = gcode.find("REMOVE=");
            size_t start = remove_pos + 7; // Length of "REMOVE="
            size_t end = gcode.find_first_of(" \t\n", start);
            std::string profile_name =
                gcode.substr(start, end == std::string::npos ? end : end - start);

            // Remove profile from list and stored data
            auto it = std::find(bed_mesh_profiles_.begin(), bed_mesh_profiles_.end(), profile_name);
            if (it != bed_mesh_profiles_.end()) {
                bed_mesh_profiles_.erase(it);
                stored_bed_mesh_profiles_.erase(profile_name);
                spdlog::info("[MoonrakerClientMock] BED_MESH_PROFILE REMOVE: removed profile '{}'",
                             profile_name);
                dispatch_bed_mesh_update();
            } else {
                spdlog::warn(
                    "[MoonrakerClientMock] BED_MESH_PROFILE REMOVE: profile '{}' not found",
                    profile_name);
            }
        }
    } else if (gcode.find("BED_MESH_CLEAR") != std::string::npos) {
        // Clear the active bed mesh
        active_bed_mesh_.name = "";
        active_bed_mesh_.probed_matrix.clear();
        active_bed_mesh_.x_count = 0;
        active_bed_mesh_.y_count = 0;
        spdlog::info("[MoonrakerClientMock] BED_MESH_CLEAR: cleared active mesh");
        dispatch_bed_mesh_update();
    }

    // Z offset - SET_GCODE_OFFSET Z=0.2 or SET_GCODE_OFFSET Z_ADJUST=-0.05
    if (gcode.find("SET_GCODE_OFFSET") != std::string::npos) {
        // Parse Z parameter (absolute offset)
        auto z_pos = gcode.find(" Z=");
        if (z_pos != std::string::npos) {
            try {
                double z_offset = std::stod(gcode.substr(z_pos + 3));
                gcode_offset_z_.store(z_offset);
                spdlog::info("[MoonrakerClientMock] SET_GCODE_OFFSET Z={:.3f}", z_offset);
                dispatch_gcode_move_update();
            } catch (...) {
            }
        }

        // Parse Z_ADJUST parameter (relative adjustment)
        auto z_adj_pos = gcode.find("Z_ADJUST=");
        if (z_adj_pos != std::string::npos) {
            try {
                double adjustment = std::stod(gcode.substr(z_adj_pos + 9));
                double new_offset = gcode_offset_z_.load() + adjustment;
                gcode_offset_z_.store(new_offset);
                spdlog::info("[MoonrakerClientMock] SET_GCODE_OFFSET Z_ADJUST={:.3f} -> Z={:.3f}",
                             adjustment, new_offset);
                dispatch_gcode_move_update();
            } catch (...) {
            }
        }
    }

    // Input shaper calibration - SHAPER_CALIBRATE AXIS=X or AXIS=Y
    if (gcode.find("SHAPER_CALIBRATE") != std::string::npos) {
        char axis = 'X'; // Default to X
        if (gcode.find("AXIS=Y") != std::string::npos ||
            gcode.find("AXIS=y") != std::string::npos) {
            axis = 'Y';
        } else if (gcode.find("AXIS=X") != std::string::npos ||
                   gcode.find("AXIS=x") != std::string::npos) {
            axis = 'X';
        }
        spdlog::info("[MoonrakerClientMock] SHAPER_CALIBRATE AXIS={}", axis);
        dispatch_shaper_calibrate_response(axis);
    }

    // SET_INPUT_SHAPER - Apply shaper settings (command handled via execute_gcode success callback)
    if (gcode.find("SET_INPUT_SHAPER") != std::string::npos) {
        spdlog::info("[MoonrakerClientMock] SET_INPUT_SHAPER: {}", gcode);
        // No additional action needed - execute_gcode path already invokes success callback
    }

    // MEASURE_AXES_NOISE - Check accelerometer noise level
    if (gcode.find("MEASURE_AXES_NOISE") != std::string::npos) {
        spdlog::info("[MoonrakerClientMock] MEASURE_AXES_NOISE");
        dispatch_measure_axes_noise_response();
    }

    // Pressure advance (NOT IMPLEMENTED)
    if (gcode.find("SET_PRESSURE_ADVANCE") != std::string::npos) {
        spdlog::warn("[MoonrakerClientMock] STUB: SET_PRESSURE_ADVANCE NOT IMPLEMENTED");
    }

    // LED control - SET_LED LED=<name> RED=<0-1> GREEN=<0-1> BLUE=<0-1> [WHITE=<0-1>]
    if (gcode.find("SET_LED") != std::string::npos) {
        // Parse LED name
        std::string led_name;
        auto led_pos = gcode.find("LED=");
        if (led_pos != std::string::npos) {
            size_t start = led_pos + 4;
            // Handle quoted LED names: LED="name" or LED=name
            if (start < gcode.size() && gcode[start] == '"') {
                start++;
                size_t end = gcode.find('"', start);
                if (end != std::string::npos) {
                    led_name = gcode.substr(start, end - start);
                }
            } else {
                size_t end = gcode.find_first_of(" \t\n", start);
                led_name = gcode.substr(start, end == std::string::npos ? end : end - start);
            }
        }

        // Parse color values (default to 0)
        auto parse_color = [&gcode](const std::string& param) -> double {
            auto pos = gcode.find(param + "=");
            if (pos != std::string::npos) {
                size_t start = pos + param.length() + 1;
                try {
                    return std::clamp(std::stod(gcode.substr(start)), 0.0, 1.0);
                } catch (...) {
                    return 0.0;
                }
            }
            return 0.0;
        };

        double red = parse_color("RED");
        double green = parse_color("GREEN");
        double blue = parse_color("BLUE");
        double white = parse_color("WHITE");

        // Find matching LED in our list (need to match by suffix since command uses short name)
        std::string full_led_name;
        for (const auto& led : leds_) {
            // Match if LED name ends with the command's led_name
            // e.g., "neopixel chamber_light" matches "chamber_light"
            if (led.length() >= led_name.length()) {
                size_t suffix_start = led.length() - led_name.length();
                if (led.substr(suffix_start) == led_name) {
                    full_led_name = led;
                    break;
                }
            }
        }

        if (!full_led_name.empty()) {
            // Update LED state
            {
                std::lock_guard<std::mutex> lock(led_mutex_);
                led_states_[full_led_name] = LedColor{red, green, blue, white};
            }

            spdlog::info("[MoonrakerClientMock] SET_LED: {} R={:.2f} G={:.2f} B={:.2f} W={:.2f}",
                         full_led_name, red, green, blue, white);

            // Dispatch LED state update notification (like real Moonraker would)
            json led_status;
            {
                std::lock_guard<std::mutex> lock(led_mutex_);
                for (const auto& [name, color] : led_states_) {
                    led_status[name] = {
                        {"color_data", json::array({{color.r, color.g, color.b, color.w}})}};
                }
            }
            dispatch_status_update(led_status);
        } else {
            spdlog::warn("[MoonrakerClientMock] SET_LED: unknown LED '{}'", led_name);
        }
    }

    // Firmware/Klipper restart - simulates klippy_state transition
    // FIRMWARE_RESTART: Full firmware reset (~3s delay)
    // RESTART: Klipper service restart (~2s delay)
    if (gcode.find("FIRMWARE_RESTART") != std::string::npos) {
        trigger_restart(/*is_firmware=*/true);
    } else if (gcode.find("RESTART") != std::string::npos &&
               gcode.find("FIRMWARE") == std::string::npos) {
        trigger_restart(/*is_firmware=*/false);
    }

    // ========================================================================
    // Z-OFFSET CALIBRATION COMMANDS (manual probe mode)
    // ========================================================================

    // PROBE_CALIBRATE or Z_ENDSTOP_CALIBRATE - Start Z-offset calibration
    // - PROBE_CALIBRATE: For printers with probe (BLTouch, inductive, etc.)
    // - Z_ENDSTOP_CALIBRATE: For printers with only mechanical Z endstop
    // Both enter manual probe mode, home if needed, and work identically
    bool is_probe_calibrate = gcode.find("PROBE_CALIBRATE") != std::string::npos;
    bool is_endstop_calibrate = gcode.find("Z_ENDSTOP_CALIBRATE") != std::string::npos;

    if (is_probe_calibrate || is_endstop_calibrate) {
        const char* cmd_name = is_probe_calibrate ? "PROBE_CALIBRATE" : "Z_ENDSTOP_CALIBRATE";

        if (!manual_probe_active_.load()) {
            // Ensure we're homed first
            {
                std::lock_guard<std::mutex> lock(homed_axes_mutex_);
                if (homed_axes_.find("xyz") == std::string::npos) {
                    // Auto-home like real Klipper would
                    homed_axes_ = "xyz";
                    pos_x_.store(0.0);
                    pos_y_.store(0.0);
                    pos_z_.store(0.0);
                    spdlog::info("[MoonrakerClientMock] {}: Auto-homed all axes", cmd_name);
                }
            }

            // Enter manual probe mode at a starting Z height
            manual_probe_active_.store(true);
            manual_probe_z_.store(5.0); // Start 5mm above bed
            pos_z_.store(5.0);          // Sync toolhead Z

            spdlog::info("[MoonrakerClientMock] {}: Entered manual probe mode, Z={:.3f}", cmd_name,
                         manual_probe_z_.load());

            // Dispatch manual probe state change
            dispatch_manual_probe_update();
        } else {
            spdlog::warn("[MoonrakerClientMock] {}: Already in manual probe mode", cmd_name);
        }
    }

    // TESTZ Z=<value> - Adjust Z position during manual probe calibration
    // Z can be absolute (Z=0.1) or relative (Z=+0.1 or Z=-0.05)
    if (gcode.find("TESTZ") != std::string::npos) {
        if (!manual_probe_active_.load()) {
            spdlog::warn("[MoonrakerClientMock] TESTZ: Not in manual probe mode (ignored)");
            return 0;
        }
        size_t z_pos = gcode.find("Z=");
        if (z_pos != std::string::npos) {
            std::string z_str = gcode.substr(z_pos + 2);
            try {
                // Check for relative move (+/- prefix)
                bool is_relative = (z_str[0] == '+' || z_str[0] == '-');
                double z_value = std::stod(z_str);

                double new_z;
                if (is_relative) {
                    new_z = manual_probe_z_.load() + z_value;
                } else {
                    new_z = z_value;
                }

                // Clamp to reasonable range (0 to 10mm above bed)
                new_z = std::clamp(new_z, -0.5, 10.0);

                manual_probe_z_.store(new_z);
                pos_z_.store(new_z); // Sync toolhead Z

                spdlog::info("[MoonrakerClientMock] TESTZ: Z={:.3f} ({}) -> new Z={:.3f}", z_value,
                             is_relative ? "relative" : "absolute", new_z);

                // Dispatch Z position update
                dispatch_manual_probe_update();
            } catch (const std::exception& e) {
                spdlog::warn("[MoonrakerClientMock] TESTZ: Failed to parse Z value: {}", e.what());
            }
        }
    }

    // ACCEPT - Accept current Z position as the calibrated offset
    if (gcode == "ACCEPT" || gcode.find("ACCEPT ") == 0) {
        if (manual_probe_active_.load()) {
            double final_z = manual_probe_z_.load();
            manual_probe_active_.store(false);

            spdlog::info(
                "[MoonrakerClientMock] ACCEPT: Z-offset calibration complete, offset={:.3f}mm",
                final_z);

            // In real Klipper, this would update probe z_offset in config
            // User typically follows with SAVE_CONFIG to persist

            // Dispatch manual probe state change (is_active=false)
            dispatch_manual_probe_update();
        } else {
            spdlog::warn("[MoonrakerClientMock] ACCEPT: Not in manual probe mode");
        }
    }

    // ABORT - Cancel manual probe calibration
    if (gcode == "ABORT" || gcode.find("ABORT ") == 0) {
        if (manual_probe_active_.load()) {
            manual_probe_active_.store(false);
            spdlog::info("[MoonrakerClientMock] ABORT: Manual probe cancelled");

            // Dispatch manual probe state change (is_active=false)
            dispatch_manual_probe_update();
        }
    }

    // EXCLUDE_OBJECT - Track excluded objects during print
    // EXCLUDE_OBJECT NAME=Part_1
    // EXCLUDE_OBJECT NAME="Part With Spaces"
    if (gcode.find("EXCLUDE_OBJECT") != std::string::npos &&
        gcode.find("EXCLUDE_OBJECT_DEFINE") == std::string::npos &&
        gcode.find("EXCLUDE_OBJECT_START") == std::string::npos &&
        gcode.find("EXCLUDE_OBJECT_END") == std::string::npos) {
        // Parse NAME parameter
        size_t name_pos = gcode.find("NAME=");
        if (name_pos != std::string::npos) {
            size_t start = name_pos + 5;
            std::string object_name;

            // Handle quoted names (NAME="Part With Spaces")
            if (start < gcode.length() && gcode[start] == '"') {
                size_t end_quote = gcode.find('"', start + 1);
                if (end_quote != std::string::npos) {
                    object_name = gcode.substr(start + 1, end_quote - start - 1);
                }
            } else {
                // Unquoted name (ends at space or end of string)
                size_t end = gcode.find_first_of(" \t\n", start);
                object_name = (end != std::string::npos) ? gcode.substr(start, end - start)
                                                         : gcode.substr(start);
            }

            if (!object_name.empty()) {
                // Update shared state if available
                if (mock_state_) {
                    mock_state_->add_excluded_object(object_name);
                }
                // Also update local state for backward compatibility
                {
                    std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
                    excluded_objects_.insert(object_name);
                }
                spdlog::info("[MoonrakerClientMock] EXCLUDE_OBJECT: '{}' added to exclusion list",
                             object_name);

                // Dispatch status update (like real Klipper would via WebSocket)
                // Use local excluded_objects_ (always up-to-date) rather than mock_state_
                // which is only available in test fixtures
                {
                    json excluded_array = json::array();
                    {
                        std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
                        for (const auto& obj : excluded_objects_) {
                            excluded_array.push_back(obj);
                        }
                    }
                    json eo_status = {
                        {"exclude_object",
                         {{"excluded_objects", excluded_array}, {"current_object", nullptr}}}};
                    dispatch_status_update(eo_status);
                }
            }
        } else {
            spdlog::warn("[MoonrakerClientMock] EXCLUDE_OBJECT without NAME parameter ignored");
        }
    }

    // EXCLUDE_OBJECT_DEFINE - Register objects for the print
    if (gcode.find("EXCLUDE_OBJECT_DEFINE") != std::string::npos) {
        size_t name_pos = gcode.find("NAME=");
        if (name_pos != std::string::npos) {
            size_t start = name_pos + 5;
            std::string object_name;
            if (start < gcode.size() && gcode[start] == '"') {
                size_t end = gcode.find('"', start + 1);
                if (end != std::string::npos) {
                    object_name = gcode.substr(start + 1, end - start - 1);
                }
            } else {
                size_t end = gcode.find_first_of(" \t\r\n", start);
                object_name = gcode.substr(start, end - start);
            }
            if (!object_name.empty() && mock_state_) {
                mock_state_->add_object_name(object_name);
                spdlog::debug("[MoonrakerClientMock] EXCLUDE_OBJECT_DEFINE: registered '{}'",
                              object_name);
            }
        }
    }

    // SET_LED_EFFECT EFFECT=<name> - Enable an LED effect
    if (gcode.find("SET_LED_EFFECT") != std::string::npos) {
        size_t effect_pos = gcode.find("EFFECT=");
        if (effect_pos != std::string::npos) {
            size_t start = effect_pos + 7;
            size_t end = gcode.find_first_of(" \t\r\n", start);
            std::string effect_name = gcode.substr(start, end - start);

            spdlog::info("[MoonrakerClientMock] SET_LED_EFFECT: enabling '{}'", effect_name);

            // Build status update: enable the target effect, disable all others
            json effect_status = json::object();
            std::string full_name = "led_effect " + effect_name;

            // Known mock effects
            const std::vector<std::string> known_effects = {
                "led_effect breathing", "led_effect fire_comet", "led_effect rainbow",
                "led_effect static_white"};

            for (const auto& name : known_effects) {
                bool should_enable = (name == full_name);
                effect_status[name] = {{"enabled", should_enable}};
            }

            // Simulate LED color output: each effect has a characteristic color
            // In real Klipper, led_effect continuously updates the neopixel color_data
            struct EffectColor {
                double r, g, b, w;
            };
            static const std::unordered_map<std::string, EffectColor> effect_colors = {
                {"breathing", {0.6, 0.6, 1.0, 0.0}},    // Soft blue-white pulse
                {"fire_comet", {1.0, 0.3, 0.0, 0.0}},   // Orange/fire
                {"rainbow", {0.5, 0.0, 1.0, 0.0}},      // Purple (mid-rainbow)
                {"static_white", {1.0, 1.0, 1.0, 0.0}}, // Pure white
            };

            auto color_it = effect_colors.find(effect_name);
            if (color_it != effect_colors.end()) {
                const auto& c = color_it->second;
                // Update internal LED state and dispatch color_data for all LED strips
                {
                    std::lock_guard<std::mutex> lock(led_mutex_);
                    for (auto& [name, color] : led_states_) {
                        color = LedColor{c.r, c.g, c.b, c.w};
                    }
                }
                json led_status;
                {
                    std::lock_guard<std::mutex> lock(led_mutex_);
                    for (const auto& [name, color] : led_states_) {
                        led_status[name] = {
                            {"color_data", json::array({{color.r, color.g, color.b, color.w}})}};
                    }
                }
                // Merge LED color updates into the effect status dispatch
                effect_status.update(led_status);
            }

            dispatch_status_update(effect_status);
        }
    }

    // STOP_LED_EFFECTS - Disable all LED effects
    if (gcode.find("STOP_LED_EFFECTS") != std::string::npos) {
        spdlog::info("[MoonrakerClientMock] STOP_LED_EFFECTS: disabling all effects");

        json effect_status = json::object();
        const std::vector<std::string> known_effects = {
            "led_effect breathing", "led_effect fire_comet", "led_effect rainbow",
            "led_effect static_white"};

        for (const auto& name : known_effects) {
            effect_status[name] = {{"enabled", false}};
        }

        // Turn LEDs off when effects stop
        {
            std::lock_guard<std::mutex> lock(led_mutex_);
            for (auto& [name, color] : led_states_) {
                color = LedColor{0.0, 0.0, 0.0, 0.0};
            }
        }
        json led_status;
        {
            std::lock_guard<std::mutex> lock(led_mutex_);
            for (const auto& [name, color] : led_states_) {
                led_status[name] = {
                    {"color_data", json::array({{color.r, color.g, color.b, color.w}})}};
            }
        }
        effect_status.update(led_status);

        dispatch_status_update(effect_status);
    }

    // QGL / Z-tilt (NOT IMPLEMENTED)
    if (gcode.find("QUAD_GANTRY_LEVEL") != std::string::npos) {
        spdlog::warn("[MoonrakerClientMock] STUB: QUAD_GANTRY_LEVEL NOT IMPLEMENTED");
    } else if (gcode.find("Z_TILT_ADJUST") != std::string::npos) {
        spdlog::warn("[MoonrakerClientMock] STUB: Z_TILT_ADJUST NOT IMPLEMENTED");
    }

    // Probe (NOT IMPLEMENTED) - excludes PROBE_CALIBRATE which is handled above
    if (gcode.find("PROBE") != std::string::npos && gcode.find("BED_MESH") == std::string::npos &&
        gcode.find("PROBE_CALIBRATE") == std::string::npos) {
        spdlog::warn("[MoonrakerClientMock] STUB: PROBE command not fully implemented");
    }

    // Return error code if any error occurred (like real Moonraker)
    {
        std::lock_guard<std::mutex> lock(gcode_error_mutex_);
        if (!last_gcode_error_.empty()) {
            return 1; // Error - call get_last_gcode_error() for message
        }
    }
    return 0; // Success
}

std::string MoonrakerClientMock::get_print_state_string() const {
    switch (print_state_.load()) {
    case 0:
        return "standby";
    case 1:
        return "printing";
    case 2:
        return "paused";
    case 3:
        return "complete";
    case 4:
        return "cancelled";
    case 5:
        return "error";
    default:
        return "standby";
    }
}

// ============================================================================
// Unified Print Control (internal implementation)
// ============================================================================

bool MoonrakerClientMock::start_print_internal(const std::string& filename) {
    // Build path to test G-code file
    // Handle both bare filenames (e.g., "3DBenchy.gcode") and full paths
    std::string full_path;

    // For modified temp files (.helix_temp/modified_xxx_OriginalName.gcode),
    // extract the original filename to find the real test file for metadata
    std::string lookup_filename = filename;
    if (filename.find(".helix_temp/modified_") != std::string::npos) {
        // Extract original filename: .helix_temp/modified_123456789_OriginalName.gcode
        // -> OriginalName.gcode
        size_t underscore_pos = filename.find('_', filename.find("modified_") + 9);
        if (underscore_pos != std::string::npos) {
            lookup_filename = filename.substr(underscore_pos + 1);
            spdlog::debug("[MoonrakerClientMock] Modified temp file '{}' -> original '{}'",
                          filename, lookup_filename);
        }
    }

    if (lookup_filename.find(RuntimeConfig::TEST_GCODE_DIR) == 0) {
        // Already a full path, use as-is
        full_path = lookup_filename;
    } else {
        // Bare filename, prepend test directory
        full_path = std::string(RuntimeConfig::TEST_GCODE_DIR) + "/" + lookup_filename;
    }

    // Extract metadata from G-code file
    auto meta = helix::gcode::extract_header_metadata(full_path);

    // Populate simulation metadata
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        print_metadata_.estimated_time_seconds =
            (meta.estimated_time_seconds > 0) ? meta.estimated_time_seconds : 300.0;
        print_metadata_.layer_count = (meta.layer_count > 0) ? meta.layer_count : 100;
        print_metadata_.target_bed_temp =
            (meta.first_layer_bed_temp > 0) ? meta.first_layer_bed_temp : 60.0;
        print_metadata_.target_nozzle_temp =
            (meta.first_layer_nozzle_temp > 0) ? meta.first_layer_nozzle_temp : 210.0;
        print_metadata_.filament_mm =
            (meta.filament_used_mm > 0) ? meta.filament_used_mm : 5400.0; // Default: ~5.4m
    }

    // Set temperature targets for preheat
    double nozzle_target, bed_target;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        nozzle_target = print_metadata_.target_nozzle_temp;
        bed_target = print_metadata_.target_bed_temp;
    }
    extruder_target_.store(nozzle_target);
    bed_target_.store(bed_target);

    // Reset idle timeout when starting a print
    reset_idle_timeout();

    // Set print filename
    {
        std::lock_guard<std::mutex> lock(print_mutex_);
        print_filename_ = filename;
    }

    // Reset progress and timing
    print_progress_.store(0.0);
    total_pause_duration_sim_ = 0.0;
    preheat_start_time_ = std::chrono::steady_clock::now();
    printing_start_time_.reset();

    // Clear excluded objects from any previous print
    if (mock_state_) {
        mock_state_->clear_excluded_objects();
    }
    {
        std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
        excluded_objects_.clear();
        object_names_.clear();
    }

    // Scan gcode file for EXCLUDE_OBJECT_DEFINE to populate object names
    {
        std::ifstream gcode_file(full_path);
        if (gcode_file.is_open()) {
            std::string line;
            while (std::getline(gcode_file, line)) {
                if (line.find("EXCLUDE_OBJECT_DEFINE") != std::string::npos) {
                    size_t name_pos = line.find("NAME=");
                    if (name_pos != std::string::npos) {
                        size_t start = name_pos + 5;
                        std::string object_name;
                        if (start < line.size() && line[start] == '"') {
                            size_t end = line.find('"', start + 1);
                            if (end != std::string::npos) {
                                object_name = line.substr(start + 1, end - start - 1);
                            }
                        } else {
                            size_t end = line.find_first_of(" \t\r\n", start);
                            object_name = (end != std::string::npos)
                                              ? line.substr(start, end - start)
                                              : line.substr(start);
                        }
                        if (!object_name.empty()) {
                            if (mock_state_) {
                                mock_state_->add_object_name(object_name);
                            }
                            std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
                            if (std::find(object_names_.begin(), object_names_.end(),
                                          object_name) == object_names_.end()) {
                                object_names_.push_back(object_name);
                            }
                        }
                    }
                }
            }
            std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
            if (!object_names_.empty()) {
                spdlog::info("[MoonrakerClientMock] Found {} EXCLUDE_OBJECT_DEFINE objects in '{}'",
                             object_names_.size(), full_path);
            }
        }
    }

    // Parse EXCLUDE_OBJECT_DEFINE lines from gcode to populate defined objects
    // This simulates what Klipper does when it processes the gcode file
    {
        std::vector<std::string> defined_objects;
        std::ifstream gcode_file(full_path);
        if (gcode_file.is_open()) {
            std::string line;
            while (std::getline(gcode_file, line)) {
                if (line.find("EXCLUDE_OBJECT_DEFINE") != std::string::npos) {
                    // Extract NAME= parameter
                    auto name_pos = line.find("NAME=");
                    if (name_pos != std::string::npos) {
                        std::string name;
                        size_t start = name_pos + 5;
                        if (start < line.size() && line[start] == '"') {
                            // Quoted name
                            size_t end = line.find('"', start + 1);
                            if (end != std::string::npos) {
                                name = line.substr(start + 1, end - start - 1);
                            }
                        } else if (start < line.size() && line[start] == '\'') {
                            // Single-quoted name
                            size_t end = line.find('\'', start + 1);
                            if (end != std::string::npos) {
                                name = line.substr(start + 1, end - start - 1);
                            }
                        } else {
                            // Unquoted: ends at space or end of line
                            size_t end = line.find(' ', start);
                            name = line.substr(start, end - start);
                        }
                        if (!name.empty()) {
                            defined_objects.push_back(name);
                            if (mock_state_) {
                                mock_state_->add_object_name(name);
                            }
                        }
                    }
                }
                // Stop scanning after first layer to avoid reading the entire file
                if (line.find(";LAYER_CHANGE") != std::string::npos ||
                    line.find("; LAYER_CHANGE") != std::string::npos) {
                    break;
                }
            }
        }

        if (!defined_objects.empty()) {
            spdlog::info("[MoonrakerClientMock] Found {} defined objects in '{}'",
                         defined_objects.size(), lookup_filename);
            if (mock_state_) {
                mock_state_->set_available_objects(defined_objects);
            }

            // Dispatch exclude_object status update so PrinterState knows about them
            json objects_array = json::array();
            for (const auto& obj_name : defined_objects) {
                objects_array.push_back({{"name", obj_name}});
            }
            json eo_status = {{"exclude_object",
                               {{"objects", objects_array},
                                {"excluded_objects", json::array()},
                                {"current_object", nullptr}}}};
            dispatch_status_update(eo_status);
        }
    }

    // Reset PRINT_START simulation phase tracking for new print
    simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::NONE));

    // Transition to PREHEAT phase
    print_phase_.store(MockPrintPhase::PREHEAT);
    print_state_.store(1); // "printing" for backward compatibility

    spdlog::debug("[MoonrakerClientMock] Starting print '{}': est_time={:.0f}s, layers={}, "
                  "nozzle={:.0f}°C, bed={:.0f}°C",
                  filename, meta.estimated_time_seconds, meta.layer_count, nozzle_target,
                  bed_target);

    dispatch_print_state_notification("printing");
    return true;
}

bool MoonrakerClientMock::pause_print_internal() {
    MockPrintPhase current_phase = print_phase_.load();

    // Can only pause from PRINTING or PREHEAT
    if (current_phase != MockPrintPhase::PRINTING && current_phase != MockPrintPhase::PREHEAT) {
        spdlog::warn("[MoonrakerClientMock] Cannot pause - not currently printing (phase={})",
                     static_cast<int>(current_phase));
        return false;
    }

    // Record pause start time
    pause_start_time_ = std::chrono::steady_clock::now();

    // Transition to PAUSED
    print_phase_.store(MockPrintPhase::PAUSED);
    print_state_.store(2); // "paused" for backward compatibility

    spdlog::info("[MoonrakerClientMock] Print paused at {:.1f}% progress",
                 print_progress_.load() * 100.0);

    dispatch_print_state_notification("paused");
    return true;
}

bool MoonrakerClientMock::resume_print_internal() {
    if (print_phase_.load() != MockPrintPhase::PAUSED) {
        spdlog::warn("[MoonrakerClientMock] Cannot resume - not currently paused");
        return false;
    }

    // Calculate pause duration and add to total
    auto pause_real = std::chrono::steady_clock::now() - pause_start_time_;
    double pause_sim = std::chrono::duration<double>(pause_real).count() * speedup_factor_.load();
    total_pause_duration_sim_ += pause_sim;

    // Resume to PRINTING phase (skip PREHEAT since temps should still be maintained)
    print_phase_.store(MockPrintPhase::PRINTING);
    print_state_.store(1); // "printing" for backward compatibility

    spdlog::info("[MoonrakerClientMock] Print resumed (pause duration: {:.1f}s simulated)",
                 pause_sim);

    dispatch_print_state_notification("printing");
    return true;
}

bool MoonrakerClientMock::cancel_print_internal() {
    MockPrintPhase current_phase = print_phase_.load();

    // Can cancel from any non-idle phase
    if (current_phase == MockPrintPhase::IDLE) {
        spdlog::warn("[MoonrakerClientMock] Cannot cancel - no active print");
        return false;
    }

    // Set targets to 0 (begin cooldown)
    extruder_target_.store(0.0);
    bed_target_.store(0.0);

    // Reset PRINT_START simulation phase
    simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::NONE));

    // Transition to CANCELLED
    print_phase_.store(MockPrintPhase::CANCELLED);
    print_state_.store(4); // "cancelled" for backward compatibility

    spdlog::debug("[MoonrakerClientMock] Print cancelled at {:.1f}% progress",
                  print_progress_.load() * 100.0);

    dispatch_print_state_notification("cancelled");
    return true;
}

void MoonrakerClientMock::emergency_stop_internal() {
    spdlog::warn("[MoonrakerClientMock] Emergency stop executed!");

    // Zero all heater targets
    extruder_target_.store(0.0);
    bed_target_.store(0.0);

    // Turn off all fans
    fan_speed_.store(0);
    {
        std::lock_guard<std::mutex> lock(fan_mutex_);
        for (auto& [name, speed] : fan_speeds_) {
            speed = 0.0;
        }
    }

    // Reset PRINT_START simulation phase
    simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::NONE));

    // Set print state to error (matches real Klipper M112 behavior)
    print_phase_.store(MockPrintPhase::ERROR);
    print_state_.store(5); // error
    dispatch_print_state_notification("error");

    // Set klippy state to SHUTDOWN (must defer to main thread)
    helix::ui::async_call(
        [](void*) { get_printer_state().set_klippy_state_sync(helix::KlippyState::SHUTDOWN); },
        nullptr);
}

bool MoonrakerClientMock::toggle_filament_runout() {
    // Find primary runout sensor from filament_sensors_ list
    std::string runout_sensor;
    for (const auto& sensor : filament_sensors_) {
        if (sensor.find("runout") != std::string::npos) {
            runout_sensor = sensor;
            break;
        }
    }

    // Fallback to first sensor if no "runout" sensor found
    if (runout_sensor.empty() && !filament_sensors_.empty()) {
        runout_sensor = filament_sensors_[0];
    }

    if (runout_sensor.empty()) {
        spdlog::warn("[MoonrakerClientMock] No filament sensor to toggle");
        return false;
    }

    // Toggle state
    bool new_state = !filament_runout_state_.load();
    filament_runout_state_.store(new_state);

    spdlog::info("[MoonrakerClientMock] Filament toggle on '{}': {} -> {}", runout_sensor,
                 new_state ? "empty" : "detected", new_state ? "detected" : "empty");

    // Dispatch status update through normal flow
    json status;
    status[runout_sensor]["filament_detected"] = new_state;
    dispatch_status_update(status);

    // Auto-pause if: runout detected + actively printing + runout modal enabled
    // This simulates Klipper's pause_on_runout behavior
    if (!new_state) { // new_state=false means filament NOT detected (runout)
        MockPrintPhase phase = print_phase_.load();
        if (phase == MockPrintPhase::PRINTING || phase == MockPrintPhase::PREHEAT) {
            if (get_runtime_config()->should_show_runout_modal()) {
                spdlog::info("[MoonrakerClientMock] Filament runout during print - auto-pausing");
                pause_print_internal();
            }
        }
    }

    return true;
}

// ============================================================================
// Simulation Helpers
// ============================================================================

bool MoonrakerClientMock::is_temp_stable(double current, double target, double tolerance) const {
    return std::abs(current - target) <= tolerance;
}

void MoonrakerClientMock::advance_print_progress(double dt_simulated) {
    double total_time;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        total_time = print_metadata_.estimated_time_seconds;
    }

    if (total_time <= 0) {
        return;
    }

    double rate = 1.0 / total_time; // Progress per simulated second
    double current = print_progress_.load();
    print_progress_.store(std::min(1.0, current + rate * dt_simulated));
}

void MoonrakerClientMock::dispatch_print_state_notification(const std::string& state) {
    // Include filename in state notifications so observers can update immediately
    // This is critical for PrintStatusPanel to load the thumbnail when print starts
    std::string filename;
    {
        std::lock_guard<std::mutex> lock(print_mutex_);
        filename = print_filename_;
    }
    spdlog::debug(
        "[MoonrakerClientMock] dispatch_print_state_notification: state='{}' filename='{}'", state,
        filename);
    json notification_status = {{"print_stats", {{"state", state}, {"filename", filename}}}};
    dispatch_status_update(notification_status);
}

void MoonrakerClientMock::dispatch_enhanced_print_status() {
    double progress = print_progress_.load();
    int current_layer = get_current_layer();
    int total_layers;
    double total_time;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        total_layers = static_cast<int>(print_metadata_.layer_count);
        total_time = print_metadata_.estimated_time_seconds;
    }

    double elapsed = progress * total_time;

    std::string filename;
    double filament_total_mm;
    {
        std::lock_guard<std::mutex> lock(print_mutex_);
        filename = print_filename_;
    }
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        filament_total_mm = print_metadata_.filament_mm;
    }

    // Simulate filament consumption proportional to progress
    double filament_used = (filament_total_mm > 0) ? progress * filament_total_mm : 0.0;

    MockPrintPhase phase = print_phase_.load();
    bool is_active = (phase == MockPrintPhase::PRINTING || phase == MockPrintPhase::PREHEAT);

    json status = {{"print_stats",
                    {{"state", get_print_state_string()},
                     {"filename", filename},
                     {"print_duration", elapsed},
                     {"total_duration", elapsed},    // Wall-clock elapsed (matches real Moonraker)
                     {"estimated_time", total_time}, // Slicer estimate (for completion modal)
                     {"filament_used", filament_used},
                     {"message", ""},
                     {"info", {{"current_layer", current_layer}, {"total_layer", total_layers}}}}},
                   {"virtual_sdcard",
                    {{"file_path", filename}, {"progress", progress}, {"is_active", is_active}}}};

    // Build exclude_object status with defined objects and excluded state
    {
        json objects_array = json::array();
        json excluded_array = json::array();
        std::string current;

        if (mock_state_) {
            // Use shared state (thread-safe copies via internal mutex)
            auto names = mock_state_->get_object_names();
            auto excl = mock_state_->get_excluded_objects();
            for (const auto& name : names) {
                objects_array.push_back({{"name", name}});
            }
            for (const auto& obj : excl) {
                excluded_array.push_back(obj);
            }
            // Pick first non-excluded object as current during active printing
            if (!names.empty() && is_active) {
                for (const auto& n : names) {
                    if (excl.count(n) == 0) {
                        current = n;
                        break;
                    }
                }
            }
        } else {
            // Fallback to local state for backward compatibility
            std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
            for (const auto& name : object_names_) {
                objects_array.push_back({{"name", name}});
            }
            for (const auto& obj : excluded_objects_) {
                excluded_array.push_back(obj);
            }
            // Pick first non-excluded object as current during active printing
            if (!object_names_.empty() && is_active) {
                for (const auto& n : object_names_) {
                    if (excluded_objects_.count(n) == 0) {
                        current = n;
                        break;
                    }
                }
            }
        }

        status["exclude_object"] = {
            {"objects", objects_array},
            {"excluded_objects", excluded_array},
            {"current_object", current.empty() ? json(nullptr) : json(current)}};
    }

    dispatch_status_update(status);
}

// ============================================================================
// Temperature Simulation
// ============================================================================

void MoonrakerClientMock::dispatch_initial_state() {
    // Build initial state JSON matching real Moonraker subscription response format
    // Uses current simulated values (room temp by default, or preset values if set)
    double ext_temp = extruder_temp_.load();
    double ext_target = extruder_target_.load();
    double bed_temp_val = bed_temp_.load();
    double bed_target_val = bed_target_.load();
    double x = pos_x_.load();
    double y = pos_y_.load();
    double z = pos_z_.load();
    int speed = speed_factor_.load();
    int flow = flow_factor_.load();
    int fan = fan_speed_.load();

    // Get homed_axes with thread safety
    std::string homed;
    {
        std::lock_guard<std::mutex> lock(homed_axes_mutex_);
        homed = homed_axes_;
    }

    // Get print state with thread safety
    std::string print_state_str = get_print_state_string();
    std::string filename;
    {
        std::lock_guard<std::mutex> lock(print_mutex_);
        filename = print_filename_;
    }
    double progress = print_progress_.load();

    // Convert probed_matrix to JSON 2D array
    json probed_matrix_json = json::array();
    for (const auto& row : active_bed_mesh_.probed_matrix) {
        json row_json = json::array();
        for (float val : row) {
            row_json.push_back(val);
        }
        probed_matrix_json.push_back(row_json);
    }

    // Build profiles object with full mesh data (Moonraker format)
    json profiles_json = json::object();
    for (const auto& [name, profile] : stored_bed_mesh_profiles_) {
        json points_json = json::array();
        for (const auto& row : profile.probed_matrix) {
            json row_json = json::array();
            for (float val : row) {
                row_json.push_back(val);
            }
            points_json.push_back(row_json);
        }
        profiles_json[name] = {{"points", points_json},
                               {"mesh_params",
                                {{"min_x", profile.mesh_min[0]},
                                 {"min_y", profile.mesh_min[1]},
                                 {"max_x", profile.mesh_max[0]},
                                 {"max_y", profile.mesh_max[1]},
                                 {"x_count", profile.x_count},
                                 {"y_count", profile.y_count}}}};
    }

    // Build LED state JSON
    json led_json = json::object();
    {
        std::lock_guard<std::mutex> lock(led_mutex_);
        for (const auto& [name, color] : led_states_) {
            led_json[name] = {{"color_data", json::array({{color.r, color.g, color.b, color.w}})}};
        }
    }
    // Add output_pin status (uses {value:} format, not color_data)
    for (const auto& led : leds_) {
        if (led.rfind("output_pin ", 0) == 0) {
            led_json[led] = {{"value", 0.75}}; // Mock: enclosure at 75% brightness
        }
    }

    // Get Z offset and klippy state
    double z_offset = gcode_offset_z_.load();
    KlippyState klippy = klippy_state_.load();
    std::string klippy_str = "ready";
    switch (klippy) {
    case KlippyState::STARTUP:
        klippy_str = "startup";
        break;
    case KlippyState::SHUTDOWN:
        klippy_str = "shutdown";
        break;
    case KlippyState::ERROR:
        klippy_str = "error";
        break;
    default:
        break;
    }

    json initial_status = {
        {"extruder", {{"temperature", ext_temp}, {"target", ext_target}}},
        {"heater_bed", {{"temperature", bed_temp_val}, {"target", bed_target_val}}},
        {"toolhead",
         {{"position", {x, y, z, 0.0}},
          {"homed_axes", homed},
          {"kinematics", hardware_.kinematics()}}},
        {"gcode_move",
         {{"gcode_position", {x, y, z, 0.0}}, // Commanded position (same as toolhead in mock)
          {"speed_factor", speed / 100.0},
          {"extrude_factor", flow / 100.0},
          {"homing_origin", {0.0, 0.0, z_offset, 0.0}}}},
        {"fan", {{"speed", fan / 255.0}}},
        {"webhooks", {{"state", klippy_str}, {"state_message", "Printer is ready"}}},
        {"print_stats", {{"state", print_state_str}, {"filename", filename}}},
        {"virtual_sdcard", {{"progress", progress}}},
        {"bed_mesh",
         {{"profile_name", active_bed_mesh_.name},
          {"probed_matrix", probed_matrix_json},
          {"mesh_min", {active_bed_mesh_.mesh_min[0], active_bed_mesh_.mesh_min[1]}},
          {"mesh_max", {active_bed_mesh_.mesh_max[0], active_bed_mesh_.mesh_max[1]}},
          {"profiles", profiles_json},
          {"mesh_params", {{"algo", active_bed_mesh_.algo}}}}}};

    // Include exclude_object initial state (empty - no objects defined until print starts)
    initial_status["exclude_object"] = {{"objects", json::array()},
                                        {"excluded_objects", json::array()},
                                        {"current_object", nullptr}};

    // Merge LED states into initial_status (each LED is a top-level key)
    for (auto& [key, value] : led_json.items()) {
        initial_status[key] = value;
    }

    // Override fan speeds with explicitly-set values from fan_speeds_ map
    {
        std::lock_guard<std::mutex> lock(fan_mutex_);
        for (const auto& [name, spd] : fan_speeds_) {
            if (name == "fan") {
                initial_status["fan"] = {{"speed", spd}};
            } else {
                initial_status[name] = {{"speed", spd}};
            }
        }
    }

    // Add temperature sensor data for all sensors in the sensors_ list
    for (const auto& s : sensors_) {
        if (s.rfind("temperature_sensor ", 0) == 0) {
            std::string sensor_name = s.substr(19);
            double temp = 25.0;
            if (sensor_name.find("chamber") != std::string::npos) {
                temp = chamber_temp_.load();
            } else if (sensor_name.find("mcu") != std::string::npos) {
                temp = mcu_temp_.load();
            } else if (sensor_name.find("raspberry") != std::string::npos ||
                       sensor_name.find("host") != std::string::npos || sensor_name == "rpi") {
                temp = host_temp_.load();
            } else {
                temp = 30.0; // Generic sensor initial value
            }
            initial_status[s] = {{"temperature", temp}};
        } else if (s.rfind("temperature_fan ", 0) == 0) {
            initial_status[s] = {{"temperature", 35.0}, {"target", 40.0}, {"speed", 0.0}};
        }
    }

    // Add filament sensor states
    // Check HELIX_MOCK_FILAMENT_STATE env var for initial state (default: detected)
    // Format: "sensor:state,sensor:state" e.g., "fsensor:empty" or "fsensor:detected,encoder:empty"
    bool default_detected = true;
    const char* state_env = std::getenv("HELIX_MOCK_FILAMENT_STATE");
    std::map<std::string, bool> sensor_states;

    if (state_env) {
        // Parse state overrides
        std::string states_str(state_env);
        size_t pos = 0;
        while ((pos = states_str.find(',')) != std::string::npos || !states_str.empty()) {
            std::string token = (pos != std::string::npos) ? states_str.substr(0, pos) : states_str;
            size_t colon = token.find(':');
            if (colon != std::string::npos) {
                std::string name = token.substr(0, colon);
                std::string state = token.substr(colon + 1);
                sensor_states[name] = (state != "empty" && state != "0" && state != "false");
            }
            if (pos == std::string::npos)
                break;
            states_str.erase(0, pos + 1);
        }
    }

    // Add state for each discovered filament sensor
    for (const auto& sensor : filament_sensors_) {
        // Extract sensor name from "filament_switch_sensor fsensor" -> "fsensor"
        size_t space = sensor.rfind(' ');
        std::string short_name = (space != std::string::npos) ? sensor.substr(space + 1) : sensor;

        bool detected = default_detected;
        auto it = sensor_states.find(short_name);
        if (it != sensor_states.end()) {
            detected = it->second;
        }

        // Filament sensor state format from Klipper
        initial_status[sensor] = {{"filament_detected", detected}, {"enabled", true}};
    }

    spdlog::debug("[MoonrakerClientMock] Dispatching initial state: extruder={}/{}°C, bed={}/{}°C, "
                  "homed_axes='{}', leds={}, filament_sensors={}",
                  ext_temp, ext_target, bed_temp_val, bed_target_val, homed, led_json.size(),
                  filament_sensors_.size());

    // Use the base class dispatch method (same as real client)
    dispatch_status_update(initial_status);
}

void MoonrakerClientMock::dispatch_historical_temperatures() {
    // Generate 2-3 minutes of synthetic temperature history
    // At 250ms intervals, that's ~600 data points for 2.5 minutes
    constexpr int HISTORY_DURATION_MS = 150000; // 2.5 minutes of history
    constexpr int SAMPLE_INTERVAL_MS = 250;     // Same as SIMULATION_INTERVAL_MS
    constexpr int HISTORY_SAMPLES = HISTORY_DURATION_MS / SAMPLE_INTERVAL_MS;

    spdlog::debug(
        "[MoonrakerClientMock] Dispatching {} historical temperature samples ({} seconds)",
        HISTORY_SAMPLES, HISTORY_DURATION_MS / 1000);

    // Simulate a realistic temperature profile: heating up to ~60°C then partial cooldown
    // This creates an interesting curve for debugging/visualization
    //
    // Profile: Start at room temp -> heat to 60°C (extruder) / 40°C (bed) -> partial cooldown
    // Timing: ~50s heating, ~30s hold, ~70s cooling (ends at ~35°C extruder, ~30°C bed)
    constexpr double PEAK_EXTRUDER_TEMP = 60.0;
    constexpr double PEAK_BED_TEMP = 40.0;
    constexpr int HEAT_PHASE_SAMPLES = 200; // ~50 seconds at 250ms = 200 samples
    constexpr int HOLD_PHASE_SAMPLES = 120; // ~30 seconds hold at peak
    // Cooling phase = remaining samples (~70s, cools extruder ~20°C to ~40°C)

    // Copy callbacks to avoid holding lock during dispatch
    std::vector<std::function<void(json)>> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_copy.reserve(notify_callbacks_.size());
        for (const auto& [id, cb] : notify_callbacks_) {
            callbacks_copy.push_back(cb);
        }
    }

    // If no callbacks registered yet, skip (caller should register before connect)
    if (callbacks_copy.empty()) {
        spdlog::warn(
            "[MoonrakerClientMock] No callbacks registered for historical temps - skipping");
        return;
    }

    // Generate and dispatch historical samples with realistic noise
    double ext_temp_hist = ROOM_TEMP;
    double bed_temp_hist = ROOM_TEMP;
    const double dt_sec = SAMPLE_INTERVAL_MS / 1000.0;

    // Simple pseudo-random number generator for deterministic noise
    // (Avoids std::random_device which could affect startup time)
    auto pseudo_random = [](int seed) -> double {
        // Linear congruential generator with normalized output [-1, 1]
        static uint32_t state = 12345;
        state = (state * 1103515245 + seed + 12345) & 0x7fffffff;
        return (static_cast<double>(state) / 0x3fffffff) - 1.0;
    };

    for (int i = 0; i < HISTORY_SAMPLES; i++) {
        // Calculate simulated timestamp (negative = in the past)
        double timestamp_sec = -((HISTORY_SAMPLES - i) * dt_sec);

        // Update base temperatures based on phase
        if (i < HEAT_PHASE_SAMPLES) {
            // Heating phase: ramp up to peak (slightly faster at start, slower near target)
            double progress = static_cast<double>(i) / HEAT_PHASE_SAMPLES;
            double rate_multiplier = 1.0 + 0.3 * (1.0 - progress); // Faster early, slower late
            ext_temp_hist += EXTRUDER_HEAT_RATE * dt_sec * rate_multiplier;
            if (ext_temp_hist > PEAK_EXTRUDER_TEMP)
                ext_temp_hist = PEAK_EXTRUDER_TEMP;

            bed_temp_hist += BED_HEAT_RATE * dt_sec * rate_multiplier;
            if (bed_temp_hist > PEAK_BED_TEMP)
                bed_temp_hist = PEAK_BED_TEMP;
        } else if (i < HEAT_PHASE_SAMPLES + HOLD_PHASE_SAMPLES) {
            // Hold phase: PID oscillation around target (realistic behavior)
            double offset = i - HEAT_PHASE_SAMPLES;
            ext_temp_hist =
                PEAK_EXTRUDER_TEMP + 0.8 * std::sin(offset * 0.15) + 0.3 * std::cos(offset * 0.31);
            bed_temp_hist =
                PEAK_BED_TEMP + 0.4 * std::sin(offset * 0.12) + 0.15 * std::cos(offset * 0.27);
        } else {
            // Cooling phase: exponential decay (more realistic than linear)
            int cool_sample = i - HEAT_PHASE_SAMPLES - HOLD_PHASE_SAMPLES;
            double cool_time = cool_sample * dt_sec;
            // Exponential decay: T(t) = T_ambient + (T_0 - T_ambient) * e^(-t/tau)
            double ext_tau = 40.0; // Extruder thermal time constant (seconds)
            double bed_tau = 80.0; // Bed thermal time constant (slower)
            ext_temp_hist =
                ROOM_TEMP + (PEAK_EXTRUDER_TEMP - ROOM_TEMP) * std::exp(-cool_time / ext_tau);
            bed_temp_hist =
                ROOM_TEMP + (PEAK_BED_TEMP - ROOM_TEMP) * std::exp(-cool_time / bed_tau);
        }

        // Add realistic sensor noise (±0.3°C for extruder, ±0.2°C for bed)
        double ext_noise = pseudo_random(i * 2) * 0.3;
        double bed_noise = pseudo_random(i * 2 + 1) * 0.2;

        double ext_with_noise = ext_temp_hist + ext_noise;
        double bed_with_noise = bed_temp_hist + bed_noise;

        // Build minimal status object (only temperature data needed for graphs)
        json status_obj = {{"extruder", {{"temperature", ext_with_noise}, {"target", 0.0}}},
                           {"heater_bed", {{"temperature", bed_with_noise}, {"target", 0.0}}}};

        // Add historical temperature data for all temperature sensors
        for (const auto& s : sensors_) {
            if (s.rfind("temperature_sensor ", 0) == 0) {
                std::string sensor_name = s.substr(19);
                double temp = 25.0;
                double noise = pseudo_random(i * 3) * 0.5;
                if (sensor_name.find("chamber") != std::string::npos) {
                    constexpr double CHAMBER_MIN = 25.0, CHAMBER_MAX = 45.0, CHAMBER_PERIOD = 120.0;
                    double mid = (CHAMBER_MIN + CHAMBER_MAX) / 2.0;
                    double amp = (CHAMBER_MAX - CHAMBER_MIN) / 2.0;
                    temp = mid + amp * std::sin(2.0 * M_PI * timestamp_sec / CHAMBER_PERIOD);
                } else if (sensor_name.find("mcu") != std::string::npos) {
                    temp = 42.0 + 3.0 * std::sin(2.0 * M_PI * timestamp_sec / 120.0);
                } else if (sensor_name.find("raspberry") != std::string::npos ||
                           sensor_name.find("host") != std::string::npos || sensor_name == "rpi") {
                    temp = 52.0 + 4.0 * std::sin(2.0 * M_PI * timestamp_sec / 75.0);
                } else {
                    temp = 30.0 + 2.0 * std::sin(2.0 * M_PI * timestamp_sec / 100.0);
                }
                status_obj[s] = {{"temperature", temp + noise}};
            } else if (s.rfind("temperature_fan ", 0) == 0) {
                double temp = 35.0 + 3.0 * std::sin(2.0 * M_PI * timestamp_sec / 80.0);
                status_obj[s] = {{"temperature", temp}, {"target", 40.0}, {"speed", 0.5}};
            }
        }

        json notification = {{"method", "notify_status_update"},
                             {"params", json::array({status_obj, timestamp_sec})}};

        // Dispatch to all callbacks
        for (const auto& cb : callbacks_copy) {
            if (cb) {
                cb(notification);
            }
        }
    }

    // Store final historical values as current temps
    extruder_temp_.store(ext_temp_hist);
    bed_temp_.store(bed_temp_hist);
    // Store chamber temp at midpoint for initial state
    if (has_chamber_sensor()) {
        chamber_temp_.store(35.0);
    }

    spdlog::debug("[MoonrakerClientMock] Historical temps dispatched: final extruder={:.1f}°C, "
                  "bed={:.1f}°C",
                  ext_temp_hist, bed_temp_hist);
}

void MoonrakerClientMock::set_extruder_target(double target) {
    extruder_target_.store(target);
}

void MoonrakerClientMock::set_bed_target(double target) {
    bed_target_.store(target);
}

void MoonrakerClientMock::dispatch_method_callback(const std::string& method, const json& msg) {
    std::vector<std::function<void(json)>> callbacks_to_invoke;

    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        auto method_it = method_callbacks_.find(method);
        if (method_it != method_callbacks_.end()) {
            for (auto& [handler_name, cb] : method_it->second) {
                callbacks_to_invoke.push_back(cb);
            }
        }
    }

    // Invoke callbacks outside the lock to prevent deadlocks
    for (auto& cb : callbacks_to_invoke) {
        cb(msg);
    }
}

void MoonrakerClientMock::start_temperature_simulation() {
    // Use exchange for atomic check-and-set - prevents race condition if called concurrently
    bool was_running = simulation_running_.exchange(true);
    spdlog::debug("[MoonrakerClientMock] start_temperature_simulation: was_running={}",
                  was_running);
    if (was_running) {
        spdlog::warn("[MoonrakerClientMock] Simulation already running, skipping thread start");
        return;
    }

    simulation_thread_ = std::thread(&MoonrakerClientMock::temperature_simulation_loop, this);
    spdlog::debug("[MoonrakerClientMock] Temperature simulation started");
}

void MoonrakerClientMock::stop_temperature_simulation(bool during_destruction) {
    // Use exchange for atomic check-and-clear - prevents double-join race condition
    // This ensures only one caller proceeds to join the thread
    if (!simulation_running_.exchange(false)) {
        return; // Was already stopped (or never started)
    }

    // Wake the simulation thread so it exits promptly instead of waiting for sleep
    sim_cv_.notify_one();

    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }
    // Skip logging during static destruction - spdlog may already be destroyed
    if (!during_destruction) {
        spdlog::info("[MoonrakerClientMock] Temperature simulation stopped");
    }
}

void MoonrakerClientMock::temperature_simulation_loop() {
    spdlog::debug("[MoonrakerClientMock] temperature_simulation_loop ENTERED");
    const double base_dt = SIMULATION_INTERVAL_MS / 1000.0; // Base time step (0.5s)

    while (simulation_running_.load()) {
        uint32_t tick = tick_count_.fetch_add(1);

        // Get speedup factor and calculate effective time step
        double speedup = speedup_factor_.load();
        double effective_dt = base_dt * speedup; // Simulated time step

        // Get current temperature state
        double ext_temp = extruder_temp_.load();
        double ext_target = extruder_target_.load();
        double bed_temp_val = bed_temp_.load();
        double bed_target_val = bed_target_.load();

        // Continuous variation parameters for idle/room temp state
        // Uses sinusoidal waves with different periods to create natural-looking fluctuation
        // This ensures graphs always have data to display during testing
        constexpr double IDLE_VARIATION_AMPLITUDE = 1.5; // +/- 1.5°C variation
        constexpr double EXTRUDER_WAVE_PERIOD = 45.0;    // 45 second period for extruder
        constexpr double BED_WAVE_PERIOD = 60.0;         // 60 second period for bed
        constexpr double PHASE_OFFSET = 1.57;            // Phase offset between heaters (pi/2)

        double sim_time = tick * base_dt; // Simulated elapsed time in seconds

        // Simulate extruder temperature change (scaled by speedup)
        if (ext_target > 0) {
            if (ext_temp < ext_target) {
                ext_temp += EXTRUDER_HEAT_RATE * effective_dt;
                if (ext_temp > ext_target)
                    ext_temp = ext_target;
            } else if (ext_temp > ext_target) {
                ext_temp -= EXTRUDER_COOL_RATE * effective_dt;
                if (ext_temp < ext_target)
                    ext_temp = ext_target;
            }
        } else {
            // Cool toward room temp, then add continuous variation
            if (ext_temp > ROOM_TEMP + IDLE_VARIATION_AMPLITUDE) {
                ext_temp -= EXTRUDER_COOL_RATE * effective_dt;
            } else {
                // At room temp: apply sinusoidal variation for continuous graph updates
                double wave = std::sin(2.0 * M_PI * sim_time / EXTRUDER_WAVE_PERIOD);
                ext_temp = ROOM_TEMP + IDLE_VARIATION_AMPLITUDE * wave;
            }
        }
        extruder_temp_.store(ext_temp);

        // Simulate bed temperature change (scaled by speedup)
        if (bed_target_val > 0) {
            if (bed_temp_val < bed_target_val) {
                bed_temp_val += BED_HEAT_RATE * effective_dt;
                if (bed_temp_val > bed_target_val)
                    bed_temp_val = bed_target_val;
            } else if (bed_temp_val > bed_target_val) {
                bed_temp_val -= BED_COOL_RATE * effective_dt;
                if (bed_temp_val < bed_target_val)
                    bed_temp_val = bed_target_val;
            }
        } else {
            // Cool toward room temp, then add continuous variation
            if (bed_temp_val > ROOM_TEMP + IDLE_VARIATION_AMPLITUDE) {
                bed_temp_val -= BED_COOL_RATE * effective_dt;
            } else {
                // At room temp: apply sinusoidal variation (phase offset from extruder)
                double wave = std::sin(2.0 * M_PI * sim_time / BED_WAVE_PERIOD + PHASE_OFFSET);
                bed_temp_val = ROOM_TEMP + IDLE_VARIATION_AMPLITUDE * wave;
            }
        }
        bed_temp_.store(bed_temp_val);

        // Simulate chamber temperature (passive sensor, slow variation between 25-45°C)
        // Chamber heats up slowly during printing/preheat, cools down when idle
        if (has_chamber_sensor()) {
            constexpr double CHAMBER_MIN = 25.0;
            constexpr double CHAMBER_MAX = 45.0;
            constexpr double CHAMBER_HEAT_RATE = 0.05;   // °C/sec (very slow heating)
            constexpr double CHAMBER_COOL_RATE = 0.02;   // °C/sec (slow passive cooling)
            constexpr double CHAMBER_WAVE_PERIOD = 90.0; // 90 second period for idle variation

            double chamber = chamber_temp_.load();
            MockPrintPhase current_phase = print_phase_.load();

            if (current_phase == MockPrintPhase::PRINTING ||
                current_phase == MockPrintPhase::PREHEAT) {
                // During printing: chamber heats up toward max
                if (chamber < CHAMBER_MAX) {
                    chamber += CHAMBER_HEAT_RATE * effective_dt;
                    if (chamber > CHAMBER_MAX)
                        chamber = CHAMBER_MAX;
                }
            } else {
                // When idle: cool toward room temp with slight variation
                if (chamber > CHAMBER_MIN + 2.0) {
                    chamber -= CHAMBER_COOL_RATE * effective_dt;
                } else {
                    // At minimum: add slow sinusoidal variation
                    double wave = std::sin(2.0 * M_PI * sim_time / CHAMBER_WAVE_PERIOD);
                    chamber = CHAMBER_MIN + 5.0 + 3.0 * wave; // Vary 27-33°C when idle
                }
            }
            chamber_temp_.store(chamber);
        }

        // Simulate MCU temperature (stable 40-55°C, slight load correlation)
        {
            constexpr double MCU_BASE = 42.0;
            constexpr double MCU_WAVE_PERIOD = 120.0;
            constexpr double MCU_AMPLITUDE = 3.0;
            constexpr double MCU_PRINT_OFFSET = 5.0; // Higher during printing

            double mcu = MCU_BASE;
            MockPrintPhase current_phase = print_phase_.load();
            if (current_phase == MockPrintPhase::PRINTING ||
                current_phase == MockPrintPhase::PREHEAT) {
                mcu += MCU_PRINT_OFFSET;
            }
            double wave = std::sin(2.0 * M_PI * sim_time / MCU_WAVE_PERIOD);
            mcu += MCU_AMPLITUDE * wave;
            mcu_temp_.store(mcu);
        }

        // Simulate host/RPi temperature (45-65°C, higher under load)
        {
            constexpr double HOST_BASE = 52.0;
            constexpr double HOST_WAVE_PERIOD = 75.0;
            constexpr double HOST_AMPLITUDE = 4.0;
            constexpr double HOST_PRINT_OFFSET = 8.0;

            double host = HOST_BASE;
            MockPrintPhase current_phase = print_phase_.load();
            if (current_phase == MockPrintPhase::PRINTING ||
                current_phase == MockPrintPhase::PREHEAT) {
                host += HOST_PRINT_OFFSET;
            }
            double wave = std::sin(2.0 * M_PI * sim_time / HOST_WAVE_PERIOD + 1.0);
            host += HOST_AMPLITUDE * wave;
            host_temp_.store(host);
        }

        // ========== Phase-Based Print Simulation ==========
        MockPrintPhase phase = print_phase_.load();

        switch (phase) {
        case MockPrintPhase::IDLE: {
            // Check idle timeout (only when not printing)
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - last_activity_time_).count();

            if (!idle_timeout_triggered_.load() &&
                elapsed >= static_cast<int64_t>(idle_timeout_seconds_.load())) {
                idle_timeout_triggered_.store(true);
                motors_enabled_.store(false);
                spdlog::info("[MoonrakerClientMock] Idle timeout triggered after {}s", elapsed);

                // Dispatch stepper_enable update
                json stepper_status = {{"stepper_enable",
                                        {{"steppers",
                                          {{"stepper_x", false},
                                           {"stepper_y", false},
                                           {"stepper_z", false},
                                           {"extruder", false}}}}}};
                dispatch_status_update(stepper_status);
            }
            break;
        }

        case MockPrintPhase::PREHEAT:
            // Advance PRINT_START simulation (dispatches G-code responses)
            advance_print_start_simulation();

            // Check if both extruder and bed have reached target temps
            if (is_temp_stable(ext_temp, ext_target) &&
                is_temp_stable(bed_temp_val, bed_target_val)) {
                // Dispatch layer 1 marker before transitioning to PRINTING
                uint8_t current_sim_phase = simulated_print_start_phase_.load();
                if (current_sim_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::LAYER_1)) {
                    dispatch_gcode_response("SET_PRINT_STATS_INFO CURRENT_LAYER=1");
                    dispatch_gcode_response("// Layer 1 starting");
                    simulated_print_start_phase_.store(
                        static_cast<uint8_t>(SimulatedPrintStartPhase::LAYER_1));
                }

                // Transition to PRINTING phase
                print_phase_.store(MockPrintPhase::PRINTING);
                printing_start_time_ = std::chrono::steady_clock::now();
                spdlog::debug("[MoonrakerClientMock] Preheat complete - starting print");
            }
            break;

        case MockPrintPhase::PRINTING:
            // Advance print progress based on file-estimated duration
            advance_print_progress(effective_dt);

            // Check for completion
            if (print_progress_.load() >= 1.0) {
                print_phase_.store(MockPrintPhase::COMPLETE);
                print_state_.store(3); // "complete" for backward compatibility
                extruder_target_.store(0.0);
                bed_target_.store(0.0);
                spdlog::info("[MoonrakerClientMock] Print complete!");
                dispatch_print_state_notification("complete");
            }
            break;

        case MockPrintPhase::PAUSED:
            // Temps maintained (targets unchanged), no progress advance
            break;

        case MockPrintPhase::COMPLETE:
        case MockPrintPhase::CANCELLED:
            // Cooling down - transition to IDLE when cool enough
            if (ext_temp < 50.0 && bed_temp_val < 35.0) {
                print_phase_.store(MockPrintPhase::IDLE);
                print_state_.store(0); // "standby" for backward compatibility
                {
                    std::lock_guard<std::mutex> lock(print_mutex_);
                    print_filename_.clear();
                }
                print_progress_.store(0.0);
                {
                    std::lock_guard<std::mutex> lock(metadata_mutex_);
                    print_metadata_.reset();
                }
                spdlog::info("[MoonrakerClientMock] Cooldown complete - returning to idle");
                dispatch_print_state_notification("standby");
            }
            break;

        case MockPrintPhase::ERROR:
            // Stay in error state until explicitly cleared (via new print start)
            break;
        }

        // ========== Position and Motion State ==========
        double x = pos_x_.load();
        double y = pos_y_.load();
        double z = pos_z_.load();

        std::string homed;
        {
            std::lock_guard<std::mutex> lock(homed_axes_mutex_);
            homed = homed_axes_;
        }

        // Simulate speed/flow oscillation (90-110%) - only during printing
        int speed = 100;
        int flow = 100;
        if (phase == MockPrintPhase::PRINTING) {
            speed = 100 + static_cast<int>(10.0 * std::sin(tick / 20.0));
            flow = 100 + static_cast<int>(5.0 * std::cos(tick / 30.0));
        }
        speed_factor_.store(speed);
        flow_factor_.store(flow);

        // Simulate fan ramping up during print (0-255 over 30 simulated seconds)
        int fan = 0;
        if (phase == MockPrintPhase::PRINTING || phase == MockPrintPhase::PREHEAT) {
            fan = std::min(255, static_cast<int>(print_progress_.load() * 255.0));
        }
        fan_speed_.store(fan);

        // ========== Build and Dispatch Status Notification ==========
        std::string print_state_str = get_print_state_string();
        std::string filename;
        {
            std::lock_guard<std::mutex> lock(print_mutex_);
            filename = print_filename_;
        }

        // Get layer info for enhanced status
        int current_layer = get_current_layer();
        int total_layers = get_total_layers();
        double total_time;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            total_time = print_metadata_.estimated_time_seconds;
        }
        double progress = print_progress_.load();
        double elapsed = progress * total_time;

        // Simulate filament consumption proportional to progress
        double filament_total_mm;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            filament_total_mm = print_metadata_.filament_mm;
        }
        double filament_used = (filament_total_mm > 0) ? progress * filament_total_mm : 0.0;

        // Get Z offset for gcode_move
        double z_offset = gcode_offset_z_.load();

        // Build notification JSON (enhanced Moonraker format with layer info)
        json status_obj = {
            {"extruder", {{"temperature", ext_temp}, {"target", ext_target}}},
            {"heater_bed", {{"temperature", bed_temp_val}, {"target", bed_target_val}}},
            {"toolhead",
             {{"position", {x, y, z, 0.0}},
              {"homed_axes", homed},
              {"kinematics", hardware_.kinematics()}}},
            {"gcode_move",
             {{"gcode_position", {x, y, z, 0.0}}, // Commanded position (same as toolhead in mock)
              {"speed_factor", speed / 100.0},
              {"extrude_factor", flow / 100.0},
              {"homing_origin", {0.0, 0.0, z_offset, 0.0}}}},
            {"fan", {{"speed", fan / 255.0}}},
            {"print_stats",
             {{"state", print_state_str},
              {"filename", filename},
              {"print_duration", elapsed},
              {"total_duration", elapsed},    // Wall-clock elapsed (matches real Moonraker)
              {"estimated_time", total_time}, // Slicer estimate (for completion modal)
              {"filament_used", filament_used},
              {"message", ""},
              {"info", {{"current_layer", current_layer}, {"total_layer", total_layers}}}}},
            {"virtual_sdcard",
             {{"file_path", filename},
              {"progress", progress},
              {"is_active",
               phase == MockPrintPhase::PRINTING || phase == MockPrintPhase::PREHEAT}}},
            // display_status: M73 slicer progress + M117 display message
            {"display_status",
             {{"progress", progress},
              {"message",
               [&]() -> json {
                   if (phase == MockPrintPhase::PREHEAT)
                       return "Heating...";
                   if (phase == MockPrintPhase::PRINTING && progress < 0.02)
                       return "Purging nozzle";
                   if (phase == MockPrintPhase::PRINTING)
                       return nullptr; // Most of print has no active message
                   return nullptr;
               }()}}},
            // stepper_enable tracks actual motor driver state (immediate response to M84)
            {"stepper_enable",
             {{"steppers",
               {{"stepper_x", motors_enabled_.load()},
                {"stepper_y", motors_enabled_.load()},
                {"stepper_z", motors_enabled_.load()},
                {"extruder", motors_enabled_.load()}}}}},
            // idle_timeout tracks activity state: "Printing", "Ready", or "Idle" (after timeout)
            {"idle_timeout", {{"state", [&]() -> std::string {
                                   if (phase == MockPrintPhase::PRINTING ||
                                       phase == MockPrintPhase::PREHEAT) {
                                       return "Printing";
                                   } else if (idle_timeout_triggered_.load()) {
                                       return "Idle";
                                   } else {
                                       return "Ready";
                                   }
                               }()}}}};

        // Add klippy state if not ready (only send when abnormal)
        KlippyState klippy = klippy_state_.load();
        if (klippy != KlippyState::READY) {
            std::string state_str;
            switch (klippy) {
            case KlippyState::STARTUP:
                state_str = "startup";
                break;
            case KlippyState::SHUTDOWN:
                state_str = "shutdown";
                break;
            case KlippyState::ERROR:
                state_str = "error";
                break;
            default:
                state_str = "ready";
                break;
            }
            status_obj["webhooks"] = {{"state", state_str}};
        }

        // Override fan speeds with explicitly-set values from fan_speeds_ map
        {
            std::lock_guard<std::mutex> lock(fan_mutex_);
            for (const auto& [name, spd] : fan_speeds_) {
                if (name == "fan") {
                    status_obj["fan"] = {{"speed", spd}};
                } else {
                    status_obj[name] = {{"speed", spd}};
                }
            }
        }

        // Add exclude_object status with defined objects and excluded state
        {
            json objects_array = json::array();
            json excluded_array = json::array();
            std::string current_obj;

            if (mock_state_) {
                auto names = mock_state_->get_object_names();
                auto excl = mock_state_->get_excluded_objects();
                for (const auto& name : names) {
                    objects_array.push_back({{"name", name}});
                }
                for (const auto& obj : excl) {
                    excluded_array.push_back(obj);
                }
                if (!names.empty() &&
                    (phase == MockPrintPhase::PRINTING || phase == MockPrintPhase::PREHEAT)) {
                    for (const auto& n : names) {
                        if (excl.count(n) == 0) {
                            current_obj = n;
                            break;
                        }
                    }
                }
            } else {
                std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
                for (const auto& name : object_names_) {
                    objects_array.push_back({{"name", name}});
                }
                for (const auto& obj : excluded_objects_) {
                    excluded_array.push_back(obj);
                }
                if (!object_names_.empty() &&
                    (phase == MockPrintPhase::PRINTING || phase == MockPrintPhase::PREHEAT)) {
                    for (const auto& n : object_names_) {
                        if (excluded_objects_.count(n) == 0) {
                            current_obj = n;
                            break;
                        }
                    }
                }
            }

            status_obj["exclude_object"] = {
                {"objects", objects_array},
                {"excluded_objects", excluded_array},
                {"current_object", current_obj.empty() ? json(nullptr) : json(current_obj)}};
        }

        // Add temperature sensor data for all sensors in the sensors_ list
        for (const auto& s : sensors_) {
            if (s.rfind("temperature_sensor ", 0) == 0) {
                std::string sensor_name = s.substr(19);
                double temp = 25.0;
                if (sensor_name.find("chamber") != std::string::npos) {
                    temp = chamber_temp_.load();
                } else if (sensor_name.find("mcu") != std::string::npos) {
                    temp = mcu_temp_.load();
                } else if (sensor_name.find("raspberry") != std::string::npos ||
                           sensor_name.find("host") != std::string::npos || sensor_name == "rpi") {
                    temp = host_temp_.load();
                } else {
                    // Generic sensor: slow drift around 30°C
                    temp = 30.0 + 2.0 * std::sin(2.0 * M_PI * sim_time / 100.0);
                }
                status_obj[s] = {{"temperature", temp}};
            } else if (s.rfind("temperature_fan ", 0) == 0) {
                // Temperature fans have temp, target, and speed
                double temp = 35.0 + 3.0 * std::sin(2.0 * M_PI * sim_time / 80.0);
                status_obj[s] = {{"temperature", temp}, {"target", 40.0}, {"speed", 0.5}};
            }
        }

        json notification = {{"method", "notify_status_update"},
                             {"params", json::array({status_obj, tick * base_dt})}};

        // Push notification through all registered callbacks
        // Two-phase: copy under lock, invoke outside to avoid deadlock
        std::vector<std::function<void(json)>> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            callbacks_copy.reserve(notify_callbacks_.size());
            for (const auto& [id, cb] : notify_callbacks_) {
                callbacks_copy.push_back(cb);
            }
        }
        for (const auto& cb : callbacks_copy) {
            if (cb) {
                cb(notification);
            }
        }

        // Log every 40 ticks (~10 seconds) to confirm loop is running
        if (tick % 40 == 0) {
            spdlog::trace("[MoonrakerClientMock] Simulation tick {} - callbacks={}", tick,
                          callbacks_copy.size());
        }

        // Sleep wall-clock interval with early-exit support for clean shutdown
        // Uses condition_variable wait instead of raw sleep so stop_temperature_simulation()
        // can wake the thread immediately instead of waiting for the full interval
        {
            std::unique_lock<std::mutex> lock(sim_mutex_);
            sim_cv_.wait_for(lock, std::chrono::milliseconds(SIMULATION_INTERVAL_MS),
                             [this] { return !simulation_running_.load(); });
        }
    }
    spdlog::debug("[MoonrakerClientMock] temperature_simulation_loop EXITED");
}

// ============================================================================
// Fan Control Helper Methods
// ============================================================================

void MoonrakerClientMock::set_fan_speed_internal(const std::string& fan_name, double speed) {
    {
        std::lock_guard<std::mutex> lock(fan_mutex_);
        fan_speeds_[fan_name] = speed;
    }

    // Also update the legacy fan_speed_ atomic for backward compatibility
    // (only for part cooling fan "fan")
    if (fan_name == "fan") {
        fan_speed_.store(static_cast<int>(speed * 255.0));
    }

    // Dispatch fan status update
    json fan_status;
    if (fan_name == "fan") {
        // Part cooling fan uses simple format
        fan_status["fan"] = {{"speed", speed}};
    } else {
        // Generic/heater fans use full name as key
        fan_status[fan_name] = {{"speed", speed}};
    }
    dispatch_status_update(fan_status);
}

std::string MoonrakerClientMock::find_fan_by_suffix(const std::string& suffix) const {
    for (const auto& fan : fans_) {
        // Match if fan name ends with the suffix (e.g., "nevermore" matches "fan_generic
        // nevermore")
        if (fan.length() >= suffix.length()) {
            size_t suffix_start = fan.length() - suffix.length();
            if (fan.substr(suffix_start) == suffix) {
                return fan;
            }
        }
    }
    return "";
}

// ============================================================================
// G-code Offset Helper Methods
// ============================================================================

void MoonrakerClientMock::dispatch_gcode_move_update() {
    double z_offset = gcode_offset_z_.load();
    int speed = speed_factor_.load();
    int flow = flow_factor_.load();
    double x = pos_x_.load();
    double y = pos_y_.load();
    double z = pos_z_.load();

    json gcode_move = {{"gcode_move",
                        {{"gcode_position", {x, y, z, 0.0}},
                         {"speed_factor", speed / 100.0},
                         {"extrude_factor", flow / 100.0},
                         {"homing_origin", {0.0, 0.0, z_offset, 0.0}}}}};
    dispatch_status_update(gcode_move);
}

// ============================================================================
// Manual Probe Helper Methods (Z-offset calibration)
// ============================================================================

void MoonrakerClientMock::dispatch_manual_probe_update() {
    bool is_active = manual_probe_active_.load();
    double z_position = manual_probe_z_.load();

    // Build manual_probe status matching Klipper's format:
    // {
    //   "manual_probe": {
    //     "is_active": true/false,
    //     "z_position": float,
    //     "z_position_lower": float (optional),
    //     "z_position_upper": float (optional)
    //   }
    // }
    json manual_probe_status = {
        {"manual_probe",
         {{"is_active", is_active},
          {"z_position", z_position},
          {"z_position_lower", nullptr}, // Not tracking bisection search in mock
          {"z_position_upper", nullptr}}}};

    dispatch_status_update(manual_probe_status);

    spdlog::debug("[MoonrakerClientMock] Dispatched manual_probe update: is_active={}, z={:.3f}",
                  is_active, z_position);
}

// ============================================================================
// G-code Response Simulation (for PRINT_START progress tracking)
// ============================================================================

void MoonrakerClientMock::dispatch_gcode_response(const std::string& line) {
    // Build notify_gcode_response message format:
    // {"method": "notify_gcode_response", "params": ["<line>"]}
    json notification = {{"method", "notify_gcode_response"}, {"params", json::array({line})}};

    // Collect callbacks while holding lock, invoke outside
    std::vector<std::function<void(json)>> callbacks_to_invoke;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        auto method_it = method_callbacks_.find("notify_gcode_response");
        if (method_it != method_callbacks_.end()) {
            for (auto& [handler_name, cb] : method_it->second) {
                callbacks_to_invoke.push_back(cb);
            }
        }
    }

    // Invoke callbacks outside lock to prevent deadlock
    for (auto& cb : callbacks_to_invoke) {
        cb(notification);
    }

    spdlog::trace("[MoonrakerClientMock] Dispatched G-code response: {}", line);
}

namespace {

/**
 * @brief Write a mock Klipper-format shaper calibration CSV
 *
 * Generates ~50 frequency bins from 5-200 Hz with a realistic spectrum:
 * base noise floor, a resonance peak, and shaper attenuation curves.
 */
void write_mock_shaper_csv(const std::string& path, char axis) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        spdlog::warn("[MoonrakerClientMock] Failed to write mock CSV to {}", path);
        return;
    }

    // Shaper definitions: name, fitted frequency
    struct ShaperDef {
        const char* name;
        float freq;
    };
    static const ShaperDef shapers[] = {
        {"zv", 59.0f}, {"mzv", 53.8f}, {"ei", 56.2f}, {"2hump_ei", 71.8f}, {"3hump_ei", 89.6f},
    };
    constexpr int num_shapers = 5;

    // Write header line
    ofs << "freq,psd_x,psd_y,psd_z,psd_xyz";
    for (int i = 0; i < num_shapers; ++i) {
        ofs << "," << shapers[i].name << "(" << std::fixed << std::setprecision(1)
            << shapers[i].freq << ")";
    }
    ofs << "\n";

    // RNG for noise variation
    std::mt19937 rng(42 + static_cast<unsigned>(axis)); // Deterministic per-axis
    std::uniform_real_distribution<float> noise_dist(0.8f, 1.2f);

    // Resonance peak parameters — should agree with optimal shaper frequencies above
    const float peak_freq = (axis == 'x' || axis == 'X') ? 53.8f : 48.2f;
    const float peak_width = 8.0f; // Hz bandwidth of resonance
    const float peak_amp = 0.02f;  // Peak amplitude
    const float noise_floor = 5e-4f;

    // Generate ~50 bins from 5 to 200 Hz (step ~4 Hz)
    for (float freq = 5.0f; freq <= 200.0f; freq += 4.0f) {
        // Raw PSD: noise floor + Lorentzian resonance peak
        float df = freq - peak_freq;
        float resonance = peak_amp / (1.0f + (df * df) / (peak_width * peak_width));
        float base_psd = noise_floor * noise_dist(rng) + resonance;

        // High-frequency rolloff above 120 Hz
        if (freq > 120.0f) {
            base_psd *= std::exp(-(freq - 120.0f) / 60.0f);
        }

        // PSD for each axis direction (main axis gets full signal)
        float psd_main = base_psd;
        float psd_cross = base_psd * 0.15f * noise_dist(rng); // Cross-axis coupling
        float psd_z = base_psd * 0.08f * noise_dist(rng);
        float psd_xyz = psd_main + psd_cross + psd_z;

        float psd_x = (axis == 'x' || axis == 'X') ? psd_main : psd_cross;
        float psd_y = (axis == 'y' || axis == 'Y') ? psd_main : psd_cross;

        ofs << std::scientific << std::setprecision(3) << freq << "," << psd_x << "," << psd_y
            << "," << psd_z << "," << psd_xyz;

        // Shaper response curves: attenuate near their fitted frequencies
        for (int i = 0; i < num_shapers; ++i) {
            float shaper_freq_val = shapers[i].freq;
            // Simple notch-filter model: strong attenuation near fitted freq
            float dist = std::abs(freq - shaper_freq_val);
            float attenuation;
            if (dist < 15.0f) {
                // Near the notch: strong attenuation
                attenuation = 0.05f + 0.95f * (dist / 15.0f) * (dist / 15.0f);
            } else {
                attenuation = 1.0f;
            }
            float shaper_val = psd_xyz * attenuation;
            ofs << "," << shaper_val;
        }
        ofs << "\n";
    }

    ofs.close();
    spdlog::info("[MoonrakerClientMock] Wrote mock shaper CSV to {}", path);
}

} // anonymous namespace

void MoonrakerClientMock::dispatch_shaper_calibrate_response(char axis) {
    // Timer-based dispatch for realistic progress animation
    // Matches PID_CALIBRATE timer pattern (line 1389)
    char axis_lower = static_cast<char>(std::tolower(static_cast<unsigned char>(axis)));

    struct ShaperSimState {
        MoonrakerClientMock* mock;
        char axis_lower;
        int step;       // Overall step counter
        int sweep_freq; // Current sweep frequency
    };

    auto* sim = new ShaperSimState{this, axis_lower, 0, 5};

    // Total steps: 20 sweep (5-100 by 5) + ~15 calc lines + 2 final = ~37
    // At 100ms per step = ~3.7 seconds total
    lv_timer_t* timer = lv_timer_create(
        [](lv_timer_t* t) {
            auto* s = static_cast<ShaperSimState*>(lv_timer_get_user_data(t));
            char buf[256];

            // Phase 1: Frequency sweep (steps 0-19)
            if (s->sweep_freq <= 100) {
                snprintf(buf, sizeof(buf), "Testing frequency %.2f Hz",
                         static_cast<float>(s->sweep_freq));
                s->mock->dispatch_gcode_response(buf);
                s->sweep_freq += 5;
                s->step++;
                return;
            }

            // Phase 2: Fitted shapers with max_accel
            // Steps 20+: shaper calculation lines
            int calc_step = s->step - 20;

            struct ShaperData {
                const char* type;
                float freq;
                float vibrations;
                float smoothing;
                int max_accel;
            };

            static const ShaperData shapers[] = {
                {"zv", 59.0f, 5.2f, 0.045f, 13400},      {"mzv", 53.8f, 1.6f, 0.130f, 4000},
                {"ei", 56.2f, 0.7f, 0.120f, 4600},       {"2hump_ei", 71.8f, 0.0f, 0.076f, 8800},
                {"3hump_ei", 89.6f, 0.0f, 0.076f, 8800},
            };

            // Each shaper has 3 lines: "Wait for calculations..", fitted, max_accel
            // So calc_step 0-2 = zv, 3-5 = mzv, 6-8 = ei, 9-11 = 2hump, 12-14 = 3hump
            int shaper_idx = calc_step / 3;
            int sub_step = calc_step % 3;

            if (shaper_idx < 5) {
                const auto& sh = shapers[shaper_idx];
                if (sub_step == 0) {
                    s->mock->dispatch_gcode_response("Wait for calculations..");
                } else if (sub_step == 1) {
                    snprintf(buf, sizeof(buf),
                             "Fitted shaper '%s' frequency = %.1f Hz (vibrations = %.1f%%, "
                             "smoothing ~= %.3f)",
                             sh.type, sh.freq, sh.vibrations, sh.smoothing);
                    s->mock->dispatch_gcode_response(buf);
                } else {
                    snprintf(buf, sizeof(buf),
                             "To avoid too much smoothing with '%s' (scv: 25), suggested max_accel "
                             "<= %d mm/sec^2",
                             sh.type, sh.max_accel);
                    s->mock->dispatch_gcode_response(buf);
                }
                s->step++;
                return;
            }

            // Phase 3: Recommendation + CSV path
            int final_step = calc_step - 15; // 5 shapers * 3 lines = 15
            if (final_step == 0) {
                snprintf(buf, sizeof(buf),
                         "Recommended shaper_type_%c = mzv, shaper_freq_%c = 53.8 Hz",
                         s->axis_lower, s->axis_lower);
                s->mock->dispatch_gcode_response(buf);
                s->step++;
                return;
            }

            if (final_step == 1) {
                // Write actual CSV file so frequency response chart has data
                std::string csv_path = std::string("/tmp/calibration_data_") + s->axis_lower +
                                       std::string("_mock.csv");
                write_mock_shaper_csv(csv_path, s->axis_lower);

                snprintf(
                    buf, sizeof(buf),
                    "Shaper calibration data written to /tmp/calibration_data_%c_mock.csv file",
                    s->axis_lower);
                s->mock->dispatch_gcode_response(buf);
            }

            spdlog::info(
                "[MoonrakerClientMock] Dispatched SHAPER_CALIBRATE response for axis {}",
                static_cast<char>(std::toupper(static_cast<unsigned char>(s->axis_lower))));
            delete s;
            lv_timer_delete(t);
        },
        100, sim); // 100ms between lines for snappy animation

    // Total: 20 sweep + 15 calc + 2 final = 37 steps
    lv_timer_set_repeat_count(timer, 37);

    spdlog::info("[MoonrakerClientMock] Started SHAPER_CALIBRATE timer for axis {}", axis);
}

void MoonrakerClientMock::dispatch_measure_axes_noise_response() {
    // Check if accelerometer is available
    if (!accelerometer_available_) {
        // Dispatch error response simulating missing accelerometer
        dispatch_gcode_response(
            "!! Unknown command:\"MEASURE_AXES_NOISE\". Check [adxl345] config.");
        spdlog::info(
            "[MoonrakerClientMock] Dispatched MEASURE_AXES_NOISE error (no accelerometer)");
        return;
    }

    // Dispatch realistic noise measurement response matching Klipper output format
    // Real Klipper format: "Axes noise for xy-axis accelerometer: 57.956 (x), 103.543 (y), 45.396
    // (z)"
    dispatch_gcode_response(
        "Axes noise for xy-axis accelerometer: 12.345678 (x), 15.678901 (y), 8.234567 (z)");

    spdlog::info("[MoonrakerClientMock] Dispatched MEASURE_AXES_NOISE response");
}

void MoonrakerClientMock::advance_print_start_simulation() {
    // Get current temperatures and targets
    double ext_temp = extruder_temp_.load();
    double ext_target = extruder_target_.load();
    double bed_temp = bed_temp_.load();
    double bed_target = bed_target_.load();

    // Get current simulated phase
    uint8_t current_phase = simulated_print_start_phase_.load();

    // Progress through phases based on temperature state
    // Each phase is dispatched once per print job

    // Phase 1: PRINT_START marker (immediately when print starts)
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::PRINT_START_MARKER)) {
        dispatch_gcode_response(
            "PRINT_START BED_TEMP=" + std::to_string(static_cast<int>(bed_target)) +
            " EXTRUDER_TEMP=" + std::to_string(static_cast<int>(ext_target)));
        simulated_print_start_phase_.store(
            static_cast<uint8_t>(SimulatedPrintStartPhase::PRINT_START_MARKER));
        return; // One phase per tick to spread out messages
    }

    // Phase 2: Homing (a few ticks after start)
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::HOMING)) {
        dispatch_gcode_response("G28");
        dispatch_gcode_response("Homing X Y Z");
        simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::HOMING));
        return;
    }

    // Phase 3: Heating bed (when bed starts warming, ~10% toward target)
    double bed_progress =
        (bed_target > ROOM_TEMP) ? (bed_temp - ROOM_TEMP) / (bed_target - ROOM_TEMP) : 1.0;
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::HEATING_BED) &&
        bed_progress > 0.05) {
        dispatch_gcode_response("M190 S" + std::to_string(static_cast<int>(bed_target)));
        dispatch_gcode_response("Heating bed to " + std::to_string(static_cast<int>(bed_target)) +
                                "C");
        simulated_print_start_phase_.store(
            static_cast<uint8_t>(SimulatedPrintStartPhase::HEATING_BED));
        return;
    }

    // Phase 4: Heating nozzle (when extruder starts warming, ~10% toward target)
    double ext_progress =
        (ext_target > ROOM_TEMP) ? (ext_temp - ROOM_TEMP) / (ext_target - ROOM_TEMP) : 1.0;
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::HEATING_NOZZLE) &&
        ext_progress > 0.05) {
        dispatch_gcode_response("M109 S" + std::to_string(static_cast<int>(ext_target)));
        dispatch_gcode_response("Heating extruder to " +
                                std::to_string(static_cast<int>(ext_target)) + "C");
        simulated_print_start_phase_.store(
            static_cast<uint8_t>(SimulatedPrintStartPhase::HEATING_NOZZLE));
        return;
    }

    // Phase 5: QGL (when bed is ~50% heated - simulate while heating)
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::QGL) && bed_progress > 0.4) {
        dispatch_gcode_response("QUAD_GANTRY_LEVEL");
        dispatch_gcode_response("// Gantry leveling complete");
        simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::QGL));
        return;
    }

    // Phase 6: Bed mesh (when bed is ~70% heated)
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::BED_MESH) &&
        bed_progress > 0.65) {
        dispatch_gcode_response("BED_MESH_CALIBRATE");
        dispatch_gcode_response("// Bed mesh calibration complete");
        simulated_print_start_phase_.store(
            static_cast<uint8_t>(SimulatedPrintStartPhase::BED_MESH));
        return;
    }

    // Phase 7: Purge line (when temps are nearly ready, ~90%)
    if (current_phase < static_cast<uint8_t>(SimulatedPrintStartPhase::PURGING) &&
        bed_progress > 0.85 && ext_progress > 0.85) {
        dispatch_gcode_response("VORON_PURGE");
        dispatch_gcode_response("// Purge complete");
        simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::PURGING));
        return;
    }

    // Phase 8: Layer 1 marker (when transitioning to PRINTING phase)
    // This is handled in the simulation loop when temps are stable
}

// ============================================================================
// Restart Simulation Helper Methods
// ============================================================================

void MoonrakerClientMock::trigger_restart(bool is_firmware) {
    // Set klippy_state to "startup"
    klippy_state_.store(KlippyState::STARTUP);

    // Clear any active print state
    if (print_phase_.load() != MockPrintPhase::IDLE) {
        print_phase_.store(MockPrintPhase::IDLE);
        print_state_.store(0); // standby
        {
            std::lock_guard<std::mutex> lock(print_mutex_);
            print_filename_.clear();
        }
        print_progress_.store(0.0);
    }

    // Set temperature targets to 0 (heaters off) - temps will naturally cool
    extruder_target_.store(0.0);
    bed_target_.store(0.0);

    // Clear excluded objects list (restart clears Klipper state)
    if (mock_state_) {
        mock_state_->clear_excluded_objects();
    }
    {
        std::lock_guard<std::mutex> lock(excluded_objects_mutex_);
        excluded_objects_.clear();
    }

    // Reset PRINT_START simulation phase
    simulated_print_start_phase_.store(static_cast<uint8_t>(SimulatedPrintStartPhase::NONE));

    // Dispatch klippy state change notification
    json status = {{"webhooks",
                    {{"state", "startup"},
                     {"state_message", is_firmware ? "Firmware restart in progress"
                                                   : "Klipper restart in progress"}}}};
    dispatch_status_update(status);

    spdlog::info("[MoonrakerClientMock] {} triggered - klippy_state='startup'",
                 is_firmware ? "FIRMWARE_RESTART" : "RESTART");

    // Schedule return to ready state using tracked thread
    // IMPORTANT: Must track and join - detached threads cause use-after-free during destruction
    double delay_sec = is_firmware ? 3.0 : 2.0;

    // Apply speedup factor to delay
    double effective_delay = delay_sec / speedup_factor_.load();

    // Cancel and wait for any existing restart thread (under lock to prevent race with destructor)
    {
        std::lock_guard<std::mutex> lock(restart_mutex_);
        restart_pending_.store(false);
        if (restart_thread_.joinable()) {
            restart_thread_.join();
        }

        // Launch new restart thread (still under lock to prevent race on assignment)
        restart_pending_.store(true);
        restart_thread_ = std::thread([this, effective_delay, is_firmware]() {
            // Sleep in small increments to allow early exit on destruction
            int total_ms = static_cast<int>(effective_delay * 1000);
            int elapsed_ms = 0;
            constexpr int SLEEP_INTERVAL_MS = 100;

            while (elapsed_ms < total_ms && restart_pending_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_INTERVAL_MS));
                elapsed_ms += SLEEP_INTERVAL_MS;
            }

            // Check if we were cancelled
            if (!restart_pending_.load()) {
                return;
            }

            // Return to ready state
            klippy_state_.store(KlippyState::READY);

            // Dispatch ready notification
            json ready_status = {
                {"webhooks", {{"state", "ready"}, {"state_message", "Printer is ready"}}}};
            dispatch_status_update(ready_status);

            spdlog::info("[MoonrakerClientMock] {} complete - klippy_state='ready'",
                         is_firmware ? "FIRMWARE_RESTART" : "RESTART");

            restart_pending_.store(false);
        });
    } // End of restart_mutex_ lock scope
}
