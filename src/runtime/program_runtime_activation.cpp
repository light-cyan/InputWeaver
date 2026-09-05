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

RuntimeActivationError ProgramRuntime::Impl::ValidateCapacities(
    const CompiledProgram& program) const noexcept
{
    const ProgramRequirements& required = program.Requirements();
    const auto capacityError = [](RuntimeActivationErrorCode code,
                                  std::uint64_t requirement,
                                  std::uint64_t available,
                                  RuntimeActivationSubject subject =
                                      RuntimeActivationSubject::None) noexcept {
        return RuntimeActivationError{
            code,
            static_cast<std::uint32_t>(subject),
            requirement,
            available};
    };
    if (program.Controls().size() > capacities.maximumControls) {
        return capacityError(
            RuntimeActivationErrorCode::ControlCapacity,
            program.Controls().size(),
            capacities.maximumControls);
    }
    if (required.stateSlotCount > capacities.maximumStateSlots) {
        return capacityError(
            RuntimeActivationErrorCode::ValueCapacity,
            required.stateSlotCount,
            capacities.maximumStateSlots,
            RuntimeActivationSubject::StateSlots);
    }
    if (required.numberSlotCount > capacities.maximumNumberSlots) {
        return capacityError(
            RuntimeActivationErrorCode::ValueCapacity,
            required.numberSlotCount,
            capacities.maximumNumberSlots,
            RuntimeActivationSubject::NumberSlots);
    }
    if (required.durationSlotCount > capacities.maximumDurationSlots) {
        return capacityError(
            RuntimeActivationErrorCode::ValueCapacity,
            required.durationSlotCount,
            capacities.maximumDurationSlots,
            RuntimeActivationSubject::DurationSlots);
    }
    if (required.arrayCount > capacities.maximumArrayCount) {
        return capacityError(
            RuntimeActivationErrorCode::ArrayCountCapacity,
            required.arrayCount,
            capacities.maximumArrayCount,
            RuntimeActivationSubject::ArrayCount);
    }
    std::uint64_t initialArrayBytes = 0U;
    for (const ArrayDescriptor& descriptor : program.Arrays()) {
        const std::optional<std::size_t> bytes =
            descriptor.elementType == ArrayElementType::State
                ? RuntimeArrayStorage::StateArray::AllocationBytesForSize(
                    descriptor.initialValues.count)
                : RuntimeArrayStorage::NumberArray::AllocationBytesForSize(
                    descriptor.initialValues.count);
        if (!bytes
            || *bytes > (std::numeric_limits<std::uint64_t>::max)()
                - initialArrayBytes) {
            return capacityError(
                RuntimeActivationErrorCode::ArrayByteCapacity,
                (std::numeric_limits<std::uint64_t>::max)(),
                capacities.maximumArrayBytes,
                RuntimeActivationSubject::ArrayBytes);
        }
        initialArrayBytes += *bytes;
    }
    if (initialArrayBytes > capacities.maximumArrayBytes) {
        return capacityError(
            RuntimeActivationErrorCode::ArrayByteCapacity,
            initialArrayBytes,
            capacities.maximumArrayBytes,
            RuntimeActivationSubject::ArrayBytes);
    }
    if (required.mappingSlotCount > capacities.maximumMappingSlots) {
        return capacityError(
            RuntimeActivationErrorCode::MappingCapacity,
            required.mappingSlotCount,
            capacities.maximumMappingSlots);
    }
    if (required.maximumExitRulesPerEvent
        > capacities.maximumExitRulesPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::ExitRuleCapacity,
            required.maximumExitRulesPerEvent,
            capacities.maximumExitRulesPerEvent);
    }
    if (required.maximumPauseRulesPerEvent
        > capacities.maximumPauseRulesPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::PauseRuleCapacity,
            required.maximumPauseRulesPerEvent,
            capacities.maximumPauseRulesPerEvent);
    }
    if (required.maximumRulesPerEvent > capacities.maximumRulesPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::RuleCapacity,
            required.maximumRulesPerEvent,
            capacities.maximumRulesPerEvent);
    }
    if (required.maximumPredicateStepsPerEvent
        > capacities.maximumPredicateStepsPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::PredicateStepCapacity,
            required.maximumPredicateStepsPerEvent,
            capacities.maximumPredicateStepsPerEvent);
    }
    if (required.maximumMappingOperationsPerEvent
        > capacities.maximumMappingOperationsPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::MappingOperationCapacity,
            required.maximumMappingOperationsPerEvent,
            capacities.maximumMappingOperationsPerEvent);
    }
    if (required.maximumExpressionStackDepth
        > capacities.maximumExpressionStackDepth) {
        return capacityError(
            RuntimeActivationErrorCode::ExpressionStackCapacity,
            required.maximumExpressionStackDepth,
            capacities.maximumExpressionStackDepth);
    }
    if (required.maximumRepeatFramesPerTask
        > capacities.maximumRepeatFramesPerTask) {
        return capacityError(
            RuntimeActivationErrorCode::RepeatFrameCapacity,
            required.maximumRepeatFramesPerTask,
            capacities.maximumRepeatFramesPerTask);
    }
    if (required.maximumOwnedControlsPerTask
        > capacities.maximumOwnedControlsPerTask) {
        return capacityError(
            RuntimeActivationErrorCode::OwnershipCapacity,
            required.maximumOwnedControlsPerTask,
            capacities.maximumOwnedControlsPerTask);
    }
    if (capacities.maximumTaskInstructionsWithoutSuspension == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::TaskInstructionCapacity,
            1U,
            0U);
    }
    if (capacities.maximumTaskOutputsWithoutSuspension == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::TaskOutputCapacity,
            1U,
            0U);
    }
    if (capacities.maximumContinuouslyReadyQuanta == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidSchedulerConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::MaximumContinuouslyReadyQuanta);
    }
    if (capacities.continuouslyReadyBackoffNanoseconds <= 0) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidSchedulerConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::ContinuouslyReadyBackoffNanoseconds);
    }
    if (capacities.maximumOutputTransitionsPerInterval == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidOutputRateConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::MaximumOutputTransitionsPerInterval);
    }
    if (capacities.outputRateIntervalNanoseconds <= 0) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidOutputRateConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::OutputRateIntervalNanoseconds);
    }
    if (required.maximumTasksPerEvent > capacities.taskSlotCount) {
        return capacityError(
            RuntimeActivationErrorCode::TaskCapacity,
            required.maximumTasksPerEvent,
            capacities.taskSlotCount);
    }
    if (required.maximumTransactionItemsPerEvent
        > capacities.transactionQueueItemCount
        || capacities.transactionQueueItemCount == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::TransactionCapacity,
            required.maximumTransactionItemsPerEvent,
            capacities.transactionQueueItemCount);
    }
    if (capacities.diagnosticRecordCount == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::DiagnosticCapacity,
            1U,
            0U);
    }
    if (required.requiresProcessLaunch
        && (!capacities.permitProcessLaunch || !processLauncher.Permitted())) {
        return {RuntimeActivationErrorCode::ProcessLaunchDenied, 0U, 1U, 0U};
    }
    return {};
}

