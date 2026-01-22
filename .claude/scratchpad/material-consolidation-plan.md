# Material Types Consolidation Plan

## Current State

**Primary source** (already exists): `include/filament_database.h`
- 35 materials with temperatures and categories
- Categories: Standard, Engineering, Flexible, Support, Specialty, High-Temp

**Duplicated/scattered data**:
| Location | Data | Issue |
|----------|------|-------|
| `ams_types.h` lines 760-769 | Drying presets (6 materials) | Hardcoded, incomplete |
| `ams_types.h` lines 897-1011 | `MaterialGroup` enum + compatibility | Parallel to categories |
| `ui_panel_filament.cpp` lines 35-38 | Quick preset temps (4 materials) | Hardcoded subset |
| `ui_xml/dryer_presets_modal.xml` | Dryer buttons (3 materials) | Hardcoded in XML |

## Proposed Consolidation

### Phase 1: Extend `filament_database.h` as Single Source of Truth

Add new fields to `MaterialInfo`:

```cpp
struct MaterialInfo {
    const char* name;
    int nozzle_min;
    int nozzle_max;
    int bed_temp;
    const char* category;

    // NEW: Drying parameters
    int dry_temp_c;        // Drying temperature (0 = not hygroscopic)
    int dry_time_min;      // Drying duration in minutes

    // NEW: Compatibility group for endless spool
    const char* compat_group;  // "PLA", "PETG", "ABS_ASA", "TPU", "PA", "PC", etc.
};
```

Updated database structure:
```cpp
//                Name          Nozzle    Bed   Category       Dry    CompatGroup
{"PLA",         190, 220,  60, "Standard",      45, 240, "PLA"},
{"PLA+",        200, 230,  60, "Standard",      45, 240, "PLA"},
{"PETG",        230, 260,  80, "Standard",      55, 360, "PETG"},
{"ABS",         240, 270, 100, "Engineering",   65, 360, "ABS_ASA"},
{"ASA",         240, 270, 100, "Engineering",   65, 360, "ABS_ASA"},
{"PA",          250, 280,  80, "Engineering",   70, 480, "PA"},
{"TPU",         210, 240,  50, "Flexible",      50, 300, "TPU"},
...
```

### Phase 2: Add Missing Materials

User mentioned we need more modern materials. Add:

```cpp
// === Additional Standard ===
{"PCTG",        240, 260,  70, "Standard",      55, 360, "PETG"},  // PETG alternative

// === Additional Engineering ===
{"ABS-GF",      250, 280, 100, "Engineering",   65, 360, "ABS_ASA"},
{"ASA-GF",      250, 280, 100, "Engineering",   65, 360, "ABS_ASA"},
{"PC-GF",       270, 300, 110, "Engineering",   70, 480, "PC"},
{"PA66",        260, 290,  90, "Engineering",   70, 480, "PA"},
{"PA66-GF",     270, 300,  90, "Engineering",   70, 480, "PA"},

// === Composites / Exotic ===
{"CF-Nylon",    260, 290,  80, "Engineering",   70, 480, "PA"},   // Generic CF nylon
{"GF-Nylon",    260, 290,  80, "Engineering",   70, 480, "PA"},
{"PPA",         290, 320, 100, "High-Temp",     80, 480, "PA"},   // Polyphthalamide
{"PPS",         290, 320, 120, "High-Temp",     80, 360, "PPS"},  // Polyphenylene sulfide

// === Flexible variants ===
{"TPU-95A",     210, 240,  50, "Flexible",      50, 300, "TPU"},
{"TPU-85A",     200, 230,  45, "Flexible",      50, 300, "TPU"},

// === Modern specialty ===
{"HTN PLA",     200, 230,  60, "Standard",      45, 240, "PLA"},  // High-temp PLA
{"r-PETG",      230, 260,  80, "Standard",      55, 360, "PETG"}, // Recycled
{"r-PLA",       190, 220,  60, "Standard",      45, 240, "PLA"},  // Recycled
```

### Phase 3: Add Material Aliases

Common alternative names that should resolve to canonical materials:

