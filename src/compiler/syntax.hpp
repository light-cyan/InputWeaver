#pragma once

#include "program/compiled_program.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace inputweaver::compiler {

enum class RuleArrowSyntax : std::uint8_t {
    ConsumeStop,
    ConsumeContinue,
    ObserveStop,
    ObserveContinue,
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

struct ExpressionSyntax;

struct TargetSyntax final {
    std::string name;
    SourceSpan span{};
    std::unique_ptr<ExpressionSyntax> index;
};

struct ExpressionSyntax final {
    enum class Kind : std::uint8_t {
        StateLiteral,
        ControlStateLiteral,
        NumberLiteral,
        DurationLiteral,
        Reference,
        ArrayElement,
        ArrayLength,
        Unary,
        Binary,
    };

    Kind kind{};
    SourceSpan span{};
    std::uint32_t depth{1U};
    std::string text;
    bool stateValue{};
    ControlState controlStateValue{};
    ControlSyntax reference;
    bool completed{};
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
        Append,
        Pop,
        Clear,
        Exec,
        If,
        Repeat,
        While,
        Pointer,
        RestartMeter,
    };

    Kind kind{};
    SourceSpan span{};
    std::string name;
    std::string secondaryName;
    ControlSyntax control;
    TargetSyntax target;
    std::string stringValue;
    std::unique_ptr<ExpressionSyntax> expression;
    std::unique_ptr<ExpressionSyntax> secondExpression;
    std::vector<ActionSyntax> body;
    std::vector<ActionSyntax> alternative;
};

struct ArrayLiteralElementSyntax final {
    std::string text;
    SourceSpan span{};
};

struct TopLevelSyntax final {
    enum class Kind : std::uint8_t {
        TargetSetting,
        TapDurationSetting,
        ActionGapSetting,
        RandomSeedSetting,
        StateDeclaration,
        NumberDeclaration,
        DurationDeclaration,
        StateArrayDeclaration,
        NumberArrayDeclaration,
        Mapping,
        ExitRule,
        PauseRule,
        EventRule,
        MouseIdleTimeoutSetting,
        MeterDeclaration,
    };

    Kind kind{};
    SourceSpan span{};
    SourceSpan valueSpan{};
    SourceSpan actionFlowSpan{};
    std::string name;
    std::string literal;
    std::vector<ArrayLiteralElementSyntax> arrayLiterals;
    bool stateValue{};
    bool targetGlobal{};
    ControlSyntax sourceControl;
    ControlSyntax targetControl;
    EventSyntax event;
    std::unique_ptr<ExpressionSyntax> condition;
    RuleArrowSyntax arrow{};
    std::string pauseEffect;
    std::vector<ActionSyntax> actions;
};

struct SyntaxTree final {
    std::vector<TopLevelSyntax> items;
};

} // namespace inputweaver::compiler
