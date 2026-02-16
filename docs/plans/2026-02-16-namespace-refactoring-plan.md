# Namespace Refactoring Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Move all HIGH and MEDIUM priority global-namespace declarations into `helix::` (or `helix::ui::`), modernize C-style enums to `enum class`, and consolidate the `using json =` alias.

**Architecture:** Big-bang migration — no backward-compat aliases. Headers define the namespace, .cpp files add `using namespace helix;` (and `using namespace helix::ui;` where needed) to minimize churn. Each task must compile and pass tests before proceeding.

**Tech Stack:** C++17, LVGL 9.4, Catch2 (tests), Make build system

**Design doc:** `docs/plans/2026-02-16-namespace-refactoring-design.md`

---

## Task 1: JSON Alias Consolidation

**Files:**
- Create: `include/json_fwd.h`
- Modify: `include/config.h`, `include/moonraker_client.h`, `include/moonraker_error.h`, `include/moonraker_types.h`, `include/moonraker_request.h`, `include/hardware_validator.h`, `include/filament_sensor_manager.h`, `include/tips_manager.h`, `include/spoolman_types.h`, `include/plugin_events.h`

**Step 1: Create `include/json_fwd.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hv/json.hpp"
using json = nlohmann::json;
```

**Step 2: Replace all `using json = nlohmann::json` in headers**

In each of the 10 headers listed above:
- Remove the local `#include "hv/json.hpp"` (if present) and `using json = nlohmann::json;`
- Add `#include "json_fwd.h"` (if not already including it transitively)

Note: `include/moonraker_manager.h` has `using json = nlohmann::json` inside `namespace helix` — leave that one alone or switch it to include json_fwd.h outside the namespace block.

**Step 3: Build and test**

```bash
make -j && make test-run
```

**Step 4: Commit**

```bash
git commit -m "refactor: consolidate json alias into json_fwd.h"
```

---

## Task 2: Enum Modernization — Small Enums

Convert the simpler typedef enums (2-6 files each) to `enum class` inside `namespace helix`.

**Files:**
- Modify: `include/ui_panel_home.h`, `include/ui_heater_config.h`, `include/ui_step_progress.h`, `include/ui_panel_motion.h`, `include/ui_nav_manager.h`, `include/ui_panel_print_status.h`
- Modify: All .cpp files that reference these enum values

**Step 1: Convert each enum**

| File | Old | New (inside `namespace helix`) |
|------|-----|------|
| `ui_panel_home.h` | `typedef enum { NETWORK_WIFI, NETWORK_ETHERNET, NETWORK_DISCONNECTED } network_type_t` | `enum class NetworkType { Wifi, Ethernet, Disconnected }` |
| `ui_heater_config.h` | `typedef enum { HEATER_NOZZLE, HEATER_BED } heater_type_t` | `enum class HeaterType { Nozzle, Bed }` |
| `ui_step_progress.h` | `typedef enum { UI_STEP_STATE_PENDING=0, UI_STEP_STATE_ACTIVE=1, UI_STEP_STATE_COMPLETED=2 } ui_step_state_t` | `enum class StepState { Pending = 0, Active = 1, Completed = 2 }` |
| `ui_nav_manager.h` | `typedef enum { UI_PANEL_HOME, ..., UI_PANEL_COUNT } ui_panel_id_t` | `enum class PanelId { Home = 0, PrintSelect, Controls, Filament, Settings, Advanced, Count }` |
| `ui_panel_motion.h` | `typedef enum { JOG_DIST_0_1MM=0, ... } jog_distance_t` | `enum class JogDistance { Dist0_1mm = 0, Dist1mm, Dist10mm, Dist100mm }` |
| `ui_panel_motion.h` | `typedef enum { JOG_DIR_N, ... } jog_direction_t` | `enum class JogDirection { N, S, E, W, NE, NW, SE, SW }` |
| `ui_panel_print_status.h` | `typedef enum { PRINT_STATE_IDLE, ... } print_state_t` | **DELETE** — this is a legacy wrapper around the existing `enum class PrintState`. Remove typedef and update any remaining users to use `PrintState` directly. |

