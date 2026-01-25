# Input Shaping Panel/Wizard Implementation Plan

**Status**: ✅ Complete - All 7 Phases Done
**Created**: 2026-01-13
**Last Updated**: 2026-01-16

---

## Progress Tracking

### Overall Progress: Phase 7 of 7 Complete ✅

| Phase | Status | Session | Notes |
|-------|--------|---------|-------|
| **Phase 1**: Core Types & API | ✅ Complete | 2 | ShaperOption, InputShaperConfig, NoiseCheckCollector, all_shapers |
| **Phase 2**: InputShaperCalibrator | ✅ Complete | 3 | State machine, API integration, 35 tests |
| **Phase 3**: Platform Detection | ✅ Complete | 4 | PlatformCapabilities, meminfo/cpuinfo parsing, 33 tests |
| **Phase 4**: UI Panel Rewrite | ✅ Complete | 5 | Panel delegates to calibrator, 78 tests total |
| **Phase 5**: Frequency Chart | ✅ Complete | 6 | FrequencyResponseChart widget, downsampling, 193 assertions |
| **Phase 6**: First-Run Wizard | ✅ Complete | 7 | WizardInputShaperStep, thread-safe callbacks, 17 tests (82 assertions) |
| **Phase 7**: Cache & Test Print | ✅ Complete | 8 | InputShaperCache, TUNING_TOWER button, 56 tests (1286 assertions) |

**Legend**: ⬜ Not Started | 🟡 In Progress | ✅ Complete | ❌ Blocked

---

## Overview

Transform the existing basic `InputShaperPanel` into a comprehensive input shaping configuration experience with:
- Visual frequency response graphs
- Integrated accelerometer health check + troubleshooting
- Guided step-by-step wizard flow
- Shaper type comparison with tradeoff explanations
- Optional test print integration
- Current settings display when idle

**Entry Point**: Settings overlay (like Machine Limits)
**Hardware**: Graceful degradation based on RAM/CPU metrics

---

## Architecture: Clean Design (Free to Refactor)

Since input shaping code isn't in production use yet, we can design it properly.

**Key Insight**: The multi-step calibration workflow (ADXL check → X test → Y test → Review → Apply) needs to be reusable across multiple UI contexts. A **coordinator class** encapsulates this logic.

```
┌─────────────────────────────────────────────────────────────┐
│                      UI LAYER                               │
├──────────────────┬──────────────────┬───────────────────────┤
│ InputShaperPanel │ WizardInputShaper│ (Future: CLI/Scripts) │
│   (Settings)     │     Step         │                       │
└────────┬─────────┴────────┬─────────┴───────────┬───────────┘
         │                  │                     │
         └──────────────────┼─────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              InputShaperCalibrator (NEW)                    │
│  Coordinates multi-step workflow, caches results            │
│  - check_accelerometer()                                    │
│  - run_calibration(axis, on_progress, on_complete)          │
│  - get_results() → CalibrationResults                       │
│  - apply_settings(x_config, y_config)                       │
│  - save_to_config()                                         │
│  - get_cached_results() → persistent storage                │
└────────┬────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────┐
│                    MoonrakerAPI                             │
│  measure_axes_noise(), start_resonance_test(),              │
│  set_input_shaper(), save_config(),                         │
│  get_input_shaper_config() (NEW)                            │
└─────────────────────────────────────────────────────────────┘
```

**Why InputShaperCalibrator?**
- Similar pattern to `MacroModificationManager` (coordinates multi-step workflow)
- Eliminates duplication between Panel and Wizard step
- Owns result caching (session + persistent)
- Single place to handle ADXL troubleshooting logic
- Testable independently of UI

---

## Development Methodology

Following best practices throughout:

1. **Test-First** (`/test-first`): Write characterization/unit tests BEFORE implementation
2. **Delegate** (`/delegate`): Agents handle actual implementation work
3. **Review** (`/review`): Review each significant chunk before proceeding
4. **Final Review**: Comprehensive review of all work at the end
5. **Clean Context**: Main session for coordination and critical thinking only
6. **Fresh Sessions**: Start each major phase in a fresh session to avoid compaction

### Work Chunks (Delegatable Units)

