# Experimental Code

This directory contains work-in-progress and experimental features that are **not part of the main build**.

## Contents

### G-code Rendering Experiments

#### SDF (Signed Distance Field) Renderer
- **Files**: `gcode_sdf_builder.{h,cpp}`, `gcode_sparse_grid.cpp`, `marching_cubes_tables.h`
- **Purpose**: Alternative 3D rendering approach using signed distance fields and marching cubes
- **Status**: Incomplete - requires `feature_type` field in `ToolpathSegment` structure
- **Benefits**: Potentially smoother surface reconstruction, better handling of layer boundaries

#### Tube Renderer
- **Files**: `gcode_tube_renderer.{h,cpp}`
- **Purpose**: Render G-code toolpaths as tubes with circular cross-sections
- **Status**: Incomplete - requires `feature_type` field in `ToolpathSegment` structure
- **Benefits**: More realistic visualization of actual filament deposition

### Test Files
- `test_gcode_*.cpp` - Various G-code parsing and rendering tests
- `test_sdf_reconstruction.cpp` - SDF mesh reconstruction validation
- `test_sparse_grid.cpp` - Sparse grid data structure tests
- `test_tinygl_triangle.cpp` - TinyGL triangle rasterization tests
- `test_dynamic_cards.cpp` - UI card system tests
- `test_responsive_theme.cpp` - Responsive theming tests

## Integration Notes

These files are **excluded from the build** because they depend on data structures that are not currently in the committed codebase. To integrate them:

1. Add `feature_type: std::string` field to `gcode::ToolpathSegment` structure
2. Update G-code parser to populate feature types from slicer comments
3. Add corresponding headers to `include/` and update build system
4. Add render mode selection to UI

## History

Created: 2025-11-19
Moved from `/tmp` to preserve work on alternative rendering approaches during investigation of layer line artifacts in TinyGL renderer.
