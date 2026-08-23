#pragma once

#include "compiled_program.hpp"

#include <vector>

namespace inputweaver {

[[nodiscard]] ProgramRequirements ComputeProgramRequirements(
    const CompiledProgramStorage& storage);

[[nodiscard]] std::vector<ProgramValidationError> ValidateCompiledProgram(
    const CompiledProgramStorage& storage);

} // namespace inputweaver
