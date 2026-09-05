#include "program_requirements.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace inputweaver {
namespace {

template <typename Id>
[[nodiscard]] bool IsTableId(Id id, std::size_t size) noexcept
{
    return id.value < size;
}

[[nodiscard]] bool IsTableRange(TableRange range, std::size_t size) noexcept
{
    const std::uint64_t end = static_cast<std::uint64_t>(range.begin)
        + range.count;
    return end <= size;
}

[[nodiscard]] std::uint32_t ToCount(std::size_t value) noexcept
{
    return value > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t SaturatingAdd(
    std::uint32_t left,
    std::uint32_t right) noexcept
{
    const std::uint64_t sum = static_cast<std::uint64_t>(left) + right;
    return sum > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(sum);
}

[[nodiscard]] bool HasMappingSource(
    const CompiledProgramStorage& storage,
    ControlRefId control) noexcept
{
    return std::any_of(
        storage.mappingSlots.begin(),
        storage.mappingSlots.end(),
        [control](const MappingSlotDescriptor& slot) {
            return slot.source == control;
        });
}

} // namespace

std::uint32_t ComputeMaximumExpressionStackDepth(
    std::span<const ExpressionInstruction> code)
{
    if (code.empty()) {
        return 0U;
    }
    std::vector<std::uint32_t> depths(code.size(), kInvalidProgramIndex);
    depths[0] = 0U;
    std::uint32_t maximum = 0U;
    for (std::size_t index = 0U; index < code.size(); ++index) {
        if (depths[index] == kInvalidProgramIndex) {
            continue;
        }
        std::uint32_t depth = depths[index];
        const ExpressionInstruction& instruction = code[index];
        bool fallthrough = true;
        const auto merge = [&](std::uint32_t target) {
            if (target < depths.size()
                && depths[target] == kInvalidProgramIndex) {
                depths[target] = depth;
            }
        };
        switch (instruction.opcode) {
        case ExpressionOpcode::PushBoolean:
        case ExpressionOpcode::PushState:
        case ExpressionOpcode::PushNumber:
        case ExpressionOpcode::PushDuration:
        case ExpressionOpcode::LoadValue:
        case ExpressionOpcode::ReadControlState:
        case ExpressionOpcode::PushControlState:
        case ExpressionOpcode::LoadArrayLength:
        case ExpressionOpcode::LoadField:
            ++depth;
            break;
        case ExpressionOpcode::Binary:
            --depth;
            break;
        case ExpressionOpcode::Jump:
            fallthrough = false;
            merge(instruction.operand0);
            break;
        case ExpressionOpcode::JumpIfFalse:
        case ExpressionOpcode::JumpIfTrue:
            --depth;
            merge(instruction.operand0);
            break;
        case ExpressionOpcode::Return:
            --depth;
            fallthrough = false;
            break;
        case ExpressionOpcode::Unary:
        case ExpressionOpcode::LoadArrayElement:
            break;
        }
        maximum = (std::max)(maximum, depth);
        if (fallthrough && index + 1U < code.size()) {
            merge(static_cast<std::uint32_t>(index + 1U));
        }
    }
    return maximum;
}

ProgramRequirements ComputeProgramRequirements(
    const CompiledProgramStorage& storage)
{
    ProgramRequirements requirements{};
    requirements.stateSlotCount = ToCount(storage.userValues.initialStates.size());
    requirements.numberSlotCount = ToCount(storage.userValues.initialNumbers.size());
    requirements.durationSlotCount = ToCount(storage.userValues.initialDurations.size());
    requirements.arrayCount = ToCount(storage.arrays.size());
    requirements.eventSourceCount = ToCount(storage.eventSources.size());
    requirements.requiresMouseObservation = !storage.eventSources.empty()
        || std::any_of(storage.expressionCode.begin(), storage.expressionCode.end(),
            [](const auto& instruction) { return instruction.opcode == ExpressionOpcode::LoadField; })
        || std::any_of(storage.eventBuckets.begin(), storage.eventBuckets.end(),
            [](const auto& bucket) { return bucket.key.transition >= EventTransition::Move; });
    requirements.requiresPointerOutput = std::any_of(
        storage.actionCode.begin(), storage.actionCode.end(),
        [](const auto& instruction) { return instruction.opcode == ActionOpcode::Pointer; });
    requirements.initialArrayElementBytes = static_cast<std::uint64_t>(
        storage.initialArrayStates.size())
        + static_cast<std::uint64_t>(storage.initialArrayNumbers.size())
            * sizeof(double);
    requirements.mappingSlotCount = ToCount(storage.mappingSlots.size());

    for (const ExpressionDescriptor& expression : storage.expressions) {
        requirements.maximumExpressionStackDepth = (std::max)(
            requirements.maximumExpressionStackDepth,
            expression.maximumStackDepth);
    }
    for (const ActionProgramDescriptor& action : storage.actionPrograms) {
        requirements.maximumRepeatFramesPerTask = (std::max)(
            requirements.maximumRepeatFramesPerTask,
            action.repeatFrameCount);
        requirements.maximumOwnedControlsPerTask = (std::max)(
            requirements.maximumOwnedControlsPerTask,
            action.maximumOwnedControlCount);
    }
    requirements.requiresProcessLaunch = std::any_of(
        storage.actionCode.begin(),
        storage.actionCode.end(),
        [](const ActionInstruction& instruction) {
            return instruction.opcode == ActionOpcode::Exec;
        });
    requirements.maximumMappingOperationsPerEvent = storage.mappings.empty()
        ? 0U
        : 1U;
    requirements.maximumTransactionItemsPerEvent =
        requirements.maximumMappingOperationsPerEvent;

    std::map<EventKey, std::uint32_t> predicateStepsByEvent;
    for (const ExitControlBucket& bucket : storage.exitControlBuckets) {
        requirements.maximumExitRulesPerEvent = (std::max)(
            requirements.maximumExitRulesPerEvent,
            bucket.rules.count);
        if (!IsTableRange(bucket.rules, storage.exitControlRules.size())) {
            continue;
        }
        std::uint32_t predicateSteps = 0U;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const ExitControlRule& rule = storage.exitControlRules[
                static_cast<std::size_t>(rawIndex)];
            if (IsTableId(rule.condition, storage.expressions.size())) {
                predicateSteps = SaturatingAdd(
                    predicateSteps,
                    storage.expressions[rule.condition.value].code.count);
            }
        }
        predicateStepsByEvent[bucket.key] = SaturatingAdd(
            predicateStepsByEvent[bucket.key],
            predicateSteps);
    }
    for (const PauseControlBucket& bucket : storage.pauseControlBuckets) {
        requirements.maximumPauseRulesPerEvent = (std::max)(
            requirements.maximumPauseRulesPerEvent,
            bucket.rules.count);
        if (!IsTableRange(bucket.rules, storage.pauseControlRules.size())) {
            continue;
        }
        std::uint32_t predicateSteps = 0U;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const PauseControlRule& rule = storage.pauseControlRules[
                static_cast<std::size_t>(rawIndex)];
            if (IsTableId(rule.condition, storage.expressions.size())) {
                predicateSteps = SaturatingAdd(
                    predicateSteps,
                    storage.expressions[rule.condition.value].code.count);
            }
        }
        predicateStepsByEvent[bucket.key] = SaturatingAdd(
            predicateStepsByEvent[bucket.key],
            predicateSteps);
    }

    for (const EventBucket& bucket : storage.eventBuckets) {
        requirements.maximumRulesPerEvent = (std::max)(
            requirements.maximumRulesPerEvent,
            bucket.rules.count);
        if (!IsTableRange(bucket.rules, storage.rules.size())) {
            continue;
        }
        std::uint32_t predicateSteps = 0U;
        std::uint32_t tasks = 0U;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const CompiledRule& rule = storage.rules[static_cast<std::size_t>(rawIndex)];
            if (IsTableId(rule.condition, storage.expressions.size())) {
                predicateSteps = SaturatingAdd(
                    predicateSteps,
                    storage.expressions[rule.condition.value].code.count);
            }
            if (rule.kind == RuleKind::Event
                && IsTableId(rule.action, storage.actionPrograms.size())) {
                tasks = SaturatingAdd(tasks, 1U);
            }
        }
        const std::uint32_t mappingOperations = HasMappingSource(
            storage,
            bucket.key.control) ? 1U : 0U;
        predicateStepsByEvent[bucket.key] = SaturatingAdd(
            predicateStepsByEvent[bucket.key],
            predicateSteps);
        requirements.maximumTasksPerEvent = (std::max)(
            requirements.maximumTasksPerEvent,
            tasks);
        requirements.maximumTransactionItemsPerEvent = (std::max)(
            requirements.maximumTransactionItemsPerEvent,
            SaturatingAdd(tasks, mappingOperations));
    }
    for (const auto& entry : predicateStepsByEvent) {
        requirements.maximumPredicateStepsPerEvent = (std::max)(
            requirements.maximumPredicateStepsPerEvent,
            entry.second);
    }
    return requirements;
}

} // namespace inputweaver
