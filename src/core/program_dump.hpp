#pragma once

#include "compiled_program.hpp"

#include <string>

namespace inputweaver {

[[nodiscard]] std::string DumpCompiledProgram(const CompiledProgram& program);

} // namespace inputweaver
