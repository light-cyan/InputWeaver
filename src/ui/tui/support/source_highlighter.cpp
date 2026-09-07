#include "source_highlighter.hpp"

#include "language/mouse_field_catalog.hpp"
#include "language/word_catalog.hpp"
#include "source_editor.hpp"

#include <algorithm>
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

[[nodiscard]] bool StartsUpper(std::string_view word) noexcept
{
    return !word.empty() && word.front() >= 'A' && word.front() <= 'Z';
}

using HighlightLines = std::vector<std::vector<SourceTokenSpan>>;

// Only an unfinished dotted reference survives a line scan. Its span indices
// let classification cross comments and newlines without retaining all lexemes.
class HighlightStream final {
public:
    HighlightStream(SourceHighlightState& state, HighlightLines& lines)
        : state_(state), lines_(lines)
    {
    }

    void ScanLine(std::string_view line, std::size_t lineIndex)
    {
        lineIndex_ = lineIndex;
        language::ScanOptions options{};
        options.initialState = state_.lexer;
        options.finalInput = false;
        options.maximumIssues = 0U;
        const auto scan = language::ScanWeave(line, options);
        state_.lexer = scan.finalState;
        for (const auto& lexeme : scan.lexemes) {
            if (lexeme.kind == language::LexemeKind::EndOfInput) break;
            Accept(lexeme);
        }
    }

    void Finish()
    {
        FlushReference(language::LexemeKind::EndOfInput);
        for (auto& line : lines_) {
            std::erase_if(line, [](const auto& span) {
                return span.kind == SourceTokenKind::Plain;
            });
        }
    }

private:
    struct ReferenceWord final {
        std::string_view text;
        std::size_t line{};
        std::size_t span{};
    };

    void Add(const language::Lexeme& lexeme, SourceTokenKind kind)
    {
        lines_[lineIndex_].push_back(
            {lexeme.span.beginByte, lexeme.span.EndByte(), kind});
    }

    void AddWord(const language::Lexeme& lexeme)
    {
        words_.push_back({lexeme.text, lineIndex_, lines_[lineIndex_].size()});
        Add(lexeme, SourceTokenKind::Plain);
        afterDot_ = false;
    }

    void Color(const ReferenceWord& word, SourceTokenKind kind)
    {
        lines_[word.line][word.span].kind = kind;
    }

    void ResetDeclaration()
    {
        state_.expectsDeclarationName = false;
        state_.declarationIsArray = false;
        state_.declarationIsMeter = false;
    }

    void ClassifyRoot(const ReferenceWord& word, bool dotted,
        language::LexemeKind following)
    {
        const auto role = language::LookupWordRole(word.text);
        if (role == language::WordRole::Type) {
            Color(word, SourceTokenKind::Type);
            state_.expectsDeclarationName = true;
            state_.declarationIsArray = false;
            state_.declarationIsMeter = word.text == "meter";
        } else if (state_.expectsDeclarationName) {
            Color(word, SourceTokenKind::Variable);
            auto& names = state_.declarationIsMeter ? state_.meterNames
                : state_.declarationIsArray ? state_.arrayNames : state_.scalarNames;
            names.emplace(word.text);
            ResetDeclaration();
        } else if (word.text == "Mouse") {
            Color(word, following == language::LexemeKind::Colon && !dotted
                ? SourceTokenKind::Control : SourceTokenKind::Variable);
        } else if (state_.scalarNames.contains(word.text)
            || state_.arrayNames.contains(word.text)
            || state_.meterNames.contains(word.text)
            || role == language::WordRole::IntrinsicValue) {
            Color(word, SourceTokenKind::Variable);
        } else if (role == language::WordRole::Constant
            || role == language::WordRole::Transition
            || role == language::WordRole::ScanPrefix) {
            Color(word, SourceTokenKind::Constant);
        } else if (role == language::WordRole::Action) {
            Color(word, SourceTokenKind::Action);
        } else if (role == language::WordRole::Keyword) {
            Color(word, SourceTokenKind::Keyword);
        } else if (!dotted && StartsUpper(word.text)) {
            Color(word, SourceTokenKind::Control);
        }
    }

