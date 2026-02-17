# Spoolman Management & Label Printing

**Date**: 2026-02-09
**Status**: Mostly Complete (wizard built and merged; visual tests and Step 9 filamentcolors.xyz pending)
**Branch**: `feature/multi-unit-ams`
**Status doc**: `2026-02-15-spool-wizard-status.md`

## Overview

Full Spoolman CRUD management from the HelixScreen touchscreen UI, plus barcode
label printing and scanning. The goal: get a new spool from Amazon, set it up
end-to-end from HelixScreen, print a label, and start using it — without ever
touching the Spoolman web UI.

## Scope

- Spool + Filament + Vendor CRUD (vendor creation inline, not standalone management)
- "New Spool" wizard that handles the full vendor → filament → spool flow in one go
- Edit and delete existing spools
- QR code label printing to networked label printers
- USB barcode scanner input for quick spool assignment
- Shared/reusable UI components extracted from existing AMS code

## Architecture

### API Layer

All Spoolman communication goes through Moonraker's proxy endpoint
(`server.spoolman.proxy`). The proxy is method-agnostic — it forwards
`request_method`, `path`, and `body` to Spoolman. No direct Spoolman connection
needed.

**New MoonrakerAPI methods required:**

| Method | HTTP | Spoolman Endpoint | Purpose |
|--------|------|-------------------|---------|
| `create_spoolman_vendor()` | POST | `/v1/vendor` | Create new vendor |
| `create_spoolman_filament()` | POST | `/v1/filament` | Create new filament |
| `create_spoolman_spool()` | POST | `/v1/spool` | Create new spool |
| `delete_spoolman_spool()` | DELETE | `/v1/spool/{id}` | Delete a spool |
| `get_spoolman_vendors()` | GET | `/v1/vendor` | List vendors (for wizard) |
| `get_spoolman_filaments()` | GET | `/v1/filament` | List filaments (for wizard) |

Existing methods retained: `get_spoolman_spools()`, `get_spoolman_spool()`,
`set_active_spool()`, `update_spoolman_spool_weight()`,
`update_spoolman_filament_color()`, `get_spoolman_status()`.

All new methods follow the existing callback pattern:
```cpp
void create_spoolman_spool(const json& spool_data,
    std::function<void(const SpoolInfo&)> on_success,
    std::function<void(const MoonrakerError&)> on_error);
```

### Data Types

Extend `spoolman_types.h` with:

```cpp
struct VendorInfo {
    int id = 0;
    std::string name;
    std::string url;  // optional
};

struct FilamentInfo {
    int id = 0;
    int vendor_id = 0;
    std::string material;       // PLA, PETG, ABS, TPU, ASA, etc.
    std::string color_name;
    uint32_t color_hex = 0;
    float density = 0;
    float diameter = 1.75f;
    float weight = 0;           // net weight per spool (g)
    float spool_weight = 0;     // empty spool weight (g)
    int nozzle_temp_min = 0;
    int nozzle_temp_max = 0;
    int bed_temp_min = 0;
    int bed_temp_max = 0;
};
```

`SpoolInfo` already exists and covers what we need for spool-level data.

---

## UI Components

### 1. Shared ContextMenu Component

Extract the existing `AmsContextMenu` into a generic, reusable `ContextMenu`
component. Both the AMS panel and Spoolman panel (and future panels) will use it.

**Pattern:**
- Full-screen semi-transparent backdrop (click to dismiss)
- Card positioned near the triggering widget (intelligent left/right placement)
- Subject-driven button enable/disable states
- Action callback dispatch via enum
- `lv_obj_delete_async()` for safe dismissal during event processing
- Single active instance (static pointer pattern)

**Public API:**
```cpp
class ContextMenu {
public:
    using ActionCallback = std::function<void(int action, int item_index)>;

    bool show_near_widget(lv_obj_t* parent, const char* xml_component,
                          int item_index, lv_obj_t* near_widget);
    void hide();
    bool is_visible() const;
    void set_action_callback(ActionCallback callback);
};
```

The XML component name is passed in so each panel defines its own menu layout
with its own buttons and bindings. The shared class handles positioning,
backdrop, dismissal, and callback plumbing.

`AmsContextMenu` becomes a thin wrapper (or is refactored to use `ContextMenu`
directly).

