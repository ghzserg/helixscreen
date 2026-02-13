# Changelog

All notable changes to HelixScreen will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.16] - 2026-02-12

### Added
- Printer Manager overlay accessible from home screen with tap-to-open, custom printer images, inline name editing, and capability chips
- Theme-aware markdown viewer
- Custom printer image selection with import support and list+preview layout

### Fixed
- Setup wizard now defaults IP to 127.0.0.1 for local Moonraker connections
- Whitespace in IP and port input fields no longer causes validation errors

### Changed
- All modals standardized to use the Modal system with ui_dialog
- AMS modals refactored to use modal_button_row component
- Release assets now include install.sh (thanks @Major_Buzzkill)
- Markdown submodule updated with faux bold fix

## [0.9.15] - 2026-02-12

### Fixed
- Touchscreen calibration wizard no longer appears on capacitive displays (#40)
- Calibration verify step now applies new calibration so accept/retry buttons are tappable
- Debug logging via HELIX_DEBUG=1 in env file now works correctly after sourcing order fix
- Release pipeline R2 upload failing when changelog contains special characters
- Symbol resolution script using wrong domain (releases.helixscreen.com → .org)
- User docs referencing `--help | head -1` instead of `--version` for version checks

## [0.9.14] - 2026-02-12

### Fixed
- Installer fails on systems without hexdump (e.g., Armbian) with "Cannot read binary header" error

## [0.9.13] - 2026-02-11

### Added
- Frequency response charts for input shaper calibration with shaper overlay toggles
- CSV parser for Klipper calibration frequency response data
- Filament usage tracking with live consumption during printing and slicer estimates on completion modal
- Unified error modal with declarative subjects and single suppression
- Ultrawide home panel layout for 1920x480 displays
- Internationalization support for header bar and overlay panel titles
- Demo mode for PID and input shaper calibration screenshots
- Klipper/Moonraker pre-flight check in AD5M and K1 installers

### Fixed
- getcwd errors during AD5M startup (#36)
- Installer permission denied on tar extraction cleanup (#34)
- Print tune panel layout adjusted to fit 800x480 screens
- CLI hyphen normalization for layout names

### Changed
- Input shaper graduated from beta to stable
- Width-aware Bresenham line drawing for G-code layer renderer
- Overlay content padding standardized across panels
- Action button widths use percentages instead of hardcoded pixels

## [0.9.12] - 2026-02-11

### Added
- QIDI printer support with detection heuristics and print start profile
- Snapmaker U1 cross-compile target, printer detection, and platform support
- Layout manager with auto-detection for alternative screen sizes and CLI override
- Input shaper panel redesigned with config display, pre-flight checks, per-axis results, and save button
- PID calibration: live temperature graph, progress tracking, old value deltas, abort support, and 15-minute timeout
- Multi-LED chip selection in settings replacing single dropdown
- Macro browser (gated behind beta features)

### Fixed
- Crash on shutdown from re-entrant Moonraker method callback map destruction
- Installer: BusyBox echo compatibility for ANSI colors and temp directory auto-detection
- Missing translations for telemetry, sound, PID, and timelapse strings
- Unwanted borders on navigation bar and home status card buttons
- Scroll-on-focus in plugin install modal
- Beta feature flag conflict hiding hardware check rows in advanced settings

### Changed
- PID calibration ungated from beta features — now available to all users
- Moonraker API abstraction boundary enforced — UI no longer accesses WebSocket client directly
- Test-only methods moved to friend test access pattern (cleaner production API)

## [0.9.11] - 2026-02-10

Sound system, KIAUH installer, display rotation, PID tuning, and timelapse support — plus a splash screen fix for AD5M.

### Added
- Sound system with multi-backend synthesizer engine (SDL audio, PWM sysfs, M300 G-code), JSON sound themes (minimal, retro chiptune), toggle sounds, and theme preview
- Sound settings overlay with volume slider and test beep on release
- KIAUH installer integration for one-click install from KIAUH menu
- Display rotation support for all three binaries (main, splash, watchdog)
- PID tuning calibration with fan control and material presets
- Timelapse plugin detection, install wizard, and settings UI (beta)
- Versioned config migration system
- Shadow support and consistent borders across widgets
- Platform-aware cache directory resolution for embedded targets
- Telemetry analytics pipeline with admin API, pull script, and analyzer

### Fixed
- Splash process not killed on AD5M when pre-started by init script (display flashing)
- Layer count tracking with G-code response fallback
- Print file list 15-second polling fallback for missed WebSocket updates
- Display wake from sleep on SDL click
- Translation sync with extractor false-positive cleanup
- Cross-compiled binaries now auto-stripped after linking
- Build system tracks lv_conf.h as LVGL compile prerequisite
- LayerTracker debug log spam reduced to change-only logging

### Changed
- Sound system and timelapse gated behind beta features flag
- Bug report and feature request GitHub issue templates added

## [0.9.10] - 2026-02-10

Hotfix release — gradient and flag images were broken for all users due to a missing decoder setting, and WiFi initialization caused a 5-second startup delay on NetworkManager-based systems.

### Added
- Optional bed warming step before Z-offset calibration
- Reusable multi-select checkbox widget
- Symbol maps for crash backtrace resolution
- KIAUH extension discovery tests

### Fixed
- Gradient and flag images failing to load (LV_BIN_DECODER_RAM_LOAD not enabled)
- WiFi backend now tries NetworkManager first, avoiding 5-second wpa_supplicant timeout on most systems
- Observer crash on shutdown from subject lifetime mismatch
- Connection wizard mDNS section hidden, subtitle improved

### Changed
- Project permission settings organized into .claude/settings.json

## [0.9.9] - 2026-02-09

Telemetry, security hardening, and a bundled uninstaller — plus deploy packages are now ~60% smaller.

### Added
- Anonymous opt-in telemetry with crash reporting, session recording, and auto-send scheduler
- Hardware survey enrichment for telemetry sessions (schema v2)
- Telemetry opt-in step in setup wizard with info modal
- Cloudflare Worker telemetry backend
- Bundled uninstaller with 151 shell tests
- Creality K2 added to GitHub release workflow

### Fixed
- Framebuffer garbage on home panel from missing container background
- Observer crash on quit from subject/display deinit ordering
- Stale subject pointers in ToastManager and WizardTouchCalibration on shutdown
- Print thumbnail offset and outcome overlay centering
- Confetti particle system rewritten to use native LVGL objects
- Print card thumbnail overlap — e-stop relocated to print card
- Auto-navigation to print status suppressed during setup wizard
- KIAUH extension discovery uses native import paths (fixes #30)
- Data root auto-detected from binary path with missing globals.xml abort
- NaN/Inf guards on all G-code generation paths
- Safe restart via absolute argv[0] path resolution
- Replaced system() with fork/execvp in ping_host()
- Tightened directory permissions, replaced strcpy with memcpy
- K2 musl cross-compilation LDFLAGS
- Telemetry opt-in enforced for crash events
- Telemetry enabled state synced at startup with API key auth

### Changed
- Deploy footprint reduced ~60% with asset excludes and LZ4 image compression
- Shell test gate added to release workflow

## [0.9.8] - 2026-02-09

### Added
- G-code toolpath render uses AMS/Spoolman filament colors for accurate color previews
- Reprint button shown for all terminal print states (error, cancelled, complete)
- Config symlinked into printer_data for editing via Mainsail/Fluidd file manager
- Async button timeout guard to prevent stuck UI on failed operations
- 35 new translation strings synced across all languages

### Fixed
- Slicer time estimate preserved across reprints instead of resetting to zero
- Install directory ownership for Moonraker update manager (fixes #29)
- Python 3.9 compatibility for Sonic Pad KIAUH integration (fixes #28)
- Display sleep using software overlay for unrecognized display hardware (#23)
- Z-offset controls compacted for small displays (#27)
- Print error state handled with badge, reprint button, and automatic heater shutoff
- WebSocket callbacks deferred to main thread preventing UI race conditions
- Responsive breakpoints based on screen height instead of max dimension
- Cooldown button uses TURN_OFF_HEATERS for reliable heater shutoff
- Splash screen support for ultra-wide displays
- 32-bit userspace detection on 64-bit Pi kernels
- Graph Y-axis label no longer clips top padding
- Print card info column taps now navigate to status screen
- Watchdog double-instance prevented on supervised restart
- Internal splash skipped when external splash process is running
- Resolution auto-detection enabled at startup

### Changed
- Z-offset scale layout dynamically adapts to measured label widths
- Filament panel temperature updates are targeted instead of full-refresh
- Machine limits G-code debounced to reduce unnecessary sends
- Delete button on print detail uses danger styling

## [0.9.7] - 2026-02-08

Z-offset calibration redesigned from scratch with a Prusa-style visual meter,
plus display reliability fixes and hardware detection improvements.

### Added
- Z-offset calibration overhaul: Prusa-style vertical meter with draw-in arrow animation, horizontal step buttons, auto-ranging scale, saved offset display, and auto-navigation when calibration is in progress
- Z-offset calibration strategy system for printer-specific save commands
- Automatic update notifications with dismiss support
- Sleep While Printing toggle to keep display on during prints
- Hardware detection: mainboard identification heuristic, non-printer addon exclusion, and kinematics filtering
- Calibration features gated behind beta feature flag

### Fixed
- Crash from rapid filament load/unload button presses
- Crash dialog not initializing touch calibration config
- Keyboard shortcuts firing when typing in text inputs
- Parent directory (..) not always sorted first in file browser
- Splash screen crash when prerendered assets missing
- Console bleed-through on fbdev displays
- Display not repainting fully after wake from sleep
- Moonraker updates switched from git_repo to zip type for reliability
- Thumbnail format forced to ARGB8888 for correct rendering
- Print outcome badges misaligned above thumbnail
- Scroll-on-focus causing unwanted panel jumps
- Install service filtering to only existing system groups
- Screws tilt adjust detection from configfile fallback
- Wizard saving literal 'None' instead of empty string for unselected hardware
- Mock printer kinematics matching actual printer type
- Touch calibration detection unified with headless pointer support

### Changed
- Dark mode applies live without restart
- Calibration button layout redesigned Mainsail-style
- Textarea widgets migrated to text_input component
- Redundant kinematics polling eliminated

## [0.9.6] - 2026-02-08

### Added
- Per-object G-code toolpath thumbnails in Print Objects overlay
- AFC (Armored Turtle) support: live device state, tool mapping, endless spool, per-lane reset, maintenance and LED controls, quiet mode, and mock simulation
- Active object count shown on layer progress line during printing
- Change Host modal for switching Moonraker connection in settings
- Z movement style override setting and E-Stop relocated to Motion section
- K1 dynamic linking toolchain and build target
- Creality K2 series cross-compilation target (ARM, static musl — untested, needs hardware validation)
- CDN-first installer downloads with GitHub fallback
- Multi-channel R2 update distribution with GitHub API fallback

### Fixed
- Toasts now render on top layer instead of active screen (fixes toasts hidden behind overlays)
- Print cancel timeout increased to 15s with active state observation for more reliable cancellation
- Pre-print time estimates seeded from slicer data with blended early progress
- Thread-safe slicer estimate seeding during print start
- G-code viewer cache thrash from current_object changes during exclude-object prints
- ForgeX startup framebuffer stomping by S99root init script
- Wrong-platform binary install prevented with ELF architecture validation and safe rollback
- Use-after-free crash on Print Objects overlay close
- Isometric thumbnail rendering with shared projection, depth shading, and thicker lines
- Install warning text centered in update download modal
- Missing alert_circle icon codepoint
- Settings About section consolidated with cleaner version row layout
- Z baby step icons and color swatch labels
- Exclude object mock mode: objects populated from G-code on print start with proper status dispatch

## [0.9.5] - 2026-02-07

### Added
- Exclude object support for streaming/2D mode with selection brackets and long-press interaction
- Print Objects list overlay showing defined objects during a print
- LED selection dropdown in settings for multi-LED printers
- Version number displayed on splash screen
- Beta and dev update channels with UI toggle and R2 upload script
- Beta feature wrapper component with badge indicator
- 32-bit ARM (armv7l) Raspberry Pi build target (#10)
- Auto-publish tagged releases to R2 with platform detection
- Exclude object G-code parsing and status dispatch in mock mode

### Fixed
- Use-after-free race in wpa_supplicant backend shutdown (#8)
- Deadlock in Happy Hare and ToolChanger AMS backend start (#9)
- DNS resolver fallback for static glibc builds
- Crash when navigating folders during metadata fetch in print selection
- LED detection excluding toolhead LEDs from main LED control
- WebSocket max message size increased from 1MB to 5MB (#7)
- Elapsed/remaining time display during mock printing
- Crash on window close from SDL event handling during shutdown
- Accidental scroll taps by increasing scroll limit default
- G-code parser now reads layer_height, first_layer_height, object_height from metadata
- Invalid text_secondary color token replaced with text_muted
- KIAUH metadata wrapper key and moonraker updater path (#3)
- Installer sparse checkout for updater repo (#11)
- Output_pin lights detected as LEDs with fallback to first LED (#14)
- Percentage rounding instead of truncating to fix float precision (#14)
- Z offset display sync when print tune overlay opens (#14)
- CoreXZ treated as gantry-moves-Z instead of bed-moves (#14)

### Changed
- Log levels cleaned up: INFO is concise, DEBUG is useful without per-layer/shutdown spam
- Duplicate log bugs fixed (PrintStartCollector double-completion, PluginManager double-unload)
- Settings panel version rows deduplicated
- Exclude object modal XML registration and single-select behavior

## [0.9.4] - 2026-02-07

### Added
- Pre-print time predictions based on historical heating/homing data
- Heater status text on temperature cards (Heating, Cooling, At Target)
- Slicer estimated time fallback for remaining time
- Seconds in duration display under 5 minutes

### Fixed
- Crash on 16bpp HDMI screens from forced 32-bit color format
- Elapsed time using wall-clock duration instead of print-only time
- Pre-print overlay showing when it shouldn't
- Backlight not turning off on AD5M
- Heater status colors (heating=red, added cooling state)
- AMS row hidden when no AMS connected
- Modal button alignment
- Install script version detection on Pi (#6)

## [0.9.3] - 2026-02-06

First public beta release. Core features are complete — we're looking for early
adopters to help find edge cases.

**Supported platforms:** Raspberry Pi (aarch64), FlashForge AD5M (armv7l),
Creality K1 (MIPS32)

> **Note:** K1 binaries are included but have not been tested on hardware. If you
> have a K1, we'd love your help verifying it works!

### Added
- Print start profiles with modular, JSON-driven signal matching for per-printer phase detection
- NetworkManager WiFi backend for broader Linux compatibility
- `.3mf` file support in print file browser
- Non-printable file filtering in print selection
- Beta features gating system for experimental UI (HelixPrint plugin)
- Platform detection and preset system for zero-config installs
- Settings action rows with bind_description for richer UI
- Restart logic consolidated into single `app_request_restart_service()` entry point

### Fixed
- Print start collector not restarting after a completed print
- Sequential progress regression on repeated signals during print start
- Bed mesh triple-rendering and profile row click targets
- Wizard WiFi step layout, password visibility toggle, and dropdown corruption
- Touch calibration skipped for USB HID touchscreens (HDMI displays)
- CJK glyph inclusion from C++ sources in font generation
- File ownership for non-root deploy targets
- Console cursor hidden on fbdev displays

### Changed
- Pi deploys now use `systemctl restart` instead of stop/start
- fbdev display backend for Pi (avoids DRM master contention)
- Comprehensive architectural documentation from 5-agent audit
- Troubleshooting guide updated with debug logging instructions

## [0.9.2] - 2026-02-05

Major internal release with live theming, temperature sensor support, and
extensive UI polish across all panels.

### Added
- Live theme switching without restart — change themes in settings instantly
- Dark/light gradient backgrounds and themed overlay constants
- Full-screen 3D splash images with dark/light mode support
- Temperature sensor manager for auxiliary temp sensors (chamber, enclosure, etc.)
- Responsive fan dial with knob glow effect
- Software update checker with download progress and install-during-idle safety
- Platform hook architecture for modularized installer functions
- Auto-detect Pi install path from Klipper ecosystem
- AD5M preset with auto-detection for zero-config setup
- Beta features config flag for gating experimental UI
- CJK glyph support (Chinese, Japanese, Russian) in generated fonts
- Pencil edit icons next to temperature controls
- OS version, MCU versions, and printer name in About section
- Shell tests (shellcheck, bats) gating release builds

### Fixed
- Shutdown crash: stop animations before destroying panels to prevent use-after-free
- Observer crash: reorder display/subject teardown sequence
- Stale widget pointer guards for temperature and fan updates
- Theme palette preservation across dark/light mode switches
- Button text contrast for layout=column buttons with XML children
- Navbar background not updating on theme toggle
- Dropdown corruption with `&#10;` newline entities in XML
- Wizard initialization: fan subscriptions, sensor select, toast suppression
- Kinematics detection and Z button icons for bed-moves printers
- Bed mesh data normalization and zero plane visibility
- Filament panel deferred `set_limits` to main thread
- Touch calibration target spread and full-screen capture

### Changed
- Pi builds target Debian Bullseye for wider compatibility
- Static-link OpenSSL for cross-platform SSL support
- Binaries relocated to `bin/` subdirectory in deploy packages
- Fan naming uses configured roles instead of heuristics
- HelixScreen brand theme set as default
- Installer modularized with platform dispatchers
- Release build timeout increased to 60 minutes

## [0.9.1] - 2026-02-04

Initial tagged release. Foundation for all subsequent development.

### Added
- 30 panels and 16 overlays covering full printer control workflow
- First-run setup wizard with 8-step guided configuration
- Multi-material support: AFC, Happy Hare, tool changers, ValgACE, Spoolman
- G-code preview and 3D bed mesh visualization
- Calibration tools: input shaper, mesh leveling, screws tilt, PID, firmware retraction
- Internationalization system with hot-reload language switching
- Light and dark themes with responsive 800x480+ layout
- Cross-compilation for Pi (aarch64), AD5M (armv7l), K1 (MIPS32)
- Automated GitHub Actions release pipeline
- One-liner installation script with platform auto-detection

[0.9.16]: https://github.com/prestonbrown/helixscreen/compare/v0.9.15...v0.9.16
[0.9.15]: https://github.com/prestonbrown/helixscreen/compare/v0.9.14...v0.9.15
[0.9.14]: https://github.com/prestonbrown/helixscreen/compare/v0.9.13...v0.9.14
[0.9.13]: https://github.com/prestonbrown/helixscreen/compare/v0.9.12...v0.9.13
[0.9.12]: https://github.com/prestonbrown/helixscreen/compare/v0.9.11...v0.9.12
[0.9.11]: https://github.com/prestonbrown/helixscreen/compare/v0.9.10...v0.9.11
[0.9.10]: https://github.com/prestonbrown/helixscreen/compare/v0.9.9...v0.9.10
[0.9.9]: https://github.com/prestonbrown/helixscreen/compare/v0.9.8...v0.9.9
[0.9.8]: https://github.com/prestonbrown/helixscreen/compare/v0.9.7...v0.9.8
[0.9.7]: https://github.com/prestonbrown/helixscreen/compare/v0.9.6...v0.9.7
[0.9.6]: https://github.com/prestonbrown/helixscreen/compare/v0.9.5...v0.9.6
[0.9.5]: https://github.com/prestonbrown/helixscreen/compare/v0.9.4...v0.9.5
[0.9.4]: https://github.com/prestonbrown/helixscreen/compare/v0.9.3...v0.9.4
[0.9.3]: https://github.com/prestonbrown/helixscreen/compare/v0.9.2...v0.9.3
[0.9.2]: https://github.com/prestonbrown/helixscreen/compare/v0.9.1...v0.9.2
[0.9.1]: https://github.com/prestonbrown/helixscreen/releases/tag/v0.9.1
