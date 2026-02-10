// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for telemetry worker endpoints.

import { describe, it, expect, vi, beforeEach } from "vitest";
import worker from "../index";

// ---------- Mock R2 helpers ----------

interface StoredObject {
  key: string;
  body: string;
  size: number;
  uploaded: Date;
  httpMetadata?: Record<string, string>;
}

function createMockBucket() {
  const storage = new Map<string, StoredObject>();

  return {
    _storage: storage,

    async put(key: string, value: string, opts?: { httpMetadata?: Record<string, string> }) {
      storage.set(key, {
        key,
        body: value,
        size: value.length,
        uploaded: new Date(),
        httpMetadata: opts?.httpMetadata,
      });
    },

    async get(key: string) {
      const obj = storage.get(key);
      if (!obj) return null;
      return {
        key: obj.key,
        size: obj.size,
        uploaded: obj.uploaded,
        body: new ReadableStream({
          start(controller) {
            controller.enqueue(new TextEncoder().encode(obj.body));
            controller.close();
          },
        }),
      };
    },

    async list(opts?: { prefix?: string; cursor?: string; limit?: number }) {
      const prefix = opts?.prefix ?? "";
      const limit = opts?.limit ?? 1000;
      const objects = Array.from(storage.values())
        .filter((o) => o.key.startsWith(prefix))
        .slice(0, limit)
        .map((o) => ({ key: o.key, size: o.size, uploaded: o.uploaded }));
      return { objects, truncated: false, cursor: undefined };
    },
  };
}

// ---------- Test env factory ----------

function createEnv(overrides?: Partial<{ INGEST_API_KEY: string; ADMIN_API_KEY: string; TELEMETRY_BUCKET: ReturnType<typeof createMockBucket> }>) {
  return {
    INGEST_API_KEY: "test-ingest-key",
    ADMIN_API_KEY: "test-admin-key",
    TELEMETRY_BUCKET: createMockBucket(),
    ...overrides,
  };
}

// ---------- Request helpers ----------

function makeRequest(url: string, init?: RequestInit): Request {
  return new Request(`https://telemetry.helixscreen.org${url}`, init);
}

function validEvent(overrides?: Record<string, unknown>) {
  return {
    schema_version: 1,
    event: "app.started",
    device_id: "device-abc-123",
    timestamp: "2026-01-15T10:30:00Z",
    ...overrides,
  };
}

function ingestRequest(body: unknown, apiKey = "test-ingest-key") {
  return makeRequest("/v1/events", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-API-Key": apiKey,
    },
    body: JSON.stringify(body),
  });
}

// ---------- Tests ----------

describe("GET /health", () => {
  it("returns healthy status", async () => {
    const res = await worker.fetch(makeRequest("/health"), createEnv());
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data).toEqual({ status: "healthy" });
  });

  it("includes CORS headers", async () => {
    const res = await worker.fetch(makeRequest("/health"), createEnv());
    expect(res.headers.get("Access-Control-Allow-Origin")).toBe("*");
  });
});

describe("OPTIONS (CORS preflight)", () => {
  it("returns 204 with CORS headers", async () => {
    const res = await worker.fetch(
      makeRequest("/v1/events", { method: "OPTIONS" }),
      createEnv(),
    );
    expect(res.status).toBe(204);
    expect(res.headers.get("Access-Control-Allow-Methods")).toContain("POST");
    expect(res.headers.get("Access-Control-Allow-Headers")).toContain("X-API-Key");
  });

  it("works on any path", async () => {
    const res = await worker.fetch(
      makeRequest("/any/random/path", { method: "OPTIONS" }),
      createEnv(),
    );
    expect(res.status).toBe(204);
  });
});

describe("404 for unknown routes", () => {
  it("returns 404 for unknown GET path", async () => {
    const res = await worker.fetch(makeRequest("/nonexistent"), createEnv());
    expect(res.status).toBe(404);
    const data = await res.json();
    expect(data.error).toBe("Not found");
  });
});

