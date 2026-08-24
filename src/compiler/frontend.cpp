#include "frontend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::compiler {
namespace {

[[nodiscard]] bool IsAsciiLetter(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
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

[[nodiscard]] SourceSpan SpanFrom(std::size_t begin, std::size_t end) noexcept
{
    return {
        static_cast<std::uint32_t>(begin),
        static_cast<std::uint32_t>(end - begin)};
}

[[nodiscard]] SourceSpan MergeSpans(SourceSpan left, SourceSpan right) noexcept
{
    const std::uint64_t leftEnd = static_cast<std::uint64_t>(left.beginByte)
        + left.byteLength;
    const std::uint64_t rightEnd = static_cast<std::uint64_t>(right.beginByte)
        + right.byteLength;
    const std::uint32_t begin = (std::min)(left.beginByte, right.beginByte);
    const std::uint64_t end = (std::max)(leftEnd, rightEnd);
    return {begin, static_cast<std::uint32_t>(end - begin)};
}

[[nodiscard]] std::size_t Utf8SequenceLength(unsigned char first) noexcept
{
    if ((first & 0xe0U) == 0xc0U) {
        return 2U;
    }
    if ((first & 0xf0U) == 0xe0U) {
        return 3U;
    }
    if ((first & 0xf8U) == 0xf0U) {
        return 4U;
    }
    return 1U;
}

class Lexer final {
public:
    Lexer(
        const SourceFile& source,
        const CompilerLimits& limits,
        DiagnosticSink& diagnostics) noexcept
        : source_(source), limits_(limits), diagnostics_(diagnostics)
    {
    }

    [[nodiscard]] std::vector<Token> Run()
    {
        while (position_ < source_.bytes.size() && !diagnostics_.Full()) {
            SkipTrivia();
            if (position_ >= source_.bytes.size() || diagnostics_.Full()) {
                break;
            }
            if (tokens_.size() + 1U >= limits_.maximumTokens) {
                diagnostics_.Add(
                    CompileDiagnosticCode::TokenLimit,
                    SpanFrom(position_, position_),
                    "token count exceeds the configured limit");
                break;
            }
            LexToken();
        }
        tokens_.push_back({
            TokenKind::EndOfFile,
            SpanFrom(source_.bytes.size(), source_.bytes.size()),
            {}});
        return std::move(tokens_);
    }

private:
    void Add(
        TokenKind kind,
        std::size_t begin,
        std::size_t end,
        std::optional<std::string> text = std::nullopt)
    {
        std::string tokenText = text.has_value()
            ? std::move(*text)
            : source_.bytes.substr(begin, end - begin);
        tokens_.push_back({kind, SpanFrom(begin, end), std::move(tokenText)});
    }

    [[nodiscard]] bool StartsWith(std::string_view value) const noexcept
    {
        return source_.bytes.compare(position_, value.size(), value) == 0;
    }

    void SkipTrivia()
    {
        bool progressed = true;
        while (progressed && position_ < source_.bytes.size()) {
            progressed = false;
            while (position_ < source_.bytes.size()) {
                const char value = source_.bytes[position_];
                if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                    break;
                }
                ++position_;
                progressed = true;
            }
            if (StartsWith("//")) {
                progressed = true;
                position_ += 2U;
                while (position_ < source_.bytes.size()
                       && source_.bytes[position_] != '\r'
                       && source_.bytes[position_] != '\n') {
                    ++position_;
                }
            } else if (StartsWith("/*")) {
                progressed = true;
                const std::size_t begin = position_;
                position_ += 2U;
                std::size_t depth = 1U;
                while (position_ < source_.bytes.size() && depth > 0U) {
                    if (source_.bytes.compare(position_, 2U, "/*") == 0) {
                        diagnostics_.Add(
                            CompileDiagnosticCode::NestedBlockComment,
                            SpanFrom(position_, position_ + 2U),
                            "block comments cannot be nested");
                        ++depth;
                        position_ += 2U;
                    } else if (source_.bytes.compare(position_, 2U, "*/") == 0) {
                        --depth;
                        position_ += 2U;
                    } else {
                        ++position_;
                    }
                }
                if (depth != 0U) {
                    diagnostics_.Add(
                        CompileDiagnosticCode::UnterminatedBlockComment,
                        SpanFrom(begin, source_.bytes.size()),
                        "unterminated block comment");
                }
            }
        }
    }

    void LexWord()
    {
        const std::size_t begin = position_++;
        while (position_ < source_.bytes.size()
               && IsWordCharacter(source_.bytes[position_])) {
            ++position_;
        }
        Add(TokenKind::Word, begin, position_);
    }

    void LexNumber()
    {
        const std::size_t begin = position_;
        if (position_ + 1U < source_.bytes.size()
            && source_.bytes[position_] == '0'
            && source_.bytes[position_ + 1U] == 'x') {
            position_ += 2U;
            const std::size_t digits = position_;
            while (position_ < source_.bytes.size()
                   && IsHexDigit(source_.bytes[position_])) {
                ++position_;
            }
            if (position_ == digits) {
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidNumber,
                    SpanFrom(begin, position_),
                    "hexadecimal integer requires at least one digit");
                Add(TokenKind::Invalid, begin, position_);
            } else {
                Add(TokenKind::HexInteger, begin, position_);
            }
            return;
        }

        while (position_ < source_.bytes.size()
               && IsDigit(source_.bytes[position_])) {
            ++position_;
        }
        if (position_ + 1U < source_.bytes.size()
            && source_.bytes[position_] == '.'
            && IsDigit(source_.bytes[position_ + 1U])) {
            ++position_;
            while (position_ < source_.bytes.size()
                   && IsDigit(source_.bytes[position_])) {
                ++position_;
            }
        }

        for (const std::string_view suffix : {"min", "ms", "s"}) {
            if (source_.bytes.compare(position_, suffix.size(), suffix) == 0) {
                const std::size_t after = position_ + suffix.size();
                if (after == source_.bytes.size()
                    || !IsWordCharacter(source_.bytes[after])) {
                    position_ = after;
                    Add(TokenKind::Duration, begin, position_);
                    return;
                }
            }
        }
        Add(TokenKind::Number, begin, position_);
    }

