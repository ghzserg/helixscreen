# Submodule Patches

This directory contains patches that modify git submodules. Patches are automatically applied by the build system before compilation.

## Why Patches?

Git submodules should not have direct commits to maintain clean version control. Instead, we apply patches during the build process.

## Patch: lvgl_sdl_window_position.patch

**File**: `lvgl_sdl_window_position.patch`
**Applies to**: `lvgl` submodule (LVGL 9.3.0)
**Modified file**: `lvgl/src/drivers/sdl/lv_sdl_window.c`

### Purpose

Adds multi-display support to LVGL 9's SDL driver by reading environment variables to control window positioning.

### Features

- **Display-based positioning**: Center window on specific display by number
- **Coordinate-based positioning**: Position window at exact pixel coordinates
- **macOS multi-monitor support**: Properly handles displays with different resolutions

### Environment Variables

The SDL driver reads these environment variables at window creation time:

- **`HELIX_SDL_DISPLAY`** - Display number (0, 1, 2...) to center window on
  - Uses `SDL_GetDisplayBounds()` to query display geometry
  - Calculates center position automatically
  - Example: `HELIX_SDL_DISPLAY=1` opens on secondary display

- **`HELIX_SDL_XPOS`** - X coordinate for exact window position (pixels)
  - Overrides display-based positioning if set
  - Example: `HELIX_SDL_XPOS=100`

- **`HELIX_SDL_YPOS`** - Y coordinate for exact window position (pixels)
  - Used with `HELIX_SDL_XPOS` for precise placement
  - Example: `HELIX_SDL_YPOS=200`

### Implementation Details

The patch modifies the SDL window creation logic to:

1. Read environment variables before creating the window
2. Calculate center position using `SDL_GetDisplayBounds()` if `HELIX_SDL_DISPLAY` is set
3. Use explicit coordinates if `HELIX_SDL_XPOS`/`HELIX_SDL_YPOS` are set
4. Call `SDL_SetWindowPosition()` after window creation (critical for macOS)

### Usage

Environment variables are set by `main.cpp` based on command line arguments:

```bash
# Display-based (centered)
./build/bin/helix-ui-proto --display 1

# Coordinate-based
./build/bin/helix-ui-proto --x-pos 100 --y-pos 200

# Both work via environment variables:
HELIX_SDL_DISPLAY=1 ./build/bin/helix-ui-proto
```

### Automatic Application

The patch is automatically applied by the Makefile before building:

```makefile
apply-patches:
	@if git -C lvgl diff --quiet src/drivers/sdl/lv_sdl_window.c; then \
		git -C lvgl apply ../patches/lvgl_sdl_window_position.patch; \
	fi
```

The check is idempotent - it only applies if the file hasn't been modified.

## Manual Patch Application

If you need to manually apply or revert patches:

```bash
# Apply patch
git -C lvgl apply ../patches/lvgl_sdl_window_position.patch

# Check if already applied
git -C lvgl diff src/drivers/sdl/lv_sdl_window.c

# Revert to clean state
git -C lvgl checkout src/drivers/sdl/lv_sdl_window.c

# Re-generate patch (after making changes)
git -C lvgl diff src/drivers/sdl/lv_sdl_window.c > patches/lvgl_sdl_window_position.patch
```

## Adding New Patches

To add a new submodule patch:

1. **Make changes** in the submodule directory
2. **Test** that changes work as expected
3. **Generate patch**:
   ```bash
   git -C <submodule> diff <file> > patches/<patch-name>.patch
   ```
4. **Update Makefile** to apply patch in `apply-patches` target
5. **Document** here in this README
6. **Test** on clean checkout to ensure patch applies correctly

## Related Documentation

- [BUILD_SYSTEM.md](../docs/BUILD_SYSTEM.md) - Build system and patch automation
- [CLAUDE.md](../CLAUDE.md) - Multi-display support documentation
