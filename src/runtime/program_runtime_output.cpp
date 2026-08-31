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

bool ProgramRuntime::Impl::PublishOutput(
    State& state,
    ControlRefId control,
    RuntimeOutputTransition transition,
    std::uint64_t producerGeneration,
    TaskInstance* producer,
    bool* rateExceeded) noexcept
{
    if (rateExceeded != nullptr) {
        *rateExceeded = false;
    }
    const std::uint64_t currentGeneration = state.generation.load(
        std::memory_order_acquire);
    if (transition != RuntimeOutputTransition::Up
        && producerGeneration != currentGeneration) {
        return false;
    }
    if (!control.IsValid() || control.value >= state.activatedControls.size()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::OutputFailure,
            {},
            control.value,
            kInvalidProgramIndex,
            static_cast<std::uint32_t>(RuntimeOutputResult::Failed));
        return false;
    }
    const ActivatedControl& activated = state.activatedControls[control.value];
    if (transition != RuntimeOutputTransition::Up) {
        if (!state.targetEligible.load(std::memory_order_acquire)) {
            return false;
        }
        if (!routePort.CanInject(state.targetKind, activated)) {
            if (state.targetKind == TargetSelectorKind::Global) {
                Invalidate(state, RuntimeCancellationReason::TargetLoss);
            } else {
                TransitionTargetEligibility(state, false);
            }
            return false;
        }

        SourceSpan producerSource{};
        std::uint32_t producerSubject = control.value;
        std::uint32_t producerPosition = kInvalidProgramIndex;
        if (producer != nullptr
            && producer->action.IsValid()
            && producer->action.value < state.program->ActionPrograms().size()) {
            const ActionProgramDescriptor& descriptor =
                state.program->ActionPrograms()[producer->action.value];
            producerSource = ActionSource(
                state,
                descriptor,
                producer->position);
            producerSubject = producer->action.value;
            producerPosition = producer->position;
        }
        if (producer != nullptr
            && producer->outputsWithoutSuspension
                >= capacities.maximumTaskOutputsWithoutSuspension) {
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::TaskBudgetExceeded,
                producerSource,
                producerSubject,
                producerPosition,
                0,
                2U);
            producer->safetyCancelled = true;
            return false;
        }

        const std::int64_t now = (std::max)(
            clock.NowNanoseconds(),
            std::int64_t{0});
        if (now < state.output.rateWindowStartNanoseconds
            || now - state.output.rateWindowStartNanoseconds
                >= capacities.outputRateIntervalNanoseconds) {
            state.output.rateWindowStartNanoseconds = now;
            state.output.rateWindowTransitions = 0U;
        }
        if (state.output.rateWindowTransitions
            >= capacities.maximumOutputTransitionsPerInterval) {
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::OutputRateExceeded,
                producerSource,
                producerSubject,
                producerPosition,
                0,
                capacities.maximumOutputTransitionsPerInterval);
            if (rateExceeded != nullptr) {
                *rateExceeded = true;
            }
            if (producer != nullptr) {
                producer->safetyCancelled = true;
            }
            return false;
        }
    }
    const RuntimeOutputResult result = outputPort.Publish({
        transition == RuntimeOutputTransition::Up
            ? currentGeneration
            : producerGeneration,
        state.output.nextSequence.fetch_add(1U, std::memory_order_relaxed),
        control,
        state.program->Controls()[control.value],
        activated,
        transition});
    if (result == RuntimeOutputResult::Accepted) {
        if (transition != RuntimeOutputTransition::Up) {
            ++state.output.rateWindowTransitions;
            if (producer != nullptr) {
                ++producer->outputsWithoutSuspension;
            }
        }
        state.metrics.outputTransitions.fetch_add(1U, std::memory_order_relaxed);
        return true;
    }
    if (result == RuntimeOutputResult::RouteRejected) {
        if (transition == RuntimeOutputTransition::Up
            || state.targetKind == TargetSelectorKind::Global) {
            Invalidate(state, RuntimeCancellationReason::TargetLoss);
        } else {
            TransitionTargetEligibility(state, false);
        }
        return false;
    }
    RequestFatal(
        state,
        RuntimeDiagnosticKind::OutputFailure,
        {},
        control.value,
        kInvalidProgramIndex,
        static_cast<std::uint32_t>(result));
    return false;
}