    void LexString()
    {
        const std::size_t begin = position_++;
        std::string decoded;
        bool terminated = false;
        while (position_ < source_.bytes.size()) {
            const unsigned char value = static_cast<unsigned char>(
                source_.bytes[position_]);
            if (value == '"') {
                ++position_;
                terminated = true;
                break;
            }
            if (value == '\r' || value == '\n') {
                break;
            }
            if (value >= 0x80U) {
                const std::size_t count = Utf8SequenceLength(value);
                diagnostics_.Add(
                    CompileDiagnosticCode::NonAsciiSyntax,
                    SpanFrom(position_, position_ + count),
                    "string literals accept ASCII characters only");
                position_ += count;
                continue;
            }
            if (value == '\0') {
                diagnostics_.Add(
                    CompileDiagnosticCode::EmbeddedNul,
                    SpanFrom(position_, position_ + 1U),
                    "string literal contains an embedded NUL");
                ++position_;
                continue;
            }
            if (value != '\\') {
                decoded.push_back(static_cast<char>(value));
                ++position_;
                continue;
            }
            const std::size_t escapeBegin = position_++;
            if (position_ >= source_.bytes.size()) {
                break;
            }
            const char escaped = source_.bytes[position_++];
            switch (escaped) {
            case '\\':
                decoded.push_back('\\');
                break;
            case '"':
                decoded.push_back('"');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            default:
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidEscape,
                    SpanFrom(escapeBegin, position_),
                    "unsupported string escape sequence");
                break;
            }
        }
        if (!terminated) {
            diagnostics_.Add(
                CompileDiagnosticCode::UnterminatedString,
                SpanFrom(begin, position_),
                "unterminated string literal");
        }
        Add(TokenKind::String, begin, position_, std::move(decoded));
    }

    void LexToken()
    {
        const std::size_t begin = position_;
        const unsigned char value = static_cast<unsigned char>(
            source_.bytes[position_]);
        if (IsAsciiLetter(static_cast<char>(value))) {
            LexWord();
            return;
        }
        if (IsDigit(static_cast<char>(value))) {
            LexNumber();
            return;
        }
        if (value == '"') {
            LexString();
            return;
        }
        if (value >= 0x80U) {
            const std::size_t count = Utf8SequenceLength(value);
            diagnostics_.Add(
                CompileDiagnosticCode::NonAsciiSyntax,
                SpanFrom(position_, position_ + count),
                "non-ASCII text is allowed only inside comments");
            position_ += count;
            Add(TokenKind::Invalid, begin, position_);
            return;
        }

        const auto punctuate = [this, begin](TokenKind kind, std::size_t count) {
            position_ += count;
            Add(kind, begin, position_);
        };
        if (StartsWith("=>>")) {
            punctuate(TokenKind::ConsumeContinue, 3U);
        } else if (StartsWith("~>>")) {
            punctuate(TokenKind::ObserveContinue, 3U);
        } else if (StartsWith("->")) {
            punctuate(TokenKind::MappingArrow, 2U);
        } else if (StartsWith("=>")) {
            punctuate(TokenKind::ConsumeStop, 2U);
        } else if (StartsWith("~>")) {
            punctuate(TokenKind::ObserveStop, 2U);
        } else if (StartsWith("==")) {
            punctuate(TokenKind::EqualEqual, 2U);
        } else if (StartsWith("!=")) {
            punctuate(TokenKind::BangEqual, 2U);
        } else if (StartsWith("<=")) {
            punctuate(TokenKind::LessEqual, 2U);
        } else if (StartsWith(">=")) {
            punctuate(TokenKind::GreaterEqual, 2U);
        } else {
            TokenKind kind = TokenKind::Invalid;
            switch (static_cast<char>(value)) {
            case '=': kind = TokenKind::Equal; break;
            case '+': kind = TokenKind::Plus; break;
            case '-': kind = TokenKind::Minus; break;
            case '*': kind = TokenKind::Star; break;
            case '/': kind = TokenKind::Slash; break;
            case '%': kind = TokenKind::Percent; break;
            case '<': kind = TokenKind::Less; break;
            case '>': kind = TokenKind::Greater; break;
            case ':': kind = TokenKind::Colon; break;
            case ';': kind = TokenKind::Semicolon; break;
            case '.': kind = TokenKind::Dot; break;
            case ',': kind = TokenKind::Comma; break;
            case '(': kind = TokenKind::LeftParen; break;
            case ')': kind = TokenKind::RightParen; break;
            case '[': kind = TokenKind::LeftBracket; break;
            case ']': kind = TokenKind::RightBracket; break;
            case '|': kind = TokenKind::Pipe; break;
            default: break;
            }
            punctuate(kind, 1U);
            if (kind == TokenKind::Invalid) {
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidCharacter,
                    SpanFrom(begin, position_),
                    "invalid syntax character");
            }
        }
    }

    const SourceFile& source_;
    const CompilerLimits& limits_;
    DiagnosticSink& diagnostics_;
    std::size_t position_{};
    std::vector<Token> tokens_;
};

