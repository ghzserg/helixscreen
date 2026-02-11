// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_update_queue.h"

namespace helix::ui {

class UpdateQueueTestAccess {
  public:
    static void drain(UpdateQueue& q) {
        q.process_pending();
    }
};

} // namespace helix::ui

// Convenience alias for use with 'using namespace helix'
using helix::ui::UpdateQueueTestAccess;
