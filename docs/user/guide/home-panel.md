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

## Active Tool Badge

On printers with a toolchanger, the Home Panel displays an active tool badge:

- Shows the current active tool (e.g. "T0", "T1")
- Updates automatically when tools are switched during a print or via macros
- Only visible on multi-tool printers — single-extruder printers will not see this badge

---

## Quick Actions

Buttons for common operations:

- **LED Button** (lightbulb icon):
  - **Tap** to toggle your LEDs on/off
  - **Long-press** to open the **LED Control Overlay** with full color, brightness, effects, and preset controls (see [LED Controls](#led-controls) below)
- **Emergency Stop**: Halt all motion immediately (confirmation required unless disabled in Safety Settings)

---

## LED Controls

Long-pressing the LED button opens the LED Control Overlay — a full control panel for all your printer's lighting. What you see depends on your hardware.

### Strip Selector

If you have more than one LED strip configured, a row of chips lets you pick which strip to control. The overlay heading updates to show the selected strip name.

### Color & Brightness (Klipper Native LEDs)

For neopixel, dotstar, and other Klipper-native strips:

- **Color presets**: Tap one of the 8 preset swatches (White, Warm, Orange, Blue, Red, Green, Purple, Cyan)
- **Custom color**: Tap the custom color button to open an HSV color picker — pick any color and it automatically separates into a base color and brightness level
- **Brightness slider**: Adjust from 0–100%, independent of color
- **Color swatch**: Shows the actual output color (base color adjusted by brightness)
- **Turn Off**: Stops any active effects and turns off the selected strip

> **Note:** Strips that don't support color (like single-channel PWM LEDs) show brightness controls only.

### LED Effects

If you have the [klipper-led_effect](https://github.com/julianschill/klipper-led_effect) plugin installed, an effects section appears with cards for each available effect. Effects are filtered to show only those that target the currently selected strip. The active effect is highlighted, and a **Stop All Effects** button lets you kill all running effects at once.

### WLED Controls

For WLED network strips:

- **On/Off toggle**: Turn the WLED strip on or off
- **Brightness slider**: 0–100%
- **Presets**: Buttons for each WLED preset — fetched directly from your WLED device, with the active preset highlighted

### Macro Device Controls

Custom macro devices you've configured in [LED Settings](settings.md#led-settings) appear here with controls matching their type:

- **On/Off devices**: Separate "Turn On" and "Turn Off" buttons
- **Toggle devices**: A single "Toggle" button
- **Preset devices**: Named buttons for each preset action

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
