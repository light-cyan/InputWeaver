#pragma once

#include "program/compiled_program.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace inputweaver::compiler {

using ConstantValue = std::variant<
    std::monostate,
    bool,
    std::uint8_t,
    ControlState,
    double,
    DurationValue>;

struct BoundExpression final {
    enum class Kind : std::uint8_t {
        BooleanConstant,
        StateConstant,
        ControlStateConstant,
        NumberConstant,
        DurationConstant,
        LoadValue,
        ReadControlState,
        LoadArrayLength,
        LoadArrayElement,
        Unary,
        Binary,
        LogicalAnd,
        LogicalOr,
    };

    Kind kind{};
    ExpressionType type{ExpressionType::None};
    SourceSpan span{};
    ConstantValue constant;
    bool booleanValue{};
    std::uint8_t stateValue{};
    ControlState controlStateValue{};
    double numberValue{};
    DurationValue durationValue{};
    ValueRef value{};
    ArrayId array{};
    ControlRef control{};
    UnaryOperator unary{};
    BinaryOperator binary{};
    std::unique_ptr<BoundExpression> left;
    std::unique_ptr<BoundExpression> right;
};

struct BoundAction final {
    enum class Kind : std::uint8_t {
        Press,
        Release,
        Tap,
        Wait,
        Gap,
        Set,
        Toggle,
        SetArrayElement,
        ToggleArrayElement,
        AppendArrayElement,
        PopArrayElement,
        ClearArray,
        Exec,
        If,
        Repeat,
        While,
    };

    Kind kind{};
    SourceSpan span{};
    ControlRef control{};
    ValueRef value{};
    ArrayId array{};
    std::string command;
    std::unique_ptr<BoundExpression> expression;
    std::unique_ptr<BoundExpression> index;
    std::vector<BoundAction> body;
    std::vector<BoundAction> alternative;
};

struct BoundVariableDebug final {
    std::string name;
    ValueRef value{};
    SourceSpan declaration{};
};

struct BoundArrayDebug final {
    std::string name;
    ArrayId array{};
    SourceSpan declaration{};
};

struct BoundRule final {
    enum class Kind : std::uint8_t {
        Event,
        Mapping,
        Exit,
        Pause,
    };

    Kind kind{};
    ControlRef source{};
    EventTransition transition{};
    ControlRef target{};
    std::unique_ptr<BoundExpression> condition;
    std::vector<BoundAction> actions;
    Delivery delivery{Delivery::Observe};
    MatchFlow flow{MatchFlow::Stop};
    PauseEffect pauseEffect{PauseEffect::On};
    std::uint32_t sourceOrdinal{};
    SourceSpan sourceSpan{};
    SourceSpan actionFlowSpan{};
};

struct BoundProgram final {
    std::string displayPath;
    std::uint32_t sourceByteLength{};
    std::vector<std::uint32_t> lineStarts;
    TargetSelectorKind targetKind{TargetSelectorKind::Unspecified};
    std::string targetText;
    SourceSpan targetSource{};
    DurationValue tapDuration{30'000'000};
    DurationValue actionGap{10'000'000};
    std::uint64_t randomSeed{};
    UserValueLayout userValues;
    std::vector<ArrayDescriptor> arrays;
    std::vector<std::uint8_t> initialArrayStates;
    std::vector<double> initialArrayNumbers;
    std::vector<BoundVariableDebug> variables;
    std::vector<BoundArrayDebug> arrayDebug;
    std::vector<BoundRule> rules;
};

} // namespace inputweaver::compiler