### 2. New Spool Wizard

A multi-step wizard flow reusing the first-start wizard navigation pattern:
header bar with "Step X of Y" progress, Back/Next buttons, content area that
swaps per step.

**Presented as:** Full-screen overlay (same as first-start wizard).

**Entry points:**
- "+" button in Spoolman panel header bar
- "New Spool" option in AMS slot edit context (when assigning a spool)

#### Step 1 — Vendor

- Searchable/scrollable list of existing vendors fetched from Spoolman
- "New Vendor" card at the top with inline fields:
  - Name (required)
  - URL (optional)
- Tap existing vendor to select → Next
- Fill new vendor fields → Next (vendor created at final commit)

#### Step 2 — Filament

- Filtered to selected vendor's filaments
- If vendor is new (created in step 1), list is empty → goes straight to new
  filament form
- "New Filament" card with fields:
  - Material — dropdown (PLA, PETG, ABS, TPU, ASA, Nylon, PC, PVA, HIPS, CF,
    custom text input)
  - Color — color picker (reuse existing `ColorPicker` component)
  - Nozzle temp range — min/max inputs, auto-filled from material database
  - Bed temp range — min/max inputs, auto-filled from material database
  - Diameter — default 1.75mm
  - Net weight — weight of filament per spool (g)
  - Spool weight — empty spool weight (g), optional
- Tap existing filament to select → Next

#### Step 3 — Spool Details

- Initial weight (g) — pre-filled from filament if available
- Spool weight (g) — pre-filled from filament if available
- Price — optional
- Lot number — optional
- Notes/comment — optional
- **"Create & Set Active"** button (creates spool + sets active in one tap)
- **"Create"** button (just creates)

**Atomicity:** Nothing is created until the user hits Create on the final step.
The wizard batches API calls: create vendor (if new) → create filament (if new)
→ create spool → optionally set active. If any step fails, roll back (delete
what was created) and show an error.

**Navigation:** Wizard-style header bar with step progress indicator (matching
the first-start wizard pattern). Back button on each screen. Progress dots.

**Validation gating:** Next button disabled until required fields are filled.
Uses the same `connection_test_passed` subject pattern (renamed to something
generic like `wizard_step_valid`).

### 3. Spoolman Context Menu

Appears when tapping a spool row on the Spoolman panel.

**Actions:**
- **Set Active** — calls existing `set_active_spool()` API
- **Edit** — opens Spool Edit Modal
- **Print Label** — generates QR label, sends to configured printer
- **Delete** — shows confirmation dialog, then DELETE via proxy

Uses the shared `ContextMenu` component with a `spoolman_context_menu.xml`
layout.

### 4. Spool Edit Modal

Extends `Modal` base class, follows `AmsEditModal` pattern.

```
┌─ Header ────────────────────────────────┐
│  "Edit Spool #42"            [X close]  │
├─────────────────────────────────────────┤
│ ┌─ Content (row) ─────────────────────┐ │
│ │ [3D Spool     ]  Vendor: Hatchbox   │ │
│ │ [ Canvas      ]  Material: PLA      │ │
│ │ [ w/ color    ]  Color: Red         │ │
│ │ [ & fill lvl  ]  ──────────────     │ │
│ │               ]  Remaining (g) [__] │ │
│ │               ]  Spool wt (g)  [__] │ │
│ │               ]  Price ($)     [__] │ │
│ │               ]  Lot Nr        [__] │ │
│ │               ]  Notes         [__] │ │
│ └─────────────────────────────────────┘ │
├─────────────────────────────────────────┤
│ [Reset]              [Save/Close]       │
└─────────────────────────────────────────┘
```

- Left side: 3D spool canvas (reuse from existing Spoolman panel) showing color
  and fill level, updates live as remaining weight changes
- Right side: form fields
- Vendor/Material/Color are **read-only** display (these live on the Filament
  object)
- Editable fields: remaining weight, spool weight, price, lot number, notes
- Dirty detection: Save button text toggles "Save" / "Close"
- PATCH via Moonraker proxy on save
- Reset button restores original values

### 5. Delete Confirmation

Uses existing `ui_modal_show_confirmation()` helper:

```cpp
ui_modal_show_confirmation(
    "Delete Spool?",
    "Spool #42 — Hatchbox PLA Red\nThis cannot be undone.",
    ModalSeverity::Warning, "Delete",
    on_confirm_delete, on_cancel, user_data);
```

On confirm: DELETE via Moonraker proxy → remove from local spool list → refresh
panel.

---

## Label Printing System

### Architecture

Abstraction layer with pluggable printer backends.

```cpp
class LabelPrinter {
public:
    virtual ~LabelPrinter() = default;

    virtual std::string name() const = 0;
    virtual bool connect(const std::string& host, int port) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // Print a pre-rendered bitmap label
    virtual bool print_label(const LabelBitmap& bitmap) = 0;

    // Query supported label sizes
    virtual std::vector<LabelSize> supported_sizes() const = 0;
};

struct LabelSize {
    std::string name;       // "29mm", "62mm", "29x90mm"
    int width_px;
    int height_px;
    int dpi;
};

struct LabelBitmap {
    std::vector<uint8_t> data;  // 1-bit packed, row-major
    int width;
    int height;
    int dpi;
};
```

### Brother QL-820NWB Backend

Reference implementation. The Brother QL printers speak a raster protocol over
TCP port 9100:

1. Connect via TCP socket
2. Send initialization commands
3. Send raster data line-by-line (1-bit monochrome, packed)
4. Send print command
5. Read status response

The protocol is well-documented. We implement it natively in C++ — no external
dependencies needed beyond a TCP socket (which we already have via libhv).

### QR Code Generation

