// SPDX-License-Identifier: GPL-3.0-or-later
// HelixScreen telemetry ingestion worker — stores batched events in R2.

import { mapEventToDataPoint } from "./analytics";
import {
  executeQuery,
  parseRange,
  overviewQueries,
  adoptionQueries,
  printsQueries,
  crashesQueries,
  crashListQuery,
  releasesQueries,
  type QueryConfig,
} from "./queries";

// Rate limiting binding type (added in @cloudflare/workers-types after our pinned version)
interface RateLimiter {
  limit(options: { key: string }): Promise<{ success: boolean }>;
}

// Analytics Engine dataset binding type
interface AnalyticsEngineDataset {
  writeDataPoint(point: {
    blobs?: string[];
    doubles?: number[];
    indexes?: string[];
  }): void;
}

interface Env {
  TELEMETRY_BUCKET: R2Bucket;
  INGEST_API_KEY: string; // Cloudflare secret: wrangler secret put INGEST_API_KEY
  ADMIN_API_KEY: string; // Cloudflare secret: wrangler secret put ADMIN_API_KEY (for analytics)
  INGEST_LIMITER: RateLimiter; // Rate limiting binding (see wrangler.toml)
  TELEMETRY_ANALYTICS?: AnalyticsEngineDataset; // Analytics Engine (see wrangler.toml)
  CLOUDFLARE_ACCOUNT_ID: string; // Set in wrangler.toml [vars]
  HELIX_ANALYTICS_READ_TOKEN?: string; // Cloudflare secret: wrangler secret put HELIX_ANALYTICS_READ_TOKEN
}

const CORS_HEADERS: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, X-API-Key",
};

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json", ...CORS_HEADERS },
  });
}

function randomHex(bytes: number): string {
  const buf = new Uint8Array(bytes);
  crypto.getRandomValues(buf);
  return Array.from(buf, (b) => b.toString(16).padStart(2, "0")).join("");
}

