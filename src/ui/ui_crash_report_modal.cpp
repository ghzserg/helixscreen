// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_crash_report_modal.h"

#include "ui_toast_manager.h"
#include "ui_update_queue.h"

#include "lvgl/src/others/translation/lv_translation.h"
#include "system/crash_reporter.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

// =============================================================================
// Static Members
// =============================================================================

bool CrashReportModal::callbacks_registered_ = false;
CrashReportModal* CrashReportModal::active_instance_ = nullptr;

// =============================================================================
// Constructor / Destructor
// =============================================================================

CrashReportModal::CrashReportModal() {
    spdlog::debug("[CrashReportModal] Constructed");
}

CrashReportModal::~CrashReportModal() {
    deinit_subjects();
    spdlog::trace("[CrashReportModal] Destroyed");
}

// =============================================================================
// Public API
// =============================================================================

void CrashReportModal::set_report(const CrashReporter::CrashReport& report) {
    report_ = report;
}

bool CrashReportModal::show_modal(lv_obj_t* parent) {
    register_callbacks();
    init_subjects();

    // Populate details subject with crash summary
    std::string details = "Signal: " + std::to_string(report_.signal) + " (" + report_.signal_name +
                          ")\nVersion: " + report_.app_version +
                          "\nUptime: " + std::to_string(report_.uptime_sec) + "s";
    if (!report_.timestamp.empty()) {
        details += "\nTime: " + report_.timestamp;
    }

    lv_subject_copy_string(&details_subject_, details.c_str());
    lv_subject_copy_string(&status_subject_,
                           lv_tr("Send this crash report to help improve HelixScreen."));

    // Call base class show
    bool result = show(parent);
    if (result && dialog()) {
        active_instance_ = this;
    }

    return result;
}

// =============================================================================
// Lifecycle Hooks
// =============================================================================

void CrashReportModal::on_show() {
    spdlog::debug("[CrashReportModal] on_show");
}

void CrashReportModal::on_hide() {
    spdlog::debug("[CrashReportModal] on_hide");
    active_instance_ = nullptr;

    // Self-delete: this modal is heap-allocated in application.cpp startup
    // and has no other owner. Deferred so hide() finishes before destruction.
    auto* self = this;
    helix::ui::async_call([](void* data) { delete static_cast<CrashReportModal*>(data); }, self);
}

// =============================================================================
// Subject Management
// =============================================================================

void CrashReportModal::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    lv_subject_init_string(&details_subject_, details_buf_, nullptr, sizeof(details_buf_), "");
    lv_subject_init_string(&status_subject_, status_buf_, nullptr, sizeof(status_buf_), "");
    lv_subject_init_int(&show_qr_subject_, 0);

    lv_xml_register_subject(nullptr, "crash_report_details", &details_subject_);
    lv_xml_register_subject(nullptr, "crash_report_status", &status_subject_);
    lv_xml_register_subject(nullptr, "crash_report_show_qr", &show_qr_subject_);

    subjects_initialized_ = true;
}

void CrashReportModal::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    lv_subject_deinit(&details_subject_);
    lv_subject_deinit(&status_subject_);
    lv_subject_deinit(&show_qr_subject_);

    subjects_initialized_ = false;
}

// =============================================================================
// Callback Registration
// =============================================================================

void CrashReportModal::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    lv_xml_register_event_cb(nullptr, "on_crash_report_send", on_send_cb);
    lv_xml_register_event_cb(nullptr, "on_crash_report_dismiss", on_dismiss_cb);

    callbacks_registered_ = true;
}

// =============================================================================
// Static Event Callbacks
// =============================================================================

void CrashReportModal::on_send_cb(lv_event_t* /*e*/) {
    if (active_instance_) {
        active_instance_->handle_send();
    }
}

void CrashReportModal::on_dismiss_cb(lv_event_t* /*e*/) {
    if (active_instance_) {
        active_instance_->handle_dismiss();
    }
}

// =============================================================================
// Instance Event Handlers
// =============================================================================

void CrashReportModal::handle_send() {
    spdlog::info("[CrashReportModal] User clicked Send Report");
    attempt_delivery();
}

void CrashReportModal::handle_dismiss() {
    spdlog::info("[CrashReportModal] User dismissed crash report");

    // Always consume the crash file so we don't nag on every launch
    CrashReporter::instance().consume_crash_file();

    hide();
}

// =============================================================================
// Delivery Logic
// =============================================================================

void CrashReportModal::attempt_delivery() {
    auto& cr = CrashReporter::instance();

    // Update status
    lv_subject_copy_string(&status_subject_, lv_tr("Sending..."));

    // Try auto-send first
    if (cr.try_auto_send(report_)) {
        spdlog::info("[CrashReportModal] Crash report sent via worker");
        cr.save_to_file(report_);
        cr.consume_crash_file();
        hide();
        ToastManager::instance().show(ToastSeverity::SUCCESS,
                                      lv_tr("Crash report sent — thank you!"), 4000);
        return;
    } else {
        // Auto-send failed — try QR code
        std::string url = cr.generate_github_url(report_);
        if (!url.empty()) {
            show_qr_code(url);
            lv_subject_copy_string(&status_subject_,
                                   lv_tr("No network. Scan QR code to report on your phone."));
        } else {
            lv_subject_copy_string(&status_subject_, lv_tr("Report saved to crash_report.txt"));
        }
    }

    // Always save to file as fallback
    cr.save_to_file(report_);

    // Consume crash file after handling
    cr.consume_crash_file();
}

void CrashReportModal::show_qr_code(const std::string& url) {
    // Show the QR container
    lv_subject_set_int(&show_qr_subject_, 1);

    // Find the QR container and create QR code widget
    if (!dialog()) {
        return;
    }

    lv_obj_t* qr_container = lv_obj_find_by_name(dialog(), "qr_container");
    if (!qr_container) {
        spdlog::warn("[CrashReportModal] QR container not found");
        return;
    }

#if LV_USE_QRCODE
    lv_obj_t* qr = lv_qrcode_create(qr_container);
    if (qr) {
        lv_qrcode_update(qr, url.c_str(), static_cast<uint32_t>(url.size()));
        lv_obj_set_size(qr, 180, 180);
        lv_obj_center(qr);
        spdlog::debug("[CrashReportModal] QR code created for URL ({} chars)", url.size());
    }
#else
    spdlog::warn("[CrashReportModal] QR code support not compiled in (LV_USE_QRCODE=0)");
    lv_subject_copy_string(&status_subject_, lv_tr("Saved to crash_report.txt (QR not available)"));
    (void)url;
#endif
}