| Chunk | Description | Tests First |
|-------|-------------|-------------|
| **C1** | InputShaperCalibrator core class | Unit tests for state machine, callbacks |
| **C2** | MoonrakerAPI new methods | Tests for measure_axes_noise, get_config |
| **C3** | Enhanced InputShaperCollector | Tests for all_shapers parsing |
| **C4** | PlatformCapabilities | Tests for tier detection |
| **C5** | InputShaperPanel rewrite | Integration tests with mock calibrator |
| **C6** | Frequency chart widget | Visual tests (screenshot comparison) |
| **C7** | WizardInputShaperStep | Tests for skip logic, flow |
| **C8** | Persistent cache | Tests for save/load |

---

## Existing Infrastructure (Reuse)

| Component | Location | Status |
|-----------|----------|--------|
| `PrinterDiscovery::has_accelerometer()` | `printer_discovery.h:169-174` | ✅ Exists |
| `MoonrakerAPI::start_resonance_test()` | `moonraker_api.h:1003` | ✅ Exists |
| `MoonrakerAPI::set_input_shaper()` | `moonraker_api.h:1015` | ✅ Exists |
| `MoonrakerAPI::save_config()` | `moonraker_api.h` | ✅ Exists |
| `InputShaperCollector` | `moonraker_api_advanced.cpp:448-633` | ⚠️ Enhance |
| `InputShaperResult` | `calibration_types.h:149-166` | ⚠️ Enhance |
| `InputShaperPanel` | `ui_panel_input_shaper.h/cpp` | ⚠️ Major rework |

---

## What Needs to be Added/Modified

### 1. New MoonrakerAPI Methods

```cpp
// moonraker_api.h - Add to advanced operations section

/// Callback with noise level (0-1000+, <100 is good)
using NoiseCheckCallback = std::function<void(float noise_level)>;

/// Check accelerometer noise level (MEASURE_AXES_NOISE)
virtual void measure_axes_noise(NoiseCheckCallback on_complete, ErrorCallback on_error);

/// Get current input shaper configuration from printer
virtual void get_input_shaper_config(
    std::function<void(const InputShaperConfig&)> on_success,
    ErrorCallback on_error);
```

### 2. New Data Types

```cpp
// calibration_types.h - Add

/// Single shaper option with all metrics
struct ShaperOption {
    std::string type;           // "zv", "mzv", "ei", etc.
    float frequency = 0.0f;     // Hz
    float vibrations = 0.0f;    // % remaining
    float smoothing = 0.0f;     // Lower is sharper
    float max_accel = 0.0f;     // Recommended max acceleration
};

/// Enhanced result with ALL shaper options
struct InputShaperResult {
    char axis = 'X';
    std::string recommended_type;
    float recommended_freq = 0.0f;
    std::vector<ShaperOption> all_shapers;  // NEW - all fitted options
    std::vector<std::pair<float, float>> freq_response;  // For graphing
    bool is_valid() const;
};

/// Current input shaper configuration (from printer state)
struct InputShaperConfig {
    std::string shaper_type_x;
    float shaper_freq_x = 0.0f;
    std::string shaper_type_y;
    float shaper_freq_y = 0.0f;
    float damping_ratio_x = 0.0f;
    float damping_ratio_y = 0.0f;
    bool is_configured = false;  // false if no shaper active
};
```

### 3. New Collector for Noise Check

```cpp
// In moonraker_api_advanced.cpp - Add NoiseCheckCollector

class NoiseCheckCollector : public std::enable_shared_from_this<NoiseCheckCollector> {
    // Parses MEASURE_AXES_NOISE output:
    // "axes_noise = 0.012345" or error if no accelerometer
};
```

### 4. Platform Capabilities (New)

```cpp
// include/platform_capabilities.h

enum class PlatformTier {
    EMBEDDED,    // <512MB RAM or single core - table view only, no charts
    BASIC,       // 512MB-2GB RAM or slower multi-core - simplified charts
    STANDARD     // 2GB+ RAM and faster multi-core - full charts with animations
};

struct PlatformCapabilities {
    // Detected hardware metrics
    size_t available_ram_mb;      // From /proc/meminfo
    int cpu_cores;                // From /proc/cpuinfo
    int cpu_mhz;                  // Estimated from BogoMIPS or similar

    // Derived capabilities
    PlatformTier tier;
    bool supports_charts;
    bool supports_animations;
    size_t max_chart_points;      // 200 for STANDARD, 50 for BASIC, 0 for EMBEDDED

    static PlatformCapabilities detect();

    // Tier thresholds (configurable for testing)
    static constexpr size_t EMBEDDED_RAM_THRESHOLD_MB = 512;
    static constexpr size_t STANDARD_RAM_THRESHOLD_MB = 2048;
    static constexpr int STANDARD_CPU_CORES_MIN = 4;
};
```

