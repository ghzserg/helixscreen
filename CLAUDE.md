# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the **LVGL 9 UI Prototype** for HelixScreen - a declarative XML-based touch UI system using LVGL 9.3 with reactive Subject-Observer data binding. The prototype runs on SDL2 for rapid development and will eventually target framebuffer displays on embedded hardware.

**Key Innovation:** Complete separation of UI layout (XML) from business logic (C++), similar to modern web frameworks. No manual widget management - all updates happen through reactive subjects.

## Quick Commands

```bash
make                          # Incremental build (auto-parallel)
make clean && make            # Clean rebuild
./build/bin/helix-ui-proto    # Run simulator
python3 scripts/generate-icon-consts.py  # Regenerate icon constants
```

**Binary:** `build/bin/helix-ui-proto`
**Panels:** home, controls, motion, nozzle-temp, bed-temp, extrusion, filament, settings, advanced, print-select

### Screenshot Workflow ⚠️

**ALWAYS use the screenshot script instead of manual BMP/magick commands:**

```bash
# Correct approach:
./scripts/screenshot.sh helix-ui-proto output [panel_name]

# Examples:
./scripts/screenshot.sh helix-ui-proto extrusion-test extrusion
./scripts/screenshot.sh helix-ui-proto controls-launcher controls
./scripts/screenshot.sh helix-ui-proto home-panel home
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

**CRITICAL:** All new source files MUST include GPL v3 copyright header.

**Reference:** `docs/COPYRIGHT_HEADERS.md` for templates (C/C++, XML variants)

### 5. Image Scaling in Flex Layouts ⚠️

**When scaling images immediately after layout changes:** Call `lv_obj_update_layout()` first, otherwise containers report 0x0 size (LVGL uses deferred layout calculation).

**Utility functions:** `ui_image_scale_to_cover()` / `ui_image_scale_to_contain()` in ui_utils.h

**Reference:** ui_panel_print_status.cpp:249-314, ui_utils.cpp:213-276

### 6. Navigation History Stack ⚠️

**Always use `ui_nav_push_overlay()` and `ui_nav_go_back()` for overlay panels (motion, temps, extrusion):**

```cpp
// When showing overlay
ui_nav_push_overlay(motion_panel);  // Pushes current to history, shows overlay

