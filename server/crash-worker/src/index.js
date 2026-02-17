// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cloudflare Worker: helix-crash-worker
// Receives crash reports from HelixScreen devices and creates GitHub issues.
//
// Endpoints:
//   GET  /                      - Health check
//   POST /v1/report             - Submit a crash report (requires X-API-Key)
//   POST /v1/debug-bundle       - Upload debug bundle (requires X-API-Key)
//   GET  /v1/debug-bundle/:code - Retrieve debug bundle (requires X-Admin-Key)
//
// Secrets (configure via `wrangler secret put`):
//   INGEST_API_KEY  - API key baked into HelixScreen binaries
//   GITHUB_TOKEN    - GitHub PAT with repo scope
//   ADMIN_API_KEY   - Admin API key for retrieving debug bundles
//   RESEND_API_KEY  - Resend API key for email notifications

export default {
  async fetch(request, env) {
    // Handle CORS preflight
    if (request.method === "OPTIONS") {
      return new Response(null, {
        status: 204,
        headers: corsHeaders(),
      });
    }

    const url = new URL(request.url);

    // Health check
    if (url.pathname === "/" && request.method === "GET") {
      return jsonResponse(200, {
        service: "helix-crash-worker",
        status: "ok",
        timestamp: new Date().toISOString(),
      });
    }

    // Crash report ingestion
    if (url.pathname === "/v1/report" && request.method === "POST") {
      return handleCrashReport(request, env);
    }

    // Debug bundle upload
    if (url.pathname === "/v1/debug-bundle" && request.method === "POST") {
      return handleDebugBundleUpload(request, env);
    }

    // Debug bundle retrieval
    if (url.pathname.startsWith("/v1/debug-bundle/") && request.method === "GET") {
      return handleDebugBundleRetrieve(request, env, url);
    }

    return jsonResponse(404, { error: "Not found" });
  },
};

/**
 * Handle an incoming crash report.
 * Validates the API key and payload, then creates a GitHub issue.
 */
async function handleCrashReport(request, env) {
  // --- Authentication ---
  const apiKey = request.headers.get("X-API-Key");
  if (!apiKey || apiKey !== env.INGEST_API_KEY) {
    return jsonResponse(401, { error: "Unauthorized: invalid or missing API key" });
  }

  // --- Rate limiting (per client IP) ---
  const clientIP = request.headers.get("CF-Connecting-IP") || "unknown";
  const { success } = await env.CRASH_LIMITER.limit({ key: clientIP });
  if (!success) {
    return jsonResponse(429, { error: "Rate limit exceeded — try again later" });
  }

  // --- Parse and validate body ---
  let body;
  try {
    body = await request.json();
  } catch {
    return jsonResponse(400, { error: "Invalid JSON body" });
  }

  const missing = validateRequiredFields(body, ["signal", "signal_name", "app_version"]);
  if (missing.length > 0) {
    return jsonResponse(400, {
      error: `Missing required fields: ${missing.join(", ")}`,
    });
  }

  // --- Create GitHub issue ---
  try {
    const issue = await createGitHubIssue(env, body);
    return jsonResponse(201, {
      status: "created",
      issue_number: issue.number,
      issue_url: issue.html_url,
    });
  } catch (err) {
    console.error("Failed to create GitHub issue:", err.message);
    return jsonResponse(500, { error: "Failed to create GitHub issue" });
  }
}

/**
 * Check that all required fields are present and non-empty.
 * Returns an array of missing field names.
 */
function validateRequiredFields(body, fields) {
  return fields.filter(
    (f) => body[f] === undefined || body[f] === null || body[f] === ""
  );
}

/**
 * Build a markdown issue body from the crash report and create it via GitHub API.
 */
async function createGitHubIssue(env, report) {
  // Include fault type in title when available (e.g., "SEGV_MAPERR at 0x00000000")
  let title = `Crash: ${report.signal_name} in v${report.app_version}`;
  if (report.fault_code_name && report.fault_addr) {
    title = `Crash: ${report.signal_name} (${report.fault_code_name} at ${report.fault_addr}) in v${report.app_version}`;
  }
  const body = formatIssueBody(report);

  const response = await fetch(
    `https://api.github.com/repos/${env.GITHUB_REPO}/issues`,
    {
      method: "POST",
      headers: {
        Authorization: `Bearer ${env.GITHUB_TOKEN}`,
        Accept: "application/vnd.github+json",
        "User-Agent": "helix-crash-worker/1.0",
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        title,
        body,
        labels: ["crash", "auto-reported"],
      }),
    }
  );

  if (!response.ok) {
    const text = await response.text();
    throw new Error(`GitHub API ${response.status}: ${text}`);
  }

  return response.json();
}

/**
 * Format the crash report into a structured markdown issue body.
 */