**Detection Logic:**
1. Read `/proc/meminfo` for `MemTotal` → available RAM
2. Count CPU cores from `/proc/cpuinfo`
3. Extract CPU speed from BogoMIPS or `cpu MHz` fields
4. Apply tier classification:
   - **EMBEDDED**: RAM < 512MB OR single core
   - **BASIC**: RAM 512MB-2GB OR 2-3 cores at low speed
   - **STANDARD**: RAM >= 2GB AND 4+ cores

This approach works on any Linux system without hardcoding device names.

---

## Wizard Flow Design

### States (Enhanced from current 4-state machine)

```cpp
enum class InputShaperWizardState {
    // Status/Idle
    IDLE,              // Show current settings, Start button

    // Pre-flight
    CHECKING_ADXL,     // Running MEASURE_AXES_NOISE
    ADXL_ERROR,        // ADXL not responding - show troubleshooting
    ADXL_WARNING,      // High noise - show warning, allow proceed

    // X-Axis Calibration
    TESTING_X,         // SHAPER_CALIBRATE AXIS=X running
    RESULTS_X,         // Show X results, Continue button

    // Y-Axis Calibration
    TESTING_Y,         // SHAPER_CALIBRATE AXIS=Y running
    RESULTS_Y,         // Show Y results, Continue button

    // Review & Apply
    REVIEW,            // Compare X+Y results, shaper selection
    APPLYING,          // SET_INPUT_SHAPER command

    // Completion
    COMPLETE,          // Success - offer SAVE_CONFIG and test print
    ERROR              // Generic error state
};
```

### Step Progression

```
START → CHECKING_ADXL
           │
           ├─ ADXL OK ────────────────────┐
           ├─ ADXL High Noise → WARNING ──┼──► TESTING_X
           └─ ADXL Failed → ERROR ────────┘        │
                   │                               │
                   ▼                               ▼
           [Troubleshooting]              RESULTS_X (graph + table)
                                                   │
                                                   ▼
                                             TESTING_Y
                                                   │
                                                   ▼
                                          RESULTS_Y (graph + table)
                                                   │
                                                   ▼
                                              REVIEW
                                     (side-by-side comparison)
                                     (shaper type selection)
                                                   │
                                                   ▼
                                             APPLYING
                                                   │
                                                   ▼
                                             COMPLETE
                                       [Save Config] [Test Print]
```

---

## UI Components

### 1. Frequency Response Chart (Hardware-Adaptive)

```cpp
// include/ui_frequency_chart.h
class FrequencyResponseChart {
public:
    void set_data(const std::vector<FrequencyPoint>& data);
    void set_recommended_frequency(float freq_hz);
    void add_shaper_overlay(const std::string& type, const std::vector<float>& response);
    void clear();

    // Hardware adaptation
    void configure_for_platform(PlatformTier tier);

private:
    lv_obj_t* chart_;
    PlatformTier tier_ = PlatformTier::STANDARD;

    // Simplify data for weaker hardware
    std::vector<FrequencyPoint> downsample(
        const std::vector<FrequencyPoint>& data,
        size_t max_points
    );
};
```

**Hardware Adaptation:**
- STANDARD: Full resolution (200+ points), animations, multiple overlays
- BASIC: Reduced resolution (50 points), no animation, single overlay
- EMBEDDED: Table view only, no chart widget

### 2. Shaper Comparison Table

```cpp
// Always available regardless of hardware tier
struct ShaperComparisonRow {
    std::string type;           // "mzv", "ei", etc.
    float frequency;
    float vibrations_pct;       // Lower is better
    float smoothing;            // Lower is sharper
    float max_accel;            // mm/s²
    std::string tradeoff_desc;  // Plain English
    bool is_recommended;
};
```

**Tradeoff Descriptions:**
- `zv`: "Fastest, minimal smoothing, may leave some ringing"
- `mzv`: "Balanced - good ringing reduction with moderate smoothing"
- `zvd`: "Better ringing reduction, more smoothing"
- `ei`: "Strong ringing reduction, noticeable smoothing"
- `2hump_ei`: "Very strong filtering, significant smoothing"
- `3hump_ei`: "Maximum filtering, most smoothing, lowest max accel"

### 3. ADXL Troubleshooting View

