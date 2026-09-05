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

InputDecision ProgramRuntime::Impl::HandleInput(
    const RuntimeInputEvent& event) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || event.origin != InputOrigin::PhysicalCandidate) {
        return InputDecision::Forward;
    }
    EventTransition transition{};
    bool wasSynchronized = true;
    const bool numericMouse = event.device == DeviceKind::Mouse
        && (event.transition == Transition::Move || event.transition == Transition::VerticalWheel
            || event.transition == Transition::HorizontalWheel);
    if (numericMouse) {
        transition = event.transition == Transition::Move ? EventTransition::Move
            : event.transition == Transition::VerticalWheel ? EventTransition::Wheel : EventTransition::HorizontalWheel;
    } else {
        if (!event.control.IsValid()
            || event.control.value >= state->program->Controls().size()) {
            return InputDecision::Forward;
        }
        std::atomic<std::uint8_t>& held =
            state->mutableState.physicalHeld[event.control.value];
        std::atomic<std::uint8_t>& synchronized =
            state->mutableState.physicalSynchronized[event.control.value];
        wasSynchronized = synchronized.load(
            std::memory_order_acquire) != 0U;
        if (event.transition == Transition::Down) {
            const bool wasHeld = held.exchange(1U, std::memory_order_acq_rel) != 0U;
            transition = event.device == DeviceKind::Keyboard && wasHeld
                ? EventTransition::Again
                : EventTransition::Down;
        } else if (event.transition == Transition::Up) {
            held.store(0U, std::memory_order_release);
            if (!wasSynchronized) {
                synchronized.store(1U, std::memory_order_release);
                PublishDiagnostic(
                    *state,
                    RuntimeDiagnosticKind::PhysicalStateSynchronization,
                    {},
                    event.control.value,
                    0U,
                    0,
                    1U);
            }
            transition = EventTransition::Up;
        } else {
            return InputDecision::Forward;
        }
    }

    const EventKey key{numericMouse ? ControlRefId{} : event.control, transition};
    const ExitControlBucket* const exitBucket = FindExitBucket(*state, key);
    const PauseControlBucket* const pauseBucket = FindPauseBucket(*state, key);
    const bool hasPauseRules = !state->program->PauseControlBuckets().empty();
    EventStateTransaction transaction(
        state->mutableState,
        !hasPauseRules
            ? PauseLockMode::None
            : pauseBucket == nullptr ? PauseLockMode::Read : PauseLockMode::Write);
    RefreshMouse(*state);
    if (state->mutableState.mouse) {
        state->mutableState.mouse->Observe(event, clock.NowNanoseconds());
        state->mutableState.mouse->SelectCompleted(state->dispatch.completed);
    }
    const auto accountDecision = [state](InputDecision decision) noexcept {
        if (decision == InputDecision::Suppress) {
            state->metrics.suppressedEvents.fetch_add(1U, std::memory_order_relaxed);
        }
        return decision;
    };
    const std::uint64_t transactionGeneration = state->generation.load(
        std::memory_order_acquire);
    if (exitBucket != nullptr
        && !state->exitRequested.load(std::memory_order_acquire)) {
        const auto rules = state->program->ExitControlRules().subspan(
            exitBucket->rules.begin,
            exitBucket->rules.count);
        for (const ExitControlRule& rule : rules) {
            bool matched = false;
            if (!EvaluatePredicate(*state, rule.condition, matched)) {
                return InputDecision::Forward;
            }
            if (!matched) {
                continue;
            }
            state->exitRequested.store(true, std::memory_order_release);
            Invalidate(*state, RuntimeCancellationReason::Exit);
            state->metrics.dispatchedEvents.fetch_add(
                1U,
                std::memory_order_relaxed);
            return accountDecision(InputDecision::Suppress);
        }
    }
    if (!state->accepting.load(std::memory_order_acquire)
        || !wasSynchronized
        || !state->targetEligible.load(std::memory_order_acquire)) {
        return InputDecision::Forward;
    }

    state->metrics.dispatchedEvents.fetch_add(1U, std::memory_order_relaxed);
    if (!routePort.TargetValid(state->targetKind)) {
        Invalidate(*state, RuntimeCancellationReason::TargetLoss);
        return InputDecision::Forward;
    }
    if (!routePort.CanDispatch(state->targetKind, event)) {
        if (event.device == DeviceKind::Mouse && state->mutableState.mouse) {
            state->mutableState.mouse->ResetMeters();
        }
        if (event.device == DeviceKind::Keyboard
            && state->targetKind != TargetSelectorKind::Global) {
            TransitionTargetEligibility(*state, false);
        }
        return InputDecision::Forward;
    }

    if (pauseBucket != nullptr) {
        const auto rules = state->program->PauseControlRules().subspan(
            pauseBucket->rules.begin,
            pauseBucket->rules.count);
        for (const PauseControlRule& rule : rules) {
            bool matched = false;
            if (!EvaluatePredicate(*state, rule.condition, matched)) {
                return InputDecision::Forward;
            }
            if (!matched) {
                continue;
            }
            if (state->generation.load(std::memory_order_acquire)
                    != transactionGeneration
                || !state->accepting.load(std::memory_order_acquire)
                || !state->targetEligible.load(std::memory_order_acquire)) {
                return InputDecision::Forward;
            }
            const bool before = state->mutableState.pauseOn;
            if (rule.effect == PauseEffect::On) {
                state->mutableState.pauseOn = true;
            } else if (rule.effect == PauseEffect::Off) {
                state->mutableState.pauseOn = false;
            } else {
                state->mutableState.pauseOn = !state->mutableState.pauseOn;
            }
            if (before != state->mutableState.pauseOn) {
                PublishStateChanged({
                    {},
                    ValueType::State,
                    state->mutableState.pauseOn});
                Invalidate(*state, RuntimeCancellationReason::Pause);
            }
            return accountDecision(rule.delivery == Delivery::Consume
                ? InputDecision::Suppress
                : InputDecision::Forward);
        }
    }
    if (hasPauseRules && !state->mutableState.pauseOn) {
        return InputDecision::Forward;
    }
    (void)UpdateMouseMeters(*state, event);
    if (state->mutableState.mouse) state->mutableState.mouse->SelectCompleted(state->dispatch.completed);
    const auto decision = DispatchOrdinary(
        *state,
        key,
        transactionGeneration,
        event.debugCaptureEpoch,
        event.debugInputSequence);
    if (state->mutableState.mouse) {
        for (const auto& occurrence : state->mutableState.mouse->Occurrences()) {
            if (debugPort && event.debugCaptureEpoch != 0 && event.debugInputSequence != 0) {
                RuntimeDebugEvent completed{};
                completed.kind = RuntimeDebugEventKind::MouseCycleCompleted;
                completed.captureEpoch = event.debugCaptureEpoch;
                completed.triggerInputSequence = event.debugInputSequence;
                completed.occurrence = occurrence;
                (void)debugPort->Publish(completed);
            }
            state->mutableState.mouse->SelectCompleted(state->dispatch.completed, &occurrence);
            (void)DispatchOrdinary(*state, {{}, EventTransition::Tick, occurrence.source}, transactionGeneration,
                event.debugCaptureEpoch, event.debugInputSequence);
        }
    }
    return accountDecision(decision);
}

