# Input Shaper Panel Implementation Handoff

**Date:** 2025-12-11
**Priority:** HIGH (TIER 2)
**Complexity:** HIGH

---

## Overview

Implement an Input Shaper calibration panel for HelixScreen. This allows users to run resonance calibration directly from the touchscreen, eliminating the need for SSH or web interface access.

---

## Current State

- **XML Stub exists:** `ui_xml/input_shaper_panel.xml` with "Coming Soon" overlay
- **No C++ implementation** yet
- **Access point:** Advanced Panel row (like Screws Tilt, Console, etc.)

---

## Feature Requirements

### Core Functionality
1. **Run SHAPER_CALIBRATE** - Execute resonance test for X or Y axis
2. **Run MEASURE_AXES_NOISE** - Check accelerometer noise levels
3. **Progress indication** - Show status during calibration (can take 1-2 minutes)
4. **Display results** - Show recommended shaper type and frequency
5. **Apply settings** - Option to save recommended settings

### UI States (Similar to Screws Tilt pattern)
1. **IDLE** - Instructions and buttons to start calibration
2. **MEASURING** - Progress spinner, cancel button
3. **RESULTS** - Display recommendations, Apply/Dismiss buttons
4. **ERROR** - Error message with retry option

---

## Klipper Commands

```gcode
# Measure noise (sanity check before calibration)
MEASURE_AXES_NOISE

# Run calibration for specific axis
SHAPER_CALIBRATE AXIS=X
SHAPER_CALIBRATE AXIS=Y

# Apply recommended settings (after user confirmation)
SET_INPUT_SHAPER SHAPER_FREQ_X=<freq> SHAPER_TYPE_X=<type>
SET_INPUT_SHAPER SHAPER_FREQ_Y=<freq> SHAPER_TYPE_Y=<type>

# Save to config (requires SAVE_CONFIG)
SAVE_CONFIG
```

### Response Parsing

The `SHAPER_CALIBRATE` command outputs results to the G-code console. Example:
```
Fitted shaper 'zv' frequency = 35.8 Hz (vibrations = 22.7%, smoothing ~= 0.100)
Fitted shaper 'mzv' frequency = 36.7 Hz (vibrations = 7.2%, smoothing ~= 0.140)
Fitted shaper 'ei' frequency = 43.2 Hz (vibrations = 6.8%, smoothing ~= 0.172)
Fitted shaper '2hump_ei' frequency = 52.4 Hz (vibrations = 5.0%, smoothing ~= 0.214)
Fitted shaper '3hump_ei' frequency = 62.0 Hz (vibrations = 4.2%, smoothing ~= 0.255)
Recommended shaper is mzv @ 36.7 Hz
```

Key parsing:
- Look for "Recommended shaper is <type> @ <freq> Hz"
- Parse vibration reduction percentage
- Parse smoothing value

---

## Moonraker Integration

### Option 1: G-code Script (Simplest)
Use existing `MoonrakerAPI::execute_gcode()` to run commands:
```cpp
api_->execute_gcode("SHAPER_CALIBRATE AXIS=X", on_success, on_error);
```

### Option 2: Subscribe to G-code Responses
To capture output, subscribe to `notify_gcode_response` notifications:
```cpp
// In MoonrakerClient
void subscribe_gcode_response(std::function<void(const std::string&)> callback);
```

### Option 3: Check printer.configfile
After calibration, recommended values may be accessible via:
```
printer.configfile.settings.input_shaper
```

---

## Files to Create

```
ui_xml/input_shaper_panel.xml     - Replace stub with full UI
include/ui_panel_input_shaper.h   - Panel class header
src/ui_panel_input_shaper.cpp     - Implementation
```

## Files to Modify

```
src/xml_registration.cpp          - Register component
src/main.cpp                      - Add -p input-shaper CLI option
ui_xml/advanced_panel.xml         - Ensure row click handler wired
```

---

## UI Design Recommendations

