#include "lowering.hpp"

#include "program/program_requirements.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace inputweaver::compiler {
namespace {

template <typename Value>
[[nodiscard]] std::uint32_t AppendIndex(std::vector<Value>& values, Value value)
{
    const std::uint32_t index = static_cast<std::uint32_t>(values.size());
    values.push_back(std::move(value));
    return index;
}

class Lowerer final {
public:
    Lowerer(BoundProgram program, std::string_view sourceText)
        : program_(std::move(program)), sourceText_(sourceText) {}

    [[nodiscard]] FinalizeResult Run()
    {
        InitializeStorage();
        LowerRules();
        BuildControlRequirements();
        builder_.DeriveRequirements();
        return std::move(builder_).Finalize();
    }

private:
    struct LocalExpressionCode final {
        std::vector<ExpressionInstruction> instructions;
        std::vector<SourceSpan> spans;
    };

    struct LocalActionCode final {
        std::vector<ActionInstruction> instructions;
        std::vector<SourceSpan> spans;
        std::set<std::uint32_t> acquiredControls;
        std::uint32_t repeatFrameCount{};
    };

    [[nodiscard]] StringId InternSourceText(SourceSpan span)
    {
        const std::uint64_t end = static_cast<std::uint64_t>(span.beginByte)
            + span.byteLength;
        if (end > sourceText_.size()) {
            return {};
        }
        return InternString(std::string{
            sourceText_.substr(span.beginByte, span.byteLength)});
    }

    void InitializeStorage()
    {
        CompiledProgramStorage& storage = builder_.Storage();
        storage.source.displayPath = InternString(program_.displayPath);
        storage.source.byteLength = program_.sourceByteLength;
        storage.lineStarts = program_.lineStarts;
        storage.source.lineStarts = {
            0U,
            static_cast<std::uint32_t>(storage.lineStarts.size())};
        storage.settings.target.kind = program_.targetKind;
        storage.settings.target.source = program_.targetSource;
        if (program_.targetKind == TargetSelectorKind::Executable) {
            storage.settings.target.text = InternString(program_.targetText);
        }
        storage.settings.tapDuration = program_.tapDuration;
        storage.settings.actionGap = program_.actionGap;
        storage.settings.randomSeed = program_.randomSeed;
        storage.settings.mouseIdleTimeout = program_.mouseIdleTimeout;
        storage.userValues = std::move(program_.userValues);
        storage.arrays = std::move(program_.arrays);
        storage.initialArrayStates = std::move(program_.initialArrayStates);
        storage.initialArrayNumbers = std::move(program_.initialArrayNumbers);
        for (const BoundVariableDebug& variable : program_.variables) {
            storage.debugInfo.variables.push_back({
                InternString(variable.name),
                InternValue(variable.value),
                variable.declaration});
        }
        for (const BoundArrayDebug& array : program_.arrayDebug) {
            storage.debugInfo.arrays.push_back({
                InternString(array.name),
                array.array,
                array.declaration});
        }
        for (const auto& source : program_.eventSources) {
            const auto period = LowerExpression(*source.period);
            storage.eventSources.push_back({InternString(source.name), source.transition, period, source.declaration});
        }
    }

    [[nodiscard]] StringId InternString(const std::string& value)
    {
        const auto found = strings_.find(value);
        if (found != strings_.end()) {
            return StringId{found->second};
        }
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t index = AppendIndex(storage.strings, value);
        strings_.emplace(value, index);
        return StringId{index};
    }

    [[nodiscard]] ControlRefId InternControl(ControlRef value)
    {
        const auto found = controls_.find(value);
        if (found != controls_.end()) {
            return ControlRefId{found->second};
        }
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t index = AppendIndex(storage.controls, value);
        controls_.emplace(value, index);
        return ControlRefId{index};
    }

    [[nodiscard]] ValueRefId InternValue(ValueRef value)
    {
        const auto found = values_.find(value);
        if (found != values_.end()) {
            return ValueRefId{found->second};
        }
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t index = AppendIndex(storage.valueRefs, value);
        values_.emplace(value, index);
        return ValueRefId{index};
    }

