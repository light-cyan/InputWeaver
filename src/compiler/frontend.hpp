#pragma once

#include "source.hpp"
#include "syntax.hpp"

#include <optional>

namespace inputweaver::compiler {

[[nodiscard]] std::optional<SyntaxTree> ParseSource(
    const SourceFile& source,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics);

} // namespace inputweaver::compiler
