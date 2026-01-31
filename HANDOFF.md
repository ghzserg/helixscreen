# HANDOFF: Convert ui_ams_slot.cpp to Declarative XML

## Status
**Phase**: Planning
**Branch**: `refactor/ams-slot-to-xml`
**Worktree**: `.worktrees/ams-slot-xml`

## Context

This session discovered that `ui_ams_slot.cpp` (1124 lines) is screaming for conversion to declarative XML. The TODO at line 416 says:
> "TODO: Convert ams_slot to XML component - styling should be declarative"

## What ui_ams_slot.cpp Does

Creates the AMS (Automatic Material System) slot widgets - the visual representation of filament spools. Includes:
- 3D spool visualization (canvas-based pseudo-3D rendering)
- OR flat ring style (simpler 2D)
- Material label (PLA, PETG, etc.)
- Fill level indicator
- Color display
- Selection state
- Click handling for slot editing

## Why This Matters

- 1124 lines of imperative C++ with `lv_obj_set_style_*` calls
- Violates "DATA in C++, APPEARANCE in XML" principle
- Hard to maintain - styling buried in code
- Demonstrates that complex widgets CAN be converted to XML

## The Challenge

The slot uses:
1. **ui_spool_canvas** - 3D canvas rendering (keep in C++, it's drawing code)
2. **Subject bindings** - material name, fill level, color
3. **Dynamic styling** - based on selection state, empty state
4. **Click handlers** - for slot editing

The XML conversion should handle #2, #3, #4. The canvas drawing (#1) stays in C++.

## Approach (TDD)

1. **Write tests first** for AMS slot behavior:
   - Slot creation with default values
   - Material label binding
   - Fill level changes
   - Color changes
   - Selection state styling
   - Click callback invocation

2. **Create XML component** `ui_xml/ams_slot.xml`:
   - Layout structure
   - Subject bindings for material, fill, color
   - Style bindings for selection states
   - Event callback for clicks

3. **Refactor C++ code**:
   - Keep `ui_spool_canvas` as-is (drawing code)
   - Move all styling to XML
   - Keep subject management in C++
   - Register component and callbacks

4. **Verify tests pass**

## Files to Touch

- `src/ui/ui_ams_slot.cpp` - Major refactor
- `include/ui_ams_slot.h` - Possibly simplify API
- `ui_xml/ams_slot.xml` - NEW: declarative layout
- `tests/unit/test_ui_ams_slot.cpp` - NEW or extend existing

## Commands

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/ams-slot-xml
make -j
./build/bin/helix-screen --test -vv  # Visual check
./build/bin/helix-tests "[ams_slot]"  # Run tests
```

## Session Start Prompt

```
Resume work on the AMS slot XML conversion:
- Worktree: .worktrees/ams-slot-xml
- Branch: refactor/ams-slot-to-xml
- Read HANDOFF.md in that worktree for context
- Use /strict-execute with TDD
- This is MAJOR work per CLAUDE.md guidelines
```

## Previous Session Accomplishments

1. Fixed MoonrakerClient callback segfault (merged to main)
2. Added confetti celebration for print completion (merged to main)
3. Set up this worktree for AMS slot conversion

## Notes

- The 3D spool canvas (`ui_spool_canvas`) should NOT be converted - it's actual drawing code
- Focus on the LAYOUT and STYLING, not the rendering
- Look at how `ui_button.cpp` handles XML registration for patterns