bool ProgramRuntime::Impl::AcquireGlobal(
    State& state,
    ControlRefId control,
    std::uint64_t producerGeneration,
    TaskInstance* producer,
    bool* rateExceeded) noexcept
{
    const std::lock_guard lock(state.output.mutex);
    if (producerGeneration
        != state.generation.load(std::memory_order_acquire)) {
        return false;
    }
    std::uint64_t& count = state.output.globalOwnership[control.value];
    if (count == (std::numeric_limits<std::uint64_t>::max)()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            control.value,
            kInvalidProgramIndex,
            1U);
        return false;
    }
    if (count == 0U
        && !PublishOutput(
            state,
             control,
             RuntimeOutputTransition::Down,
             producerGeneration,
             producer,
             rateExceeded)) {
        return false;
    }
    ++count;
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::OwnershipChange,
        {},
        control.value,
        kInvalidProgramIndex,
        0,
        1U);
    return true;
}

bool ProgramRuntime::Impl::ReleaseGlobal(
    State& state,
    ControlRefId control,
    std::uint64_t count) noexcept
{
    const std::lock_guard lock(state.output.mutex);
    std::uint64_t& globalCount = state.output.globalOwnership[control.value];
    if (count == 0U) {
        return true;
    }
    if (globalCount < count) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            control.value,
            kInvalidProgramIndex,
            2U);
        return false;
    }
    if (globalCount == count
        && !PublishOutput(
            state,
            control,
            RuntimeOutputTransition::Up,
            state.generation.load(std::memory_order_acquire))) {
        return false;
    }
    globalCount -= count;
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::OwnershipChange,
        {},
        control.value,
        kInvalidProgramIndex,
        0,
        0U);
    return true;
}

bool ProgramRuntime::Impl::AcquireTaskControl(
    State& state,
    TaskInstance& task,
    ControlRefId control) noexcept
{
    TaskOwnershipRecord* freeRecord = nullptr;
    TaskOwnershipRecord* record = nullptr;
    for (TaskOwnershipRecord& candidate : task.ownership) {
        if (candidate.count != 0U && candidate.control == control) {
            record = &candidate;
            break;
        }
        if (candidate.count == 0U && freeRecord == nullptr) {
            freeRecord = &candidate;
        }
    }
    if (record == nullptr) {
        record = freeRecord;
    }
    if (record == nullptr
        || record->count == (std::numeric_limits<std::uint64_t>::max)()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            control.value,
            task.position,
            3U);
        return false;
    }
    if (!AcquireGlobal(state, control, task.generation, &task)) {
        return false;
    }
    if (record->count == 0U) {
        record->control = control;
    }
    ++record->count;
    return true;
}

bool ProgramRuntime::Impl::ReleaseTaskControl(
    State& state,
    TaskInstance& task,
    ControlRefId control) noexcept
{
    for (TaskOwnershipRecord& record : task.ownership) {
        if (record.count == 0U || record.control != control) {
            continue;
        }
        if (!ReleaseGlobal(state, control, 1U)) {
            return false;
        }
        --record.count;
        if (record.count == 0U) {
            record.control = {};
        }
        return true;
    }
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::TaskActionFault,
        {},
        control.value,
        task.position,
        4U);
    return false;
}

bool ProgramRuntime::Impl::ReleaseTaskOwnership(
    State& state,
    TaskInstance& task) noexcept
{
    bool succeeded = true;
    for (std::size_t index = task.ownership.size(); index != 0U; --index) {
        TaskOwnershipRecord& record = task.ownership[index - 1U];
        if (record.count == 0U) {
            continue;
        }
        if (!ReleaseGlobal(state, record.control, record.count)) {
            succeeded = false;
            continue;
        }
        record = {};
    }
    return succeeded;
}

