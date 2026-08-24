#include "compiler.hpp"

#include "artifact_file.hpp"
#include "frontend.hpp"
#include "lowering.hpp"
#include "path.hpp"
#include "semantics.hpp"
#include "source.hpp"

#include "program/program_dump.hpp"
#include "program/weavec_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace inputweaver::compiler {
namespace {

enum class RequestedProduct : std::uint8_t {
    None,
    Artifact,
    Dump,
    All,
};

[[nodiscard]] CompileOutput CompileValidatedSource(
    SourceFile source,
    const CompilerLimits& limits,
    RequestedProduct products)
{
    DiagnosticSink diagnostics(&source);
    const std::vector<Token> tokens = LexSource(source, limits, diagnostics);
    const std::optional<SyntaxTree> syntax = ParseTokens(
        source,
        tokens,
        limits,
        diagnostics);
    if (!syntax.has_value() || diagnostics.HasErrors()) {
        return {{}, {}, std::move(diagnostics).Take()};
    }
    std::optional<BoundProgram> bound = BindProgram(
        source,
        *syntax,
        diagnostics);
    if (!bound.has_value() || diagnostics.HasErrors()) {
        return {{}, {}, std::move(diagnostics).Take()};
    }

    FinalizeResult finalized = LowerProgram(std::move(*bound));
    if (finalized.program == nullptr || !finalized.errors.empty()) {
        for (const ProgramValidationError& error : finalized.errors) {
            diagnostics.Add(
                CompileDiagnosticCode::InternalCompiler,
                {},
                "lowered program failed structural validation at "
                    + error.location + ": " + error.message);
        }
        if (finalized.errors.empty()) {
            diagnostics.Add(
                CompileDiagnosticCode::InternalCompiler,
                {},
                "lowering returned no compiled program");
        }
        return {{}, {}, std::move(diagnostics).Take()};
    }
    CompileOutput output;
    if (products == RequestedProduct::Dump
        || products == RequestedProduct::All) {
        output.dump = DumpCompiledProgram(*finalized.program);
    }
    if (products == RequestedProduct::Artifact
        || products == RequestedProduct::All) {
        output.artifact = EncodeWeavec(*finalized.program);
    }
    output.diagnostics = std::move(diagnostics).Take();
    return output;
}

void AddCommandError(
    CompilerCommandResult& result,
    CompileDiagnosticCode code,
    const std::filesystem::path& path,
    std::string message)
{
    if (result.diagnostics.size() >= kMaximumCompileDiagnostics) {
        return;
    }
    CompileDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.displayPath = PathToUtf8(path);
    diagnostic.message = std::move(message);
    result.diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] bool WriteArtifact(
    const std::filesystem::path& destination,
    const std::vector<std::uint8_t>& bytes,
    CompilerCommandResult& result)
{
    const std::filesystem::path temporary =
        artifact_file::MakeSiblingTemporaryPath(destination);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        AddCommandError(
            result,
            CompileDiagnosticCode::ArtifactWriteFailed,
            destination,
            "unable to create sibling temporary artifact");
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        output.close();
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        AddCommandError(
            result,
            CompileDiagnosticCode::ArtifactWriteFailed,
            destination,
            "unable to write complete temporary artifact");
        return false;
    }
    output.close();
    if (!output) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        AddCommandError(
            result,
            CompileDiagnosticCode::ArtifactWriteFailed,
            destination,
            "unable to close temporary artifact");
        return false;
    }
    std::string replaceError;
    if (!artifact_file::ReplaceDestination(
            temporary,
            destination,
            replaceError)) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        AddCommandError(
            result,
            CompileDiagnosticCode::ArtifactReplaceFailed,
            destination,
            "unable to replace destination artifact: " + replaceError);
        return false;
    }
    return true;
}

struct LoadedCompileResult final {
    CompilerCommandResult command;
    std::vector<std::uint8_t> artifact;
};

[[nodiscard]] LoadedCompileResult CompileLoadedFile(
    const std::filesystem::path& sourcePath,
    const CompilerLimits& limits,
    RequestedProduct products)
{
    DiagnosticSink loadDiagnostics;
    std::optional<SourceFile> source = LoadSourceFile(
        sourcePath,
        limits,
        loadDiagnostics);
    if (!source.has_value()) {
        LoadedCompileResult result;
        result.command.diagnostics = std::move(loadDiagnostics).Take();
        return result;
    }
    CompileOutput output = CompileValidatedSource(
        std::move(*source),
        limits,
        products);
    LoadedCompileResult result;
    result.command.succeeded = output.Succeeded();
    result.command.artifactByteLength = static_cast<std::uint64_t>(
        output.artifact.size());
    result.command.dump = std::move(output.dump);
    result.command.diagnostics = std::move(output.diagnostics);
    result.artifact = std::move(output.artifact);
    return result;
}

} // namespace

bool CompileOutput::Succeeded() const noexcept
{
    return diagnostics.empty();
}