```cpp
struct MaterialAlias {
    const char* alias;
    const char* canonical;
};

inline constexpr MaterialAlias MATERIAL_ALIASES[] = {
    {"Nylon",      "PA"},
    {"Nylon-CF",   "PA-CF"},
    {"Nylon-GF",   "PA-GF"},
    {"CF-Nylon",   "PA-CF"},
    {"GF-Nylon",   "PA-GF"},
    {"Polycarbonate", "PC"},
    {"PCTG",       "PETG"},      // Close enough for temps/drying
    {"PLA Silk",   "Silk PLA"},
    {"PLA Matte",  "Matte PLA"},
    {"PLA Wood",   "Wood PLA"},
    {"Generic",    "PLA"},       // Safe default
};
```

The `find_material()` function checks aliases if exact match not found.

### Phase 4: Add Helper Functions

```cpp
// Resolve alias to canonical name (returns input if not an alias)
inline std::string_view resolve_alias(std::string_view material);

// Get drying preset for a material (resolves aliases)
inline std::optional<DryingPreset> get_drying_preset(std::string_view material);

// Get unique drying presets by compat_group (for dropdown menu)
inline std::vector<DryingPreset> get_drying_presets_by_group();

// Check material compatibility using compat_group
inline bool are_materials_compatible(std::string_view mat1, std::string_view mat2);

// Get compatibility group for a material (resolves aliases)
inline const char* get_compatibility_group(std::string_view material);
```

### Phase 5: Refactor Consumers

1. **`ams_types.h`**:
   - Remove `MaterialGroup` enum (use string-based compat_group)
   - Remove `get_material_group()` function
   - Change `are_materials_compatible()` to call `filament::are_materials_compatible()`
   - Change `get_default_drying_presets()` to call `filament::get_drying_presets_by_group()`

2. **`ui_panel_filament.cpp`**:
   - Remove hardcoded `MATERIAL_NOZZLE_TEMPS[]`, `MATERIAL_BED_TEMPS[]`, `MATERIAL_NAMES[]`
   - Use `filament::find_material()` to get temperatures

3. **Dryer presets UI** (buttons → dropdown):
   - Remove `ui_xml/dryer_presets_modal.xml` preset buttons
   - Add dropdown populated from `filament::get_drying_presets_by_group()`
   - Dropdown shows: "PLA (45°C, 4h)", "PETG (55°C, 6h)", "ABS/ASA (65°C, 6h)", etc.
   - Selection auto-fills temp/duration fields

---

## Files Changed

| File | Change |
|------|--------|
| `include/filament_database.h` | Add dry_temp, dry_time, compat_group; add aliases; add materials; add helpers |
| `include/ams_types.h` | Remove MaterialGroup enum, DryingPreset, redirect to filament_database |
| `src/ui/ui_panel_filament.cpp` | Use database instead of hardcoded arrays |
| `src/ui/ui_ams_context_menu.cpp` | Add material validation for endless spool backup |
| `ui_xml/dryer_presets_modal.xml` | Replace preset buttons with material dropdown |
| `tests/unit/test_ams_endless_spool.cpp` | Update tests for new API |
| `tests/unit/test_filament_database.cpp` | NEW: Tests for aliases, compatibility, drying presets |

---

## Benefits

1. **Single source of truth** - One place to add/edit materials
2. **Consistent temperatures** - No more drift between UI and logic
3. **Extensible** - Easy to add new materials
4. **Testable** - Database can have unit tests
5. **Configurable** - Could later load from JSON/config file

---

## Decisions

1. **Compile-time database sufficient** - No config file needed for now
2. **Support aliases** - "Nylon" → "PA", etc.
3. **Dryer presets: dropdown menu** - Replace buttons with material type dropdown

---

## Integration with AMS Tool Mapping/Endless Spool Fixes

This consolidation enables the remaining fixes from the original plan:

### Already Done (from previous session)
- [x] Fix 1: Mock backend emits EVENT_STATE_CHANGED after tool/backup changes
- [x] Fix 2: Clear old tool mapping when reassigning (prevents duplicates)
- [x] Tests for event emission and duplicate clearing

### Remaining (uses consolidated material database)
- [ ] **Fix 3b: Material validation in endless spool context menu**
  - Use `filament::are_materials_compatible(primary.material, backup.material)`
  - Block incompatible materials with toast error

- [ ] **Fix 3c: Show compatibility in backup dropdown**
  - Mark incompatible slots with warning indicator
  - Format: "Slot 2 - PETG" vs "⚠ Slot 3 - ABS (incompatible)"