**Step 2: Update all call sites**

For each enum, find-and-replace the old values with new qualified names. Example:
- `NETWORK_WIFI` → `helix::NetworkType::Wifi` (or just `NetworkType::Wifi` in files with `using namespace helix;`)
- `UI_PANEL_HOME` → `helix::PanelId::Home`
- `HEATER_NOZZLE` → `helix::HeaterType::Nozzle`

Add `using namespace helix;` at top of .cpp files that use these enums to minimize churn.

**Step 3: Build and test**

```bash
make -j && make test-run
```

**Step 4: Commit**

```bash
git commit -m "refactor: modernize UI typedef enums to enum class in helix::"
```

---

## Task 3: Enum Modernization — G-code Viewer & Bed Mesh

These are inside `extern "C"` blocks and used by the rendering subsystem.

**Files:**
- Modify: `include/ui_gcode_viewer.h`, `include/bed_mesh_renderer.h`
- Modify: All .cpp files that reference these enum values

**Step 1: Convert each enum**

| File | Old | New (inside `namespace helix`) |
|------|-----|------|
| `ui_gcode_viewer.h` | `gcode_viewer_state_enum_t` | `enum class GcodeViewerState { Empty, Loading, Loaded, Error }` |
| `ui_gcode_viewer.h` | `gcode_viewer_render_mode_t` | `enum class GcodeViewerRenderMode { Auto, Render3D, Layer2D }` |
| `ui_gcode_viewer.h` | `gcode_viewer_preset_view_t` | `enum class GcodeViewerPresetView { Isometric, Top, Front, Side }` |
| `bed_mesh_renderer.h` | `bed_mesh_render_mode_t` | `enum class BedMeshRenderMode { Auto, Force3D, Force2D }` |

Note: These files use `extern "C"` blocks. The enums need to be moved OUT of the extern "C" block and into `namespace helix`. The C-linkage functions that use these types will need their signatures updated.

**Step 2: Update all call sites**

Find-and-replace old values (e.g., `BED_MESH_RENDER_MODE_AUTO` → `helix::BedMeshRenderMode::Auto`).

**Step 3: Build and test**

```bash
make -j && make test-run
```

**Step 4: Commit**

```bash
git commit -m "refactor: modernize gcode viewer and bed mesh enums to enum class"
```

---

## Task 4: Manager Classes → `helix::`

Wrap 5 manager/service classes in `namespace helix`.

**Files:**
- Modify headers: `include/panel_factory.h`, `include/wifi_manager.h`, `include/sound_manager.h`, `include/tips_manager.h`, `include/mdns_discovery.h`
- Modify: All .cpp files that reference these classes (~40 files)

**Step 1: Wrap each class declaration in `namespace helix { }`**

For each header:
```cpp
// Before
class WiFiManager { ... };

// After
namespace helix {
class WiFiManager { ... };
} // namespace helix
```

Do the same for: `PanelFactory`, `SoundManager`, `TipsManager`, `IMdnsDiscovery`, `MdnsDiscovery`.

Also move any associated types/structs that are in the same headers into the namespace.

**Step 2: Update all .cpp files**

Add `using namespace helix;` to implementation files. Update any explicit references in headers.

**Step 3: Build and test**

```bash
make -j && make test-run
```

**Step 4: Commit**

```bash
git commit -m "refactor: move manager classes into helix:: namespace"
```

---

## Task 5: Callback Typedefs → `helix::`

Move ~39 global `using XCallback = std::function<...>` declarations into `namespace helix`.