RuntimeActivationResult ProgramRuntime::Impl::Activate(
    std::shared_ptr<const CompiledProgram> program,
    TargetSelectorKind targetKindOverride)
{
    if (!program) {
        return {false, {RuntimeActivationErrorCode::MissingProgram}};
    }
    ScreenPoint pointer{};
    if ((program->Requirements().requiresMouseObservation && !routePort.QueryPointerPosition(pointer))
        || (program->Requirements().requiresPointerOutput && !outputPort.SupportsPointerOutput())) {
        return {false, {RuntimeActivationErrorCode::MissingMouseCapability}};
    }
    const RuntimeActivationError capacityError = ValidateCapacities(*program);
    if (capacityError.code != RuntimeActivationErrorCode::None) {
        return {false, capacityError};
    }
    const std::uint64_t randomSeed = program->Settings().randomSeed;

    std::unique_ptr<State> candidate;
    try {
        candidate = std::make_unique<State>(
            std::move(program),
            randomSeed,
            nextProgramSerial,
            observableGeneration,
            capacities);
    } catch (const std::bad_alloc&) {
        return {false, {RuntimeActivationErrorCode::AllocationFailure}};
    }

    const ProgramSettings& settings = candidate->program->Settings();
    candidate->targetKind = targetKindOverride == TargetSelectorKind::Unspecified
        ? settings.target.kind
        : targetKindOverride;
    if (!routePort.ValidateTarget(candidate->targetKind)) {
        return {false, {RuntimeActivationErrorCode::InvalidTarget}};
    }
    candidate->targetEligible.store(
        routePort.TargetValid(candidate->targetKind),
        std::memory_order_release);

    controlPort.BeginActivation();
    for (const ControlRequirement& requirement
         : candidate->program->ControlRequirements()) {
        ActivatedControl activated{};
        const RuntimeControlBindResult result = controlPort.BindControl(
            requirement.control,
            candidate->program->Controls()[requirement.control.value],
            requirement.uses,
            activated);
        if (result != RuntimeControlBindResult::Bound) {
            controlPort.AbortActivation();
            return {
                false,
                {result == RuntimeControlBindResult::UnsupportedIdentity
                     ? RuntimeActivationErrorCode::UnsupportedControl
                     : RuntimeActivationErrorCode::MissingControlCapability,
                 requirement.control.value,
                 requirement.uses,
                 activated.capabilities}};
        }
        if ((activated.capabilities & requirement.uses) != requirement.uses) {
            controlPort.AbortActivation();
            return {
                false,
                {RuntimeActivationErrorCode::MissingControlCapability,
                 requirement.control.value,
                 requirement.uses,
                 activated.capabilities}};
        }
        candidate->activatedControls[requirement.control.value] = activated;
    }
    const bool restartWorker = taskThreadRunning.load(std::memory_order_acquire);
    StopTaskThread();
    if (active) {
        Invalidate(*active, RuntimeCancellationReason::Reload);
        CleanupAfterInvalidation(*active);
        if (active->output.HasOwnedOutputs()) {
            controlPort.AbortActivation();
            if (restartWorker) {
                (void)StartTaskThread();
            }
            return {false, {RuntimeActivationErrorCode::CleanupFailure}};
        }
    }
    controlPort.CommitActivation();
    if (active == nullptr
        && observableGeneration.load(std::memory_order_acquire) == 0U) {
        observableGeneration.store(1U, std::memory_order_release);
    }
    active = std::move(candidate);
    if (active->mutableState.mouse) {
        active->mutableState.mouse->Initialize(
            {static_cast<double>(pointer.x), static_cast<double>(pointer.y)}, clock.NowNanoseconds());
        active->mutableState.mouseGeneration = active->generation.load(std::memory_order_acquire);
    }
    ++nextProgramSerial;
    if (restartWorker) {
        (void)StartTaskThread();
    }
    return {true, {}};
}

