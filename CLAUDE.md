# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the **LVGL 9 UI Prototype** for GuppyScreen - a declarative XML-based touch UI system using LVGL 9.3 with reactive Subject-Observer data binding. The prototype runs on SDL2 for rapid development and will eventually target framebuffer displays on embedded hardware.

**Key Innovation:** Complete separation of UI layout (XML) from business logic (C++), similar to modern web frameworks. No manual widget management - all updates happen through reactive subjects.

## Quick Commands

```bash
make                          # Incremental build (auto-parallel)
make clean && make            # Clean rebuild
./build/bin/guppy-ui-proto    # Run simulator
python3 scripts/generate-icon-consts.py  # Regenerate icon constants
```

**Binary:** `build/bin/guppy-ui-proto`
**Panels:** home, controls, motion, nozzle-temp, bed-temp, extrusion, filament, settings, advanced, print-select

### Screenshot Workflow ⚠️

**ALWAYS use the screenshot script instead of manual BMP/magick commands:**

```bash
# Correct approach:
./scripts/screenshot.sh guppy-ui-proto output [panel_name]

# Examples:
./scripts/screenshot.sh guppy-ui-proto extrusion-test extrusion
./scripts/screenshot.sh guppy-ui-proto controls-launcher controls
./scripts/screenshot.sh guppy-ui-proto home-panel home
```

The script handles:
- Building the binary
- Running with 2-second auto-screenshot
- Converting BMP → PNG automatically
- Saving to `/tmp/[output-name].png`

**❌ Avoid:** Reading raw BMPs from `/tmp` and manually running `magick` commands. The screenshot script is the canonical way to capture UI states.

## Architecture

```
XML Components (ui_xml/*.xml)
    ↓ bind_text/bind_value/bind_flag
Subjects (reactive data)
    ↓ lv_subject_set_*/copy_*
C++ Wrappers (src/ui_*.cpp)
```

**Component Hierarchy:**
```
app_layout.xml
├── navigation_bar.xml (5 buttons)
└── content_area
    ├── home_panel.xml
    ├── controls_panel.xml (launcher → motion/temps/extrusion sub-screens)
    ├── print_select_panel.xml
    └── [filament/settings/advanced]_panel.xml
```

All components reference `globals.xml` for shared constants (`#primary_color`, `#nav_width`, etc).

## Critical Patterns (Project-Specific)

**⚠️ IMPORTANT:** When implementing new features, **always reference existing, working code/XML implementations** for:
- Design patterns and architectural approaches
- Naming conventions and file organization
- Event handler patterns and reactive data flow
- XML component structure and styling patterns
- Error handling and logging practices

**Example:** When creating a new sub-screen panel, review `motion_panel.xml` / `ui_panel_motion.cpp` or `nozzle_temp_panel.xml` / `ui_panel_controls_temp.cpp` for established patterns rather than inventing new approaches.

### 1. Subject Initialization Order ⚠️

**MUST initialize subjects BEFORE creating XML:**

```cpp
// CORRECT ORDER:
lv_xml_component_register_from_file("A:/ui_xml/globals.xml");
lv_xml_component_register_from_file("A:/ui_xml/home_panel.xml");

ui_nav_init();                      // Initialize subjects
ui_panel_home_init_subjects();

lv_xml_create(screen, "app_layout", NULL);  // NOW create UI
```

If subjects are created in XML before C++ initialization, they'll have empty/default values.

### 2. Component Instantiation Names ⚠️

**CRITICAL:** Always add explicit `name` attributes to component tags:

```xml
<!-- app_layout.xml -->
<lv_obj name="content_area">
  <controls_panel name="controls_panel"/>  <!-- Explicit name required -->
  <home_panel name="home_panel"/>
</lv_obj>
```

**Why:** Component names in `<view name="...">` definitions do NOT propagate to `<component_tag/>` instantiations. Without explicit names, `lv_obj_find_by_name()` returns NULL.

See **docs/LVGL9_XML_GUIDE.md** section "Component Instantiation: Always Add Explicit Names" for details.

### 3. Widget Lookup by Name

Always use `lv_obj_find_by_name(parent, "widget_name")` instead of index-based `lv_obj_get_child(parent, 3)`.

```cpp
// In XML: <lv_label name="temp_display" bind_text="temp_text"/>
// In C++:
lv_obj_t* label = lv_obj_find_by_name(panel, "temp_display");
```

See **docs/QUICK_REFERENCE.md** for common patterns.

### 4. Copyright Headers ⚠️

**CRITICAL:** All new source files MUST include the GPL v3 copyright header.

