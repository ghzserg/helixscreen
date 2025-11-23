/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../catch_amalgamated.hpp"
#include "printer_state.h"
#include "../ui_test_utils.h"

using Catch::Approx;

// Test fixture for PrinterState tests
class PrinterStateFixture {
protected:
    void SetUp() {
        lv_init();
        state = std::make_unique<PrinterState>();
        state->init_subjects();
    }

    void TearDown() {
        state.reset();
    }

    std::unique_ptr<PrinterState> state;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_CASE("PrinterState: Initialization sets default values", "[printer_state][init]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    // Temperature subjects should be initialized to 0
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 0);

    // Print progress should be 0
    REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 0);

    // Print state should be "standby"
    const char* print_state = lv_subject_get_string(state.get_print_state_subject());
    REQUIRE(std::string(print_state) == "standby");

    // Position should be 0
    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_position_y_subject()) == 0);
    REQUIRE(lv_subject_get_int(state.get_position_z_subject()) == 0);

    // Speed/flow factors should be 100%
    REQUIRE(lv_subject_get_int(state.get_speed_factor_subject()) == 100);
    REQUIRE(lv_subject_get_int(state.get_flow_factor_subject()) == 100);

    // Fan speed should be 0
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 0);

    // Connection state should be 0 (disconnected)
    REQUIRE(lv_subject_get_int(state.get_connection_state_subject()) == 0);
}

// ============================================================================
// Temperature Update Tests
// ============================================================================

TEST_CASE("PrinterState: Update extruder temperature from notification", "[printer_state][temp]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"extruder", {
                    {"temperature", 205.3},
                    {"target", 210.0}
                }}
            },
            1234567890.0 // eventtime
        }}
    };

    state.update_from_notification(notification);

    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 205);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 210);
}

TEST_CASE("PrinterState: Update bed temperature from notification", "[printer_state][temp]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"heater_bed", {
                    {"temperature", 60.5},
                    {"target", 60.0}
                }}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 60);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 60);
}

TEST_CASE("PrinterState: Temperature rounding edge cases", "[printer_state][temp][edge]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    SECTION("Round down: 205.4 -> 205") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"extruder", {{"temperature", 205.4}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 205);
    }

    SECTION("Round up: 205.6 -> 205") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"extruder", {{"temperature", 205.6}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 205);
    }

    SECTION("Exact integer: 210.0 -> 210") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"extruder", {{"temperature", 210.0}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 210);
    }
}

// ============================================================================
// Print Progress Tests
// ============================================================================

TEST_CASE("PrinterState: Update print progress from notification", "[printer_state][progress]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"virtual_sdcard", {
                    {"progress", 0.45}
                }}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 45);
}

TEST_CASE("PrinterState: Update print state and filename", "[printer_state][progress]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"print_stats", {
                    {"state", "printing"},
                    {"filename", "benchy.gcode"}
                }}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    const char* print_state = lv_subject_get_string(state.get_print_state_subject());
    REQUIRE(std::string(print_state) == "printing");

    const char* filename = lv_subject_get_string(state.get_print_filename_subject());
    REQUIRE(std::string(filename) == "benchy.gcode");
}

TEST_CASE("PrinterState: Progress percentage edge cases", "[printer_state][progress][edge]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    SECTION("0% progress") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"virtual_sdcard", {{"progress", 0.0}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 0);
    }

    SECTION("100% progress") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"virtual_sdcard", {{"progress", 1.0}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 100);
    }

    SECTION("67.3% progress -> 67%") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"virtual_sdcard", {{"progress", 0.673}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 67);
    }
}

// ============================================================================
// Motion/Position Tests
// ============================================================================

TEST_CASE("PrinterState: Update toolhead position", "[printer_state][motion]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"toolhead", {
                    {"position", {125.5, 87.3, 45.2, 1234.5}},
                    {"homed_axes", "xyz"}
                }}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 125);
    REQUIRE(lv_subject_get_int(state.get_position_y_subject()) == 87);
    REQUIRE(lv_subject_get_int(state.get_position_z_subject()) == 45);

    const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
    REQUIRE(std::string(homed) == "xyz");
}

TEST_CASE("PrinterState: Homed axes variations", "[printer_state][motion]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    SECTION("Only X and Y homed") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"toolhead", {{"homed_axes", "xy"}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
        REQUIRE(std::string(homed) == "xy");
    }

    SECTION("No axes homed") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", {{{"toolhead", {{"homed_axes", ""}}}}, 0.0}}
        };
        state.update_from_notification(notification);
        const char* homed = lv_subject_get_string(state.get_homed_axes_subject());
        REQUIRE(std::string(homed) == "");
    }
}

// ============================================================================
// Speed/Flow Factor Tests
// ============================================================================

TEST_CASE("PrinterState: Update speed and flow factors", "[printer_state][speed]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"gcode_move", {
                    {"speed_factor", 1.25},
                    {"extrude_factor", 0.95}
                }}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    REQUIRE(lv_subject_get_int(state.get_speed_factor_subject()) == 125);
    REQUIRE(lv_subject_get_int(state.get_flow_factor_subject()) == 95);
}