describe("POST /v1/events (ingestion)", () => {
  let env: ReturnType<typeof createEnv>;

  beforeEach(() => {
    env = createEnv();
  });

  // -- Auth --

  it("rejects request with no API key", async () => {
    const req = makeRequest("/v1/events", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify([validEvent()]),
    });
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(401);
  });

  it("rejects request with wrong API key", async () => {
    const res = await worker.fetch(ingestRequest([validEvent()], "wrong-key"), env);
    expect(res.status).toBe(401);
  });

  it("rejects ADMIN_API_KEY on ingest endpoint", async () => {
    const res = await worker.fetch(ingestRequest([validEvent()], "test-admin-key"), env);
    expect(res.status).toBe(401);
  });

  // -- Method --

  it("rejects GET method", async () => {
    const req = makeRequest("/v1/events", {
      method: "GET",
      headers: { "X-API-Key": "test-ingest-key" },
    });
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(405);
  });

  // -- Content-Type --

  it("rejects non-JSON content type", async () => {
    const req = makeRequest("/v1/events", {
      method: "POST",
      headers: {
        "Content-Type": "text/plain",
        "X-API-Key": "test-ingest-key",
      },
      body: JSON.stringify([validEvent()]),
    });
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("application/json");
  });

  // -- Body validation --

  it("rejects invalid JSON", async () => {
    const req = makeRequest("/v1/events", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-API-Key": "test-ingest-key",
      },
      body: "not json{{{",
    });
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("Invalid JSON");
  });

  it("rejects non-array body", async () => {
    const res = await worker.fetch(ingestRequest({ not: "an array" }), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("array");
  });

  it("rejects empty array", async () => {
    const res = await worker.fetch(ingestRequest([]), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("1-20");
  });

  it("rejects batch larger than 20", async () => {
    const events = Array.from({ length: 21 }, () => validEvent());
    const res = await worker.fetch(ingestRequest(events), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("1-20");
  });

  // -- Schema validation --

  it("rejects event missing schema_version", async () => {
    const res = await worker.fetch(
      ingestRequest([validEvent({ schema_version: undefined })]),
      env,
    );
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("schema_version");
  });

  it("rejects event with non-number schema_version", async () => {
    const res = await worker.fetch(
      ingestRequest([validEvent({ schema_version: "1" })]),
      env,
    );
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("schema_version");
  });

  it("rejects event missing event name", async () => {
    const res = await worker.fetch(
      ingestRequest([validEvent({ event: "" })]),
      env,
    );
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("event");
  });

  it("rejects event missing device_id", async () => {
    const res = await worker.fetch(
      ingestRequest([validEvent({ device_id: "" })]),
      env,
    );
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("device_id");
  });

  it("rejects event missing timestamp", async () => {
    const res = await worker.fetch(
      ingestRequest([validEvent({ timestamp: "" })]),
      env,
    );
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("timestamp");
  });

  it("rejects non-object event (string)", async () => {
    const res = await worker.fetch(ingestRequest(["not an object"]), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("must be an object");
  });

  it("rejects null event in array", async () => {
    const res = await worker.fetch(ingestRequest([null]), env);
    expect(res.status).toBe(400);
  });

  it("reports correct index for bad event in batch", async () => {
    const events = [validEvent(), validEvent({ event: "" })];
    const res = await worker.fetch(ingestRequest(events), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("event[1]");
  });

  // -- Happy path --

  it("stores a single valid event and returns ok", async () => {
    const res = await worker.fetch(ingestRequest([validEvent()]), env);
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data.status).toBe("ok");
    expect(data.stored).toBe(1);
    // Verify something was stored in the mock bucket
    expect(env.TELEMETRY_BUCKET._storage.size).toBe(1);
  });

  it("stores batch of 20 events", async () => {
    const events = Array.from({ length: 20 }, (_, i) =>
      validEvent({ event: `event.${i}` }),
    );
    const res = await worker.fetch(ingestRequest(events), env);
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data.stored).toBe(20);
  });

  it("stores events under events/ prefix with .json extension", async () => {
    await worker.fetch(ingestRequest([validEvent()]), env);
    const keys = Array.from(env.TELEMETRY_BUCKET._storage.keys());
    expect(keys).toHaveLength(1);
    expect(keys[0]).toMatch(/^events\/\d{4}\/\d{2}\/\d{2}\/\d+-[0-9a-f]+\.json$/);
  });

  it("stores the event payload as JSON in R2", async () => {
    const event = validEvent({ event: "print.started" });
    await worker.fetch(ingestRequest([event]), env);
    const stored = Array.from(env.TELEMETRY_BUCKET._storage.values())[0];
    const parsed = JSON.parse(stored.body);
    expect(parsed).toEqual([event]);
  });

  it("returns 500 when R2 put fails", async () => {
    const failBucket = createMockBucket();
    failBucket.put = vi.fn().mockRejectedValue(new Error("R2 down"));
    const failEnv = createEnv({ TELEMETRY_BUCKET: failBucket });
    const res = await worker.fetch(ingestRequest([validEvent()]), failEnv);
    expect(res.status).toBe(500);
    const data = await res.json();
    expect(data.error).toContain("Failed to store");
  });
});

describe("GET /v1/events/list (admin listing)", () => {
  let env: ReturnType<typeof createEnv>;

  beforeEach(() => {
    env = createEnv();
  });

  function listRequest(params = "", apiKey = "test-admin-key") {
    return makeRequest(`/v1/events/list${params ? "?" + params : ""}`, {
      headers: { "X-API-Key": apiKey },
    });
  }

  // -- Auth --

  it("rejects request with no API key", async () => {
    const req = makeRequest("/v1/events/list");
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(401);
  });

  it("rejects wrong API key", async () => {
    const res = await worker.fetch(listRequest("", "wrong-key"), env);
    expect(res.status).toBe(401);
  });

  it("rejects INGEST_API_KEY on list endpoint", async () => {
    const res = await worker.fetch(listRequest("", "test-ingest-key"), env);
    expect(res.status).toBe(401);
  });

  // -- Prefix validation --

  it("rejects prefix not starting with events/", async () => {
    const res = await worker.fetch(listRequest("prefix=secrets/"), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("events/");
  });

  it("rejects directory traversal prefix", async () => {
    const res = await worker.fetch(listRequest("prefix=../etc/"), env);
    expect(res.status).toBe(400);
  });

  // -- Happy path --

  it("returns empty list when no objects exist", async () => {
    const res = await worker.fetch(listRequest(), env);
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data.keys).toEqual([]);
    expect(data.truncated).toBe(false);
  });

  it("lists stored events with key, size, uploaded fields", async () => {
    // Store an event first
    await worker.fetch(ingestRequest([validEvent()]), env);

    const res = await worker.fetch(listRequest("prefix=events/"), env);
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data.keys).toHaveLength(1);
    expect(data.keys[0]).toHaveProperty("key");
    expect(data.keys[0]).toHaveProperty("size");
    expect(data.keys[0]).toHaveProperty("uploaded");
    expect(data.keys[0].key).toMatch(/^events\//);
  });

  it("defaults prefix to events/ when not provided", async () => {
    await worker.fetch(ingestRequest([validEvent()]), env);
    const res = await worker.fetch(listRequest(), env);
    const data = await res.json();
    expect(data.keys).toHaveLength(1);
  });

  it("passes limit parameter to R2 list", async () => {
    const listSpy = vi.spyOn(env.TELEMETRY_BUCKET, "list");
    await worker.fetch(listRequest("limit=5"), env);
    expect(listSpy).toHaveBeenCalledWith(
      expect.objectContaining({ limit: 5 }),
    );
  });

  it("clamps limit to 1-1000 range", async () => {
    const listSpy = vi.spyOn(env.TELEMETRY_BUCKET, "list");

    // Over 1000 gets clamped to 1000
    await worker.fetch(listRequest("limit=5000"), env);
    expect(listSpy).toHaveBeenCalledWith(
      expect.objectContaining({ limit: 1000 }),
    );

    listSpy.mockClear();

    // 0 is falsy so falls through to default 1000 via || operator
    await worker.fetch(listRequest("limit=0"), env);
    expect(listSpy).toHaveBeenCalledWith(
      expect.objectContaining({ limit: 1000 }),
    );

    listSpy.mockClear();

    // Negative values get clamped to 1 via Math.max
    await worker.fetch(listRequest("limit=-5"), env);
    expect(listSpy).toHaveBeenCalledWith(
      expect.objectContaining({ limit: 1 }),
    );
  });
});

describe("GET /v1/events/get (admin download)", () => {
  let env: ReturnType<typeof createEnv>;

  beforeEach(() => {
    env = createEnv();
  });

  function getRequest(params = "", apiKey = "test-admin-key") {
    return makeRequest(`/v1/events/get${params ? "?" + params : ""}`, {
      headers: { "X-API-Key": apiKey },
    });
  }

  // -- Auth --

  it("rejects request with no API key", async () => {
    const req = makeRequest("/v1/events/get?key=events/2026/01/15/test.json");
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(401);
  });

  it("rejects wrong API key", async () => {
    const res = await worker.fetch(
      getRequest("key=events/2026/01/15/test.json", "wrong-key"),
      env,
    );
    expect(res.status).toBe(401);
  });

  it("rejects INGEST_API_KEY on get endpoint", async () => {
    const res = await worker.fetch(
      getRequest("key=events/2026/01/15/test.json", "test-ingest-key"),
      env,
    );
    expect(res.status).toBe(401);
  });

  // -- Key validation --

  it("rejects missing key parameter", async () => {
    const res = await worker.fetch(getRequest(), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("Invalid key");
  });

  it("rejects key not starting with events/", async () => {
    const res = await worker.fetch(getRequest("key=secrets/data.json"), env);
    expect(res.status).toBe(400);
  });

  it("rejects key not ending with .json", async () => {
    const res = await worker.fetch(getRequest("key=events/2026/01/data.txt"), env);
    expect(res.status).toBe(400);
  });

  it("rejects key that starts with events/ but does not end with .json", async () => {
    const res = await worker.fetch(getRequest("key=events/2026/01/15/data"), env);
    expect(res.status).toBe(400);
  });

  // -- Happy path --

  it("returns stored event data", async () => {
    // Store an event first
    await worker.fetch(ingestRequest([validEvent()]), env);
    const storedKey = Array.from(env.TELEMETRY_BUCKET._storage.keys())[0];

    const res = await worker.fetch(getRequest(`key=${storedKey}`), env);
    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe("application/json");

    const body = await res.text();
    const parsed = JSON.parse(body);
    expect(parsed).toEqual([validEvent()]);
  });

  it("returns 404 for non-existent key", async () => {
    const res = await worker.fetch(
      getRequest("key=events/2026/01/15/nonexistent.json"),
      env,
    );
    expect(res.status).toBe(404);
    const data = await res.json();
    expect(data.error).toBe("Not found");
  });

  it("includes CORS headers on response", async () => {
    await worker.fetch(ingestRequest([validEvent()]), env);
    const storedKey = Array.from(env.TELEMETRY_BUCKET._storage.keys())[0];
    const res = await worker.fetch(getRequest(`key=${storedKey}`), env);
    expect(res.headers.get("Access-Control-Allow-Origin")).toBe("*");
  });
});

describe("GET /v1/symbols/:version", () => {
  let env: ReturnType<typeof createEnv>;

  beforeEach(() => {
    env = createEnv();
  });

  it("returns platforms for a valid version", async () => {
    // Pre-populate symbol files
    await env.TELEMETRY_BUCKET.put("symbols/v1.2.3/linux-arm.sym", "data");
    await env.TELEMETRY_BUCKET.put("symbols/v1.2.3/linux-x86_64.sym", "data");

    const res = await worker.fetch(makeRequest("/v1/symbols/1.2.3"), env);
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data.version).toBe("1.2.3");
    expect(data.platforms).toContain("linux-arm");
    expect(data.platforms).toContain("linux-x86_64");
  });

  it("returns empty platforms when no symbols exist", async () => {
    const res = await worker.fetch(makeRequest("/v1/symbols/9.9.9"), env);
    expect(res.status).toBe(200);
    const data = await res.json();
    expect(data.platforms).toEqual([]);
  });

  it("rejects invalid version format", async () => {
    // Use a version string that fails the regex but stays in the path
    const res = await worker.fetch(makeRequest("/v1/symbols/bad version!@#"), env);
    expect(res.status).toBe(400);
    const data = await res.json();
    expect(data.error).toContain("Invalid version");
  });

  it("rejects non-GET method", async () => {
    const res = await worker.fetch(
      makeRequest("/v1/symbols/1.0.0", { method: "POST" }),
      env,
    );
    expect(res.status).toBe(405);
  });
});

describe("Cross-key isolation", () => {
  const env = createEnv();

  it("ADMIN key cannot ingest events", async () => {
    const res = await worker.fetch(ingestRequest([validEvent()], "test-admin-key"), env);
    expect(res.status).toBe(401);
  });

  it("INGEST key cannot list events", async () => {
    const req = makeRequest("/v1/events/list", {
      headers: { "X-API-Key": "test-ingest-key" },
    });
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(401);
  });

  it("INGEST key cannot download events", async () => {
    const req = makeRequest("/v1/events/get?key=events/2026/01/15/test.json", {
      headers: { "X-API-Key": "test-ingest-key" },
    });
    const res = await worker.fetch(req, env);
    expect(res.status).toBe(401);
  });
});
