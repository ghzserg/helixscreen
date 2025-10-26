# Filament Panel - UI Requirements v1

**Panel Name:** Filament Panel (Filament Management)
**Inspiration:** GuppyScreen ExtruderPanel + Bambu Lab filament controls
**Status:** Design Phase - Ready for Implementation
**Date:** 2025-10-25

---

## Overview

The Filament Panel provides filament loading/unloading operations with material-specific temperature management. This panel focuses on pre-print filament operations (loading, unloading, purging) as distinct from in-print extrusion controls (which exist in the Extrusion sub-screen under Controls).

**Architecture:**
- **Full-panel layout** accessed via navbar (4th icon - filament spool)
- **Two-column design:** Left (visualization + status), Right (controls)
- **Temperature integration:** Material presets with automatic heating
- **Safety features:** Temperature checks, visual warnings
- **Reuses existing components:** Temperature graph widget, header bar pattern

**Key Features:**
- Material-specific temperature presets (PLA, PETG, ABS, Custom)
- Load/Unload/Purge filament operations
- Real-time temperature monitoring with graph
- Safety checks (minimum 170°C for extrusion)
- Visual status indicators

---

## 1. Overall Layout

### 1.1 Panel Structure
- **Layout Type:** Full-panel (not overlay) - accessed via navigation bar
- **Background:** Dark panel background (`#panel_bg` = `#1e1e1e`)
- **Padding:** 20px all sides
- **Content Layout:** Two-column horizontal flex
  - **Left Column (45%):** Visualization + temperature status card
  - **Right Column (55%):** Controls (presets + action buttons)

### 1.2 Dimensions
- **Container:** 100% width × 100% height (fills content area)
- **Left Column:** ~410px width (45% of 910px content width)
- **Right Column:** ~500px width (55% of 910px content width)
- **Gap:** 20px between columns

---

## 2. Left Column - Visualization & Status

### 2.1 Filament Spool Visualization

**Container:**
- **Size:** 100% column width × 320px height
- **Background:** `#card_bg` (`#2a2a2a`)
- **Border Radius:** 12px
- **Padding:** 24px
- **Alignment:** Center content

**Image Asset:**
- **File:** `assets/images/filament_spool.png` (EXISTING)
- **Display Size:** Scale to fit within container (maintain aspect ratio)
- **Fallback:** FontAwesome icon `fa-spool` (if image unavailable)
- **Centering:** Horizontally and vertically centered

**Alternative Assets (EXISTING):**
- `assets/images/material/filament_img.c` - Filament icon
- `assets/images/material/load_filament_img.c` - Load filament icon
- `assets/images/material/unload_filament_img.c` - Unload filament icon

**Note:** May use load/unload icons in action buttons instead of or in addition to FontAwesome icons.

### 2.2 Temperature Status Card

**Container:**
- **Position:** Below visualization (20px gap)
- **Size:** 100% column width × auto height
- **Background:** `#card_bg` (`#2a2a2a`)
- **Border Radius:** 12px
- **Padding:** 20px
- **Layout:** Vertical flex

**Header:**
- **Text:** "Nozzle Temperature"
- **Font:** montserrat_20 (bold)
- **Color:** `#text_primary` (`#ffffff`)
- **Margin Bottom:** 16px

**Temperature Display:**
- **Format:** `210°C / 220°C` (current / target)
- **Font:** montserrat_28 (large, bold)
- **Color:** `#text_primary`
- **Alignment:** Centered
- **Reactive Binding:** `bind_text` to `filament_temp_display` subject

**Status Indicator:**
- **Text:** Reactive status message
  - ✓ "Ready to load" (temp within 5°C of target, >170°C)
  - ⚡ "Heating to 210°C..." (heating in progress)
  - ❄ "Nozzle cold - select material" (temp < 170°C)
- **Font:** montserrat_14 (italic)
- **Color:** Dynamic
  - Green (`#4caf50`) - Ready
  - Orange (`#ff9800`) - Heating
  - Gray (`#text_secondary`) - Cold
