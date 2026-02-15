# Settings

![Settings Panel](../../images/user/settings.png)

Access via the **Gear icon** in the navigation bar.

---

## Display Settings

![Display Settings](../../images/user/settings-display.png)

| Setting | Options |
|---------|---------|
| **Brightness** | 0-100% backlight level |
| **Dim timeout** | When screen dims (30s, 1m, 2m, 5m, Never) |
| **Sleep timeout** | When screen turns off (1m, 2m, 5m, 10m, Never) |
| **Time format** | 12-hour or 24-hour clock |
| **Bed mesh render** | Auto, 3D, or 2D visualization |
| **Display rotation** | 0°, 90°, 180°, 270° — rotates all three binaries (main, splash, watchdog) |

**Layout auto-detection:** HelixScreen automatically selects the best layout for your display size. Standard (800x480), ultrawide (1920x480), and compact (480x320) layouts are supported. You can override with the `--layout` command-line flag.

---

## Theme Settings

![Theme Settings](../../images/user/settings-theme.png)

1. Tap **Theme** to open the theme explorer
2. Browse available themes (built-in and custom)
3. Toggle dark/light mode to preview
4. Tap **Apply** to use a theme (restart may be required)
5. Tap **Edit** to customize colors in the theme editor

---

## Sound Settings

HelixScreen can play sounds for button presses, navigation, print events, and alerts. Sounds work on all supported hardware — from desktop speakers to the tiny buzzer on your Creality printer.

> **Note:** Sound settings only appear when HelixScreen detects a speaker or buzzer on your printer. If you don't see the sound section in Settings, your printer may not have audio output hardware. See [Troubleshooting](#sound-troubleshooting) below.

### Toggles

| Setting | Effect |
|---------|--------|
| **Sounds Enabled** | Master toggle. Turns all sounds on or off. |
| **UI Sounds Enabled** | Controls button taps, navigation, and toggle sounds. When off, only important sounds still play (print complete, errors, alarms). Useful if you want notifications but not click feedback. |

Both toggles take effect immediately.

### Sound Themes

HelixScreen comes with three built-in themes:

| Theme | Description |
|-------|-------------|
| **Default** | Balanced, tasteful sounds. Subtle clicks, smooth navigation chirps, and a melodic fanfare when your print completes. |
| **Minimal** | Only plays sounds for important events: print complete, errors, and alarms. No button or navigation sounds at all. |
| **Retro** | 8-bit chiptune style. Punchy square-wave arpeggios, a Mario-style victory fanfare, and buzzy retro alarms. |

To change themes, go to **Settings > Sound Theme** and select from the dropdown. A test sound plays immediately so you can preview.

