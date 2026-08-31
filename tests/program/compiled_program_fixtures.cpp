#include "compiled_program_fixtures.hpp"

#include "program/program_requirements.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace inputweaver::test {
namespace {

constexpr DurationValue kDefaultTapDuration{30'000'000};
constexpr DurationValue kDefaultActionGap{10'000'000};
constexpr ControlRef kF6{
    kControlNamespaceUsbHid,
    0x07U,
    0x3fU,
    kControlQualifierNone};
constexpr ControlRef kF7{
    kControlNamespaceUsbHid,
    0x07U,
    0x40U,
    kControlQualifierNone};
constexpr ControlRef kF12{
    kControlNamespaceUsbHid,
    0x07U,
    0x45U,
    kControlQualifierNone};
constexpr ControlRef kLeftControl{
    kControlNamespaceUsbHid,
    0x07U,
    0xe0U,
    kControlQualifierNone};
constexpr ControlRef kRightControl{
    kControlNamespaceUsbHid,
    0x07U,
    0xe4U,
    kControlQualifierNone};
constexpr ControlRef kLeftShift{
    kControlNamespaceUsbHid,
    0x07U,
    0xe1U,
    kControlQualifierNone};
constexpr ControlRef kRightShift{
    kControlNamespaceUsbHid,
    0x07U,
    0xe5U,
    kControlQualifierNone};

[[nodiscard]] SourceSpan SpanOf(
    const std::string& source,
    std::string_view text) noexcept
{
    const std::size_t begin = source.find(text);
    assert(begin != std::string::npos);
    return {
        static_cast<std::uint32_t>(begin),
        static_cast<std::uint32_t>(text.size())};
}

void SetCommonSource(
    CompiledProgramStorage& storage,
    std::string displayPath,
    const std::string& source)
{
    storage.strings = {std::move(displayPath)};
    storage.source.displayPath = StringId{0U};
    storage.source.byteLength = static_cast<std::uint32_t>(source.size());
    storage.lineStarts.push_back(0U);
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\n') {
            storage.lineStarts.push_back(static_cast<std::uint32_t>(index + 1U));
        }
    }
    storage.source.lineStarts = {
        0U,
        static_cast<std::uint32_t>(storage.lineStarts.size())};
    storage.settings.target = {
        TargetSelectorKind::Global,
        StringId{},
        SpanOf(source, "GLOBAL")};
    storage.settings.tapDuration = kDefaultTapDuration;
    storage.settings.actionGap = kDefaultActionGap;
}

void AddCommonControls(CompiledProgramStorage& storage)
{
    storage.controls = {
        kF7,
        kF6,
    };
}

