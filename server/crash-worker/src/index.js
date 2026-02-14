// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cloudflare Worker: helix-crash-worker
// Receives crash reports from HelixScreen devices and creates GitHub issues.
//
// Endpoints:
//   GET  /           - Health check
//   POST /v1/report  - Submit a crash report (requires X-API-Key)
//
// Secrets (configure via `wrangler secret put`):
//   INGEST_API_KEY  - API key baked into HelixScreen binaries
//   GITHUB_TOKEN    - GitHub PAT with repo scope

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
    "Access-Control-Allow-Headers": "Content-Type, X-API-Key",
  };
}