    [[nodiscard]] std::uint32_t InternNumber(double value)
    {
        if (value == 0.0) {
            value = 0.0;
        }
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        const auto found = numbers_.find(bits);
        if (found != numbers_.end()) {
            return found->second;
        }
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t index = AppendIndex(storage.numberConstants, value);
        numbers_.emplace(bits, index);
        return index;
    }

    [[nodiscard]] std::uint32_t InternDuration(DurationValue value)
    {
        const auto found = durations_.find(value.nanoseconds);
        if (found != durations_.end()) {
            return found->second;
        }
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t index = AppendIndex(storage.durationConstants, value);
        durations_.emplace(value.nanoseconds, index);
        return index;
    }

    void EmitExpression(
        const BoundExpression& expression,
        LocalExpressionCode& code)
    {
        const auto emit = [&code, &expression](ExpressionInstruction instruction) {
            code.instructions.push_back(instruction);
            code.spans.push_back(expression.span);
        };
        switch (expression.kind) {
        case BoundExpression::Kind::LoadField:
            emit({ExpressionOpcode::LoadField, expression.type,
                expression.field.source.value, expression.field.Selector()});
            break;
        case BoundExpression::Kind::BooleanConstant:
            emit({
                ExpressionOpcode::PushBoolean,
                ExpressionType::Boolean,
                expression.booleanValue ? 1U : 0U,
                0U});
            break;
        case BoundExpression::Kind::StateConstant:
            emit({
                ExpressionOpcode::PushState,
                ExpressionType::State,
                expression.stateValue,
                0U});
            break;
        case BoundExpression::Kind::ControlStateConstant:
            emit({
                ExpressionOpcode::PushControlState,
                ExpressionType::ControlState,
                static_cast<std::uint32_t>(expression.controlStateValue),
                0U});
            break;
        case BoundExpression::Kind::NumberConstant:
            emit({
                ExpressionOpcode::PushNumber,
                ExpressionType::Number,
                InternNumber(expression.numberValue),
                0U});
            break;
        case BoundExpression::Kind::DurationConstant:
            emit({
                ExpressionOpcode::PushDuration,
                ExpressionType::Duration,
                InternDuration(expression.durationValue),
                0U});
            break;
        case BoundExpression::Kind::LoadValue:
            emit({
                ExpressionOpcode::LoadValue,
                expression.type,
                InternValue(expression.value).value,
                0U});
            break;
        case BoundExpression::Kind::ReadControlState:
            emit({
                ExpressionOpcode::ReadControlState,
                ExpressionType::ControlState,
                InternControl(expression.control).value,
                0U});
            break;
        case BoundExpression::Kind::LoadArrayLength:
            emit({
                ExpressionOpcode::LoadArrayLength,
                ExpressionType::Number,
                expression.array.value,
                0U});
            break;
        case BoundExpression::Kind::LoadArrayElement:
            EmitExpression(*expression.left, code);
            emit({
                ExpressionOpcode::LoadArrayElement,
                expression.type,
                expression.array.value,
                0U});
            break;
        case BoundExpression::Kind::Unary:
            EmitExpression(*expression.left, code);
            emit({
                ExpressionOpcode::Unary,
                expression.type,
                static_cast<std::uint32_t>(expression.unary),
                0U});
            break;
        case BoundExpression::Kind::Binary:
            EmitExpression(*expression.left, code);
            EmitExpression(*expression.right, code);
            emit({
                ExpressionOpcode::Binary,
                expression.type,
                static_cast<std::uint32_t>(expression.binary),
                0U});
            break;
        case BoundExpression::Kind::LogicalAnd:
        case BoundExpression::Kind::LogicalOr: {
            EmitExpression(*expression.left, code);
            const std::size_t conditional = code.instructions.size();
            emit({
                expression.kind == BoundExpression::Kind::LogicalAnd
                    ? ExpressionOpcode::JumpIfFalse
                    : ExpressionOpcode::JumpIfTrue,
                ExpressionType::None,
                0U,
                0U});
            EmitExpression(*expression.right, code);
            const std::size_t jump = code.instructions.size();
            emit({ExpressionOpcode::Jump, ExpressionType::None, 0U, 0U});
            const std::uint32_t constantPosition = static_cast<std::uint32_t>(
                code.instructions.size());
            emit({
                ExpressionOpcode::PushBoolean,
                ExpressionType::Boolean,
                expression.kind == BoundExpression::Kind::LogicalAnd ? 0U : 1U,
                0U});
            const std::uint32_t end = static_cast<std::uint32_t>(
                code.instructions.size());
            code.instructions[conditional].operand0 = constantPosition;
            code.instructions[jump].operand0 = end;
            break;
        }
        }
    }