Use [qrcodegen](https://github.com/nayuki/QR-Code-generator) — a tiny,
single-file, public-domain C/C++ library. Header-only, no dependencies.

**QR code content:** `web+spoolman:s-{spool_id}`

This matches Spoolman's default QR format. Their scanner also accepts
`http://{host}/spool/show/{id}`, but the `web+spoolman:` URI scheme is the
standard.

**Error correction level:** H (30%) — required to support the HelixScreen logo
embedded in the center of the QR code.

### Label Layout

Labels are rendered to a `LabelBitmap` in memory at the target printer's DPI.

Layout (horizontal, for standard label tape):
```
┌──────────────────────────────────┐
│  ┌──────┐  Hatchbox PLA         │
│  │ QR   │  Red                   │
│  │ code │  1000g — Spool #42    │
│  │ [HS] │                        │
│  └──────┘                        │
└──────────────────────────────────┘
```

- QR code on the left with HelixScreen logo centered inside
- Text fields on the right: vendor + material, color, weight + spool ID
- Smart layout that adapts to label size
- A few built-in presets rather than freeform template customization:
  - **Standard** — QR + vendor, material, color, weight, ID
  - **Compact** — QR + material + color only (for small labels)
  - **Minimal** — QR code only

### Label Printer Settings

New section under **Settings → System → Label Printer**:

- **Printer Type** — dropdown: Brother QL-820NWB (more backends in the future)
- **Printer Address** — text input for IP/hostname
- **Port** — default 9100, editable
- **Label Size** — dropdown populated from backend's `supported_sizes()`
- **Label Preset** — Standard / Compact / Minimal
- **Test Print** button — prints a test label with sample data

Settings stored via `SettingsManager`, persisted to config.

---

## USB Barcode Scanner Input

### How It Works

USB barcode scanners present as HID keyboard devices. They "type" the decoded
string followed by Enter. On the Pi:

1. **Device detection**: Monitor `/dev/input/` for HID devices. Identify barcode
   scanners by vendor/product ID (configurable in settings) or auto-detect
   devices that type rapidly (scanners type entire strings in milliseconds,
   humans don't).
2. **Input buffering**: Capture characters into a buffer. On Enter (or timeout),
   attempt to parse the buffer as a Spoolman QR code.
3. **QR code parsing**: Match against both Spoolman formats:
   - `web+spoolman:s-{id}` (case-insensitive)
   - `http(s)://{host}/spool/show/{id}`
4. **Spool lookup**: On successful parse, call `get_spoolman_spool(id)` to fetch
   full spool details.

### UX Behavior

**Context-aware auto-population:**
- If the user is on the **AMS panel editing a slot** → assign the scanned spool
  to that slot automatically
- If the user is on the **Spoolman panel** → highlight/select the scanned spool
- If the user is **anywhere else** → show a toast notification:
  "Scanned: Hatchbox PLA Red (#42)" with actions: "Set Active" / "Dismiss"

### Scanner Settings

Under **Settings → System → Barcode Scanner**:

- **Enable/Disable** toggle
- **Scanner Device** — auto-detected or manual device path selection
- **Auto-detect mode** — use rapid-input heuristic (type a full string in
  <100ms = scanner, not human)

---

## Implementation Plan

Work will be done in a **worktree branch** with **TDD** (test-first) and
**frequent code reviews** at each logical chunk. Sub-agents handle
implementation; main session orchestrates.

### Phase 1: Foundation

1. **New Moonraker API methods** — POST/DELETE/GET for vendors, filaments, spools
   - Tests: mock responses, error handling, JSON parsing
2. **New data types** — `VendorInfo`, `FilamentInfo` structs
   - Tests: serialization, validation
3. **Shared ContextMenu component** — extract from AmsContextMenu
   - Tests: positioning logic, show/hide lifecycle
   - Review checkpoint

### Phase 2: CRUD UI

4. **Spoolman context menu** — new XML + wiring on Spoolman panel
   - Tests: action dispatch, visibility states
5. **Spool Edit Modal** — edit overlay with form fields
   - Tests: dirty detection, save/reset, field validation
6. **Delete confirmation** — wire up existing confirmation dialog
   - Tests: confirm/cancel flow, API call
   - Review checkpoint

### Phase 3: New Spool Wizard ✅ (merged into `feature/multi-unit-ams`)

7. **Wizard framework** — reuse wizard navigation pattern for new context ✅
   - Tests: step navigation, back/forward, skip logic
8. **Step 1: Vendor selection/creation** ✅
   - Tests: search, new vendor validation
9. **Step 2: Filament selection/creation** ✅
   - Tests: filtering by vendor, new filament validation, color picker
   - Pending: filamentcolors.xyz color enhancement (Step 9)
10. **Step 3: Spool details + atomic creation** ✅
    - Tests: batch API calls, rollback on failure, set-active option
    - Visual tests pending (see status doc)

### Phase 4: Label Printing

11. **QR code generation** — integrate qrcodegen, render with logo overlay
    - Tests: QR content format, bitmap rendering, logo placement
12. **Label layout engine** — render label bitmap with text + QR
    - Tests: layout presets, different label sizes, text truncation
13. **LabelPrinter abstraction + Brother QL backend**
    - Tests: protocol encoding, connection handling, mock printer
14. **Label printer settings UI**
    - Tests: settings persistence, validation
    - Review checkpoint

### Phase 5: Barcode Scanner

15. **HID input listener** — device detection, input buffering
    - Tests: buffer parsing, QR format matching, timeout handling
16. **Context-aware auto-population** — panel detection, spool assignment
    - Tests: panel state detection, assignment flow
17. **Scanner settings UI**
    - Review checkpoint

### Phase 6: Polish & Integration

18. **Entry points** — "+" button on Spoolman panel, AMS slot "New Spool" option
19. **Error handling** — Spoolman offline states, network failures, timeouts
20. **End-to-end testing** — full workflows: create → print label → scan → assign
    - Final review

---

## Dependencies

**New external code:**
- `qrcodegen` (C/C++) — QR code generation, public domain, single file

**Existing components reused:**
- `Modal` base class — for edit modal
- `ColorPicker` — for filament color selection in wizard
- `ui_modal_show_confirmation()` — for delete confirmation
- First-start wizard navigation pattern — for new spool wizard
- 3D spool canvas — for edit modal preview
- `ContextMenu` (extracted from `AmsContextMenu`) — for both panels
- Moonraker proxy pattern — for all new API methods
- `SettingsManager` — for printer/scanner settings

**No new system dependencies.** Brother QL protocol is implemented over TCP
sockets (libhv). Scanner uses Linux input subsystem (evdev).

---

## Open Questions

None — all design decisions resolved during brainstorming session.

## Future Enhancements

- **Phomemo M110 backend** — second label printer backend
- **Webcam QR scanning** — use printer's camera for QR code scanning
- **Vendor management panel** — standalone vendor CRUD (currently inline only)
- **Filament usage history** — `get_spool_usage_history()` stub already exists
- **Freeform label templates** — if users want more control than presets
