#pragma once

#include "support/callback_ref.hpp"

namespace inputweaver {

using StopRequest = support::CallbackRef<void() noexcept>;

}  // namespace inputweaver
