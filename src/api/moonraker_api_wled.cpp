// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file moonraker_api_wled.cpp
 * @brief WLED control operations via Moonraker's WLED bridge
 *
 * Moonraker exposes WLED devices configured in moonraker.conf through:
 * - GET  /machine/wled/strips  - List discovered WLED strips
 * - POST /machine/wled/strip   - Control a strip (on/off/toggle/brightness/preset)
 *
 * These are thin wrappers around call_rest_get/call_rest_post.
 */

#include "moonraker_api.h"
#include "moonraker_error.h"

#include <spdlog/spdlog.h>

#include "hv/json.hpp"

// ============================================================================
// WLED Control Operations
// ============================================================================

void MoonrakerAPI::wled_get_strips(RestCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] Fetching WLED strips");

    call_rest_get("/machine/wled/strips", [on_success, on_error](const RestResponse& resp) {
        if (resp.success) {
            if (on_success) {
                on_success(resp);
            }
        } else {
            spdlog::debug("[Moonraker API] WLED get_strips failed: {}", resp.error);
            if (on_error) {
                on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0, resp.error, "wled"});
            }
        }
    });
}

void MoonrakerAPI::wled_get_status(RestCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] Fetching WLED status");

    call_rest_get("/machine/wled/strips", [on_success, on_error](const RestResponse& resp) {
        if (resp.success) {
            if (on_success) {
                on_success(resp);
            }
        } else {
            spdlog::debug("[Moonraker API] WLED get_status failed: {}", resp.error);
            if (on_error) {
                on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0, resp.error, "wled"});
            }
        }
    });
}

void MoonrakerAPI::get_server_config(RestCallback on_success, ErrorCallback on_error) {
    spdlog::debug("[Moonraker API] Fetching server config");

    call_rest_get("/server/config", [on_success, on_error](const RestResponse& resp) {
        if (resp.success) {
            if (on_success) {
                on_success(resp);
            }
        } else {
            spdlog::warn("[Moonraker API] get_server_config failed: {}", resp.error);
            if (on_error) {
                on_error(
                    MoonrakerError{MoonrakerErrorType::UNKNOWN, 0, resp.error, "server_config"});
            }
        }
    });
}

void MoonrakerAPI::wled_set_strip(const std::string& strip, const std::string& action,
                                  int brightness, int preset, SuccessCallback on_success,
                                  ErrorCallback on_error) {
    json body;
    body["strip"] = strip;
    body["action"] = action;

    if (brightness >= 0) {
        body["brightness"] = brightness;
    }
    if (preset >= 0) {
        body["preset"] = preset;
    }

    spdlog::debug("[Moonraker API] WLED set_strip: strip={} action={} brightness={} preset={}",
                  strip, action, brightness, preset);

    call_rest_post(
        "/machine/wled/strip", body, [on_success, on_error, strip](const RestResponse& resp) {
            if (resp.success) {
                if (on_success) {
                    on_success();
                }
            } else {
                spdlog::warn("[Moonraker API] WLED set_strip '{}' failed: {}", strip, resp.error);
                if (on_error) {
                    on_error(MoonrakerError{MoonrakerErrorType::UNKNOWN, 0, resp.error, "wled"});
                }
            }
        });
}
