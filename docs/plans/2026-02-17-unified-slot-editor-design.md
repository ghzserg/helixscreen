# Unified Slot Editor Design

## Problem

The AMS context menu has two separate entry points for the same concept: "Edit" (manual metadata) and "Assign Spool" (Spoolman picker). From the user's perspective, both are "tell the system what's in this slot." The split is an implementation detail, not a meaningful UX distinction.

## Solution

Merge Edit and Assign Spool into a single "Edit" flow that adapts based on context (Spoolman connected, slot state). The existing `AmsSpoolmanPicker` gets absorbed into the edit modal as a sub-view.

## Context Menu

Drop from 4 items to 3:
- **Load** / **Unload** (unchanged)
- **Edit** (replaces both "Edit" and "Assign Spool")

## First View Logic

| Spoolman connected? | Slot has data? | First view |
|---------------------|----------------|------------|
| No | No | Empty edit form |
| No | Yes | Edit form with current data |
| Yes | No | Spoolman picker |
| Yes | Yes | Edit form with current data |

## Edit Form

Current fields (unchanged):
- Color swatch (tappable → color picker)
- Vendor dropdown
- Material dropdown
- Remaining % with slider/input
- Nozzle/Bed temps (read-only, from filament definition)

Additions when Spoolman is connected:
- **Spoolman-linked slot**: "Change Spool" button → opens picker sub-view. Spoolman ID badge in header. "Unlink" option.
- **Not linked**: "Link to Spoolman" button → opens picker sub-view.

## Save Logic

### Not Spoolman-linked
Save locally to slot info. Same as today.

### Spoolman-linked, spool-level fields only (remaining %)
`PATCH /api/v1/spool/{id}` with new remaining weight.

### Spoolman-linked, filament-level fields changed (vendor, material, color)
1. Search: `GET /api/v1/filament?vendor.name={v}&material={m}&color_hex={c}` (core fields + diameter)
2. Match found → `PATCH /api/v1/spool/{id}` with matched `filament_id`
3. No match → `POST /api/v1/filament` with new definition, then `PATCH /api/v1/spool/{id}` to re-link
4. Orphaned old filament definitions are left alone (not our cleanup responsibility)

## Spoolman Picker Sub-View

Shown when:
- Slot is empty + Spoolman connected (automatic first view)
- User taps "Change Spool" or "Link to Spoolman"

Reuses existing `AmsSpoolmanPicker` logic (browse/search Spoolman inventory). On selection, auto-fills the edit form and switches to form view. "Manual entry" escape hatch at bottom of picker for users who want to skip Spoolman.

## Scope

- Applies to ALL backends (tool changers, AMS, AFC, Happy Hare)
- The edit modal is backend-agnostic
- Absorbs and removes the standalone `AmsSpoolmanPicker` / `ams_context_spoolman_cb` path

## Approach

- TDD: write failing tests before implementation
- Delegate to sub-agents for parallel work
- Code reviews at logical chunk boundaries