void ProgramRuntime::Impl::Deactivate() noexcept
{
    StopTaskThread();
    if (!active) {
        return;
    }
    Invalidate(*active, RuntimeCancellationReason::Shutdown);
    CleanupAfterInvalidation(*active);
    observableGeneration.store(0U, std::memory_order_release);
    active.reset();
}

RuntimeEvaluationResult ProgramRuntime::Impl::Evaluate(
    State& state,
    ExpressionId expression,
    RuntimeExpressionScratch& scratch,
    std::span<const MouseCycle> completed) noexcept
{
    return EvaluateRuntimeExpression(
        *state.program,
        expression,
        state.mutableState.ExpressionState(*state.program, completed),
        scratch);
}

bool ProgramRuntime::Impl::EvaluatePredicate(
    State& state,
    ExpressionId expression,
    bool& matched) noexcept
{
    if (!expression.IsValid()) {
        matched = true;
        return true;
    }
    const RuntimeEvaluationResult result = Evaluate(
        state,
        expression,
        state.dispatch.expressionScratch, state.dispatch.completed);
    if (!result.Succeeded() || result.value.type != ExpressionType::Boolean) {
        ReportExpressionFault(
            state,
            RuntimeDiagnosticKind::PredicateFault,
            expression,
            result,
            true);
        matched = false;
        return result.fault == RuntimeEvaluationFault::MissingCompletedEvent;
    }
    matched = result.value.booleanValue;
    return true;
}

