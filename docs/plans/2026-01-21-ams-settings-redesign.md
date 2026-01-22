# AMS Settings Redesign

## Overview

Consolidate and improve the AMS settings UI from 7 sparse, text-heavy panels to a single settings panel plus enhanced main AMS panel with visual configuration.

## Current State

7 separate AMS settings sub-panels:
1. Tool Mapping - vertical list of dropdowns
2. Endless Spool - vertical list of dropdowns
3. Maintenance - vertical list of action cards
4. Behavior - vertical list of toggle cards
5. Calibration - stub (Device Actions filtered)
6. Speed Settings - stub (Device Actions filtered)
7. Spoolman - vertical list of settings cards

Problems:
- Too many panels, deep navigation
- Text-heavy, no visual relationships shown
- Sparse information density
- Tool/slot configuration separate from slot visualization

## New Architecture

### Main AMS Panel (Enhanced)

Move visual slot configuration into the main AMS panel where users already see their slots.

**Additions to existing panel:**

1. **Tool badges on slots** - Small "T0", "T1" labels in corner of each slot showing tool assignment

2. **Endless spool arrows** - Curved arrows connecting slots that have backup relationships. Shows chains at a glance (Slot 1 → Slot 3 → Slot 5). Use subtle color (`#text_secondary`) to avoid overwhelming filament colors.

3. **Tap slot → popup editor** - Tap any slot to open a small popup positioned adjacent to the slot (keeping slot visible for context). Contains:
   - Slot header ("Slot 3")
   - Tool dropdown (T0, T1, T2... or "None")
   - Backup slot dropdown (Slot 1, Slot 2... or "None")
   - Done/close button

   Popup is compact (~150-180px wide), config-only (slot info already visible on grid).

**Slot grid density:**
- Match Bambu-style compact layout (~60-70px per slot)
- Group by AMS unit with visual tray separation
- Can fit 8 slots per row, 16 total with two rows
- Reuse existing AMS panel slot components

### Settings → AMS → Device Operations (Single Panel)

One consolidated panel with categorized cards (matching controls panel pattern).

**Card 1: Quick Actions & Behavior**
- Home / Recover / Abort buttons in a row (3 compact action buttons)
- Bypass Mode toggle (with label + description)
- Auto-Heat status indicator (if supported by backend)

**Card 2: Calibration**
- Section header "Calibration"
- Dynamic actions from `backend->get_device_actions("calibration")`
- Renders as buttons, toggles, or sliders based on action type
- Typically 3-5 items (AFC: calibration wizards, Happy Hare: servo calibration, encoder tests, gate checks)

**Card 3: Speed Settings**
- Section header "Speed Settings"
- Dynamic actions from `backend->get_device_actions("speed")`
- Backend-specific speed controls
- Typically 3-5 items

### Spoolman Settings

Weight sync settings (sync toggle, refresh interval) move to the existing Spoolman settings panel where they logically belong. Not AMS-specific.

## Navigation Changes

**Before:**
```
Settings → AMS Settings (hub)
├── Tool Mapping
├── Endless Spool
├── Maintenance
├── Behavior
├── Calibration
├── Speed Settings
└── Spoolman
```

**After:**
```
Main AMS Panel
└── [tap slot → edit tool mapping + endless spool]

Settings
├── AMS → Device Operations (single panel, 3 cards)
└── Spoolman → [weight sync settings here]
```

7 panels → 1 panel. Visual config moves to main AMS panel.

## Visual Design Notes

### Tool Badges
- Small corner badge on each slot
- Text: "T0", "T1", etc.
- Use `text_small` typography
- Position: top-right or bottom-left corner of slot

### Endless Spool Arrows
- Curved connecting lines between slots
- Color: `#text_secondary` (subtle, doesn't compete with filament colors)
- Arrow direction shows primary → backup relationship
- Multiple chains visible simultaneously

### Slot Popup
- Width: ~150-180px
- Position: adjacent to tapped slot (not centered)
- Background: `#card_bg`
- Border radius: `#border_radius`
- Contains: header + 2 dropdowns + close button
- Dismiss: tap outside or close button

### Device Operations Cards
- Follow existing `ui_card` pattern from controls panel
- Section headers within cards for grouping
- Buttons in horizontal row where appropriate (Quick Actions)
- Toggles with label + description for behavior settings

## Implementation Phases

### Phase 1: Main AMS Panel Enhancements
1. Add tool badge component to slot visualization
2. Implement endless spool arrow overlay
3. Create slot edit popup component
4. Wire up tool mapping save to backend
5. Wire up endless spool save to backend

### Phase 2: Device Operations Panel
1. Create new Device Operations panel/overlay
2. Implement Quick Actions & Behavior card
3. Implement dynamic Calibration card
4. Implement dynamic Speed Settings card
5. Update main settings panel navigation

### Phase 3: Cleanup
1. Remove old AMS settings sub-panels (6 panels)
2. Remove AMS Settings hub panel
3. Move Spoolman weight sync to Spoolman settings
4. Update any navigation references

## Files Affected

### New Files
- `ui_xml/ams_slot_edit_popup.xml` - Slot configuration popup
- `include/ui_ams_slot_edit_popup.h` - Popup controller
- `src/ui/ui_ams_slot_edit_popup.cpp` - Popup implementation
- `ui_xml/ams_device_operations_panel.xml` - Consolidated device operations
- `include/ui_ams_device_operations_overlay.h` - Panel controller
- `src/ui/ui_ams_device_operations_overlay.cpp` - Panel implementation

### Modified Files
- `ui_xml/ams_panel.xml` - Add tool badges, arrow overlay container
- `src/ui/ui_panel_ams.cpp` - Slot tap handling, popup management, arrow rendering
- `ui_xml/settings_panel.xml` - Update AMS navigation rows
- `src/ui/ui_panel_settings.cpp` - Update AMS navigation
- `ui_xml/spoolman_settings.xml` - Add weight sync settings (if not already there)

### Deleted Files
- `ui_xml/ams_settings_panel.xml` - Hub panel (no longer needed)
- `ui_xml/ams_settings_tool_mapping.xml`
- `ui_xml/ams_settings_endless_spool.xml`
- `ui_xml/ams_settings_maintenance.xml`
- `ui_xml/ams_settings_behavior.xml`
- `ui_xml/ams_settings_device_actions.xml`
- `ui_xml/ams_settings_spoolman.xml`
- Corresponding C++ files for above

## Progress

- [ ] Phase 1: Main AMS Panel Enhancements
- [ ] Phase 2: Device Operations Panel (partial - overlay created, needs navigation)
- [ ] Phase 3: Cleanup

## Success Criteria

1. Tool mapping configurable directly from main AMS panel
2. Endless spool relationships visible as arrows on main AMS panel
3. All device operations accessible from single settings panel
4. Navigation depth reduced (fewer taps to reach any setting)
5. Information density improved (more visible at once)
6. Visual consistency with existing AMS panel and controls panel patterns
