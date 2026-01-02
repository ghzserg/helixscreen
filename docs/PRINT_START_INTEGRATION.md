# Print Start Phase Detection

HelixScreen detects when your print's preparation phase (heating, homing, leveling, etc.) is complete and actual printing begins. This document explains how the detection works and how to optimize it for your setup.

## How It Works

When a print starts, HelixScreen shows a "Preparing Print" overlay on the home panel with a progress bar. Once the preparation is complete, the UI transitions to show normal print status.

HelixScreen uses a multi-signal detection system with the following priority:

| Priority | Signal | How It Works |
|----------|--------|--------------|
| 1 | **Macro Variables** | Watches `gcode_macro _HELIX_STATE.active` or `gcode_macro _START_PRINT.print_started` |
| 2 | **G-code Console** | Parses console output for `HELIX:READY`, `LAYER: 1`, or `SET_PRINT_STATS_INFO CURRENT_LAYER=` |
| 3 | **Layer Count** | Monitors `print_stats.info.current_layer` becoming ≥1 |
| 4 | **Progress + Temps** | Print progress ≥2% AND temps within 5°C of target |
| 5 | **Timeout** | 45 seconds in PRINTING state with temps ≥90% of target |

## Making Your Setup HelixScreen-Friendly

### Option 1: Install HelixScreen Macros (Recommended)

The HelixScreen macro file provides:
- **Instant detection**: `HELIX_READY` signal at the end of your PRINT_START
- **Phase tracking**: Optional `HELIX_PHASE_*` macros for detailed progress display
- **Pre-print helpers**: `HELIX_BED_LEVEL_IF_NEEDED`, `HELIX_CLEAN_NOZZLE`, `HELIX_START_PRINT`

**Installation via Settings UI:**

1. Go to Settings → Advanced → HelixScreen Macros
2. Click "Install Macros"
3. Restart Klipper when prompted

**Manual Installation:**

1. Copy `config/helix_macros.cfg` to your Klipper config directory
2. Add to your `printer.cfg`:

```ini
[include helix_macros.cfg]
```

3. Add `HELIX_READY` at the end of your PRINT_START macro:

```gcode
[gcode_macro PRINT_START]
gcode:
    # Your heating commands
    M190 S{BED_TEMP}
    M109 S{EXTRUDER_TEMP}

    # Your homing and leveling
    G28
    BED_MESH_CALIBRATE

    # Signal HelixScreen that prep is done
    HELIX_READY

    # Purge line or first layer starts here
```

### Option 1b: With Phase Tracking

For detailed progress display during preparation, add phase signals:

```gcode
[gcode_macro PRINT_START]
gcode:
    HELIX_PHASE_HOMING
    G28

    HELIX_PHASE_HEATING_BED
    M190 S{BED_TEMP}

    HELIX_PHASE_BED_MESH
    BED_MESH_CALIBRATE

    HELIX_PHASE_HEATING_NOZZLE
    M109 S{EXTRUDER_TEMP}

    HELIX_PHASE_PURGING
    # Your purge line code here

    # Signal preparation complete
    HELIX_READY
```

### Option 2: Add Variables to Existing Macro

If you have an existing `_START_PRINT` or `START_PRINT` macro, add state tracking:

```gcode
[gcode_macro _START_PRINT]
variable_print_started: False
gcode:
    # ... your heating, homing, leveling code ...

    # At the end, signal completion:
    SET_GCODE_VARIABLE MACRO=_START_PRINT VARIABLE=print_started VALUE=True
```

### Option 3: Use SET_PRINT_STATS_INFO (Slicer-Based)

If your slicer supports it, enable layer info in your G-code:

**PrusaSlicer/SuperSlicer:**
- Enable "Verbose G-code" in Printer Settings → General → Advanced
- The slicer will insert `SET_PRINT_STATS_INFO CURRENT_LAYER=N` commands