    [[nodiscard]] ExpressionId LowerExpression(const BoundExpression& expression)
    {
        LocalExpressionCode local;
        EmitExpression(expression, local);
        local.instructions.push_back({
            ExpressionOpcode::Return,
            expression.type,
            0U,
            0U});
        local.spans.push_back(expression.span);
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t begin = static_cast<std::uint32_t>(
            storage.expressionCode.size());
        const std::uint32_t count = static_cast<std::uint32_t>(
            local.instructions.size());
        const std::uint32_t maximumStack = ComputeMaximumExpressionStackDepth(
            local.instructions);
        storage.expressionCode.insert(
            storage.expressionCode.end(),
            local.instructions.begin(),
            local.instructions.end());
        storage.debugInfo.expressionInstructionSpans.insert(
            storage.debugInfo.expressionInstructionSpans.end(),
            local.spans.begin(),
            local.spans.end());
        const std::uint32_t descriptor = AppendIndex(storage.expressions, {
            {begin, count},
            expression.type,
            maximumStack,
            expression.span});
        return ExpressionId{descriptor};
    }

    void EmitAction(
        LocalActionCode& code,
        ActionOpcode opcode,
        std::uint32_t operand0,
        std::uint32_t operand1,
        SourceSpan span)
    {
        EmitAction(code, opcode, operand0, operand1, 0U, span);
    }

    void EmitAction(
        LocalActionCode& code,
        ActionOpcode opcode,
        std::uint32_t operand0,
        std::uint32_t operand1,
        std::uint32_t operand2,
        SourceSpan span)
    {
        code.instructions.push_back({opcode, operand0, operand1, operand2});
        code.spans.push_back(span);
    }

