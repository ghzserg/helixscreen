# LVGL 9 XML UI Prototype - Development Roadmap

**Last Updated:** 2025-11-28

---

## 🎯 Current Priorities

1. **Phase 13:** Complete G-code ops detection and modification
2. **Phase 11:** Finish wizard connection/summary screens
3. **Phase 8:** Implement thumbnail HTTP downloads
4. **Phase 8:** File upload functionality
5. **Phase 12:** Production readiness

**Completed Phases:** 1, 2, 3, 4, 14
**Mostly Complete:** 5, 7, 8
**In Progress:** 6, 9, 10, 11, 13

---

## ✅ Phase 1: Foundation (COMPLETED)

- [x] LVGL 9.3 setup with XML support
- [x] Navigation bar with proper flex layout
- [x] Home panel with declarative XML
- [x] Reactive data binding (Subject-Observer pattern)
- [x] Theme system with global constants
- [x] FontAwesome icon integration with auto-generation
- [x] Screenshot utility supporting multiple binaries
- [x] Comprehensive documentation
- [x] C++ wrappers for XML panels with clean API
- [x] LVGL logging integration

## ✅ Phase 2: Navigation & Blank Panels (COMPLETED)

**Priority: High** - Enable panel switching with reactive navbar highlighting

- [x] **Navigation State Management**
  - Active panel tracking via integer subject
  - Navbar button click handlers (C++ event handlers)
  - Panel visibility toggling (show/hide based on active state)
  - Reactive navbar icon highlighting (red #ff4444 for active, white #ffffff for inactive)

- [x] **Blank Placeholder Panels**
  - Home panel (with printer status content)
  - Controls panel (blank placeholder)
  - Filament panel (blank placeholder)
  - Settings panel (blank placeholder)
  - Advanced panel (blank placeholder)

- [x] **Navigation Implementation**
  - Updated navigation_bar.xml with FontAwesome icons
  - Created C++ wrapper (ui_nav.cpp) with Subject-Observer pattern
  - Panel switching via clickable navigation icons
  - Icon color updates based on active panel

**Key Learnings:**
- Never call `SDL_PollEvent()` manually - violates LVGL display driver abstraction
- Must create mouse input device with `lv_sdl_mouse_create()` for clicks
- Event handlers in C++ with `lv_obj_add_event_cb()` for `LV_EVENT_CLICKED`
- Labels must have `LV_OBJ_FLAG_EVENT_BUBBLE` and not be clickable for clicks to reach buttons

## 📋 Phase 3: Print Select Panel Core (COMPLETED)

**Status: COMPLETE** - Card view with detail overlay working

- [x] **Print Select Panel** - File browser with dynamic cards
  - ✅ Dynamic XML card component system (print_file_card.xml)
  - ✅ 4-column grid layout (204×280px cards optimized for 1024×800)
  - ✅ Thumbnail display with centering (180×180px images)
  - ✅ Filename truncation with ellipsis
  - ✅ Mock print file data (30 test files)
  - ✅ Utility functions for time/filament formatting
  - ✅ Metadata labels with icons (clock + time, leaf + weight)
  - ✅ Clickable cards opening detail overlay
  - ✅ Full-screen detail view with large thumbnail, options, action buttons
  - ✅ Filament type dropdown (11 filament types)
  - ✅ Automatic bed leveling checkbox
  - ✅ Delete + Print action buttons

**Completed:** 2025-10-12 Evening

---

## ✅ Phase 4: Print Select Panel Polish (COMPLETED)

**Priority: High** - List view, sorting, empty state, confirmations

- [x] **View Modes**
  - [x] View toggle button (icon-only: fa-list ↔ fa-th-large)
  - [x] List view with sortable table layout
  - [x] Instant toggle between card and list views (show/hide containers)

- [x] **Sortable List View**
  - [x] Column headers simplified to 4 columns: Filename, Size, Modified, Print Time
  - [x] Click header to sort ascending/descending
  - [x] Visual sort indicators (▲/▼ arrows in header labels)
  - [x] Default sort: Filename, ascending
  - [x] Toggle direction on same column, switch to ascending on new column
  - [x] Reactive re-rendering with std::sort() and update functions

- [x] **Empty State**
  - [x] Display when no files available
  - [x] "No files available for printing" message (montserrat_20)
  - [x] "Upload gcode files to get started" secondary text (montserrat_16)
  - [x] Centered in content area (400×200px container)
  - [x] Shared between card and list views

- [x] **Confirmation Dialogs**
  - [x] Reusable confirmation dialog XML component (confirmation_dialog.xml)
  - [x] Semi-transparent backdrop overlay (#000000 opacity 180)
  - [x] 400×200px centered dialog card
  - [x] API properties for dynamic title and message
  - [x] Cancel + Confirm buttons (160px each)
  - [x] Click backdrop or Cancel to dismiss
  - [x] Confirm triggers callback function

- [x] **Icon System**
  - [x] Added ICON_LIST and ICON_TH_LARGE to ui_fonts.h
  - [x] Generated #icon_list and #icon_th_large in globals.xml
  - [x] Updated generate-icon-consts.py script

- [x] **Utility Functions**
  - [x] format_file_size() - Converts bytes to "1.2 MB" format
  - [x] format_modified_date() - Formats timestamps to "Jan 15 14:30"

**Completed:** 2025-10-12 Evening

**Key Decisions:**
- Simplified list view from 7 columns to 4 (removed Thumbnail, Filament, Slicer for clarity)
- View toggle shows opposite mode icon (intuitive: shows where you'll go, not where you are)
- Default view: Card mode (visual preference)
- Default sort: Filename ascending (alphabetical)
- Per-session view preference (remembered until restart)

---

## ✅ Phase 5: Controls Panel (MOSTLY COMPLETE)

**Priority: High** - Manual printer control with Bambu X1C-style sub-screens

**Status:** Motion, Nozzle Temp, Bed Temp, Extrusion screens complete. Fan control pending.

### Phase 1: Launcher Panel ✅ COMPLETE (2025-10-12 Night)

- [x] **6-Card Launcher Menu**
  - [x] 2×3 grid layout (400×200px cards)
  - [x] Proper flex row_wrap for card wrapping
  - [x] Vertical scrolling for overflow
  - [x] Card icons + titles + subtitles
  - [x] Click handlers for all 6 cards
  - [x] C++ integration (ui_panel_controls.cpp/h)
  - [x] Fan Control card dimmed as "Coming soon" placeholder

- [x] **Design Specification**
  - [x] 70-page comprehensive UI design document (controls-panel-v1.md)
  - [x] Complete sub-screen mockups and specifications
  - [x] Component file structure defined
  - [x] API patterns documented

- [x] **Icon System Updates**
  - [x] Added 10 new icon constants to ui_fonts.h
  - [x] Regenerated globals.xml with 27 total icons
  - [x] Used existing fa_icons_64 glyphs as placeholders

**Completed:** 2025-10-12 Night

### Phase 2: Reusable Components ✅ COMPLETE (2025-10-12 Late Night)

- [x] **Numeric Keypad Modal Component**
  - [x] XML component (numeric_keypad_modal.xml) with backdrop + modal
  - [x] 700px width, right-docked design
  - [x] Header bar: back button + dynamic title + OK button
  - [x] Input display with large font (montserrat_48) + unit label
  - [x] 3×4 button grid: [7][8][9], [4][5][6], [1][2][3], [EMPTY][0][BACKSPACE]
  - [x] C++ wrapper (ui_component_keypad.cpp/h) with callback API
  - [x] Event handlers for all buttons (digits, backspace, OK, cancel)
  - [x] Input validation (min/max clamping)
  - [x] Backdrop click to dismiss
  - [x] Single reusable instance pattern
  - [x] FontAwesome backspace icon (U+F55A) in fa_icons_32
  - [x] Font regeneration workflow with lv_font_conv

**Files Created:**
- `ui_xml/numeric_keypad_modal.xml` - Full modal component
- `src/ui_component_keypad.cpp` - Implementation with state management
- `include/ui_component_keypad.h` - Public API with config struct
- `package.json` - Updated font ranges for backspace icon

**Files Modified:**
- `ui_xml/globals.xml` - Added keypad dimension and color constants
- `assets/fonts/fa_icons_32.c` - Regenerated with backspace icon
- `src/main.cpp` - Keypad initialization and test demo

**Completed:** 2025-10-12 Late Night

---

### Phase 3: Motion Sub-Screen ✅ COMPLETE (2025-11-15)

- [x] **Movement Controls**
  - [x] Button grid jog pad (3×3 grid with center home)
  - [x] Z-axis buttons (↑10, ↑1, ↓1, ↓10)
  - [x] Distance selector (0.1mm, 1mm, 10mm, 100mm radio buttons)
  - [x] Real-time position display (X/Y/Z with reactive subjects)
  - [x] Home buttons (All, X, Y, Z)
  - [x] Back button navigation
  - [x] Wired to Moonraker gcode_script() API

### Phase 4: Temperature Sub-Screens ✅ COMPLETE (2025-11-15)

- [x] **Nozzle Temperature Screen**
  - [x] Current/Target temperature display with live updates
  - [x] Preset buttons (Off, PLA 210°, PETG 240°, ABS 250°)
  - [x] Custom button opens numeric keypad
  - [x] Temperature graph with real-time data
  - [x] Confirm button with green styling
  - [x] Back button navigation

- [x] **Heatbed Temperature Screen**
  - [x] Current/Target temperature display with live updates
  - [x] Preset buttons (Off, PLA 60°, PETG 80°, ABS 100°)
  - [x] Custom button opens numeric keypad
  - [x] Temperature graph with real-time data
  - [x] Confirm button with green styling
  - [x] Back button navigation

### Phase 5: Extrusion Sub-Screen ✅ COMPLETE (2025-11-15)

- [x] **Extrude/Retract Controls**
  - [x] Amount selector (5mm, 10mm, 25mm, 50mm radio buttons)
  - [x] Extrude button (green, icon + text)
  - [x] Retract button (orange, icon + text)
  - [x] Temperature status card (nozzle temp with checkmark/warning)
  - [x] Safety warning when nozzle < 170°C
  - [x] Back button navigation

### Phase 6: Advanced Features (PENDING)

- [ ] **Fan Control Sub-Screen**
  - [ ] Part cooling fan slider (0-100%)
  - [ ] Preset buttons (Off, 50%, 100%)
  - [ ] Current speed display

- [ ] **Motors Disable**
  - [ ] Confirmation dialog
  - [ ] Send M84 command
  - [ ] Visual feedback

**Design Reference:** Based on Bambu Lab X1C controls system with sub-screens

---

## 📱 Phase 6: Additional Panel Content (FUTURE)

**Priority: Medium** - Build out remaining panel functionality

- [ ] **Storage Source Tabs** (Print Select Panel)
  - Internal/SD card tab navigation
  - Tab bar in panel header
  - Reactive tab switching
  - File list updates per storage source

- [x] **Print Status Panel** ✅ COMPLETE (2025-11-18)
  - ✅ Progress bar with real Moonraker data
  - ✅ Print time/remaining time with reactive updates
  - ✅ Bed/nozzle temperatures with live updates from printer
  - ✅ Pause/Resume/Cancel buttons (wired to Moonraker API)
  - ✅ Wired to Print button on file detail view
  - ✅ Responsive flex layout (fixed previous overlap issues)

- [ ] **Filament Panel** - Filament management
  - Load/unload controls
  - Filament profiles
  - Color/material selection

- [ ] **Settings Panel** - Configuration
  - Network settings
  - Display settings (brightness, theme)
  - Printer settings
  - System info

- [ ] **Advanced/Tools Panel** - Advanced features
  - Bed mesh visualization
  - Console/logs
  - File manager
  - System controls

## 🎨 Phase 6: Enhanced UI Components (FUTURE)

**Priority: Medium** - Richer interactions and reusable widgets

- [ ] **Integer Subjects for Numeric Displays**
  - Progress bars with `bind_value`
  - Sliders with bi-directional binding
  - Arc/gauge widgets

- [ ] **Custom Widgets as XML Components**
  - Temperature display widget
  - Print progress widget
  - Status badge component

- [x] **Additional Modal Patterns** ✅ PARTIALLY COMPLETE
  - [x] ModalBase RAII class for safe lifecycle management (2025-11-17)
  - [x] Toast/snackbar notifications - floating design, top-right position (2025-11-27)
  - [ ] Error message dialogs
  - [ ] Loading indicators

- [ ] **Lists and Scrolling**
  - Virtual scrolling for large lists (100+ items)
  - Settings menu items
  - Log viewer with auto-scroll

## 🔗 Phase 7: Panel Transitions & Polish (PARTIALLY COMPLETE)

**Priority: Low** - Visual refinement and animations

- [x] **Panel Transitions** ✅ PARTIALLY COMPLETE (2025-11-18)
  - [x] Slide animations for overlay panels
  - [ ] Fade in/out animations
  - [x] Proper cleanup when switching panels (via PanelBase)

- [ ] **Button Feedback**
  - Navbar button press effects
  - Hover states (if applicable)
  - Ripple effects

- [ ] **View Transitions**
  - Smooth card ↔ list view transitions
  - Detail overlay fade-in/fade-out
  - Dialog animations

## 🔌 Phase 8: Backend Integration (IN PROGRESS)

**Priority: High** - Connect to Klipper/Moonraker

- [x] **WebSocket Client for Moonraker** ✅ COMPLETE (2025-10-26)
  - [x] Connection management (libhv WebSocketClient)
  - [x] JSON-RPC protocol with auto-incrementing request IDs
  - [x] Auto-discovery chain (objects.list → server.info → printer.info → subscribe)
  - [x] Object categorization (heaters, sensors, fans, LEDs)
  - [x] Persistent notification callbacks
  - [x] Reconnection logic ✅ COMPLETE (2025-11-01)

- [x] **Configuration System** ✅ COMPLETE (2025-10-26)
  - [x] Config singleton with JSON storage
  - [x] Auto-generates helixconfig.json with defaults
  - [x] Multi-printer structure support
  - [x] JSON pointer-based get/set API

- [x] **Moonraker API Phase 1: Core Infrastructure** ✅ COMPLETE (2025-11-01)
  - [x] Connection state machine (5 states with automatic transitions)
  - [x] Request timeout management with PendingRequest tracking
  - [x] Comprehensive error handling (MoonrakerError structure)
  - [x] Configuration-driven timeouts (all values in helixconfig.json)
  - [x] MoonrakerAPI facade layer for high-level operations
  - [x] Automatic cleanup on disconnect with error callbacks

- [~] **Printer State Management** (PARTIAL)
  - [x] PrinterState reactive subjects created
  - [x] Notification callback wired to update_from_notification()
  - [x] Connection state tracking (enhanced with 5-state machine)
  - [ ] Parse and bind all printer objects to subjects
  - [ ] Handle state changes and errors

- [ ] **Moonraker API Phase 2-5** (PLANNED)
  - [ ] Phase 2: File management (list, metadata, delete, upload)
  - [ ] Phase 3: Job control (start, pause, resume, cancel)
  - [ ] Phase 4: Multi-extruder support (dynamic heater discovery)
  - [ ] Phase 5: System administration (updates, power, monitoring)

- [x] **File Operations** ✅ PARTIAL (2025-11-02)
  - [x] List print files (server.files.list) - fully implemented
  - [x] Get file metadata (print time, filament, thumbnails) - fully implemented
  - [x] Delete files - already implemented
  - [x] Start prints - already implemented
  - [x] Thumbnail URL construction - implemented (HTTP download deferred)
  - [ ] Upload files
  - [ ] Thumbnail HTTP downloads (needs libhv HttpClient integration)

- [x] **Real-time Updates** ✅ COMPLETE (2025-11-18)
  - [x] Temperature monitoring - Home panel shows live nozzle/bed temps
  - [x] Print progress - Print status panel shows live progress
  - [x] LED control - Wired to Moonraker with reactive bindings
  - [x] Connection state - Disconnected overlay on home panel

## 🎭 Phase 9: Theming & Accessibility (PARTIALLY COMPLETE)

**Priority: Low** - Visual refinement

- [x] **Theme Variants**
  - [x] Light/Dark mode toggle (runtime theme switching)
  - [x] Theme color variant system (XML-based)
  - [ ] High contrast mode
  - [ ] Custom color schemes

- [x] **Responsive Layouts** ✅ COMPLETE
  - [x] Support multiple resolutions (breakpoint system)
  - [x] LVGL theme breakpoint patch (480/800 breakpoints)
  - [x] Comprehensive test suite (50 tests)
  - [x] DPI scaling for different hardware profiles
  - [ ] Orientation changes

- [ ] **Animations**
  - Button press effects
  - Panel transitions
  - Loading animations
  - Success/error feedback

- [ ] **Accessibility**
  - Larger touch targets
  - High contrast options
  - Status announcements

## 🧪 Phase 10: Testing & Optimization (ONGOING)

**Priority: Ongoing** - Quality assurance

- [ ] **Memory Profiling**
  - Check for leaks
  - Optimize subject usage
  - Profile panel switching

- [ ] **Performance Testing**
  - Frame rate monitoring
  - UI responsiveness
  - Touch latency

- [x] **Build Quality** ✅ COMPLETE
  - [x] Zero LVGL XML warnings
  - [x] Zero spdlog/fmt deprecation warnings
  - [x] Independent spdlog submodule (fmt 11.2.0)
  - [x] Clean build output
  - [x] Doxygen API documentation generation

- [x] **Responsive Theme Testing** ✅ COMPLETE
  - [x] 50-test comprehensive suite
  - [x] Breakpoint classification tests
  - [x] DPI scaling tests (4 hardware profiles)
  - [x] Theme toggle verification

- [ ] **Cross-platform Testing**
  - Raspberry Pi target
  - BTT Pad target
  - Different screen sizes

- [ ] **Edge Case Handling**
  - Connection loss
  - Print errors
  - File system errors

## 🎯 Phase 11: First-Run Configuration Wizard (HIGH PRIORITY)

**Priority: High** - Initial setup flow for new installations

**Status: IN PROGRESS** - Hardware selection complete, other steps pending

**Progress:**
- ✅ Wizard framework (navigation, responsive constants, subjects)
- ✅ WiFi setup screen (fully working)
- ✅ **Hardware selection screens (steps 4-7)** - ✅ COMPLETE (2025-11-02)
  - Dynamic dropdown population from MoonrakerClient discovery
  - Hardware filtering (bed, hotend, fans by type, LEDs)
  - Fixed critical layout bug (LV_SIZE_CONTENT → flex_grow)
  - Config template updated with all wizard fields
- ✅ **Wizard header component extraction** (2025-11-27)
  - Reusable wizard_header_bar.xml component
  - Contextual subtitles per wizard step
  - Improved UX with consistent navigation
- ⚠️ Connection/printer ID/summary screens (XML exists, not fully wired)
- ⏳ Next: Real printer testing + remaining wizard step implementation

### User Story
New users need a guided setup wizard to:
1. Connect to their Moonraker instance (manual IP entry)
2. Map auto-discovered printer components to UI defaults
3. Save these mappings for consistent behavior

### Requirements

- [ ] **First-Run Detection**
  - [ ] Detect missing/incomplete configuration on startup
  - [ ] Show wizard automatically on first run
  - [ ] Allow re-run from settings panel ("Reset Configuration")
  - [ ] Skip wizard if config is complete

- [ ] **Moonraker Connection Screen**
  - [ ] Manual IP address entry field with validation
  - [ ] Port configuration field (default 7125)
  - [ ] "Test Connection" button with spinner/status feedback
  - [ ] Connection status display (success/error messages)
  - [ ] "Next" button (disabled until successful connection)
  - [ ] Save connection settings to helixconfig.json
  - [ ] Optional future enhancement: mDNS/Bonjour auto-discovery scan

- [x] **Hardware Mapping Wizard** - ✅ UI COMPLETE (Backend wiring pending)
  - [x] Auto-detect available components via printer.objects.list
  - [x] Multi-screen wizard flow (one category per screen):
    1. ✅ **Heated Bed Selection**
       - Dropdown: Select heater (filtered by "bed" keyword)
       - Dropdown: Select sensor (all sensors shown)
       - Dynamic population from MoonrakerClient
    2. ✅ **Hotend Selection**
       - Dropdown: Select heater (filtered by "extruder"/"hotend")
       - Dropdown: Select sensor (filtered by "extruder"/"hotend")
       - Dynamic population from MoonrakerClient
    3. ✅ **Fan Selection**
       - Dropdown: Hotend fan (filtered: "heater_fan", "hotend_fan")
       - Dropdown: Part cooling fan (filtered: excludes heater fans)
       - Dynamic population from MoonrakerClient
    4. ✅ **LED Selection**
       - Dropdown: Main LED (all LEDs shown)
       - "None" option available
  - [x] Back/Next navigation buttons on each screen (part of wizard framework)
  - [ ] Summary screen showing all selections before saving (pending)
  - [x] Save mappings to config file under printer-specific section (event callbacks wired)

- [ ] **Configuration Storage**
  - [ ] Extend helixconfig.json schema with hardware mappings:
    ```json
    {
      "printers": {
        "default_printer": {
          "moonraker_host": "192.168.1.112",
          "moonraker_port": 7125,
          "hardware_map": {
            "heated_bed": {
              "heater": "heater_bed",
              "sensor": "heater_bed"
            },
            "hotend": {
              "heater": "extruder",
              "sensor": "extruder"
            },
            "fans": {
              "hotend_fan": "heater_fan hotend_fan",
              "part_cooling_fan": "fan",
              "bed_fans": ["fan_generic bed_fan_1", "fan_generic bed_fan_2"]
            },
            "leds": {
              "main_led": "neopixel chamber_led"
            }
          }
        }
      }
    }
    ```
  - [ ] Config validation on load
  - [ ] Migration logic for old configs

- [ ] **UI Components Needed**
  - [ ] Wizard panel wrapper (progress indicator, back/next/finish buttons)
  - [ ] IP address input field with numeric keypad integration
  - [ ] Dropdown/select widgets for component selection
  - [ ] Multi-select checkbox list for optional components
  - [ ] Success/error message overlays
  - [ ] Progress spinner for connection testing

### Design Notes

- **Auto-default behavior:** If only one obvious component exists (e.g., single heater_bed, single extruder), pre-select it and skip that wizard screen
- **Validation:** Ensure selections are valid before allowing user to proceed
- **User control:** Always allow manual override of auto-detected defaults
- **mDNS Discovery:** Future enhancement - Moonraker supports zeroconf component but not universally enabled. Manual IP entry is primary method.

### Dependencies

- Requires Phase 8 (Backend Integration) complete
- MoonrakerClient auto-discovery functional
- Config system with JSON pointer API

### Testing

- [ ] Test with no config file (first run)
- [ ] Test with partial config (migration)
- [ ] Test with complete config (skip wizard)
- [ ] Test connection failures (invalid IP, port, timeout)
- [ ] Test with various printer configurations:
  - Single extruder
  - Multi-extruder (extruder + extruder1)
  - No heated bed
  - Multiple fans
  - No LEDs

---

## 🚀 Phase 12: Production Readiness (FUTURE)

**Priority: Future** - Deployment prep

- [ ] **Error Handling**
  - Graceful degradation
  - Error recovery
  - User notifications

- [ ] **Logging System**
  - Structured logging
  - Log levels
  - Log rotation

- [ ] **Build System Improvements**
  - Cross-compilation setup
  - Packaging for targets
  - Install scripts

---

## 🔧 Phase 13: G-code Pre-Print Modification (IN PROGRESS)

**Priority: Medium** - Enable/disable print operations before starting

**Status: IN PROGRESS** - Architecture defined, implementation underway

### Overview
Allow users to enable/disable optional print operations (bed leveling, purge line, etc.) before starting a print, with automatic G-code modification.

### Components

- [ ] **GcodeOpsDetector** (`gcode_ops_detector.cpp`)
  - [ ] Detect bed leveling sequences (G28, G29, BED_MESH_CALIBRATE)
  - [ ] Detect purge/prime line sequences
  - [ ] Detect other configurable operations
  - [ ] Return line ranges for each detected operation

- [ ] **GcodeFileModifier** (`gcode_file_modifier.cpp`)
  - [ ] Comment out line ranges to disable operations
  - [ ] Inject custom G-code sequences
  - [ ] Create modified G-code files for printing

- [ ] **CommandSequencer** (`command_sequencer.cpp`)
  - [ ] Coordinate multi-step print preparation
  - [ ] Handle async operations (preheat → level → print)
  - [ ] Manage operation dependencies

- [ ] **PrinterCapabilities** (`printer_capabilities.cpp`)
  - [ ] Track what features printer supports
  - [ ] Auto-detect from Klipper config
  - [ ] Store in helixconfig.json

### UI Integration
- [ ] Print file detail checkboxes for:
  - [ ] "Run bed leveling" (if detected in G-code)
  - [ ] "Run purge line" (if detected)
  - [ ] "Preheat before print"
- [ ] Visual feedback for detected operations

### Files
```
include/gcode_ops_detector.h     # Operation detection API
include/gcode_file_modifier.h    # G-code modification API
include/command_sequencer.h      # Multi-step coordination
include/printer_capabilities.h   # Printer feature tracking
src/gcode_ops_detector.cpp
src/gcode_file_modifier.cpp
src/command_sequencer.cpp
src/printer_capabilities.cpp
tests/unit/test_gcode_ops_detector.cpp
tests/unit/test_gcode_file_modifier.cpp
tests/unit/test_command_sequencer.cpp
tests/unit/test_printer_capabilities.cpp
```

### Design Doc
See `docs/PRINT_OPTIONS_IMPLEMENTATION_PLAN.md` for full specification.

---

## ✅ Phase 14: Class-Based Panel Architecture (COMPLETED)

**Priority: N/A** - Completed 2025-11-17

**Summary:** Migrated all panels from function-based to class-based architecture using PanelBase.

- [x] **PanelBase class** - Encapsulates panel lifecycle (init, show, hide, destroy)
- [x] **All 13+ panels migrated** - Home, Controls, Motion, Temps, Print Select, etc.
- [x] **Wizard steps migrated** - All 7 wizard steps use class-based pattern
- [x] **Deprecated wrappers removed** - Clean break from old function-based API

**Benefits:**
- RAII lifecycle management
- Consistent show/hide behavior
- Easier testing and maintenance
- Clear ownership semantics

**Reference:** `docs/archive/PANEL_MIGRATION.md`, `docs/archive/PANEL_REFACTORING_PLAN.md`

---

## Recent Work (2025-11-14 to 2025-11-28)

| Feature | Phase | Date |
|---------|-------|------|
| Class-based panel refactoring (PanelBase) | 14 | 2025-11-17 |
| Print Status → real Moonraker API | 8 | 2025-11-18 |
| Home Panel → live temps/LEDs | 8 | 2025-11-18 |
| Motion sub-screen complete | 5 | 2025-11-15 |
| Temperature sub-screens complete | 5 | 2025-11-15 |
| Extrusion sub-screen complete | 5 | 2025-11-15 |
| Toast redesign (floating, top-right) | 6 | 2025-11-27 |
| Wizard header component extraction | 11 | 2025-11-27 |
| ModalBase RAII pattern | 6 | 2025-11-17 |
| Slide animations for overlays | 7 | 2025-11-18 |
| Printer database v2.0 (50+ printers) | 11 | 2025-11-22 |
| Clang-format pre-commit hooks | 10 | 2025-11-17 |

---

## Notes

- **Reactive Pattern:** All UI updates should use Subject-Observer pattern
- **XML First:** Prefer XML layout over C++ when possible
- **Clean Separation:** Keep business logic in C++, layout in XML
- **Documentation:** Update guides as patterns emerge
- **UI Patterns:** Document reusable patterns in LVGL9_XML_GUIDE.md (e.g., vertical accent bars, dynamic card components)
- **Responsive Design:** Use constants in globals.xml for easy layout adjustments (4-col vs 3-col grids)
- **Testing:** Test each phase before moving to next
