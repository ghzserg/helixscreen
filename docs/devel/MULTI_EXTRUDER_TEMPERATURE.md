# Multi-Extruder Temperature System

Dynamic per-extruder temperature tracking with reactive LVGL subjects. Supports arbitrary numbers of extruders discovered at runtime from Klipper heater objects.

**Related docs:** [TOOL_ABSTRACTION.md](TOOL_ABSTRACTION.md) (tool-to-extruder mapping), [FILAMENT_MANAGEMENT.md](FILAMENT_MANAGEMENT.md) (filament systems)

---

## Overview

`PrinterTemperatureState` manages all temperature-related LVGL subjects. It was extracted from `PrinterState` as part of the god class decomposition. The class supports:

- **Dynamic multi-extruder discovery** from Klipper heater objects
- **Per-extruder subjects** for independent temperature/target binding
- **Legacy compatibility** with single-extruder XML bindings
- **Centidegree precision** (0.1C resolution via integer subjects)
- **Bed and chamber** temperature tracking

```
  Moonraker status JSON
          │
          ▼
  PrinterState::set_*_internal()
          │ delegates to
          ▼
  PrinterTemperatureState::update_from_status()
          │
          ├──> extruders_["extruder"].temp_subject     (per-extruder, heap)
          ├──> extruders_["extruder1"].temp_subject    (per-extruder, heap)
          ├──> extruder_temp_  (legacy static, mirrors "extruder")
          ├──> bed_temp_       (static)
          └──> chamber_temp_   (static)
                  │
                  ▼
              XML bindings / observer callbacks
```

---

## ExtruderInfo Struct

Each discovered extruder gets an `ExtruderInfo` entry in the `extruders_` map:

```cpp
struct ExtruderInfo {
    std::string name;         // Klipper name: "extruder", "extruder1", etc.
    std::string display_name; // Human-readable: "Nozzle", "Nozzle 1"
    float temperature = 0.0f; // Raw float for internal tracking
    float target = 0.0f;
    std::unique_ptr<lv_subject_t> temp_subject;   // Centidegrees (value * 10)
    std::unique_ptr<lv_subject_t> target_subject; // Centidegrees
};
```

### Why Heap-Allocated Subjects

Subjects are stored as `unique_ptr<lv_subject_t>` rather than inline members. This is required because `ExtruderInfo` lives in an `unordered_map`, and map rehash operations move entries. LVGL subjects must have stable addresses because observers hold raw pointers to them. Heap allocation via `unique_ptr` ensures pointer stability across rehash.

### Display Names

Single extruder printers get `display_name = "Nozzle"`. Multi-extruder printers get `"Nozzle 1"`, `"Nozzle 2"`, etc. (1-indexed for user display).

---

## Discovery Flow

Extruders are discovered during printer connection via `init_extruders()`:

```
MoonrakerClient connects
        │
        ▼
PrinterDiscovery::parse_objects()
  Finds: "extruder", "extruder1", "heater_bed", ...
        │
        ▼
PrinterTemperatureState::init_extruders(heaters)
  1. Filter heater list for "extruder" and "extruderN" names
     (rejects "extruder_stepper" and other prefixed names)
  2. Clean up previous ExtruderInfo entries (deinit old subjects)
  3. Create ExtruderInfo for each extruder with heap-allocated subjects
  4. Bump extruder_version subject to trigger UI rebuilds
```

The filtering logic accepts:
- `"extruder"` -- the default/first extruder
- `"extruder1"`, `"extruder2"`, etc. -- additional extruders (digit immediately after "extruder")

It rejects:
- `"extruder_stepper"` -- not a heater
- Any other `"extruder_*"` prefixed names

`init_extruders()` is safe to call multiple times. It cleans up previous subjects before creating new ones.

---

## Subject Management

### Static Subjects (Legacy)

These are always available and registered with the LVGL XML system at startup:

| Subject Name | Type | Description |
|--------------|------|-------------|
| `extruder_temp` | int | First extruder current temp (centidegrees) |
| `extruder_target` | int | First extruder target temp (centidegrees) |
| `bed_temp` | int | Bed current temp (centidegrees) |
| `bed_target` | int | Bed target temp (centidegrees) |
| `chamber_temp` | int | Chamber current temp (centidegrees) |
| `extruder_version` | int | Bumped when extruder list changes |

### Dynamic Per-Extruder Subjects

Created by `init_extruders()`. Accessed via name-based lookup:

```cpp
auto& pts = printer_state.temperature();

// Get per-extruder subjects by Klipper name
lv_subject_t* temp = pts.get_extruder_temp_subject("extruder1");
lv_subject_t* target = pts.get_extruder_target_subject("extruder1");

// Returns nullptr if extruder not found
if (temp) {
    int centidegrees = lv_subject_get_int(temp);
    float degrees = centidegrees / 10.0f;
}
```

### Version Subject

`extruder_version` is an integer counter that increments whenever the extruder list changes (via `init_extruders()`). UI panels observe this subject to rebuild their temperature displays:

```cpp
// Observer triggers UI rebuild when extruder list changes
add_observer(observe_int_async<TempPanel>(
    pts.get_extruder_version_subject(),
    this,
    [](TempPanel* self, int32_t version) {
        self->rebuild_extruder_list();
    }
));
```