    void LowerActionFlow(
        const std::vector<BoundAction>& actions,
        LocalActionCode& code)
    {
        for (const BoundAction& action : actions) {
            switch (action.kind) {
            case BoundAction::Kind::Pointer: {
                const auto first = LowerExpression(*action.expression);
                const auto second = action.secondExpression ? LowerExpression(*action.secondExpression).value : 0U;
                EmitAction(code, ActionOpcode::Pointer, static_cast<std::uint32_t>(action.pointerOperation),
                    first.value, second, action.span);
                break;
            }
            case BoundAction::Kind::RestartEvent:
                EmitAction(code, ActionOpcode::RestartEvent, action.eventSource.value, 0U, action.span);
                break;
            case BoundAction::Kind::Press:
            case BoundAction::Kind::Release:
            case BoundAction::Kind::Tap: {
                const ControlRefId control = InternControl(action.control);
                ActionOpcode opcode = ActionOpcode::Press;
                if (action.kind == BoundAction::Kind::Release) {
                    opcode = ActionOpcode::Release;
                } else if (action.kind == BoundAction::Kind::Tap) {
                    opcode = ActionOpcode::Tap;
                }
                EmitAction(code, opcode, control.value, 0U, action.span);
                if (opcode != ActionOpcode::Release) {
                    code.acquiredControls.insert(control.value);
                }
                break;
            }
            case BoundAction::Kind::Wait:
                EmitAction(
                    code,
                    ActionOpcode::Wait,
                    LowerExpression(*action.expression).value,
                    0U,
                    action.span);
                break;
            case BoundAction::Kind::Gap:
                EmitAction(
                    code,
                    ActionOpcode::Gap,
                    0U,
                    0U,
                    action.span);
                break;
            case BoundAction::Kind::Set:
                EmitAction(
                    code,
                    ActionOpcode::Set,
                    InternValue(action.value).value,
                    LowerExpression(*action.expression).value,
                    action.span);
                break;
            case BoundAction::Kind::Toggle:
                EmitAction(
                    code,
                    ActionOpcode::Toggle,
                    InternValue(action.value).value,
                    0U,
                    action.span);
                break;
            case BoundAction::Kind::SetArrayElement: {
                const ExpressionId index = LowerExpression(*action.index);
                const ExpressionId value = LowerExpression(*action.expression);
                EmitAction(
                    code,
                    ActionOpcode::SetArrayElement,
                    action.array.value,
                    index.value,
                    value.value,
                    action.span);
                break;
            }
            case BoundAction::Kind::ToggleArrayElement:
                EmitAction(
                    code,
                    ActionOpcode::ToggleArrayElement,
                    action.array.value,
                    LowerExpression(*action.index).value,
                    action.span);
                break;
            case BoundAction::Kind::AppendArrayElement:
                EmitAction(
                    code,
                    ActionOpcode::AppendArrayElement,
                    action.array.value,
                    LowerExpression(*action.expression).value,
                    action.span);
                break;
            case BoundAction::Kind::PopArrayElement:
                EmitAction(
                    code,
                    ActionOpcode::PopArrayElement,
                    action.array.value,
                    InternValue(action.value).value,
                    action.span);
                break;
            case BoundAction::Kind::ClearArray:
                EmitAction(
                    code,
                    ActionOpcode::ClearArray,
                    action.array.value,
                    0U,
                    action.span);
                break;
            case BoundAction::Kind::Exec:
                EmitAction(
                    code,
                    ActionOpcode::Exec,
                    InternString(action.command).value,
                    0U,
                    action.span);
                break;
            case BoundAction::Kind::If: {
                const ExpressionId condition = LowerExpression(*action.expression);
                const std::size_t conditional = code.instructions.size();
                EmitAction(
                    code,
                    ActionOpcode::JumpIfFalse,
                    condition.value,
                    0U,
                    action.span);
                LowerActionFlow(action.body, code);
                if (action.alternative.empty()) {
                    code.instructions[conditional].operand1 = static_cast<std::uint32_t>(
                        code.instructions.size());
                } else {
                    const std::size_t jump = code.instructions.size();
                    EmitAction(code, ActionOpcode::Jump, 0U, 0U, action.span);
                    code.instructions[conditional].operand1 = static_cast<std::uint32_t>(
                        code.instructions.size());
                    LowerActionFlow(action.alternative, code);
                    code.instructions[jump].operand0 = static_cast<std::uint32_t>(
                        code.instructions.size());
                }
                break;
            }
            case BoundAction::Kind::Repeat: {
                const std::uint32_t frame = code.repeatFrameCount++;
                const ExpressionId limit = LowerExpression(*action.expression);
                EmitAction(
                    code,
                    ActionOpcode::RepeatInit,
                    frame,
                    limit.value,
                    action.span);
                const std::uint32_t check = static_cast<std::uint32_t>(
                    code.instructions.size());
                EmitAction(
                    code,
                    ActionOpcode::RepeatCheck,
                    frame,
                    0U,
                    action.span);
                LowerActionFlow(action.body, code);
                EmitAction(
                    code,
                    ActionOpcode::RepeatNext,
                    frame,
                    0U,
                    action.span);
                EmitAction(code, ActionOpcode::Yield, 0U, 0U, action.span);
                EmitAction(code, ActionOpcode::Jump, check, 0U, action.span);
                code.instructions[check].operand1 = static_cast<std::uint32_t>(
                    code.instructions.size());
                break;
            }
            case BoundAction::Kind::While: {
                const std::uint32_t header = static_cast<std::uint32_t>(
                    code.instructions.size());
                const ExpressionId condition = LowerExpression(*action.expression);
                const std::size_t conditional = code.instructions.size();
                EmitAction(
                    code,
                    ActionOpcode::JumpIfFalse,
                    condition.value,
                    0U,
                    action.span);
                LowerActionFlow(action.body, code);
                EmitAction(code, ActionOpcode::Yield, 0U, 0U, action.span);
                EmitAction(code, ActionOpcode::Jump, header, 0U, action.span);
                code.instructions[conditional].operand1 = static_cast<std::uint32_t>(
                    code.instructions.size());
                break;
            }
            }
        }
    }