function validateEvent(evt: unknown, index: number): string | null {
  if (typeof evt !== "object" || evt === null || Array.isArray(evt)) {
    return `event[${index}]: must be an object`;
  }
  const e = evt as Record<string, unknown>;
  if (typeof e.schema_version !== "number") {
    return `event[${index}]: schema_version must be a number`;
  }
  if (typeof e.event !== "string" || e.event.length === 0) {
    return `event[${index}]: event must be a non-empty string`;
  }
  if (typeof e.device_id !== "string" || e.device_id.length === 0) {
    return `event[${index}]: device_id must be a non-empty string`;
  }
  if (typeof e.timestamp !== "string" || e.timestamp.length === 0) {
    return `event[${index}]: timestamp must be a non-empty string`;
  }
  return null;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    // CORS preflight
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS_HEADERS });
    }

    // Health check
    if (url.pathname === "/health" && request.method === "GET") {
      return json({ status: "healthy" });
    }

    // Event ingestion
    if (url.pathname === "/v1/events") {
      if (request.method !== "POST") {
        return json({ error: "Method not allowed" }, 405);
      }

      // Verify API key
      const apiKey = request.headers.get("x-api-key") ?? "";
      if (!env.INGEST_API_KEY || apiKey !== env.INGEST_API_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }

      // Rate limiting (per client IP)
      const clientIP = request.headers.get("CF-Connecting-IP") || "unknown";
      const { success } = await env.INGEST_LIMITER.limit({ key: clientIP });
      if (!success) {
        return json({ error: "Rate limit exceeded" }, 429);
      }

      const contentType = request.headers.get("content-type") ?? "";
      if (!contentType.includes("application/json")) {
        return json({ error: "Content-Type must be application/json" }, 400);
      }

      let body: unknown;
      try {
        body = await request.json();
      } catch {
        return json({ error: "Invalid JSON body" }, 400);
      }

      if (!Array.isArray(body)) {
        return json({ error: "Body must be a JSON array of events" }, 400);
      }
      if (body.length === 0 || body.length > 20) {
        return json({ error: "Batch must contain 1-20 events" }, 400);
      }

      for (let i = 0; i < body.length; i++) {
        const err = validateEvent(body[i], i);
        if (err) return json({ error: err }, 400);
      }

      // Build R2 key: events/YYYY/MM/DD/{epochMs}-{random6hex}.json
      const now = new Date();
      const yyyy = now.getUTCFullYear();
      const mm = String(now.getUTCMonth() + 1).padStart(2, "0");
      const dd = String(now.getUTCDate()).padStart(2, "0");
      const key = `events/${yyyy}/${mm}/${dd}/${now.getTime()}-${randomHex(3)}.json`;

      try {
        await env.TELEMETRY_BUCKET.put(key, JSON.stringify(body), {
          httpMetadata: { contentType: "application/json" },
        });
      } catch {
        return json({ error: "Failed to store events" }, 500);
      }

      // Dual-write to Analytics Engine (fire-and-forget, never blocks response)
      if (env.TELEMETRY_ANALYTICS) {
        for (const evt of body) {
          try {
            env.TELEMETRY_ANALYTICS.writeDataPoint(
              mapEventToDataPoint(evt as Record<string, unknown>),
            );
          } catch {
            // Analytics Engine failure must not affect ingestion
          }
        }
      }

      return json({ status: "ok", stored: body.length });
    }

    // Event listing — returns keys for a given date prefix (for analytics pull)
    // GET /v1/events/list?prefix=events/2026/01/15/&cursor=...
    // Requires ADMIN_API_KEY (NOT the ingest key baked into client binaries)
    if (url.pathname === "/v1/events/list" && request.method === "GET") {
      const apiKey = request.headers.get("x-api-key") ?? "";
      if (!env.ADMIN_API_KEY || apiKey !== env.ADMIN_API_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }

      const prefix = url.searchParams.get("prefix") ?? "events/";
      if (!prefix.startsWith("events/")) {
        return json({ error: "Prefix must start with events/" }, 400);
      }
      const cursor = url.searchParams.get("cursor") ?? undefined;
      const limit = Math.max(1, Math.min(
        parseInt(url.searchParams.get("limit") ?? "1000", 10) || 1000,
        1000,
      ));

      const listed = await env.TELEMETRY_BUCKET.list({ prefix, cursor, limit });
      return json({
        keys: listed.objects.map((obj) => ({
          key: obj.key,
          size: obj.size,
          uploaded: obj.uploaded.toISOString(),
        })),
        truncated: listed.truncated,
        cursor: listed.truncated ? listed.cursor : undefined,
      });
    }

    // Event download — stream a specific event file
    // GET /v1/events/get?key=events/2026/01/15/1234567890-abc123.json
    // Requires ADMIN_API_KEY (NOT the ingest key baked into client binaries)
    if (url.pathname === "/v1/events/get" && request.method === "GET") {
      const apiKey = request.headers.get("x-api-key") ?? "";
      if (!env.ADMIN_API_KEY || apiKey !== env.ADMIN_API_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }

      const key = url.searchParams.get("key");
      if (!key || !key.startsWith("events/") || !key.endsWith(".json")) {
        return json({ error: "Invalid key" }, 400);
      }

      const obj = await env.TELEMETRY_BUCKET.get(key);
      if (!obj) {
        return json({ error: "Not found" }, 404);
      }

      return new Response(obj.body, {
        headers: {
          "Content-Type": "application/json",
          ...CORS_HEADERS,
        },
      });
    }

    // ---------- Dashboard endpoints (all require ADMIN_API_KEY) ----------

    if (url.pathname.startsWith("/v1/dashboard/")) {
      if (request.method !== "GET") {
        return json({ error: "Method not allowed" }, 405);
      }

      const apiKey = request.headers.get("x-api-key") ?? "";
      if (!env.ADMIN_API_KEY || apiKey !== env.ADMIN_API_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }

      if (!env.CLOUDFLARE_ACCOUNT_ID || !env.HELIX_ANALYTICS_READ_TOKEN) {
        return json({ error: "Analytics Engine not configured" }, 503);
      }

      const queryConfig: QueryConfig = {
        accountId: env.CLOUDFLARE_ACCOUNT_ID,
        apiToken: env.HELIX_ANALYTICS_READ_TOKEN,
      };

      const range = url.searchParams.get("range");
      const days = parseRange(range);

      try {
        // GET /v1/dashboard/overview
        if (url.pathname === "/v1/dashboard/overview") {
          const queries = overviewQueries(days);
          const [devicesRes, totalRes, rateRes, printRes, timeRes] =
            await Promise.all(queries.map((q) => executeQuery(queryConfig, q)));

          const devicesData = devicesRes as { data: Array<{ active_devices: number }> };
          const totalData = totalRes as { data: Array<{ total_events: number }> };
          const rateData = rateRes as { data: Array<{ crash_count: number; session_count: number }> };
          const printData = printRes as { data: Array<{ successes: number; total: number }> };
          const timeData = timeRes as { data: Array<{ date: string; count: number }> };

          const crashRow = rateData.data?.[0] ?? { crash_count: 0, session_count: 0 };
          const printRow = printData.data?.[0] ?? { successes: 0, total: 0 };

          return json({
            active_devices: devicesData.data?.[0]?.active_devices ?? 0,
            total_events: totalData.data?.[0]?.total_events ?? 0,
            crash_rate: crashRow.session_count > 0 ? crashRow.crash_count / crashRow.session_count : 0,
            print_success_rate: printRow.total > 0 ? printRow.successes / printRow.total : 0,
            events_over_time: (timeData.data ?? []).map((r) => ({
              date: r.date,
              count: r.count,
            })),
          });
        }

        // GET /v1/dashboard/adoption
        if (url.pathname === "/v1/dashboard/adoption") {
          const queries = adoptionQueries(days);
          const [platformsRes, versionsRes, modelsRes, kinematicsRes] =
            await Promise.all(queries.map((q) => executeQuery(queryConfig, q)));

          const toList = (res: unknown) => {
            const d = res as { data: Array<{ name: string; count: number }> };
            return (d.data ?? []).map((r) => ({ name: r.name, count: r.count }));
          };

          return json({
            platforms: toList(platformsRes),
            versions: toList(versionsRes),
            printer_models: toList(modelsRes),
            kinematics: toList(kinematicsRes),
          });
        }

        // GET /v1/dashboard/prints
        if (url.pathname === "/v1/dashboard/prints") {
          const queries = printsQueries(days);
          const [rateTimeRes, filamentRes, avgDurRes] =
            await Promise.all(queries.map((q) => executeQuery(queryConfig, q)));

          const rateTimeData = rateTimeRes as { data: Array<{ date: string; rate: number; total: number }> };
          const filamentData = filamentRes as { data: Array<{ type: string; success_rate: number; count: number }> };
          const avgDurData = avgDurRes as { data: Array<{ avg_duration_sec: number }> };

          return json({
            success_rate_over_time: (rateTimeData.data ?? []).map((r) => ({
              date: r.date,
              rate: r.rate,
              total: r.total,
            })),
            by_filament: (filamentData.data ?? []).map((r) => ({
              type: r.type,
              success_rate: r.success_rate,
              count: r.count,
            })),
            avg_duration_sec: avgDurData.data?.[0]?.avg_duration_sec ?? 0,
          });
        }

        // GET /v1/dashboard/crashes
        if (url.pathname === "/v1/dashboard/crashes") {
          const queries = crashesQueries(days);
          const [crashByVerRes, sessionByVerRes, signalRes, uptimeRes] =
            await Promise.all(queries.map((q) => executeQuery(queryConfig, q)));

          const crashByVer = crashByVerRes as { data: Array<{ ver: string; crash_count: number }> };
          const sessionByVer = sessionByVerRes as { data: Array<{ ver: string; session_count: number }> };
          const signalData = signalRes as { data: Array<{ signal: string; count: number }> };
          const uptimeData = uptimeRes as { data: Array<{ avg_uptime_sec: number }> };

          // Build session count lookup
          const sessionMap = new Map<string, number>();
          for (const row of sessionByVer.data ?? []) {
            sessionMap.set(row.ver, row.session_count);
          }

          return json({
            by_version: (crashByVer.data ?? []).map((r) => {
              const sessionCount = sessionMap.get(r.ver) ?? 0;
              return {
                version: r.ver,
                crash_count: r.crash_count,
                session_count: sessionCount,
                rate: sessionCount > 0 ? r.crash_count / sessionCount : 0,
              };
            }),
            by_signal: (signalData.data ?? []).map((r) => ({
              signal: r.signal,
              count: r.count,
            })),
            avg_uptime_sec: uptimeData.data?.[0]?.avg_uptime_sec ?? 0,
          });
        }

        // GET /v1/dashboard/crash-list?range=30d&limit=50
        if (url.pathname === "/v1/dashboard/crash-list") {
          const limit = Math.max(1, Math.min(
            parseInt(url.searchParams.get("limit") ?? "50", 10) || 50,
            200,
          ));
          const sql = crashListQuery(days, limit);
          const result = await executeQuery(queryConfig, sql);
          const data = result as {
            data: Array<{
              timestamp: string;
              device_id: string;
              ver: string;
              sig: string;
              platform: string;
              uptime_sec: number;
            }>;
          };

          return json({
            crashes: (data.data ?? []).map((r) => ({
              timestamp: r.timestamp,
              device_id: r.device_id,
              version: r.ver,
              signal: r.sig,
              platform: r.platform,
              uptime_sec: r.uptime_sec,
            })),
          });
        }

        // GET /v1/dashboard/releases?versions=v0.9.18,v0.9.19
        if (url.pathname === "/v1/dashboard/releases") {
          const versionsParam = url.searchParams.get("versions");
          if (!versionsParam) {
            return json({ error: "versions parameter required" }, 400);
          }
          const versions = versionsParam.split(",").map((v) => v.trim()).filter(Boolean);
          if (versions.length === 0 || versions.length > 20) {
            return json({ error: "Provide 1-20 comma-separated versions" }, 400);
          }

          const queries = releasesQueries(versions);
          const [statsRes, printStatsRes] =
            await Promise.all(queries.map((q) => executeQuery(queryConfig, q)));

          const statsData = statsRes as {
            data: Array<{ ver: string; total_sessions: number; total_crashes: number; active_devices: number }>;
          };
          const printStatsData = printStatsRes as {
            data: Array<{ ver: string; print_successes: number; print_total: number }>;
          };

          // Build print stats lookup
          const printMap = new Map<string, { successes: number; total: number }>();
          for (const row of printStatsData.data ?? []) {
            printMap.set(row.ver, { successes: row.print_successes, total: row.print_total });
          }

          return json({
            versions: (statsData.data ?? []).map((r) => {
              const prints = printMap.get(r.ver) ?? { successes: 0, total: 0 };
              return {
                version: r.ver,
                active_devices: r.active_devices,
                crash_rate: r.total_sessions > 0 ? r.total_crashes / r.total_sessions : 0,
                print_success_rate: prints.total > 0 ? prints.successes / prints.total : 0,
                total_sessions: r.total_sessions,
                total_crashes: r.total_crashes,
              };
            }),
          });
        }

        return json({ error: "Not found" }, 404);
      } catch (err) {
        const message = err instanceof Error ? err.message : "Unknown error";
        return json({ error: `Analytics query failed: ${message}` }, 502);
      }
    }

    // ---------- Backfill endpoint (admin only) ----------
    // POST /v1/admin/backfill — writes historical events to Analytics Engine
    if (url.pathname === "/v1/admin/backfill" && request.method === "POST") {
      const apiKey = request.headers.get("x-api-key") ?? "";
      if (!env.ADMIN_API_KEY || apiKey !== env.ADMIN_API_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }

      if (!env.TELEMETRY_ANALYTICS) {
        return json({ error: "Analytics Engine not configured" }, 503);
      }

      const contentType = request.headers.get("content-type") ?? "";
      if (!contentType.includes("application/json")) {
        return json({ error: "Content-Type must be application/json" }, 400);
      }

      let body: unknown;
      try {
        body = await request.json();
      } catch {
        return json({ error: "Invalid JSON body" }, 400);
      }

      if (typeof body !== "object" || body === null || Array.isArray(body)) {
        return json({ error: "Body must be an object with events array" }, 400);
      }

      const events = (body as Record<string, unknown>).events;
      if (!Array.isArray(events)) {
        return json({ error: "events must be an array" }, 400);
      }

      if (events.length === 0) {
        return json({ error: "events array must not be empty" }, 400);
      }

      let written = 0;
      for (const evt of events) {
        try {
          env.TELEMETRY_ANALYTICS.writeDataPoint(
            mapEventToDataPoint(evt as Record<string, unknown>),
          );
          written++;
        } catch {
          // Skip individual failures, continue backfilling
        }
      }

      return json({ status: "ok", written });
    }

    // Symbol map listing — returns available platforms for a version
    if (url.pathname.startsWith("/v1/symbols/")) {
      if (request.method !== "GET") {
        return json({ error: "Method not allowed" }, 405);
      }

      const version = url.pathname.replace("/v1/symbols/", "").replace(/\/+$/, "");
      if (!version || !/^[\d.]+[-\w.]*$/.test(version)) {
        return json({ error: "Invalid version format" }, 400);
      }

      const prefix = `symbols/v${version}/`;
      const listed = await env.TELEMETRY_BUCKET.list({ prefix });
      const platforms = listed.objects
        .map((obj) => obj.key.replace(prefix, "").replace(/\.sym$/, ""))
        .filter((p) => p.length > 0);

      return json({ version, platforms });
    }

    // Everything else
    return json({ error: "Not found" }, 404);
  },
} satisfies ExportedHandler<Env>;