const ExitControlBucket* ProgramRuntime::Impl::FindExitBucket(
    const State& state,
    EventKey key) const noexcept
{
    const auto buckets = state.program->ExitControlBuckets();
    const auto found = std::lower_bound(
        buckets.begin(),
        buckets.end(),
        key,
        [](const ExitControlBucket& bucket, EventKey candidate) noexcept {
            return bucket.key < candidate;
        });
    return found != buckets.end() && found->key == key
        ? &*found
        : nullptr;
}

const PauseControlBucket* ProgramRuntime::Impl::FindPauseBucket(
    const State& state,
    EventKey key) const noexcept
{
    const auto buckets = state.program->PauseControlBuckets();
    const auto found = std::lower_bound(
        buckets.begin(),
        buckets.end(),
        key,
        [](const PauseControlBucket& bucket, EventKey candidate) noexcept {
            return bucket.key < candidate;
        });
    return found != buckets.end() && found->key == key
        ? &*found
        : nullptr;
}

const EventBucket* ProgramRuntime::Impl::FindEventBucket(
    const State& state,
    EventKey key) const noexcept
{
    const auto buckets = state.program->EventBuckets();
    const auto found = std::lower_bound(
        buckets.begin(),
        buckets.end(),
        key,
        [](const EventBucket& bucket, EventKey candidate) noexcept {
            return bucket.key < candidate;
        });
    return found != buckets.end() && found->key == key
        ? &*found
        : nullptr;
}

std::uint32_t ProgramRuntime::Impl::FindMappingSlot(
    const State& state,
    ControlRefId source) const noexcept
{
    const auto slots = state.program->MappingSlots();
    const auto found = std::lower_bound(
        slots.begin(),
        slots.end(),
        source,
        [](const MappingSlotDescriptor& slot, ControlRefId candidate) noexcept {
            return slot.source < candidate;
        });
    return found != slots.end() && found->source == source
        ? static_cast<std::uint32_t>(found - slots.begin())
        : kInvalidProgramIndex;
}

void ProgramRuntime::Impl::PublishDiagnostic(
    State& state,
    RuntimeDiagnosticKind kind,
    SourceSpan source,
    std::uint32_t subject,
    std::uint32_t position,
    std::int64_t deadline,
    std::uint32_t detail,
    std::uint32_t platformError) noexcept
{
    const RuntimeDiagnosticRecord record{
        kind,
        state.programSerial,
        state.generation.load(std::memory_order_acquire),
        state.output.nextSequence.load(std::memory_order_relaxed),
        source,
        subject,
        position,
        deadline,
        detail,
        platformError};
    state.diagnostics.TryPush(record);
    if (debugPort != nullptr && IsDebugIssue(kind)) {
        RuntimeDebugEvent event{};
        event.kind = RuntimeDebugEventKind::RuntimeIssue;
        event.issue = {
            record.kind,
            record.source,
            record.subject,
            record.position,
            record.deadlineNanoseconds,
            record.detail,
            record.platformError};
        (void)debugPort->Publish(event);
    }
}

void ProgramRuntime::Impl::PublishStateChanged(RuntimeDebugValue value) noexcept
{
    if (debugPort == nullptr) {
        return;
    }
    RuntimeDebugEvent event{};
    event.kind = RuntimeDebugEventKind::StateChanged;
    event.value = value;
    (void)debugPort->Publish(event);
}