- **Margin Top:** 8px
- **Reactive Binding:** `bind_text` to `filament_status` subject

**Temperature Graph (Optional Enhancement):**
- **Component:** Reuse `ui_temp_graph` widget (existing)
- **Size:** 100% width × 120px height
- **Position:** Below status indicator (12px gap)
- **Series:** Single series (nozzle temperature)
- **Time Range:** Last 5 minutes
- **Grid:** Show temperature gridlines
- **Note:** Can be added in v2 if time permits, not critical for v1

---

## 3. Right Column - Control Panel

### 3.1 Section Header

**Text:** "Material Selection"
**Font:** montserrat_16 (semi-bold)
**Color:** `#text_secondary`
**Margin Bottom:** 12px

### 3.2 Material Preset Buttons

**Layout:** 2×2 grid of radio-style buttons
**Button Grid:**
- **Columns:** 2
- **Rows:** 2
- **Gap:** 12px between buttons
- **Button Size:** 220px × 56px each

**Button List:**

| Position | Label | Icon | Nozzle Temp | Description |
|----------|-------|------|-------------|-------------|
| Top-Left | PLA | fa-leaf | 210°C | PLA filament |
| Top-Right | PETG | fa-cube | 240°C | PETG filament |
| Bottom-Left | ABS | fa-fire | 250°C | ABS filament |
| Bottom-Right | Custom... | fa-edit | Variable | Open keypad |

**Button Styles:**
- **Background (Unselected):** `#3a3a3a` (dark gray)
- **Background (Selected):** `#ff4444` (red accent)
- **Text Color (Unselected):** `#text_secondary` (`#b0b0b0`)
- **Text Color (Selected):** `#text_primary` (`#ffffff`)
- **Border Radius:** 8px
- **Padding:** 12px
- **Font:** montserrat_16 (bold)

**Button Layout (Per Button):**
- **Horizontal flex:** icon (left) + label + temp (right)
- **Icon Size:** 24px
- **Icon Color:** Inherits text color
- **Temperature Display:** "210°C" montserrat_14, right-aligned

**Behavior:**
- **Click:** Select material, set target temperature, begin heating
- **Visual Feedback:** Change background to red, white text
- **Radio Group:** Only one preset can be selected at a time
- **Custom Button:** Opens numeric keypad modal for manual temp entry

**Reactive State:**
- **Subject:** `filament_material_selected` (int: 0=PLA, 1=PETG, 2=ABS, 3=Custom)
- **Binding:** Button `flag_checked` binds to subject value match

### 3.3 Action Buttons Section

**Section Header:**
- **Text:** "Filament Operations"
- **Font:** montserrat_16 (semi-bold)
- **Color:** `#text_secondary`
- **Margin Top:** 32px
- **Margin Bottom:** 12px

**Buttons Layout:** Vertical stack (3 buttons)
**Button Size:** 100% width × 72px height
**Gap:** 12px between buttons

#### 3.3.1 Load Filament Button

**Style:**
- **Background:** `#4caf50` (green)
- **Background (Disabled):** `#3a3a3a` (gray)
- **Border Radius:** 8px
- **Padding:** 16px
- **Layout:** Horizontal flex (icon + text)

**Icon:**
- **Source:** `load_filament_img.c` OR fa-arrow-down-to-line (`\uf33d`)
- **Size:** 32px
- **Color:** `#ffffff`
- **Position:** Left-aligned with 12px right margin

**Label:**
- **Text:** "Load Filament"
- **Font:** montserrat_20 (bold)
- **Color:** `#ffffff`
- **Alignment:** Left (after icon)

**Behavior:**
- **Enabled When:** Nozzle temp >= 170°C
- **Disabled When:** Nozzle temp < 170°C (grayed out, no click)
- **Action:** Execute LOAD_FILAMENT macro (configurable, stubbed for now)
- **Visual Feedback:** Press animation (scale 0.98)