---

## Legacy Compatibility

The static `extruder_temp` and `extruder_target` subjects always mirror the first extruder (`"extruder"` key in Moonraker status). This preserves backward compatibility with existing XML bindings:

```xml
<!-- These XML bindings continue to work unchanged -->
<text_body bind_text="extruder_temp"/>
<lv_arc bind_value="extruder_target"/>
```

In `update_from_status()`, the `"extruder"` key updates both:
1. The dynamic `extruders_["extruder"]` subjects
2. The legacy static `extruder_temp_` / `extruder_target_` subjects

---

## Temperature Precision

All temperature subjects store **centidegrees** (value multiplied by 10):

| Actual Temp | Subject Value | Calculation |
|-------------|---------------|-------------|
| 205.3 C | 2053 | `205.3 * 10` |
| 60.0 C | 600 | `60.0 * 10` |
| 0.0 C | 0 | `0.0 * 10` |

This provides 0.1C resolution using integer subjects (LVGL subjects don't support float). Conversion uses `helix::units::json_to_centidegrees()` from `unit_conversions.h`.

**Display formatting:** UI code divides by 10 for display: `"{}.{}C", value/10, value%10`.

**Force-notify pattern:** Temperature subjects call `lv_subject_notify()` after `lv_subject_set_int()` even when the value hasn't changed. This ensures chart/graph widgets receive updates for time-series rendering.

---

## Status Update Flow

`update_from_status()` processes the Moonraker status JSON:

```
JSON status arrives (background thread)
        │
        ▼
PrinterState::set_*_internal()
  Posts to LVGL thread via ui_async_call
        │
        ▼
PrinterTemperatureState::update_from_status(status)
        │
        ├── For each extruder in extruders_ map:
        │     Check status[extruder_name] for "temperature" and "target"
        │     Convert to centidegrees, set per-extruder subjects
        │
        ├── Legacy: status["extruder"] -> extruder_temp_, extruder_target_
        │
        ├── status["heater_bed"] -> bed_temp_, bed_target_
        │
        └── status[chamber_sensor_name_] -> chamber_temp_
```

### Chamber Sensor

The chamber temperature sensor name is configurable via `set_chamber_sensor_name()`. It's set during discovery when `PrinterDiscovery` detects a `temperature_sensor` with "chamber" in the name. The status JSON key for chamber varies per printer (e.g., `"temperature_sensor chamber"`, `"temperature_sensor Chamber_Temp"`).

---

## PrinterState Delegation

`PrinterTemperatureState` is a member of `PrinterState`, accessed via:

```cpp
auto& ps = PrinterState::instance();

// Legacy accessors (delegate to PrinterTemperatureState)
lv_subject_t* temp = ps.get_extruder_temp_subject();      // Static, first extruder
lv_subject_t* bed = ps.get_bed_temp_subject();

// Per-extruder access
lv_subject_t* t1 = ps.temperature().get_extruder_temp_subject("extruder1");

// Extruder enumeration
const auto& extruders = ps.temperature().extruders();
for (const auto& [name, info] : extruders) {
    spdlog::info("{}: {:.1f}C", info.display_name, info.temperature);
}
```

---

## UI Integration

### HomePanel Temperature Display

Shows the first extruder and bed temperatures using legacy static subjects. On multi-extruder printers, observes `extruder_version` to optionally show a multi-tool temperature summary.

### TempControlPanel Extruder Selector

Provides a dropdown populated from the `extruders()` map. Each entry shows the `display_name`. Selecting an extruder binds the temperature arc and target controls to that extruder's per-extruder subjects. Hidden on single-extruder printers.

### PrintStatusPanel Per-Tool Temps

During a print, shows each tool's temperature with its tool name prefix. Uses `ToolState` to map tools to extruder names, then looks up per-extruder subjects for binding.

```
  ToolState::tools()           PrinterTemperatureState::extruders()
  ┌──────────────┐             ┌─────────────────────────────┐
  │T0 -> extruder│────────────>│extruder: temp_subject=2050  │
  │T1 -> extruder1│───────────>│extruder1: temp_subject=2103 │
  └──────────────┘             └─────────────────────────────┘
         │                                    │
         ▼                                    ▼
  Display: "T0: 205.0/210   T1: 210.3/215"
```

---

## Key Files

| File | Purpose |
|------|---------|
| `include/printer_temperature_state.h` | ExtruderInfo struct, PrinterTemperatureState class |
| `src/printer/printer_temperature_state.cpp` | Discovery, status parsing, subject management |
| `include/unit_conversions.h` | `json_to_centidegrees()` conversion helper |
| `include/printer_discovery.h` | Heater discovery: `heaters()` list |
| `include/tool_state.h` | Tool-to-extruder name mapping |

---

## See Also

- **[TOOL_ABSTRACTION.md](TOOL_ABSTRACTION.md)** -- Tool state, tool-to-extruder mapping
- **[FILAMENT_MANAGEMENT.md](FILAMENT_MANAGEMENT.md)** -- AMS backend architecture
- **[ARCHITECTURE.md](ARCHITECTURE.md)** -- Domain decomposition of PrinterState
- **[MOONRAKER_ARCHITECTURE.md](MOONRAKER_ARCHITECTURE.md)** -- Status subscriptions, PrinterDiscovery