When ADXL check fails, show:
1. Error message (what failed)
2. Likely causes checklist:
   - [ ] ADXL wired correctly (SPI pins)
   - [ ] MCU configured for ADXL (`[adxl345]` section)
   - [ ] USB connection stable (for USB accelerometers)
   - [ ] Firmware flashed with ADXL support
3. Link to documentation
4. "Check Again" button

---

## XML Structure

```
ui_xml/
├── input_shaper_panel.xml            # Main container
├── input_shaper_idle.xml             # IDLE state - current settings
├── input_shaper_preflight.xml        # ADXL check screen
├── input_shaper_testing.xml          # Progress during calibration
├── input_shaper_results.xml          # Single axis results (graph + table)
├── input_shaper_review.xml           # Both axes comparison
├── input_shaper_complete.xml         # Success + next steps
└── components/
    ├── frequency_chart.xml           # Chart component (or placeholder)
    └── shaper_comparison_row.xml     # Table row component
```

---

## Implementation Phases

### Phase 1: Core Types & API (Chunks C2, C3)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Tests written for `measure_axes_noise()`
- [x] Tests written for `get_input_shaper_config()`
- [x] Tests written for enhanced `InputShaperCollector` (all shapers)
- [x] `ShaperOption` struct added to `calibration_types.h`
- [x] `InputShaperConfig` struct added to `calibration_types.h`
- [x] `InputShaperResult` enhanced with `all_shapers` vector
- [x] `measure_axes_noise()` method added to `MoonrakerAPI`
- [x] `get_input_shaper_config()` method added to `MoonrakerAPI`
- [x] `NoiseCheckCollector` implemented
- [x] `InputShaperCollector` enhanced to return all shapers
- [x] Mock implementations added
- [x] All tests pass (15 test cases, 75 assertions)
- [x] Code reviewed (no blocking issues)

**Files to modify:**
- `include/calibration_types.h`
- `include/moonraker_api.h`
- `src/api/moonraker_api_advanced.cpp`
- `src/api/moonraker_api_mock.cpp`
- `tests/unit/test_moonraker_api_input_shaper.cpp`

---

### Phase 2: InputShaperCalibrator (Chunk C1)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Tests written for calibrator state machine
- [x] Tests written for check_accelerometer flow
- [x] Tests written for run_calibration flow
- [x] Tests written for apply_settings
- [x] Tests written for error handling
- [x] `InputShaperCalibrator` header created
- [x] State machine implemented
- [x] Accelerometer check flow implemented
- [x] Calibration run flow implemented
- [x] Apply settings implemented
- [x] Error handling implemented
- [x] All tests pass (35 test cases, 65 assertions)
- [x] Code reviewed

**Files to create:**
- `include/input_shaper_calibrator.h`
- `src/calibration/input_shaper_calibrator.cpp`
- `tests/unit/test_input_shaper_calibrator.cpp`

**Core Design:**
```cpp
class InputShaperCalibrator {
    enum class State { IDLE, CHECKING_ADXL, TESTING_X, TESTING_Y, READY };

    void check_accelerometer(AccelCheckCallback on_complete);
    void run_calibration(char axis, ProgressCallback, ResultCallback, ErrorCallback);
    void cancel();

    const CalibrationResults& get_results() const;
    void apply_settings(const ApplyConfig&, SuccessCallback, ErrorCallback);
    void save_to_config(SuccessCallback, ErrorCallback);

    // Caching
    void load_cached_results();
    void save_results_to_cache();
};
```

---

### Phase 3: Platform Detection (Chunk C4)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Tests written for RAM detection
- [x] Tests written for CPU core count detection
- [x] Tests written for tier classification
- [x] `PlatformCapabilities` header created
- [x] `/proc/meminfo` parsing implemented
- [x] `/proc/cpuinfo` parsing implemented
- [x] Tier classification logic implemented
- [x] All tests pass (33 tests, 67 assertions)
- [x] Code reviewed

**Files created:**
- `include/platform_capabilities.h`
- `src/system/platform_capabilities.cpp`
- `tests/unit/test_platform_capabilities.cpp`

---

### Phase 4: UI Panel Rewrite (Chunk C5)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Integration tests written with mock calibrator
- [x] `InputShaperPanel` header updated
- [x] State machine simplified (delegates to calibrator)
- [x] XML files created for all states (kept existing unified XML approach)
- [x] IDLE state UI working
- [x] MEASURING state UI working (with calibrator delegation)
- [x] RESULTS state UI working
- [x] ERROR state UI working
- [x] Mock mode testing passes
- [x] Code reviewed - fixed async callback safety [L012]