    [[nodiscard]] ActionProgramId LowerActionProgram(
        const std::vector<BoundAction>& actions,
        SourceSpan source)
    {
        LocalActionCode local;
        LowerActionFlow(actions, local);
        EmitAction(local, ActionOpcode::End, 0U, 0U, source);
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t begin = static_cast<std::uint32_t>(
            storage.actionCode.size());
        const std::uint32_t count = static_cast<std::uint32_t>(
            local.instructions.size());
        storage.actionCode.insert(
            storage.actionCode.end(),
            local.instructions.begin(),
            local.instructions.end());
        storage.debugInfo.actionInstructionSpans.insert(
            storage.debugInfo.actionInstructionSpans.end(),
            local.spans.begin(),
            local.spans.end());
        const std::uint32_t descriptor = AppendIndex(storage.actionPrograms, {
            {begin, count},
            local.repeatFrameCount,
            static_cast<std::uint32_t>(local.acquiredControls.size()),
            source});
        return ActionProgramId{descriptor};
    }

    [[nodiscard]] MappingSlotId InternMappingSlot(ControlRefId source)
    {
        const auto found = mappingSlots_.find(source.value);
        if (found != mappingSlots_.end()) {
            return MappingSlotId{found->second};
        }
        CompiledProgramStorage& storage = builder_.Storage();
        const std::uint32_t index = AppendIndex(
            storage.mappingSlots,
            MappingSlotDescriptor{source});
        mappingSlots_.emplace(source.value, index);
        return MappingSlotId{index};
    }

    void LowerRules()
    {
        CompiledProgramStorage& storage = builder_.Storage();
        for (const BoundRule& rule : program_.rules) {
            ExpressionId condition{};
            if (rule.condition != nullptr) {
                condition = LowerExpression(*rule.condition);
            }
            const ControlRefId source = rule.transition <= EventTransition::Up ? InternControl(rule.source) : ControlRefId{};
            const EventKey key{source, rule.transition, rule.eventSource};
            if (rule.kind == BoundRule::Kind::Exit) {
                exitRules_[key].push_back({
                    condition,
                    rule.sourceOrdinal,
                    rule.sourceSpan});
                continue;
            }
            if (rule.kind == BoundRule::Kind::Pause) {
                pauseRules_[key].push_back({
                    condition,
                    rule.delivery,
                    rule.pauseEffect,
                    rule.sourceOrdinal,
                    rule.sourceSpan});
                continue;
            }
            if (rule.kind == BoundRule::Kind::Mapping) {
                storage.debugInfo.rules.push_back({
                    rule.sourceOrdinal,
                    rule.condition != nullptr
                        ? InternSourceText(rule.condition->span)
                        : StringId{},
                    InternSourceText(rule.sourceSpan)});
                const MappingSlotId slot = InternMappingSlot(source);
                const MappingId mapping{AppendIndex(storage.mappings, {
                    slot,
                    InternControl(rule.target),
                    rule.sourceSpan})};
                eventRules_[key].push_back({
                    condition,
                    ActionProgramId{},
                    mapping,
                    Delivery::Consume,
                    MatchFlow::Stop,
                    RuleKind::MappingDown,
                    rule.sourceOrdinal,
                    rule.sourceSpan});
                continue;
            }
            ActionProgramId action{};
            if (!rule.actions.empty()) {
                action = LowerActionProgram(rule.actions, rule.actionFlowSpan);
            }
            storage.debugInfo.rules.push_back({
                rule.sourceOrdinal,
                rule.condition != nullptr
                    ? InternSourceText(rule.condition->span)
                    : StringId{},
                InternSourceText(rule.actionFlowSpan)});
            eventRules_[key].push_back({
                condition,
                action,
                MappingId{},
                rule.delivery,
                rule.flow,
                RuleKind::Event,
                rule.sourceOrdinal,
                rule.sourceSpan});
        }

        for (auto& [key, rules] : exitRules_) {
            const std::uint32_t begin = static_cast<std::uint32_t>(
                storage.exitControlRules.size());
            storage.exitControlRules.insert(
                storage.exitControlRules.end(),
                rules.begin(),
                rules.end());
            storage.exitControlBuckets.push_back({
                key,
                {begin, static_cast<std::uint32_t>(rules.size())}});
        }
        for (auto& [key, rules] : pauseRules_) {
            const std::uint32_t begin = static_cast<std::uint32_t>(
                storage.pauseControlRules.size());
            storage.pauseControlRules.insert(
                storage.pauseControlRules.end(),
                rules.begin(),
                rules.end());
            storage.pauseControlBuckets.push_back({
                key,
                {begin, static_cast<std::uint32_t>(rules.size())}});
        }
        for (auto& [key, rules] : eventRules_) {
            const std::uint32_t begin = static_cast<std::uint32_t>(
                storage.rules.size());
            storage.rules.insert(storage.rules.end(), rules.begin(), rules.end());
            storage.eventBuckets.push_back({
                key,
                {begin, static_cast<std::uint32_t>(rules.size())}});
        }
    }

