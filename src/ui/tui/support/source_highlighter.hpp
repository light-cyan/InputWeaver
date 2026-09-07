#pragma once

#include "language/lexer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::ui::tui {

class SourceEditor;

enum class SourceTokenKind {
    Plain,
    Keyword,
    Type,
    Variable,
    Constant,
    Control,
    Action,
    Operator,
    String,
    Comment,
};

struct SourceTokenSpan final {
    std::size_t beginByte{};
    std::size_t endByte{};
    SourceTokenKind kind{SourceTokenKind::Keyword};
};

struct SourceHighlightState final {
    language::LexerState lexer{};
    std::set<std::string, std::less<>> scalarNames;
    std::set<std::string, std::less<>> arrayNames;
    std::set<std::string, std::less<>> meterNames;
    bool expectsDeclarationName{};
    bool declarationIsArray{};
    bool declarationIsMeter{};
};

[[nodiscard]] std::vector<SourceTokenSpan> HighlightWeaveLine(
    std::string_view line,
    SourceHighlightState& state);

class SourceHighlightDocument final {
public:
    [[nodiscard]] bool Update(const SourceEditor& editor);
    [[nodiscard]] std::span<const SourceTokenSpan> Line(
        std::size_t index) const noexcept;

private:
    std::uint64_t revision_{};
    std::vector<std::vector<SourceTokenSpan>> lines_;
};

} // namespace inputweaver::ui::tui
