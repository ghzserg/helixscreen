# Build System Documentation

This document describes the HelixScreen prototype build system, including automatic patch application, multi-display support, and development workflows.

## Build System Overview

The project uses **GNU Make** with:
- Color-coded output for easy visual parsing
- Verbosity control to show/hide full compiler commands
- Automatic dependency checking before builds
- Fail-fast error handling with clear diagnostics
- Parallel build support with output synchronization
- Build timing for performance tracking

### Quick Start

```bash
# Parallel build (use -j explicitly)
make -j8

# Clean parallel build with progress/timing
make build

# Verbose mode (shows full commands)
make V=1

# Dependency checking
make check-deps

# Help (shows all targets and options)
make help

# Apply patches manually (usually automatic)
make apply-patches

# Generate IDE/LSP support
make compile_commands
```

### Build Options

- **`V=1`** - Verbose mode: shows full compiler commands instead of short `[CC]`/`[CXX]` tags
- **`JOBS=N`** - Set parallel job count (default: auto-detects CPU cores)
- **`NO_COLOR=1`** - Disable colored output (useful for CI/CD)
- **`-j<N>`** - Enable parallel builds with N jobs (NOT auto-enabled by default)

### Build Output

The build system uses color-coded tags:

- **`[CC]`** (cyan) - Compiling C sources (LVGL)
- **`[CXX]`** (blue) - Compiling C++ sources (app code)
- **`[FONT]`** (green) - Compiling font assets
- **`[ICON]`** (green) - Compiling icon assets
- **`[LD]`** (magenta) - Linking binary
- **`✓`** (green) - Success messages
- **`✗`** (red) - Error messages
- **`⚠`** (yellow) - Warning messages

### Error Handling

When compilation fails, the build system:
1. Shows the failed file with a red `✗` marker
2. Displays the full compiler command for debugging
3. Exits immediately (fail-fast behavior)

Example:
```
[CXX] src/ui_panel_home.cpp
✗ Compilation failed: src/ui_panel_home.cpp
Command: clang++ -std=c++17 -Wall -Wextra -O2 -g -I. -Iinclude ...
```

## Automatic Patch Application

The build system automatically applies patches to git submodules before compilation.

### How It Works

1. **Patch Storage**: All submodule patches are stored in `../patches/` (relative to submodule in `prototype-ui9/lvgl/`)
2. **Auto-Detection**: Makefile checks if patches are already applied before each build
3. **Idempotent**: Safe to run multiple times - patches are only applied once
4. **Transparent**: No manual intervention needed for normal development

### Patch: LVGL SDL Window Position

**File**: `patches/lvgl_sdl_window_position.patch`

**Purpose**: Adds multi-display support to LVGL 9's SDL driver by reading environment variables.

**Environment Variables**:
- `HELIX_SDL_DISPLAY` - Display number (0, 1, 2...) to center window on
- `HELIX_SDL_XPOS` - X coordinate for exact window position
- `HELIX_SDL_YPOS` - Y coordinate for exact window position

**Application Logic** (in `Makefile`):
```makefile
apply-patches:
	@echo "Checking LVGL patches..."
	@if git -C $(LVGL_DIR) diff --quiet src/drivers/sdl/lv_sdl_window.c; then \
		# File is clean, apply patch
		git -C $(LVGL_DIR) apply ../patches/lvgl_sdl_window_position.patch
	else \
		# File already modified (patch applied)
		echo "✓ LVGL SDL window position patch already applied"
	fi
```

**Status Messages**:
- `✓ Patch applied successfully` - Patch was applied during this build
- `✓ LVGL SDL window position patch already applied` - Patch was already present
- `⚠ Cannot apply patch (already applied or conflicts)` - Manual intervention needed

### Adding New Patches

To add a new submodule patch:

1. **Make changes** in the submodule directory
2. **Generate patch**:
   ```bash
   cd prototype-ui9/lvgl
   git diff > ../patches/my-new-patch.patch
   ```