bool ProgramRuntime::Impl::SeedPhysicalState(
    ControlRefId control,
    bool down) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || !control.IsValid()
        || control.value >= state->program->Controls().size()) {
        return false;
    }
    state->mutableState.physicalHeld[control.value].store(
        down ? 1U : 0U,
        std::memory_order_release);
    state->mutableState.physicalSynchronized[control.value].store(
        1U,
        std::memory_order_release);
    return true;
}

bool ProgramRuntime::Impl::MarkPhysicalStateUnsynchronized(
    ControlRefId control) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || !control.IsValid()
        || control.value >= state->program->Controls().size()) {
        return false;
    }
    state->mutableState.physicalHeld[control.value].store(
        0U,
        std::memory_order_release);
    state->mutableState.physicalSynchronized[control.value].store(
        0U,
        std::memory_order_release);
    PublishDiagnostic(
        *state,
        RuntimeDiagnosticKind::PhysicalStateSynchronization,
        {},
        control.value,
        0U,
        0,
        0U);
    return true;
}

void ProgramRuntime::Impl::SetTargetEligible(bool eligible) noexcept
{
    State* const state = active.get();
    if (state == nullptr) {
        return;
    }
    TransitionTargetEligibility(*state, eligible);
}

void ProgramRuntime::Impl::TransitionTargetEligibility(
    State& state,
    bool eligible) noexcept
{
    if (state.targetKind == TargetSelectorKind::Global) {
        return;
    }
    if (eligible && !state.accepting.load(std::memory_order_acquire)) {
        return;
    }
    const bool previous = state.targetEligible.exchange(
        eligible,
        std::memory_order_acq_rel);
    if (previous == eligible) {
        return;
    }
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::TargetEligibilityChange,
        {},
        kInvalidProgramIndex,
        kInvalidProgramIndex,
        0,
        eligible ? 1U : 0U);
    if (!eligible && state.accepting.load(std::memory_order_acquire)) {
        Invalidate(state, RuntimeCancellationReason::TargetIneligible);
    }
    Wake();
}