class ParseFailure final : public std::runtime_error {
public:
    ParseFailure() : std::runtime_error("parse failure") {}
};

class Parser final {
public:
    Parser(
        const SourceFile& source,
        const std::vector<Token>& tokens,
        const CompilerLimits& limits,
        DiagnosticSink& diagnostics) noexcept
        : source_(source), tokens_(tokens), limits_(limits), diagnostics_(diagnostics)
    {
    }

    [[nodiscard]] SyntaxTree Run()
    {
        SyntaxTree tree;
        while (!Is(TokenKind::EndOfFile) && !diagnostics_.Full()) {
            try {
                tree.items.push_back(ParseTopLevel());
            } catch (const ParseFailure&) {
                SynchronizeTopLevel();
            }
        }
        return tree;
    }

private:
    class NestingScope final {
    public:
        NestingScope(Parser& parser, SourceSpan span) : parser_(parser)
        {
            ++parser_.nestingDepth_;
            if (parser_.nestingDepth_ > parser_.limits_.maximumNestingDepth) {
                --parser_.nestingDepth_;
                parser_.Fail(
                    CompileDiagnosticCode::SyntaxNestingLimit,
                    span,
                    "syntax nesting exceeds the configured limit");
            }
        }

        ~NestingScope()
        {
            --parser_.nestingDepth_;
        }

        NestingScope(const NestingScope&) = delete;
        NestingScope& operator=(const NestingScope&) = delete;

    private:
        Parser& parser_;
    };

    [[nodiscard]] const Token& Current() const noexcept
    {
        return tokens_[position_];
    }

    [[nodiscard]] const Token& Previous() const noexcept
    {
        return tokens_[position_ - 1U];
    }

    [[nodiscard]] bool Is(TokenKind kind) const noexcept
    {
        return Current().kind == kind;
    }

