#include "source.hpp"

#include "path.hpp"
#include "support/utf8.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace inputweaver::compiler {
namespace {

void DeriveLineStarts(SourceFile& source)
{
    source.lineStarts.clear();
    source.lineStarts.push_back(0U);
    std::size_t index = 0U;
    while (index < source.bytes.size()) {
        std::size_t next = index + 1U;
        bool lineBreak = false;
        if (source.bytes[index] == '\r') {
            lineBreak = true;
            if (next < source.bytes.size() && source.bytes[next] == '\n') {
                ++next;
            }
        } else if (source.bytes[index] == '\n') {
            lineBreak = true;
        }
        if (lineBreak) {
            source.lineStarts.push_back(static_cast<std::uint32_t>(next));
        }
        index = next;
    }
}

} // namespace

SourceSpan SourceFile::WholeSpan() const noexcept
{
    return {0U, static_cast<std::uint32_t>(bytes.size())};
}

std::pair<std::uint32_t, std::uint32_t> SourceFile::LineColumn(
    SourceSpan span) const noexcept
{
    const auto position = std::upper_bound(
        lineStarts.begin(),
        lineStarts.end(),
        span.beginByte);
    const std::size_t lineIndex = position == lineStarts.begin()
        ? 0U
        : static_cast<std::size_t>(position - lineStarts.begin() - 1);
    const std::uint32_t lineStart = lineStarts.empty() ? 0U : lineStarts[lineIndex];
    return {
        static_cast<std::uint32_t>(lineIndex + 1U),
        span.beginByte - lineStart + 1U};
}

std::string SourceFile::SourceLine(SourceSpan span) const
{
    if (bytes.empty() || span.beginByte > bytes.size()) {
        return {};
    }
    const auto [line, column] = LineColumn(span);
    static_cast<void>(column);
    const std::size_t lineIndex = static_cast<std::size_t>(line - 1U);
    const std::size_t begin = lineStarts[lineIndex];
    std::size_t end = bytes.find_first_of("\r\n", begin);
    if (end == std::string::npos) {
        end = bytes.size();
    }
    constexpr std::size_t kMaximumExcerptBytes = 512U;
    const std::size_t count = (std::min)(end - begin, kMaximumExcerptBytes);
    return bytes.substr(begin, count);
}

DiagnosticSink::DiagnosticSink(const SourceFile* source) noexcept
    : source_(source)
{
}

void DiagnosticSink::SetSource(const SourceFile* source) noexcept
{
    source_ = source;
}

void DiagnosticSink::Add(
    CompileDiagnosticCode code,
    SourceSpan primary,
    std::string message,
    std::vector<RelatedCompileSpan> related)
{
    if (Full()) {
        return;
    }
    if (related.size() > kMaximumRelatedDiagnosticSpans) {
        related.resize(kMaximumRelatedDiagnosticSpans);
    }
    CompileDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.primary = primary;
    diagnostic.message = std::move(message);
    diagnostic.related = std::move(related);
    if (source_ != nullptr) {
        diagnostic.displayPath = source_->displayPath;
        const auto [line, column] = source_->LineColumn(primary);
        diagnostic.line = line;
        diagnostic.column = column;
        diagnostic.sourceLine = source_->SourceLine(primary);
    }
    diagnostics_.push_back(std::move(diagnostic));
}

bool DiagnosticSink::HasErrors() const noexcept
{
    return !diagnostics_.empty();
}

bool DiagnosticSink::Full() const noexcept
{
    return diagnostics_.size() >= kMaximumCompileDiagnostics;
}

std::vector<CompileDiagnostic> DiagnosticSink::Take() &&
{
    return std::move(diagnostics_);
}

std::optional<SourceFile> MakeSourceFile(
    std::string displayPath,
    std::string bytes,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics)
{
    SourceFile source{std::move(displayPath), std::move(bytes), {0U}};
    diagnostics.SetSource(&source);
    if (source.displayPath.find('\0') != std::string::npos) {
        diagnostics.Add(
            CompileDiagnosticCode::InvalidDisplayPath,
            {},
            "source display path contains an embedded NUL");
        return std::nullopt;
    }
    if (source.bytes.size() > limits.maximumSourceBytes
        || source.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.Add(
            CompileDiagnosticCode::SourceTooLarge,
            {},
            "source file exceeds the configured byte limit");
        return std::nullopt;
    }
    DeriveLineStarts(source);
    if (!support::IsValidUtf8(source.bytes)) {
        diagnostics.Add(
            CompileDiagnosticCode::InvalidUtf8,
            source.WholeSpan(),
            "source file is not valid UTF-8");
        return std::nullopt;
    }
    diagnostics.SetSource(nullptr);
    return source;
}

std::optional<SourceFile> LoadSourceFile(
    const std::filesystem::path& path,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics)
{
    const std::string displayPath = PathToUtf8(path);
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        SourceFile source{displayPath, {}, {0U}};
        diagnostics.SetSource(&source);
        diagnostics.Add(
            CompileDiagnosticCode::SourceReadFailed,
            {},
            "unable to inspect source file: " + sizeError.message());
        diagnostics.SetSource(nullptr);
        return std::nullopt;
    }
    if (size > limits.maximumSourceBytes
        || size > std::numeric_limits<std::uint32_t>::max()) {
        SourceFile source{displayPath, {}, {0U}};
        diagnostics.SetSource(&source);
        diagnostics.Add(
            CompileDiagnosticCode::SourceTooLarge,
            {},
            "source file exceeds the configured byte limit");
        diagnostics.SetSource(nullptr);
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SourceFile source{displayPath, {}, {0U}};
        diagnostics.SetSource(&source);
        diagnostics.Add(
            CompileDiagnosticCode::SourceReadFailed,
            {},
            "unable to open source file");
        diagnostics.SetSource(nullptr);
        return std::nullopt;
    }
    std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (!input.eof() && input.fail()) {
        SourceFile source{displayPath, {}, {0U}};
        diagnostics.SetSource(&source);
        diagnostics.Add(
            CompileDiagnosticCode::SourceReadFailed,
            {},
            "unable to read source file");
        diagnostics.SetSource(nullptr);
        return std::nullopt;
    }
    return MakeSourceFile(displayPath, std::move(bytes), limits, diagnostics);
}

} // namespace inputweaver::compiler