TEST_CASE("PrinterState: Update fan speed", "[printer_state][fan]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"fan", {
                    {"speed", 0.75}
                }}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 75);
}

// ============================================================================
// Connection State Tests
// ============================================================================

TEST_CASE("PrinterState: Set connection state", "[printer_state][connection]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    state.set_connection_state(2, "Connected");

    REQUIRE(lv_subject_get_int(state.get_connection_state_subject()) == 2);

    const char* message = lv_subject_get_string(state.get_connection_message_subject());
    REQUIRE(std::string(message) == "Connected");
}

TEST_CASE("PrinterState: Connection state transitions", "[printer_state][connection]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    SECTION("Disconnected -> Connecting") {
        state.set_connection_state(0, "Disconnected");
        state.set_connection_state(1, "Connecting...");
        REQUIRE(lv_subject_get_int(state.get_connection_state_subject()) == 1);
    }

    SECTION("Connecting -> Connected") {
        state.set_connection_state(1, "Connecting...");
        state.set_connection_state(2, "Ready");
        REQUIRE(lv_subject_get_int(state.get_connection_state_subject()) == 2);
    }

    SECTION("Connected -> Reconnecting") {
        state.set_connection_state(2, "Ready");
        state.set_connection_state(3, "Reconnecting...");
        REQUIRE(lv_subject_get_int(state.get_connection_state_subject()) == 3);
    }

    SECTION("Failed connection") {
        state.set_connection_state(4, "Connection failed");
        REQUIRE(lv_subject_get_int(state.get_connection_state_subject()) == 4);
    }
}

// ============================================================================
// Invalid/Malformed Notification Tests
// ============================================================================

TEST_CASE("PrinterState: Ignore invalid notification methods", "[printer_state][error]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "some_other_method"},
        {"params", {
            {{"extruder", {{"temperature", 999.9}}}}
        }}
    };

    state.update_from_notification(notification);

    // Temperature should remain at default (0)
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
}

TEST_CASE("PrinterState: Handle missing fields gracefully", "[printer_state][error]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    SECTION("Missing 'method' field") {
        json notification = {
            {"params", {
                {{"extruder", {{"temperature", 999.9}}}}
            }}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    }

    SECTION("Missing 'params' field") {
        json notification = {
            {"method", "notify_status_update"}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    }

    SECTION("Empty params array") {
        json notification = {
            {"method", "notify_status_update"},
            {"params", json::array()}
        };
        state.update_from_notification(notification);
        REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 0);
    }
}

// ============================================================================
// Comprehensive State Update Tests
// ============================================================================

TEST_CASE("PrinterState: Complete printing state update", "[printer_state][integration]") {
    lv_init();
    PrinterState state;
    state.init_subjects();

    json notification = {
        {"method", "notify_status_update"},
        {"params", {
            {
                {"extruder", {{"temperature", 210.5}, {"target", 210.0}}},
                {"heater_bed", {{"temperature", 60.2}, {"target", 60.0}}},
                {"virtual_sdcard", {{"progress", 0.67}}},
                {"print_stats", {{"state", "printing"}, {"filename", "model.gcode"}}},
                {"toolhead", {{"position", {125.0, 87.0, 45.0, 1234.0}}, {"homed_axes", "xyz"}}},
                {"gcode_move", {{"speed_factor", 1.0}, {"extrude_factor", 1.0}}},
                {"fan", {{"speed", 0.5}}}
            },
            1234567890.0
        }}
    };

    state.update_from_notification(notification);

    // Verify all values updated correctly
    REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 210);
    REQUIRE(lv_subject_get_int(state.get_extruder_target_subject()) == 210);
    REQUIRE(lv_subject_get_int(state.get_bed_temp_subject()) == 60);
    REQUIRE(lv_subject_get_int(state.get_bed_target_subject()) == 60);
    REQUIRE(lv_subject_get_int(state.get_print_progress_subject()) == 67);
    REQUIRE(std::string(lv_subject_get_string(state.get_print_state_subject())) == "printing");
    REQUIRE(std::string(lv_subject_get_string(state.get_print_filename_subject())) == "model.gcode");
    REQUIRE(lv_subject_get_int(state.get_position_x_subject()) == 125);
    REQUIRE(lv_subject_get_int(state.get_position_y_subject()) == 87);
    REQUIRE(lv_subject_get_int(state.get_position_z_subject()) == 45);
    REQUIRE(std::string(lv_subject_get_string(state.get_homed_axes_subject())) == "xyz");
    REQUIRE(lv_subject_get_int(state.get_speed_factor_subject()) == 100);
    REQUIRE(lv_subject_get_int(state.get_flow_factor_subject()) == 100);
    REQUIRE(lv_subject_get_int(state.get_fan_speed_subject()) == 50);
}
