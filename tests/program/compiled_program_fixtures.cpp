#include "compiled_program_fixtures.hpp"

#include "program/program_validator.hpp"

#include <cstdint>
#include <string>

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
        if (source[index] == '\n' && index + 1U < source.size()) {
            storage.lineStarts.push_back(static_cast<std::uint32_t>(index + 1U));
        }
    }
    storage.source.lineStarts = {
        0U,
        static_cast<std::uint32_t>(storage.lineStarts.size())};
    storage.settings.target = {
        TargetSelectorKind::Global,
        StringId{},
        {0U, storage.source.byteLength}};
    storage.settings.tapDuration = kDefaultTapDuration;
    storage.settings.actionGap = kDefaultActionGap;
}

[[nodiscard]] SourceSpan WholeSource(
    const CompiledProgramStorage& storage) noexcept
{
    return {0U, storage.source.byteLength};
}

void AddCommonControls(CompiledProgramStorage& storage)
{
    storage.controls = {
        kF7,
        kF6,
    };
}

void AddTapAction(CompiledProgramStorage& storage)
{
    const SourceSpan source = WholeSource(storage);
    storage.actionPrograms.push_back({{0U, 2U}, 0U, 1U, source});
    storage.actionCode = {
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans = {source, source};
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
    AddTapAction(storage);
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
        WholeSource(storage)});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 1U}});
    Derive(storage);
    return storage;
}

CompiledProgramStorage MakeMappingFixtureStorage()
{
    static const std::string source =
        "TARGET = GLOBAL;\n"
        "F6 := F7;\n";
    CompiledProgramStorage storage{};
    SetCommonSource(storage, "fixture.mapping.weave", source);
    AddCommonControls(storage);
    storage.controlRequirements = {
        {ControlRefId{0U}, static_cast<std::uint8_t>(
            ToControlUseBits(ControlUse::OutputDownUp)
            | ToControlUseBits(ControlUse::OutputRepeat))},
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.mappingSlots.push_back({
        ControlRefId{1U}});
    storage.mappings.push_back({
        MappingSlotId{0U},
        ControlRefId{0U},
        WholeSource(storage)});
    storage.rules.push_back({
        ExpressionId{},
        ActionProgramId{},
        MappingId{0U},
        Delivery::Consume,
        MatchFlow::Stop,
        RuleKind::MappingDown,
        0U,
        WholeSource(storage)});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 1U}});
    Derive(storage);
    return storage;
}

CompiledProgramStorage MakeConditionalRepeatFixtureStorage()
{
    static const std::string source =
        "TARGET = GLOBAL;\n"
        "state enabled = on;\n"
        "F6:down when enabled[on] => repeat 2 do tap(F7) | end;\n";
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
        WholeSource(storage)});

    storage.numberConstants.push_back(2.0);
    storage.expressions = {
        {{0U, 4U}, ExpressionType::Boolean, 2U, WholeSource(storage)},
        {{4U, 2U}, ExpressionType::Number, 1U, WholeSource(storage)},
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
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(),
        WholeSource(storage));

    storage.actionPrograms.push_back({
        {0U, 8U},
        1U,
        1U,
        WholeSource(storage)});
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
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(),
        WholeSource(storage));

    storage.rules.push_back({
        ExpressionId{0U},
        ActionProgramId{0U},
        MappingId{},
        Delivery::Consume,
        MatchFlow::Stop,
        RuleKind::Event,
        0U,
        WholeSource(storage)});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 1U}});
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
        WholeSource(storage)});
    storage.pauseControlBuckets.push_back({
        {ControlRefId{0U}, EventTransition::Down},
        {0U, 1U}});
    Derive(storage);
    return storage;
}

} // namespace inputweaver::test
