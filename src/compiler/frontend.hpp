#pragma once

#include "source.hpp"
#include "syntax.hpp"

#include <optional>
#include <vector>

namespace inputweaver::compiler {

[[nodiscard]] std::vector<Token> LexSource(
    const SourceFile& source,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics);

[[nodiscard]] std::optional<SyntaxTree> ParseTokens(
    const SourceFile& source,
    const std::vector<Token>& tokens,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics);

} // namespace inputweaver::compiler