**Files modified:**
- `include/ui_panel_input_shaper.h` - Added calibrator member, getter for testing
- `src/ui/ui_panel_input_shaper.cpp` - Delegates to calibrator instead of direct API calls

**Files created:**
- `tests/unit/test_input_shaper_panel_integration.cpp` - Mock calibrator and contract tests

**Design decision:** Kept existing unified `input_shaper_panel.xml` with subject-bound visibility
instead of creating separate XML files per state. This is simpler and already works well.

---

### Phase 5: Frequency Chart (Chunk C6)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Chart widget header created
- [x] LVGL chart implementation done
- [x] Downsampling for BASIC tier implemented
- [x] Table fallback for EMBEDDED tier implemented
- [x] XML container added to input_shaper_panel.xml
- [x] Tests pass (193 assertions in 23 test cases)
- [x] Code reviewed (9/10 quality rating)

**Files created:**
- `include/ui_frequency_response_chart.h`
- `src/ui/ui_frequency_response_chart.cpp`
- `tests/unit/test_frequency_response_chart.cpp`

**Files modified:**
- `ui_xml/input_shaper_panel.xml` - Added chart_container element

---

### Phase 6: First-Run Wizard Integration (Chunk C7)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Tests written for skip logic
- [x] Tests written for wizard flow
- [x] `WizardInputShaperStep` header created
- [x] Step implementation using calibrator
- [x] XML template created
- [x] Integration with `ui_wizard.cpp`
- [x] Skip logic works when no accelerometer
- [x] Full flow works with accelerometer
- [x] All tests pass (309 assertions in 63 test cases)
- [x] Code reviewed

**Files created:**
- `include/ui_wizard_input_shaper.h`
- `src/ui/ui_wizard_input_shaper.cpp`
- `ui_xml/wizard_input_shaper.xml`
- `tests/unit/test_wizard_input_shaper_step.cpp`

**Files modified:**
- `src/ui/ui_wizard.cpp` - Added step 9 (input shaper), renumbered summary to step 10
- `src/xml_registration.cpp` - Registered wizard_input_shaper.xml component

**Design decisions:**
- Wizard step is skippable when no accelerometer detected
- Uses existing InputShaperCalibrator for calibration workflow
- User can "Skip for now" to configure later in Settings
- Step validated when calibration complete OR user explicitly skips

---

### Phase 7: Persistent Cache & Test Print (Chunk C8)

**Status**: ✅ Complete

#### Checkpoints:
- [x] Tests written for cache save/load
- [x] Cache functionality added (InputShaperCache class)
- [x] "Print Test Pattern" button working (uses TUNING_TOWER)
- [x] Cache persists across sessions
- [x] All tests pass (56 cache tests, 93 total input shaper tests)
- [x] Code reviewed

**Files created:**
- `include/input_shaper_cache.h`
- `src/calibration/input_shaper_cache.cpp`
- `tests/unit/test_input_shaper_cache.cpp`

**Cache location:** `~/.cache/helix/input_shaper_cache.json` (XDG-compliant)

**Design decisions:**
- Uses TUNING_TOWER command instead of embedded gcode (no file upload needed)
- 30-day TTL with printer ID validation
- Follows ThumbnailCache pattern for directory resolution

---

## Files Summary

### Files to Modify

| File | Changes |
|------|---------|
| `include/calibration_types.h` | Add `ShaperOption`, `InputShaperConfig`, expand `InputShaperResult` |
| `include/moonraker_api.h` | Add `measure_axes_noise()`, `get_input_shaper_config()` |
| `src/api/moonraker_api_advanced.cpp` | Add `NoiseCheckCollector`, enhance `InputShaperCollector` |
| `src/api/moonraker_api_mock.cpp` | Mock implementations for new methods |
| `include/ui_panel_input_shaper.h` | Simplify state machine, delegate to calibrator |
| `src/ui/ui_panel_input_shaper.cpp` | Major rewrite using InputShaperCalibrator |
| `src/ui/ui_wizard.cpp` | Add optional input shaper step |

### Files to Create