**Cura:**
- Use a post-processing script to add layer comments
- HelixScreen looks for `LAYER:N` or `LAYER: N` patterns

## Available Macros

When `helix_macros.cfg` is installed, these macros are available:

### Core Signals

| Macro | Purpose |
|-------|---------|
| `HELIX_READY` | Signal that print preparation is complete |
| `HELIX_ENDED` | Signal that print has ended (called in PRINT_END) |
| `HELIX_RESET` | Reset state (for cancel/error recovery) |

### Phase Tracking (Optional)

| Macro | Phase Displayed |
|-------|-----------------|
| `HELIX_PHASE_HOMING` | "Homing..." |
| `HELIX_PHASE_HEATING_BED` | "Heating bed..." |
| `HELIX_PHASE_HEATING_NOZZLE` | "Heating nozzle..." |
| `HELIX_PHASE_BED_MESH` | "Bed leveling..." |
| `HELIX_PHASE_QGL` | "Quad gantry level..." |
| `HELIX_PHASE_Z_TILT` | "Z-tilt adjust..." |
| `HELIX_PHASE_CLEANING` | "Cleaning nozzle..." |
| `HELIX_PHASE_PURGING` | "Purging..." |

### Pre-Print Helpers

| Macro | Purpose |
|-------|---------|
| `HELIX_BED_LEVEL_IF_NEEDED` | Run bed mesh only if stale (configurable max age) |
| `HELIX_CLEAN_NOZZLE` | Nozzle cleaning sequence (configure brush position) |
| `HELIX_START_PRINT` | Complete start print sequence with all options |

## Troubleshooting

### "Preparing" Stuck Forever

If the home panel stays on "Preparing Print" indefinitely:

1. **Check console output**: Run your print and look at the Klipper console. Do you see any layer markers?
2. **Verify macro variables**: Query `gcode_macro _HELIX_STATE` via Moonraker to see if `active` is being set
3. **Enable fallbacks**: The timeout fallback should trigger after 45 seconds if temps are near target

### Quick Detection Not Working

If detection takes the full 45-second timeout:

1. **Add HELIX_READY**: The most reliable option
2. **Check if your slicer sets layer info**: Look for `SET_PRINT_STATS_INFO` in your G-code
3. **Verify macro object subscriptions**: HelixScreen subscribes to `gcode_macro _HELIX_STATE`, `gcode_macro _START_PRINT`, and `gcode_macro START_PRINT`

## Technical Details

### Detection Signals

**Macro Variables (Priority 1)**

HelixScreen subscribes to these Moonraker objects:
- `gcode_macro _HELIX_STATE` → watches `active` variable
- `gcode_macro _START_PRINT` → watches `print_started` variable
- `gcode_macro START_PRINT` → watches `preparation_done` variable

When any of these become `True`, detection is instant.

**G-code Console (Priority 2)**

HelixScreen watches `notify_gcode_response` for patterns:
```regex
SET_PRINT_STATS_INFO\s+CURRENT_LAYER=|LAYER:?\s*1\b|;LAYER:1|First layer|HELIX:READY
```

**Layer Count (Priority 3)**

Monitors `print_stats.info.current_layer` from Moonraker. When ≥1, prep is considered complete.

**Progress + Temps (Priority 4)**

Combines file progress (from `virtual_sdcard.progress`) with temperature stability:
- Progress ≥2% (past typical preamble/macros)
- Extruder within 5°C of target
- Bed within 5°C of target (if target >0)

**Timeout (Priority 5)**

Last resort after 45 seconds in PRINTING state:
- Extruder ≥90% of target
- Bed ≥90% of target (if target >0)

### Files

| File | Purpose |
|------|---------|
| `src/print/print_start_collector.cpp` | Detection logic and fallback implementation |
| `config/helix_macros.cfg` | Klipper macros for detection and phase tracking |
| `src/printer/macro_manager.cpp` | Macro installation management |
| `src/moonraker_client.cpp` | Object subscription setup |