void ProgramRuntime::Impl::ProcessMappingWork(
    State& state,
    const WorkItem& item) noexcept
{
    if (!item.mappingSlot.IsValid()
        || item.mappingSlot.value >= state.dispatch.mappingOwners.size()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            item.mappingSlot.value,
            kInvalidProgramIndex,
            5U);
        return;
    }
    MappingOwner& owner = state.dispatch.mappingOwners[item.mappingSlot.value];
    if (item.kind == WorkKind::MappingAcquire) {
        if (owner.owned) {
            if (owner.generation == item.generation) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    {},
                    item.mapping.value,
                    kInvalidProgramIndex,
                    6U);
                return;
            }
            if (!ReleaseGlobal(state, owner.target, 1U)) {
                return;
            }
            PublishDebugExecutionEnded(
                owner.debugCaptureEpoch,
                owner.debugExecutionMarker,
                RuntimeExecutionResult::Cancelled);
            owner = {};
        }
        const std::uint64_t debugMarker = BeginDebugExecution(state, item);
        bool rateExceeded = false;
        if (AcquireGlobal(
                state,
                item.control,
                item.generation,
                nullptr,
                &rateExceeded)) {
            owner.owned = true;
            owner.generation = item.generation;
            owner.mapping = item.mapping;
            owner.target = item.control;
            owner.debugCaptureEpoch = item.debugCaptureEpoch;
            owner.debugExecutionMarker = debugMarker;
        } else if (rateExceeded) {
            state.dispatch.activeMappings[item.mappingSlot.value].store(
                kInvalidProgramIndex,
                std::memory_order_release);
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::MappingChange,
                {},
                item.mapping.value,
                0U,
                0,
                0U);
            PublishDebugExecutionEnded(
                item.debugCaptureEpoch,
                debugMarker,
                RuntimeExecutionResult::Failed);
        } else {
            PublishDebugExecutionEnded(
                item.debugCaptureEpoch,
                debugMarker,
                RuntimeExecutionResult::Failed);
        }
        return;
    }
    if (item.kind == WorkKind::MappingAgain) {
        if (owner.owned) {
            bool rateExceeded = false;
            const bool published = PublishOutput(
                state,
                owner.target,
                RuntimeOutputTransition::Again,
                item.generation,
                nullptr,
                &rateExceeded);
            if (!published && rateExceeded) {
                state.dispatch.activeMappings[item.mappingSlot.value].store(
                    kInvalidProgramIndex,
                    std::memory_order_release);
                if (ReleaseGlobal(state, owner.target, 1U)) {
                    PublishDebugExecutionEnded(
                        owner.debugCaptureEpoch,
                        owner.debugExecutionMarker,
                        RuntimeExecutionResult::Cancelled);
                    owner = {};
                }
                PublishDiagnostic(
                    state,
                    RuntimeDiagnosticKind::MappingChange,
                    {},
                    item.mapping.value,
                    0U,
                    0,
                    0U);
            }
        }
        return;
    }
    if (item.kind == WorkKind::MappingRelease && owner.owned) {
        if (ReleaseGlobal(state, owner.target, 1U)) {
            PublishDebugExecutionEnded(
                owner.debugCaptureEpoch,
                owner.debugExecutionMarker,
                RuntimeExecutionResult::Completed);
            owner = {};
        } else {
            PublishDebugExecutionEnded(
                owner.debugCaptureEpoch,
                owner.debugExecutionMarker,
                RuntimeExecutionResult::Failed);
            owner.debugCaptureEpoch = 0U;
            owner.debugExecutionMarker = 0U;
        }
    }
}

void ProgramRuntime::Impl::CleanupMappingOwners(State& state) noexcept
{
    for (MappingOwner& owner : state.dispatch.mappingOwners) {
        if (!owner.owned) {
            continue;
        }
        if (ReleaseGlobal(state, owner.target, 1U)) {
            PublishDebugExecutionEnded(
                owner.debugCaptureEpoch,
                owner.debugExecutionMarker,
                RuntimeExecutionResult::Cancelled);
            owner = {};
        }
    }
}

