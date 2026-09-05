#include "program_runtime_internal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <semaphore>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace inputweaver {

void ProgramRuntime::Impl::DrainWork(State& state) noexcept
{
    WorkItem item{};
    while (state.dispatch.workQueue.TryPop(item)) {
        const std::uint64_t generation = state.generation.load(
            std::memory_order_acquire);
        const bool accepting = state.accepting.load(std::memory_order_acquire);
        const bool targetEligible = state.targetEligible.load(
            std::memory_order_acquire);
        if (item.kind == WorkKind::TaskStart) {
            if (item.taskSlot >= state.scheduler.taskCount) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    {},
                    item.taskSlot,
                    kInvalidProgramIndex,
                    7U);
                continue;
            }
            TaskInstance& task = state.scheduler.tasks[item.taskSlot];
            if (task.status.load(std::memory_order_acquire) != TaskStatus::Reserved) {
                continue;
            }
            if (item.generation != generation
                || !accepting
                || !targetEligible) {
                task.status.store(TaskStatus::Free, std::memory_order_release);
                state.metrics.cancelledTasks.fetch_add(
                    1U,
                    std::memory_order_relaxed);
                continue;
            }
            const std::uint64_t marker = BeginDebugExecution(state, item, task.completed);
            if (marker != 0U) {
                task.debugCaptureEpoch = item.debugCaptureEpoch;
                task.debugExecutionMarker = marker;
            }
            task.readyOrder = state.scheduler.nextReadyOrder++;
            task.status.store(TaskStatus::Ready, std::memory_order_release);
            state.metrics.startedTasks.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        if (item.generation == generation
            && accepting
            && (targetEligible || item.kind == WorkKind::MappingRelease)) {
            ProcessMappingWork(state, item);
        }
    }
}

void ProgramRuntime::Impl::PromoteTimed(State& state) noexcept
{
    const std::int64_t now = clock.NowNanoseconds();
    while (true) {
        std::uint32_t selected = kInvalidTaskSlot;
        std::int64_t selectedDeadline = (std::numeric_limits<std::int64_t>::max)();
        std::uint64_t selectedOrder = (std::numeric_limits<std::uint64_t>::max)();
        for (std::size_t index = 0U;
             index < state.scheduler.taskCount;
             ++index) {
            const TaskInstance& task = state.scheduler.tasks[index];
            if (task.status.load(std::memory_order_acquire) != TaskStatus::Timed
                || task.deadlineNanoseconds > now) {
                continue;
            }
            if (task.deadlineNanoseconds < selectedDeadline
                || (task.deadlineNanoseconds == selectedDeadline
                    && task.timedOrder < selectedOrder)) {
                selected = static_cast<std::uint32_t>(index);
                selectedDeadline = task.deadlineNanoseconds;
                selectedOrder = task.timedOrder;
            }
        }
        if (selected == kInvalidTaskSlot) {
            return;
        }
        TaskInstance& task = state.scheduler.tasks[selected];
        task.readyOrder = state.scheduler.nextReadyOrder++;
        task.status.store(TaskStatus::Ready, std::memory_order_release);
    }
}

std::uint32_t ProgramRuntime::Impl::SelectReadyTask(State& state) noexcept
{
    std::uint32_t selected = kInvalidTaskSlot;
    std::uint64_t selectedOrder = (std::numeric_limits<std::uint64_t>::max)();
    for (std::size_t index = 0U;
         index < state.scheduler.taskCount;
         ++index) {
        const TaskInstance& task = state.scheduler.tasks[index];
        if (task.status.load(std::memory_order_acquire) == TaskStatus::Ready
            && task.readyOrder < selectedOrder) {
            selected = static_cast<std::uint32_t>(index);
            selectedOrder = task.readyOrder;
        }
    }
    return selected;
}

