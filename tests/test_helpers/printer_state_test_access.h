// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_update_queue.h"

#include "printer_state.h"
#include "update_queue_test_access.h"

namespace helix {

class PrinterPrintStateTestAccess {
  public:
    static void reset_extra(PrinterPrintState& pps) {
        pps.estimated_print_time_ = 0;
        pps.has_real_layer_data_ = false;
    }
};

// PrinterStateTestAccess must be in namespace helix to match friend declaration in PrinterState
class PrinterStateTestAccess {
  public:
    static void reset(PrinterState& ps) {
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        ps.deinit_subjects();
        ps.printer_type_.clear();
        ps.print_start_capabilities_ = PrintStartCapabilities();
        ps.z_offset_calibration_strategy_ = ZOffsetCalibrationStrategy::PROBE_CALIBRATE;
        ps.auto_detected_bed_moves_ = false;
        ps.last_kinematics_.clear();
        PrinterPrintStateTestAccess::reset_extra(ps.print_domain_);
    }
};

} // namespace helix

using namespace helix;