void ProgramRuntime::Impl::FinishTask(
    State& state,
    std::uint32_t slot,
    bool cancelled,
    RuntimeExecutionResult result) noexcept
{
    TaskInstance& task = state.scheduler.tasks[slot];
    bool requestedCancelled = task.safetyCancelled || cancelled;
    RuntimeExecutionResult requested = task.safetyCancelled
        ? RuntimeExecutionResult::Cancelled
        : result;
    if (task.status.load(std::memory_order_acquire) == TaskStatus::Cleanup) {
        requestedCancelled = requestedCancelled || task.cleanupCancelled;
        requested = MergeExecutionResult(task.cleanupResult, requested);
    }
    if (!ReleaseTaskOwnership(state, task)) {
        requested = MergeExecutionResult(
            requested,
            ClassifyTaskOperationFailure(state, task));
        task.cleanupCancelled = requestedCancelled;
        task.cleanupResult = requested;
        task.resumeKind = TaskResumeKind::None;
        task.pendingTap = {};
        task.status.store(TaskStatus::Cleanup, std::memory_order_release);
        return;
    }
    const RuntimeExecutionResult finalResult = requested;
    if (debugPort != nullptr
        && task.debugCaptureEpoch != 0U
        && task.debugExecutionMarker != 0U) {
        RuntimeDebugEvent event{};
        event.kind = RuntimeDebugEventKind::ExecutionEnded;
        event.captureEpoch = task.debugCaptureEpoch;
        event.executionMarker = task.debugExecutionMarker;
        event.result = finalResult;
        (void)debugPort->Publish(event);
    }
    task.debugCaptureEpoch = 0U;
    task.debugExecutionMarker = 0U;
    task.cleanupResult = RuntimeExecutionResult::Completed;
    task.cleanupCancelled = false;
    task.safetyCancelled = false;
    task.resumeKind = TaskResumeKind::None;
    task.pendingTap = {};
    task.instructionsWithoutSuspension = 0U;
    task.outputsWithoutSuspension = 0U;
    task.status.store(TaskStatus::Free, std::memory_order_release);
    if (requestedCancelled) {
        state.metrics.cancelledTasks.fetch_add(1U, std::memory_order_relaxed);
    } else {
        state.metrics.completedTasks.fetch_add(1U, std::memory_order_relaxed);
    }
}

RuntimeExecutionResult ProgramRuntime::Impl::ClassifyTaskOperationFailure(
    const State& state,
    const TaskInstance& task) const noexcept
{
    if (task.safetyCancelled) {
        return RuntimeExecutionResult::Cancelled;
    }
    if (state.fatalShutdownRequested.load(std::memory_order_acquire)) {
        return RuntimeExecutionResult::Failed;
    }
    return task.generation != state.generation.load(std::memory_order_acquire)
        ? RuntimeExecutionResult::Cancelled
        : RuntimeExecutionResult::Failed;
}

void ProgramRuntime::Impl::CleanupStale(State& state) noexcept
{
    const std::uint64_t generation = state.generation.load(std::memory_order_acquire);
    for (std::size_t index = 0U; index < state.scheduler.taskCount; ++index) {
        TaskInstance& task = state.scheduler.tasks[index];
        const TaskStatus status = task.status.load(std::memory_order_acquire);
        if (status == TaskStatus::Free
            || status == TaskStatus::Initializing
            || task.generation == generation) {
            continue;
        }
        FinishTask(
            state,
            static_cast<std::uint32_t>(index),
            true,
            RuntimeExecutionResult::Cancelled);
    }
    for (MappingOwner& owner : state.dispatch.mappingOwners) {
        if (!owner.owned || owner.generation == generation) {
            continue;
        }
        if (ReleaseGlobal(state, owner.target, 1U)) {
            PublishDebugExecutionEnded(
                owner.debugCaptureEpoch,
                owner.debugExecutionMarker,
                RuntimeExecutionResult::Cancelled);
            owner = {};
        }
    }
}

void ProgramRuntime::Impl::CleanupAfterInvalidation(State& state) noexcept
{
    constexpr std::size_t cleanupAttempts = 3U;
    for (std::size_t attempt = 0U; attempt < cleanupAttempts; ++attempt) {
        CleanupStale(state);
        DrainWork(state);
        CleanupStale(state);
        CleanupMappingOwners(state);
    }
}


} // namespace inputweaver
