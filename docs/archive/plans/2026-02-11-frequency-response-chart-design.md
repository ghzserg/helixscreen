# Frequency Response Chart Design (Chunk 3b)

## Overview

Add a frequency response chart to the Input Shaper results page, showing the raw vibration spectrum and optional per-shaper filtered overlays. The chart is gated on **data availability** (can we read the CSV?), not hardware capability.

## Data Source

Klipper's `SHAPER_CALIBRATE` writes a CSV to `/tmp/calibration_data_{axis}_{timestamp}.csv` on the printer's filesystem. The CSV path is already captured in `InputShaperResult.csv_path`.

### CSV Format

```
freq, psd_x, psd_y, psd_z, psd_xyz, shapers:, zv(59.0), mzv(53.8), ei(56.2), 2hump_ei(71.8), 3hump_ei(89.6)
5.0,  1.234e-03, 2.345e-03, 1.123e-03, 4.702e-03, , 0.001, 0.001, 0.001, 0.000, 0.000
...
```

- ~132 frequency bins (5-200 Hz range)
- Raw PSD columns: `psd_x`, `psd_y`, `psd_z`, `psd_xyz`
- Per-shaper filtered response: one column per shaper type
- Total parsed size: ~5.3 KB in memory

### Data Pipeline

1. **Local file read** (primary): Read CSV directly from filesystem. Works when HelixScreen runs on the same machine as Klipper (most common deployment).
2. **Moonraker file API** (fallback): Download via `server/files/` endpoint if `/tmp/` isn't accessible locally. Needs investigation — Moonraker may not expose `/tmp/`.
3. **Graceful skip**: If neither method works, omit the chart. The comparison table provides all the essential data.

### Parsed Data Storage

Extend `InputShaperResult` (or a new struct) to hold:
- `std::vector<float> frequencies` — frequency bins (X axis)
- `std::vector<float> raw_psd` — raw power spectral density (use `psd_x` or `psd_y` depending on axis)
- `std::vector<ShaperResponseCurve> shaper_curves` — per-shaper filtered response data, each with name + frequency values

## Hardware Tiers — No Gating

The temp graph already runs 1,200 points with live updates on the AD5M (EMBEDDED tier, 108 MB RAM). A static 132-point chart drawn once is lighter. All tiers get the chart.

Point budget per tier (downsampled from ~132 source points):
- **EMBEDDED**: 50 points
- **BASIC** (Pi 3): 80 points
- **STANDARD** (Pi 4+): 132 points (no downsampling)

Memory impact: ~10 KB total including widget overhead. Negligible on all platforms.

## UI Layout

The chart sits between the "Calibration Complete!" header and the per-axis result cards on the results page. One chart per axis that has results.

```
+----------------------------------+
| Calibration Complete!            |
+----------------------------------+
| X Axis          MZV @ 53.8 Hz   |
| [=== frequency response chart ==]|
| [ZV] [MZV] [EI] [2H_EI] [3H_EI]|
| Type  Freq   Vibration  MaxAccel |
| ZV    59.0   5.2% Good    13400  |
| MZV * 53.8   1.6% Exc     4000  | <-- highlighted
| ...                              |
| * Good balance of speed and...   |
+----------------------------------+
|          [ Save ]                |
+----------------------------------+
```

### Chart Behavior

- Raw PSD shown as a filled area (semi-transparent) — always visible
- Peak frequency marked with a vertical indicator
- Shaper overlay chips below the chart: `[ZV] [MZV] [EI] [2HUMP_EI] [3HUMP_EI]`
- Recommended shaper chip starts pre-selected (its line is visible by default)
- Tap a chip to toggle its filtered response line on/off
- Multiple shapers can be overlaid simultaneously for comparison
- Each shaper line uses a distinct color

### Chart Widget

Reuse the existing `ui_frequency_response_chart` widget. It already supports:
- Multi-series (up to 8)
- Tier-aware downsampling
- Peak marking
- Configurable frequency/amplitude ranges

Update the EMBEDDED tier to allow charts (currently disabled — too conservative given temp graph precedent).

### Chip Toggle Implementation

Declarative approach:
- 5 int subjects per axis: `is_x_shaper_overlay_zv`, `is_x_shaper_overlay_mzv`, etc. (0=off, 1=on)
- XML chips with `bind_state_if_eq` for checked visual state
- XML `event_cb` for click handlers
- C++ handler toggles subject and adds/removes chart series

## Conditional Display

The chart section is only shown when frequency response data is available:
- Add `is_x_has_freq_data` / `is_y_has_freq_data` int subjects (0 or 1)
- XML uses `bind_flag_if_eq` to hide chart container when data unavailable
- Comparison table always shows regardless of chart availability

## Implementation Steps

1. **CSV parser**: New utility to parse Klipper calibration CSV into structured data
2. **Data pipeline**: After calibration result, attempt local file read, populate freq_response
3. **Chart integration**: Wire parsed data into existing `ui_frequency_response_chart` widget
4. **Chip toggles**: Declarative XML chips + subjects for shaper overlay control
5. **EMBEDDED tier update**: Remove chart_mode=false restriction, use 50-point budget
6. **Mock data**: Generate synthetic frequency response CSV for testing
7. **Tests**: CSV parser tests, chart data population tests
