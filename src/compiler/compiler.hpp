#pragma once

#include "program/compiled_program.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::compiler {

inline constexpr std::size_t kMaximumCompileDiagnostics = 64U;
inline constexpr std::size_t kMaximumRelatedDiagnosticSpans = 4U;

struct CompilerLimits final {
    std::uint64_t maximumSourceBytes{16U * 1024U * 1024U};
    std::uint32_t maximumTokens{1U * 1024U * 1024U};
    std::uint32_t maximumSyntaxNodes{1U * 1024U * 1024U};
    std::uint32_t maximumNestingDepth{256U};
};

enum class CompileDiagnosticSeverity : std::uint8_t {
    Error,
};

enum class CompileDiagnosticCode : std::uint16_t {
    SourceReadFailed,
    SourceTooLarge,
    InvalidUtf8,
    InvalidDisplayPath,
    TokenLimit,
    SyntaxNodeLimit,
    SyntaxNestingLimit,
    InvalidCharacter,
    NonAsciiSyntax,
    UnterminatedString,
    InvalidEscape,
    UnterminatedBlockComment,
    NestedBlockComment,
    InvalidNumber,
    ExpectedToken,
    UnexpectedToken,
    InvalidPauseRule,
    DuplicateSetting,
    DuplicateSymbol,
    ReservedName,
    UnknownValue,
    UnknownControl,
    InvalidRawControl,
    TypeMismatch,
    InvalidDuration,
    SettingOutOfRange,
    ReadOnlyValue,
    ConstantEvaluation,
    EmbeddedNul,
    InternalCompiler,
    ArtifactWriteFailed,
    ArtifactReplaceFailed,
};

struct RelatedCompileSpan final {
    SourceSpan span{};
    std::string message;
};

struct CompileDiagnostic final {
    CompileDiagnosticCode code{};
    CompileDiagnosticSeverity severity{CompileDiagnosticSeverity::Error};
    SourceSpan primary{};
    std::string message;
    std::vector<RelatedCompileSpan> related;
    std::string displayPath;
    std::uint32_t line{1U};
    std::uint32_t column{1U};
    std::string sourceLine;
};

struct CompileOutput final {
    std::vector<std::uint8_t> artifact;
    std::string dump;
    std::vector<CompileDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct CompilerCommandResult final {
    bool succeeded{};
    std::uint64_t artifactByteLength{};
    std::string dump;
    std::vector<CompileDiagnostic> diagnostics;
};

[[nodiscard]] std::string_view CompileDiagnosticCodeName(
    CompileDiagnosticCode code) noexcept;

[[nodiscard]] std::string FormatCompileDiagnostic(
    const CompileDiagnostic& diagnostic);

[[nodiscard]] CompileOutput CompileSource(
    std::string displayPath,
    std::string sourceBytes,
    const CompilerLimits& limits = {});

[[nodiscard]] CompilerCommandResult CompileFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationPath,
    const CompilerLimits& limits = {});

[[nodiscard]] CompilerCommandResult ValidateFile(
    const std::filesystem::path& sourcePath,
    const CompilerLimits& limits = {});

[[nodiscard]] CompilerCommandResult DumpFile(
    const std::filesystem::path& sourcePath,
    const CompilerLimits& limits = {});

} // namespace inputweaver::compiler
