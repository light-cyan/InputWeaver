#include "debug_client.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

namespace inputweaver::debug {
namespace {

[[nodiscard]] DebugControlIdentity MakeControl(
    const InputEventPayload& input) noexcept
{
    return {
        input.device,
        input.hasCompiledControl,
        input.compiledIdentity,
        input.virtualKey,
        input.scanCode,
        input.nativeQualifier,
        input.mouseData};
}

template <typename Item>
void AppendRecent(std::vector<Item>& items, Item item, std::size_t capacity)
{
    if (items.size() == capacity && !items.empty()) {
        items.erase(items.begin());
    }
    items.push_back(std::move(item));
}

} // namespace

bool SameDebugControl(
    const DebugControlIdentity& left,
    const DebugControlIdentity& right) noexcept
{
    if (left.device != right.device
        || left.hasCompiledControl != right.hasCompiledControl) {
        return false;
    }
    if (left.hasCompiledControl) {
        return left.compiledIdentity == right.compiledIdentity;
    }
    return left.virtualKey == right.virtualKey
        && left.scanCode == right.scanCode
        && left.nativeQualifier == right.nativeQualifier
        && left.mouseData == right.mouseData;
}

std::string_view InputOriginLabel(InputOrigin origin) noexcept
{
    switch (origin) {
    case InputOrigin::PhysicalCandidate:
        return "PHY";
    case InputOrigin::CurrentInstanceInjected:
        return "ECHO";
    case InputOrigin::ExternalInjected:
        return "EXT";
    case InputOrigin::InitialSample:
        return "INIT";
    }
    return {};
}

struct DebugStateReducer::Impl final {
    struct PendingExecution final {
        std::uint64_t executionMarker{};
        std::uint64_t triggerInputSequence{};
        std::shared_ptr<const DebugRuleProgram> program;
        std::int64_t matchedTimeNanoseconds{};
        std::int64_t matchedUnixTimeMilliseconds{};
        std::optional<std::uint32_t> currentInstructionIndex;
        std::vector<std::uint32_t> recentInstructionIndices;
        std::optional<RuntimeExecutionResult> result;
    };

    explicit Impl(DebugClientCapacities selectedCapacities)
        : capacities(selectedCapacities),
          published(std::make_shared<const DebugClientState>(state))
    {
    }

    void Publish()
    {
        ++state.version;
        published = std::make_shared<const DebugClientState>(state);
    }

    void ClearCapture()
    {
        state.captureEpoch = 0U;
        state.recentInputEvents.clear();
        state.pressedControls.clear();
        state.ruleExecutions.clear();
        state.runtimeIssues.clear();
        pendingExecutions.clear();
        lastInputSequence = 0U;
        storedInstructions = 0U;
        captureStartTimeNanoseconds = 0;
        captureStartUnixTimeMilliseconds = 0;
    }

    [[nodiscard]] bool CaptureUnixTime(
        std::int64_t captureTimeNanoseconds,
        std::int64_t& captureUnixTimeMilliseconds) const noexcept
    {
        if (captureStartTimeNanoseconds < 0
            || captureStartUnixTimeMilliseconds <= 0
            || captureTimeNanoseconds < captureStartTimeNanoseconds) {
            return false;
        }
        const std::int64_t elapsedMilliseconds =
            (captureTimeNanoseconds - captureStartTimeNanoseconds)
            / 1'000'000LL;
        if (captureStartUnixTimeMilliseconds
            > (std::numeric_limits<std::int64_t>::max)()
                - elapsedMilliseconds) {
            return false;
        }
        captureUnixTimeMilliseconds = captureStartUnixTimeMilliseconds
            + elapsedMilliseconds;
        return true;
    }

    [[nodiscard]] DebugReductionAction Recover(DebugClientFault fault)
    {
        state.captureRequested = true;
        state.capturing = false;
        state.captureTrusted = false;
        state.lastFault = fault;
        if (!recoveryPending) {
            recoveryPending = true;
            Publish();
            return DebugReductionAction::RestartCapture;
        }
        Publish();
        return DebugReductionAction::None;
    }

