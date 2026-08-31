#pragma once

#include "compiled_program.hpp"

#include <span>

namespace inputweaver {

[[nodiscard]] std::uint32_t ComputeMaximumExpressionStackDepth(
    std::span<const ExpressionInstruction> code);

[[nodiscard]] ProgramRequirements ComputeProgramRequirements(
    const CompiledProgramStorage& storage);

} // namespace inputweaver
