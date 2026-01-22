# Material Database Consolidation

## Overview

Consolidate scattered material type definitions into a single source of truth in `filament_database.h`, enabling material compatibility validation for endless spool and consistent temperature/drying data across the UI.

## Problem

Material data is duplicated across 4+ files:
- `filament_database.h` - 35 materials with temps (primary)
- `ams_types.h` - MaterialGroup enum, drying presets (duplicate)
- `ui_panel_filament.cpp` - hardcoded preset arrays (duplicate)
- `dryer_presets_modal.xml` - hardcoded buttons (duplicate)

This causes drift, makes adding materials tedious, and blocks endless spool material compatibility validation.

## Solution

Extend `filament_database.h` as the single source of truth with all thermal parameters, drying info, physical properties, and compatibility grouping.

---

## Data Model

### MaterialInfo Struct

```cpp
struct MaterialInfo {
    const char* name;

    // Print temperatures (°C)
    int nozzle_min;             // Minimum nozzle temp
    int nozzle_max;             // Maximum nozzle temp
    int bed_temp;               // Recommended bed temp
    int chamber_temp_c;         // Recommended chamber temp (0 = none/open)

    // Drying parameters
    int dry_temp_c;             // Drying temperature (0 = not hygroscopic)
    int dry_time_min;           // Drying duration in minutes

    // Physical properties
    float density_g_cm3;        // Material density (g/cm³)

    // Classification
    const char* category;       // "Standard", "Engineering", etc.
    const char* compat_group;   // "PLA", "PETG", "ABS_ASA", etc.

    // Helpers
    constexpr int nozzle_recommended() const { return (nozzle_min + nozzle_max) / 2; }
    constexpr bool needs_enclosure() const { return chamber_temp_c > 0; }
    constexpr bool needs_drying() const { return dry_temp_c > 0; }
};
```

### Compatibility Groups (7)

| Group | Materials |
|-------|-----------|
| PLA | PLA, PLA+, PLA-CF, PLA-GF, Silk PLA, Matte PLA, Wood PLA, etc. |
| PETG | PETG, PETG-CF, PETG-GF, PCTG |
| ABS_ASA | ABS, ABS-CF, ABS-GF, ASA, ASA-CF, ASA-GF |
| PA | PA, PA6, PA12, PA66, PA-CF, PA-GF, PPA |
| TPU | TPU, TPU-85A, TPU-95A, TPE |
| PC | PC, PC-CF, PC-GF, PC-ABS |
| HIGH_TEMP | PEEK, PEI, PSU, PPSU, PPS |

Unknown materials → compatible with everything (safe default).

### Material Aliases

```cpp
struct MaterialAlias {
    const char* alias;
    const char* canonical;
};

// Examples:
{"Nylon", "PA"}, {"Nylon-CF", "PA-CF"}, {"Polycarbonate", "PC"},
{"PLA Silk", "Silk PLA"}, {"Generic", "PLA"}
```

### Reference Data

**Chamber temperatures:**
| Material | Chamber (°C) |
|----------|-------------|
| PLA, PETG | 0 (open) |
| ABS, ASA | 45-50 |
| PA (Nylon) | 45-50 |
| PC | 50-60 |
| PEEK, PEI | 80-120 |

**Densities:**
| Material | Density (g/cm³) |
|----------|-----------------|
| PLA | 1.24 |
| PETG | 1.27 |
| ABS | 1.04 |
| ASA | 1.07 |
| PA | 1.14 |
| TPU | 1.21 |
| PC | 1.20 |
| PEEK | 1.30 |

---

## API

```cpp
namespace filament {

// Lookup (resolves aliases, case-insensitive)
std::optional<MaterialInfo> find_material(std::string_view name);
std::string_view resolve_alias(std::string_view name);

// Compatibility
bool are_materials_compatible(std::string_view mat1, std::string_view mat2);
const char* get_compatibility_group(std::string_view material);

// Drying presets (one per compat_group, for dropdown)
struct DryingPreset { const char* name; int temp_c; int time_min; };
std::vector<DryingPreset> get_drying_presets_by_group();

// Calculations
float weight_to_length_m(float weight_g, float density, float diameter_mm = 1.75f);

}
```

---

## Phases

### Phase 0: Extend filament_database.h
- Add new fields to MaterialInfo (chamber_temp, dry_temp, dry_time, density, compat_group)
- Update existing 35 materials with new data
- Add MaterialAlias system
- Add helper functions

**Verification:** Unit tests for lookup, alias resolution, compatibility

### Phase 1: Add new materials
- Add ~15 new materials (composites, nylon variants, TPU variants, recycled)
- Ensure all compat_groups have representative materials

**Verification:** All materials have valid data, no gaps in groups

### Phase 2: Refactor ams_types.h
- Remove MaterialGroup enum
- Remove get_material_group(), are_materials_compatible(), get_default_drying_presets()
- Add thin wrappers that delegate to filament namespace (for API compatibility if needed)

**Verification:** Build succeeds, existing tests pass

### Phase 3: Endless spool material validation
- Add validation in ui_ams_context_menu.cpp handle_backup_changed()
- Show toast error for incompatible materials
- Mark incompatible slots in backup dropdown

**Verification:** Manual test - try setting PLA backup for PETG slot, should be blocked

### Phase 4: Dryer presets dropdown
- Replace buttons in dryer_presets_modal.xml with dropdown
- Populate from filament::get_drying_presets_by_group()
- Auto-fill temp/duration on selection

**Verification:** Dropdown shows all groups, selection populates fields

### Phase 5: Refactor ui_panel_filament.cpp
- Remove hardcoded MATERIAL_* arrays
- Use filament::find_material() for temperatures

**Verification:** Quick preset buttons still work correctly

---

## Progress

- [x] Phase 0: Extend filament_database.h - `24d411ff`
- [x] Phase 1: Add new materials - `b51b59bd`
- [x] Phase 2: Refactor ams_types.h - `f4d354f7`
- [x] Phase 3: Endless spool material validation - `3f58aad3`
- [x] Phase 4: Dryer presets dropdown - `e9320a51`
- [x] Phase 5: Refactor ui_panel_filament.cpp - `3a608f98`

---

## Key Files

| File | Role |
|------|------|
| `include/filament_database.h` | Single source of truth - all material data |
| `include/ams_types.h` | Remove duplicates, delegate to filament |
| `src/ui/ui_ams_context_menu.cpp` | Add material validation |
| `src/ui/ui_panel_filament.cpp` | Use database for presets |
| `ui_xml/dryer_presets_modal.xml` | Buttons → dropdown |
| `tests/unit/test_filament_database.cpp` | NEW: Database tests |

---

## Decisions

1. **Compile-time database** - No config file needed for now
2. **Support aliases** - "Nylon" → "PA", etc.
3. **Dryer presets: dropdown** - Replace buttons with material type dropdown
4. **Unknown = compatible** - Safe default for unrecognized materials
