# Sound System (Developer Guide)

How the sound system works internally, how to extend it with new themes/backends/sounds, and full reference for the JSON theme schema and C++ API.

**User-facing doc**: [Sound Settings](../user/guide/settings.md#sound-settings) (enabling/disabling, choosing themes, troubleshooting)

---

## Architecture Overview

The sound system has four layers, each with a clear responsibility:

```
SoundManager (singleton, public API)
  |  Respects SettingsManager toggles. Looks up sounds by name in the theme.
  |
  +-> SoundTheme / SoundThemeParser (JSON parser, note/duration conversion)
  |     Loads config/sounds/*.json, converts note names to Hz, musical durations to ms.
  |
  +-> SoundSequencer (dedicated thread, ADSR/LFO/sweep, priority queue)
  |     Ticks at ~1ms (backend-dependent). Computes envelope, modulation, filter sweep
  |     each tick and calls backend->set_tone().
  |
  +-> SoundBackend (abstract interface)
        |
        +-> SDLSoundBackend   (desktop builds, full waveform synthesis + biquad filter)
        +-> PWMSoundBackend   (AD5M hardware buzzer, sysfs /sys/class/pwm)
        +-> M300SoundBackend  (Klipper printers, G-code via Moonraker)
```

### Thread Model

```
LVGL thread (main)                  Sequencer thread (dedicated)
  |                                   |
  SoundManager::play("button_tap")    |
    |                                 |
    +-- mutex lock --+                |
    |  push to queue |                |
    +-- unlock ------+                |
    |                                 |
    |                    queue_cv_ wakeup
    |                                 |
    |                    pop request from queue
    |                    priority check -> preempt or drop
    |                                 |
    |                    tick loop @ ~1ms:
    |                      compute_envelope()   -> amplitude multiplier
    |                      compute_lfo()        -> parameter modulation
    |                      compute_sweep()      -> frequency/filter glide
    |                      backend->set_waveform()  (if supported)
    |                      backend->set_filter()    (if supported)
    |                      backend->set_tone(freq, amplitude, duty)
    |                                 |
    |                    advance_step() when step completes
    |                    end_playback() when sequence completes
```

The sequencer thread sleeps on a condition variable when idle (no sound playing, queue empty). When a sound is queued, it wakes and ticks at the backend's `min_tick_ms()` interval until playback completes.

---

## Backend Auto-Detection

`SoundManager::create_backend()` tries backends in this order. First success wins.

| Order | Backend | Condition | Target Hardware |
|-------|---------|-----------|-----------------|
| 1 | SDL | `#ifdef HELIX_DISPLAY_SDL` + `SDL_OpenAudioDevice` succeeds | Desktop/simulator |
| 2 | PWM | `/sys/class/pwm/pwmchip0/pwm6` exists | AD5M hardware buzzer |
| 3 | M300 | `MoonrakerClient` pointer set via `set_moonraker_client()` | Klipper printers with `output_pin beeper` |
| 4 | None | All above failed | Sounds silently disabled |

### Backend Capabilities

The sequencer adapts to what the backend can do. Features not supported by the backend are silently skipped.

| Backend | Waveforms | Amplitude | Filter | min_tick_ms | Notes |
|---------|-----------|-----------|--------|-------------|-------|
| SDL     | yes       | yes       | yes    | 1.0         | Full synthesis: 4 waveforms, biquad lowpass/highpass |
| PWM     | no*       | yes       | no     | 2.0         | Approximates waveforms via duty cycle ratios |
| M300    | no        | no        | no     | 50.0        | Frequency only, 100-10000 Hz, deduplicates commands |

*PWM `supports_waveforms()` returns `false`, but `set_waveform()` stores the waveform internally to adjust the duty cycle ratio: Square=50%, Saw=25%, Triangle=35%, Sine=40%. This gives perceptually different timbres even on a single-pin buzzer.

---

## Priority System

| Priority | Value | Use Case | Behavior |
|----------|-------|----------|----------|
| `UI`     | 0     | Button taps, nav sounds, toggles | Interrupted by anything |
| `EVENT`  | 1     | Print complete, errors | Only interrupted by ALARM |
| `ALARM`  | 2     | Critical failures | Never interrupted |

Rules:
- Higher priority preempts lower priority (new sound replaces current).
- Same priority: new sound replaces current.
- Lower priority than current: new sound is dropped (not queued).
- All priority checks happen on the sequencer thread when popping from the queue.

---

## Sound Theme JSON Schema

Themes live in `config/sounds/{name}.json`. The parser is lenient -- unknown fields are silently ignored, missing optional fields use defaults.

```jsonc
{
  "name": "theme_name",           // Required: displayed in settings dropdown
  "description": "...",            // Required: human-readable description
  "version": 1,                    // Required: schema version (currently 1)

  "defaults": {                    // Optional: applied when steps omit fields
    "wave": "square",              // square | saw | triangle | sine
    "vel": 0.8,                    // 0.0-1.0 (clamped)
    "env": {                       // ADSR envelope defaults
      "a": 5,                      // Attack time in ms (ramp 0 -> 1.0)
      "d": 40,                     // Decay time in ms (ramp 1.0 -> sustain)
      "s": 0.6,                    // Sustain level 0.0-1.0
      "r": 80                      // Release time in ms (ramp sustain -> 0)
    }
  },

  "sounds": {
    "sound_name": {
      "description": "...",        // Optional: documentation only
      "bpm": 140,                  // Optional: enables musical duration notation
      "repeat": 3,                 // Optional: repeat count (default 1)

      "steps": [
        {
          // === Frequency (one of): ===
          "freq": 440,             // Raw Hz (clamped to 20-20000)
          "note": "A4",            // Note name: A4=440 Hz, 12-TET, C0-B8
                                   // Supports sharps (C#4) and flats (Db4)

          // === Duration (one of): ===
          "dur": 100,              // Raw milliseconds (clamped to 1-30000)
          "dur": "8n",             // Musical: requires "bpm" on the sound
                                   // 1n=whole, 2n=half, 4n=quarter, 8n=eighth,
                                   // 16n=sixteenth, 4n.=dotted, 8t=triplet

          // === OR pause (exclusive with freq/note): ===
          "pause": 50,             // Silence in ms (clamped to 1-30000)

          // === Synthesis params: ===
          "wave": "square",        // square | saw | triangle | sine
                                   // Overrides theme default_wave
          "vel": 0.9,              // Velocity/volume 0.0-1.0
                                   // Overrides theme default_velocity

          // === ADSR envelope: ===
          "env": {                 // Overrides theme default_envelope
            "a": 5,                // Attack ms
            "d": 40,               // Decay ms
            "s": 0.6,              // Sustain level
            "r": 80                // Release ms
          },

          // === LFO modulation: ===
          "lfo": {
            "target": "amplitude", // "freq" | "amplitude" | "duty"
            "rate": 8,             // Oscillation rate in Hz
            "depth": 0.5           // Modulation depth (units depend on target)
          },

          // === Linear sweep: ===
          "sweep": {
            "target": "freq",      // Currently only "freq" is supported
            "end": 2400            // End value -- linearly interpolated over step
          },

          // === Filter (SDL backend only): ===
          "filter": {
            "type": "lowpass",     // "lowpass" | "highpass" (biquad, Butterworth Q)
            "cutoff": 800,         // Initial cutoff frequency in Hz
            "sweep_to": 4000       // Optional: sweep cutoff linearly over step
          }
        }
      ]
    }
  }
}
```

### Parsing Notes

- If both `freq` and `note` are present, `note` takes priority (checked first).
- If `dur` is a string, it requires `bpm` to be set on the sound definition. Without `bpm`, string durations resolve to 0.
- The `pause` field is exclusive with `freq`/`note` -- if `pause` is present, the step becomes a silence gap.
- All ADSR/LFO/sweep/filter fields are optional at every level. Missing fields use struct defaults (not theme defaults, unless the envelope is entirely omitted).
- Filter parameters are only sent to backends that return `supports_filter() == true` (currently SDL only).

---

## All Sound Names

These are the 12 standard sound names recognized by the system. Theme files may include any subset.

| Sound Name | Default Priority | Category | When Played |
|------------|------------------|----------|-------------|
| `button_tap` | UI | Interaction | Any `<ui_button>` clicked |
| `toggle_on` | UI | Interaction | Any `<ui_switch>` turned on |
| `toggle_off` | UI | Interaction | Any `<ui_switch>` turned off |
| `nav_forward` | UI | Navigation | Panel switch, overlay push |
| `nav_back` | UI | Navigation | Overlay close |
| `dropdown_open` | UI | Interaction | Reserved (not currently hooked) |
| `print_complete` | EVENT | Print Status | Print job completed successfully |
| `print_cancelled` | EVENT | Print Status | Print job cancelled |
| `error_alert` | EVENT | Error | Repeated urgent alert |
| `error_tone` | EVENT | Error | Error severity toast shown |
| `alarm_urgent` | ALARM | Critical | Critical failure alarm |
| `test_beep` | UI | Settings | Test sound button in settings panel |

**UI sounds** (`button_tap`, `toggle_on`, `toggle_off`, `nav_forward`, `nav_back`, `dropdown_open`) are affected by the `ui_sounds_enabled` toggle. EVENT and ALARM sounds play regardless of the UI toggle -- only the `sounds_enabled` master toggle can disable them.

The list of UI sounds is defined in `SoundManager::is_ui_sound()` in `sound_manager.cpp`.

---

## Adding a New Sound Theme

1. Create `config/sounds/mytheme.json`
2. Include all 12 standard sound names (or a subset -- missing sounds just won't play)
3. Set `name`, `description`, `version` fields at the top level
4. Theme appears automatically in Settings > Sound Theme dropdown
5. No code changes or rebuild needed -- `get_available_themes()` scans `config/sounds/*.json` at runtime

Example minimal structure:

```json
{
  "name": "mytheme",
  "description": "My custom sound theme",
  "version": 1,
  "defaults": {
    "wave": "square",
    "vel": 0.8,
    "env": { "a": 5, "d": 40, "s": 0.6, "r": 80 }
  },
  "sounds": {
    "button_tap": {
      "steps": [
        { "freq": 4000, "dur": 6, "vel": 0.9,
          "env": { "a": 1, "d": 5, "s": 0, "r": 1 } }
      ]
    }
  }
}
```

---

## Adding a New Sound

1. Add the sound definition to theme JSON files (all themes, or just the ones you want)
2. Call `SoundManager::instance().play("new_sound_name")` from C++ code
3. If it's a UI interaction sound (should be gated by `ui_sounds_enabled`), add the name to `SoundManager::is_ui_sound()` in `sound_manager.cpp`:

```cpp
bool SoundManager::is_ui_sound(const std::string& name) {
    return name == "button_tap" || name == "toggle_on" || name == "toggle_off" ||
           name == "nav_forward" || name == "nav_back" || name == "dropdown_open" ||
           name == "my_new_ui_sound";  // <-- add here
}
```

If the sound name isn't found in the current theme, `play()` logs a debug message and does nothing. No crash, no error -- this is by design so themes can include subsets of sounds.

---

## Adding a New Backend

1. Create header `include/my_backend.h` and source `src/system/my_backend.cpp`
2. Inherit from `SoundBackend` and implement the required interface:

```cpp
class MyBackend : public SoundBackend {
  public:
    // Required overrides:
    void set_tone(float freq_hz, float amplitude, float duty_cycle) override;
    void silence() override;

    // Optional overrides (defaults are all false/1.0):
    bool supports_waveforms() const override;   // default: false
    bool supports_amplitude() const override;   // default: false
    bool supports_filter() const override;      // default: false
    void set_waveform(Waveform w) override;     // default: no-op
    void set_filter(const std::string& type, float cutoff) override;  // default: no-op
    float min_tick_ms() const override;         // default: 1.0
};
```

3. Add detection logic in `SoundManager::create_backend()` in `sound_manager.cpp`. Order matters -- earlier entries take priority:

```cpp
std::shared_ptr<SoundBackend> SoundManager::create_backend() {
#ifdef HELIX_DISPLAY_SDL
    // ... SDL detection ...
#endif

    // Try my backend before PWM
    auto my_backend = std::make_shared<MyBackend>();
    if (my_backend->initialize()) {
        spdlog::info("[SoundManager] Using my backend");
        return my_backend;
    }

    // ... PWM detection ...
    // ... M300 detection ...
}
```

4. The Makefile auto-discovers new `.cpp` files via wildcard -- no Makefile edits needed.

### Backend Contract

- `set_tone()` is called at `min_tick_ms()` intervals while a step is active. Parameters change smoothly per-tick (ADSR, sweep, LFO).
- `silence()` must stop sound output immediately. May be called redundantly.
- `min_tick_ms()` determines the sequencer's sleep interval. Return a higher value for high-latency backends (e.g., M300 returns 50ms because G-code round-trips are slow).
- `set_waveform()` is only called if `supports_waveforms()` returns true.
- `set_filter()` is only called if `supports_filter()` returns true.

---

## Settings Integration

Three settings control sound behavior. All are persisted across restarts.

### Settings Subjects

| Subject | Type | Persisted | Effect |
|---------|------|-----------|--------|
| `settings_sounds_enabled` | bool (int 0/1) | Yes | Master toggle. When off, ALL sounds are suppressed. Hides sub-settings in UI. |
| `settings_ui_sounds_enabled` | bool (int 0/1) | Yes | UI sounds toggle. When off, only button/nav/toggle sounds are suppressed. |
| `printer_has_speaker` | bool (int 0/1) | No (auto-detected) | Set by `PrinterCapabilitiesState` based on hardware detection. When 0, hides the entire sound settings section. |

### Settings Panel XML

The sound settings live in `container_sounds` within `settings_panel.xml`:

```
container_sounds
  +-- row_sounds              (master toggle, bound to settings_sounds_enabled)
  +-- container_ui_sounds     (hidden when master off)
  |     +-- row_ui_sounds     (UI toggle, bound to settings_ui_sounds_enabled)
  +-- container_sound_theme   (hidden when master off)
  |     +-- row_sound_theme   (dropdown, populated from get_available_themes())
  +-- test beep button        (hidden when master off)
```

The `container_ui_sounds`, `container_sound_theme`, and test beep button all use `bind_flag_if_eq` to hide when `settings_sounds_enabled` is 0.

### SettingsManager API

```cpp
// Read
bool sounds_on = SettingsManager::instance().get_sounds_enabled();
bool ui_on = SettingsManager::instance().get_ui_sounds_enabled();
std::string theme = SettingsManager::instance().get_sound_theme();

// Write (persists to config)
SettingsManager::instance().set_sounds_enabled(true);
SettingsManager::instance().set_ui_sounds_enabled(false);
SettingsManager::instance().set_sound_theme("retro");

// Subjects for XML binding
lv_subject_t* subj = SettingsManager::instance().subject_sounds_enabled();
lv_subject_t* subj = SettingsManager::instance().subject_ui_sounds_enabled();
```

---

## API Quick Reference

```cpp
// --- Play sounds ---
SoundManager::instance().play("button_tap");
SoundManager::instance().play("print_complete", SoundPriority::EVENT);
SoundManager::instance().play("alarm_urgent", SoundPriority::ALARM);

// --- Backward-compat wrappers ---
SoundManager::instance().play_test_beep();         // play("test_beep")
SoundManager::instance().play_print_complete();     // play("print_complete", EVENT)
SoundManager::instance().play_error_alert();        // play("error_alert", EVENT)

// --- Theme management ---
SoundManager::instance().set_theme("retro");
auto themes = SoundManager::instance().get_available_themes();  // ["default", "minimal", "retro"]
auto current = SoundManager::instance().get_current_theme();    // "retro"

// --- Lifecycle ---
SoundManager::instance().set_moonraker_client(client);  // Before initialize()
SoundManager::instance().initialize();                   // Auto-detect backend, load theme, start sequencer
// ... app runs ...
SoundManager::instance().shutdown();                     // Stop sequencer, cleanup

// --- Availability ---
if (SoundManager::instance().is_available()) {
    // Backend exists AND sounds_enabled is true
}
```

### Thread Safety

- `play()` is safe to call from any thread (LVGL thread, WebSocket callbacks, etc.). It pushes to a mutex-protected queue.
- `set_theme()` should only be called from the LVGL thread (it modifies `current_theme_` which is read by `play()`).
- `initialize()` and `shutdown()` should be called from the LVGL thread during app startup/teardown.
- The sequencer thread is the only thread that calls backend methods.

---

## File Map

| File | Purpose |
|------|---------|
| `include/sound_backend.h` | Abstract backend interface (`set_tone`, `silence`, capabilities) |
| `include/sound_theme.h` | Theme structs (`SoundTheme`, `SoundStep`, `ADSREnvelope`, etc.) + parser class |
| `include/sound_sequencer.h` | Playback engine (`SoundPriority` enum, sequencer thread) |
| `include/sound_manager.h` | Singleton public API |
| `include/sdl_sound_backend.h` | SDL2 audio backend (desktop, `#ifdef HELIX_DISPLAY_SDL`) |
| `include/pwm_sound_backend.h` | PWM sysfs backend (AD5M buzzer) |
| `include/m300_sound_backend.h` | M300 G-code backend (Klipper via Moonraker) |
| `src/system/sound_theme.cpp` | Theme JSON parsing, note-to-freq, musical duration conversion |
| `src/system/sound_sequencer.cpp` | Sequencer thread + tick loop, ADSR/LFO/sweep math |
| `src/system/sound_manager.cpp` | Manager singleton, backend auto-detection, theme loading |
| `src/system/sdl_sound_backend.cpp` | SDL waveform synthesis, biquad filter (Direct Form II) |
| `src/system/pwm_sound_backend.cpp` | PWM sysfs writes (period, duty_cycle, enable) |
| `src/system/m300_sound_backend.cpp` | M300 G-code formatting, frequency deduplication |
| `config/sounds/default.json` | Default theme (12 sounds, balanced) |
| `config/sounds/minimal.json` | Minimal theme (6 sounds, event/alarm only) |
| `config/sounds/retro.json` | Retro chiptune theme (12 sounds, 8-bit style) |
| `tests/unit/test_sound_theme.cpp` | Theme parser tests |
| `tests/unit/test_sound_sequencer.cpp` | Sequencer tests |
| `tests/unit/test_sdl_sound_backend.cpp` | SDL backend tests |
| `tests/unit/test_pwm_sound_backend.cpp` | PWM backend tests |
| `tests/unit/test_m300_sound_backend.cpp` | M300 backend tests |

---

## Existing Themes

| Theme | File | Sounds | Style |
|-------|------|--------|-------|
| **default** | `config/sounds/default.json` | 12 | Balanced, tasteful. Saw waves with filter sweeps for nav, clean square taps. |
| **minimal** | `config/sounds/minimal.json` | 6 | Event/alarm sounds only. No UI interaction sounds. |
| **retro** | `config/sounds/retro.json` | 12 | 8-bit chiptune. Fast arpeggios, narrow duty cycles, game-style chirps. |

---

## Testing

```bash
# All sound tests
./build/bin/helix-tests "[sound]"

# Specific component tests
./build/bin/helix-tests "[sound_theme]"     # Theme parser
./build/bin/helix-tests "[sound_seq]"       # Sequencer
./build/bin/helix-tests "[sdl_sound]"       # SDL backend
./build/bin/helix-tests "[pwm_sound]"       # PWM backend
./build/bin/helix-tests "[m300]"            # M300 backend

# Full suite with sharding (recommended)
make test-run
```

**Important**: Do NOT run `./build/bin/helix-tests` without a tag filter or `make test-run` sharding -- some non-sound tests hang in single-process mode.

### Testing Backends Without Hardware

- **PWM backend**: Constructor accepts a custom `base_path` parameter. Tests create a temp directory tree mimicking `/sys/class/pwm/pwmchip0/pwm6/` and verify file writes.
- **M300 backend**: Constructor accepts a `GcodeSender` callback (lambda). Tests capture sent G-code strings.
- **SDL backend**: Static helper methods (`generate_samples`, `compute_biquad_coeffs`, `apply_filter`) are public for direct unit testing without SDL audio hardware.

---

## ADSR Envelope Details

The sequencer computes the amplitude envelope every tick. Given a step with duration `D` and ADSR values `(A, D, S, R)`:

```
Amplitude
  1.0 |    /\
      |   /  \
  S   |  /    \______________
      | /                     \
  0.0 |/                       \
      +---+---+-----------+----+---> time (ms)
      0   A  A+D     D-R       D

      |att|dec|  sustain  |rel |
```

- **Attack** (0 to A ms): Linear ramp from 0.0 to 1.0
- **Decay** (A to A+D ms): Linear ramp from 1.0 to sustain level
- **Sustain** (A+D to D-R ms): Hold at sustain level
- **Release** (D-R to D ms): Linear ramp from sustain to 0.0

If `A + D + R > duration`, the sustain phase is skipped and release starts immediately after decay.

If all ADSR values are 0, the envelope returns 1.0 (flat, no shaping).

---

## Sequencer Tick Internals

Each tick (~1ms on SDL, ~2ms on PWM, ~50ms on M300):

1. **Check stop request**: If `stop()` was called, end playback immediately.
2. **Process queue**: Pop all pending requests. Higher/equal priority preempts; lower priority is dropped.
3. **Advance elapsed time**: `step_state_.elapsed_ms += dt_ms` (capped at 5ms to prevent scheduling-delay jumps).
4. **Check step completion**: If elapsed >= total, call `advance_step()`.
5. **Compute parameters** for current step:
   - Base frequency from `step.freq_hz`
   - Base amplitude from `step.velocity`
   - Envelope multiplier from `compute_envelope()`
   - Sweep interpolation from `compute_sweep()` (linear, based on progress 0.0-1.0)
   - LFO offset from `compute_lfo()` (sinusoidal)
6. **Clamp outputs**: freq 20-20000 Hz, amplitude 0.0-1.0, duty 0.0-1.0
7. **Send to backend**: `set_waveform()`, `set_filter()`, `set_tone()`

When a step completes, `advance_step()` increments the step index. If past the end of the steps array, it decrements `repeat_remaining`. If repeats remain, it resets to step 0. Otherwise, playback ends.

---

## M300 Backend Specifics

The M300 backend sends G-code commands through Moonraker's `gcode_script` API. Key behaviors:

- **Frequency deduplication**: If `set_tone()` is called with the same frequency as the last call (and amplitude > 0), it's a no-op. This prevents spamming Moonraker with redundant commands.
- **Frequency clamping**: Hz values are clamped to 100-10000 (M300 safe range).
- **Duration in commands**: Each `M300 S{freq} P{dur}` uses `min_tick_ms()` (50ms) as the duration. The sequencer re-sends at each tick interval for continuing tones.
- **Silence**: `M300 S0 P1` stops the beeper. Only sent if not already silent.
- **No amplitude or waveform control**: M300 is frequency-only. The printer firmware controls volume.

The M300 backend requires Klipper to have `[output_pin beeper]` configured. Without it, M300 commands are silently ignored by the firmware.

---

## Future Extensions

- **Duty cycle parsing**: The `duty` field appears in theme JSON files (e.g., retro theme) but is not yet parsed into `SoundStep`. The sequencer defaults duty to 0.5 and only modulates it via LFO. Adding `duty` to `SoundStep` + the parser is a minor change.
- **Volume normalization**: Per-theme or per-backend volume curves.
- **Custom user themes**: `config/sounds.d/` directory for user-created themes that survive updates.
- **M300 batch mode**: Pre-compute a sound sequence into a list of M300 commands and send them all at once, reducing round-trip latency.