    [[nodiscard]] bool IsWord(std::string_view text) const noexcept
    {
        return Is(TokenKind::Word) && Current().text == text;
    }

    const Token& Advance() noexcept
    {
        if (!Is(TokenKind::EndOfFile)) {
            ++position_;
        }
        return Previous();
    }

    [[nodiscard]] bool Match(TokenKind kind) noexcept
    {
        if (!Is(kind)) {
            return false;
        }
        Advance();
        return true;
    }

    [[nodiscard]] bool MatchWord(std::string_view text) noexcept
    {
        if (!IsWord(text)) {
            return false;
        }
        Advance();
        return true;
    }

    [[noreturn]] void Fail(
        CompileDiagnosticCode code,
        SourceSpan span,
        std::string message)
    {
        diagnostics_.Add(code, span, std::move(message));
        throw ParseFailure{};
    }

    const Token& Expect(TokenKind kind, std::string_view description)
    {
        if (!Is(kind)) {
            Fail(
                CompileDiagnosticCode::ExpectedToken,
                Current().span,
                "expected " + std::string(description));
        }
        return Advance();
    }

    const Token& ExpectWordToken(std::string_view description)
    {
        return Expect(TokenKind::Word, description);
    }

    const Token& ExpectWord(std::string_view word)
    {
        if (!IsWord(word)) {
            Fail(
                CompileDiagnosticCode::ExpectedToken,
                Current().span,
                "expected '" + std::string(word) + "'");
        }
        return Advance();
    }

    void CountNode(SourceSpan span)
    {
        ++nodeCount_;
        if (nodeCount_ > limits_.maximumSyntaxNodes) {
            Fail(
                CompileDiagnosticCode::SyntaxNodeLimit,
                span,
                "syntax node count exceeds the configured limit");
        }
    }

    void SynchronizeTopLevel() noexcept
    {
        while (!Is(TokenKind::EndOfFile)) {
            if (Match(TokenKind::Semicolon)) {
                return;
            }
            Advance();
        }
    }

    [[nodiscard]] TopLevelSyntax ParseTopLevel()
    {
        const SourceSpan begin = Current().span;
        if (MatchWord("TARGET")) {
            return ParseTargetSetting(begin);
        }
        if (MatchWord("TAP_DURATION")) {
            return ParseDurationSetting(
                begin,
                TopLevelSyntax::Kind::TapDurationSetting);
        }
        if (MatchWord("ACTION_GAP")) {
            return ParseDurationSetting(
                begin,
                TopLevelSyntax::Kind::ActionGapSetting);
        }
        if (MatchWord("state")) {
            return ParseDeclaration(begin, TopLevelSyntax::Kind::StateDeclaration);
        }
        if (MatchWord("number")) {
            return ParseDeclaration(begin, TopLevelSyntax::Kind::NumberDeclaration);
        }
        if (MatchWord("duration")) {
            return ParseDeclaration(begin, TopLevelSyntax::Kind::DurationDeclaration);
        }
        if (MatchWord("pause")) {
            return ParsePauseRule(begin);
        }
        if (MatchWord("exit")) {
            return ParseExitRule(begin);
        }
        return ParseMappingOrEventRule(begin);
    }