bool ProgramRuntime::Impl::ScheduleTimed(
    State& state,
    TaskInstance& task,
    DurationValue duration) noexcept
{
    if (duration.nanoseconds == 0) {
        return false;
    }
    const std::int64_t now = (std::max)(clock.NowNanoseconds(), std::int64_t{0});
    task.deadlineNanoseconds = AddDeadline(now, duration);
    if (task.deadlineNanoseconds > now) {
        task.instructionsWithoutSuspension = 0U;
        task.outputsWithoutSuspension = 0U;
        state.scheduler.positiveSuspensions.fetch_add(
            1U,
            std::memory_order_relaxed);
    }
    task.timedOrder = state.scheduler.nextTimedOrder++;
    task.status.store(TaskStatus::Timed, std::memory_order_release);
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::OwnershipChange,
        {},
        task.action.value,
        task.position,
        task.deadlineNanoseconds,
        2U);
    return true;
}

RuntimeEvaluationResult ProgramRuntime::Impl::EvaluateTaskExpression(
    State& state,
    ExpressionId expression, const TaskInstance& task) noexcept
{
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::unique_lock variableLock(state.mutableState.variableMutex);
    RefreshMouse(state);
    return Evaluate(state, expression, state.scheduler.expressionScratch, task.completed);
}

SourceSpan ProgramRuntime::Impl::ActionSource(
    const State& state,
    const ActionProgramDescriptor& descriptor,
    std::uint32_t position) const noexcept
{
    const std::uint32_t globalPosition = descriptor.code.begin + position;
    const auto spans = state.program->DebugInfo().actionInstructionSpans;
    return globalPosition < spans.size()
        ? spans[globalPosition]
        : descriptor.source;
}

SourceSpan ProgramRuntime::Impl::ExpressionSource(
    const State& state,
    ExpressionId expression,
    std::uint32_t position,
    std::uint32_t& globalPosition) const noexcept
{
    const auto expressions = state.program->Expressions();
    if (!expression.IsValid() || expression.value >= expressions.size()) {
        globalPosition = position;
        return {};
    }
    const ExpressionDescriptor& descriptor = expressions[expression.value];
    globalPosition = descriptor.code.begin + position;
    const auto spans = state.program->DebugInfo().expressionInstructionSpans;
    return position < descriptor.code.count && globalPosition < spans.size()
        ? spans[globalPosition]
        : descriptor.source;
}

void ProgramRuntime::Impl::ReportExpressionFault(
    State& state,
    RuntimeDiagnosticKind kind,
    ExpressionId expression,
    const RuntimeEvaluationResult& result,
    bool fatal) noexcept
{
    std::uint32_t position{};
    const SourceSpan source = ExpressionSource(
        state,
        expression,
        result.instructionPosition,
        position);
    const std::uint32_t detail = static_cast<std::uint32_t>(result.fault);
    if (fatal && result.fault != RuntimeEvaluationFault::MissingCompletedMeter) {
        RequestFatal(state, kind, source, expression.value, position, detail);
    } else {
        PublishDiagnostic(state, kind, source, expression.value, position, 0, detail);
    }
}

