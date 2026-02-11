// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_versions_state.cpp
 * @brief Software version state management extracted from PrinterState
 *
 * Manages Klipper and Moonraker version subjects for UI display in the
 * Settings panel About section.
 *
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_versions_state.h"

#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterVersionsState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterVersionsState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterVersionsState] Initializing subjects (register_xml={})", register_xml);

    // Initialize string subjects with em dash default
    INIT_SUBJECT_STRING(klipper_version, "—", subjects_, register_xml);
    INIT_SUBJECT_STRING(moonraker_version, "—", subjects_, register_xml);
    INIT_SUBJECT_STRING(os_version, "—", subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterVersionsState] Subjects initialized successfully");
}

void PrinterVersionsState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterVersionsState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterVersionsState::set_klipper_version_internal(const std::string& version) {
    lv_subject_copy_string(&klipper_version_, version.c_str());
    spdlog::debug("[PrinterVersionsState] Klipper version set: {}", version);
}

void PrinterVersionsState::set_moonraker_version_internal(const std::string& version) {
    lv_subject_copy_string(&moonraker_version_, version.c_str());
    spdlog::debug("[PrinterVersionsState] Moonraker version set: {}", version);
}

void PrinterVersionsState::set_os_version_internal(const std::string& version) {
    lv_subject_copy_string(&os_version_, version.c_str());
    spdlog::debug("[PrinterVersionsState] OS version set: {}", version);
}

} // namespace helix
