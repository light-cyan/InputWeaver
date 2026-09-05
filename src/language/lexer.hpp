#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::language {

inline constexpr std::size_t kDefaultMaximumStoredLexemes = 1U << 20U;
inline constexpr std::size_t kDefaultMaximumLexicalIssues = 1024U;

enum class LexemeKind : std::uint8_t {
    Whitespace,
    LineComment,
    BlockComment,
    Word,
    Number,
    Duration,
    HexInteger,
    String,
    IncompleteString,
    Equal,
    MappingArrow,
    ConsumeStop,
    ConsumeContinue,
    ObserveStop,
    ObserveContinue,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Colon,
    Semicolon,
    Dot,
    Comma,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Pipe,
    At,
    Invalid,
    EndOfInput,
};

struct LexemeSpan final {
    std::size_t beginByte{};
    std::size_t byteLength{};

    [[nodiscard]] constexpr std::size_t EndByte() const noexcept
    {
        return beginByte + byteLength;
    }
};

struct Lexeme final {
    LexemeKind kind{LexemeKind::Invalid};
    LexemeSpan span{};
    std::string_view text;
};

enum class LexicalIssueCode : std::uint8_t {
    InvalidCharacter,
    NonAsciiSyntax,
    EmbeddedNul,
    InvalidNumber,
    UnterminatedString,
    InvalidEscape,
    UnterminatedBlockComment,
    NestedBlockComment,
};

struct LexicalIssue final {
    LexicalIssueCode code{};
    LexemeSpan span{};
};

struct LexerState final {
    std::uint32_t blockCommentDepth{};
    std::size_t blockCommentOpeningByte{};
};

struct ScanOptions final {
    std::size_t baseByteOffset{};
    LexerState initialState{};
    bool finalInput{true};
    bool retainTrivia{true};
    std::size_t maximumSignificantLexemes{
        (std::numeric_limits<std::size_t>::max)()};
    std::size_t maximumStoredLexemes{kDefaultMaximumStoredLexemes};
    std::size_t maximumIssues{kDefaultMaximumLexicalIssues};
};

struct ScanResult final {
    std::vector<Lexeme> lexemes;
    std::vector<LexicalIssue> issues;
    LexerState finalState{};
    bool significantLexemeLimitReached{};
    bool storedLexemeLimitReached{};
    bool issueLimitReached{};
};

struct StringDecodeResult final {
    std::string value;
    std::vector<LexicalIssue> issues;
    bool terminated{};
    bool issueLimitReached{};
};

[[nodiscard]] constexpr bool IsTrivia(LexemeKind kind) noexcept
{
    return kind == LexemeKind::Whitespace
        || kind == LexemeKind::LineComment
        || kind == LexemeKind::BlockComment;
}

[[nodiscard]] ScanResult ScanWeave(
    std::string_view source,
    const ScanOptions& options = {});

[[nodiscard]] StringDecodeResult DecodeStringLiteral(
    std::string_view source,
    std::size_t baseByteOffset = 0U,
    std::size_t maximumIssues = kDefaultMaximumLexicalIssues);

} // namespace inputweaver::language
