# Settings: Motion

The Motion section controls how HelixScreen handles movement commands and safety features.

---

## Z Movement

Controls how Z-axis movement is displayed in the motion controls.

| Mode | Behavior |
|------|----------|
| **Auto** (default) | HelixScreen auto-detects based on your printer type (bed-slinger vs CoreXY vs delta) |
| **Bed Moves** | Z controls labeled as bed movement (bed goes down = nozzle moves up relative to bed) |
| **Nozzle Moves** | Z controls labeled as nozzle movement (nozzle goes up = away from bed) |

This only changes the direction labels in the UI — the actual G-code sent is the same. Use this if auto-detection picks the wrong style for your printer.

---

## Machine Limits

Tap to open the Machine Limits overlay. Adjust motion limits for the current session:

- Maximum velocity per axis
- Maximum acceleration per axis

These override your Klipper config temporarily — useful for testing or troubleshooting motion issues. **Changes are lost on restart.** To make permanent changes, edit `printer.cfg` directly.

---

## E-Stop Confirmation

| State | Behavior |
|-------|----------|
| **Off** (default) | Tapping E-Stop fires immediately — fastest emergency response |
| **On** | Shows a confirmation dialog requiring a tap-and-hold before triggering |

Enable this if you find yourself accidentally hitting the E-Stop button. Disable it if you need the fastest possible emergency response.

---

## Cancel Escalation

When a print cancel is sent, some printers take a long time to finish their cancel routine (parking tools, cooling down, running CANCEL_PRINT macros). Cancel Escalation adds a safety net: if the cancel doesn't complete within a timeout, HelixScreen automatically escalates to an emergency stop (M112).

| Setting | Options |
|---------|---------|
| **Cancel Escalation** | On/Off toggle. **Off by default.** |
| **Escalation Timeout** | 15, 30, 60, or 120 seconds. Only shown when escalation is enabled. Default: 30 seconds. |

**When to leave this off:**
- Toolchangers that need to park tools during cancel
- Printers with long CANCEL_PRINT macros
- Any printer where the cancel routine is expected to take more than a few seconds

**When to turn this on:**
- Simple printers where cancel should complete quickly
- If you've experienced "stuck" cancels where the printer never returns to idle

---

[Back to Settings](../settings.md) | [Prev: Notifications](notifications.md) | [Next: System](system.md)
