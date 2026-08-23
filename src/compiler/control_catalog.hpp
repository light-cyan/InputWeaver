#pragma once

#include "program/compiled_program.hpp"

#include <optional>
#include <string_view>

namespace inputweaver::compiler {

[[nodiscard]] std::optional<ControlRef> ResolveNamedControl(
    std::string_view name) noexcept;

[[nodiscard]] bool IsReservedControlIdentifier(std::string_view name) noexcept;

} // namespace inputweaver::compiler