**Reactive Binding:**
- **State Subject:** `filament_extrusion_allowed` (bool)
- **Attribute:** `flag_clickable` binds to subject value

#### 3.3.2 Unload Filament Button

**Style:**
- **Background:** `#ff9800` (orange)
- **Background (Disabled):** `#3a3a3a` (gray)
- **Other Styles:** Same as Load button

**Icon:**
- **Source:** `unload_filament_img.c` OR fa-arrow-up-from-line (`\uf342`)
- **Size:** 32px
- **Color:** `#ffffff`

**Label:**
- **Text:** "Unload Filament"
- **Font/Color:** Same as Load button

**Behavior:**
- **Enabled/Disabled:** Same as Load button (>= 170°C)
- **Action:** Execute UNLOAD_FILAMENT macro (stubbed)

#### 3.3.3 Purge Filament Button

**Style:**
- **Background:** `#2196f3` (blue)
- **Background (Disabled):** `#3a3a3a` (gray)
- **Other Styles:** Same as Load button

**Icon:**
- **Icon:** fa-droplet (`\uf043`)
- **Size:** 24px
- **Color:** `#ffffff`

**Label:**
- **Text:** "Purge 10mm"
- **Font/Color:** Same as Load button

**Behavior:**
- **Enabled/Disabled:** Same as Load button (>= 170°C)
- **Action:** Execute short extrude (G1 E10 F300) to prime nozzle
- **Use Case:** After loading filament, verify flow

### 3.4 Safety Warning Card

**Container:**
- **Position:** Bottom of right column
- **Size:** 100% width × auto height
- **Background:** `rgba(255, 68, 68, 0.15)` (red with low opacity)
- **Border:** 2px solid `#ff4444` (red)
- **Border Radius:** 8px
- **Padding:** 16px
- **Margin Top:** 20px
- **Visibility:** Hidden when temp >= 170°C, shown when temp < 170°C

**Icon:**
- **Symbol:** ⚠ warning triangle (fa-triangle-exclamation `\uf071`)
- **Size:** 24px
- **Color:** `#ff4444`
- **Position:** Top-left

**Title:**
- **Text:** "Nozzle Too Cold"
- **Font:** montserrat_16 (bold)
- **Color:** `#ff4444`
- **Margin Bottom:** 8px

**Message:**
- **Text:** "Heat nozzle to at least 170°C before loading or unloading filament."
- **Font:** montserrat_14
- **Color:** `#text_secondary`
- **Line Height:** 1.4

**Current Temp Display:**
- **Format:** "Current: 25°C | Target: 0°C"
- **Font:** montserrat_14
- **Color:** `#text_secondary`
- **Margin Top:** 8px
- **Reactive Binding:** `bind_text` to subject

**Reactive Binding:**
- **Subject:** `filament_safety_warning_visible` (bool)
- **Attribute:** `flag_hidden` binds to inverted subject value

---

## 4. Numeric Keypad Integration

**Component:** Reuse existing `numeric_keypad_modal.xml` (ALREADY IMPLEMENTED)

**Trigger:** Click "Custom..." material preset button

**Configuration:**
```cpp
ui_keypad_config_t config = {
    .title = "Custom Temperature",
    .initial_value = 200,  // Default suggestion
    .min_value = 0,
    .max_value = 350,      // Nozzle max temp
    .unit_label = "°C",
    .callback = on_custom_temp_confirmed,
    .user_data = nullptr
};
ui_component_keypad_show(&config);
```

**Callback Behavior:**
- **On Confirm (OK):** Set target temperature to entered value, begin heating
- **On Cancel (Back/Backdrop):** Close keypad, no changes

---

## 5. Reactive Data Bindings (Subject-Observer)

### 5.1 Temperature Subjects

```cpp
// Shared with other temperature panels (nozzle_temp, bed_temp)
extern lv_subject_t nozzle_current_temp_subject;  // int (°C)
extern lv_subject_t nozzle_target_temp_subject;   // int (°C)
```

