# Settings: LED Settings

Tap **LED Settings** in the Printer section to open the LED configuration overlay. This is where you choose which lights HelixScreen controls and how they behave.

> **Tip:** To control your LEDs during a print, **long-press the lightbulb button** on the Home Panel to open the LED Control Overlay. See [Home Panel > LED Controls](../home-panel.md#led-controls) for details.

---

## Supported LED Types

HelixScreen auto-detects your LED hardware from Klipper and Moonraker. Four types of lighting are supported:

| Type | Examples | How It's Detected |
|------|----------|-------------------|
| **Klipper native strips** | Neopixel (WS2812, SK6812), Dotstar (APA102), PCA9632, GPIO LEDs | Automatically from your Klipper config |
| **WLED strips** | Network-attached WLED controllers | From Moonraker's WLED configuration |
| **LED effects** | Animated effects (breathing, rainbow, etc.) | Requires the [klipper-led_effect](https://github.com/julianschill/klipper-led_effect) plugin |
| **Macro devices** | Any Klipper macro that controls lights | User-configured (see [Macro Devices](#macro-devices) below) |

---

## LED Strip Selection

Multi-select chips show all detected strips. Tap a chip to toggle whether HelixScreen controls that strip. You can select multiple strips — they'll all respond to the lightbulb toggle on the Home Panel.

**If you don't see your LEDs here:**
- Make sure the LED is defined in your Klipper config (`printer.cfg`)
- For WLED, make sure it's configured in Moonraker (`moonraker.conf`)
- Restart HelixScreen after adding new LED hardware

---

## LED On At Start

Toggle this on to automatically turn your selected LEDs on when Klipper becomes ready. Useful for chamber lights that should always be on when the printer is powered up.

---

## Auto-State Lighting

When enabled, your LEDs automatically change based on what the printer is doing — no macros or manual control needed. This is great for visual status feedback: dim lights when idle, bright white while printing, green when a print finishes.

### Printer States

| State | When It Triggers |
|-------|-----------------|
| **Idle** | Klipper is ready, not printing |
| **Heating** | Extruder is heating up (target > 0, not yet printing) |
| **Printing** | Print is in progress |
| **Paused** | Print is paused |
| **Error** | Klipper error or shutdown |
| **Complete** | Print just finished |

### Action Types

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

### Example Setup

| State | Action | Setting |
|-------|--------|---------|
| Idle | Brightness | 30% (dim standby light) |
| Heating | Color | Red at 100% |
| Printing | Color | White at 100% |
| Paused | Effect | "breathing" |
| Error | Color | Red at 100% |
| Complete | Color | Green at 100% |

---

## Macro Devices

Macro devices let you control LEDs that aren't directly supported by Klipper's LED system — like relay-switched cabinet lights, custom G-code lighting macros, or multi-mode LED setups.

**Auto-discovery:** HelixScreen automatically finds Klipper macros with "led" or "light" in their name and makes them available in the macro dropdown lists.

### Device Types

| Type | Best For | Controls |
|------|----------|----------|
| **On/Off** | Lights with separate on and off macros | Two macros: one to turn on, one to turn off |
| **Toggle** | Lights with a single toggle macro | One macro that flips the state |
| **Preset** | Multi-mode lights (e.g., party mode, reading mode) | Multiple named presets, each mapped to a different macro |

### Adding a Macro Device

1. Tap the **+** button in the Macro Devices section
2. Enter a display name (e.g., "Cabinet Light")
3. Choose a device type
4. Select the appropriate macros from the dropdown(s)
5. Tap **Save**

**To edit or delete:** Tap the **pencil icon** to modify, or the **trash icon** to remove.

**Example:** If you have macros `LIGHTS_CABINET_ON` and `LIGHTS_CABINET_OFF` in your Klipper config, create an **On/Off** device named "Cabinet Light" and map each macro accordingly. It will appear as a controllable device in the LED Control Overlay.

---

## Setting Up Different LED Types

### Klipper Native Strips (Neopixel, Dotstar, etc.)

These work out of the box. Define them in your `printer.cfg`:

```ini
[neopixel chamber_light]
pin: PA8
chain_count: 24
color_order: GRB
```

Restart Klipper, then open **Settings > LED Settings** — the strip appears automatically. Select it and you'll get full color and brightness control.

### WLED Network Strips

WLED strips are network-attached LED controllers managed outside of Klipper. Configure them in `moonraker.conf`:

```ini
[wled strip_name]
address: 192.168.1.100
```

After restarting Moonraker, the WLED strip appears in LED Settings. You get on/off toggle, brightness, and access to all WLED presets you've configured in the WLED web interface.

### LED Effects (Animated Patterns)

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

### Macro-Controlled Lights

For lights controlled via G-code macros (relay-switched enclosure lights, Klipper macros wrapping custom commands, etc.):

1. Define your macros in Klipper (include "led" or "light" in the name for auto-discovery)
2. Go to **Settings > LED Settings > Macro Devices**
3. Create a device and map the appropriate macros
4. The device appears in the LED Control Overlay alongside your other strips

---

[Back to Settings](../settings.md) | [Prev: Help & About](help-about.md)
