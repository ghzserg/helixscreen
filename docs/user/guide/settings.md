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

Tap **Sound** in Settings to open the dedicated sound overlay:

| Setting | Options |
|---------|---------|
| **Enable sounds** | Toggle all sound effects on/off |
| **Volume slider** | Adjust volume level (plays a test beep when you release the slider) |
| **Sound theme** | Choose a theme: Minimal (subtle) or Retro Chiptune (8-bit) |
| **Completion alert** | How to notify when prints finish (Off, Notification, Alert) |

> **Note:** Sound uses the best available backend for your hardware: SDL audio on Pi/desktop, PWM on embedded boards, or M300 G-code commands as fallback. Sound is currently a beta feature.

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
