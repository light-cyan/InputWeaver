#pragma once

#include "program/compiled_program.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace inputweaver::compiler {

enum class TokenKind : std::uint8_t {
    Word,
    Number,
    Duration,
    HexInteger,
    String,
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
    Invalid,
    EndOfFile,
};

struct Token final {
    TokenKind kind{};
    SourceSpan span{};
    std::string text;
};

struct RawControlArgumentSyntax final {
    std::string text;
    SourceSpan span{};
};

struct ControlSyntax final {
    std::string name;
    std::vector<RawControlArgumentSyntax> arguments;
    bool raw{};
    SourceSpan span{};
};

struct ExpressionSyntax final {
    enum class Kind : std::uint8_t {
        StateLiteral,
        NumberLiteral,
        DurationLiteral,
        ValueReference,
        StateQuery,
        Unary,
        Binary,
    };

    Kind kind{};
    SourceSpan span{};
    std::uint32_t depth{1U};
    std::string text;
    bool stateValue{};
    ControlSyntax querySubject;
    std::unique_ptr<ExpressionSyntax> left;
    std::unique_ptr<ExpressionSyntax> right;
};

struct EventSyntax final {
    ControlSyntax control;
    std::string transition;
    SourceSpan span{};
};

struct ActionSyntax final {
    enum class Kind : std::uint8_t {
        Input,
        Wait,
        Gap,
        Set,
        Toggle,
        Exec,
        If,
        Repeat,
        While,
    };

    Kind kind{};
    SourceSpan span{};
    std::string name;
    ControlSyntax control;
    std::string stringValue;
    std::unique_ptr<ExpressionSyntax> expression;
    std::vector<ActionSyntax> body;
    std::vector<ActionSyntax> alternative;
};

struct TopLevelSyntax final {
    enum class Kind : std::uint8_t {
        TargetSetting,
        TapDurationSetting,
        ActionGapSetting,
        StateDeclaration,
        NumberDeclaration,
        DurationDeclaration,
        Mapping,
        ExitRule,
        PauseRule,
        EventRule,
    };

    Kind kind{};
    SourceSpan span{};
    SourceSpan valueSpan{};
    SourceSpan actionFlowSpan{};
    std::string name;
    std::string literal;
    bool stateValue{};
    bool targetGlobal{};
    ControlSyntax sourceControl;
    ControlSyntax targetControl;
    EventSyntax event;
    std::unique_ptr<ExpressionSyntax> condition;
    TokenKind arrow{};
    std::string pauseEffect;
    std::vector<ActionSyntax> actions;
};

struct SyntaxTree final {
    std::vector<TopLevelSyntax> items;
};

} // namespace inputweaver::compiler
