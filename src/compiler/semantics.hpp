#pragma once

#include "bound_program.hpp"
#include "source.hpp"
#include "syntax.hpp"

#include <optional>

namespace inputweaver::compiler {

[[nodiscard]] std::optional<BoundProgram> BindProgram(
    const SourceFile& source,
    const SyntaxTree& syntax,
    DiagnosticSink& diagnostics);

} // namespace inputweaver::compiler
