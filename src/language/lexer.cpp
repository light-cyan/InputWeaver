#include "lexer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace inputweaver::language {
namespace {

[[nodiscard]] bool IsAsciiLetter(char value) noexcept
{
    return (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z');
}

[[nodiscard]] bool IsDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool IsHexDigit(char value) noexcept
{
    return IsDigit(value)
        || (value >= 'A' && value <= 'F')
        || (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool IsWordCharacter(char value) noexcept
{
    return IsAsciiLetter(value) || IsDigit(value) || value == '_';
}

[[nodiscard]] std::size_t Utf8SequenceLength(unsigned char first) noexcept
{
    if ((first & 0xe0U) == 0xc0U) return 2U;
    if ((first & 0xf0U) == 0xe0U) return 3U;
    if ((first & 0xf8U) == 0xf0U) return 4U;
    return 1U;
}

struct ProcessedString final {
    std::size_t consumed{};
    bool terminated{};
};

template <typename ReportIssue>
[[nodiscard]] ProcessedString ProcessString(
    std::string_view source,
    std::size_t baseByteOffset,
    bool finalInput,
    std::string* decoded,
    ReportIssue&& reportIssue)
{
    ProcessedString result;
    if (source.empty() || source.front() != '"') return result;

    std::size_t position = 1U;
    bool physicalLineBreak{};
    while (position < source.size()) {
        const unsigned char value = static_cast<unsigned char>(source[position]);
        if (value == '"') {
            result.terminated = true;
            ++position;
            break;
        }
        if (value == '\r' || value == '\n') {
            physicalLineBreak = true;
            break;
        }
        if (value >= 0x80U) {
            const std::size_t count = (std::min)(
                Utf8SequenceLength(value), source.size() - position);
            reportIssue(LexicalIssueCode::NonAsciiSyntax,
                LexemeSpan{baseByteOffset + position, count});
            position += count;
            continue;
        }
        if (value == '\0') {
            reportIssue(LexicalIssueCode::EmbeddedNul,
                LexemeSpan{baseByteOffset + position, 1U});
            ++position;
            continue;
        }
        if (value != '\\') {
            if (decoded != nullptr) decoded->push_back(static_cast<char>(value));
            ++position;
            continue;
        }

        const std::size_t escapeBegin = position++;
        if (position >= source.size()) break;
        if (source[position] == '\r' || source[position] == '\n') {
            physicalLineBreak = true;
            break;
        }
        const char escaped = source[position++];
        char decodedValue{};
        switch (escaped) {
        case '\\': decodedValue = '\\'; break;
        case '"': decodedValue = '"'; break;
        case 'n': decodedValue = '\n'; break;
        case 'r': decodedValue = '\r'; break;
        case 't': decodedValue = '\t'; break;
        default:
            reportIssue(LexicalIssueCode::InvalidEscape,
                LexemeSpan{baseByteOffset + escapeBegin, position - escapeBegin});
            continue;
        }
        if (decoded != nullptr) decoded->push_back(decodedValue);
    }
    result.consumed = position;
    if (!result.terminated && (physicalLineBreak || finalInput)) {
        reportIssue(LexicalIssueCode::UnterminatedString,
            LexemeSpan{baseByteOffset, position});
    }
    return result;
}

class Scanner final {
public:
    Scanner(std::string_view source, const ScanOptions& options) noexcept
        : source_(source), options_(options), state_(options.initialState)
    {
    }

    [[nodiscard]] ScanResult Run()
    {
        while (position_ < source_.size()) {
            const bool trivia = state_.blockCommentDepth != 0U
                || IsWhitespace(source_[position_])
                || StartsWith("//") || StartsWith("/*");
            if (!BeginLexeme(!trivia)) break;
            if (state_.blockCommentDepth != 0U) ScanBlockComment(false);
            else if (IsWhitespace(source_[position_])) ScanWhitespace();
            else if (StartsWith("//")) ScanLineComment();
            else if (StartsWith("/*")) ScanBlockComment(true);
            else ScanToken();
        }
        if (position_ == source_.size() && options_.finalInput
            && state_.blockCommentDepth != 0U) {
            const std::size_t end = options_.baseByteOffset + source_.size();
            const std::size_t begin = (std::min)(state_.blockCommentOpeningByte, end);
            Issue(LexicalIssueCode::UnterminatedBlockComment,
                {begin, end - begin});
        }
        lexemes_.push_back({LexemeKind::EndOfInput,
            {options_.baseByteOffset + position_, 0U}, {}});
        return {std::move(lexemes_), std::move(issues_), state_,
            significantLimitReached_, storedLimitReached_, issueLimitReached_};
    }

private:
    [[nodiscard]] static bool IsWhitespace(char value) noexcept
    {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    }

    [[nodiscard]] bool StartsWith(std::string_view value) const noexcept
    {
        return source_.substr(position_).starts_with(value);
    }

    [[nodiscard]] bool BeginLexeme(bool significant) noexcept
    {
        if (significant
            && significantLexemeCount_ >= options_.maximumSignificantLexemes) {
            significantLimitReached_ = true;
            return false;
        }
        if ((significant || options_.retainTrivia)
            && lexemes_.size() >= options_.maximumStoredLexemes) {
            storedLimitReached_ = true;
            return false;
        }
        if (significant) ++significantLexemeCount_;
        return true;
    }

    void Add(LexemeKind kind, std::size_t begin, std::size_t end)
    {
        if (!options_.retainTrivia && IsTrivia(kind)) return;
        lexemes_.push_back({kind,
            {options_.baseByteOffset + begin, end - begin},
            source_.substr(begin, end - begin)});
    }

    void Issue(LexicalIssueCode code, LexemeSpan span)
    {
        if (issues_.size() < options_.maximumIssues) {
            issues_.push_back({code, span});
        } else {
            issueLimitReached_ = true;
        }
    }

    void Issue(LexicalIssueCode code, std::size_t begin, std::size_t length)
    {
        Issue(code, {options_.baseByteOffset + begin, length});
    }

    void ScanWhitespace()
    {
        const std::size_t begin = position_++;
        while (position_ < source_.size() && IsWhitespace(source_[position_])) {
            ++position_;
        }
        Add(LexemeKind::Whitespace, begin, position_);
    }

    void ScanLineComment()
    {
        const std::size_t begin = position_;
        position_ += 2U;
        while (position_ < source_.size()
            && source_[position_] != '\r' && source_[position_] != '\n') {
            if (source_[position_] == '\0') {
                Issue(LexicalIssueCode::EmbeddedNul, position_, 1U);
            }
            ++position_;
        }
        Add(LexemeKind::LineComment, begin, position_);
    }

    void ScanBlockComment(bool opening)
    {
        const std::size_t begin = position_;
        if (opening) {
            state_.blockCommentDepth = 1U;
            state_.blockCommentOpeningByte = options_.baseByteOffset + position_;
            position_ += 2U;
        }
        while (position_ < source_.size() && state_.blockCommentDepth != 0U) {
            if (StartsWith("/*")) {
                Issue(LexicalIssueCode::NestedBlockComment, position_, 2U);
                if (state_.blockCommentDepth
                    != (std::numeric_limits<std::uint32_t>::max)()) {
                    ++state_.blockCommentDepth;
                }
                position_ += 2U;
            } else if (StartsWith("*/")) {
                --state_.blockCommentDepth;
                position_ += 2U;
            } else {
                if (source_[position_] == '\0') {
                    Issue(LexicalIssueCode::EmbeddedNul, position_, 1U);
                }
                ++position_;
            }
        }
        Add(LexemeKind::BlockComment, begin, position_);
    }

    void ScanWord()
    {
        const std::size_t begin = position_++;
        while (position_ < source_.size()
            && IsWordCharacter(source_[position_])) ++position_;
        Add(LexemeKind::Word, begin, position_);
    }

    void ConsumeNumberTail()
    {
        while (position_ < source_.size()
            && (IsWordCharacter(source_[position_]) || source_[position_] == '.')) {
            ++position_;
        }
    }

    void FinishNumber(std::size_t begin, LexemeKind kind, bool invalid)
    {
        if (invalid) {
            Issue(LexicalIssueCode::InvalidNumber, begin, position_ - begin);
            kind = LexemeKind::Invalid;
        }
        Add(kind, begin, position_);
    }

    void ScanNumber()
    {
        const std::size_t begin = position_;
        if (source_.substr(position_).starts_with("0x")) {
            position_ += 2U;
            const std::size_t digits = position_;
            while (position_ < source_.size()
                && IsHexDigit(source_[position_])) ++position_;
            bool invalid = position_ == digits;
            if (position_ < source_.size()
                && (IsWordCharacter(source_[position_])
                    || source_[position_] == '.')) {
                ConsumeNumberTail();
                invalid = true;
            }
            FinishNumber(begin, LexemeKind::HexInteger, invalid);
            return;
        }

        const bool leadingDot = source_[position_] == '.';
        bool invalid = leadingDot;
        if (leadingDot) ++position_;
        while (position_ < source_.size() && IsDigit(source_[position_])) {
            ++position_;
        }
        if (!leadingDot && position_ < source_.size()
            && source_[position_] == '.') {
            ++position_;
            const std::size_t fraction = position_;
            while (position_ < source_.size() && IsDigit(source_[position_])) {
                ++position_;
            }
            invalid = position_ == fraction;
        }
        if (position_ < source_.size()
            && (source_[position_] == 'e' || source_[position_] == 'E')) {
            invalid = true;
            ++position_;
            if (position_ < source_.size()
                && (source_[position_] == '+' || source_[position_] == '-')) {
                ++position_;
            }
            while (position_ < source_.size() && IsDigit(source_[position_])) {
                ++position_;
            }
        }

        if (!invalid) {
            constexpr std::array suffixes{
                std::string_view{"min"}, std::string_view{"ms"},
                std::string_view{"s"}};
            for (const std::string_view suffix : suffixes) {
                if (source_.substr(position_).starts_with(suffix)) {
                    const std::size_t after = position_ + suffix.size();
                    if (after == source_.size()
                        || !IsWordCharacter(source_[after])) {
                        position_ = after;
                        FinishNumber(begin, LexemeKind::Duration, false);
                        return;
                    }
                }
            }
        }
        if (position_ < source_.size()
            && (IsWordCharacter(source_[position_]) || source_[position_] == '.')) {
            ConsumeNumberTail();
            invalid = true;
        }
        FinishNumber(begin, LexemeKind::Number, invalid);
    }

    void ScanString()
    {
        const std::size_t begin = position_;
        const ProcessedString processed = ProcessString(
            source_.substr(position_), options_.baseByteOffset + position_,
            options_.finalInput, nullptr,
            [this](LexicalIssueCode code, LexemeSpan span) { Issue(code, span); });
        position_ += processed.consumed;
        Add(processed.terminated ? LexemeKind::String
                                 : LexemeKind::IncompleteString,
            begin, position_);
    }

    void ScanToken()
    {
        const std::size_t begin = position_;
        const unsigned char value = static_cast<unsigned char>(source_[position_]);
        if (IsAsciiLetter(static_cast<char>(value))) return ScanWord();
        if (IsDigit(static_cast<char>(value))
            || (value == '.' && position_ + 1U < source_.size()
                && IsDigit(source_[position_ + 1U]))) return ScanNumber();
        if (value == '"') return ScanString();
        if (value >= 0x80U) {
            const std::size_t count = (std::min)(
                Utf8SequenceLength(value), source_.size() - position_);
            position_ += count;
            Issue(LexicalIssueCode::NonAsciiSyntax, begin, count);
            Add(LexemeKind::Invalid, begin, position_);
            return;
        }

        struct Compound final {
            std::string_view text;
            LexemeKind kind;
        };
        constexpr std::array compounds{
            Compound{"=>>", LexemeKind::ConsumeContinue},
            Compound{"~>>", LexemeKind::ObserveContinue},
            Compound{"->", LexemeKind::MappingArrow},
            Compound{"=>", LexemeKind::ConsumeStop},
            Compound{"~>", LexemeKind::ObserveStop},
            Compound{"==", LexemeKind::EqualEqual},
            Compound{"!=", LexemeKind::BangEqual},
            Compound{"<=", LexemeKind::LessEqual},
            Compound{">=", LexemeKind::GreaterEqual}};
        for (const Compound& compound : compounds) {
            if (StartsWith(compound.text)) {
                position_ += compound.text.size();
                Add(compound.kind, begin, position_);
                return;
            }
        }

        LexemeKind kind = LexemeKind::Invalid;
        switch (static_cast<char>(value)) {
        case '=': kind = LexemeKind::Equal; break;
        case '+': kind = LexemeKind::Plus; break;
        case '-': kind = LexemeKind::Minus; break;
        case '*': kind = LexemeKind::Star; break;
        case '/': kind = LexemeKind::Slash; break;
        case '%': kind = LexemeKind::Percent; break;
        case '<': kind = LexemeKind::Less; break;
        case '>': kind = LexemeKind::Greater; break;
        case ':': kind = LexemeKind::Colon; break;
        case ';': kind = LexemeKind::Semicolon; break;
        case '.': kind = LexemeKind::Dot; break;
        case ',': kind = LexemeKind::Comma; break;
        case '(': kind = LexemeKind::LeftParen; break;
        case ')': kind = LexemeKind::RightParen; break;
        case '[': kind = LexemeKind::LeftBracket; break;
        case ']': kind = LexemeKind::RightBracket; break;
        case '|': kind = LexemeKind::Pipe; break;
        default: break;
        }
        ++position_;
        Add(kind, begin, position_);
        if (kind == LexemeKind::Invalid) {
            Issue(value == '\0' ? LexicalIssueCode::EmbeddedNul
                                 : LexicalIssueCode::InvalidCharacter,
                begin, 1U);
        }
    }

    std::string_view source_;
    const ScanOptions& options_;
    LexerState state_{};
    std::size_t position_{};
    std::size_t significantLexemeCount_{};
    bool significantLimitReached_{};
    bool storedLimitReached_{};
    bool issueLimitReached_{};
    std::vector<Lexeme> lexemes_;
    std::vector<LexicalIssue> issues_;
};

} // namespace

ScanResult ScanWeave(std::string_view source, const ScanOptions& options)
{
    return Scanner(source, options).Run();
}

StringDecodeResult DecodeStringLiteral(
    std::string_view source,
    std::size_t baseByteOffset,
    std::size_t maximumIssues)
{
    StringDecodeResult result;
    const auto report = [&](LexicalIssueCode code, LexemeSpan span) {
        if (result.issues.size() < maximumIssues) {
            result.issues.push_back({code, span});
        } else {
            result.issueLimitReached = true;
        }
    };
    const ProcessedString processed = ProcessString(
        source, baseByteOffset, true, &result.value, report);
    result.terminated = processed.terminated;
    return result;
}

} // namespace inputweaver::language
