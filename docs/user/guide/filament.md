# Filament Management

---

## Extrusion Panel

![Extrusion Panel](../../images/user/controls-extrusion.png)

Manual filament control:

| Button | Action |
|--------|--------|
| **Extrude** | Push filament through nozzle |
| **Retract** | Pull filament back |

**Amount selector**: 5mm, 10mm, 25mm, 50mm
**Speed selector**: Slow, Normal, Fast

> **Safety:** Extrusion requires the hotend to be at minimum temperature (usually 180°C for PLA, higher for other materials).

---

## Load / Unload Filament

**To load filament:**

1. Heat the nozzle to appropriate temperature
2. Insert filament into extruder
3. Use **Extrude** button (10-25mm increments) until filament flows cleanly

**To unload filament:**

1. Heat the nozzle
2. Use **Retract** button repeatedly until filament clears the extruder

---

## AMS / Multi-Material Systems

![AMS Panel](../../images/user/ams.png)

For multi-material systems (Happy Hare, AFC-Klipper):

### Slot Status

- Visual display of all slots with material labels (PLA, PETG, ABS, ASA, etc.)
- Spool icons with color indicators for loaded filament
- Active slot highlighted — shows "Currently Loaded" with material and remaining weight
- Hub and bypass path visualization shows the filament routing

### Controls

- **Load**: Feed filament from selected slot to toolhead
- **Unload**: Retract filament back to buffer
- **Home**: Run homing sequence for the AMS

Tap a slot to select it before load/unload operations.

---

## Spoolman Integration

![Spoolman](../../images/user/advanced-spoolman.png)

When Spoolman is configured:

- Spool name and material type displayed per slot
- Remaining filament weight shown
- Tap a slot to open **Spool Picker** and assign a different spool

---

## Dryer Control

If your AMS has an integrated dryer:

- Temperature display and control
- Timer settings
- Enable/disable drying cycle

---

**Next:** [Calibration & Tuning](calibration.md) | **Prev:** [Motion & Positioning](motion.md) | [Back to User Guide](../USER_GUIDE.md)