Advanced users can create custom sound themes by adding a JSON file to `config/sounds/` on the printer. Custom themes appear automatically in the dropdown — no restart required. See the [Sound System developer docs](../../devel/SOUND_SYSTEM.md#adding-a-new-sound-theme) for the file format.

### What Sounds When

| Event | Sound | When It Plays |
|-------|-------|---------------|
| Button press | Short click | Any button tapped |
| Toggle on | Rising chirp | A switch turned on |
| Toggle off | Falling chirp | A switch turned off |
| Navigate forward | Ascending tone | Opening a screen or overlay |
| Navigate back | Descending tone | Closing an overlay or going back |
| Print complete | Victory melody | Print finished successfully |
| Print cancelled | Descending tone | Print job cancelled |
| Error alert | Pulsing alarm | A significant error occurred |
| Error notification | Short buzz | An error toast appeared |
| Critical alarm | Urgent siren | Critical failure requiring attention |
| Test sound | Short beep | "Test Sound" button in settings |

The first five (button press, toggles, navigation) are **UI sounds** and respect the "UI Sounds Enabled" toggle. The rest always play as long as the master toggle is on.

### Supported Hardware

HelixScreen auto-detects your audio hardware at startup:

| Hardware | How It Works |
|----------|-------------|
| **Desktop (SDL)** | Full audio synthesis through your computer speakers. Best sound quality. |
| **Creality AD5M / AD5M Pro** | Hardware PWM buzzer. Supports different tones and volume levels. |
| **Other Klipper printers** | Beeper commands sent through Moonraker. Requires `[output_pin beeper]` in your Klipper config. Basic beep tones only. |

If no audio hardware is detected, sound settings are hidden and HelixScreen operates silently.

### Sound Troubleshooting

**I don't see sound settings in the Settings panel.**
Your printer doesn't have a detected speaker or buzzer. For Klipper printers, make sure you have `[output_pin beeper]` configured in your `printer.cfg`, then restart HelixScreen.

**Sounds are too quiet or too loud.**
Volume varies by theme. Try switching to a different theme. Custom themes let you adjust volume per sound.

**Print complete sound doesn't play.**
Make sure the master "Sounds Enabled" toggle is on. The "UI Sounds" toggle does not affect print completion sounds.

**Button click sounds are annoying.**
Turn off "UI Sounds Enabled" in Settings. This disables button, toggle, and navigation sounds while keeping important notifications.

**Sounds work on desktop but not on my printer.**
Confirm your printer has audio hardware. For Klipper printers, verify `[output_pin beeper]` is present and correctly configured. Test by sending an `M300` command from the Klipper console.

---

## Network Settings

![Network Settings](../../images/user/settings-network.png)

- **WiFi**: Connect to wireless networks, view signal strength
- **Moonraker**: Change printer connection address and port

---

## Sensor Settings

![Sensor Settings](../../images/user/settings-sensors.png)

Manage all printer sensors:

**Filament sensors** — choose the role for each:

| Role | Behavior |
|------|----------|
| **Runout** | Pauses print when filament runs out |
| **Motion** | Detects filament movement (clog detection) |
| **Ignore** | Sensor present but not monitored |

**Other sensors** — view detected hardware:

- Accelerometers (for Input Shaper)
- Probe sensors (BLTouch, eddy current, etc.)
- Humidity, width, color sensors

---

## LED Settings

Tap **LED Settings** in Settings to open the LED configuration overlay. This is where you choose which lights HelixScreen controls and how they behave.

> **Tip:** To control your LEDs during a print, **long-press the lightbulb button** on the Home Panel to open the LED Control Overlay. See [Home Panel > LED Controls](home-panel.md#led-controls) for details.

### Supported LED Types

HelixScreen auto-detects your LED hardware from Klipper and Moonraker. Four types of lighting are supported:

| Type | Examples | How It's Detected |
|------|----------|-------------------|
| **Klipper native strips** | Neopixel (WS2812, SK6812), Dotstar (APA102), PCA9632, GPIO LEDs | Automatically from your Klipper config |
| **WLED strips** | Network-attached WLED controllers | From Moonraker's WLED configuration |
| **LED effects** | Animated effects (breathing, rainbow, etc.) | Requires the [klipper-led_effect](https://github.com/julianschill/klipper-led_effect) plugin |
| **Macro devices** | Any Klipper macro that controls lights | User-configured (see [Macro Devices](#macro-devices) below) |

### LED Strip Selection

Multi-select chips show all detected strips. Tap a chip to toggle whether HelixScreen controls that strip. You can select multiple strips — they'll all respond to the lightbulb toggle on the Home Panel.

**If you don't see your LEDs here:**
- Make sure the LED is defined in your Klipper config (`printer.cfg`)
- For WLED, make sure it's configured in Moonraker (`moonraker.conf`)
- Restart HelixScreen after adding new LED hardware

### LED On At Start

Toggle this on to automatically turn your selected LEDs on when Klipper becomes ready. Useful for chamber lights that should always be on when the printer is powered up.

### Auto-State Lighting

When enabled, your LEDs automatically change based on what the printer is doing — no macros or manual control needed. This is great for visual status feedback: dim lights when idle, bright white while printing, green when a print finishes.

Configure behavior for six printer states:

| State | When It Triggers |
|-------|-----------------|
| **Idle** | Klipper is ready, not printing |
| **Heating** | Extruder is heating up (target > 0, not yet printing) |
| **Printing** | Print is in progress |
| **Paused** | Print is paused |
| **Error** | Klipper error or shutdown |
| **Complete** | Print just finished |

Each state has an action type dropdown — what HelixScreen does with your LEDs when that state is entered:

| Action | What It Does |
|--------|--------------|
| **Off** | Turn LEDs off |
| **Brightness** | Set a brightness level (0–100%) without changing color |
| **Color** | Set a specific color from the preset swatches, with a brightness slider |
| **Effect** | Activate a Klipper LED effect (e.g., breathing, rainbow) |
| **WLED Preset** | Activate a WLED preset by ID |
| **Macro** | Run a configured LED macro device |

> **Note:** Only actions your hardware supports are shown. **Effect** requires the `led_effect` Klipper plugin. **WLED Preset** requires WLED strips. **Color** requires color-capable strips (not single-channel PWM).

**Example setup:**

| State | Action | Setting |
|-------|--------|---------|
| Idle | Brightness | 30% (dim standby light) |
| Heating | Color | Red at 100% |
| Printing | Color | White at 100% |
| Paused | Effect | "breathing" |
| Error | Color | Red at 100% |
| Complete | Color | Green at 100% |

### Macro Devices

Macro devices let you control LEDs that aren't directly supported by Klipper's LED system — like relay-switched cabinet lights, custom G-code lighting macros, or multi-mode LED setups.

**Auto-discovery:** HelixScreen automatically finds Klipper macros with "led" or "light" in their name and makes them available in the macro dropdown lists.

Three device types are available:

| Type | Best For | Controls |
|------|----------|----------|
| **On/Off** | Lights with separate on and off macros | Two macros: one to turn on, one to turn off |
| **Toggle** | Lights with a single toggle macro | One macro that flips the state |
| **Preset** | Multi-mode lights (e.g., party mode, reading mode) | Multiple named presets, each mapped to a different macro |

**To add a macro device:**

1. Tap the **+** button in the Macro Devices section
2. Enter a display name (e.g., "Cabinet Light")
3. Choose a device type
4. Select the appropriate macros from the dropdown(s)
5. Tap **Save**

**To edit or delete:** Tap the **pencil icon** to modify, or the **trash icon** to remove.

**Example:** If you have macros `LIGHTS_CABINET_ON` and `LIGHTS_CABINET_OFF` in your Klipper config, create an **On/Off** device named "Cabinet Light" and map each macro accordingly. It will appear as a controllable device in the LED Control Overlay.

### Setting Up Different LED Types

#### Klipper Native Strips (Neopixel, Dotstar, etc.)

These work out of the box. Define them in your `printer.cfg`:

```ini
[neopixel chamber_light]
pin: PA8
chain_count: 24
color_order: GRB
```

Restart Klipper, then open **Settings > LED Settings** — the strip appears automatically. Select it and you'll get full color and brightness control.

#### WLED Network Strips

WLED strips are network-attached LED controllers managed outside of Klipper. Configure them in `moonraker.conf`:

```ini
[wled strip_name]
address: 192.168.1.100
```

After restarting Moonraker, the WLED strip appears in LED Settings. You get on/off toggle, brightness, and access to all WLED presets you've configured in the WLED web interface.

#### LED Effects (Animated Patterns)

Install the [klipper-led_effect](https://github.com/julianschill/klipper-led_effect) plugin and define effects in your `printer.cfg`:

```ini
[led_effect breathing]
leds:
    neopixel:chamber_light
autostart: false
frame_rate: 24
layers:
    breathing 10 1 top (1.0, 1.0, 1.0)
```

Effects show up in the LED Control Overlay and as auto-state action options. Only effects targeting the currently selected strip are displayed.

#### Macro-Controlled Lights

For lights controlled via G-code macros (relay-switched enclosure lights, Klipper macros wrapping custom commands, etc.):

1. Define your macros in Klipper (include "led" or "light" in the name for auto-discovery)
2. Go to **Settings > LED Settings > Macro Devices**
3. Create a device and map the appropriate macros
4. The device appears in the LED Control Overlay alongside your other strips

---

## Touch Calibration

> **Note:** This option only appears on touchscreen displays, not in the desktop simulator.

Recalibrate if taps register in the wrong location:

1. Tap **Touch Calibration** in Settings
2. Tap each crosshair target as it appears
3. Calibration saves automatically

The settings row shows "Calibrated" or "Not calibrated" status.

---

## Hardware Health

![Hardware Health](../../images/user/settings-hardware.png)

View detected hardware issues:

| Category | Meaning |
|----------|---------|
| **Critical** | Required hardware missing |
| **Warning** | Expected hardware not found |
| **Info** | Newly discovered hardware |
| **Session** | Hardware changed since last session |

**Actions for non-critical issues:**

- **Ignore**: Mark as optional (won't warn again)
- **Save**: Add to expected list (will warn if removed later)

Use this when adding or removing hardware to keep HelixScreen's expectations accurate.

---

## Safety Settings

| Setting | Description |
|---------|-------------|
| **E-Stop confirmation** | Require tap-and-hold before emergency stop |
| **Cancel Escalation** | When enabled, automatically escalates a stalled cancel to an emergency stop after the configured timeout. **Off by default** — cancel waits for the printer to finish its cancel routine. Useful to leave off for toolchangers and printers with long cancel macros. |
| **Escalation Timeout** | Only visible when Cancel Escalation is on. How long to wait before escalating: 15, 30, 60, or 120 seconds. |

---

## Machine Limits

Adjust motion limits for the current session:

- Maximum velocity per axis
- Maximum acceleration per axis
- Maximum jerk per axis

These override your Klipper config temporarily — useful for testing or troubleshooting motion issues.

---

## Factory Reset

Clears all HelixScreen settings and restarts the Setup Wizard. Does not affect Klipper configuration.

---

**Next:** [Advanced Features](advanced.md) | **Prev:** [Calibration & Tuning](calibration.md) | [Back to User Guide](../USER_GUIDE.md)