bool ProgramRuntime::Impl::ExecuteSet(
    State& state,
    TaskInstance& task,
    const ActionInstruction& instruction,
    std::uint32_t instructionPosition) noexcept
{
    const auto refs = state.program->ValueRefs();
    if (instruction.operand0 >= refs.size()) {
        return false;
    }
    const ValueRef& target = refs[instruction.operand0];
    if (!IsUserDomain(target.domain)) {
        return false;
    }
    MutationTransaction transaction(*this, state, task);
    if (!transaction.current) {
        return false;
    }
    const ExpressionId expression{instruction.operand1};
    const RuntimeEvaluationResult result = Evaluate(
        state,
        expression,
        state.scheduler.expressionScratch, task.completed);
    if (!result.Succeeded()) {
        ReportExpressionFault(
            state,
            RuntimeDiagnosticKind::TaskExpressionFault,
            expression,
            result,
            true);
        return false;
    }
    MutationPublication publication{};
    publication.scalar.reference = ValueRefId{instruction.operand0};
    publication.scalar.type = target.type;
    publication.hasScalar = true;
    if (target.domain == ValueDomain::UserState
        && target.index < state.mutableState.userStates.size()
        && result.value.type == ExpressionType::State) {
        state.mutableState.userStates[target.index] = result.value.stateValue;
        publication.scalar.stateValue = result.value.stateValue != 0U;
    } else if (target.domain == ValueDomain::UserNumber
        && target.index < state.mutableState.userNumbers.size()
        && result.value.type == ExpressionType::Number) {
        state.mutableState.userNumbers[target.index] = result.value.numberValue;
        publication.scalar.numberValue = result.value.numberValue;
    } else if (target.domain == ValueDomain::UserDuration
        && target.index < state.mutableState.userDurations.size()
        && result.value.type == ExpressionType::Duration) {
        state.mutableState.userDurations[target.index] = result.value.durationValue;
        publication.scalar.durationValue = result.value.durationValue;
    } else {
        return false;
    }
    (void)instructionPosition;
    return CompleteMutation(transaction, publication);
}

bool ProgramRuntime::Impl::ExecuteToggle(
    State& state,
    TaskInstance& task,
    const ActionInstruction& instruction,
    std::uint32_t instructionPosition) noexcept
{
    const auto refs = state.program->ValueRefs();
    if (instruction.operand0 >= refs.size()) {
        return false;
    }
    const ValueRef& target = refs[instruction.operand0];
    if (target.domain != ValueDomain::UserState
        || target.index >= state.mutableState.userStates.size()) {
        return false;
    }
    MutationTransaction transaction(*this, state, task);
    if (!transaction.current) {
        return false;
    }
    std::uint8_t& value = state.mutableState.userStates[target.index];
    value = value == 0U ? 1U : 0U;
    (void)instructionPosition;
    MutationPublication publication{};
    publication.scalar = {
        ValueRefId{instruction.operand0},
        ValueType::State,
        value != 0U};
    publication.hasScalar = true;
    return CompleteMutation(transaction, publication);
}