### IDLE State
```
┌─────────────────────────────────────┐
│  Input Shaper Calibration           │
├─────────────────────────────────────┤
│                                     │
│  ⚡ Reduce ringing and ghosting     │
│     in your prints                  │
│                                     │
│  Requirements:                      │
│  • ADXL345 accelerometer attached   │
│  • [resonance_tester] configured    │
│                                     │
│  ┌───────────────┐ ┌───────────────┐│
│  │ Calibrate X   │ │ Calibrate Y   ││
│  └───────────────┘ └───────────────┘│
│                                     │
│  ┌─────────────────────────────────┐│
│  │       Measure Noise Level       ││
│  └─────────────────────────────────┘│
└─────────────────────────────────────┘
```

### MEASURING State
```
┌─────────────────────────────────────┐
│  Calibrating X Axis...              │
├─────────────────────────────────────┤
│                                     │
│           [Spinner]                 │
│                                     │
│  Running resonance test...          │
│  This may take 1-2 minutes          │
│                                     │
│        ┌──────────────┐             │
│        │    Cancel    │             │
│        └──────────────┘             │
└─────────────────────────────────────┘
```

### RESULTS State
```
┌─────────────────────────────────────┐
│  X Axis Results                     │
├─────────────────────────────────────┤
│                                     │
│  ✓ Recommended: MZV @ 36.7 Hz       │
│                                     │
│  ┌─────────────────────────────────┐│
│  │ Type      Freq    Vibration     ││
│  │ zv        35.8    22.7%         ││
│  │ mzv ★     36.7    7.2%          ││
│  │ ei        43.2    6.8%          ││
│  │ 2hump_ei  52.4    5.0%          ││
│  │ 3hump_ei  62.0    4.2%          ││
│  └─────────────────────────────────┘│
│                                     │
│  ┌──────────┐  ┌──────────────────┐ │
│  │  Close   │  │  Apply & Save    │ │
│  └──────────┘  └──────────────────┘ │
└─────────────────────────────────────┘
```

---

## Implementation Notes

### Pattern to Follow
Follow the `ScrewsTiltPanel` pattern:
- State machine with UI views for each state
- XML event callbacks registered via `lv_xml_register_event_cb()`
- Lazy panel creation on first open
- Results stored in class member for display

### Capability Detection
Check if input_shaper is available:
```cpp
// In PrinterCapabilities
bool has_input_shaper() const { return capabilities_.count("resonance_tester") > 0; }
```

If not available, show a different message explaining how to set it up.

### Progress Tracking
Since calibration takes time:
1. Show spinner immediately after starting
2. Consider polling `printer.idle_timeout.state` to detect completion
3. Or subscribe to G-code responses and parse for "Recommended shaper"

### Save Config Warning
After applying settings, warn user:
- "Settings applied for this session"
- "Use SAVE_CONFIG to make permanent (printer will restart)"

---

## Testing

```bash
# Run with mock printer
./build/bin/helix-screen --test -p input-shaper -vv

# Run with real printer (needs ADXL345)
./build/bin/helix-screen -p input-shaper -vv
```

### Mock Responses
Add to `MoonrakerClientMock` or `MoonrakerAPIMock`:
```cpp
// Simulate SHAPER_CALIBRATE response
void simulate_shaper_result(const std::string& axis,
                           const std::string& shaper_type,
                           float frequency);
```

---

## References

- Klipper Input Shaper docs: https://www.klipper3d.org/Resonance_Compensation.html
- Measuring Resonances: https://www.klipper3d.org/Measuring_Resonances.html
- Existing panel pattern: `src/ui_panel_screws_tilt.cpp`
- Console panel for response parsing: `src/ui_panel_console.cpp`

---

## Success Criteria

- [ ] Panel opens from Advanced panel row
- [ ] Can run MEASURE_AXES_NOISE and see result
- [ ] Can run SHAPER_CALIBRATE for X and Y axes
- [ ] Results displayed with recommended shaper highlighted
- [ ] Can apply recommended settings
- [ ] Error handling for missing accelerometer
- [ ] Cancel button works during calibration
- [ ] Works with `--test` mock mode