```bash
# Reference templates are in:
docs/COPYRIGHT_HEADERS.md
```

**When creating new files:**
1. For `.cpp` and `.h` files: Use the C-style comment header
2. For `.xml` files: Use the XML comment header (after `<?xml version="1.0"?>` if present)
3. Update copyright year if creating files in a new year

The headers identify:
- **Copyright holder:** 356C LLC
- **Author:** Preston Brown <pbrown@brown-house.net>
- **License:** GPL v3.0 or later

**Never create files without copyright headers.** See `docs/COPYRIGHT_HEADERS.md` for complete templates.

## Common Gotchas

1. **Subject registration conflict** - If `globals.xml` declares subjects, they're registered with empty values before C++ initialization. Solution: Remove `<subjects>` from globals.xml.

2. **Icon constants not rendering** - Run `python3 scripts/generate-icon-consts.py` to regenerate UTF-8 byte sequences.

3. **BMP screenshots too large** - Always convert to PNG before reading: `magick screenshot.bmp screenshot.png`

4. **Labels not clickable** - Use `lv_button` instead of `lv_label` with `flag_clickable`. XML parser doesn't apply clickable flag to labels.

5. **Component names** - LVGL uses **filename** as component name, not view's `name` attribute. File `nozzle_temp_panel.xml` → component `nozzle_temp_panel`.

6. **Right-aligned overlays** - Use `align="right_mid"` attribute for panels docked to right edge (motion, temp, keypad).

## Screenshot Workflow

```bash
./scripts/screenshot.sh                    # Auto-capture after 2s
LATEST=$(ls -t /tmp/ui-screenshot-*.bmp | head -1)
magick "$LATEST" "${LATEST%.bmp}.png"      # Convert BMP → PNG
```

Press 'S' in running UI for manual screenshot.

## UI Review System (Optional)

For automated UI verification against requirements:

```bash
./scripts/review-ui-screenshot.sh \
  --screenshot /tmp/screenshot.png \
  --requirements docs/requirements/panel-v1.md \
  --xml-source ui_xml/panel.xml \
  --changelog docs/changelogs/panel-2025-01-13.md
```

Templates available in `docs/templates/`. See section in original CLAUDE.md for full details if needed.

## Documentation Structure

**IMPORTANT:** Each documentation file has a specific purpose. Do NOT duplicate content across files.

### Development History & Planning

**[STATUS.md](STATUS.md)** - **COMPREHENSIVE DEVELOPMENT JOURNAL**
- Chronological history of all development work (newest first)
- What was accomplished each session
- Bugs fixed, features added, decisions made
- **Update this:** After every significant change or session
- **Do NOT duplicate:** Anywhere else - this is the single source of truth for "what happened"

**[ROADMAP.md](docs/ROADMAP.md)** - **PLANNED FEATURES & MILESTONES**
- Future work, planned phases
- Feature prioritization
- Long-term architecture goals
- **Update this:** When planning new features or completing major milestones
- **Do NOT update:** With completed work details (that goes in STATUS.md)

### Quick-Start & Patterns

**[HANDOFF.md](HANDOFF.md)** - **QUICK-START FOR NEXT DEVELOPER**
- Brief current state summary (2-3 paragraphs max)
- Critical architecture patterns (how-to sections)
- Next priorities (what to work on)
- Known gotchas
- **Update this:** When adding new critical patterns or changing priorities
- **Do NOT put:** Historical session details, chronological updates (use STATUS.md)
- **Rule:** If it's not actionable for the next developer, it doesn't belong here

### Technical Reference

**[LVGL9_XML_GUIDE.md](docs/LVGL9_XML_GUIDE.md)** - **COMPLETE LVGL 9 XML REFERENCE**
- XML syntax, attributes, event system
- Component creation patterns
- Troubleshooting (LV_SIZE_CONTENT, event callbacks, etc.)
- Comprehensive technical documentation
- **Update this:** When discovering new LVGL 9 XML patterns or bugs

**[QUICK_REFERENCE.md](docs/QUICK_REFERENCE.md)** - **COMMON PATTERNS QUICK LOOKUP**
- Code snippets for frequent tasks
- Widget lookup, subject binding, event handlers
- Material icon conversion workflow
- **Update this:** When establishing new repeatable patterns

**[COPYRIGHT_HEADERS.md](docs/COPYRIGHT_HEADERS.md)** - **GPL v3 HEADER TEMPLATES**
- Required copyright headers for new files
- C/C++ and XML variants
- **Reference this:** When creating any new source files

## File Organization