    void FlushReference(language::LexemeKind following)
    {
        if (words_.empty()) return;
        const bool dotted = words_.size() > 1U || afterDot_;
        const bool control = words_.size() > 1U
            && std::all_of(words_.begin(), words_.end(), [](const auto& word) {
                return StartsUpper(word.text);
            });
        if (control) {
            for (const auto& word : words_) Color(word, SourceTokenKind::Control);
            ResetDeclaration();
        } else {
            const auto& root = words_.front();
            ClassifyRoot(root, dotted, following);
            if (words_.size() == 2U) {
                const auto& member = words_[1];
                const auto* field = language::FindMouseFieldWord(member.text);
                const bool mouseField = root.text == "Mouse" && field != nullptr && field->mouseState;
                const bool meterField = state_.meterNames.contains(root.text)
                    && field != nullptr && field->meter;
                const bool arrayLength = state_.arrayNames.contains(root.text) && member.text == "length";
                if (mouseField || meterField || arrayLength) {
                    Color(member, SourceTokenKind::Variable);
                }
            }
        }
        words_.clear();
        afterDot_ = false;
    }

    void Accept(const language::Lexeme& lexeme)
    {
        using language::LexemeKind;
        if (lexeme.kind == LexemeKind::LineComment
            || lexeme.kind == LexemeKind::BlockComment) {
            Add(lexeme, SourceTokenKind::Comment);
            return;
        }
        if (lexeme.kind == LexemeKind::Whitespace) return;

        if (!words_.empty()) {
            if (!afterDot_ && lexeme.kind == LexemeKind::Dot) {
                afterDot_ = true;
                return;
            }
            if (afterDot_ && lexeme.kind == LexemeKind::Word) {
                AddWord(lexeme);
                return;
            }
            FlushReference(lexeme.kind);
        }
        if (lexeme.kind == LexemeKind::Word) {
            AddWord(lexeme);
            return;
        }
        if (state_.expectsDeclarationName) {
            if (lexeme.kind == LexemeKind::LeftBracket) {
                state_.declarationIsArray = true;
            } else if (lexeme.kind != LexemeKind::RightBracket
                || !state_.declarationIsArray) {
                ResetDeclaration();
            }
        }
        if (lexeme.kind == LexemeKind::String
            || lexeme.kind == LexemeKind::IncompleteString) {
            Add(lexeme, SourceTokenKind::String);
        } else if (lexeme.kind == LexemeKind::Number
            || lexeme.kind == LexemeKind::Duration
            || lexeme.kind == LexemeKind::HexInteger) {
            Add(lexeme, SourceTokenKind::Constant);
        } else if (lexeme.kind == LexemeKind::At) {
            Add(lexeme, SourceTokenKind::Variable);
        } else if (lexeme.kind == LexemeKind::Pipe) {
            Add(lexeme, SourceTokenKind::Action);
        } else if (IsArrow(lexeme.kind)) {
            Add(lexeme, SourceTokenKind::Operator);
        }
    }

    SourceHighlightState& state_;
    HighlightLines& lines_;
    std::size_t lineIndex_{};
    std::vector<ReferenceWord> words_;
    bool afterDot_{};
};

} // namespace

std::vector<SourceTokenSpan> HighlightWeaveLine(
    std::string_view line,
    SourceHighlightState& state)
{
    HighlightLines lines(1U);
    HighlightStream stream(state, lines);
    stream.ScanLine(line, 0U);
    stream.Finish();
    return std::move(lines.front());
}

bool SourceHighlightDocument::Update(const SourceEditor& editor)
{
    if (revision_ == editor.Revision()
        && lines_.size() == editor.LineCount()) {
        return false;
    }
    SourceHighlightState state{};
    HighlightLines lines(editor.LineCount());
    HighlightStream stream(state, lines);
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        stream.ScanLine(editor.Line(index), index);
    }
    stream.Finish();
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
