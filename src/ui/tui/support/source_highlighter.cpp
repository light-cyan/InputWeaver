#include "source_highlighter.hpp"

#include "language/word_catalog.hpp"
#include "source_editor.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace inputweaver::ui::tui {
namespace {

[[nodiscard]] bool IsArrow(language::LexemeKind kind) noexcept
{
    switch (kind) {
    case language::LexemeKind::MappingArrow:
    case language::LexemeKind::ConsumeStop:
    case language::LexemeKind::ConsumeContinue:
    case language::LexemeKind::ObserveStop:
    case language::LexemeKind::ObserveContinue:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::size_t NextSyntaxLexeme(
    const std::vector<language::Lexeme>& lexemes,
    std::size_t index) noexcept
{
    do {
        ++index;
    } while (index < lexemes.size()
        && language::IsTrivia(lexemes[index].kind));
    return index;
}

[[nodiscard]] std::size_t PreviousSyntaxLexeme(
    const std::vector<language::Lexeme>& lexemes,
    std::size_t index) noexcept
{
    while (index != 0U) {
        --index;
        if (!language::IsTrivia(lexemes[index].kind)) return index;
    }
    return lexemes.size();
}

struct DottedRun final {
    std::size_t end{};
    std::size_t wordCount{1U};
    bool controlSpelling{};
};

[[nodiscard]] bool IsDottedRunStart(
    const std::vector<language::Lexeme>& lexemes,
    std::size_t index) noexcept
{
    const std::size_t dot = PreviousSyntaxLexeme(lexemes, index);
    if (dot >= lexemes.size()
        || lexemes[dot].kind != language::LexemeKind::Dot) return true;
    const std::size_t word = PreviousSyntaxLexeme(lexemes, dot);
    return word >= lexemes.size()
        || lexemes[word].kind != language::LexemeKind::Word;
}

[[nodiscard]] DottedRun ScanDottedRun(
    const std::vector<language::Lexeme>& lexemes,
    std::size_t begin) noexcept
{
    const auto startsUpper = [&lexemes](std::size_t word) {
        const std::string_view text = lexemes[word].text;
        return !text.empty() && text.front() >= 'A' && text.front() <= 'Z';
    };
    DottedRun run{begin, 1U, startsUpper(begin)};
    for (;;) {
        const std::size_t dot = NextSyntaxLexeme(lexemes, run.end);
        const std::size_t word = dot < lexemes.size()
            && lexemes[dot].kind == language::LexemeKind::Dot
            ? NextSyntaxLexeme(lexemes, dot) : lexemes.size();
        if (word >= lexemes.size()
            || lexemes[word].kind != language::LexemeKind::Word) break;
        run.end = word;
        ++run.wordCount;
        run.controlSpelling = run.controlSpelling && startsUpper(word);
    }
    return run;
}

[[nodiscard]] bool IsArrayLengthWord(
    const std::vector<language::Lexeme>& lexemes,
    std::size_t index,
    const SourceHighlightState& state)
{
    if (lexemes[index].text != "length") return false;
    const std::size_t dot = PreviousSyntaxLexeme(lexemes, index);
    const std::size_t array = dot < lexemes.size()
        ? PreviousSyntaxLexeme(lexemes, dot) : lexemes.size();
    return array < lexemes.size()
        && lexemes[dot].kind == language::LexemeKind::Dot
        && lexemes[array].kind == language::LexemeKind::Word
        && state.arrayNames.contains(lexemes[array].text);
}

} // namespace

std::vector<SourceTokenSpan> HighlightWeaveLine(
    std::string_view line,
    SourceHighlightState& state)
{
    language::ScanOptions options{};
    options.initialState = state.lexer;
    options.finalInput = false;
    options.maximumIssues = 0U;
    language::ScanResult scan = language::ScanWeave(line, options);
    state.lexer = scan.finalState;

    std::vector<SourceTokenSpan> spans;
    const auto add = [&spans](const language::Lexeme& lexeme,
                             SourceTokenKind kind) {
        spans.push_back({lexeme.span.beginByte, lexeme.span.EndByte(), kind});
    };
    for (std::size_t index = 0U; index < scan.lexemes.size(); ++index) {
        const language::Lexeme& lexeme = scan.lexemes[index];
        if (lexeme.kind == language::LexemeKind::EndOfInput) break;
        if (lexeme.kind == language::LexemeKind::LineComment
            || lexeme.kind == language::LexemeKind::BlockComment) {
            add(lexeme, SourceTokenKind::Comment);
            continue;
        }
        if (lexeme.kind == language::LexemeKind::Whitespace) continue;

        if (state.expectsDeclarationName
            && lexeme.kind != language::LexemeKind::Word) {
            if (lexeme.kind == language::LexemeKind::LeftBracket) {
                state.declarationIsArray = true;
            } else if (lexeme.kind != language::LexemeKind::RightBracket
                || !state.declarationIsArray) {
                state.expectsDeclarationName = false;
                state.declarationIsArray = false;
            }
        }
        if (lexeme.kind == language::LexemeKind::String
            || lexeme.kind == language::LexemeKind::IncompleteString) {
            add(lexeme, SourceTokenKind::String);
            continue;
        }
        if (lexeme.kind == language::LexemeKind::Number
            || lexeme.kind == language::LexemeKind::Duration
            || lexeme.kind == language::LexemeKind::HexInteger) {
            add(lexeme, SourceTokenKind::Constant);
            continue;
        }
        if (lexeme.kind == language::LexemeKind::Pipe) {
            add(lexeme, SourceTokenKind::Action);
            continue;
        }
        if (IsArrow(lexeme.kind)) {
            add(lexeme, SourceTokenKind::Operator);
            continue;
        }
        if (lexeme.kind != language::LexemeKind::Word) continue;

        const language::WordRole role = language::LookupWordRole(lexeme.text);
        if (role == language::WordRole::Type) {
            add(lexeme, SourceTokenKind::Type);
            state.expectsDeclarationName = true;
            state.declarationIsArray = false;
            continue;
        }
        if (state.expectsDeclarationName) {
            add(lexeme, SourceTokenKind::Variable);
            auto& names = state.declarationIsArray
                ? state.arrayNames : state.scalarNames;
            names.emplace(lexeme.text);
            state.expectsDeclarationName = false;
            state.declarationIsArray = false;
            continue;
        }

        const bool isArray = state.arrayNames.contains(lexeme.text);
        const bool runStart = IsDottedRunStart(scan.lexemes, index);
        const DottedRun run = runStart
            ? ScanDottedRun(scan.lexemes, index)
            : DottedRun{index};
        const bool dottedMember = !runStart || run.wordCount > 1U;
        const bool arrayLength = run.wordCount == 2U
            && scan.lexemes[run.end].text == "length"
            && isArray;
        if (run.controlSpelling && run.wordCount > 1U && !arrayLength) {
            for (std::size_t cursor = index; cursor <= run.end; ++cursor) {
                const language::Lexeme& part = scan.lexemes[cursor];
                if (part.kind == language::LexemeKind::Word) {
                    add(part, SourceTokenKind::Control);
                } else if (part.kind == language::LexemeKind::BlockComment
                    || part.kind == language::LexemeKind::LineComment) {
                    add(part, SourceTokenKind::Comment);
                }
            }
            index = run.end;
        } else if (isArray || state.scalarNames.contains(lexeme.text)
            || IsArrayLengthWord(scan.lexemes, index, state)) {
            add(lexeme, SourceTokenKind::Variable);
        } else if (role == language::WordRole::IntrinsicValue) {
            add(lexeme, SourceTokenKind::Variable);
        } else if (role == language::WordRole::Constant
            || role == language::WordRole::Transition
            || role == language::WordRole::ScanPrefix) {
            add(lexeme, SourceTokenKind::Constant);
        } else if (role == language::WordRole::Action) {
            add(lexeme, SourceTokenKind::Action);
        } else if (role == language::WordRole::Keyword) {
            add(lexeme, SourceTokenKind::Keyword);
        } else if (!dottedMember
            && lexeme.text.front() >= 'A' && lexeme.text.front() <= 'Z') {
            add(lexeme, SourceTokenKind::Control);
        }
    }
    return spans;
}

bool SourceHighlightDocument::Update(const SourceEditor& editor)
{
    if (revision_ == editor.Revision()
        && lines_.size() == editor.LineCount()) {
        return false;
    }
    SourceHighlightState state{};
    std::vector<std::vector<SourceTokenSpan>> lines(editor.LineCount());
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        lines[index] = HighlightWeaveLine(editor.Line(index), state);
    }
    lines_ = std::move(lines);
    revision_ = editor.Revision();
    return true;
}

std::span<const SourceTokenSpan> SourceHighlightDocument::Line(
    std::size_t index) const noexcept
{
    return index < lines_.size()
        ? std::span<const SourceTokenSpan>{lines_[index]}
        : std::span<const SourceTokenSpan>{};
}

} // namespace inputweaver::ui::tui