function formatIssueBody(r) {
  const timestamp = r.timestamp || new Date().toISOString();
  const uptime = r.uptime_seconds != null ? `${r.uptime_seconds}s` : "unknown";

  let md = `## Crash Summary

| Field | Value |
|-------|-------|
| **Signal** | ${r.signal} (${r.signal_name}) |
| **Version** | ${r.app_version} |
| **Uptime** | ${uptime} |
| **Timestamp** | ${timestamp} |
`;

  // Fault info (Phase 2)
  if (r.fault_code_name && r.fault_addr) {
    md += `| **Fault** | ${r.fault_code_name} at ${r.fault_addr} |\n`;
  }

  // Register state (Phase 2)
  if (r.registers) {
    md += `\n## Registers

| Register | Value |
|----------|-------|
`;
    if (r.registers.pc) md += `| **PC** | ${r.registers.pc} |\n`;
    if (r.registers.sp) md += `| **SP** | ${r.registers.sp} |\n`;
    if (r.registers.lr) md += `| **LR** | ${r.registers.lr} |\n`;
    if (r.registers.bp) md += `| **BP** | ${r.registers.bp} |\n`;
  }

  // System info section (all fields optional)
  if (r.platform || r.display_backend || r.ram_mb || r.cpu_info || r.printer_model || r.klipper_version) {
    md += `\n## System Info

| Field | Value |
|-------|-------|
`;
    if (r.platform) md += `| **Platform** | ${r.platform} |\n`;
    if (r.display_backend) md += `| **Display** | ${r.display_backend} |\n`;
    if (r.ram_mb) md += `| **RAM** | ${r.ram_mb} MB |\n`;
    if (r.cpu_cores) md += `| **CPU** | ${r.cpu_cores} cores |\n`;
    if (r.printer_model) md += `| **Printer** | ${r.printer_model} |\n`;
    if (r.klipper_version) md += `| **Klipper** | ${r.klipper_version} |\n`;
  }

  // Backtrace section
  if (r.backtrace && r.backtrace.length > 0) {
    md += `\n## Backtrace

\`\`\`
${r.backtrace.join("\n")}
\`\`\`
`;
  }

  // Log tail in a collapsed section
  if (r.log_tail && r.log_tail.length > 0) {
    md += `
<details>
<summary>Log Tail (last ${r.log_tail.length} lines)</summary>

\`\`\`
${r.log_tail.join("\n")}
\`\`\`

</details>
`;
  }

  md += `\n---\n*Auto-reported by HelixScreen crash handler*\n`;

  return md;
}

/**
 * Generate a random share code using an unambiguous character set.
 * Excludes I, O, 0, 1 to avoid confusion when reading codes aloud.
 */
function generateShareCode(length = 8) {
  const charset = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const values = crypto.getRandomValues(new Uint8Array(length));
  return Array.from(values, (v) => charset[v % charset.length]).join("");
}

/**
 * Handle an incoming debug bundle upload.
 * Validates the API key and payload, stores in R2, returns a share code.
 */
async function handleDebugBundleUpload(request, env) {
  // --- Authentication ---
  const apiKey = request.headers.get("X-API-Key");
  if (!apiKey || apiKey !== env.INGEST_API_KEY) {
    return jsonResponse(401, { error: "Unauthorized: invalid or missing API key" });
  }

  // --- Rate limiting (per client IP) ---
  const clientIP = request.headers.get("CF-Connecting-IP") || "unknown";
  const { success } = await env.DEBUG_BUNDLE_LIMITER.limit({ key: clientIP });
  if (!success) {
    return jsonResponse(429, { error: "Rate limit exceeded — try again later" });
  }

  // --- Read and validate body ---
  const body = await request.arrayBuffer();
  if (!body || body.byteLength === 0) {
    return jsonResponse(400, { error: "Empty body" });
  }

  const maxSize = 500 * 1024; // 500KB
  if (body.byteLength > maxSize) {
    return jsonResponse(413, { error: "Payload too large (max 500KB)" });
  }

  // --- Store in R2 with a share code ---
  const shareCode = generateShareCode();
  await env.DEBUG_BUNDLES.put(shareCode, body, {
    httpMetadata: {
      contentType: "application/json",
      contentEncoding: "gzip",
    },
  });

  // --- Send email notification (best-effort, don't fail the upload) ---
  try {
    const metadata = await extractBundleMetadata(body);
    await sendBundleNotification(env, shareCode, clientIP, metadata);
  } catch (err) {
    console.error("Failed to send bundle notification:", err.message);
  }

  return jsonResponse(201, { share_code: shareCode });
}

/**
 * Retrieve a debug bundle by share code.
 * Requires admin API key for access.
 */
