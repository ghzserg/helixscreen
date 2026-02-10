// SPDX-License-Identifier: GPL-3.0-or-later
// HelixScreen telemetry ingestion worker — stores batched events in R2.

interface Env {
  TELEMETRY_BUCKET: R2Bucket;
  INGEST_API_KEY: string; // Cloudflare secret: wrangler secret put INGEST_API_KEY
}

const CORS_HEADERS: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
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

      return json({ status: "ok", stored: body.length });
    }

    // Everything else
    return json({ error: "Not found" }, 404);
  },
} satisfies ExportedHandler<Env>;
