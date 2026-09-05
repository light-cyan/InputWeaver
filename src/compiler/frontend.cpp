#include "frontend.hpp"

#include "language/lexer.hpp"
#include "language/word_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::compiler {
namespace {

using language::LexemeKind;

struct ParserLexeme final {
    LexemeKind kind{LexemeKind::Invalid};
    SourceSpan span{};
    std::string_view text;
};

[[nodiscard]] SourceSpan CompilerSpan(language::LexemeSpan span) noexcept
{
    return {
        static_cast<std::uint32_t>(span.beginByte),
        static_cast<std::uint32_t>(span.byteLength)};
}

[[nodiscard]] ParserLexeme ParserView(
    const language::Lexeme& lexeme) noexcept
{
    return {lexeme.kind, CompilerSpan(lexeme.span), lexeme.text};
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

[[nodiscard]] CompileDiagnosticCode CompilerIssueCode(
    language::LexicalIssueCode code) noexcept
{
    switch (code) {
    case language::LexicalIssueCode::InvalidCharacter: return CompileDiagnosticCode::InvalidCharacter;
    case language::LexicalIssueCode::NonAsciiSyntax: return CompileDiagnosticCode::NonAsciiSyntax;
    case language::LexicalIssueCode::EmbeddedNul: return CompileDiagnosticCode::EmbeddedNul;
    case language::LexicalIssueCode::InvalidNumber: return CompileDiagnosticCode::InvalidNumber;
    case language::LexicalIssueCode::UnterminatedString: return CompileDiagnosticCode::UnterminatedString;
    case language::LexicalIssueCode::InvalidEscape: return CompileDiagnosticCode::InvalidEscape;
    case language::LexicalIssueCode::UnterminatedBlockComment: return CompileDiagnosticCode::UnterminatedBlockComment;
    case language::LexicalIssueCode::NestedBlockComment: return CompileDiagnosticCode::NestedBlockComment;
    }
    return CompileDiagnosticCode::InternalCompiler;
}

[[nodiscard]] std::string CompilerIssueMessage(
    language::LexicalIssueCode code)
{
    switch (code) {
    case language::LexicalIssueCode::InvalidCharacter: return "invalid syntax character";
    case language::LexicalIssueCode::NonAsciiSyntax: return "non-ASCII text is allowed only inside comments";
    case language::LexicalIssueCode::EmbeddedNul: return "source contains an embedded NUL";
    case language::LexicalIssueCode::InvalidNumber: return "invalid number literal";
    case language::LexicalIssueCode::UnterminatedString: return "unterminated string literal";
    case language::LexicalIssueCode::InvalidEscape: return "unsupported string escape sequence";
    case language::LexicalIssueCode::UnterminatedBlockComment: return "unterminated block comment";
    case language::LexicalIssueCode::NestedBlockComment: return "block comments cannot be nested";
    }
    return "unknown lexical issue";
}

class ParseFailure final : public std::runtime_error {
public:
    ParseFailure() : std::runtime_error("parse failure") {}
};

class Parser final {
public:
    Parser(
        const std::vector<language::Lexeme>& lexemes,
        const CompilerLimits& limits,
        DiagnosticSink& diagnostics) noexcept
        : lexemes_(lexemes), limits_(limits), diagnostics_(diagnostics)
    {}

    [[nodiscard]] SyntaxTree Run()
    {
        SyntaxTree tree;
        while (!Is(LexemeKind::EndOfInput) && !diagnostics_.Full()) {
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

    [[nodiscard]] ParserLexeme Current() const noexcept
    {
        return ParserView(lexemes_[position_]);
    }

    [[nodiscard]] ParserLexeme Previous() const noexcept
    {
        return ParserView(lexemes_[previousPosition_]);
    }

    [[nodiscard]] bool Is(LexemeKind kind) const noexcept
    {
        const LexemeKind current = lexemes_[position_].kind;
        return current == kind
            || (kind == LexemeKind::String
                && current == LexemeKind::IncompleteString);
    }

    [[nodiscard]] bool IsWord(std::string_view text) const noexcept
    {
        return Is(LexemeKind::Word) && Current().text == text;
    }

    ParserLexeme Advance() noexcept
    {
        const ParserLexeme current = Current();
        previousPosition_ = position_;
        if (!Is(LexemeKind::EndOfInput)) {
            ++position_;
        }
        return current;
    }

    [[nodiscard]] bool Match(LexemeKind kind) noexcept
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

    ParserLexeme Expect(LexemeKind kind, std::string_view description)
    {
        if (!Is(kind)) {
            Fail(
                CompileDiagnosticCode::ExpectedToken,
                Current().span,
                "expected " + std::string(description));
        }
        return Advance();
    }

    ParserLexeme ExpectWordToken(std::string_view description)
    {
        return Expect(LexemeKind::Word, description);
    }

    ParserLexeme ExpectWord(std::string_view word)
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
        while (!Is(LexemeKind::EndOfInput)) {
            if (Match(LexemeKind::Semicolon)) {
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
        if (MatchWord("RAND_SEED")) {
            return ParseRandomSeedSetting(begin);
        }
        if (MatchWord("MOUSE_IDLE_TIMEOUT")) {
            return ParseDurationSetting(begin, TopLevelSyntax::Kind::MouseIdleTimeoutSetting);
        }
        if (MatchWord("meter")) {
            return ParseMeterDeclaration(begin);
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
        Expect(LexemeKind::Equal, "'='");
        if (MatchWord("GLOBAL")) {
            item.targetGlobal = true;
            item.valueSpan = Previous().span;
        } else {
            const ParserLexeme& value = Expect(LexemeKind::String, "a target string or GLOBAL");
            item.literal = language::DecodeStringLiteral(
                value.text,
                value.span.beginByte,
                0U).value;
            item.valueSpan = value.span;
        }
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
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
        Expect(LexemeKind::Equal, "'='");
        const ParserLexeme& value = Expect(LexemeKind::Duration, "a duration literal");
        item.literal = value.text;
        item.valueSpan = value.span;
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] TopLevelSyntax ParseRandomSeedSetting(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.kind = TopLevelSyntax::Kind::RandomSeedSetting;
        Expect(LexemeKind::Equal, "'='");
        const ParserLexeme& value = Expect(
            LexemeKind::Number,
            "a decimal unsigned integer");
        item.literal = value.text;
        item.valueSpan = value.span;
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
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
        if ((kind == TopLevelSyntax::Kind::StateDeclaration
             || kind == TopLevelSyntax::Kind::NumberDeclaration)
            && Match(LexemeKind::LeftBracket)) {
            Expect(LexemeKind::RightBracket, "']'");
            item.kind = kind == TopLevelSyntax::Kind::StateDeclaration
                ? TopLevelSyntax::Kind::StateArrayDeclaration
                : TopLevelSyntax::Kind::NumberArrayDeclaration;
        }
        const ParserLexeme& name = ExpectWordToken("an identifier");
        item.name = name.text;
        Expect(LexemeKind::Equal, "'='");
        if (item.kind == TopLevelSyntax::Kind::StateArrayDeclaration
            || item.kind == TopLevelSyntax::Kind::NumberArrayDeclaration) {
            const ParserLexeme& open = Expect(LexemeKind::LeftBracket, "'['");
            if (!Is(LexemeKind::RightBracket)) {
                for (;;) {
                    const SourceSpan elementBegin = Current().span;
                    if (item.kind == TopLevelSyntax::Kind::StateArrayDeclaration) {
                        if (!MatchWord("on") && !MatchWord("off")) {
                            Fail(
                                CompileDiagnosticCode::ExpectedToken,
                                Current().span,
                                "expected 'on' or 'off'");
                        }
                        item.arrayLiterals.push_back({
                            std::string{Previous().text}, Previous().span});
                    } else {
                        std::string valueText;
                        if (Match(LexemeKind::Minus)) {
                            valueText = "-";
                        }
                        const ParserLexeme& value = Expect(
                            LexemeKind::Number,
                            "a number literal");
                        valueText.append(value.text);
                        item.arrayLiterals.push_back({
                            std::move(valueText),
                            MergeSpans(elementBegin, value.span)});
                    }
                    if (!Match(LexemeKind::Comma)) {
                        break;
                    }
                }
            }
            const ParserLexeme& close = Expect(LexemeKind::RightBracket, "']'");
            item.valueSpan = MergeSpans(open.span, close.span);
        } else if (kind == TopLevelSyntax::Kind::StateDeclaration) {
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
            if (Match(LexemeKind::Minus)) {
                sign = "-";
                valueBegin = Previous().span;
            }
            const ParserLexeme& value = Expect(LexemeKind::Number, "a number literal");
            item.literal = sign;
            item.literal.append(value.text);
            item.valueSpan = MergeSpans(valueBegin, value.span);
        } else {
            const ParserLexeme& value = Expect(LexemeKind::Duration, "a duration literal");
            item.literal = value.text;
            item.valueSpan = value.span;
        }
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
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
        Expect(LexemeKind::Colon, "':'");
        const ParserLexeme& transition = ExpectWordToken("down, again, or up");
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
        if (Is(LexemeKind::ConsumeContinue) || Is(LexemeKind::ObserveContinue)) {
            Fail(
                CompileDiagnosticCode::InvalidPauseRule,
                Current().span,
                "pause rules do not accept continuing arrows");
        }
        if (!Is(LexemeKind::ConsumeStop) && !Is(LexemeKind::ObserveStop)) {
            Fail(
                CompileDiagnosticCode::InvalidPauseRule,
                Current().span,
                "pause rule requires '=>' or '~>'");
        }
        const LexemeKind arrow = Advance().kind;
        switch (arrow) {
        case LexemeKind::ConsumeStop:
            item.arrow = RuleArrowSyntax::ConsumeStop;
            break;
        case LexemeKind::ConsumeContinue:
            item.arrow = RuleArrowSyntax::ConsumeContinue;
            break;
        case LexemeKind::ObserveStop:
            item.arrow = RuleArrowSyntax::ObserveStop;
            break;
        case LexemeKind::ObserveContinue:
            item.arrow = RuleArrowSyntax::ObserveContinue;
            break;
        default:
            Fail(
                CompileDiagnosticCode::InternalCompiler,
                Previous().span,
                "validated rule arrow has no syntax value");
        }
        const ParserLexeme& effect = ExpectWordToken("on, off, or toggle");
        if (effect.text != "on" && effect.text != "off" && effect.text != "toggle") {
            Fail(
                CompileDiagnosticCode::InvalidPauseRule,
                effect.span,
                "pause effect must be on, off, or toggle");
        }
        item.pauseEffect = effect.text;
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
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
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] TopLevelSyntax ParseMappingOrEventRule(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.sourceControl = ParseControl();
        if (Match(LexemeKind::MappingArrow)) {
            item.kind = TopLevelSyntax::Kind::Mapping;
            item.targetControl = ParseControl();
            item.condition = ParseOptionalCondition();
            const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
            item.span = MergeSpans(begin, semicolon.span);
            CountNode(item.span);
            return item;
        }

        item.kind = TopLevelSyntax::Kind::EventRule;
        Expect(LexemeKind::Colon, "':' or '->'");
        const ParserLexeme& transition = ExpectWordToken("down, again, or up");
        item.event.control = std::move(item.sourceControl);
        item.event.transition = transition.text;
        item.event.span = MergeSpans(item.event.control.span, transition.span);
        item.condition = ParseOptionalCondition();
        if (!Is(LexemeKind::ConsumeStop)
            && !Is(LexemeKind::ConsumeContinue)
            && !Is(LexemeKind::ObserveStop)
            && !Is(LexemeKind::ObserveContinue)) {
            Fail(
                CompileDiagnosticCode::ExpectedToken,
                Current().span,
                "expected a rule arrow");
        }
        const LexemeKind arrow = Advance().kind;
        switch (arrow) {
        case LexemeKind::ConsumeStop:
            item.arrow = RuleArrowSyntax::ConsumeStop;
            break;
        case LexemeKind::ConsumeContinue:
            item.arrow = RuleArrowSyntax::ConsumeContinue;
            break;
        case LexemeKind::ObserveStop:
            item.arrow = RuleArrowSyntax::ObserveStop;
            break;
        case LexemeKind::ObserveContinue:
            item.arrow = RuleArrowSyntax::ObserveContinue;
            break;
        default:
            Fail(
                CompileDiagnosticCode::InternalCompiler,
                Previous().span,
                "validated rule arrow has no syntax value");
        }
        const SourceSpan flowBegin = Current().span;
        item.actions = ParseActionFlow({";"});
        const ParserLexeme& semicolon = Expect(LexemeKind::Semicolon, "';'");
        item.actionFlowSpan = item.actions.empty()
            ? SourceSpan{flowBegin.beginByte, 0U}
            : MergeSpans(item.actions.front().span, item.actions.back().span);
        item.span = MergeSpans(begin, semicolon.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] ControlSyntax ParseControl()
    {
        ControlSyntax control{};
        const ParserLexeme& first = ExpectWordToken("a control reference");
        control.name = first.text;
        SourceSpan end = first.span;
        while (Match(LexemeKind::Dot)) {
            const ParserLexeme& segment = ExpectWordToken("a control-name segment");
            control.name += '.';
            control.name.append(segment.text);
            end = segment.span;
        }
        if (language::LookupWordRole(control.name)
                == language::WordRole::RawControl
            && Match(LexemeKind::LeftParen)) {
            control.raw = true;
            if (!Is(LexemeKind::RightParen)) {
                for (;;) {
                    const SourceSpan argumentBegin = Current().span;
                    std::string sign;
                    if (Match(LexemeKind::Minus)) {
                        sign = "-";
                    }
                    if (!Is(LexemeKind::Number)
                        && !Is(LexemeKind::HexInteger)
                        && !Is(LexemeKind::Word)) {
                        Fail(
                            CompileDiagnosticCode::ExpectedToken,
                            Current().span,
                            "expected a raw control argument");
                    }
                    const ParserLexeme& argument = Advance();
                    sign.append(argument.text);
                    control.arguments.push_back({
                        std::move(sign),
                        MergeSpans(argumentBegin, argument.span)});
                    if (!Match(LexemeKind::Comma)) {
                        break;
                    }
                }
            }
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            end = close.span;
        }
        control.span = MergeSpans(first.span, end);
        return control;
    }

    [[nodiscard]] bool AtActionTerminator(
        const std::vector<std::string_view>& terminators) const noexcept
    {
        for (const std::string_view terminator : terminators) {
            if ((terminator == ";" && Is(LexemeKind::Semicolon))
                || (terminator != ";" && IsWord(terminator))) {
                return true;
            }
        }
        return Is(LexemeKind::EndOfInput);
    }

    [[nodiscard]] bool IsActionStart() const noexcept
    {
        if (Is(LexemeKind::Pipe)) {
            return true;
        }
        if (!Is(LexemeKind::Word)) {
            return false;
        }
        constexpr std::array<std::string_view, 19U> names{{
            "press", "release", "tap", "wait", "gap", "set", "toggle",
            "append", "pop", "clear", "exec", "if", "repeat", "while",
            "move_by", "move_to", "scroll", "scroll_horizontal", "restart",
        }};
        return std::find(names.begin(), names.end(), Current().text) != names.end();
    }

    [[nodiscard]] bool SynchronizeActionFlow(
        const std::vector<std::string_view>& terminators) noexcept
    {
        std::uint32_t delimiterDepth = 0U;
        while (!Is(LexemeKind::EndOfInput)) {
            if (delimiterDepth == 0U) {
                if (AtActionTerminator(terminators) || IsActionStart()) {
                    return true;
                }
                if (Is(LexemeKind::Semicolon)
                    || IsWord("else")
                    || IsWord("end")) {
                    return false;
                }
            }
            if (Is(LexemeKind::LeftParen) || Is(LexemeKind::LeftBracket)) {
                ++delimiterDepth;
            } else if ((Is(LexemeKind::RightParen) || Is(LexemeKind::RightBracket))
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

    [[nodiscard]] TargetSyntax ParseTarget()
    {
        TargetSyntax target{};
        const ParserLexeme& name = ExpectWordToken("a writable target");
        target.name = name.text;
        target.span = name.span;
        if (Match(LexemeKind::LeftBracket)) {
            target.index = ParseExpression();
            const ParserLexeme& close = Expect(LexemeKind::RightBracket, "']'");
            target.span = MergeSpans(name.span, close.span);
        }
        return target;
    }

    [[nodiscard]] ActionSyntax ParseAction()
    {
        if (Match(LexemeKind::Pipe)) {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Gap;
            action.span = Previous().span;
            CountNode(action.span);
            return action;
        }
        if (!Is(LexemeKind::Word)) {
            Fail(
                CompileDiagnosticCode::UnexpectedToken,
                Current().span,
                "expected an action item");
        }
        const ParserLexeme& name = Advance();
        if (name.text == "move_by" || name.text == "move_to" || name.text == "scroll"
            || name.text == "scroll_horizontal" || name.text == "restart") {
            return ParseMouseAction(name);
        }
        if (name.text == "press" || name.text == "release" || name.text == "tap") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Input;
            action.name = name.text;
            Expect(LexemeKind::LeftParen, "'('");
            action.control = ParseControl();
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "wait") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Wait;
            Expect(LexemeKind::LeftParen, "'('");
            action.expression = ParseExpression();
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "gap") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Gap;
            Expect(LexemeKind::LeftParen, "'('");
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "set") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Set;
            Expect(LexemeKind::LeftParen, "'('");
            action.target = ParseTarget();
            Expect(LexemeKind::Comma, "','");
            action.expression = ParseExpression();
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "toggle") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Toggle;
            Expect(LexemeKind::LeftParen, "'('");
            action.target = ParseTarget();
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "append") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Append;
            Expect(LexemeKind::LeftParen, "'('");
            action.name = ExpectWordToken("an array name").text;
            Expect(LexemeKind::Comma, "','");
            action.expression = ParseExpression();
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "pop") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Pop;
            Expect(LexemeKind::LeftParen, "'('");
            action.name = ExpectWordToken("an array name").text;
            Expect(LexemeKind::Comma, "','");
            action.secondaryName = ExpectWordToken("a writable scalar name").text;
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "clear") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Clear;
            Expect(LexemeKind::LeftParen, "'('");
            action.name = ExpectWordToken("an array name").text;
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            action.span = MergeSpans(name.span, close.span);
            CountNode(action.span);
            return action;
        }
        if (name.text == "exec") {
            ActionSyntax action{};
            action.kind = ActionSyntax::Kind::Exec;
            Expect(LexemeKind::LeftParen, "'('");
            const ParserLexeme command = Expect(
                LexemeKind::String,
                "a command string");
            action.stringValue = language::DecodeStringLiteral(
                command.text,
                command.span.beginByte,
                0U).value;
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
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
            const ParserLexeme& end = ExpectWord("end");
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
            const ParserLexeme& end = ExpectWord("end");
            action.span = MergeSpans(name.span, end.span);
            CountNode(action.span);
            return action;
        }
        Fail(
            CompileDiagnosticCode::UnexpectedToken,
            name.span,
            "unknown action '" + std::string{name.text} + "'");
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
            const ParserLexeme operation = Previous();
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
            const ParserLexeme operation = Previous();
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
        while (Is(LexemeKind::EqualEqual) || Is(LexemeKind::BangEqual)) {
            const ParserLexeme operation = Advance();
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
        while (Is(LexemeKind::Less)
               || Is(LexemeKind::LessEqual)
               || Is(LexemeKind::Greater)
               || Is(LexemeKind::GreaterEqual)) {
            const ParserLexeme operation = Advance();
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
        while (Is(LexemeKind::Plus) || Is(LexemeKind::Minus)) {
            const ParserLexeme operation = Advance();
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
        while (Is(LexemeKind::Star)
               || Is(LexemeKind::Slash)
               || Is(LexemeKind::Percent)) {
            const ParserLexeme operation = Advance();
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
        if (Is(LexemeKind::Plus)
            || Is(LexemeKind::Minus)
            || IsWord("not")) {
            const ParserLexeme operation = Advance();
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
        if (Match(LexemeKind::At)) {
            const auto begin = Previous().span;
            const auto source = ExpectWordToken("a meter name");
            Expect(LexemeKind::Dot, "'.'");
            const auto field = ExpectWordToken("a completed meter field");
            auto expression = MakeExpression(ExpressionSyntax::Kind::Reference, MergeSpans(begin, field.span));
            expression->reference.name = std::string(source.text) + '.' + std::string(field.text);
            expression->reference.span = expression->span;
            expression->text = expression->reference.name;
            expression->completed = true;
            return expression;
        }
        if (MatchWord("on") || MatchWord("off")) {
            const ParserLexeme value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::StateLiteral,
                value.span);
            expression->stateValue = value.text == "on";
            return expression;
        }
        if (MatchWord("held") || MatchWord("idle")) {
            const ParserLexeme value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::ControlStateLiteral,
                value.span);
            expression->controlStateValue = value.text == "held"
                ? ControlState::Held
                : ControlState::Idle;
            return expression;
        }
        if (Match(LexemeKind::Number)) {
            const ParserLexeme value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::NumberLiteral,
                value.span);
            expression->text = value.text;
            return expression;
        }
        if (Match(LexemeKind::Duration)) {
            const ParserLexeme value = Previous();
            auto expression = MakeExpression(
                ExpressionSyntax::Kind::DurationLiteral,
                value.span);
            expression->text = value.text;
            return expression;
        }
        if (Match(LexemeKind::LeftParen)) {
            const ParserLexeme open = Previous();
            NestingScope nesting(*this, open.span);
            auto expression = ParseExpression();
            const ParserLexeme& close = Expect(LexemeKind::RightParen, "')'");
            expression->span = MergeSpans(open.span, close.span);
            return expression;
        }
        if (Is(LexemeKind::Word)) {
            ControlSyntax subject = ParseControl();
            if (Match(LexemeKind::LeftBracket)) {
                if (subject.raw
                    || subject.name.find('.') != std::string::npos) {
                    Fail(
                        CompileDiagnosticCode::UnexpectedToken,
                        subject.span,
                        "array access requires an unqualified array name");
                }
                auto index = ParseExpression();
                const ParserLexeme& close = Expect(LexemeKind::RightBracket, "']'");
                auto expression = MakeExpression(
                    ExpressionSyntax::Kind::ArrayElement,
                    MergeSpans(subject.span, close.span));
                expression->text = std::move(subject.name);
                expression->left = std::move(index);
                CheckExpressionDepth(*expression);
                return expression;
            }
            constexpr std::string_view lengthSuffix = ".length";
            if (!subject.raw && subject.name.ends_with(lengthSuffix)) {
                const std::string_view fullName = subject.name;
                const std::string_view arrayName = fullName.substr(
                    0U,
                    fullName.size() - lengthSuffix.size());
                if (arrayName.find('.') == std::string_view::npos) {
                    auto expression = MakeExpression(
                        ExpressionSyntax::Kind::ArrayLength,
                        subject.span);
                    expression->text = std::string{arrayName};
                    return expression;
                }
            }
            {
                auto expression = MakeExpression(
                    ExpressionSyntax::Kind::Reference,
                    subject.span);
                expression->text = subject.name;
                expression->reference = std::move(subject);
                return expression;
            }
        }
        Fail(
            CompileDiagnosticCode::ExpectedToken,
            Current().span,
            "expected an expression");
    }

#include "frontend_mouse.inc"

    const std::vector<language::Lexeme>& lexemes_;
    const CompilerLimits& limits_;
    DiagnosticSink& diagnostics_;
    std::size_t position_{};
    std::size_t previousPosition_{};
    std::uint64_t nodeCount_{};
    std::uint32_t nestingDepth_{};
};

} // namespace

std::optional<SyntaxTree> ParseSource(
    const SourceFile& source,
    const CompilerLimits& limits,
    DiagnosticSink& diagnostics)
{
    language::ScanOptions options{};
    options.retainTrivia = false;
    options.maximumSignificantLexemes = limits.maximumTokens == 0U
        ? 0U
        : static_cast<std::size_t>(limits.maximumTokens - 1U);
    options.maximumStoredLexemes = options.maximumSignificantLexemes;
    options.maximumIssues = kMaximumCompileDiagnostics;
    language::ScanResult scan = language::ScanWeave(source.bytes, options);
    for (const language::LexicalIssue& issue : scan.issues) {
        diagnostics.Add(
            CompilerIssueCode(issue.code),
            CompilerSpan(issue.span),
            CompilerIssueMessage(issue.code));
    }
    if (scan.significantLexemeLimitReached || scan.storedLexemeLimitReached) {
        diagnostics.Add(
            CompileDiagnosticCode::TokenLimit,
            CompilerSpan(scan.lexemes.back().span),
            "token count exceeds the configured limit");
    }
    if (scan.lexemes.empty()) {
        diagnostics.Add(
            CompileDiagnosticCode::InternalCompiler,
            {},
            "language scanner returned no end-of-input lexeme");
        return std::nullopt;
    }
    return Parser(scan.lexemes, limits, diagnostics).Run();
}

} // namespace inputweaver::compiler
