// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlay_base.h"

#include "ui_nav_manager.h"

#include <spdlog/spdlog.h>

OverlayBase::~OverlayBase() {
    // Fallback unregister in case cleanup() wasn't called
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    if (!cleanup_called_) {
        // Cannot call get_name() here as derived class is already destroyed
        spdlog::warn("[OverlayBase] Overlay destroyed without cleanup() - may cause issues with "
                     "async callbacks");
    }
}

void OverlayBase::on_activate() {
    spdlog::trace("[OverlayBase] on_activate() - {}", get_name());
    visible_ = true;
}

void OverlayBase::on_deactivate() {
    spdlog::trace("[OverlayBase] on_deactivate() - {}", get_name());
    visible_ = false;
}

void OverlayBase::cleanup() {
    spdlog::trace("[OverlayBase] cleanup() - {}", get_name());
    cleanup_called_ = true;
    visible_ = false;
}
