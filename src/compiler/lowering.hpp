#pragma once

#include "bound_program.hpp"

#include "program/compiled_program.hpp"

#include <string_view>

namespace inputweaver::compiler {

[[nodiscard]] FinalizeResult LowerProgram(
    BoundProgram program,
    std::string_view sourceText);

} // namespace inputweaver::compiler
