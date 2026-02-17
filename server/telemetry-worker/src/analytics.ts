// SPDX-License-Identifier: GPL-3.0-or-later
// Maps telemetry events to Analytics Engine data points for real-time querying.

export interface AnalyticsEngineDataPoint {
  blobs?: string[];
  doubles?: number[];
  indexes?: string[];
}

/**
 * Maps a raw telemetry event to an Analytics Engine data point.
 * Each event type has a specific schema for blobs/doubles to enable
 * efficient SQL queries via the Analytics Engine SQL API.
 */
export function mapEventToDataPoint(
  event: Record<string, unknown>,
): AnalyticsEngineDataPoint {
  const eventType = String(event.event ?? "unknown");
  const deviceId = String(event.device_id ?? "");

  // Support both flat (schema v1) and nested (schema v2) field access
  const app = (event.app ?? {}) as Record<string, unknown>;
  const printer = (event.printer ?? {}) as Record<string, unknown>;
  const host = (event.host ?? {}) as Record<string, unknown>;

  if (eventType === "session") {
    return {
      indexes: ["session"],
      blobs: [
        deviceId,
        String(app.version ?? event.app_version ?? event.version ?? ""),
        String(app.platform ?? event.app_platform ?? event.platform ?? ""),
        String(printer.detected_model ?? event.printer_model ?? ""),
        String(printer.kinematics ?? event.kinematics ?? ""),
        String(app.display ?? event.display ?? ""),
        String(app.locale ?? event.locale ?? ""),
        String(app.theme ?? event.theme ?? ""),
        String(host.arch ?? event.arch ?? ""),
        "",
        "",
        "",
      ],
      doubles: [
        Number(host.ram_total_mb ?? event.ram_total_mb ?? 0),
        Number(host.cpu_cores ?? event.cpu_cores ?? 0),
        Number(printer.extruder_count ?? event.extruder_count ?? 0),
        0,
        0,
        0,
        0,
        0,
      ],
    };
  }

  if (eventType === "print_outcome") {
    return {
      indexes: ["print_outcome"],
      blobs: [
        deviceId,
        String(event.outcome ?? ""),
        String(event.filament_type ?? ""),
        String(app.version ?? event.app_version ?? event.version ?? ""),
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
      ],
      doubles: [
        Number(event.duration_sec ?? 0),
        Number(event.nozzle_temp ?? 0),
        Number(event.bed_temp ?? 0),
        Number(event.filament_used_mm ?? 0),
        Number(event.phases_completed ?? 0),
        0,
        0,
        0,
      ],
    };
  }

  if (eventType === "crash") {
    return {
      indexes: ["crash"],
      blobs: [
        deviceId,
        String(app.version ?? event.app_version ?? event.version ?? ""),
        String(event.signal_name ?? ""),
        String(app.platform ?? event.app_platform ?? event.platform ?? ""),
        String(event.load_base ?? ""),
        String(event.fault_code_name ?? ""),
        String(event.fault_addr ?? ""),
        "",
        "",
        "",
        "",
        "",
      ],
      doubles: [
        Number(event.uptime_sec ?? 0),
        Number(event.signal ?? event.signal_number ?? 0),
        Number(
          event.backtrace_depth ??
            (Array.isArray(event.backtrace) ? event.backtrace.length : 0),
        ),
        0,
        0,
        0,
        0,
        0,
      ],
    };
  }

  // Unknown event type — still write basic info for discoverability
  return {
    indexes: [eventType],
    blobs: [deviceId, eventType, "", "", "", "", "", "", "", "", "", ""],
    doubles: [0, 0, 0, 0, 0, 0, 0, 0],
  };
}
