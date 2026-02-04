// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboard_shortcuts.h"

namespace helix::input {

void KeyboardShortcuts::register_key(int scancode, Action action) {
    m_bindings.push_back({scancode, 0, std::move(action), nullptr, false});
}

void KeyboardShortcuts::register_key_if(int scancode, Action action, Condition condition) {
    m_bindings.push_back({scancode, 0, std::move(action), std::move(condition), false});
}

void KeyboardShortcuts::register_combo(int modifiers, int scancode, Action action) {
    m_bindings.push_back({scancode, modifiers, std::move(action), nullptr, false});
}

void KeyboardShortcuts::process(KeyStateProvider is_key_pressed, int current_modifiers) {
    for (auto& binding : m_bindings) {
        bool key_pressed = is_key_pressed(binding.scancode);

        // Check modifier requirement
        if (binding.modifiers != 0) {
            // Any matching modifier bit must be set (e.g., KMOD_GUI matches left OR right)
            if ((current_modifiers & binding.modifiers) == 0) {
                key_pressed = false;
            }
        }

        // Edge detection: fire on press, not on hold
        if (key_pressed && !binding.was_pressed) {
            // Check condition if present
            if (!binding.condition || binding.condition()) {
                binding.action();
            }
        }

        binding.was_pressed = key_pressed;
    }
}

void KeyboardShortcuts::clear() {
    m_bindings.clear();
}

} // namespace helix::input
