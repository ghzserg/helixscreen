# Advanced Features

![Advanced Panel](../../images/user/advanced.png)

Access via the **More** icon in the navigation bar.

---

## Console

View G-code command history and Klipper responses:

1. Navigate to **Advanced > Console**
2. Scroll through recent commands

**Color coding:**

- **White**: Commands sent
- **Green**: Successful responses
- **Red**: Errors and warnings

---

## Macro Execution

![Macro Panel](../../images/user/advanced-macros.png)

Execute custom Klipper macros:

1. Navigate to **Advanced > Macros**
2. Browse available macros (alphabetically sorted)
3. Tap a macro to execute

**Notes:**

- System macros (starting with `_`) are hidden by default
- Names are prettified: `CLEAN_NOZZLE` becomes "Clean Nozzle"
- Dangerous macros (`SAVE_CONFIG`, etc.) require confirmation

---

## Power Device Control

Control Moonraker power devices:

1. Navigate to **Advanced > Power**
2. Toggle devices on/off with switches

**Notes:**

- Devices may be locked during prints (safety feature)
- Lock icon indicates protected devices

---

## Print History

![Print History](../../images/user/advanced-history.png)

View past print jobs:

**Dashboard view:**

- Total prints, success rate
- Print time and filament usage statistics
- Trend graphs over time

**List view:**

- Search by filename
- Filter by status (completed, failed, cancelled)
- Sort by date, duration, or name

**Detail view:**

- Tap any job for full details
- **Reprint**: Start the same file again
- **Delete**: Remove from history

---

## Notification History

Review past system notifications:

1. Tap the **bell icon** in the status bar
2. Scroll through history
3. Tap **Clear All** to dismiss

**Color coding:**

- Blue: Info
- Yellow: Warning
- Red: Error

---

## Timelapse Settings

Configure Moonraker-Timelapse (beta feature):

1. Navigate to **Advanced > Timelapse**
2. If the timelapse plugin is not installed, HelixScreen detects this and offers an **Install Wizard** to set it up
3. Once installed, configure settings:
   - Enable/disable timelapse recording
   - Select mode: **Layer Macro** (snapshot at each layer) or **Hyperlapse** (time-based)
   - Set framerate (15/24/30/60 fps)
   - Enable auto-render for automatic video creation

---

**Next:** [Beta Features](beta-features.md) | **Prev:** [Settings](settings.md) | [Back to User Guide](../USER_GUIDE.md)
