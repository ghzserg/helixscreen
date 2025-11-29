# Pre-Print Options - Session Handoff

**Last Updated:** 2025-11-29
**Status:** Phase 1 Complete - Preparing UI Wired to CommandSequencer

---

## What's Been Completed

### Core Infrastructure (DONE)
- **GCodeOpsDetector** (`src/gcode_ops_detector.cpp`) - Scans G-code files and detects operations like `BED_MESH_CALIBRATE`, `QUAD_GANTRY_LEVEL`, `CLEAN_NOZZLE`, etc.
- **PrinterCapabilities** (`src/printer_capabilities.cpp`) - Detects printer features from Moonraker's `printer.objects.list` (QGL, Z-tilt, bed mesh, macros)
- **CommandSequencer** (`src/command_sequencer.cpp`) - Coordinates multi-step print preparation with state-based completion detection
- **MacroManager** (`src/helix_macro_manager.cpp`) - Manages HelixScreen helper macros (HELIX_START_PRINT, etc.)

### UI Integration (DONE)
- **Pre-print checkboxes** in file detail view (`ui_xml/print_file_detail.xml`)
  - Bed Leveling, QGL, Z Tilt, Nozzle Clean checkboxes
  - Visibility controlled by `printer_has_*` subjects
  - Wired to `start_print()` flow in `ui_panel_print_select.cpp`
- **Filament type dropdown** auto-populated from G-code metadata

### "Preparing" Phase UI (DONE - Phase 1)
- **PrintStatusPanel "Preparing" state** with visual feedback:
  - Added `preparing_overlay` to `ui_xml/print_status_panel.xml` with:
    - Spinner (LVGL `lv_spinner`)
    - Operation label bound to `preparing_operation` subject
    - Progress bar bound to `preparing_progress` subject
    - Visibility controlled by `preparing_visible` subject (hidden when == 0)
  - CommandSequencer progress callbacks wired to `set_preparing()`, `set_preparing_progress()`, `end_preparing()`
  - Print status panel shows immediately when "Print" clicked, shows Preparing overlay during pre-print ops
  - Transitions to normal print view when preparation completes
- **Removed mock print infrastructure** from `PrintStatusPanel`:
  - Removed `start_mock_print()`, `stop_mock_print()`, `tick_mock_print()`
  - Panel now purely reactive via Moonraker → PrinterState observers
  - Separation of concerns: UI code has no knowledge of mock infrastructure

### Test Coverage (DONE)
- `test_command_sequencer.cpp` - 10 test cases, 108 assertions
- `test_printer_capabilities.cpp` - Hardware and macro detection
- `test_gcode_ops_detector.cpp` - Operation detection + robustness edge cases
- `test_helix_macro_manager.cpp` - Macro content and installation
- Validation tests fixed with RFC 1035 compliance

---

## What's Remaining (Priority Order)

### 1. G-code File Modification (Stage 3 from plan)
**Files:** `src/gcode_file_modifier.cpp` (NEW)

The detector finds operations, but we need to **comment them out** when user disables an option that's already in the G-code:

```cpp
class GCodeFileModifier {
public:
    // Comment out lines between start_line and end_line
    std::string disable_operation(const std::string& content,
                                   size_t start_line, size_t end_line);

    // Inject G-code at specific position
    std::string inject_gcode(const std::string& content,
                             size_t after_line,
                             const std::string& gcode_to_inject);

    // Create modified temp file for printing
    std::string create_modified_file(const std::string& original_path,
                                      const std::vector<Modification>& mods);
};
```

**Key consideration:** The current approach uses G-code injection (sending commands before `start_print()`), NOT file modification. File modification is the **secondary approach** for cases where injection isn't sufficient.

### 2. HTTP File Upload (Stage 4 from plan)
**Files:** `src/moonraker_api.cpp` (MODIFY)

MacroManager needs HTTP file upload to install `helix_macros.cfg`:
- POST to `/server/files/upload`
- Multipart form data with `file` and `root=config`
- Uses libhv HttpClient (already in project)

Currently stubbed with `TODO` comment in `helix_macro_manager.cpp:293-307`.

### 3. Print Status "Preparing" Phase (Stage 6 from plan) ✅ DONE
**Files:** `src/ui_panel_print_status.cpp`, `ui_xml/print_status_panel.xml`