bool ProgramRuntime::Impl::ExecuteArrayAction(
    State& state,
    TaskInstance& task,
    const ActionInstruction& instruction,
    std::uint32_t instructionPosition,
    SourceSpan source) noexcept
{
    const ArrayId arrayId{instruction.operand0};
    const auto actionFault = [&](std::uint32_t detail) noexcept {
        PublishDiagnostic(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            source,
            instruction.operand0,
            instructionPosition,
            0,
            detail);
    };
    if (instruction.operand0 >= state.mutableState.arrays.size()) {
        actionFault(20U);
        return false;
    }
    RuntimeArrayStorage& array = state.mutableState.arrays[instruction.operand0];
    const auto expressionFault = [&](ExpressionId expression,
                                     const RuntimeEvaluationResult& result) noexcept {
        ReportExpressionFault(
            state,
            RuntimeDiagnosticKind::TaskExpressionFault,
            expression,
            result,
            false);
    };
    const auto completeArray = [&](MutationTransaction& transaction,
                                   MutationPublication publication) noexcept {
        publication.array = BuildArraySnapshot(arrayId, array);
        publication.hasArray = true;
        return CompleteMutation(transaction, publication);
    };

    if (instruction.opcode == ActionOpcode::SetArrayElement
        || instruction.opcode == ActionOpcode::ToggleArrayElement) {
        MutationTransaction transaction(*this, state, task);
        if (!transaction.current) {
            return false;
        }
        const ExpressionId indexExpression{instruction.operand1};
        const RuntimeEvaluationResult indexResult = Evaluate(
            state,
            indexExpression,
            state.scheduler.expressionScratch, task.completed);
        if (!indexResult.Succeeded()
            || indexResult.value.type != ExpressionType::Number) {
            expressionFault(indexExpression, indexResult);
            return false;
        }
        const NormalizedArrayIndex index = NormalizeArrayIndex(
            indexResult.value.numberValue,
            array.Size());
        if (!index.Valid()) {
            actionFault(index.status == ArrayIndexStatus::OutOfBounds
                ? 22U
                : 21U);
            return false;
        }
        if (instruction.opcode == ActionOpcode::SetArrayElement) {
            const ExpressionId valueExpression{instruction.operand2};
            const RuntimeEvaluationResult valueResult = Evaluate(
                state,
                valueExpression,
                state.scheduler.expressionScratch, task.completed);
            if (!valueResult.Succeeded()) {
                expressionFault(valueExpression, valueResult);
                return false;
            }
            if (!array.Set(index.value, valueResult.value)) {
                actionFault(23U);
                return false;
            }
        } else if (!array.Toggle(index.value)) {
            actionFault(23U);
            return false;
        }
        return completeArray(transaction, {});
    }

    if (instruction.opcode == ActionOpcode::AppendArrayElement) {
        MutationTransaction planning(*this, state, task);
        if (!planning.current) {
            return false;
        }
        const ExpressionId valueExpression{instruction.operand1};
        const RuntimeEvaluationResult valueResult = Evaluate(
            state,
            valueExpression,
            state.scheduler.expressionScratch, task.completed);
        if (!valueResult.Succeeded()) {
            expressionFault(valueExpression, valueResult);
            return false;
        }
        const std::optional<ArrayAppendPlan> plan = array.PlanAppend();
        const std::uint64_t oldBytes = array.AllocatedBytes();
        const std::uint64_t currentBytes = state.metrics.currentArrayBytes.load(
            std::memory_order_relaxed);
        const auto growthFits = [&](std::uint64_t total) noexcept {
            return plan && plan->projectedBytes >= oldBytes
                && total <= capacities.maximumArrayBytes
                && plan->projectedBytes - oldBytes
                    <= capacities.maximumArrayBytes - total;
        };
        if (!growthFits(currentBytes)) {
            state.metrics.rejectedArrayGrowth.fetch_add(
                1U,
                std::memory_order_relaxed);
            actionFault(plan ? 25U : 24U);
            return false;
        }
        planning.Unlock();
        std::optional<RuntimeArrayStorage::PreparedAppend> prepared =
            array.PrepareAppend(*plan);
        if (!prepared) {
            state.metrics.rejectedArrayGrowth.fetch_add(1U, std::memory_order_relaxed);
            actionFault(24U);
            return false;
        }
        MutationTransaction commit(*this, state, task);
        const std::uint64_t committedBytes = state.metrics.currentArrayBytes.load(
            std::memory_order_relaxed);
        if (!commit.current || array.PlanAppend() != plan) {
            return false;
        }
        if (!growthFits(committedBytes)) {
            state.metrics.rejectedArrayGrowth.fetch_add(
                1U,
                std::memory_order_relaxed);
            actionFault(25U);
            return false;
        }
        if (!array.Append(valueResult.value, *plan, std::move(*prepared))) {
            actionFault(23U);
            return false;
        }
        const std::uint64_t newTotal = committedBytes
            + plan->projectedBytes - oldBytes;
        state.metrics.currentArrayBytes.store(
            newTotal,
            std::memory_order_relaxed);
        state.metrics.peakArrayBytes.store((std::max)(
            newTotal,
            state.metrics.peakArrayBytes.load(std::memory_order_relaxed)),
            std::memory_order_relaxed);
        return completeArray(commit, {});
    }

    if (instruction.opcode == ActionOpcode::PopArrayElement) {
        const auto refs = state.program->ValueRefs();
        if (instruction.operand1 >= refs.size()) {
            actionFault(20U);
            return false;
        }
        const ValueRef& target = refs[instruction.operand1];
        const bool targetValid =
            (array.ElementType() == ArrayElementType::State
                && target.domain == ValueDomain::UserState
                && target.type == ValueType::State
                && target.index < state.mutableState.userStates.size())
            || (array.ElementType() == ArrayElementType::Number
                && target.domain == ValueDomain::UserNumber
                && target.type == ValueType::Number
                && target.index < state.mutableState.userNumbers.size());
        if (!targetValid) {
            actionFault(23U);
            return false;
        }
        MutationTransaction transaction(*this, state, task);
        if (!transaction.current) {
            return false;
        }
        RuntimeValue value{};
        if (!array.Pop(value)) {
            actionFault(26U);
            return false;
        }
        if (target.domain == ValueDomain::UserState) {
            state.mutableState.userStates[target.index] = value.stateValue;
        } else {
            state.mutableState.userNumbers[target.index] = value.numberValue;
        }
        MutationPublication publication{};
        publication.scalar.reference = ValueRefId{instruction.operand1};
        publication.scalar.type = target.type;
        publication.scalar.stateValue = value.stateValue != 0U;
        publication.scalar.numberValue = value.numberValue;
        publication.hasScalar = true;
        return completeArray(transaction, publication);
    }

    if (instruction.opcode == ActionOpcode::ClearArray) {
        MutationTransaction transaction(*this, state, task);
        if (!transaction.current) {
            return false;
        }
        array.Clear();
        return completeArray(transaction, {});
    }

    actionFault(20U);
    return false;
}

