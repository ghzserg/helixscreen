# Home Panel

![Home Panel](../../images/user/home.png)

The Home Panel is your printer dashboard — showing status at a glance.

---

## Status Area

The top area displays:

- **Printer state**: Idle, Printing, Paused, Complete, Error
- **Print progress**: Percentage and time remaining (when printing)
- **Current filename**: What's being printed
- **Connection indicator**: Your link to Moonraker/Klipper

---

## Temperature Displays

Real-time temperature readouts show:

- **Nozzle**: Current / Target temperature
- **Bed**: Current / Target temperature
- **Chamber**: Current temperature (if equipped)

**Tap any temperature** to jump directly to its control panel.

---

## AMS / Filament Status

If you have a multi-material system (Happy Hare, AFC-Klipper, Bambu AMS):

- Visual display of loaded filament slots
- Current active slot highlighted
- Color indicators from Spoolman (if integrated)
- Tap to access the Filament panel

---

## Quick Actions

Buttons for common operations:

- **LED Toggle**: Turn chamber/printer lights on/off
- **Emergency Stop**: Halt all motion immediately (confirmation required unless disabled in Safety Settings)

---

## Printer Manager

**Tap the printer image** on the Home Panel to open the Printer Manager overlay. This is your central place to view and customize your printer's identity.

### Printer Identity Card

The top of the overlay displays an identity card with your printer image, name, and model. From here you can:

- **Change the printer image**: Tap the printer image (marked with a pencil badge) to open the Printer Image picker overlay. You can choose from:
  - **Auto-Detect** (default) — HelixScreen selects an image based on your printer type reported by Klipper
  - **Shipped Images** — Over 25 pre-rendered images covering Voron, Creality, FlashForge, Anycubic, RatRig, FLSUN, and more
  - **Custom Images** — Your own PNG or JPEG files (see below)

  The picker shows a list on the left and a live preview on the right. Your selection persists across restarts.

- **Edit the printer name**: Tap the printer name (shown with a pencil icon) to enable inline editing. Type the new name, press **Enter** to save, or **Escape** to cancel.

### Software Versions

Below the identity card, the overlay displays current software versions for Klipper, Moonraker, and HelixScreen.

### Hardware Capabilities

A row of chips shows detected hardware capabilities: Probe, Bed Mesh, Heated Bed, LEDs, ADXL, QGL, Z-Tilt, and others depending on your Klipper configuration.

### Adding Custom Printer Images

To use your own printer image:

1. Place a PNG or JPEG file into `config/custom_images/` in your HelixScreen installation directory
2. Open the Printer Image picker from the Printer Manager
3. Your custom images appear automatically — HelixScreen converts them to optimized LVGL binary format on first load

**Custom image requirements:** PNG or JPEG, maximum 5MB file size, maximum 2048x2048 pixels. HelixScreen generates optimized 300px and 150px variants automatically.

---

**Next:** [Printing](printing.md) | **Prev:** [Getting Started](getting-started.md) | [Back to User Guide](../USER_GUIDE.md)