**Files to modify (headers):**
- `include/macro_types.h` — `MacroListCallback`
- `include/spoolman_types.h` — 8 callbacks (`SpoolListCallback`, `SpoolCallback`, `FilamentUsageCallback`, `VendorListCallback`, `FilamentListCallback`, `SpoolCreateCallback`, `VendorCreateCallback`, `FilamentCreateCallback`)
- `include/print_history_manager.h` — `HistoryChangedCallback`
- `include/calibration_types.h` — `ScrewTiltCallback`, `InputShaperCallback`, `MachineLimitsCallback`
- `include/moonraker_client.h` — `SubscriptionId`, `RequestId`
- `include/ui_nav_manager.h` — `OverlayCloseCallback`
- `include/ui_print_select_usb_source.h` — `UsbFilesReadyCallback`, `SourceChangedCallback`
- `include/gcode_object_thumbnail_renderer.h` — `ThumbnailCompleteCallback`
- `include/ui_print_select_file_provider.h` — `FilesReadyCallback`, `MetadataUpdatedCallback`, `FileErrorCallback`
- `include/ui_print_select_card_view.h` — `FileClickCallback`, `MetadataFetchCallback`
- `include/ui_print_select_list_view.h` — `FileClickCallback`, `MetadataFetchCallback` (duplicates of card_view)
- `include/input_shaper_calibrator.h` — `AccelCheckCallback`, `ProgressCallback`, `ResultCallback`, `SuccessCallback`, `ErrorCallback`
- `include/theme_manager.h` — `StyleConfigureFn`
- `include/ui_macro_enhance_wizard.h` — `WizardCompleteCallback`
- `include/print_start_enhancer.h` — `EnhancementProgressCallback`, `EnhancementCompleteCallback`, `EnhancementErrorCallback`
- `include/moonraker_events.h` — `MoonrakerEventCallback`
- `include/ui_print_preparation_manager.h` — `NavigateToStatusCallback`, `PrintCompletionCallback`, `ScanCompleteCallback`, `MacroAnalysisCallback`
- `include/advanced_panel_types.h` — `AdvancedSuccessCallback`, `AdvancedErrorCallback`, `AdvancedProgressCallback`
- `include/ui_hsv_picker.h` — `HsvPickerCallback`
- `include/plugin_api.h` — `PluginInitFunc`, `PluginDeinitFunc`, `PluginApiVersionFunc`
- `include/print_start_analyzer.h` — `PrintStartOpCategory`
- `include/gcode_ops_detector.h` — `OperationType`

**Step 1: Wrap each typedef in `namespace helix { }`**

For headers that already have a `namespace helix { }` block, move the typedef inside it.
For headers with no namespace block, add one around the typedefs.

Handle duplicate names (`FileClickCallback`, `MetadataFetchCallback` in both card_view and list_view) — these are identical signatures so they can be consolidated into a single definition in a shared location, or moved into `helix::ui` in each header.

**Step 2: Update .cpp files**

Add `using namespace helix;` where needed. Most .cpp files that use these callbacks will get this from the singleton migration (Task 6), so this step may be minimal.

**Step 3: Build and test**

```bash
make -j && make test-run
```

**Step 4: Commit**

```bash
git commit -m "refactor: move callback typedefs into helix:: namespace"
```

---

## Task 6: UI Free Functions → `helix::ui::`

Move UI utility functions into `helix::ui::` and drop the `ui_` prefix.

**Files to modify (headers):**
- `include/ui_modal.h` — all `ui_modal_*` functions → `helix::ui::modal_*`
- `include/ui_notification_manager.h` — all `ui_status_bar_*` functions → `helix::ui::status_bar_*`, all `ui_notification_*` functions → `helix::ui::notification_*`
- `include/ui_update_queue.h` — `ui_queue_update` → `helix::ui::queue_update`, `ui_update_queue_*` → `helix::ui::update_queue_*`, `ui_async_call` → `helix::ui::async_call`
- `include/ui_event_safety.h` — `ui_event_safe_call` → `helix::ui::event_safe_call`
- `include/ui_utils.h` — `lv_obj_safe_delete` → `helix::ui::safe_delete`, `ui_toggle_list_empty_state` → `helix::ui::toggle_list_empty_state`

**Affected .cpp files:** ~120 files

**Step 1: Wrap all function declarations/definitions in `namespace helix::ui { }`**

