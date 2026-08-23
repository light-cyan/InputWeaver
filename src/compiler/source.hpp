#pragma once

#include "compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::compiler {

struct SourceFile final {
    std::string displayPath;
    std::string bytes;
    std::vector<std::uint32_t> lineStarts;

    [[nodiscard]] SourceSpan WholeSpan() const noexcept;
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> LineColumn(
        SourceSpan span) const noexcept;
    [[nodiscard]] std::string SourceLine(SourceSpan span) const;
};

class DiagnosticSink final {
public:
    explicit DiagnosticSink(const SourceFile* source = nullptr) noexcept;

    void SetSource(const SourceFile* source) noexcept;
    void Add(
        CompileDiagnosticCode code,
        SourceSpan primary,
        std::string message,
        std::vector<RelatedCompileSpan> related = {});
    [[nodiscard]] bool HasErrors() const noexcept;
    [[nodiscard]] bool Full() const noexcept;
    [[nodiscard]] std::vector<CompileDiagnostic> Take() &&;

private:
    const SourceFile* source_{};
    std::vector<CompileDiagnostic> diagnostics_;
};

[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;

[[nodiscard]] std::optional<SourceFile> MakeSourceFile(
    std::string displayPath,
    std::string bytes,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics);

[[nodiscard]] std::optional<SourceFile> LoadSourceFile(
    const std::filesystem::path& path,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics);

} // namespace inputweaver::compiler
