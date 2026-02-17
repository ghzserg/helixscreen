// SPDX-License-Identifier: GPL-3.0-or-later
// SQL query builders for Analytics Engine dashboard endpoints.

export interface QueryConfig {
  accountId: string;
  apiToken: string;
}

/**
 * Parse a range string like "7d", "30d", "90d" into a SQL timestamp filter.
 * Returns the number of days, clamped to [1, 90].
 */
export function parseRange(range: string | null): number {
  if (!range) return 30;
  const match = range.match(/^(\d+)d$/);
  if (!match) return 30;
  const days = parseInt(match[1], 10);
  return Math.max(1, Math.min(days, 90));
}

/**
 * Execute a SQL query against the Analytics Engine SQL API.
 */
export async function executeQuery(
  config: QueryConfig,
  sql: string,
): Promise<unknown> {
  const url = `https://api.cloudflare.com/client/v4/accounts/${config.accountId}/analytics_engine/sql`;
  const res = await fetch(url, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${config.apiToken}`,
      "Content-Type": "text/plain",
    },
    body: sql,
  });

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Analytics Engine SQL API error (${res.status}): ${text}`);
  }

  return res.json();
}

// SQL query builders for each dashboard endpoint

export function overviewQueries(days: number): string[] {
  const dataset = "helixscreen_telemetry";
  return [
    // Active devices (unique device_ids from sessions)
    `SELECT count(DISTINCT blob1) as active_devices FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'session'`,
    // Total events
    `SELECT count() as total_events FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY`,
    // Crash rate (crashes / sessions)
    `SELECT
      sumIf(1, index1 = 'crash') as crash_count,
      sumIf(1, index1 = 'session') as session_count
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY`,
    // Print success rate
    `SELECT
      sumIf(1, blob2 = 'success') as successes,
      count() as total
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'print_outcome'`,
    // Events over time
    `SELECT
      toDate(timestamp) as date,
      count() as count
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY
    GROUP BY date
    ORDER BY date`,
  ];
}

export function adoptionQueries(days: number): string[] {
  const dataset = "helixscreen_telemetry";
  return [
    // Platforms
    `SELECT blob3 as name, count(DISTINCT blob1) as count FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'session' AND blob3 != '' GROUP BY name ORDER BY count DESC`,
    // Versions
    `SELECT blob2 as name, count(DISTINCT blob1) as count FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'session' AND blob2 != '' GROUP BY name ORDER BY count DESC`,
    // Printer models
    `SELECT blob4 as name, count(DISTINCT blob1) as count FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'session' AND blob4 != '' GROUP BY name ORDER BY count DESC`,
    // Kinematics
    `SELECT blob5 as name, count(DISTINCT blob1) as count FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'session' AND blob5 != '' GROUP BY name ORDER BY count DESC`,
  ];
}

export function printsQueries(days: number): string[] {
  const dataset = "helixscreen_telemetry";
  return [
    // Success rate over time
    `SELECT
      toDate(timestamp) as date,
      sumIf(1, blob2 = 'success') / count() as rate,
      count() as total
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'print_outcome'
    GROUP BY date
    ORDER BY date`,
    // By filament type
    `SELECT
      blob3 as type,
      sumIf(1, blob2 = 'success') / count() as success_rate,
      count() as count
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'print_outcome' AND blob3 != ''
    GROUP BY type
    ORDER BY count DESC`,
    // Average duration
    `SELECT avg(double1) as avg_duration_sec FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'print_outcome'`,
  ];
}

export function crashesQueries(days: number): string[] {
  const dataset = "helixscreen_telemetry";
  return [
    // By version (crash count + session count for rate)
    `SELECT
      blob2 as ver,
      count() as crash_count
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'crash' AND blob2 != ''
    GROUP BY ver
    ORDER BY crash_count DESC`,
    // Session counts by version (for crash rate denominator)
    `SELECT
      blob2 as ver,
      count() as session_count
    FROM ${dataset}
    WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'session' AND blob2 != ''
    GROUP BY ver`,
    // By signal
    `SELECT blob3 as signal, count() as count FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'crash' AND blob3 != '' GROUP BY signal ORDER BY count DESC`,
    // Average uptime
    `SELECT avg(double1) as avg_uptime_sec FROM ${dataset} WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'crash'`,
  ];
}

export function crashListQuery(days: number, limit: number): string {
  const dataset = "helixscreen_telemetry";
  return `SELECT
    timestamp,
    blob1 as device_id,
    blob2 as ver,
    blob3 as sig,
    blob4 as platform,
    double1 as uptime_sec
  FROM ${dataset}
  WHERE timestamp >= NOW() - INTERVAL '${days}' DAY AND index1 = 'crash'
  ORDER BY timestamp DESC
  LIMIT ${limit}`;
}

export function releasesQueries(versions: string[]): string[] {
  const dataset = "helixscreen_telemetry";
  // Clean version strings — strip 'v' prefix if present for matching
  const cleanVersions = versions.map((v) => v.replace(/^v/, ""));
  const versionList = cleanVersions.map((v) => `'${v}'`).join(", ");
  return [
    // Per-version stats: sessions, crashes, active devices
    `SELECT
      blob2 as ver,
      sumIf(1, index1 = 'session') as total_sessions,
      sumIf(1, index1 = 'crash') as total_crashes,
      countIf(DISTINCT blob1, index1 = 'session') as active_devices
    FROM ${dataset}
    WHERE blob2 IN (${versionList})
    GROUP BY ver`,
    // Per-version print stats
    `SELECT
      blob4 as ver,
      sumIf(1, blob2 = 'success') as print_successes,
      count() as print_total
    FROM ${dataset}
    WHERE index1 = 'print_outcome' AND blob4 IN (${versionList})
    GROUP BY ver`,
  ];
}
