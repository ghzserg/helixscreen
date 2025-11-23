# Running Tests - Quick Reference Guide

This guide provides quick instructions for building and running the HelixScreen test suite.

## Quick Start

```bash
# Build and run all tests
make test

# Build tests only (don't run)
make build/bin/run_tests

# Run tests with specific tags
./build/bin/run_tests "[printer_state]"
```

## Prerequisites

### System Dependencies

```bash
# Install build tools
sudo apt install build-essential cmake clang pkg-config

# Install optional dependencies
sudo apt install libfmt-dev libpango1.0-dev
```

### Build Dependencies

```bash
# Initialize git submodules
git submodule update --init --recursive

# Install npm dependencies
npm install

# Or skip font generation
touch .fonts.stamp
```

## Running Specific Test Suites

### New Test Files (Just Added)

```bash
# Printer state management
./build/bin/run_tests "[printer_state]"

# Utility functions
./build/bin/run_tests "[ui_utils]"

# Temperature validation
./build/bin/run_tests "[temp_utils]"

# Bed mesh coordinate transforms
./build/bin/run_tests "[bed_mesh]"

# GCode camera control
./build/bin/run_tests "[gcode_camera]"

# Ethernet management
./build/bin/run_tests "[ethernet]"

# Theme color parsing
./build/bin/run_tests "[ui_theme]"
```

### Existing Test Files

```bash
# Configuration tests
./build/bin/run_tests "[config]"

# GCode parser
./build/bin/run_tests "[gcode]"

# Moonraker client
./build/bin/run_tests "[moonraker]"

# WiFi management
./build/bin/run_tests "[wifi]"

# Wizard tests
./build/bin/run_tests "[wizard]"
```

## Test Output Options

### Verbose Mode

```bash
# Show successful assertions
./build/bin/run_tests -s

# Maximum verbosity
./build/bin/run_tests -s -v high
```

### List Tests

```bash
# List all test cases
./build/bin/run_tests --list-tests

# List tests with specific tag
./build/bin/run_tests "[printer_state]" --list-tests
```

### Run Specific Test

```bash
# Run by test case name
./build/bin/run_tests "PrinterState: Update extruder temperature"

# Run by partial match
./build/bin/run_tests "temperature"
```

## Troubleshooting

### Build Failures

#### Missing LVGL

```bash
# Ensure submodules are initialized
git submodule update --init --recursive
```

#### Font Generation Errors

```bash
# Skip font generation
touch .fonts.stamp
make test
```

#### TinyGL Build Errors (Linux)

If you see errors about `-mmacosx-version-min`:

```bash
# Edit tinygl/src/Makefile and remove the macOS-specific flag
# Or skip TinyGL if not needed for tests
```

#### Missing libhv

```bash
# Clean and rebuild to ensure libhv is built
make clean
make -j
make test
```

### Test Failures

#### LVGL Initialization Errors

Some tests require LVGL to be properly initialized. The test framework handles this automatically via `setup_lvgl_for_testing()`.

#### Floating Point Comparison Failures

Tests use `Catch::Approx` for floating-point comparisons. If tests fail due to precision:

```cpp
// Increase epsilon if needed
REQUIRE(value == Approx(expected).epsilon(0.01));
```

## Test Categories

### Unit Tests

- Test individual functions in isolation
- Mock external dependencies
- Fast execution
- Located in `tests/unit/`

### Integration Tests

- Test multiple components together
- May use real dependencies
- Longer execution time
- Located in `tests/integration/`

## Writing New Tests

### Test File Template

```cpp
/*
 * Copyright (C) 2025 356C LLC
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "../catch_amalgamated.hpp"
#include "your_module.h"

using Catch::Approx;

TEST_CASE("Module: Feature description", "[module][feature]") {
    SECTION("Test scenario") {
        // Setup
        YourClass obj;

        // Execute
        auto result = obj.method();

        // Verify
        REQUIRE(result == expected_value);
    }
}
```

### Best Practices

1. **Use descriptive test names**: "Module: What it does" format
2. **Use tags**: `[module][feature]` for filtering
3. **Use SECTION**: Group related tests
4. **Test edge cases**: Boundary values, NULL, empty, overflow
5. **Use Approx**: For floating-point comparisons
6. **Document intent**: Add comments for complex test scenarios

### Adding Test to Build System

Tests in `tests/unit/test_*.cpp` are automatically discovered and compiled.

No Makefile changes needed for new test files!

## Continuous Integration

Tests run automatically on push via GitHub Actions:

- Build all tests
- Run unit tests
- Run integration tests
- Report failures

See `.github/workflows/test.yml` for CI configuration.

## Coverage Analysis

### Generate Coverage Report (Future)

```bash
# Build with coverage
make coverage

# View report
open coverage_html/index.html
```

### Manual Coverage Check

```bash
# List all source files
find src/ -name "*.cpp" | sort

# List all test files
find tests/unit/ -name "test_*.cpp" | sort

# Compare to identify gaps
```

## Performance Benchmarks

### Test Execution Time

```bash
# Time all tests
time make test

# Profile specific tests
./build/bin/run_tests "[printer_state]" --benchmark-samples 100
```

## Common Test Patterns

### Testing JSON Parsing

```cpp
json notification = {
    {"method", "notify_status_update"},
    {"params", {
        {{"extruder", {{"temperature", 210.0}}}},
        1234567890.0
    }}
};

state.update_from_notification(notification);

REQUIRE(lv_subject_get_int(state.get_extruder_temp_subject()) == 210);
```

### Testing Formatting Functions

```cpp
REQUIRE(format_print_time(90) == "1h30m");
REQUIRE(format_file_size(1024) == "1.0 KB");
REQUIRE(format_filament_weight(12.5f) == "12.5g");
```

### Testing Coordinate Transformations

```cpp
using Catch::Approx;

double x = mesh_col_to_world_x(0, 3, 10.0);
REQUIRE(x == Approx(-10.0));
```

### Testing Camera Math

```cpp
GCodeCamera camera;
camera.set_top_view();

REQUIRE(camera.get_azimuth() == Approx(0.0f));
REQUIRE(camera.get_elevation() == Approx(89.0f));
```

## Resources

- **Catch2 Documentation**: https://github.com/catchorg/Catch2
- **Testing Guide**: `/home/user/helixscreen/docs/TESTING.md`
- **Coverage Report**: `/home/user/helixscreen/TEST_COVERAGE_REPORT.md`

## Contact

For questions about tests, see:
- `docs/TESTING.md` - Complete testing guide
- `docs/CONTRIBUTING.md` - Code standards
- GitHub Issues - Report test failures