    [[nodiscard]] std::vector<DebugPressedControl>::iterator FindPressed(
        const DebugControlIdentity& control,
        InputOrigin origin)
    {
        return std::find_if(
            state.pressedControls.begin(),
            state.pressedControls.end(),
            [&](const DebugPressedControl& pressed) {
                return pressed.origin == origin
                    && SameDebugControl(pressed.control, control);
            });
    }

    [[nodiscard]] bool RemovePressed(
        const DebugControlIdentity& control,
        InputOrigin origin)
    {
        const auto found = FindPressed(control, origin);
        if (found == state.pressedControls.end()) {
            return false;
        }
        state.pressedControls.erase(found);
        return true;
    }

    [[nodiscard]] bool AddPressed(
        const DebugControlIdentity& control,
        InputOrigin origin)
    {
        if (FindPressed(control, origin) != state.pressedControls.end()) {
            return true;
        }
        if (state.pressedControls.size() >= capacities.maximumPressedControls) {
            return false;
        }
        state.pressedControls.push_back({control, origin});
        return true;
    }

    [[nodiscard]] DebugInputEvent* FindInput(std::uint64_t sequence)
    {
        const auto found = std::find_if(
            state.recentInputEvents.begin(),
            state.recentInputEvents.end(),
            [sequence](const DebugInputEvent& input) {
                return input.inputSequence == sequence;
            });
        return found == state.recentInputEvents.end() ? nullptr : &*found;
    }

    [[nodiscard]] DebugRuleExecution* FindExecution(std::uint64_t marker)
    {
        const auto found = std::find_if(
            state.ruleExecutions.begin(),
            state.ruleExecutions.end(),
            [marker](const DebugRuleExecution& execution) {
                return execution.executionMarker == marker;
            });
        return found == state.ruleExecutions.end() ? nullptr : &*found;
    }