void AddTapAction(CompiledProgramStorage& storage, const std::string& source)
{
    const SourceSpan action = SpanOf(source, "tap(F7)");
    storage.actionPrograms.push_back({{0U, 2U}, 0U, 1U, action});
    storage.actionCode = {
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans = {action, action};
}

void AddExitControl(CompiledProgramStorage& storage)
{
    const ControlRefId leftControl{
        static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back(kLeftControl);
    const ControlRefId rightControl{
        static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back(kRightControl);
    const ControlRefId leftShift{
        static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back(kLeftShift);
    const ControlRefId rightShift{
        static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back(kRightShift);
    const ControlRefId f12{
        static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back(kF12);

    const ExpressionId condition{
        static_cast<std::uint32_t>(storage.expressions.size())};
    const std::uint32_t begin = static_cast<std::uint32_t>(
        storage.expressionCode.size());
    storage.expressions.push_back({
        {begin, 22U},
        ExpressionType::Boolean,
        2U,
        {}});
    storage.expressionCode.insert(storage.expressionCode.end(), {
        {ExpressionOpcode::ReadControlState, ExpressionType::ControlState,
            leftControl.value, 0U},
        {ExpressionOpcode::PushControlState, ExpressionType::ControlState,
            static_cast<std::uint32_t>(ControlState::Held), 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::JumpIfTrue, ExpressionType::None, 8U, 0U},
        {ExpressionOpcode::ReadControlState, ExpressionType::ControlState,
            rightControl.value, 0U},
        {ExpressionOpcode::PushControlState, ExpressionType::ControlState,
            static_cast<std::uint32_t>(ControlState::Held), 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Jump, ExpressionType::None, 9U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::JumpIfFalse, ExpressionType::None, 20U, 0U},
        {ExpressionOpcode::ReadControlState, ExpressionType::ControlState,
            leftShift.value, 0U},
        {ExpressionOpcode::PushControlState, ExpressionType::ControlState,
            static_cast<std::uint32_t>(ControlState::Held), 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::JumpIfTrue, ExpressionType::None, 18U, 0U},
        {ExpressionOpcode::ReadControlState, ExpressionType::ControlState,
            rightShift.value, 0U},
        {ExpressionOpcode::PushControlState, ExpressionType::ControlState,
            static_cast<std::uint32_t>(ControlState::Held), 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Jump, ExpressionType::None, 19U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::Jump, ExpressionType::None, 21U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    storage.debugInfo.expressionInstructionSpans.insert(
        storage.debugInfo.expressionInstructionSpans.end(),
        22U,
        SourceSpan{});

    for (const ControlRefId control : {
             leftControl,
             rightControl,
             leftShift,
             rightShift}) {
        storage.controlRequirements.push_back({
            control,
            ToControlUseBits(ControlUse::PhysicalState)});
    }
    storage.controlRequirements.push_back({
        f12,
        ToControlUseBits(ControlUse::EventSource)});
    storage.exitControlRules.push_back({
        condition,
        kInvalidProgramIndex,
        {}});
    storage.exitControlBuckets.push_back({
        {f12, EventTransition::Down},
        {0U, 1U}});
}

void Derive(CompiledProgramStorage& storage)
{
    storage.requirements = ComputeProgramRequirements(storage);
}

} // namespace

CompiledProgramStorage MakeTapFixtureStorage()
{
    static const std::string source =
        "TARGET = GLOBAL;\n"
        "F6:down => tap(F7);\n";
    CompiledProgramStorage storage{};
    SetCommonSource(storage, "fixture.tap.weave", source);
    AddCommonControls(storage);
    AddTapAction(storage, source);
    storage.controlRequirements = {
        {ControlRefId{0U}, ToControlUseBits(ControlUse::OutputDownUp)},
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.rules.push_back({
        ExpressionId{},
        ActionProgramId{0U},
        MappingId{},
        Delivery::Consume,
        MatchFlow::Stop,
        RuleKind::Event,
        0U,
        SpanOf(source, "F6:down => tap(F7);")});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 1U}});
    AddExitControl(storage);
    Derive(storage);
    return storage;
}

CompiledProgramStorage MakeMappingFixtureStorage()
{
    static const std::string source =
        "TARGET = GLOBAL;\n"
        "F6 -> F7;\n";
    CompiledProgramStorage storage{};
    SetCommonSource(storage, "fixture.mapping.weave", source);
    AddCommonControls(storage);
    storage.controlRequirements = {
        {ControlRefId{0U}, static_cast<std::uint8_t>(
            ToControlUseBits(ControlUse::OutputDownUp)
            | ToControlUseBits(ControlUse::OutputAgain))},
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.mappingSlots.push_back({
        ControlRefId{1U}});
    storage.mappings.push_back({
        MappingSlotId{0U},
        ControlRefId{0U},
        SpanOf(source, "F6 -> F7;")});
    storage.rules.push_back({
        ExpressionId{},
        ActionProgramId{},
        MappingId{0U},
        Delivery::Consume,
        MatchFlow::Stop,
        RuleKind::MappingDown,
        0U,
        SpanOf(source, "F6 -> F7;")});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 1U}});
    AddExitControl(storage);
    Derive(storage);
    return storage;
}

CompiledProgramStorage MakeConditionalRepeatFixtureStorage()
{
    static const std::string source =
        "TARGET = GLOBAL;\n"
        "state enabled = on;\n"
        "F6:down when enabled == on => repeat 2 do tap(F7) | end;\n";
    CompiledProgramStorage storage{};
    SetCommonSource(storage, "fixture.conditional-repeat.weave", source);
    storage.strings.push_back("enabled");
    AddCommonControls(storage);
    storage.controlRequirements = {
        {ControlRefId{0U}, ToControlUseBits(ControlUse::OutputDownUp)},
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };

    storage.userValues.initialStates.push_back(1U);
    storage.valueRefs.push_back({ValueDomain::UserState, ValueType::State, 0U});
    storage.debugInfo.variables.push_back({
        StringId{1U},
        ValueRefId{0U},
        SpanOf(source, "state enabled = on;")});

    storage.numberConstants.push_back(2.0);
    const SourceSpan condition = SpanOf(source, "enabled == on");
    const SourceSpan conditionValue{condition.beginByte, 7U};
    const SourceSpan conditionConstant{condition.beginByte + 11U, 2U};
    const SourceSpan repeatLimit = SpanOf(source, "2");
    storage.expressions = {
        {{0U, 4U}, ExpressionType::Boolean, 2U, condition},
        {{4U, 2U}, ExpressionType::Number, 1U, repeatLimit},
    };
    storage.expressionCode = {
        {ExpressionOpcode::LoadValue, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    };
    storage.debugInfo.expressionInstructionSpans = {
        conditionValue,
        conditionConstant,
        condition,
        condition,
        repeatLimit,
        repeatLimit,
    };

    const SourceSpan repeatAction = SpanOf(source, "repeat 2 do tap(F7) | end");
    const SourceSpan tapAction = SpanOf(source, "tap(F7)");
    const SourceSpan gapAction = SpanOf(source, "|");
    storage.actionPrograms.push_back({
        {0U, 8U},
        1U,
        1U,
        repeatAction});
    storage.actionCode = {
        {ActionOpcode::RepeatInit, 0U, 1U},
        {ActionOpcode::RepeatCheck, 0U, 7U},
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::Gap, 0U, 0U},
        {ActionOpcode::RepeatNext, 0U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 1U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans = {
        repeatAction,
        repeatAction,
        tapAction,
        gapAction,
        repeatAction,
        repeatAction,
        repeatAction,
        repeatAction,
    };

    storage.rules.push_back({
        ExpressionId{0U},
        ActionProgramId{0U},
        MappingId{},
        Delivery::Consume,
        MatchFlow::Stop,
        RuleKind::Event,
        0U,
        SpanOf(source,
        "F6:down when enabled == on => repeat 2 do tap(F7) | end;")});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 1U}});
    AddExitControl(storage);
    Derive(storage);
    return storage;
}

CompiledProgramStorage MakePauseControlFixtureStorage()
{
    static const std::string source =
        "TARGET = GLOBAL;\n"
        "pause F6:down => toggle;\n";
    CompiledProgramStorage storage{};
    SetCommonSource(storage, "fixture.pause-control.weave", source);
    storage.controls = {
        kF6,
    };
    storage.controlRequirements = {
        {ControlRefId{0U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.pauseControlRules.push_back({
        ExpressionId{},
        Delivery::Consume,
        PauseEffect::Toggle,
        0U,
        SpanOf(source, "pause F6:down => toggle;")});
    storage.pauseControlBuckets.push_back({
        {ControlRefId{0U}, EventTransition::Down},
        {0U, 1U}});
    AddExitControl(storage);
    Derive(storage);
    return storage;
}

} // namespace inputweaver::test
