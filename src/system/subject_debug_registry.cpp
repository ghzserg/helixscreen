// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subject_debug_registry.h"

#include <spdlog/spdlog.h>

SubjectDebugRegistry& SubjectDebugRegistry::instance() {
    static SubjectDebugRegistry instance;
    return instance;
}

void SubjectDebugRegistry::register_subject(lv_subject_t* subject, const std::string& name,
                                            lv_subject_type_t type, const std::string& file,
                                            int line) {
    if (subject == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    subjects_[subject] = SubjectDebugInfo{name, type, file, line};
}

const SubjectDebugInfo* SubjectDebugRegistry::lookup(lv_subject_t* subject) {
    if (subject == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subjects_.find(subject);
    if (it == subjects_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::string SubjectDebugRegistry::type_name(lv_subject_type_t type) {
    switch (type) {
    case LV_SUBJECT_TYPE_INVALID:
        return "INVALID";
    case LV_SUBJECT_TYPE_NONE:
        return "NONE";
    case LV_SUBJECT_TYPE_INT:
        return "INT";
    case LV_SUBJECT_TYPE_FLOAT:
        return "FLOAT";
    case LV_SUBJECT_TYPE_POINTER:
        return "POINTER";
    case LV_SUBJECT_TYPE_COLOR:
        return "COLOR";
    case LV_SUBJECT_TYPE_GROUP:
        return "GROUP";
    case LV_SUBJECT_TYPE_STRING:
        return "STRING";
    default:
        return "UNKNOWN";
    }
}

void SubjectDebugRegistry::dump_all_subjects() {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("[SubjectDebugRegistry] Registered subjects: {}", subjects_.size());

    for (const auto& [ptr, info] : subjects_) {
        spdlog::debug("[SubjectDebugRegistry]   {} ({}): {} @ {}:{}", static_cast<void*>(ptr),
                      type_name(info.type), info.name, info.file, info.line);
    }
}

void SubjectDebugRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subjects_.clear();
}
