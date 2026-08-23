#pragma once

#include "bound_program.hpp"

#include "program/compiled_program.hpp"

namespace inputweaver::compiler {

[[nodiscard]] FinalizeResult LowerProgram(BoundProgram program);

} // namespace inputweaver::compiler