    [[nodiscard]] TopLevelSyntax ParseTargetSetting(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.kind = TopLevelSyntax::Kind::TargetSetting;
        Expect(TokenKind::Equal, "'='");
        if (MatchWord("GLOBAL")) {
            item.targetGlobal = true;
            item.valueSpan = Previous().span;
        } else {
            const Token& value = Expect(TokenKind::String, "a target string or GLOBAL");
            item.literal = value.text;
            item.valueSpan = value.span;
        }
        const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] TopLevelSyntax ParseDurationSetting(
        SourceSpan begin,
        TopLevelSyntax::Kind kind)
    {
        TopLevelSyntax item{};
        item.kind = kind;
        Expect(TokenKind::Equal, "'='");
        const Token& value = Expect(TokenKind::Duration, "a duration literal");
        item.literal = value.text;
        item.valueSpan = value.span;
        const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] TopLevelSyntax ParseDeclaration(
        SourceSpan begin,
        TopLevelSyntax::Kind kind)
    {
        TopLevelSyntax item{};
        item.kind = kind;
        const Token& name = ExpectWordToken("an identifier");
        item.name = name.text;
        Expect(TokenKind::Equal, "'='");
        if (kind == TopLevelSyntax::Kind::StateDeclaration) {
            if (MatchWord("on")) {
                item.stateValue = true;
            } else if (MatchWord("off")) {
                item.stateValue = false;
            } else {
                Fail(
                    CompileDiagnosticCode::ExpectedToken,
                    Current().span,
                    "expected 'on' or 'off'");
            }
            item.valueSpan = Previous().span;
        } else if (kind == TopLevelSyntax::Kind::NumberDeclaration) {
            std::string sign;
            SourceSpan valueBegin = Current().span;
            if (Match(TokenKind::Minus)) {
                sign = "-";
                valueBegin = Previous().span;
            }
            const Token& value = Expect(TokenKind::Number, "a number literal");
            item.literal = sign + value.text;
            item.valueSpan = MergeSpans(valueBegin, value.span);
        } else {
            const Token& value = Expect(TokenKind::Duration, "a duration literal");
            item.literal = value.text;
            item.valueSpan = value.span;
        }
        const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseOptionalCondition()
    {
        if (!MatchWord("when")) {
            return nullptr;
        }
        return ParseExpression();
    }

    [[nodiscard]] EventSyntax ParseEvent()
    {
        EventSyntax event{};
        event.control = ParseControl();
        Expect(TokenKind::Colon, "':'");
        const Token& transition = ExpectWordToken("down, repeat, or up");
        event.transition = transition.text;
        event.span = MergeSpans(event.control.span, transition.span);
        return event;
    }

    [[nodiscard]] TopLevelSyntax ParsePauseRule(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.kind = TopLevelSyntax::Kind::PauseRule;
        item.event = ParseEvent();
        item.condition = ParseOptionalCondition();
        if (Is(TokenKind::ConsumeContinue) || Is(TokenKind::ObserveContinue)) {
            Fail(
                CompileDiagnosticCode::InvalidPauseRule,
                Current().span,
                "pause rules do not accept continuing arrows");
        }
        if (!Is(TokenKind::ConsumeStop) && !Is(TokenKind::ObserveStop)) {
            Fail(
                CompileDiagnosticCode::InvalidPauseRule,
                Current().span,
                "pause rule requires '=>' or '~>'");
        }
        item.arrow = Advance().kind;
        const Token& effect = ExpectWordToken("on, off, or toggle");
        if (effect.text != "on" && effect.text != "off" && effect.text != "toggle") {
            Fail(
                CompileDiagnosticCode::InvalidPauseRule,
                effect.span,
                "pause effect must be on, off, or toggle");
        }
        item.pauseEffect = effect.text;
        const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] TopLevelSyntax ParseExitRule(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.kind = TopLevelSyntax::Kind::ExitRule;
        item.event = ParseEvent();
        item.condition = ParseOptionalCondition();
        const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] TopLevelSyntax ParseMappingOrEventRule(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.sourceControl = ParseControl();
        if (Match(TokenKind::MappingArrow)) {
            item.kind = TopLevelSyntax::Kind::Mapping;
            item.targetControl = ParseControl();
            item.condition = ParseOptionalCondition();
            const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
            item.span = MergeSpans(begin, semicolon.span);
            CountNode(item.span);
            return item;
        }

        item.kind = TopLevelSyntax::Kind::EventRule;
        Expect(TokenKind::Colon, "':' or '->'");
        const Token& transition = ExpectWordToken("down, repeat, or up");
        item.event.control = std::move(item.sourceControl);
        item.event.transition = transition.text;
        item.event.span = MergeSpans(item.event.control.span, transition.span);
        item.condition = ParseOptionalCondition();
        if (!Is(TokenKind::ConsumeStop)
            && !Is(TokenKind::ConsumeContinue)
            && !Is(TokenKind::ObserveStop)
            && !Is(TokenKind::ObserveContinue)) {
            Fail(
                CompileDiagnosticCode::ExpectedToken,
                Current().span,
                "expected a rule arrow");
        }
        item.arrow = Advance().kind;
        const SourceSpan flowBegin = Current().span;
        item.actions = ParseActionFlow({";"});
        const Token& semicolon = Expect(TokenKind::Semicolon, "';'");
        item.actionFlowSpan = item.actions.empty()
            ? SourceSpan{flowBegin.beginByte, 0U}
            : MergeSpans(item.actions.front().span, item.actions.back().span);
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] bool IsRawControlName(std::string_view name) const noexcept
    {
        return name == "HID.Usage"
            || name == "Windows.VirtualKey"
            || name == "Windows.ScanCode"
            || name == "Linux.Key"
            || name == "MacOS.KeyCode";
    }

    [[nodiscard]] ControlSyntax ParseControl()
    {
        ControlSyntax control{};
        const Token& first = ExpectWordToken("a control reference");
        control.name = first.text;
        SourceSpan end = first.span;
        while (Match(TokenKind::Dot)) {
            const Token& segment = ExpectWordToken("a control-name segment");
            control.name += "." + segment.text;
            end = segment.span;
        }
        if (IsRawControlName(control.name) && Match(TokenKind::LeftParen)) {
            control.raw = true;
            if (!Is(TokenKind::RightParen)) {
                for (;;) {
                    const SourceSpan argumentBegin = Current().span;
                    std::string sign;
                    if (Match(TokenKind::Minus)) {
                        sign = "-";
                    }
                    if (!Is(TokenKind::Number)
                        && !Is(TokenKind::HexInteger)
                        && !Is(TokenKind::Word)) {
                        Fail(
                            CompileDiagnosticCode::ExpectedToken,
                            Current().span,
                            "expected a raw control argument");
                    }
                    const Token& argument = Advance();
                    control.arguments.push_back({
                        sign + argument.text,
                        MergeSpans(argumentBegin, argument.span)});
                    if (!Match(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            const Token& close = Expect(TokenKind::RightParen, "')'");
            end = close.span;
        }
        control.span = MergeSpans(first.span, end);
        return control;
    }

    [[nodiscard]] bool AtActionTerminator(
        const std::vector<std::string_view>& terminators) const noexcept
    {
        for (const std::string_view terminator : terminators) {
            if ((terminator == ";" && Is(TokenKind::Semicolon))
                || (terminator != ";" && IsWord(terminator))) {
                return true;
            }
        }
        return Is(TokenKind::EndOfFile);
    }

    [[nodiscard]] bool IsActionStart() const noexcept
    {
        if (Is(TokenKind::Pipe)) {
            return true;
        }
        if (!Is(TokenKind::Word)) {
            return false;
        }
        constexpr std::array<std::string_view, 11U> names{{
            "press", "release", "tap", "wait", "gap", "set", "toggle",
            "exec", "if", "repeat", "while",
        }};
        return std::find(names.begin(), names.end(), Current().text) != names.end();
    }

    [[nodiscard]] bool SynchronizeActionFlow(
        const std::vector<std::string_view>& terminators) noexcept
    {
        std::uint32_t delimiterDepth = 0U;
        while (!Is(TokenKind::EndOfFile)) {
            if (delimiterDepth == 0U) {
                if (AtActionTerminator(terminators) || IsActionStart()) {
                    return true;
                }
                if (Is(TokenKind::Semicolon)
                    || IsWord("else")
                    || IsWord("end")) {
                    return false;
                }
            }
            if (Is(TokenKind::LeftParen) || Is(TokenKind::LeftBracket)) {
                ++delimiterDepth;
            } else if ((Is(TokenKind::RightParen) || Is(TokenKind::RightBracket))
                && delimiterDepth > 0U) {
                --delimiterDepth;
            }
            Advance();
        }
        return false;
    }

    [[nodiscard]] std::vector<ActionSyntax> ParseActionFlow(
        const std::vector<std::string_view>& terminators)
    {
        std::vector<ActionSyntax> actions;
        while (!AtActionTerminator(terminators)) {
            try {
                actions.push_back(ParseAction());
            } catch (const ParseFailure&) {
                if (!SynchronizeActionFlow(terminators)) {
                    throw;
                }
            }
        }
        return actions;
    }

    [[nodiscard]] ActionSyntax ParseAction()
    {
        if (Match(TokenKind::Pipe)) {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Gap;
            action.span = Previous().span;
            CountNode(action.span);
            return action;
        }
        if (!Is(TokenKind::Word)) {
            Fail(
                CompileDiagnosticCode::UnexpectedToken,
                Current().span,
                "expected an action item");
        }
        const Token& name = Advance();
        if (name.text == "press" || name.text == "release" || name.text == "tap") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Input;
            action.name = name.text;
            Expect(TokenKind::LeftParen, "'('");
            action.control = ParseControl();
            const Token& close = Expect(TokenKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "wait") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Wait;
            Expect(TokenKind::LeftParen, "'('");
            action.expression = ParseExpression();
            const Token& close = Expect(TokenKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "gap") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Gap;
            Expect(TokenKind::LeftParen, "'('");
            const Token& close = Expect(TokenKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "set") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Set;
            Expect(TokenKind::LeftParen, "'('");
            action.name = ExpectWordToken("a value name").text;
            Expect(TokenKind::Comma, "','");
            action.expression = ParseExpression();
            const Token& close = Expect(TokenKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "toggle") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Toggle;
            Expect(TokenKind::LeftParen, "'('");
            action.name = ExpectWordToken("a value name").text;
            const Token& close = Expect(TokenKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "exec") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Exec;
            Expect(TokenKind::LeftParen, "'('");
            action.stringValue = Expect(TokenKind::String, "a command string").text;
            const Token& close = Expect(TokenKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "if") {
            NestingScope nesting(*this, name.span);
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::If;
            action.expression = ParseExpression();
            ExpectWord("then");
            action.body = ParseActionFlow({"else", "end"});
            if (MatchWord("else")) {
                action.alternative = ParseActionFlow({"end"});
            }
            const Token& end = ExpectWord("end");
            action.span = MergeSpans(name.span, end.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "repeat" || name.text == "while") {
            NestingScope nesting(*this, name.span);
            ActionSyntax action{};
            action.kind = name.text == "repeat"
                ? ActionSyntax::Kind::Repeat
                : ActionSyntax::Kind::While;
            action.expression = ParseExpression();
            ExpectWord("do");
            action.body = ParseActionFlow({"end"});
            const Token& end = ExpectWord("end");
            action.span = MergeSpans(name.span, end.span);
            CountNode(action.span);
            return action;
        }
        Fail(
            CompileDiagnosticCode::UnexpectedToken,
            name.span,
            "unknown action '" + name.text + "'");
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> MakeExpression(
        ExpressionSyntax::Kind kind,
        SourceSpan span)
    {
        CountNode(span);
        auto expression = std::make_unique<ExpressionSyntax>();
        expression->kind = kind;
        expression->span = span;
        return expression;
    }

    void CheckExpressionDepth(ExpressionSyntax& expression)
    {
        const std::uint32_t leftDepth = expression.left == nullptr
            ? 0U
            : expression.left->depth;
        const std::uint32_t rightDepth = expression.right == nullptr
            ? 0U
            : expression.right->depth;
        expression.depth = (std::max)(leftDepth, rightDepth) + 1U;
        if (expression.depth > limits_.maximumNestingDepth) {
            Fail(
                CompileDiagnosticCode::SyntaxNestingLimit,
                expression.span,
                "expression tree depth exceeds the configured limit");
        }
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseExpression()
    {
        return ParseOr();
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseOr()
    {
        auto left = ParseAnd();
        while (MatchWord("or")) {
            const Token operation = Previous();
            auto right = ParseAnd();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Binary,
                MergeSpans(left->span, right->span));
            expression->text = operation.text;
            expression->left = std::move(left);
            expression->right = std::move(right);
            CheckExpressionDepth(*expression);
            left = std::move(expression);
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseAnd()
    {
        auto left = ParseEquality();
        while (MatchWord("and")) {
            const Token operation = Previous();
            auto right = ParseEquality();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Binary,
                MergeSpans(left->span, right->span));
            expression->text = operation.text;
            expression->left = std::move(left);
            expression->right = std::move(right);
            CheckExpressionDepth(*expression);
            left = std::move(expression);
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseEquality()
    {
        auto left = ParseRelational();
        while (Is(TokenKind::EqualEqual) || Is(TokenKind::BangEqual)) {
            const Token operation = Advance();
            auto right = ParseRelational();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Binary,
                MergeSpans(left->span, right->span));
            expression->text = operation.text;
            expression->left = std::move(left);
            expression->right = std::move(right);
            CheckExpressionDepth(*expression);
            left = std::move(expression);
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseRelational()
    {
        auto left = ParseAdditive();
        while (Is(TokenKind::Less)
               || Is(TokenKind::LessEqual)
               || Is(TokenKind::Greater)
               || Is(TokenKind::GreaterEqual)) {
            const Token operation = Advance();
            auto right = ParseAdditive();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Binary,
                MergeSpans(left->span, right->span));
            expression->text = operation.text;
            expression->left = std::move(left);
            expression->right = std::move(right);
            CheckExpressionDepth(*expression);
            left = std::move(expression);
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseAdditive()
    {
        auto left = ParseMultiplicative();
        while (Is(TokenKind::Plus) || Is(TokenKind::Minus)) {
            const Token operation = Advance();
            auto right = ParseMultiplicative();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Binary,
                MergeSpans(left->span, right->span));
            expression->text = operation.text;
            expression->left = std::move(left);
            expression->right = std::move(right);
            CheckExpressionDepth(*expression);
            left = std::move(expression);
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseMultiplicative()
    {
        auto left = ParseUnary();
        while (Is(TokenKind::Star)
               || Is(TokenKind::Slash)
               || Is(TokenKind::Percent)) {
            const Token operation = Advance();
            auto right = ParseUnary();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Binary,
                MergeSpans(left->span, right->span));
            expression->text = operation.text;
            expression->left = std::move(left);
            expression->right = std::move(right);
            CheckExpressionDepth(*expression);
            left = std::move(expression);
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParseUnary()
    {
        if (Is(TokenKind::Plus)
            || Is(TokenKind::Minus)
            || IsWord("not")) {
            const Token operation = Advance();
            NestingScope nesting(*this, operation.span);
            auto operand = ParseUnary();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::Unary,
                MergeSpans(operation.span, operand->span));
            expression->text = operation.text;
            expression->left = std::move(operand);
            CheckExpressionDepth(*expression);
            return expression;
        }
        return ParsePrimary();
    }

    [[nodiscard]] std::unique_ptr<ExpressionSyntax> ParsePrimary()
    {
        if (MatchWord("on") || MatchWord("off")) {
            const Token value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::StateLiteral,
                value.span);
            expression->stateValue = value.text == "on";
            return expression;
        }
        if (Match(TokenKind::Number)) {
            const Token value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::NumberLiteral,
                value.span);
            expression->text = value.text;
            return expression;
        }
        if (Match(TokenKind::Duration)) {
            const Token value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::DurationLiteral,
                value.span);
            expression->text = value.text;
            return expression;
        }
        if (Match(TokenKind::LeftParen)) {
            const Token open = Previous();
            NestingScope nesting(*this, open.span);
            auto expression = ParseExpression();
            const Token& close = Expect(TokenKind::RightParen, "')'");
            expression->span = MergeSpans(open.span, close.span);
            return expression;
        }
        if (Is(TokenKind::Word)) {
            ControlSyntax subject = ParseControl();
            if (Match(TokenKind::LeftBracket)) {
                const Token& test = ExpectWordToken("a state predicate");
                const Token& close = Expect(TokenKind::RightBracket, "']'");
                auto expression = MakeExpression(
                    ExpressionSyntax::Kind::StateQuery,
                    MergeSpans(subject.span, close.span));
                expression->text = test.text;
                expression->querySubject = std::move(subject);
                return expression;
            }
            if (!subject.raw && subject.name.find('.') == std::string::npos) {
                auto expression = MakeExpression(
                    ExpressionSyntax::Kind::ValueReference,
                    subject.span);
                expression->text = std::move(subject.name);
                return expression;
            }
            Fail(
                CompileDiagnosticCode::UnexpectedToken,
                subject.span,
                "control references are values only inside a state query");
        }
        Fail(
            CompileDiagnosticCode::ExpectedToken,
            Current().span,
            "expected an expression");
    }

    const SourceFile& source_;
    const std::vector<Token>& tokens_;
    const CompilerLimits& limits_;
    DiagnosticSink& diagnostics_;
    std::size_t position_{};
    std::uint64_t nodeCount_{};
    std::uint32_t nestingDepth_{};
};

} // namespace

std::vector<Token> LexSource(
    const SourceFile& source,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics)
{
    return Lexer(source, limits, diagnostics).Run();
}

std::optional<SyntaxTree> ParseTokens(
    const SourceFile& source,
    const std::vector<Token>& tokens,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics)
{
    if (tokens.empty()) {
        diagnostics.Add(
            CompileDiagnosticCode::InternalCompiler,
            {},
            "lexer returned no end-of-file token");
        return std::nullopt;
    }
    return Parser(source, tokens, limits, diagnostics).Run();
}

} // namespace inputweaver::compiler
