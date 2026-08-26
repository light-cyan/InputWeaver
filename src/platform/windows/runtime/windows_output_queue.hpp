#pragma once

#include "support/fixed_spsc_ring.hpp"
#include "windows_input_types.hpp"

namespace inputweaver {

using WindowsOutputQueue = support::FixedSpscRing<
    WindowsOutputItem,
    kWindowsOutputQueueCapacity>;

} // namespace inputweaver