| File | Purpose |
|------|---------|
| `include/input_shaper_calibrator.h` | **Core coordinator class** |
| `src/calibration/input_shaper_calibrator.cpp` | Calibrator implementation + cache |
| `include/platform_capabilities.h` | Hardware tier detection |
| `src/system/platform_capabilities.cpp` | cpuinfo/meminfo parsing |
| `include/ui_frequency_chart.h` | LVGL chart wrapper with downsampling |
| `src/ui/ui_frequency_chart.cpp` | Chart implementation |
| `include/ui_wizard_input_shaper.h` | First-run wizard step |
| `src/ui/ui_wizard_input_shaper.cpp` | Step implementation (uses calibrator) |
| `ui_xml/input_shaper_*.xml` | 6 state-specific XML files |
| `ui_xml/wizard_input_shaper.xml` | First-run wizard step XML |
| `ui_xml/components/frequency_chart.xml` | Chart component |
| `ui_xml/components/shaper_comparison_row.xml` | Comparison row component |
| `assets/test_gcodes/ringing_test.gcode` | Embedded test model |

### Test Files to Create

| File | Purpose |
|------|---------|
| `tests/unit/test_input_shaper_calibrator.cpp` | Calibrator state machine, callbacks |
| `tests/unit/test_platform_capabilities.cpp` | Tier detection |
| `tests/unit/test_wizard_input_shaper_step.cpp` | Skip logic, flow |

---

## Verification Plan

1. **Unit tests** for API methods:
   - `test_moonraker_api_input_shaper.cpp` - noise check, config query, enhanced results
   - Test mock responses match real Klipper output format

2. **Mock mode testing**: `./build/bin/helix-screen --test -vv`
   - Navigate to Settings → Input Shaping
   - Verify IDLE state shows "Not configured" or current settings
   - Start calibration wizard → verify ADXL check passes (mock)
   - Verify X-axis test completes with mock data
   - Verify Y-axis test completes
   - Verify Review screen shows all shaper options
   - Apply settings → verify SET_INPUT_SHAPER called
   - Test "Save to Config" flow

3. **Platform detection testing**:
   - Test on Pi 4 → should show charts
   - Test on Pi 3/B+ → should show simplified charts or table
   - Test on embedded → should show table only

4. **Real hardware testing** (Pi 4 + ADXL345):
   - Run full wizard flow
   - Verify MEASURE_AXES_NOISE returns valid noise level
   - Verify SHAPER_CALIBRATE completes for both axes
   - Verify frequency graph renders (if data available)
   - Verify Apply + Save Config works

5. **First-run wizard testing**:
   - Verify step skipped when `has_accelerometer() == false`
   - Verify step shown when accelerometer detected
   - Verify "Skip" works

---

## Design Decisions

1. **Frequency data caching**: Persistent cache - save results to disk, show last results on next open
   - Store in `~/.config/helix-screen/input_shaper_results.json`
   - Include timestamp, allow "Re-run test" to refresh
   - Helps users compare results over time

2. **Test print model**: Embed in assets (~50KB gcode)
   - Bundle small ringing test model in `assets/test_gcodes/ringing_test.gcode`
   - No network dependency
   - Include quick guide image in assets

3. **First-run wizard**: Optional step if `resonance_tester` capability detected
   - Show "Calibrate Input Shaping" step after hardware discovery
   - Can be skipped - not mandatory
   - Uses same calibrator (reusable)

4. **Platform detection**: Based on hardware metrics (RAM, CPU cores), not device signatures
   - Works on any Linux system
   - Thresholds configurable for testing

---

## First-Run Wizard Integration

Add optional step after LED/filament sensor selection:

```
Step 7: LED Select (if LEDs detected)
Step 8: Filament Sensor (if 2+ sensors)
Step 9: Input Shaping (NEW - if resonance_tester detected)  ← Skip-able
Step 10: Summary
```

**WizardInputShaperStep** (new file):
- Checks `has_accelerometer()` capability from `PrinterDiscovery`
- `should_skip()` returns true if no accelerometer
- Shows simplified flow: health check → calibrate both axes → apply
- Reuses `InputShaperCalibrator` for all operations

---

## Session Handoff Template

**To start a new session, point Claude at this document and say "GO".**

Claude will:
1. Read this document to understand context
2. Find the first phase with status ⬜ or 🟡
3. Update status to 🟡 In Progress
4. Follow the methodology below
5. Work through checkpoints
6. Update status to ✅ when complete
7. Log session in Session Notes

### Methodology (ALWAYS follow)