std::string_view CompileDiagnosticCodeName(CompileDiagnosticCode code) noexcept
{
    switch (code) {
    case CompileDiagnosticCode::SourceReadFailed: return "IW1001";
    case CompileDiagnosticCode::SourceTooLarge: return "IW1002";
    case CompileDiagnosticCode::InvalidUtf8: return "IW1003";
    case CompileDiagnosticCode::InvalidDisplayPath: return "IW1004";
    case CompileDiagnosticCode::TokenLimit: return "IW1101";
    case CompileDiagnosticCode::SyntaxNodeLimit: return "IW1102";
    case CompileDiagnosticCode::SyntaxNestingLimit: return "IW1103";
    case CompileDiagnosticCode::InvalidCharacter: return "IW1104";
    case CompileDiagnosticCode::NonAsciiSyntax: return "IW1105";
    case CompileDiagnosticCode::UnterminatedString: return "IW1106";
    case CompileDiagnosticCode::InvalidEscape: return "IW1107";
    case CompileDiagnosticCode::UnterminatedBlockComment: return "IW1108";
    case CompileDiagnosticCode::NestedBlockComment: return "IW1109";
    case CompileDiagnosticCode::InvalidNumber: return "IW1110";
    case CompileDiagnosticCode::ExpectedToken: return "IW1201";
    case CompileDiagnosticCode::UnexpectedToken: return "IW1202";
    case CompileDiagnosticCode::InvalidPauseRule: return "IW1203";
    case CompileDiagnosticCode::DuplicateSetting: return "IW1301";
    case CompileDiagnosticCode::DuplicateSymbol: return "IW1302";
    case CompileDiagnosticCode::ReservedName: return "IW1303";
    case CompileDiagnosticCode::UnknownValue: return "IW1304";
    case CompileDiagnosticCode::UnknownControl: return "IW1305";
    case CompileDiagnosticCode::InvalidRawControl: return "IW1306";
    case CompileDiagnosticCode::TypeMismatch: return "IW1307";
    case CompileDiagnosticCode::InvalidDuration: return "IW1308";
    case CompileDiagnosticCode::SettingOutOfRange: return "IW1309";
    case CompileDiagnosticCode::ReadOnlyValue: return "IW1310";
    case CompileDiagnosticCode::ConstantEvaluation: return "IW1311";
    case CompileDiagnosticCode::EmbeddedNul: return "IW1312";
    case CompileDiagnosticCode::EmptyString: return "IW1313";
    case CompileDiagnosticCode::InternalCompiler: return "IW1401";
    case CompileDiagnosticCode::ArtifactWriteFailed: return "IW1501";
    case CompileDiagnosticCode::ArtifactReplaceFailed: return "IW1502";
    }
    return "IW0000";
}

std::string FormatCompileDiagnostic(const CompileDiagnostic& diagnostic)
{
    std::ostringstream output;
    output << diagnostic.displayPath << ':' << diagnostic.line << ':'
           << diagnostic.column << ": error "
           << CompileDiagnosticCodeName(diagnostic.code) << ": "
           << diagnostic.message << '\n';
    if (!diagnostic.sourceLine.empty()) {
        output << diagnostic.sourceLine << '\n';
        const std::uint32_t rawCaretColumn = diagnostic.column > 0U
            ? diagnostic.column - 1U
            : 0U;
        const std::uint32_t caretColumn = (std::min)(
            rawCaretColumn,
            static_cast<std::uint32_t>(diagnostic.sourceLine.size()));
        output << std::string(caretColumn, ' ') << '^';
        const std::uint32_t rawMarkerLength = diagnostic.primary.byteLength > 1U
            ? diagnostic.primary.byteLength - 1U
            : 0U;
        const std::uint32_t available = static_cast<std::uint32_t>(
            diagnostic.sourceLine.size()) - caretColumn;
        const std::uint32_t markerLength = (std::min)(rawMarkerLength, available);
        output << std::string(markerLength, '~') << '\n';
    }
    for (const RelatedCompileSpan& related : diagnostic.related) {
        output << "  related " << related.span.beginByte << '+'
               << related.span.byteLength << ": " << related.message << '\n';
    }
    return output.str();
}

CompileOutput CompileSource(
    std::string displayPath,
    std::string sourceBytes,
    const CompilerLimits& limits)
{
    DiagnosticSink diagnostics;
    std::optional<SourceFile> source = MakeSourceFile(
        std::move(displayPath),
        std::move(sourceBytes),
        limits,
        diagnostics);
    if (!source.has_value()) {
        return {{}, {}, std::move(diagnostics).Take()};
    }
    return CompileValidatedSource(
        std::move(*source),
        limits,
        RequestedProduct::All);
}

CompilerCommandResult CompileFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationPath,
    const CompilerLimits& limits)
{
    LoadedCompileResult loaded = CompileLoadedFile(
        sourcePath,
        limits,
        RequestedProduct::Artifact);
    if (!loaded.command.succeeded) {
        return std::move(loaded.command);
    }
    if (!WriteArtifact(destinationPath, loaded.artifact, loaded.command)) {
        loaded.command.succeeded = false;
        loaded.command.artifactByteLength = 0U;
    }
    return std::move(loaded.command);
}

CompilerCommandResult ValidateFile(
    const std::filesystem::path& sourcePath,
    const CompilerLimits& limits)
{
    LoadedCompileResult loaded = CompileLoadedFile(
        sourcePath,
        limits,
        RequestedProduct::None);
    return std::move(loaded.command);
}

CompilerCommandResult DumpFile(
    const std::filesystem::path& sourcePath,
    const CompilerLimits& limits)
{
    LoadedCompileResult loaded = CompileLoadedFile(
        sourcePath,
        limits,
        RequestedProduct::Dump);
    return std::move(loaded.command);
}

} // namespace inputweaver::compiler