```
prototype-ui9/
├── src/              # C++ business logic
├── include/          # Headers
├── ui_xml/           # XML component definitions
├── assets/           # Fonts, images
├── scripts/          # Build/screenshot automation
├── docs/             # Documentation
└── Makefile          # Build system
```

## Using Claude Code Agents

Claude Code provides specialized agents for complex tasks. **Use agents proactively** when tasks match their capabilities.

### Available Agents

**general-purpose**
- Multi-step implementations requiring file searches, reads, and modifications
- Researching complex questions across multiple files
- When uncertain about file locations or need exploratory search
- **Example:** "Implement temperature sub-screens" (Phase 5.4 - successfully used)

**Explore**
- Fast codebase exploration (glob/grep/read operations)
- Quick searches for patterns, keywords, or file structures
- Specify thoroughness: "quick", "medium", or "very thorough"
- **Example:** "Find all panel navigation patterns" or "How do panels register themselves?"

**widget-maker**
- LVGL v9 widget creation using C/C++ and XML layouts
- Implementing new UI components and panels
- Working with LVGL 9 XML system patterns
- **Example:** "Create a new progress bar component" or "Build fan control widget"

**ui-reviewer**
- LVGL v9 XML UI review against requirements
- Analyzes UI screenshots and identifies issues
- Provides detailed XML fixes and suggestions
- **Example:** "Review the motion panel layout against spec"

**general-coding-agent**
- C++17 development and LVGL UI framework tasks
- Implementing panels, UI components, or general C++ features
- Follows GuppyScreen architectural patterns
- **Example:** "Add error handling to WebSocket connection code"

**refractor**
- Code refactoring following best practices
- Restructuring, optimization, improved readability
- **Example:** "Refactor temperature panel code for better modularity"

**moonraker-api-agent**
- Klipper/Moonraker WebSocket/API implementation
- JSON-RPC protocols, real-time state sync
- **Example:** "Implement printer status WebSocket handler"

### When NOT to Use Agents

- Reading specific known file paths (use Read tool directly)
- Searching for specific class definitions (use Glob tool)
- Searching within 2-3 known files (use Read tool)
- Simple, single-file operations

### Agent Usage Examples

**✅ Good Use Cases:**
```
User: "Implement the temperature sub-screens from the spec"
→ Use general-purpose agent with full context and requirements

User: "How does the panel navigation system work?"
→ Use Explore agent with "medium" thoroughness

User: "Create a new fan control widget"
→ Use widget-maker agent with design requirements

User: "Review the motion panel implementation"
→ Use ui-reviewer agent with screenshot and requirements
```

**❌ Bad Use Cases:**
```
User: "Read ui_xml/globals.xml"
→ Use Read tool directly (known path)

User: "Find the motion_panel class definition"
→ Use Grep/Glob directly (simple search)
```

### Agent Best Practices

1. **Provide complete context:** Give agent full task description, expected deliverables
2. **Run in parallel when possible:** Use single message with multiple agent calls
3. **Trust agent outputs:** Agents are autonomous and return comprehensive results
4. **Specify code vs research:** Tell agent if you expect code changes or just investigation

## Development Workflow

1. Edit XML for layout changes (no recompilation needed)
2. Edit C++ for logic/subjects changes → `make`
3. Test with `./build/bin/guppy-ui-proto [panel_name]`
4. Screenshot with `./scripts/screenshot.sh` or press 'S' in UI
5. For complex multi-step tasks → use appropriate agent (see above)

## Recent Accomplishments

**Phase 5.4 (2025-10-13) - Temperature Sub-Screens** ✅
- Nozzle and Bed temperature control panels
- Extended header_bar with optional right button
- Material preset buttons (PLA, PETG, ABS)
- Reactive temperature display (25 / 0°C format)
- Successfully used general-purpose agent for implementation

**Phase 5.3 (2025-10-13) - Motion Panel** ✅
- 8-direction jog pad with custom Unicode arrow font
- Z-axis controls, distance selector, position display
- Fixed button event handling (removed nested containers)

**Phase 5.2 (2025-10-12) - Numeric Keypad** ✅
- Reusable integer input modal component
- Right-docked overlay (700px) with backdrop

**Phase 5.1 (2025-10-12) - Controls Launcher** ✅
- 6-card menu for manual printer control
- Navigation to sub-screens

## Future Integration

This prototype will integrate with main GuppyScreen:
- Replace SDL2 with framebuffer for embedded hardware
- Connect to Moonraker WebSocket for printer data
- Complete remaining panel content (Extrusion, Fan Control)
- Add animations and multi-language support
- Implement temperature graphs/progress displays
- Wire interactive buttons on completed panels