```
1. /test-first   - Write failing tests BEFORE any implementation
2. /delegate     - Agents do the coding, main session coordinates
3. /review       - Review each significant chunk before proceeding
4. Final review  - Comprehensive review when phase complete
5. Clean context - Main session for thinking, agents for doing
```

### Per-Phase Workflow

```
START PHASE:
  1. Update phase status: ⬜ → 🟡
  2. Read phase checkpoints carefully
  3. Identify first unchecked item

FOR EACH CHECKPOINT:
  1. If test-related: Use /test-first to write tests
  2. If implementation: /delegate to agent with clear scope
  3. Wait for agent completion
  4. /review the result
  5. Mark checkpoint complete: [ ] → [x]
  6. Commit if appropriate

END PHASE:
  1. Run `make test-run` to verify all tests pass
  2. Final /review of all phase work
  3. Update phase status: 🟡 → ✅
  4. Add entry to Session Notes
  5. Commit with message: "feat(input-shaper): complete phase N - description"
```

### Quick Reference - What Each Phase Delivers

| Phase | Key Deliverable | Entry Point |
|-------|-----------------|-------------|
| 1 | API methods + enhanced collector | `moonraker_api.h` |
| 2 | InputShaperCalibrator class | `input_shaper_calibrator.h` |
| 3 | PlatformCapabilities detection | `platform_capabilities.h` |
| 4 | UI Panel with wizard flow | `ui_panel_input_shaper.cpp` |
| 5 | Frequency chart widget | `ui_frequency_chart.cpp` |
| 6 | First-run wizard step | `ui_wizard_input_shaper.cpp` |
| 7 | Persistent cache + test print | calibrator cache methods |

---

## Session Notes

_Log each session here for continuity_

### Session 1 (Planning)
- Date: 2026-01-13
- Completed: Initial plan creation, tracking document
- Next: Phase 1 - Core Types & API

### Session 2 (Phase 1 Implementation)
- Date: 2026-01-13
- Branch: `feature/input-shaping` in worktree `helixscreen-input-shaping`
- Commit: `9df8822f` - feat(input-shaper): complete Phase 1 - Core Types & API
- Completed:
  - Created worktree from origin/main
  - Wrote tests first (10 new test cases)
  - Added `ShaperOption`, `InputShaperConfig` structs
  - Added `all_shapers` vector to `InputShaperResult`
  - Implemented `measure_axes_noise()` with `NoiseCheckCollector`
  - Implemented `get_input_shaper_config()`
  - Enhanced `InputShaperCollector` to populate all_shapers
  - Mock support: `set_accelerometer_available()`, `set_input_shaper_configured()`
  - All 15 input shaper tests pass (75 assertions)
  - Code reviewed - no blocking issues
- Next: Phase 2 - InputShaperCalibrator coordinator class

### Session 3 (Phase 2 Implementation)
- Date: 2026-01-13
- Branch: `feature/input-shaping` in worktree `helixscreen-input-shaping`
- Commit: `8f1de83e` - feat(input-shaper): complete Phase 2 - InputShaperCalibrator
- Completed:
  - Created `InputShaperCalibrator` class with state machine (IDLE, CHECKING_ADXL, TESTING_X, TESTING_Y, READY)
  - Test-first: wrote 35 test cases before implementation
  - Implemented `check_accelerometer()` using `api_->measure_axes_noise()`
  - Implemented `run_calibration()` using `api_->start_resonance_test()`
  - Implemented `apply_settings()` using `api_->set_input_shaper()`
  - Implemented `save_to_config()` using `api_->save_config()`
  - Input validation (axis, frequency, shaper_type)
  - Concurrent run protection (guard against double calibration)
  - All 35 calibrator tests pass (65 assertions)
  - Code reviewed - clean implementation
- Files created:
  - `include/input_shaper_calibrator.h`
  - `src/calibration/input_shaper_calibrator.cpp`
  - `tests/unit/test_input_shaper_calibrator.cpp`
- Next: Phase 3 - Platform Detection

### Session 4 (Phase 3 Implementation)
- Date: 2026-01-13
- Branch: `feature/input-shaping` in worktree `helixscreen-input-shaping`
- Completed:
  - Created `PlatformCapabilities` class for hardware tier detection
  - Test-first: wrote 33 test cases before implementation
  - Implemented `/proc/meminfo` parsing for RAM detection
  - Implemented `/proc/cpuinfo` parsing for CPU cores and BogoMIPS
  - Tier classification: EMBEDDED (<512MB or single core), BASIC, STANDARD (>=2GB and 4+ cores)
  - Derived capabilities: supports_charts, supports_animations, max_chart_points
  - Integration test marked [!mayfail] for non-Linux systems
  - Code reviewed - added edge case tests and clarifying comments
  - All 33 tests pass (67 assertions)