bool ProgramRuntime::Impl::RunTaskSlice(
    State& state,
    std::uint32_t slot) noexcept
{
    TaskInstance& task = state.scheduler.tasks[slot];
    TaskStatus expected = TaskStatus::Ready;
    if (!task.status.compare_exchange_strong(
            expected,
            TaskStatus::Running,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return false;
    }
    const std::uint64_t generation = state.generation.load(std::memory_order_acquire);
    if (task.generation != generation) {
        FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
        return true;
    }
    const auto descriptors = state.program->ActionPrograms();
    if (!task.action.IsValid() || task.action.value >= descriptors.size()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            task.action.value,
            task.position,
            8U);
        FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
        return true;
    }
    const ActionProgramDescriptor& descriptor = descriptors[task.action.value];
    const auto code = state.program->ActionCode().subspan(
        descriptor.code.begin,
        descriptor.code.count);

    if (task.resumeKind == TaskResumeKind::TapRelease) {
        if (!ReleaseTaskControl(state, task, task.pendingTap)) {
            FinishTask(
                state,
                slot,
                task.generation != state.generation.load(std::memory_order_acquire),
                ClassifyTaskOperationFailure(state, task));
            return true;
        }
        task.resumeKind = TaskResumeKind::None;
        task.pendingTap = {};
        ++task.position;
    }

    while (task.position < code.size()) {
        if (task.generation != state.generation.load(std::memory_order_acquire)) {
            FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
            return true;
        }
        const std::uint32_t position = task.position;
        if (task.instructionsWithoutSuspension
            >= capacities.maximumTaskInstructionsWithoutSuspension) {
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::TaskBudgetExceeded,
                ActionSource(state, descriptor, position),
                task.action.value,
                position,
                0,
                1U);
            task.safetyCancelled = true;
            FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
            return true;
        }
        ++task.instructionsWithoutSuspension;
        const ActionInstruction& instruction = code[position];
        const SourceSpan source = ActionSource(state, descriptor, position);
        switch (instruction.opcode) {
        case ActionOpcode::Press:
            if (!AcquireTaskControl(state, task, ControlRefId{instruction.operand0})) {
                FinishTask(
                    state,
                    slot,
                    task.generation
                        != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            ++task.position;
            break;
        case ActionOpcode::Release:
            if (!ReleaseTaskControl(state, task, ControlRefId{instruction.operand0})) {
                FinishTask(
                    state,
                    slot,
                    task.generation
                        != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            ++task.position;
            break;
        case ActionOpcode::Tap:
            if (!AcquireTaskControl(state, task, ControlRefId{instruction.operand0})) {
                FinishTask(
                    state,
                    slot,
                    task.generation
                        != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            if (!ScheduleTimed(state, task, state.program->Settings().tapDuration)) {
                if (!ReleaseTaskControl(
                        state,
                        task,
                        ControlRefId{instruction.operand0})) {
                    FinishTask(
                        state,
                        slot,
                        task.generation
                            != state.generation.load(std::memory_order_acquire),
                        ClassifyTaskOperationFailure(state, task));
                    return true;
                }
                ++task.position;
                break;
            }
            task.resumeKind = TaskResumeKind::TapRelease;
            task.pendingTap = ControlRefId{instruction.operand0};
            return true;
        case ActionOpcode::Wait: {
            const ExpressionId expression{instruction.operand0};
            const RuntimeEvaluationResult result = EvaluateTaskExpression(
                state,
                expression, task);
            if (!result.Succeeded()
                || result.value.type != ExpressionType::Duration) {
                ReportExpressionFault(
                    state,
                    RuntimeDiagnosticKind::TaskExpressionFault,
                    expression,
                    result,
                    true);
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            if (ScheduleTimed(state, task, result.value.durationValue)) {
                return true;
            }
            break;
        }
        case ActionOpcode::Gap:
            ++task.position;
            if (ScheduleTimed(state, task, state.program->Settings().actionGap)) {
                return true;
            }
            break;
        case ActionOpcode::Set:
        case ActionOpcode::Toggle:
        case ActionOpcode::SetArrayElement:
        case ActionOpcode::ToggleArrayElement:
        case ActionOpcode::AppendArrayElement:
        case ActionOpcode::PopArrayElement:
        case ActionOpcode::ClearArray: {
            const bool scalar = instruction.opcode == ActionOpcode::Set
                || instruction.opcode == ActionOpcode::Toggle;
            const bool succeeded = instruction.opcode == ActionOpcode::Set
                ? ExecuteSet(state, task, instruction, position)
                : instruction.opcode == ActionOpcode::Toggle
                    ? ExecuteToggle(state, task, instruction, position)
                    : ExecuteArrayAction(
                    state,
                    task,
                    instruction,
                    position,
                    source);
            if (!succeeded) {
                const bool cancelled = task.generation
                    != state.generation.load(std::memory_order_acquire);
                if (scalar && !cancelled
                    && !state.fatalShutdownRequested.load(std::memory_order_acquire)) {
                    PublishDiagnostic(
                        state,
                        RuntimeDiagnosticKind::TaskActionFault,
                        source,
                        task.action.value,
                        position,
                        0,
                        instruction.opcode == ActionOpcode::Set ? 9U : 10U);
                }
                FinishTask(
                    state,
                    slot,
                    cancelled,
                    cancelled
                        ? RuntimeExecutionResult::Cancelled
                        : RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            break;
        }
        case ActionOpcode::Exec: {
            if (task.generation != state.generation.load(std::memory_order_acquire)) {
                FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
                return true;
            }
            TaskCancellationContext cancellationContext{
                &state.generation,
                task.generation};
            const RuntimeLaunchOutcome outcome = processLauncher.Launch(
                state.program->Strings()[instruction.operand0],
                RuntimeCancellationProbe{
                    &cancellationContext,
                    &TaskCancelled});
            if (!outcome.Succeeded()) {
                PublishDiagnostic(
                    state,
                    RuntimeDiagnosticKind::LaunchFailure,
                    source,
                    task.action.value,
                    position,
                    0,
                    static_cast<std::uint32_t>(outcome.result),
                    outcome.platformError);
                FinishTask(
                    state,
                    slot,
                    outcome.result == RuntimeLaunchResult::Cancelled,
                    outcome.result == RuntimeLaunchResult::Cancelled
                        ? RuntimeExecutionResult::Cancelled
                        : RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            break;
        }
        case ActionOpcode::Jump:
            task.position = instruction.operand0;
            break;
        case ActionOpcode::JumpIfFalse: {
            const ExpressionId expression{instruction.operand0};
            const RuntimeEvaluationResult result = EvaluateTaskExpression(
                state,
                expression, task);
            if (!result.Succeeded()
                || result.value.type != ExpressionType::Boolean) {
                ReportExpressionFault(
                    state,
                    RuntimeDiagnosticKind::TaskExpressionFault,
                    expression,
                    result,
                    true);
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            task.position = result.value.booleanValue
                ? position + 1U
                : instruction.operand1;
            break;
        }
        case ActionOpcode::RepeatInit: {
            const ExpressionId expression{instruction.operand1};
            const RuntimeEvaluationResult result = EvaluateTaskExpression(
                state,
                expression, task);
            if (!result.Succeeded()
                || result.value.type != ExpressionType::Number) {
                ReportExpressionFault(
                    state,
                    RuntimeDiagnosticKind::TaskExpressionFault,
                    expression,
                    result,
                    true);
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            task.repeatFrames[instruction.operand0] = {
                0U,
                (std::max)(0.0, std::floor(result.value.numberValue))};
            ++task.position;
            break;
        }
        case ActionOpcode::RepeatCheck: {
            const RepeatFrame& frame = task.repeatFrames[instruction.operand0];
            task.position = static_cast<double>(frame.index) < frame.limit
                ? position + 1U
                : instruction.operand1;
            break;
        }
        case ActionOpcode::RepeatNext: {
            RepeatFrame& frame = task.repeatFrames[instruction.operand0];
            if (frame.index == (std::numeric_limits<std::uint64_t>::max)()) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    source,
                    task.action.value,
                    position,
                    11U);
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            ++frame.index;
            ++task.position;
            break;
        }
        case ActionOpcode::Yield:
            ++task.position;
            task.readyOrder = state.scheduler.nextReadyOrder++;
            task.status.store(TaskStatus::Ready, std::memory_order_release);
            return true;
        case ActionOpcode::End:
            FinishTask(state, slot, false, RuntimeExecutionResult::Completed);
            return true;
        case ActionOpcode::RestartMeter: {
            MutationTransaction transaction(*this, state, task);
            if (!transaction.current) {
                transaction.Unlock();
                FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
                return true;
            }
            state.mutableState.mouse->Restart(MeterId{instruction.operand0});
            ++task.position;
            break;
        }
        case ActionOpcode::Pointer:
            if (!ExecutePointer(state, task, instruction)) {
                FinishTask(state, slot, task.generation != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            ++task.position;
            break;
        }
    }

    RequestFatal(
        state,
        RuntimeDiagnosticKind::TaskActionFault,
        descriptor.source,
        task.action.value,
        task.position,
        12U);
    FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
    return true;
}

RuntimePumpResult ProgramRuntime::Impl::Pump(std::size_t maximumSlices) noexcept
{
    State* const state = active.get();
    if (state == nullptr) {
        return {};
    }
    const std::lock_guard pumpLock(state->scheduler.pumpMutex);

    CleanupStale(*state);
    DrainWork(*state);
    CleanupStale(*state);
    PromoteTimed(*state);

    std::size_t slices = 0U;
    while (slices < maximumSlices) {
        const std::uint32_t slot = SelectReadyTask(*state);
        if (slot == kInvalidTaskSlot) {
            break;
        }
        (void)RunTaskSlice(*state, slot);
        ++slices;
        CleanupStale(*state);
        DrainWork(*state);
        CleanupStale(*state);
        PromoteTimed(*state);
    }

    bool ready = false;
    bool timed = false;
    for (std::size_t index = 0U;
         index < state->scheduler.taskCount;
         ++index) {
        const TaskStatus status = state->scheduler.tasks[index].status.load(
            std::memory_order_acquire);
        ready = ready || status == TaskStatus::Ready;
        timed = timed || status == TaskStatus::Timed;
    }
    return {slices, ready || !state->dispatch.workQueue.Empty(), timed};
}

std::int64_t ProgramRuntime::Impl::NextDeadline(const State& state) const noexcept
{
    std::int64_t deadline = (std::numeric_limits<std::int64_t>::max)();
    for (std::size_t index = 0U;
         index < state.scheduler.taskCount;
         ++index) {
        const TaskInstance& task = state.scheduler.tasks[index];
        if (task.status.load(std::memory_order_acquire) == TaskStatus::Timed) {
            deadline = (std::min)(deadline, task.deadlineNanoseconds);
        }
    }
    return deadline;
}

void ProgramRuntime::Impl::Wake() noexcept
{
    wakeSemaphore.release();
}

bool ProgramRuntime::Impl::StartTaskThread()
{
    if (active == nullptr) {
        return false;
    }
    bool expected = false;
    if (!taskThreadRunning.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return true;
    }
    taskThreadStop.store(false, std::memory_order_release);
    try {
        taskThread = std::thread(&Impl::TaskThreadMain, this);
    } catch (...) {
        taskThreadRunning.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void ProgramRuntime::Impl::StopTaskThread() noexcept
{
    if (!taskThreadRunning.load(std::memory_order_acquire)) {
        return;
    }
    taskThreadStop.store(true, std::memory_order_release);
    Wake();
    if (taskThread.joinable()) {
        taskThread.join();
    }
    taskThreadRunning.store(false, std::memory_order_release);
}

void ProgramRuntime::Impl::TaskThreadMain() noexcept
{
    std::uint32_t continuouslyReadyQuanta = 0U;
    while (!taskThreadStop.load(std::memory_order_acquire)) {
        const State* const pumpState = active.get();
        const std::uint64_t suspensionsBefore = pumpState == nullptr
            ? 0U
            : pumpState->scheduler.positiveSuspensions.load(
                  std::memory_order_relaxed);
        const RuntimePumpResult result = Pump(1024U);
        const std::uint64_t suspensionsAfter = pumpState == nullptr
            ? 0U
            : pumpState->scheduler.positiveSuspensions.load(
                  std::memory_order_relaxed);
        if (result.readyWorkRemaining) {
            if (suspensionsAfter != suspensionsBefore) {
                continuouslyReadyQuanta = 0U;
                continue;
            }
            ++continuouslyReadyQuanta;
            if (continuouslyReadyQuanta
                < capacities.maximumContinuouslyReadyQuanta) {
                continue;
            }
            continuouslyReadyQuanta = 0U;
            State* const state = active.get();
            if (state != nullptr) {
                state->metrics.schedulerBackoffs.fetch_add(
                    1U,
                    std::memory_order_relaxed);
            }
            std::int64_t backoffNanoseconds =
                capacities.continuouslyReadyBackoffNanoseconds;
            if (state != nullptr) {
                const std::int64_t deadline = NextDeadline(*state);
                if (deadline != (std::numeric_limits<std::int64_t>::max)()) {
                    const std::int64_t now = (std::max)(
                        clock.NowNanoseconds(),
                        std::int64_t{0});
                    const std::int64_t remaining = deadline > now
                        ? deadline - now
                        : 0;
                    backoffNanoseconds = (std::min)(
                        backoffNanoseconds,
                        remaining);
                }
            }
            (void)wakeSemaphore.try_acquire_for(
                std::chrono::nanoseconds(backoffNanoseconds));
            while (wakeSemaphore.try_acquire()) {
            }
            continue;
        }
        continuouslyReadyQuanta = 0U;
        const State* const state = active.get();
        const std::int64_t deadline = state == nullptr
            ? (std::numeric_limits<std::int64_t>::max)()
            : NextDeadline(*state);
        if (deadline == (std::numeric_limits<std::int64_t>::max)()) {
            wakeSemaphore.acquire();
        } else {
            const std::int64_t now = clock.NowNanoseconds();
            const std::int64_t remaining = (std::max)(deadline - now, std::int64_t{0});
            (void)wakeSemaphore.try_acquire_for(
                std::chrono::nanoseconds(remaining));
        }
        while (wakeSemaphore.try_acquire()) {
        }
    }
}


} // namespace inputweaver