**Format:** Integer temperature values (Celsius)
**Update Rate:** 1-2 seconds (from Moonraker when integrated)
**Current Implementation:** Mock data for prototype

### 5.2 UI State Subjects

```cpp
// Filament panel specific
lv_subject_t filament_temp_display;              // string: "210°C / 220°C"
lv_subject_t filament_status;                    // string: "Ready to load" / "Heating..." / etc.
lv_subject_t filament_material_selected;         // int: 0=PLA, 1=PETG, 2=ABS, 3=Custom
lv_subject_t filament_extrusion_allowed;         // bool: nozzle >= 170°C
lv_subject_t filament_safety_warning_visible;    // bool: nozzle < 170°C
```

### 5.3 Subject Initialization Pattern

**File:** `src/ui_panel_filament.cpp`

```cpp
void ui_panel_filament_init_subjects() {
    lv_subject_init_string(&filament_temp_display, "0°C / 0°C");
    lv_subject_init_string(&filament_status, "Select material to begin");
    lv_subject_init_int(&filament_material_selected, -1);  // None selected
    lv_subject_init_int(&filament_extrusion_allowed, 0);   // false
    lv_subject_init_int(&filament_safety_warning_visible, 1);  // true (cold at start)
}
```

**CRITICAL:** Must call `ui_panel_filament_init_subjects()` BEFORE creating XML (see CLAUDE.md Pattern #1)

### 5.4 Temperature Update Logic

```cpp
void update_filament_temperature_display() {
    int current = lv_subject_get_int(&nozzle_current_temp_subject);
    int target = lv_subject_get_int(&nozzle_target_temp_subject);

    // Update display string
    char buf[32];
    snprintf(buf, sizeof(buf), "%d°C / %d°C", current, target);
    lv_subject_set_string(&filament_temp_display, buf);

    // Update safety state
    bool extrusion_allowed = (current >= 170);
    lv_subject_set_int(&filament_extrusion_allowed, extrusion_allowed ? 1 : 0);
    lv_subject_set_int(&filament_safety_warning_visible, extrusion_allowed ? 0 : 1);

    // Update status message
    const char* status;
    if (current >= target - 5 && current >= 170) {
        status = "✓ Ready to load";
    } else if (target > 0 && current < target) {
        snprintf(buf, sizeof(buf), "⚡ Heating to %d°C...", target);
        status = buf;
    } else {
        status = "❄ Select material to begin";
    }
    lv_subject_set_string(&filament_status, status);
}
```

---

## 6. Color Palette & Typography

### 6.1 Colors (from globals.xml)

```xml
<color name="panel_bg" value="0x1e1e1e"/>         <!-- Panel background -->
<color name="card_bg" value="0x2a2a2a"/>          <!-- Card background -->
<color name="primary_color" value="0xff4444"/>    <!-- Red accent -->
<color name="text_primary" value="0xffffff"/>     <!-- White text -->
<color name="text_secondary" value="0xb0b0b0"/>   <!-- Gray text -->
<color name="success_color" value="0x4caf50"/>    <!-- Green (load) -->
<color name="warning_color" value="0xff9800"/>    <!-- Orange (unload) -->
<color name="info_color" value="0x2196f3"/>       <!-- Blue (purge) -->
<color name="error_color" value="0xf44336"/>      <!-- Red (errors/warnings) -->
```

### 6.2 Typography

```xml
montserrat_28 - Large temperature display
montserrat_20 - Section headers, button labels
montserrat_16 - Body text, material labels
montserrat_14 - Status messages, secondary text
```

### 6.3 Icon List (FontAwesome 6)

**Existing Icons (AVAILABLE):**
```cpp
#define ICON_FIRE           "\uf06d"  // fa-fire (heating)
#define ICON_DROPLET        "\uf043"  // fa-droplet (purge)
#define ICON_LEAF           "\uf06c"  // fa-leaf (PLA/eco)
#define ICON_CUBE           "\uf1b2"  // fa-cube (PETG)
#define ICON_EDIT           "\uf044"  // fa-edit (custom)
#define ICON_TRIANGLE_EXCLAMATION "\uf071"  // fa-triangle-exclamation (warning)
```

**New Icons Needed (ADD TO ui_fonts.h):**
```cpp
#define ICON_ARROW_DOWN_TO_LINE  "\uf33d"  // fa-arrow-down-to-line (load)
#define ICON_ARROW_UP_FROM_LINE  "\uf342"  // fa-arrow-up-from-line (unload)
#define ICON_SPOOL              "\uf3cd"  // fa-spool (filament spool, if available)
```

**Action:** Run `python3 scripts/generate-icon-consts.py` after adding to `ui_fonts.h`

**Alternative:** Use existing image assets instead of FontAwesome for load/unload icons

---

## 7. Component File Structure

### 7.1 XML Files

```
ui_xml/
└── filament_panel.xml              # Main filament panel layout
```

**Dependencies:**
- `globals.xml` - Color/dimension constants
- `numeric_keypad_modal.xml` - Custom temperature entry (EXISTING)

### 7.2 C++ Files

```
src/
└── ui_panel_filament.cpp           # Panel logic + subject management

include/
└── ui_panel_filament.h             # Public API + initialization
```

**Reference Implementations (STUDY THESE PATTERNS):**
- `src/ui_panel_controls_temp.cpp` - Temperature preset buttons, keypad integration
- `src/ui_panel_controls_extrusion.cpp` - Safety checks, extrusion logic
- `src/ui_panel_nozzle_temp.cpp` - Temperature graph integration

### 7.3 Asset Files

**Existing Assets (READY TO USE):**
```
assets/images/
├── filament_spool.png              # Main visualization graphic
└── material/
    ├── filament_img.c              # Filament icon (compiled image)
    ├── load_filament_img.c         # Load icon (compiled image)
    └── unload_filament_img.c       # Unload icon (compiled image)
```

**Note:** Compiled image files (.c) are generated from PNGs via LVGL converter. Can be used directly with `lv_image_set_src()`.

---

## 8. Implementation Phases

### Phase 1: Foundation (PRIORITY)

- [ ] **Add new icons to ui_fonts.h**
  - ICON_ARROW_DOWN_TO_LINE (`\uf33d`)
  - ICON_ARROW_UP_FROM_LINE (`\uf342`)
  - Run `python3 scripts/generate-icon-consts.py`

- [ ] **Create filament_panel.xml**
  - Two-column layout (visualization | controls)
  - Material preset buttons (2×2 grid)
  - Action buttons (Load, Unload, Purge)
  - Temperature status card
  - Safety warning card
  - Reactive bindings for all dynamic content

- [ ] **Create ui_panel_filament.cpp/h**
  - Subject initialization (6 subjects listed above)
  - Material preset button handlers
  - Action button handlers (stubbed for Moonraker)
  - Temperature update logic
  - Safety state management
  - Panel initialization function

- [ ] **Wire to navigation**
  - Update `app_layout.xml` to include `<filament_panel name="filament_panel"/>`
  - Update `ui_nav.cpp` to show/hide filament panel
  - Test navigation from navbar (4th icon)

### Phase 2: Visualization & Assets (HIGH)

- [ ] **Integrate filament spool image**
  - Use `filament_spool.png` in visualization container
  - Implement proper scaling/centering
  - Fallback to FontAwesome icon if image fails

- [ ] **Use compiled image assets for buttons**
  - Load `load_filament_img.c` for Load button icon
  - Load `unload_filament_img.c` for Unload button icon
  - Test button rendering with images vs FontAwesome

### Phase 3: Temperature Integration (HIGH)

- [ ] **Connect to shared temperature subjects**
  - Link to `nozzle_current_temp_subject` (from temp panels)
  - Link to `nozzle_target_temp_subject`
  - Implement update callbacks
  - Test with mock temperature changes

- [ ] **Integrate numeric keypad**
  - Wire "Custom..." button to `ui_component_keypad_show()`
  - Configure keypad for temperature entry (0-350°C)
  - Implement confirmation callback
  - Test custom temperature setting

- [ ] **Optional: Temperature graph widget**
  - Integrate `ui_temp_graph` component (EXISTING)
  - Add to temperature status card
  - Configure for nozzle temperature series
  - Test real-time graph updates

### Phase 4: Safety & Polish (MEDIUM)

- [ ] **Safety logic implementation**
  - Temperature threshold checks (170°C)
  - Button enable/disable based on temp
  - Warning card visibility toggling
  - Visual feedback for unsafe states

- [ ] **Material preset logic**
  - Radio button group behavior
  - Temperature setting on preset click
  - Visual selection feedback
  - Default to no selection on panel open

- [ ] **Action button feedback**
  - Press animations (scale effect)
  - Disabled state styling
  - Loading indicators (future: during macro execution)

### Phase 5: Testing & Screenshots (MEDIUM)

- [ ] **Functional testing**
  - Test all material presets (PLA, PETG, ABS, Custom)
  - Test action buttons (enabled/disabled states)
  - Test custom temperature entry via keypad
  - Test safety warning visibility

- [ ] **Screenshot testing**
  - Use `./scripts/screenshot.sh helix-ui-proto filament-cold filament`
  - Capture with cold nozzle (warning visible)
  - Capture with heated nozzle (buttons enabled)
  - Capture with material selected
  - Test across screen sizes (tiny, small, large)

- [ ] **Navigation testing**
  - Test navbar switching to filament panel
  - Test panel visibility toggle
  - Verify no memory leaks on panel switching

### Phase 6: Documentation (LOW)

- [ ] **Create changelog document**
  - Use template: `docs/templates/ui-changelog-template.md`
  - Document implementation details
  - Note any deviations from spec
  - Screenshot gallery

- [ ] **Update HANDOFF.md**
  - Mark filament panel as complete
  - Document any known issues
  - List Moonraker integration TODOs

- [ ] **Update STATUS.md**
  - Add entry for filament panel completion
  - List features implemented
  - Note any future enhancements

---

## 9. Open Questions & Decisions

### 9.1 Temperature Graph Widget

**Question:** Should v1 include the temperature graph in the status card?

**Options:**
1. Include graph (reuse `ui_temp_graph` component) - adds visual richness
2. Skip graph for v1, add in v2 - simpler initial implementation

**Recommendation:** **Skip for v1** - Focus on core functionality first. Graph is a nice-to-have but not critical for filament operations. Can be added easily in v2 as enhancement.

### 9.2 Image Assets vs FontAwesome Icons

**Question:** Should action buttons use compiled image assets or FontAwesome icons?

**Options:**
1. Use existing compiled images (`load_filament_img.c`, etc.)
2. Use FontAwesome icons for consistency
3. Hybrid: Images for main buttons, FA icons for presets

**Recommendation:** **Start with FontAwesome** for consistency and simplicity. Compiled images can be tested as v2 enhancement if they look better.

### 9.3 Spoolman Integration

**Question:** Should we add Spoolman spool selection in v1?

**Background:** GuppyScreen has separate SpoolmanPanel for filament inventory management (active spool, material tracking, etc.)

**Recommendation:** **No, defer to future phase.** This is Settings/Advanced panel territory. Filament panel should focus on loading/unloading operations. Spoolman integration requires:
- Moonraker API connection
- Spool database browsing
- Active spool management
- Separate UI for spool selection

**Future Enhancement:** Add "Select Spool..." button that opens Spoolman browser overlay (Phase 7+).

### 9.4 Macro Configuration

**Question:** Should macros be configurable (LOAD_FILAMENT vs custom names)?

**Recommendation:** **Hard-code for v1, make configurable later.** Use standard macro names:
- `LOAD_FILAMENT` (load operation)
- `UNLOAD_FILAMENT` (unload operation)
- `M83 \n G1 E10 F300` (direct G-code for purge)

When Moonraker integration happens, can add config file support for custom macro names.

---

## 10. Success Criteria

### 10.1 Functionality

- [ ] Material preset buttons select material and set target temp
- [ ] Custom button opens numeric keypad for manual temp entry
- [ ] Load/Unload/Purge buttons enabled when temp >= 170°C
- [ ] Load/Unload/Purge buttons disabled when temp < 170°C
- [ ] Safety warning visible when cold, hidden when hot
- [ ] Temperature display updates reactively (mock data for now)
- [ ] Navigation from navbar works (show/hide panel)

### 10.2 Visual Quality

- [ ] Matches existing panel design aesthetic (dark theme, red accents)
- [ ] Consistent spacing and alignment with other panels
- [ ] Icons render clearly at all sizes
- [ ] Text is legible and properly sized
- [ ] Cards and buttons have appropriate visual feedback
- [ ] Filament spool image displays correctly (centered, scaled)

### 10.3 Usability

- [ ] Material presets are easy to select (large touch targets)
- [ ] Action buttons are clearly labeled and color-coded
- [ ] Safety warnings are prominent and clear
- [ ] Temperature status is easy to read at a glance
- [ ] Custom temperature entry is intuitive (keypad)

### 10.4 Performance

- [ ] Panel switching is smooth (no lag)
- [ ] Button presses are responsive
- [ ] No frame drops during temperature updates
- [ ] Memory usage stable during panel switching

---

## 11. Future Enhancements (Post-v1)

### 11.1 Temperature Graph Widget
- Add real-time temperature graph to status card
- Show heating progress visually
- Historical temperature data (last 5-10 minutes)

### 11.2 Advanced Filament Operations
- Filament change macro (M600 support)
- Purge amount customization (5mm, 10mm, 25mm options)
- Speed control for load/unload operations

### 11.3 Spoolman Integration
- Active spool display (material, color, remaining)
- "Select Spool" button opening spool browser
- Automatic material preset based on active spool
- Filament tracking (consumed amount)

### 11.4 Visual Enhancements
- Animation during load/unload (rotating spool graphic)
- Glowing effect during heating
- Color-coded temperature status (cold=blue, heating=orange, ready=green)

### 11.5 Macro Configurability
- Config file support for custom macro names
- User-defined load/unload sequences
- Speed/temperature profiles per material

---

## 12. Resources & References

### 12.1 Existing Code to Study

**Temperature Management Patterns:**
- `src/ui_panel_controls_temp.cpp` (lines 1-500) - Preset buttons, keypad integration
- `ui_xml/nozzle_temp_panel.xml` - Two-column layout with presets

**Safety Logic Patterns:**
- `src/ui_panel_controls_extrusion.cpp` (lines 180-250) - Temperature safety checks
- `ui_xml/extrusion_panel.xml` - Safety warning card implementation

**Subject Management Patterns:**
- `src/ui_panel_motion.cpp` (lines 1-100) - Subject initialization order
- `src/ui_nav.cpp` (lines 50-150) - Reactive state updates

**Numeric Keypad Usage:**
- `src/ui_component_keypad.cpp` - Complete keypad implementation
- `src/ui_panel_controls_temp.cpp` (lines 150-200) - Keypad callback example

### 12.2 Documentation References

**Critical Reading (READ FIRST):**
- `CLAUDE.md` - Project patterns and gotchas (Subject init order!)
- `docs/QUICK_REFERENCE.md` - Common code patterns
- `docs/LVGL9_XML_GUIDE.md` - XML syntax and troubleshooting
- `docs/COPYRIGHT_HEADERS.md` - GPL v3 header templates for new files

**Design References:**
- `docs/requirements/controls-panel-v1.md` - Similar panel structure
- `docs/RESPONSIVE_DESIGN_PATTERN.md` - Layout scaling patterns

**GuppyScreen Reference (Parent Project):**
- `/Users/pbrown/code/guppyscreen/src/extruder_panel.cpp` - Load/unload implementation
- `/Users/pbrown/code/guppyscreen/src/spoolman_panel.cpp` - Filament inventory (future)

### 12.3 Web Research Summary

**Klipper Filament Macros (Common Patterns):**
- Default macro names: `LOAD_FILAMENT`, `UNLOAD_FILAMENT`
- Typical load sequence: Heat → Move toolhead → Extrude → Verify
- Typical unload sequence: Heat → Retract → Cool (optional)
- Purge/prime: 5-10mm extrusion at slow speed (F300 = 5mm/s)
- Safety: Always check temp >= 170°C before extruding

**Material Temperature Ranges (Standard Presets):**
- PLA: 190-220°C (recommend 210°C)
- PETG: 230-250°C (recommend 240°C)
- ABS: 240-260°C (recommend 250°C)
- TPU: 210-230°C (not included in v1)

### 12.4 Asset Files Inventory

**Available Image Assets (EXISTING):**
```bash
assets/images/filament_spool.png                 # 512x512px spool graphic
assets/images/material/filament_img.c            # Compiled LVGL image
assets/images/material/load_filament_img.c       # Compiled LVGL image
assets/images/material/unload_filament_img.c     # Compiled LVGL image
```

**FontAwesome Icons (Available in fa_icons_32/64):**
- Fire icon (heating) - U+F06D
- Droplet icon (purge) - U+F043
- Leaf icon (PLA) - U+F06C
- Cube icon (PETG) - U+F1B2
- Edit icon (custom) - U+F044
- Warning triangle - U+F071

**Icons to Add:**
- Arrow down to line (load) - U+F33D
- Arrow up from line (unload) - U+F342

---

## 13. Implementation Checklist

### Pre-Implementation

- [ ] Read `CLAUDE.md` critical patterns (Subject init order, component names)
- [ ] Read `docs/QUICK_REFERENCE.md` for common code patterns
- [ ] Study `ui_panel_controls_temp.cpp` for temperature preset pattern
- [ ] Study `ui_panel_controls_extrusion.cpp` for safety logic pattern
- [ ] Verify all image assets are accessible (`ls assets/images/filament*`)

### Phase 1: Core Implementation

- [ ] Add new icon constants to `ui_fonts.h`
- [ ] Run `python3 scripts/generate-icon-consts.py`
- [ ] Create `ui_xml/filament_panel.xml` with full layout
- [ ] Create `include/ui_panel_filament.h` with public API
- [ ] Create `src/ui_panel_filament.cpp` with subject initialization
- [ ] Add GPL v3 copyright headers to all new files
- [ ] Update `app_layout.xml` to include filament_panel component
- [ ] Update `ui_nav.cpp` to handle filament panel switching
- [ ] Update `Makefile` to compile new source files (if needed)

### Phase 2: Testing

- [ ] Build: `make clean && make`
- [ ] Run: `./build/bin/helix-ui-proto -s large -p filament`
- [ ] Test material preset selection
- [ ] Test custom temperature keypad
- [ ] Test action button enable/disable states
- [ ] Test safety warning visibility
- [ ] Screenshot: `./scripts/screenshot.sh helix-ui-proto filament-test filament`

### Phase 3: Documentation

- [ ] Create changelog: `docs/changelogs/filament-panel-implementation.md`
- [ ] Update `HANDOFF.md` with completion status
- [ ] Update `STATUS.md` with session entry
- [ ] Update `docs/ROADMAP.md` to mark filament panel complete

---

**Document Version:** 1.0
**Last Updated:** 2025-10-25
**Status:** Ready for Implementation
**Next Step:** Phase 1 - Add icons and create XML layout
**Estimated Time:** 2-3 hours for complete implementation
