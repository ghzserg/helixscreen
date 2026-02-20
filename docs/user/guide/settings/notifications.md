# Settings: Notifications

The Notifications section controls audio feedback and print completion alerts.

---

## Sound Settings

> Only shown when HelixScreen detects a speaker or buzzer on your printer.

Tap **Sound Settings** to open the overlay:

| Setting | Effect |
|---------|--------|
| **Sounds** | Master toggle — turns all sounds on or off |
| **Volume** | Master volume slider (0–100%). Only shown when sounds are enabled. |
| **UI Sounds** | Controls button taps, navigation, and toggle sounds. When off, only important sounds still play (print complete, errors, alarms). |
| **Sound Theme** | Choose sound style. A test sound plays when you switch themes. |
| **Test Sound** | Play a test beep to verify audio is working |

All options except the master toggle are hidden when sounds are disabled. Both toggles take effect immediately.

### Sound Themes

HelixScreen comes with three built-in themes:

| Theme | Description |
|-------|-------------|
| **Default** | Balanced, tasteful sounds. Subtle clicks, smooth navigation chirps, and a melodic fanfare when your print completes. |
| **Minimal** | Only plays sounds for important events: print complete, errors, and alarms. No button or navigation sounds at all. |
| **Retro** | 8-bit chiptune style. Punchy square-wave arpeggios, a Mario-style victory fanfare, and buzzy retro alarms. |

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

The first five (button press, toggles, navigation) are **UI sounds** and respect the "UI Sounds" toggle. The rest always play as long as the master toggle is on.

### Supported Hardware

| Hardware | How It Works |
|----------|-------------|
| **Desktop (SDL)** | Full audio synthesis through your computer speakers. Best sound quality. |
| **Creality AD5M / AD5M Pro** | Hardware PWM buzzer. Supports different tones and volume levels. |
| **Other Klipper printers** | Beeper commands sent through Moonraker. Requires `[output_pin beeper]` in your Klipper config. Basic beep tones only. |

If no audio hardware is detected, the Sound Settings row is hidden entirely.

### Sound Troubleshooting

**I don't see Sound Settings in the Settings panel.**
Your printer doesn't have a detected speaker or buzzer. For Klipper printers, make sure you have `[output_pin beeper]` configured in your `printer.cfg`, then restart HelixScreen.

**Sounds are too quiet or too loud.**
Adjust the Volume slider in Sound Settings. Volume also varies by theme — try switching themes.

**Print complete sound doesn't play.**
Make sure the master "Sounds" toggle is on. The "UI Sounds" toggle does not affect print completion sounds.

**Button click sounds are annoying.**
Turn off "UI Sounds" in Sound Settings. This disables button, toggle, and navigation sounds while keeping important notifications.

**Sounds work on desktop but not on my printer.**
Confirm your printer has audio hardware. For Klipper printers, verify `[output_pin beeper]` is present and correctly configured. Test by sending an `M300` command from the Klipper console.

---

## Print Completion Alert

Controls how HelixScreen notifies you when a print finishes, is cancelled, or fails — when you're not already on the print status screen.

| Mode | Behavior |
|------|----------|
| **Off** | No visual notification (sound still plays if enabled) |
| **Notification** | Brief toast message at the top of the screen |
| **Alert** (default) | Full-screen modal showing print stats — duration, layers, filament used — with confetti for successful prints |

To change: **Settings > Print Completion Alert** dropdown.

> **Note:** Print errors always show the full alert modal regardless of this setting, since errors need immediate visibility. If you're already on the print status screen when a print ends, no notification is shown (the panel itself shows the result).

Sound always plays for terminal print states (complete, cancelled, error) regardless of alert mode, as long as the master Sounds toggle is on.

---

[Back to Settings](../settings.md) | [Prev: Printer](printer.md) | [Next: Motion](motion.md)
