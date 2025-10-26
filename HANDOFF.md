# Session Handoff Document

**Last Updated:** 2025-10-26
**Current Focus:** Config + Moonraker Auto-Discovery Complete - Ready for Settings UI and Real Printer Testing

---

## Recent Work (2025-10-26 Evening)

### Config System + Moonraker Auto-Discovery + Connection ✅ COMPLETE
- Created `Config` singleton class with JSON storage and auto-migration
- Implemented full auto-discovery chain in `MoonrakerClient`
- Auto-categorizes heaters, sensors, fans, LEDs from `printer.objects.list`
- Connects to Moonraker on startup with config values
- Wires notification updates to `PrinterState` reactive subjects
- Successfully tested connection to `ws://127.0.0.1:7125/websocket`

**Config Features:**
- JSON pointer-based get/set API (`get<T>(json_ptr)`, `set<T>(json_ptr, value)`)
- Auto-generates `helixconfig.json` with sensible defaults on first run
- Multi-printer structure (ready for expansion)
- Empty sensors/fans arrays (populated by auto-discovery)

**Discovery Features:**
- Discovery chain: `objects.list` → `server.info` → `printer.info` → `subscribe`
- Intelligent object categorization via string prefix matching
- Subscribes to core objects + all discovered components
- Logs comprehensive printer metadata

**Integration:**
- Config initialized before LVGL (main.cpp:334-335)
- MoonrakerClient connects on startup (main.cpp:633-668)
- Discovery triggered on successful connection
- Connection state updates UI subject

**Testing:**
- ✅ Config generation works
- ✅ WebSocket connection successful
- ⚠️ Discovery parsing needs refinement for "not ready" state

**Files Created:**
- `include/config.h`, `src/config.cpp` - Config singleton
- `helixconfig.json` - Auto-generated runtime config

**Files Modified:**
- `include/moonraker_client.h` - Added discovery methods and object storage
- `src/moonraker_client.cpp` - Implemented discovery chain (~173 lines added)
- `src/main.cpp` - Integrated config and Moonraker connection

### Earlier: Moonraker Integration Foundation ✅ COMPLETE (2025-10-26 Morning)
- Integrated libhv WebSocket library (static linking via parent repo)
- Created `MoonrakerClient` wrapper class with JSON-RPC support
- Created `PrinterState` reactive state manager with LVGL subjects
- Cross-platform build system (macOS/Linux-aware NPROC + linker flags)
- Increased `LV_DRAW_THREAD_STACK_SIZE` to 32KB (eliminates warning)
- Files: `include/moonraker_client.h`, `src/moonraker_client.cpp`, `include/printer_state.h`, `src/printer_state.cpp`, `Makefile`, `lv_conf.h`

---

## Project Status

**All UI components complete. Infrastructure ready for Moonraker integration.**

Navigation system robust. All panels render correctly across all screen sizes. Reactive state management infrastructure in place with LVGL subjects.

### What Works
- ✅ Navigation system with history stack
- ✅ All UI panels functional with mock data
- ✅ Responsive design (480×320 to 1280×720)
- ✅ Material Design icons with dynamic recoloring
- ✅ **Config** - JSON-based configuration with auto-migration
- ✅ **MoonrakerClient** - WebSocket client with auto-discovery
- ✅ **PrinterState** - Reactive state manager with subjects
- ✅ **Cross-platform build** - macOS/Linux-aware Makefile
- ✅ **Connection on Startup** - Connects and discovers printer automatically

### Next Steps
- ⚠️ **Refine discovery response parsing** - Handle Klipper "not ready" state gracefully
- 🎨 **Build Settings UI panel** - Connection config (host/port), display sleep, log level
- 🧪 **Test with live "ready" Klipper printer** - Verify discovery and subscription work end-to-end
- 🔌 **Bind UI to real subjects** - Replace mock data with printer_state subjects in XML
- 🔌 **Implement control actions** - Wire buttons to gcode_script() calls (motion, temps, extrusion)

---

## Critical Architecture Patterns

### Navigation System

Always use `ui_nav_push_overlay()` and `ui_nav_go_back()`:

```cpp
// Show overlay panel
ui_nav_push_overlay(motion_panel);

// Back button
ui_nav_go_back();  // Handles stack, shows previous or HOME
```

Nav bar buttons clear stack automatically. State preserved when navigating back.

**CRITICAL:** Never hide `app_layout` - prevents navbar disappearing.

### Subject Initialization Order

Subjects MUST be initialized BEFORE XML creation:

```cpp
// 1. Register XML components
lv_xml_component_register_from_file("A:/ui_xml/globals.xml");

// 2. Initialize subjects FIRST
ui_nav_init();
ui_panel_home_init_subjects();

// 3. NOW create UI
lv_obj_t* screen = lv_xml_create(NULL, "app_layout", NULL);
```

### Event Callbacks

Use `<lv_event-call_function>`, NOT `<event_cb>`:

```xml
<lv_button name="my_button">
    <lv_event-call_function trigger="clicked" callback="my_handler"/>
</lv_button>
```

Register in C++ before XML loads:
```cpp
lv_xml_register_event_cb(NULL, "my_handler", my_handler_function);
```

### Component Names

Always add explicit `name` attributes to component tags:

```xml
<lv_obj name="content_area">
  <controls_panel name="controls_panel"/>  <!-- Explicit name required -->
</lv_obj>
```

### Name-Based Widget Lookup

Always use names, never indices:

```cpp
// ✓ CORRECT
lv_obj_t* widget = lv_obj_find_by_name(parent, "widget_name");

// ✗ WRONG
lv_obj_t* widget = lv_obj_get_child(parent, 3);
```

---

## Next Priority: Moonraker Integration 🔌

**All UI complete. Ready to connect to live printer.**

### Step 1: WebSocket Foundation
- Review existing HelixScreen Moonraker client code (parent repo)
- Adapt libhv WebSocket implementation
- Connect on startup, handle connection events

### Step 2: Printer Status Updates
- Subscribe to printer object updates
- Wire temperature subjects to live data
- Update home panel with real-time temps

### Step 3: Motion & Control Commands
- Jog buttons → `printer.gcode.script` (G0/G1)
- Temperature presets → M104/M140 commands
- Home buttons → G28 commands

### Step 4: Print Management
- File list → `server.files.list` API
- Print start/pause/resume/cancel commands
- Live print status updates

**Existing subjects (already wired):**
- Print progress, layer, elapsed/remaining time
- Nozzle/bed temps, speed, flow
- Print state (Printing/Paused/Complete)

---

## Testing Commands

```bash
# Build
make                          # Incremental build (auto-parallel)
make clean && make            # Clean rebuild

# Run
./build/bin/helix-ui-proto                    # Default (medium, home panel)
./build/bin/helix-ui-proto -s tiny            # 480×320
./build/bin/helix-ui-proto -s large           # 1280×720
./build/bin/helix-ui-proto -p controls        # Start at Controls
./build/bin/helix-ui-proto -p print-select    # Print select

# Controls
# Cmd+Q (macOS) / Win+Q (Windows) to quit
# 'S' key to save screenshot

# Screenshot
./scripts/screenshot.sh helix-ui-proto output-name [panel-name]
```

**Screen sizes:** tiny (480×320), small (800×480), medium (1024×600), large (1280×720)

**Panel names:** home, controls, motion, nozzle-temp, bed-temp, extrusion, print-select, file-detail, filament, settings, advanced

---

## Documentation

- **[STATUS.md](STATUS.md)** - Complete chronological development journal
- **[ROADMAP.md](docs/ROADMAP.md)** - Planned features
- **[LVGL9_XML_GUIDE.md](docs/LVGL9_XML_GUIDE.md)** - LVGL 9 XML reference
- **[QUICK_REFERENCE.md](docs/QUICK_REFERENCE.md)** - Common patterns

---

## Known Gotchas

### LVGL 9 XML Attribute Names

**No `flag_` prefix:**
```xml
<!-- ✓ CORRECT -->
<lv_button hidden="true" clickable="false"/>

<!-- ✗ WRONG -->
<lv_button flag_hidden="true" flag_clickable="false"/>
```

**Use `style_image_*`, not `style_img_*`:**
```xml
<!-- ✓ CORRECT -->
<lv_image style_image_recolor="#primary_color" style_image_recolor_opa="255"/>

<!-- ✗ WRONG -->
<lv_image style_img_recolor="#primary_color" style_img_recolor_opa="255"/>
```

**Use `scale_x`/`scale_y`, not `zoom`:**
```xml
<!-- ✓ CORRECT (256 = 100%) -->
<lv_image scale_x="128" scale_y="128"/>

<!-- ✗ WRONG -->
<lv_image zoom="128"/>
```

### Subject Type Must Match API

Image recoloring requires color subjects:
```cpp
// ✓ CORRECT
lv_subject_init_color(&subject, lv_color_hex(0xFFD700));
lv_obj_set_style_img_recolor(widget, color, LV_PART_MAIN);

// ✗ WRONG
lv_subject_init_string(&subject, buffer, NULL, size, "0xFFD700");
```

---

**For complete development history, see STATUS.md**