Completed:
- Added `preparing_overlay` with spinner, operation label, and progress bar
- CommandSequencer callbacks wired to `set_preparing()`, `set_preparing_progress()`, `end_preparing()`
- Transitions from Preparing → Printing (success) or Idle (failure)

### 4. Wizard Connection/Summary Screens (Phase 11)
**Files:** `ui_xml/wizard_*.xml`, `src/ui_wizard_*.cpp`

Connection test and summary screens exist but aren't fully wired.

---

## Key Files to Read

| File | Purpose |
|------|---------|
| `docs/PRINT_OPTIONS_IMPLEMENTATION_PLAN.md` | Full architecture and stages |
| `docs/TESTING_PRE_PRINT_OPTIONS.md` | Manual test plan and checklist |
| `src/ui_panel_print_select.cpp:954-993` | Current `start_print()` flow |
| `src/command_sequencer.cpp` | How operations are sequenced |
| `src/printer_capabilities.cpp` | How printer features are detected |
| `include/gcode_ops_detector.h` | Operation detection API |

---

## Architecture Summary

```
User clicks "Print" with options checked
         │
         ▼
┌─────────────────────────────────────┐
│  PrintSelectPanel::start_print()   │
│  1. Collect checkbox states        │
│  2. Build operation sequence       │
│  3. Execute via CommandSequencer   │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│      CommandSequencer               │
│  For each operation:                │
│  1. Send G-code via MoonrakerAPI   │
│  2. Poll printer state             │
│  3. Wait for completion condition  │
│  4. Move to next operation         │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  MoonrakerAPI::start_print()       │
│  Actual print begins               │
└─────────────────────────────────────┘
```

---

## Recent Commits (for context)

```
83902ab fix(cli): require --test mode for --gcode-file option
e66c1fa fix(validation): improve wizard IP/hostname/port validation
ae92452 feat(gcode-viewer): add ghost layer visualization
836bcd6 feat(macros): add HelixMacroManager for helper macro installation
907e8b5 docs(testing): add comprehensive pre-print options test plan
2c6a22d feat(pre-print): integrate CommandSequencer with start_print()
8c76c4f feat(pre-print): add Bambu-style pre-print options to file detail
```

---

## Known Issues

1. **Geometry builder SIGSEGV** - `test_gcode_geometry_builder.cpp:163` - RibbonGeometry move semantics bug (unrelated to pre-print)
2. **Wizard UI XML loading** - `wizard_container` returns nullptr in test environment (infrastructure issue)

---

## Next Session Prompt

```
Continue implementing the Bambu-style pre-print options feature for HelixScreen.

COMPLETED:
- GCodeOpsDetector, PrinterCapabilities, CommandSequencer, MacroManager
- UI checkboxes in file detail view, wired to start_print() flow
- PrintStatusPanel "Preparing" phase with spinner, operation label, progress bar
- CommandSequencer progress callbacks wired to Preparing UI
- Mock print infrastructure removed (panel is now purely reactive)
- Comprehensive test coverage

NEXT STEPS (pick one):
A) Implement GCodeFileModifier to comment out detected ops when user disables them
B) Implement HTTP file upload in MoonrakerAPI for macro installation
C) Wire up wizard connection test + summary screens

Read these files first:
- docs/PRINT_OPTIONS_IMPLEMENTATION_PLAN.md (full architecture)
- docs/TESTING_PRE_PRINT_OPTIONS.md (test checklist)
- src/ui_panel_print_select.cpp (current print flow, see start_print())
- src/ui_panel_print_status.cpp (set_preparing(), end_preparing())
- ui_xml/print_status_panel.xml (preparing_overlay with spinner)

The project uses LVGL 9.4 with XML-based UI, Moonraker WebSocket API, and Catch2 for testing.
Build with: make -j
Run with: ./build/bin/helix-ui-proto --test -p print-select
```

---

## Build & Test Commands

```bash
# Build
make -j

# Run with mock printer
./build/bin/helix-ui-proto --test -p print-select

# Auto-select file and show detail view
./build/bin/helix-ui-proto --test -p print-select --select-file "3DBenchy.gcode"

# Run pre-print related tests
./build/bin/run_tests "[sequencer]" "[capabilities]" "[gcode_ops]" "[helix_macros]"

# Verbose logging
./build/bin/helix-ui-proto --test -vv 2>&1 | grep -E "CommandSequencer|start_print"
```