3. **Update Makefile** to apply the patch in the `apply-patches` target
4. **Document** in `patches/README.md`

## Multi-Display Support (macOS)

The prototype supports multi-monitor development workflows with automatic window positioning.

### Command Line Arguments

```bash
# Display-based positioning (centered)
./build/bin/helix-ui-proto --display 0    # Main display
./build/bin/helix-ui-proto --display 1    # Secondary display
./build/bin/helix-ui-proto -d 2           # Third display (short form)

# Exact pixel coordinates
./build/bin/helix-ui-proto --x-pos 100 --y-pos 200
./build/bin/helix-ui-proto -x 1500 -y -500  # Works with negative Y (display above)

# Combined with other options
./build/bin/helix-ui-proto -d 1 -s small --panel home
```

### Implementation Details

**Flow**:
1. `main.cpp` parses command line arguments
2. Sets environment variables before LVGL initialization:
   ```cpp
   setenv("HELIX_SDL_DISPLAY", "1", 1);  // For --display 1
   // or
   setenv("HELIX_SDL_XPOS", "100", 1);   // For --x-pos 100
   setenv("HELIX_SDL_YPOS", "200", 1);   // For --y-pos 200
   ```
3. LVGL SDL driver reads environment variables during window creation
4. Uses `SDL_GetDisplayBounds()` to query display geometry
5. Calculates center position: `display_x + (display_w - window_w) / 2`
6. Calls `SDL_SetWindowPosition()` after window creation (fixes macOS quirks)

**Source Files**:
- `src/main.cpp` - Argument parsing and environment setup (lines 218-220, 385-401)
- `lvgl/src/drivers/sdl/lv_sdl_window.c` - Window positioning logic (patch)

### Screenshot Script Integration

The `scripts/screenshot.sh` script automatically uses display positioning:

```bash
# Default: opens on display 1 (keeps terminal visible on display 0)
./scripts/screenshot.sh helix-ui-proto output-name panel

# Override display
HELIX_SCREENSHOT_DISPLAY=0 ./scripts/screenshot.sh helix-ui-proto output panel
```

**How it works**:
```bash
# In screenshot.sh
HELIX_SCREENSHOT_DISPLAY=${HELIX_SCREENSHOT_DISPLAY:-1}  # Default to display 1
EXTRA_ARGS="--display $HELIX_SCREENSHOT_DISPLAY $EXTRA_ARGS"
```

This ensures the UI window appears on a different display from the terminal, making it easier to monitor build output and screenshots simultaneously.

## Parallel Compilation

**Important**: Parallel builds are **NOT** enabled by default. Use `-j` flag explicitly.

### Platform Detection

```makefile
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)
    PLATFORM := macOS
else
    # Linux
    NPROC := $(shell nproc 2>/dev/null || echo 4)
    PLATFORM := Linux
endif
```

### Usage

```bash
make -j          # Auto-detect CPU cores and parallelize
make -j8         # Use 8 parallel jobs
make JOBS=4      # Set job count via variable
make build       # Clean parallel build (auto-detects cores)
```

The build system uses `--output-sync=target` to prevent interleaved output during parallel builds.

## Build Targets

### Primary Targets

- **`all`** (default) - Build the main binary with dependency checks
- **`build`** - Clean parallel build with progress and timing
- **`clean`** - Remove all build artifacts
- **`run`** - Build and run the prototype
- **`help`** - Show comprehensive help with all targets and options

### Development Targets

- **`compile_commands`** - Generate `compile_commands.json` for IDE/LSP (requires `bear`)
- **`check-deps`** - Verify all build dependencies are installed
- **`apply-patches`** - Manually apply submodule patches (usually automatic)

### Test Targets

- **`test`** - Run unit tests
- **`test-cards`** - Test dynamic card instantiation
- **`test-print-select`** - Test print select panel with mock data

### Demo Target

- **`demo`** - Build LVGL demo widgets (for LVGL API testing)

## Dependency Checking

Before building, the system automatically checks for required dependencies:

**Required:**
- `clang` / `clang++` - C/C++ compiler with C++17 support
- `sdl2-config` - SDL2 development libraries
- `libhv` - WebSocket client library (symlinked from parent repo)
- `spdlog` - Logging library (symlinked from parent repo)
- LVGL submodule

**Optional:**
- `bear` - For generating `compile_commands.json`
- `imagemagick` - For screenshot conversion

### Manual Dependency Check

```bash
make check-deps
```

Example output:
```
Checking build dependencies...
✓ clang found: Apple clang version 17.0.0
✓ clang++ found: Apple clang version 17.0.0
✓ SDL2 found: 2.32.10
✓ libhv found: libhv/lib/libhv.a
✓ spdlog found: spdlog
✓ LVGL found: lvgl

All dependencies satisfied!
```

If dependencies are missing, the check provides installation instructions.

## Dependency Management

### Git Submodules

The project uses git submodules for external dependencies:

- `lvgl` - LVGL 9.3 graphics library (with patches)
- `libhv` - HTTP/WebSocket client (symlinked from parent repo)
- `spdlog` - Logging library (symlinked from parent repo)

**Important**: Submodule patches are applied automatically by the build system. Never commit changes directly to submodules - always create patches instead.

### SDL2

SDL2 is a system dependency installed via package manager:

```bash
# macOS
brew install sdl2

# Debian/Ubuntu
sudo apt install libsdl2-dev

# Fedora/RHEL
sudo dnf install SDL2-devel
```

The Makefile uses `sdl2-config` to auto-detect paths:
```makefile
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS := $(shell sdl2-config --libs)
```

## Troubleshooting

### Patch Application Fails

**Symptom**: `⚠ Cannot apply patch (already applied or conflicts)`

**Causes**:
1. Submodule was manually modified (expected if patch is working)
2. Patch conflicts with newer LVGL version
3. Patch file is corrupted

**Solutions**:
```bash
# Check if file is modified (expected)
git -C lvgl diff src/drivers/sdl/lv_sdl_window.c

# Revert to original (re-applies patch on next build)
git -C lvgl checkout src/drivers/sdl/lv_sdl_window.c
make apply-patches

# Force re-apply
git -C lvgl checkout src/drivers/sdl/lv_sdl_window.c
git -C lvgl apply ../patches/lvgl_sdl_window_position.patch
```

### Build Performance

**Symptom**: Slow compilation

**Solutions**:
- Use parallel builds: `make -j8` or `make -j$(nproc)`
- Use incremental builds: `make -j8` instead of `make clean && make`
- Check CPU usage during build (should be near 100% with parallel builds)
- Use `make build` for optimized clean builds with timing

### SDL2 Not Found

**Symptom**: `sdl2-config: command not found`

**Solutions**:
```bash
# macOS
brew install sdl2

# Debian/Ubuntu
sudo apt install libsdl2-dev

# Verify installation
which sdl2-config
sdl2-config --version
```

## Best Practices

### Development Workflow

1. **Edit code** in `src/` or `include/`
2. **Run `make -j8`** - parallel incremental build with auto-patching
3. **Test** with `./build/bin/helix-ui-proto`
4. **Screenshot** with `./scripts/screenshot.sh` (auto-opens on display 1)
5. **Commit** with working incremental changes

For debugging build issues:
```bash
make clean
make V=1   # Verbose sequential build
```

### Clean Builds

Only use `make clean && make` when:
- Switching branches with significant changes
- Build artifacts are corrupted
- Troubleshooting mysterious build errors

**Avoid** clean rebuilds for normal development (wastes time).

### Submodule Management

**Never**:
- Commit changes directly to submodules
- Update submodule commits without testing
- Modify submodule files without creating patches

**Always**:
- Create patches for submodule changes
- Document patches in `patches/README.md`
- Test patch application on clean checkouts

## See Also

- [README.md](../README.md) - Project overview and quick start
- [CLAUDE.md](../CLAUDE.md) - Development context and guidelines
- [patches/README.md](../patches/README.md) - Patch documentation