    void BuildControlRequirements()
    {
        CompiledProgramStorage& storage = builder_.Storage();
        std::vector<std::uint8_t> uses(storage.controls.size(), 0U);
        const auto add = [&uses](ControlRefId control, ControlUse use) {
            if (!control.IsValid()) return;
            uses[control.value] = static_cast<std::uint8_t>(
                uses[control.value] | ToControlUseBits(use));
        };
        for (const ExpressionInstruction instruction : storage.expressionCode) {
            if (instruction.opcode == ExpressionOpcode::ReadControlState) {
                add(ControlRefId{instruction.operand0}, ControlUse::PhysicalState);
            }
        }
        for (const ActionInstruction instruction : storage.actionCode) {
            if (instruction.opcode == ActionOpcode::Press
                || instruction.opcode == ActionOpcode::Release
                || instruction.opcode == ActionOpcode::Tap) {
                add(ControlRefId{instruction.operand0}, ControlUse::OutputDownUp);
            }
        }
        for (const PauseControlBucket& bucket : storage.pauseControlBuckets) {
            add(bucket.key.control, ControlUse::EventSource);
        }
        for (const ExitControlBucket& bucket : storage.exitControlBuckets) {
            add(bucket.key.control, ControlUse::EventSource);
        }
        for (const EventBucket& bucket : storage.eventBuckets) {
            add(bucket.key.control, ControlUse::EventSource);
        }
        for (const MappingSlotDescriptor slot : storage.mappingSlots) {
            add(slot.source, ControlUse::EventSource);
        }
        for (const MappingDescriptor mapping : storage.mappings) {
            add(mapping.target, ControlUse::OutputDownUp);
            add(mapping.target, ControlUse::OutputAgain);
        }
        for (std::size_t index = 0U; index < uses.size(); ++index) {
            if (uses[index] != 0U) {
                storage.controlRequirements.push_back({
                    ControlRefId{static_cast<std::uint32_t>(index)},
                    uses[index]});
            }
        }
    }

    BoundProgram program_;
    std::string_view sourceText_;
    CompiledProgramBuilder builder_;
    std::map<std::string, std::uint32_t, std::less<>> strings_;
    std::map<ControlRef, std::uint32_t> controls_;
    std::map<ValueRef, std::uint32_t> values_;
    std::map<std::uint64_t, std::uint32_t> numbers_;
    std::map<std::int64_t, std::uint32_t> durations_;
    std::map<std::uint32_t, std::uint32_t> mappingSlots_;
    std::map<EventKey, std::vector<ExitControlRule>> exitRules_;
    std::map<EventKey, std::vector<PauseControlRule>> pauseRules_;
    std::map<EventKey, std::vector<CompiledRule>> eventRules_;
};

} // namespace

FinalizeResult LowerProgram(BoundProgram program, std::string_view sourceText)
{
    return Lowerer(std::move(program), sourceText).Run();
}

} // namespace inputweaver::compiler