void ProgramRuntime::Impl::PublishArrayChanged(
    RuntimeDebugArraySnapshot array) noexcept
{
    if (debugPort == nullptr) {
        return;
    }
    RuntimeDebugEvent event{};
    event.kind = RuntimeDebugEventKind::ArrayChanged;
    event.array = array;
    (void)debugPort->Publish(event);
}

bool ProgramRuntime::Impl::CompleteMutation(
    MutationTransaction& transaction,
    const MutationPublication& publication) noexcept
{
    transaction.Unlock();
    if (publication.hasScalar) {
        PublishStateChanged(publication.scalar);
    }
    if (publication.hasArray) {
        PublishArrayChanged(publication.array);
    }
    return true;
}

std::uint64_t ProgramRuntime::Impl::BeginDebugExecution(
    State& state,
    const WorkItem& item) noexcept
{
    if (debugPort == nullptr
        || item.debugCaptureEpoch == 0U
        || item.debugInputSequence == 0U
        || item.debugRuleIndex == kInvalidProgramIndex) {
        return 0U;
    }
    const std::uint64_t marker = state.scheduler.nextDebugExecutionMarker++;
    if (marker == 0U) {
        return 0U;
    }
    RuntimeDebugEvent event{};
    event.kind = RuntimeDebugEventKind::RuleMatched;
    event.captureEpoch = item.debugCaptureEpoch;
    event.executionMarker = marker;
    event.triggerInputSequence = item.debugInputSequence;
    event.ruleIndex = item.debugRuleIndex;
    return debugPort->Publish(event) ? marker : 0U;
}

void ProgramRuntime::Impl::PublishDebugExecutionEnded(
    std::uint64_t captureEpoch,
    std::uint64_t executionMarker,
    RuntimeExecutionResult result) noexcept
{
    if (debugPort == nullptr || captureEpoch == 0U || executionMarker == 0U) {
        return;
    }
    RuntimeDebugEvent event{};
    event.kind = RuntimeDebugEventKind::ExecutionEnded;
    event.captureEpoch = captureEpoch;
    event.executionMarker = executionMarker;
    event.result = result;
    (void)debugPort->Publish(event);
}

void ProgramRuntime::Impl::Invalidate(
    State& state,
    RuntimeCancellationReason reason) noexcept
{
    std::uint64_t generation = state.generation.load(std::memory_order_relaxed);
    while (generation != (std::numeric_limits<std::uint64_t>::max)()
        && !state.generation.compare_exchange_weak(
            generation,
            generation + 1U,
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
    }
    if (generation == (std::numeric_limits<std::uint64_t>::max)()) {
        if (!state.fatalShutdownRequested.exchange(
                true,
                std::memory_order_acq_rel)) {
            fatalStopRequest.Invoke();
        }
        state.accepting.store(false, std::memory_order_release);
    }
    if (reason == RuntimeCancellationReason::TargetLoss) {
        state.targetEligible.store(false, std::memory_order_release);
    }
    if (reason != RuntimeCancellationReason::Pause
        && reason != RuntimeCancellationReason::TargetIneligible
        && reason != RuntimeCancellationReason::TargetLoss) {
        state.accepting.store(false, std::memory_order_release);
    }
    for (std::size_t index = 0U;
         index < state.program->MappingSlots().size();
         ++index) {
        state.dispatch.activeMappings[index].store(
            kInvalidProgramIndex,
            std::memory_order_release);
    }
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::Cancellation,
        {},
        static_cast<std::uint32_t>(reason));
    Wake();
}

void ProgramRuntime::Impl::RequestFatal(
    State& state,
    RuntimeDiagnosticKind kind,
    SourceSpan source,
    std::uint32_t subject,
    std::uint32_t position,
    std::uint32_t detail) noexcept
{
    PublishDiagnostic(
        state,
        kind,
        source,
        subject,
        position,
        0,
        detail);
    if (!state.fatalShutdownRequested.exchange(true, std::memory_order_acq_rel)) {
        Invalidate(state, RuntimeCancellationReason::FatalFailure);
        fatalStopRequest.Invoke();
    }
}


} // namespace inputweaver
