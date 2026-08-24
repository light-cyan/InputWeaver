#pragma once

#include "program/compiled_program.hpp"

#include <string>
#include <string_view>

namespace inputweaver::win32 {

[[nodiscard]] bool ResolveCompiledTarget(
    const CompiledProgram& program,
    bool commandLineGlobal,
    std::wstring_view commandLineExecutable,
    TargetSelectorKind& kind,
    std::wstring& selector,
    std::wstring& errorMessage);

}  // namespace inputweaver::win32
