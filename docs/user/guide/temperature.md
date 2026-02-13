# Temperature Control

![Temperature Controls](../../images/user/controls-temperature.png)

---

## Nozzle Temperature Panel

- **Current temperature**: Live reading from thermistor
- **Target input**: Tap to enter exact temperature
- **Presets**: Quick buttons for common temperatures
- **Temperature graph**: History over time

---

## Bed Temperature Panel

Same layout as nozzle control:

- Current and target temperature
- Presets for common materials
- Temperature graph

---

## Temperature Presets

Built-in presets:

| Material | Nozzle | Bed |
|----------|--------|-----|
| Off | 0°C | 0°C |
| PLA | 210°C | 60°C |
| PETG | 240°C | 80°C |
| ABS | 250°C | 100°C |

Tap a preset to set both current and target. Custom presets can be configured via Klipper macros.

---

## Temperature Graphs

Live graphs show:

- **Current temperature** (solid line)
- **Target temperature** (dashed line)
- **History** scrolling over time

Useful for diagnosing heating issues or verifying PID tuning.

---

**Next:** [Motion & Positioning](motion.md) | **Prev:** [Printing](printing.md) | [Back to User Guide](../USER_GUIDE.md)