- Files created:
  - `include/platform_capabilities.h`
  - `src/system/platform_capabilities.cpp`
  - `tests/unit/test_platform_capabilities.cpp`
- Next: Phase 4 - UI Panel Rewrite

### Session 5 (Phase 4 Implementation)
- Date: 2026-01-13
- Branch: `feature/input-shaping` in worktree `helixscreen-input-shaping`
- Completed:
  - Refactored `InputShaperPanel` to delegate to `InputShaperCalibrator`
  - Test-first: wrote integration tests with mock calibrator (28 new tests)
  - Added `calibrator_` unique_ptr member to panel
  - Added `get_calibrator()` accessor for testing
  - Delegated: start_calibration -> calibrator_->run_calibration()
  - Delegated: measure_noise -> calibrator_->check_accelerometer()
  - Delegated: apply_recommendation -> calibrator_->apply_settings()
  - Delegated: save_configuration -> calibrator_->save_to_config()
  - Delegated: cancel_calibration -> calibrator_->cancel()
  - Code reviewed: fixed missing `alive_` checks in async callbacks [L012]
  - All 78 input shaper tests pass (152 assertions)
- Files modified:
  - `include/ui_panel_input_shaper.h`
  - `src/ui/ui_panel_input_shaper.cpp`
- Files created:
  - `tests/unit/test_input_shaper_panel_integration.cpp`
- Design decision: Kept existing unified XML approach instead of separate files per state
- Next: Phase 5 - Frequency Chart

### Session 6 (Phase 5 Implementation)
- Date: 2026-01-13
- Branch: `feature/input-shaping` in worktree `helixscreen-input-shaping`
- Completed:
  - Created `FrequencyResponseChart` widget following `ui_temp_graph` pattern
  - Test-first: wrote 23 test cases (193 assertions) before implementation
  - C-style procedural API with opaque struct
  - Series management (add, remove, show/hide, up to 8 series)
  - Data downsampling for BASIC tier (50 points) and STANDARD tier (200 points)
  - Table mode fallback for EMBEDDED tier (is_chart_mode = false)
  - Peak frequency marking per series
  - Frequency and amplitude range configuration
  - Platform tier switching (creates/destroys LVGL chart widget dynamically)
  - Code reviewed: 9/10 quality rating, fixed division-by-zero edge case
  - All tests pass
- Files created:
  - `include/ui_frequency_response_chart.h`
  - `src/ui/ui_frequency_response_chart.cpp`
  - `tests/unit/test_frequency_response_chart.cpp`
- Files modified:
  - `ui_xml/input_shaper_panel.xml` - Added chart_container element
- Next: Phase 6 - First-Run Wizard

### Session 8 (Phase 7 Implementation)
- Date: 2026-01-16
- Branch: `feature/input-shaping-phase7` in worktree `helixscreen-input-shaping-phase7`
- Commit: `d2a9aeb6` - feat(input-shaper): complete Phase 7 - Cache and Test Print
- Completed:
  - Created `InputShaperCache` class for persistent calibration results
  - Test-first: wrote 56 test cases (1286 assertions) for cache functionality
  - JSON serialization for InputShaperResult, ShaperOption, CalibrationResults
  - XDG-compliant cache directory resolution (following ThumbnailCache pattern)
  - 30-day TTL with printer ID validation
  - Added "Test Print" button using TUNING_TOWER command (no embedded gcode)
  - Async-safe callbacks with alive_ guard [L012]
  - Toast notifications for user feedback
  - Code reviewed: no issues found
  - All tests pass (93 input shaper tests, 444 total)
- Files created:
  - `include/input_shaper_cache.h`
  - `src/calibration/input_shaper_cache.cpp`
  - `tests/unit/test_input_shaper_cache.cpp`
- Files modified:
  - `include/ui_panel_input_shaper.h` - Added handle_print_test_pattern_clicked()
  - `src/ui/ui_panel_input_shaper.cpp` - Implementation and callback registration
  - `ui_xml/input_shaper_panel.xml` - Added Test Print button
  - `tests/unit/test_input_shaper_panel_integration.cpp` - Added test case
- **Input Shaping Feature Complete!**