// In back button callback
if (!ui_nav_go_back()) {
    // Fallback: manual navigation if history is empty
}
```

**Behavior:**
- Clicking nav bar icons clears history automatically
- State preserved when navigating back
- All back buttons should use this pattern

**Reference:** ui_nav.h:54-62, ui_nav.cpp:250-327, HANDOFF.md Pattern #0

## Common Gotchas

**⚠️ READ DOCUMENTATION FIRST:** Before implementing features in these areas, **ALWAYS read the relevant documentation** to avoid common pitfalls:
- **XML syntax/attributes:** Read **docs/LVGL9_XML_GUIDE.md** "Troubleshooting" section (lines 1130-1323) FIRST
- **Component patterns/registration:** Read **docs/QUICK_REFERENCE.md** "Registration Order" and examples FIRST
- **Icon workflow:** Read **docs/QUICK_REFERENCE.md** "Icon & Image Assets" section (lines 273-377) FIRST
- **Architecture patterns:** Reference existing working implementations (motion_panel, nozzle_temp_panel) FIRST

### Quick Gotcha Reference

1. **✅ LVGL 9 XML Flag Attribute Syntax** - NEVER use `flag_` prefix in XML attributes. LVGL 9 XML uses simplified syntax:
   - ❌ Wrong: `flag_hidden="true"`, `flag_clickable="true"`, `flag_scrollable="false"`
   - ✅ Correct: `hidden="true"`, `clickable="true"`, `scrollable="false"`

   **Why:** LVGL 9 XML property system auto-generates simplified attribute names from enum values. The C enum is `LV_PROPERTY_OBJ_FLAG_HIDDEN` but the XML attribute is just `hidden`. Parser silently ignores attributes with `flag_` prefix.

   **Status:** ✅ **FIXED** (2025-10-24) - All 12 XML files updated, 80+ incorrect usages corrected. See **STATUS.md** for details.

2. **Subject registration conflict** - If `globals.xml` declares subjects, they're registered with empty values before C++ initialization. Solution: Remove `<subjects>` from globals.xml.

3. **Icon constants not rendering** - Run `python3 scripts/generate-icon-consts.py` to regenerate UTF-8 byte sequences. **Note:** Icon values appear empty in terminal/grep (FontAwesome uses Private Use Area U+F000-U+F8FF) but contain UTF-8 bytes. Verify with: `python3 -c "import re; f=open('ui_xml/globals.xml'); print([match.group(1).encode('utf-8').hex() for line in f for match in [re.search(r'icon_backspace.*value=\"([^\"]*)\"', line)] if match])"` should output `['ef959a']`

4. **BMP screenshots too large** - Always convert to PNG before reading: `magick screenshot.bmp screenshot.png`

5. **Labels not clickable** - Use `lv_button` instead of `lv_label`. While XML has a `clickable` attribute, it doesn't work reliably with labels.

6. **Component names** - LVGL uses **filename** as component name, not view's `name` attribute. File `nozzle_temp_panel.xml` → component `nozzle_temp_panel`.

7. **Right-aligned overlays** - Use `align="right_mid"` attribute for panels docked to right edge (motion, temp, keypad).

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

**[HANDOFF.md](HANDOFF.md)** - **ACTIVE WORK & NEXT PRIORITIES**
- **ONLY contains:** Ongoing work, current problems, active bugs, pending tasks
- What's being worked on RIGHT NOW (incomplete work)
- What needs to be done NEXT (priorities)
- Critical architecture patterns (how-to sections)
- Known gotchas that affect current work
- **Update this:** When starting new work, completing tasks, or changing priorities
- **Do NOT put:** Completed work (goes in STATUS.md), historical details
- **Rule:** If it's finished/resolved, remove it from HANDOFF and document in STATUS.md
- **Clean up:** Remove stale completed items every session - keep it current

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

This project has specialized agents - use them proactively to keep context smaller and leverage domain expertise.

### Project-Specific Agents

**widget-maker** - LVGL 9 UI expert
- Creating/modifying UI panels and components
- Implementing XML layouts with reactive data binding
- Working with LVGL 9 XML patterns and subjects
- **Use when:** Any UI panel or component work

**ui-reviewer** - LVGL 9 UI auditor
- Analyzing screenshots against requirements
- Identifying layout/styling issues
- Providing detailed XML fixes
- **Use when:** After taking screenshots, auditing existing UI

**moonraker-api-agent** - Klipper/Moonraker integration expert
- WebSocket communication patterns
- JSON-RPC protocol implementation
- Real-time printer state synchronization
- **Use when:** Implementing Moonraker integration (future work)

### General Agents (see global CLAUDE.md)

**Explore** - Fast codebase exploration
- "How does X work?"
- "Where are Y handled?"
- Architectural pattern discovery
- **Specify thoroughness:** `quick`, `medium`, or `very thorough`

**general-coding-agent** - C++17/embedded systems expert
- Multi-file C++ implementations
- Complex business logic
- Cross-component features

**refractor** - Code optimization
- Refactoring existing code
- Pattern improvements
- Performance optimization

**When in doubt:** Delegate to an agent. They work independently and report back concisely.

## Development Workflow

1. Edit XML for layout changes (no recompilation needed)
2. Edit C++ for logic/subjects changes → `make`
3. Test with `./build/bin/helix-ui-proto [panel_name]`
4. Screenshot with `./scripts/screenshot.sh` or press 'S' in UI
5. For complex multi-step tasks → use appropriate agent (see above)

## Project Status

**Current state:** Navigation history stack complete. All UI panels functional with mock data. Ready for interactive testing and Moonraker integration.

**Recent:** Navigation history (2025-10-25), Print status panel (2025-10-24), Print select/detail views with thumbnail scaling (2025-10-24)

**For development history:** See STATUS.md (chronological accomplishments)
**For planned work:** See docs/ROADMAP.md (future features/milestones)
**For handoff:** See HANDOFF.md (current focus and next priorities)