    [[nodiscard]] PendingExecution* FindPending(std::uint64_t marker)
    {
        const auto found = std::find_if(
            pendingExecutions.begin(),
            pendingExecutions.end(),
            [marker](const PendingExecution& execution) {
                return execution.executionMarker == marker;
            });
        return found == pendingExecutions.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool MarkerExists(std::uint64_t marker)
    {
        return FindExecution(marker) != nullptr || FindPending(marker) != nullptr;
    }

    [[nodiscard]] std::size_t InstructionCount(
        const DebugRuleProgram& program) const noexcept
    {
        return program.conditionInstructions.size()
            + program.actionInstructions.size();
    }

    [[nodiscard]] bool RemoveOldestEndedExecution()
    {
        const auto found = std::find_if(
            state.ruleExecutions.begin(),
            state.ruleExecutions.end(),
            [](const DebugRuleExecution& execution) {
                return execution.result.has_value();
            });
        if (found == state.ruleExecutions.end()) {
            return false;
        }
        storedInstructions -= InstructionCount(*found->program);
        state.ruleExecutions.erase(found);
        return true;
    }

    [[nodiscard]] bool ReserveExecutionSpace(std::size_t extraInstructions)
    {
        if (extraInstructions > capacities.maximumStoredInstructions) {
            return false;
        }
        while ((state.ruleExecutions.size()
                    >= capacities.maximumRuleExecutions
                || storedInstructions
                        > capacities.maximumStoredInstructions
                            - extraInstructions)
            && RemoveOldestEndedExecution()) {
        }
        return state.ruleExecutions.size() < capacities.maximumRuleExecutions
            && storedInstructions
                <= capacities.maximumStoredInstructions - extraInstructions;
    }

    [[nodiscard]] bool Materialize(
        std::size_t pendingIndex,
        const DebugInputEvent& trigger)
    {
        if (pendingIndex >= pendingExecutions.size()
            || !ReserveExecutionSpace(0U)) {
            return false;
        }
        PendingExecution pending = std::move(pendingExecutions[pendingIndex]);
        pendingExecutions.erase(
            pendingExecutions.begin()
            + static_cast<std::ptrdiff_t>(pendingIndex));
        DebugRuleExecution execution{};
        execution.executionMarker = pending.executionMarker;
        execution.triggerInput = trigger;
        execution.program = std::move(pending.program);
        execution.matchedTimeNanoseconds = pending.matchedTimeNanoseconds;
        execution.matchedUnixTimeMilliseconds =
            pending.matchedUnixTimeMilliseconds;
        execution.currentInstructionIndex = pending.currentInstructionIndex;
        execution.recentInstructionIndices = std::move(
            pending.recentInstructionIndices);
        execution.result = pending.result;
        state.ruleExecutions.push_back(std::move(execution));
        return true;
    }

    [[nodiscard]] bool MaterializeForInput(const DebugInputEvent& trigger)
    {
        for (std::size_t index = 0U; index < pendingExecutions.size();) {
            if (pendingExecutions[index].triggerInputSequence
                != trigger.inputSequence) {
                ++index;
                continue;
            }
            if (!Materialize(index, trigger)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] DebugReductionAction AcceptCaptureStarted(
        const Message& message)
    {
        if (!state.captureRequested) {
            return DebugReductionAction::None;
        }
        if (message.header.captureEpoch == 0U
            || (state.captureEpoch != 0U
                && message.header.captureEpoch <= state.captureEpoch)) {
            return Recover(DebugClientFault::CaptureEpochMismatch);
        }
        if (message.header.captureTimeNanoseconds < 0
            || message.captureStarted.captureUnixTimeMilliseconds <= 0) {
            return Recover(DebugClientFault::InconsistentState);
        }
        ClearCapture();
        captureStartTimeNanoseconds = message.header.captureTimeNanoseconds;
        captureStartUnixTimeMilliseconds =
            message.captureStarted.captureUnixTimeMilliseconds;
        state.captureEpoch = message.header.captureEpoch;
        state.capturing = true;
        state.captureTrusted = true;
        state.lastFault = DebugClientFault::None;
        recoveryPending = false;
        Publish();
        return DebugReductionAction::None;
    }

    [[nodiscard]] bool ValidInput(const InputEventPayload& input) const noexcept
    {
        if (input.inputSequence == 0U) {
            return false;
        }
        if (input.origin == InputOrigin::InitialSample) {
            return input.transition == Transition::Down
                && input.disposition == InputDisposition::NotApplicable;
        }
        return input.disposition != InputDisposition::NotApplicable;
    }

    [[nodiscard]] DebugReductionAction AcceptInput(const Message& message)
    {
        const InputEventPayload& payload = message.inputEvent;
        if (!ValidInput(payload)
            || payload.inputSequence != lastInputSequence + 1U) {
            return Recover(DebugClientFault::InconsistentState);
        }
        if (capacities.maximumInputEvents == 0U) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        DebugInputEvent input{};
        if (!CaptureUnixTime(
                message.header.captureTimeNanoseconds,
                input.captureUnixTimeMilliseconds)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        input.inputSequence = payload.inputSequence;
        input.captureTimeNanoseconds = message.header.captureTimeNanoseconds;
        input.control = MakeControl(payload);
        input.transition = payload.transition;
        input.origin = payload.origin;
        input.disposition = payload.disposition;
        if (payload.transition == Transition::Down) {
            if (payload.origin == InputOrigin::InitialSample) {
                input.repeatedDown = FindPressed(
                    input.control,
                    InputOrigin::InitialSample) != state.pressedControls.end();
                if (!AddPressed(input.control, InputOrigin::InitialSample)) {
                    return Recover(DebugClientFault::CapacityExceeded);
                }
            } else {
                const bool replacedInitial = RemovePressed(
                    input.control,
                    InputOrigin::InitialSample);
                const bool alreadyPressed = FindPressed(
                    input.control,
                    payload.origin) != state.pressedControls.end();
                input.repeatedDown = replacedInitial || alreadyPressed;
                if (!AddPressed(input.control, payload.origin)) {
                    return Recover(DebugClientFault::CapacityExceeded);
                }
            }
        } else if (payload.transition == Transition::Up) {
            if (payload.origin == InputOrigin::InitialSample) {
                return Recover(DebugClientFault::InconsistentState);
            }
            const bool removedInitial = RemovePressed(
                input.control,
                InputOrigin::InitialSample);
            const bool removedOrigin = RemovePressed(
                input.control,
                payload.origin);
            input.unmatchedUp = !removedInitial && !removedOrigin;
        }
        lastInputSequence = payload.inputSequence;
        AppendRecent(
            state.recentInputEvents,
            input,
            capacities.maximumInputEvents);
        DebugInputEvent* stored = FindInput(payload.inputSequence);
        if (stored == nullptr || !MaterializeForInput(*stored)) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        Publish();
        return DebugReductionAction::None;
    }

    [[nodiscard]] DebugReductionAction AcceptRuleMatched(
        const Message& message)
    {
        const RuleMatchedPayload& matched = message.ruleMatched;
        if (matched.executionMarker == 0U
            || matched.triggerInputSequence == 0U
            || MarkerExists(matched.executionMarker)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        std::int64_t matchedUnixTimeMilliseconds{};
        if (!CaptureUnixTime(
                message.header.captureTimeNanoseconds,
                matchedUnixTimeMilliseconds)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        const std::size_t instructions = matched.conditionInstructions.size()
            + matched.actionInstructions.size();
        if (instructions > capacities.maximumStoredInstructions
            || storedInstructions
                > capacities.maximumStoredInstructions - instructions) {
            while (storedInstructions
                    > capacities.maximumStoredInstructions - instructions
                && RemoveOldestEndedExecution()) {
            }
            if (storedInstructions
                > capacities.maximumStoredInstructions - instructions) {
                return Recover(DebugClientFault::CapacityExceeded);
            }
        }
        if (matched.triggerInputSequence <= lastInputSequence
            && FindInput(matched.triggerInputSequence) == nullptr) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        if (pendingExecutions.size()
            >= capacities.maximumPendingRuleExecutions) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        std::shared_ptr<DebugRuleProgram> program;
        try {
            program = std::make_shared<DebugRuleProgram>();
            program->eventTransition = matched.eventTransition;
            program->eventControl = matched.eventControl;
            program->conditionInstructions = matched.conditionInstructions;
            program->actionInstructions = matched.actionInstructions;
        } catch (...) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        PendingExecution pending{};
        pending.executionMarker = matched.executionMarker;
        pending.triggerInputSequence = matched.triggerInputSequence;
        pending.program = std::move(program);
        pending.matchedTimeNanoseconds = message.header.captureTimeNanoseconds;
        pending.matchedUnixTimeMilliseconds = matchedUnixTimeMilliseconds;
        storedInstructions += instructions;
        pendingExecutions.push_back(std::move(pending));
        DebugInputEvent* trigger = FindInput(matched.triggerInputSequence);
        if (trigger != nullptr
            && !Materialize(pendingExecutions.size() - 1U, *trigger)) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        Publish();
        return DebugReductionAction::None;
    }

    template <typename Execution>
    [[nodiscard]] bool StartAction(
        Execution& execution,
        const ActionStartedPayload& started)
    {
        if (execution.result.has_value()
            || execution.program == nullptr
            || started.instructionIndex
                >= execution.program->actionInstructions.size()) {
            return false;
        }
        execution.currentInstructionIndex = started.instructionIndex;
        AppendRecent(
            execution.recentInstructionIndices,
            started.instructionIndex,
            3U);
        return true;
    }

    [[nodiscard]] DebugReductionAction AcceptActionStarted(
        const Message& message)
    {
        const ActionStartedPayload& started = message.actionStarted;
        DebugRuleExecution* execution = FindExecution(started.executionMarker);
        if (execution != nullptr) {
            if (!StartAction(*execution, started)) {
                return Recover(DebugClientFault::InconsistentState);
            }
            Publish();
            return DebugReductionAction::None;
        }
        PendingExecution* pending = FindPending(started.executionMarker);
        if (pending == nullptr) {
            return Recover(DebugClientFault::UnknownExecutionMarker);
        }
        if (!StartAction(*pending, started)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        Publish();
        return DebugReductionAction::None;
    }

    template <typename Execution>
    [[nodiscard]] bool EndExecution(
        Execution& execution,
        const ExecutionEndedPayload& ended)
    {
        if (execution.result.has_value()) {
            return false;
        }
        execution.currentInstructionIndex.reset();
        execution.result = ended.result;
        return true;
    }

    [[nodiscard]] DebugReductionAction AcceptExecutionEnded(
        const Message& message)
    {
        const ExecutionEndedPayload& ended = message.executionEnded;
        DebugRuleExecution* execution = FindExecution(ended.executionMarker);
        if (execution != nullptr) {
            if (!EndExecution(
                    *execution,
                    ended)) {
                return Recover(DebugClientFault::InconsistentState);
            }
            Publish();
            return DebugReductionAction::None;
        }
        PendingExecution* pending = FindPending(ended.executionMarker);
        if (pending == nullptr) {
            return Recover(DebugClientFault::UnknownExecutionMarker);
        }
        if (!EndExecution(
                *pending,
                ended)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        Publish();
        return DebugReductionAction::None;
    }

    [[nodiscard]] DebugReductionAction AcceptRuntimeIssue(
        const Message& message)
    {
        if (capacities.maximumRuntimeIssues == 0U) {
            return Recover(DebugClientFault::CapacityExceeded);
        }
        std::int64_t captureUnixTimeMilliseconds{};
        if (!CaptureUnixTime(
                message.header.captureTimeNanoseconds,
                captureUnixTimeMilliseconds)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        AppendRecent(
            state.runtimeIssues,
            DebugRuntimeIssue{
                message.header.captureTimeNanoseconds,
                captureUnixTimeMilliseconds,
                message.runtimeIssue},
            capacities.maximumRuntimeIssues);
        if (message.runtimeIssue.code == IssueCode::DebugStreamOverflow) {
            return Recover(DebugClientFault::DebugStreamLost);
        }
        Publish();
        return DebugReductionAction::None;
    }

    [[nodiscard]] DebugReductionAction Accept(const Message& message)
    {
        if (!state.connected) {
            return DebugReductionAction::None;
        }
        if (message.header.targetSessionId != state.targetSessionId) {
            return Recover(DebugClientFault::SessionMismatch);
        }
        if (sequenceUnknown
            && recoveryPending
            && message.header.kind == MessageKind::CaptureStarted) {
            nextProtocolSequence = message.header.protocolSequence ==
                    (std::numeric_limits<std::uint64_t>::max)()
                ? message.header.protocolSequence
                : message.header.protocolSequence + 1U;
            sequenceUnknown = false;
        } else if (message.header.protocolSequence != nextProtocolSequence) {
            nextProtocolSequence = (std::max)(
                nextProtocolSequence,
                message.header.protocolSequence ==
                        (std::numeric_limits<std::uint64_t>::max)()
                    ? message.header.protocolSequence
                    : message.header.protocolSequence + 1U);
            return Recover(DebugClientFault::ProtocolSequenceMismatch);
        }
        if (!sequenceUnknown
            && message.header.protocolSequence
                != (std::numeric_limits<std::uint64_t>::max)()) {
            nextProtocolSequence = message.header.protocolSequence + 1U;
        }
        if (!state.captureRequested) {
            return DebugReductionAction::None;
        }
        if (message.header.kind == MessageKind::CaptureStarted) {
            return AcceptCaptureStarted(message);
        }
        if (!state.capturing || recoveryPending) {
            return DebugReductionAction::None;
        }
        if (message.header.captureEpoch != state.captureEpoch) {
            return Recover(DebugClientFault::CaptureEpochMismatch);
        }
        switch (message.header.kind) {
        case MessageKind::InputEvent:
            return AcceptInput(message);
        case MessageKind::RuleMatched:
            return AcceptRuleMatched(message);
        case MessageKind::ActionStarted:
            return AcceptActionStarted(message);
        case MessageKind::ExecutionEnded:
            return AcceptExecutionEnded(message);
        case MessageKind::RuntimeIssue:
            return AcceptRuntimeIssue(message);
        case MessageKind::Hello:
        case MessageKind::HelloAccepted:
        case MessageKind::StartCapture:
        case MessageKind::StopCapture:
        case MessageKind::RequestExecutorStop:
        case MessageKind::CaptureStarted:
            return Recover(DebugClientFault::InconsistentState);
        }
        return Recover(DebugClientFault::InconsistentState);
    }

    DebugClientCapacities capacities;
    mutable std::mutex mutex;
    DebugClientState state;
    std::shared_ptr<const DebugClientState> published;
    std::vector<PendingExecution> pendingExecutions;
    std::uint64_t nextProtocolSequence{1U};
    std::uint64_t lastInputSequence{};
    std::size_t storedInstructions{};
    std::int64_t captureStartTimeNanoseconds{};
    std::int64_t captureStartUnixTimeMilliseconds{};
    bool recoveryPending{};
    bool sequenceUnknown{};
};

DebugStateReducer::DebugStateReducer(DebugClientCapacities capacities)
    : impl_(std::make_unique<Impl>(capacities))
{
}

DebugStateReducer::~DebugStateReducer() = default;

void DebugStateReducer::Connected(std::uint64_t targetSessionId)
{
    std::lock_guard lock(impl_->mutex);
    impl_->state = {};
    impl_->pendingExecutions.clear();
    impl_->storedInstructions = 0U;
    impl_->lastInputSequence = 0U;
    impl_->captureStartTimeNanoseconds = 0;
    impl_->captureStartUnixTimeMilliseconds = 0;
    impl_->nextProtocolSequence = 1U;
    impl_->recoveryPending = false;
    impl_->sequenceUnknown = false;
    impl_->state.connected = true;
    impl_->state.targetSessionId = targetSessionId;
    impl_->Publish();
}

void DebugStateReducer::CaptureRequested()
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->state.connected) {
        return;
    }
    impl_->state.captureRequested = true;
    impl_->state.capturing = false;
    impl_->state.captureTrusted = false;
    impl_->Publish();
}

void DebugStateReducer::CaptureStopped()
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->state.connected) {
        return;
    }
    impl_->state.captureRequested = false;
    impl_->state.capturing = false;
    impl_->state.captureTrusted = false;
    impl_->recoveryPending = false;
    impl_->Publish();
}

void DebugStateReducer::Disconnected(DebugClientFault fault)
{
    std::lock_guard lock(impl_->mutex);
    impl_->state.connected = false;
    impl_->state.captureRequested = false;
    impl_->state.capturing = false;
    impl_->state.captureTrusted = false;
    if (fault != DebugClientFault::None) {
        impl_->state.lastFault = fault;
    }
    impl_->recoveryPending = false;
    impl_->Publish();
}

DebugReductionAction DebugStateReducer::Accept(const Message& message)
{
    std::lock_guard lock(impl_->mutex);
    try {
        return impl_->Accept(message);
    } catch (...) {
        try {
            return impl_->Recover(DebugClientFault::CapacityExceeded);
        } catch (...) {
            return DebugReductionAction::None;
        }
    }
}

DebugReductionAction DebugStateReducer::RejectFrame(
    const MessageHeader* header) noexcept
{
    try {
        std::lock_guard lock(impl_->mutex);
        if (header == nullptr) {
            impl_->sequenceUnknown = true;
        } else if (header->targetSessionId != impl_->state.targetSessionId) {
            return impl_->Recover(DebugClientFault::SessionMismatch);
        } else if (header->protocolSequence
            == (std::numeric_limits<std::uint64_t>::max)()) {
            impl_->sequenceUnknown = true;
        } else {
            impl_->nextProtocolSequence = header->protocolSequence + 1U;
        }
        return impl_->Recover(DebugClientFault::CorruptFrame);
    } catch (...) {
        return DebugReductionAction::None;
    }
}

std::shared_ptr<const DebugClientState> DebugStateReducer::ReadState() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->published;
}

} // namespace inputweaver::debug