async function handleDebugBundleRetrieve(request, env, url) {
  // --- Authentication (admin key) ---
  const adminKey = request.headers.get("X-Admin-Key");
  if (!adminKey || adminKey !== env.ADMIN_API_KEY) {
    return jsonResponse(401, { error: "Unauthorized: invalid or missing admin key" });
  }

  // --- Extract share code from URL ---
  const code = url.pathname.split("/").pop();
  if (!code) {
    return jsonResponse(400, { error: "Missing share code" });
  }

  // --- Retrieve from R2 ---
  const object = await env.DEBUG_BUNDLES.get(code);
  if (!object) {
    return jsonResponse(404, { error: "Debug bundle not found" });
  }

  return new Response(object.body, {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Content-Encoding": "gzip",
      ...corsHeaders(),
    },
  });
}

/**
 * Decompress a gzipped ArrayBuffer and extract metadata from the JSON bundle.
 * Returns an object with version, printer_model, timestamp, etc.
 */
async function extractBundleMetadata(gzippedBody) {
  try {
    const ds = new DecompressionStream("gzip");
    const writer = ds.writable.getWriter();
    writer.write(new Uint8Array(gzippedBody));
    writer.close();

    const reader = ds.readable.getReader();
    const chunks = [];
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
    }

    const text = new TextDecoder().decode(
      chunks.reduce((acc, chunk) => {
        const merged = new Uint8Array(acc.length + chunk.length);
        merged.set(acc);
        merged.set(chunk, acc.length);
        return merged;
      }, new Uint8Array())
    );

    const json = JSON.parse(text);
    return {
      version: json.version || "unknown",
      printer_model: json.printer_info?.model || "unknown",
      klipper_version: json.printer_info?.klipper_version || "unknown",
      platform: json.system_info?.platform || "unknown",
      timestamp: json.timestamp || new Date().toISOString(),
    };
  } catch {
    return {
      version: "unknown",
      printer_model: "unknown",
      klipper_version: "unknown",
      platform: "unknown",
      timestamp: new Date().toISOString(),
    };
  }
}

/**
 * Send a notification email via Resend when a debug bundle is uploaded.
 */
async function sendBundleNotification(env, shareCode, clientIP, metadata) {
  if (!env.RESEND_API_KEY) {
    console.warn("RESEND_API_KEY not set, skipping notification");
    return;
  }

  const from = env.EMAIL_FROM || "HelixScreen <noreply@helixscreen.org>";
  const to = env.NOTIFICATION_EMAIL;
  if (!to) {
    console.warn("NOTIFICATION_EMAIL not set, skipping notification");
    return;
  }

  const subject = `Debug Bundle: ${shareCode} — ${metadata.printer_model} v${metadata.version}`;

  const html = `
    <h2>New Debug Bundle Uploaded</h2>
    <table style="border-collapse: collapse; font-family: monospace;">
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Share Code</td><td>${shareCode}</td></tr>
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Version</td><td>${metadata.version}</td></tr>
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Printer</td><td>${metadata.printer_model}</td></tr>
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Klipper</td><td>${metadata.klipper_version}</td></tr>
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Platform</td><td>${metadata.platform}</td></tr>
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Client IP</td><td>${clientIP}</td></tr>
      <tr><td style="padding: 4px 12px 4px 0; font-weight: bold;">Time</td><td>${metadata.timestamp}</td></tr>
    </table>
    <p style="margin-top: 16px;">Retrieve with:</p>
    <pre style="background: #f4f4f4; padding: 8px; border-radius: 4px;">curl --compressed -H "X-Admin-Key: \$HELIX_ADMIN_KEY" https://crash.helixscreen.org/v1/debug-bundle/${shareCode}</pre>
  `;

  const text = `New Debug Bundle: ${shareCode}
Version: ${metadata.version}
Printer: ${metadata.printer_model}
Klipper: ${metadata.klipper_version}
Platform: ${metadata.platform}
IP: ${clientIP}
Time: ${metadata.timestamp}

Retrieve: curl --compressed -H "X-Admin-Key: $HELIX_ADMIN_KEY" https://crash.helixscreen.org/v1/debug-bundle/${shareCode}`;

  const response = await fetch("https://api.resend.com/emails", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${env.RESEND_API_KEY}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ from, to: [to], subject, html, text }),
  });

  if (!response.ok) {
    const err = await response.text();
    throw new Error(`Resend API ${response.status}: ${err}`);
  }
}

/**
 * Build a JSON response with CORS headers.
 */
function jsonResponse(status, data) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "Content-Type": "application/json",
      ...corsHeaders(),
    },
  });
}

/**
 * Standard CORS headers.
 * Not strictly needed for device-to-worker traffic, but included for
 * completeness if a web dashboard ever hits this endpoint.
 */
function corsHeaders() {
  return {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-API-Key, X-Admin-Key",
  };
}