InputDecision ProgramRuntime::Impl::DispatchOrdinary(
    State& state,
    EventKey key,
    std::uint64_t transactionGeneration,
    std::uint64_t debugCaptureEpoch,
    std::uint64_t debugInputSequence) noexcept
{
    const auto transactionCurrent = [&state, transactionGeneration]() noexcept {
        return state.generation.load(std::memory_order_acquire)
                == transactionGeneration
            && state.accepting.load(std::memory_order_acquire)
            && state.targetEligible.load(std::memory_order_acquire);
    };
    if (!transactionCurrent()) {
        return InputDecision::Forward;
    }
    std::size_t scratchCount = 0U;
    bool consumed = false;
    std::uint32_t mappingActivationSlot = kInvalidProgramIndex;
    std::uint32_t mappingActivationId = kInvalidProgramIndex;
    std::uint32_t mappingClearSlot = kInvalidProgramIndex;

    const std::uint32_t slot = FindMappingSlot(state, key.control);
    if (slot != kInvalidProgramIndex
        && (key.transition == EventTransition::Again
            || key.transition == EventTransition::Up)) {
        const std::uint32_t mapping = state.dispatch.activeMappings[slot].load(
            std::memory_order_acquire);
        if (mapping != kInvalidProgramIndex) {
            state.dispatch.transactionScratch[scratchCount++] = {
                key.transition == EventTransition::Again
                    ? WorkKind::MappingAgain
                    : WorkKind::MappingRelease,
                transactionGeneration,
                kInvalidTaskSlot,
                {},
                MappingId{mapping},
                MappingSlotId{slot},
                state.program->Mappings()[mapping].target};
            consumed = true;
            if (key.transition == EventTransition::Up) {
                mappingClearSlot = slot;
            }
        }
    }

    const EventBucket* const bucket = FindEventBucket(state, key);
    if (bucket != nullptr) {
        const auto rules = state.program->Rules().subspan(
            bucket->rules.begin,
            bucket->rules.count);
        for (std::size_t ruleOffset = 0U; ruleOffset < rules.size(); ++ruleOffset) {
            const CompiledRule& rule = rules[ruleOffset];
            bool matched = false;
            if (!EvaluatePredicate(state, rule.condition, matched)) {
                return InputDecision::Forward;
            }
            if (!matched) {
                continue;
            }
            if (rule.kind == RuleKind::MappingDown) {
                const MappingDescriptor& mapping =
                    state.program->Mappings()[rule.mapping.value];
                const std::uint32_t mappingSlot = mapping.slot.value;
                if (state.dispatch.activeMappings[mappingSlot].load(
                        std::memory_order_acquire) == kInvalidProgramIndex) {
                    WorkItem& item =
                        state.dispatch.transactionScratch[scratchCount++];
                    item = {
                        WorkKind::MappingAcquire,
                        transactionGeneration,
                        kInvalidTaskSlot,
                        {},
                        rule.mapping,
                        mapping.slot,
                        mapping.target};
                    item.debugCaptureEpoch = debugCaptureEpoch;
                    item.debugInputSequence = debugInputSequence;
                    item.debugRuleIndex = bucket->rules.begin
                        + static_cast<std::uint32_t>(ruleOffset);
                    mappingActivationSlot = mappingSlot;
                    mappingActivationId = rule.mapping.value;
                }
            } else if (rule.action.IsValid()) {
                WorkItem& item = state.dispatch.transactionScratch[scratchCount++];
                item = {
                    WorkKind::TaskStart,
                    transactionGeneration,
                    kInvalidTaskSlot,
                    rule.action};
                item.debugCaptureEpoch = debugCaptureEpoch;
                item.debugInputSequence = debugInputSequence;
                item.debugRuleIndex = bucket->rules.begin
                    + static_cast<std::uint32_t>(ruleOffset);
            }
            consumed = consumed || rule.delivery == Delivery::Consume;
            if (rule.flow == MatchFlow::Stop) {
                break;
            }
        }
    }

    if (scratchCount > state.dispatch.workQueue.Available()) {
        state.metrics.transactionRejections.fetch_add(
            1U,
            std::memory_order_relaxed);
        PublishDiagnostic(state, RuntimeDiagnosticKind::TransactionCapacity);
        return InputDecision::Forward;
    }

    std::size_t reservedCount = 0U;
    for (std::size_t index = 0; index < scratchCount; ++index) {
        auto& item = state.dispatch.transactionScratch[index];
        item.debugMeter = key.source;
        item.debugCycleSequence = key.source.IsValid() ? state.dispatch.completed[key.source.value].sequence : 0;
    }
    if (!ReserveTasks(state, scratchCount, reservedCount)) {
        state.metrics.transactionRejections.fetch_add(
            1U,
            std::memory_order_relaxed);
        PublishDiagnostic(state, RuntimeDiagnosticKind::TransactionCapacity);
        return InputDecision::Forward;
    }
    if (!transactionCurrent()) {
        ReleaseReservedTasks(state, reservedCount);
        return InputDecision::Forward;
    }
    if (scratchCount != 0U
        && !state.dispatch.workQueue.TryPush(
            {state.dispatch.transactionScratch.data(), scratchCount})) {
        ReleaseReservedTasks(state, reservedCount);
        state.metrics.transactionRejections.fetch_add(
            1U,
            std::memory_order_relaxed);
        PublishDiagnostic(state, RuntimeDiagnosticKind::TransactionCapacity);
        return InputDecision::Forward;
    }

    if (mappingActivationSlot != kInvalidProgramIndex) {
        state.dispatch.activeMappings[mappingActivationSlot].store(
            mappingActivationId,
            std::memory_order_release);
        PublishDiagnostic(
            state,
            RuntimeDiagnosticKind::MappingChange,
            {},
            mappingActivationId,
            0U,
            0,
            1U);
    }
    if (mappingClearSlot != kInvalidProgramIndex) {
        state.dispatch.activeMappings[mappingClearSlot].store(
            kInvalidProgramIndex,
            std::memory_order_release);
        PublishDiagnostic(
            state,
            RuntimeDiagnosticKind::MappingChange,
            {},
            mappingClearSlot,
            0U,
            0,
            0U);
    }
    if (scratchCount != 0U) {
        Wake();
    }
    if (!transactionCurrent()) {
        if (mappingActivationSlot != kInvalidProgramIndex) {
            std::uint32_t expected = mappingActivationId;
            (void)state.dispatch.activeMappings[mappingActivationSlot]
                .compare_exchange_strong(
                    expected,
                    kInvalidProgramIndex,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
        }
        return InputDecision::Forward;
    }
    return consumed ? InputDecision::Suppress : InputDecision::Forward;
}

bool ProgramRuntime::Impl::ReserveTasks(
    State& state,
    std::size_t scratchCount,
    std::size_t& reservedCount) noexcept
{
    reservedCount = 0U;
    for (std::size_t itemIndex = 0U;
         itemIndex < scratchCount;
         ++itemIndex) {
        WorkItem& item = state.dispatch.transactionScratch[itemIndex];
        if (item.kind != WorkKind::TaskStart) {
            continue;
        }
        bool reserved = false;
        for (std::size_t slot = 0U; slot < state.scheduler.taskCount; ++slot) {
            TaskStatus expected = TaskStatus::Free;
            if (!state.scheduler.tasks[slot].status.compare_exchange_strong(
                    expected,
                    TaskStatus::Initializing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                continue;
            }
            TaskInstance& task = state.scheduler.tasks[slot];
            task.action = item.action;
            task.position = 0U;
            task.generation = item.generation;
            task.readyOrder = 0U;
            task.timedOrder = 0U;
            task.deadlineNanoseconds = 0;
            task.resumeKind = TaskResumeKind::None;
            task.pendingTap = {};
            task.instructionsWithoutSuspension = 0U;
            task.outputsWithoutSuspension = 0U;
            task.debugCaptureEpoch = 0U;
            task.debugExecutionMarker = 0U;
            task.cleanupResult = RuntimeExecutionResult::Completed;
            task.cleanupCancelled = false;
            task.safetyCancelled = false;
            std::copy(state.dispatch.completed.begin(), state.dispatch.completed.end(), task.completed.begin());
            std::fill(task.repeatFrames.begin(), task.repeatFrames.end(), RepeatFrame{});
            std::fill(
                task.ownership.begin(),
                task.ownership.end(),
                TaskOwnershipRecord{});
            task.status.store(TaskStatus::Reserved, std::memory_order_release);
            item.taskSlot = static_cast<std::uint32_t>(slot);
            state.dispatch.reservedTaskScratch[reservedCount++] = item.taskSlot;
            reserved = true;
            break;
        }
        if (!reserved) {
            ReleaseReservedTasks(state, reservedCount);
            reservedCount = 0U;
            return false;
        }
    }
    return true;
}

void ProgramRuntime::Impl::ReleaseReservedTasks(
    State& state,
    std::size_t reservedCount) noexcept
{
    for (std::size_t index = 0U; index < reservedCount; ++index) {
        state.scheduler.tasks[state.dispatch.reservedTaskScratch[index]].status.store(
            TaskStatus::Free,
            std::memory_order_release);
    }
}


} // namespace inputweaver
