# Calibration & Tuning

HelixScreen provides built-in tools for the most common Klipper calibration tasks.

---

## Bed Mesh

![Bed Mesh Panel](../../images/user/controls-bed-mesh.png)

3D visualization of your bed surface:

- **Color gradient**: Blue (low) to Red (high)
- **Touch to rotate** the 3D view
- **Mesh profile selector**: Switch between saved meshes

**Actions:**

| Button | What It Does |
|--------|--------------|
| **Calibrate** | Run new mesh probing |
| **Clear** | Remove current mesh from memory |
| **Load** | Load a saved mesh profile |

The visualization mode (3D, 2D, or Auto) can be changed in **Settings > Display**.

---

## Screws Tilt Adjust

![Screws Tilt Panel](../../images/user/advanced-screws.png)

Assisted manual bed leveling:

1. Navigate to **Advanced > Screws Tilt**
2. Tap **Measure** to probe all bed screw positions
3. View adjustment amounts (e.g., "CW 00:15" = clockwise 15 minutes)
4. Adjust screws and re-measure until level

**Color coding:**

- **Green**: Level (within tolerance)
- **Yellow**: Minor adjustment needed
- **Red**: Significant adjustment needed

---

## Input Shaper

![Input Shaper Panel](../../images/user/advanced-shaper.png)

Tune vibration compensation for smoother, faster prints:

1. Navigate to **Advanced > Input Shaper**
2. Review your current shaper configuration displayed at the top
3. Pre-flight check verifies accelerometer is connected
4. Select axis to test (X or Y)
5. Tap **Calibrate** to run the resonance test (5-minute timeout applies)
6. View **frequency response chart** with interactive shaper overlay toggles
7. Review the **comparison table** showing recommended shaper and alternatives (frequency, vibration reduction, smoothing)
8. Tap **Apply** to use for this session or **Save Config** to persist

**Chart features:**
- Toggle different shaper types on/off to compare their frequency response curves
- Platform-adaptive: full interactive charts on desktop, simplified on embedded hardware
- Per-axis results shown independently

![Input Shaper Results](../../images/screenshot-shaper-results.png)

> **Requirement:** Accelerometer must be configured in Klipper for measurements. If no accelerometer is detected, the pre-flight check will show a warning.

---

## Z-Offset Calibration

![Z-Offset Panel](../../images/user/advanced-zoffset.png)

Dedicated panel for dialing in your Z-offset when not printing:

1. Navigate to **Advanced > Z-Offset**
2. Home the printer
3. Use adjustment buttons to set the gap
4. Paper test: adjust until paper drags slightly
5. Tap **Save** to write to Klipper config

---

## PID Tuning

![PID Tuning Panel](../../images/user/controls-pid.png)

Calibrate temperature controllers for stable heating:

1. Navigate to **Advanced > PID**
2. Select **Nozzle** or **Bed**
3. Choose a **material preset** (PLA, PETG, ABS, etc.) or enter a custom target temperature
4. Optionally set **fan speed** — calibrating with the fan on gives more accurate results for printing conditions
5. Tap **Start** to begin automatic tuning

**During calibration:**
- **Live temperature graph** shows the heater cycling in real-time
- **Progress percentage** updates as calibration proceeds
- **Abort button** available if you need to stop early
- A **15-minute timeout** acts as a safety net for stuck calibrations

**When complete:**
- View new PID values with **old-to-new deltas** so you can see what changed
- Tap **Save Config** to persist the new values to your Klipper configuration

> **Tip:** Run PID tuning after any hardware change (new heater, thermistor, or hotend) and with the fan speed you typically use while printing.

---

**Next:** [Settings](settings.md) | **Prev:** [Filament Management](filament.md) | [Back to User Guide](../USER_GUIDE.md)
