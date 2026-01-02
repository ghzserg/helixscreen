# LED Effects Plugin

A proof-of-concept HelixScreen plugin that demonstrates the plugin system's capabilities.

## Features

- Injects an LED control widget into the home panel
- Toggle LED on/off via Klipper's `SET_LED` gcode command
- Demonstrates plugin lifecycle (init/deinit)
- Shows event subscription (printer_connected, print_started, print_completed)
- Demonstrates subject registration for reactive UI binding

## Building

```bash
cd plugins/led-effects
make
```

The plugin will be built as `build/libhelix_led_effects.dylib` (macOS) or `build/libhelix_led_effects.so` (Linux).

## Testing

Run HelixScreen with the `--test` flag to use mock printer data:

```bash
./build/bin/helix-screen --test -p home -vv
```

You should see:
- "LED Effects plugin initializing..." in logs
- An "LED Control" widget in the home panel (below the status cards)
- "Toggle" button that sends SET_LED gcode commands

## File Structure

```
led-effects/
  manifest.json          # Plugin metadata and configuration
  src/
    led_effects_plugin.cpp  # Plugin implementation
  ui_xml/
    led_widget.xml       # XML layout for injected widget
  Makefile              # Build configuration
  README.md             # This file
```

## API Usage Examples

This plugin demonstrates several plugin API features:

### Subject Registration
```cpp
lv_subject_init_int(&s_led_mode, 0);
api->register_subject("led_effects.mode", &s_led_mode);
```

### Event Subscription
```cpp
api->on_event(events::PRINT_STARTED, [](const EventData& e) {
    // React to print start
});
```

### Widget Injection
```cpp
api->inject_widget("home_widget_area", "led_widget", callbacks);
```

### Gcode Execution
```cpp
moonraker->execute_gcode("SET_LED LED=chamber_light RED=1.0 GREEN=1.0 BLUE=1.0",
    on_success, on_error);
```

## LED Configuration

This plugin assumes an LED named `chamber_light` is configured in Klipper.
Modify the gcode in `led_effects_plugin.cpp` to match your printer's LED configuration.

Common LED names:
- `chamber_light` - Chamber illumination
- `chamber_led` - Alternative chamber LED
- `neopixel <name>` - NeoPixel strips

## License

GPL-3.0-or-later - See LICENSE file in repository root.