For inline wrappers in headers, move them inside the namespace block and rename:
```cpp
// Before
inline void ui_queue_update(helix::ui::UpdateCallback callback) {
    helix::ui::UpdateQueue::instance().queue(std::move(callback));
}

// After (inside namespace helix::ui {})
inline void queue_update(UpdateCallback callback) {
    UpdateQueue::instance().queue(std::move(callback));
}
```

For non-inline functions (declared in headers, defined in .cpp files), update both declaration and definition.

**Step 2: Update .cpp call sites**

Add `using namespace helix::ui;` at the top of .cpp files that call these functions. This is the biggest batch of changes (~120 files).

Alternatively, many .cpp files will already have `using namespace helix;` from Task 6, so they'd only need the additional `using namespace helix::ui;`.

**Step 3: Update XML event callback registrations**

Check if any of these functions are registered as XML event callbacks via `lv_xml_register_event_cb()`. If so, the registration call needs updating but the callback signature stays the same.

**Step 4: Build and test**

```bash
make -j && make test-run
```

**Step 5: Commit**

```bash
git commit -m "refactor: move UI free functions into helix::ui:: namespace"
```

---

## Task 7: Core Singletons → `helix::`

The biggest change — wrap `PrinterState`, `MoonrakerClient`, `Config`, `SettingsManager` in `namespace helix`.

**Files to modify (headers):**
- `include/printer_state.h` — `PrinterState` class + associated enums (`NetworkStatus`, `PrinterStatus`, `KlippyState`, `PrintJobState`, `PrintOutcome`, `PrintStartPhase`, `ZOffsetCalibrationStrategy`) + free functions (`parse_print_job_state`, `print_job_state_to_string`)
- `include/moonraker_client.h` — `MoonrakerClient` class + `ConnectionState` enum
- `include/config.h` — `Config` class + `MacroConfig` struct + `CURRENT_CONFIG_VERSION`
- `include/settings_manager.h` — `SettingsManager` class + enums (`CompletionAlertMode`, `TimeFormat`, `ZMovementStyle`)

**Affected .cpp files:** ~400 files (with overlap — many files reference multiple singletons)

**Step 1: Wrap header declarations in `namespace helix { }`**

For `printer_state.h`, wrap everything from the enums through the class:
```cpp
namespace helix {

enum class NetworkStatus { ... };
enum class PrinterStatus { ... };
// ... all enums ...

PrintJobState parse_print_job_state(const char* state_str);
const char* print_job_state_to_string(PrintJobState state);

class PrinterState {
    // ... entire class ...
};

} // namespace helix
```

Same pattern for the other 3 headers.

**Step 2: Add `using namespace helix;` to ALL .cpp files that reference these types**

This is the bulk of the work. Every .cpp file that uses `PrinterState::instance()`, `Config::instance()`, `SettingsManager::instance()`, or `MoonrakerClient` needs `using namespace helix;` added (if not already present from earlier tasks).

**Step 3: Update header cross-references**

Some headers reference these types (e.g., a panel header might have `PrinterState&` as a member type). These headers need either:
- A forward declaration: `namespace helix { class PrinterState; }`
- Or a `using helix::PrinterState;` after the include

Prefer forward declarations in headers, `using namespace` in .cpp files.

**Step 4: Build and test**

```bash
make -j && make test-run
```

**Step 5: Commit**

```bash
git commit -m "refactor: move core singletons into helix:: namespace"
```

---

## Task 8: Final Cleanup & Verification

**Step 1: Search for any remaining global-namespace pollution**

```bash
# Look for classes/enums/typedefs still in global namespace in headers
grep -rn "^class " include/*.h | grep -v "namespace"
grep -rn "^typedef enum" include/*.h
grep -rn "^using.*Callback" include/*.h | grep -v "namespace"
```

**Step 2: Run full test suite**

```bash
make clean && make -j && make test-run
```

**Step 3: Verify no regressions with mock printer**

```bash
./build/bin/helix-screen --test -vv
```

Run through the UI briefly to verify no obvious breaks.

**Step 4: Commit any cleanup**

```bash
git commit -m "refactor: namespace refactoring cleanup"
```
