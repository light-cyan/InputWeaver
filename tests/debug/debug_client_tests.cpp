#include "debug/debug_client.hpp"
#include "mouse_debug_fixture.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int gFailureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++gFailureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

[[nodiscard]] inputweaver::debug::Message MakeMessage(
    inputweaver::debug::MessageKind kind,
    std::uint64_t sequence,
    std::uint64_t epoch = 1U,
    std::uint64_t session = 17U)
{
    inputweaver::debug::Message message{};
    message.header.kind = kind;
    message.header.targetSessionId = session;
    message.header.captureEpoch = epoch;
    message.header.protocolSequence = sequence;
    message.header.captureTimeNanoseconds = static_cast<std::int64_t>(
        sequence * 1'000'000U);
    if (kind == inputweaver::debug::MessageKind::CaptureStarted) {
        message.captureStarted.captureUnixTimeMilliseconds =
            1'725'000'000'000LL;
    }
    return message;
}

[[nodiscard]] inputweaver::debug::Message MakeInput(
    std::uint64_t protocolSequence,
    std::uint64_t inputSequence,
    std::uint32_t virtualKey,
    inputweaver::Transition transition,
    inputweaver::InputOrigin origin,
    std::uint64_t epoch = 1U)
{
    auto message = MakeMessage(
        inputweaver::debug::MessageKind::InputEvent,
        protocolSequence,
        epoch);
    message.inputEvent.inputSequence = inputSequence;
    message.inputEvent.device = inputweaver::DeviceKind::Keyboard;
    message.inputEvent.transition = transition;
    message.inputEvent.origin = origin;
    message.inputEvent.disposition = origin == inputweaver::InputOrigin::InitialSample
        ? inputweaver::debug::InputDisposition::NotApplicable
        : inputweaver::debug::InputDisposition::Forward;
    message.inputEvent.virtualKey = virtualKey;
    message.inputEvent.scanCode = virtualKey;
    return message;
}

void BeginCapture(
    inputweaver::debug::DebugStateReducer& reducer,
    std::uint64_t epoch = 1U)
{
    reducer.Connected(17U);
    reducer.CaptureRequested();
    Check(
        reducer.Accept(MakeMessage(
            inputweaver::debug::MessageKind::CaptureStarted,
            1U,
            epoch)) == inputweaver::debug::DebugReductionAction::None,
        "capture starts");
}

void TestOriginsAndPressedState()
{
    using inputweaver::InputOrigin;
    Check(
        inputweaver::debug::InputOriginLabel(InputOrigin::PhysicalCandidate)
            == "PHY",
        "physical origin label is PHY");
    Check(
        inputweaver::debug::InputOriginLabel(
            InputOrigin::CurrentInstanceInjected) == "ECHO",
        "current injection origin label is ECHO");
    Check(
        inputweaver::debug::InputOriginLabel(InputOrigin::ExternalInjected)
            == "EXT",
        "external injection origin label is EXT");
    Check(
        inputweaver::debug::InputOriginLabel(InputOrigin::InitialSample)
            == "INIT",
        "initial sample origin label is INIT");

    inputweaver::debug::DebugStateReducer reducer;
    BeginCapture(reducer);
    const auto captureState = reducer.ReadState();
    Check(
        reducer.Accept(MakeInput(
            2U,
            1U,
            65U,
            inputweaver::Transition::Down,
            InputOrigin::InitialSample))
            == inputweaver::debug::DebugReductionAction::None,
        "INIT Down is accepted");
    auto state = reducer.ReadState();
    Check(
        state != captureState && state->version > captureState->version
            && captureState->recentInputEvents.empty(),
        "ReadState publishes an immutable versioned snapshot");
    Check(
        state->pressedControls.size() == 1U
            && state->pressedControls[0].origin == InputOrigin::InitialSample,
        "INIT creates initial pressed state");

    Check(
        reducer.Accept(MakeInput(
            3U,
            2U,
            65U,
            inputweaver::Transition::Down,
            InputOrigin::PhysicalCandidate))
            == inputweaver::debug::DebugReductionAction::None,
        "concrete Down replaces INIT");
    state = reducer.ReadState();
    Check(
        state->pressedControls.size() == 1U
            && state->pressedControls[0].origin
                == InputOrigin::PhysicalCandidate,
        "concrete origin replaces INIT state");
    Check(
        state->recentInputEvents.back().againDown,
        "first concrete Down after INIT is again");

    Check(
        reducer.Accept(MakeInput(
            4U,
            3U,
            65U,
            inputweaver::Transition::Down,
            InputOrigin::PhysicalCandidate))
            == inputweaver::debug::DebugReductionAction::None,
        "again concrete Down is accepted");
    state = reducer.ReadState();
    Check(
        state->recentInputEvents.back().againDown,
        "same-origin Down is again");

    Check(
        reducer.Accept(MakeInput(
            5U,
            4U,
            65U,
            inputweaver::Transition::Up,
            InputOrigin::PhysicalCandidate))
            == inputweaver::debug::DebugReductionAction::None,
        "concrete Up is accepted");
    state = reducer.ReadState();
    Check(
        state->pressedControls.empty()
            && !state->recentInputEvents.back().unmatchedUp,
        "matched Up clears pressed state");

    Check(
        reducer.Accept(MakeInput(
            6U,
            5U,
            66U,
            inputweaver::Transition::Down,
            InputOrigin::InitialSample))
            == inputweaver::debug::DebugReductionAction::None,
        "second INIT Down is accepted");
    Check(
        reducer.Accept(MakeInput(
            7U,
            6U,
            66U,
            inputweaver::Transition::Up,
            InputOrigin::ExternalInjected))
            == inputweaver::debug::DebugReductionAction::None,
        "first concrete Up removes INIT");
    Check(
        reducer.ReadState()->pressedControls.empty()
            && !reducer.ReadState()->recentInputEvents.back().unmatchedUp,
        "concrete Up clears temporary INIT state");

    Check(
        reducer.Accept(MakeInput(
            8U,
            7U,
            67U,
            inputweaver::Transition::Down,
            InputOrigin::CurrentInstanceInjected))
            == inputweaver::debug::DebugReductionAction::None,
        "ordinary Down without INIT is accepted");
    state = reducer.ReadState();
    Check(
        !state->recentInputEvents.back().againDown
            && state->pressedControls.back().origin
                == InputOrigin::CurrentInstanceInjected,
        "ordinary first Down is not again");

    Check(
        reducer.Accept(MakeInput(
            9U,
            8U,
            68U,
            inputweaver::Transition::Up,
            InputOrigin::ExternalInjected))
            == inputweaver::debug::DebugReductionAction::None,
        "Up without a matching pressed origin is accepted");
    state = reducer.ReadState();
    Check(
        state->recentInputEvents.back().unmatchedUp
            && state->pressedControls.size() == 1U,
        "unmatched Up is classified without changing pressed state");
}

[[nodiscard]] inputweaver::debug::Message MakeMatched(
    std::uint64_t protocolSequence,
    std::uint64_t marker,
    std::uint64_t triggerInputSequence)
{
    auto message = MakeMessage(
        inputweaver::debug::MessageKind::RuleMatched,
        protocolSequence);
    message.ruleMatched.executionMarker = marker;
    message.ruleMatched.triggerInputSequence = triggerInputSequence;
    message.ruleMatched.conditionText = "combat == on";
    message.ruleMatched.actionText =
        "wait(10ms) wait(20ms) wait(30ms) wait(40ms)";
    return message;
}

[[nodiscard]] inputweaver::debug::Message MakeEnded(
    std::uint64_t protocolSequence,
    std::uint64_t marker,
    inputweaver::RuntimeExecutionResult result)
{
    auto message = MakeMessage(
        inputweaver::debug::MessageKind::ExecutionEnded,
        protocolSequence);
    message.executionEnded.executionMarker = marker;
    message.executionEnded.result = result;
    return message;
}

[[nodiscard]] const inputweaver::debug::DebugRuleExecution* FindExecution(
    const inputweaver::debug::DebugClientState& state,
    std::uint64_t marker)
{
    for (const auto& execution : state.ruleExecutions) {
        if (execution.executionMarker == marker) {
            return &execution;
        }
    }
    return nullptr;
}

void TestRuleCorrelationAndInterleaving()
{
    inputweaver::debug::DebugStateReducer reducer;
    BeginCapture(reducer);
    Check(
        reducer.Accept(MakeMatched(2U, 11U, 1U))
            == inputweaver::debug::DebugReductionAction::None,
        "first RuleMatched waits for its input");
    Check(
        reducer.Accept(MakeMatched(3U, 22U, 2U))
            == inputweaver::debug::DebugReductionAction::None,
        "second marker interleaves");
    Check(
        reducer.ReadState()->ruleExecutions.empty(),
        "pending executions remain isolated until trigger association");
    Check(
        reducer.Accept(MakeEnded(
            4U,
            11U,
            inputweaver::RuntimeExecutionResult::Completed))
            == inputweaver::debug::DebugReductionAction::None,
        "pending execution completes");
    Check(
        reducer.Accept(MakeEnded(
            5U,
            22U,
            inputweaver::RuntimeExecutionResult::Failed))
            == inputweaver::debug::DebugReductionAction::None,
        "interleaved execution fails independently");
    Check(
        reducer.ReadState()->ruleExecutions.empty(),
        "entries are not formed before trigger inputs arrive");

    Check(
        reducer.Accept(MakeInput(
            6U,
            1U,
            65U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::PhysicalCandidate))
            == inputweaver::debug::DebugReductionAction::None,
        "first trigger input materializes its execution");
    Check(
        reducer.Accept(MakeInput(
            7U,
            2U,
            66U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::ExternalInjected))
            == inputweaver::debug::DebugReductionAction::None,
        "second trigger input materializes its execution");
    auto state = reducer.ReadState();
    const auto* first = FindExecution(*state, 11U);
    const auto* second = FindExecution(*state, 22U);
    Check(
        first != nullptr
            && first->matchedUnixTimeMilliseconds == 1'725'000'000'001LL
            && first->triggerInput.captureTimeNanoseconds == 6'000'000
            && first->triggerInput.captureUnixTimeMilliseconds
                == 1'725'000'000'005LL
            && first->conditionText == "combat == on"
            && first->actionText
                == "wait(10ms) wait(20ms) wait(30ms) wait(40ms)",
        "execution keeps display times and readable condition and action data");
    Check(
        first != nullptr && first->result
                == inputweaver::RuntimeExecutionResult::Completed,
        "execution keeps its completed result");
    Check(
        second != nullptr
            && second->result == inputweaver::RuntimeExecutionResult::Failed,
        "interleaved marker keeps an independent failed result");

    Check(
        reducer.Accept(MakeMatched(8U, 33U, 3U))
            == inputweaver::debug::DebugReductionAction::None,
        "third execution waits for input");
    Check(
        reducer.Accept(MakeInput(
            9U,
            3U,
            67U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::CurrentInstanceInjected))
            == inputweaver::debug::DebugReductionAction::None,
        "active execution receives trigger input");
    state = reducer.ReadState();
    auto* third = FindExecution(*state, 33U);
    Check(
        third != nullptr && !third->result.has_value(),
        "execution remains visible while active");
    Check(
        reducer.Accept(MakeEnded(
            10U,
            33U,
            inputweaver::RuntimeExecutionResult::Cancelled))
            == inputweaver::debug::DebugReductionAction::None,
        "cancelled result is accepted");
    state = reducer.ReadState();
    third = FindExecution(*state, 33U);
    Check(
        third != nullptr
            && third->result == inputweaver::RuntimeExecutionResult::Cancelled,
        "cancelled result is retained");
}

void TestIssuesAndRecovery()
{
    inputweaver::debug::DebugStateReducer reducer;
    BeginCapture(reducer, 4U);
    auto issue = MakeMessage(
        inputweaver::debug::MessageKind::RuntimeIssue,
        2U,
        4U);
    issue.runtimeIssue.issue.kind =
        inputweaver::RuntimeDiagnosticKind::TaskActionFault;
    Check(
        reducer.Accept(issue) == inputweaver::debug::DebugReductionAction::None,
        "runtime diagnostic is retained");
    auto state = reducer.ReadState();
    Check(
        state->runtimeIssues.size() == 1U && state->captureTrusted,
        "ordinary runtime issue does not invalidate the stream");

    issue.header.protocolSequence = 3U;
    issue.runtimeIssue.code = inputweaver::debug::IssueCode::DebugStreamOverflow;
    issue.runtimeIssue.droppedRecords = 9U;
    Check(
        reducer.Accept(issue)
            == inputweaver::debug::DebugReductionAction::RestartCapture,
        "stream loss requests a new capture");
    state = reducer.ReadState();
    Check(
        !state->captureTrusted && !state->capturing
            && state->runtimeIssues.size() == 2U
            && state->lastFault
                == inputweaver::debug::DebugClientFault::DebugStreamLost,
        "stream loss preserves issue history and marks state untrusted");

    Check(
        reducer.Accept(MakeMessage(
            inputweaver::debug::MessageKind::CaptureStarted,
            4U,
            5U)) == inputweaver::debug::DebugReductionAction::None,
        "fresh CaptureStarted restores trust");
    state = reducer.ReadState();
    Check(
        state->captureTrusted && state->capturing
            && state->captureEpoch == 5U
            && state->runtimeIssues.empty(),
        "fresh capture clears prior-cycle derived state");

    Check(
        reducer.Accept(MakeInput(
            6U,
            1U,
            70U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::PhysicalCandidate,
            5U)) == inputweaver::debug::DebugReductionAction::RestartCapture,
        "protocol sequence gap requests capture restart");
    state = reducer.ReadState();
    Check(
        state->lastFault
            == inputweaver::debug::DebugClientFault::ProtocolSequenceMismatch,
        "protocol sequence fault is published");
}

void TestStrictValidationAndCapacity()
{
    inputweaver::debug::DebugStateReducer reducer;
    BeginCapture(reducer);
    auto wrongSession = MakeInput(
        2U,
        1U,
        65U,
        inputweaver::Transition::Down,
        inputweaver::InputOrigin::PhysicalCandidate);
    wrongSession.header.targetSessionId = 18U;
    Check(
        reducer.Accept(wrongSession)
            == inputweaver::debug::DebugReductionAction::RestartCapture,
        "target session mismatch requests restart");
    Check(
        reducer.ReadState()->lastFault
            == inputweaver::debug::DebugClientFault::SessionMismatch,
        "session mismatch fault is published");

    inputweaver::debug::DebugStateReducer epochReducer;
    BeginCapture(epochReducer);
    Check(
        epochReducer.Accept(MakeInput(
            2U,
            1U,
            65U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::PhysicalCandidate,
            2U)) == inputweaver::debug::DebugReductionAction::RestartCapture,
        "capture epoch mismatch requests restart");
    Check(
        epochReducer.ReadState()->lastFault
            == inputweaver::debug::DebugClientFault::CaptureEpochMismatch,
        "capture epoch fault is published");

    inputweaver::debug::DebugStateReducer markerReducer;
    BeginCapture(markerReducer);
    Check(
        markerReducer.Accept(MakeEnded(
            2U,
            999U,
            inputweaver::RuntimeExecutionResult::Completed))
            == inputweaver::debug::DebugReductionAction::RestartCapture,
        "unknown marker requests restart");
    Check(
        markerReducer.ReadState()->lastFault
            == inputweaver::debug::DebugClientFault::UnknownExecutionMarker,
        "unknown marker fault is published");

    inputweaver::debug::DebugClientCapacities capacities{};
    capacities.maximumPressedControls = 1U;
    inputweaver::debug::DebugStateReducer capacityReducer(capacities);
    BeginCapture(capacityReducer);
    Check(
        capacityReducer.Accept(MakeInput(
            2U,
            1U,
            65U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::InitialSample))
            == inputweaver::debug::DebugReductionAction::None,
        "first pressed control fits capacity");
    Check(
        capacityReducer.Accept(MakeInput(
            3U,
            2U,
            66U,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::InitialSample))
            == inputweaver::debug::DebugReductionAction::RestartCapture,
        "pressed-state capacity overflow requests restart");
    Check(
        capacityReducer.ReadState()->lastFault
            == inputweaver::debug::DebugClientFault::CapacityExceeded,
        "capacity fault is published");

    inputweaver::debug::DebugStateReducer corruptReducer;
    BeginCapture(corruptReducer);
    Check(
        corruptReducer.RejectFrame()
            == inputweaver::debug::DebugReductionAction::RestartCapture,
        "corrupt frame requests restart");
    Check(
        !corruptReducer.ReadState()->captureTrusted,
        "corrupt frame invalidates current state");
}

void TestValueState()
{
    inputweaver::debug::DebugStateReducer reducer;
    reducer.Connected(17U);
    reducer.CaptureRequested();
    auto started = MakeMessage(
        inputweaver::debug::MessageKind::CaptureStarted,
        1U);
    started.captureStarted.values = {
        {"PAUSE", {inputweaver::ValueType::State, true, 0.0, {}}},
        {"combat", {inputweaver::ValueType::State, false, 0.0, {}}},
        {"count", {inputweaver::ValueType::Number, false, 1.0, {}}},
    };
    Check(
        reducer.Accept(started)
            == inputweaver::debug::DebugReductionAction::None,
        "capture accepts its complete value snapshot");
    auto state = reducer.ReadState();
    Check(
        state->values.size() == 3U
            && state->values[0].name == "PAUSE"
            && state->values[0].value.stateValue
            && state->values[1].name == "combat"
            && !state->values[1].value.stateValue
            && state->values[2].value.numberValue == 1.0,
        "value names, types, and initial values enter derived state");

    auto changed = MakeMessage(
        inputweaver::debug::MessageKind::StateChanged,
        2U);
    changed.stateChanged.valueIndex = 1U;
    changed.stateChanged.value = {
        inputweaver::ValueType::State,
        true,
        0.0,
        {}};
    Check(
        reducer.Accept(changed)
            == inputweaver::debug::DebugReductionAction::None,
        "matching value update is accepted");
    state = reducer.ReadState();
    Check(
        state->values[1].value.stateValue,
        "value update is published in derived state");

    changed.header.protocolSequence = 3U;
    changed.stateChanged.value.type = inputweaver::ValueType::Number;
    Check(
        reducer.Accept(changed)
            == inputweaver::debug::DebugReductionAction::RestartCapture,
        "value type drift requests a fresh capture");
}

void TestArrayState()
{
    using namespace inputweaver;
    debug::DebugStateReducer reducer;
    reducer.Connected(17U);
    reducer.CaptureRequested();
    auto started = MakeMessage(debug::MessageKind::CaptureStarted, 1U);
    debug::DebugArrayValue initial{};
    initial.elementType = ArrayElementType::State;
    initial.length = 2U;
    initial.prefixCount = 2U;
    initial.elements[0].stateValue = true;
    started.captureStarted.arrays.push_back({"gates", initial});
    Check(
        reducer.Accept(started) == debug::DebugReductionAction::None,
        "capture accepts its complete array snapshot");
    auto state = reducer.ReadState();
    Check(
        state->arrays.size() == 1U
            && state->arrays[0].name == "gates"
            && state->arrays[0].value.length == 2U,
        "array name, type, length, and preview enter derived state");

    auto changed = MakeMessage(debug::MessageKind::ArrayChanged, 2U);
    changed.arrayChanged.arrayIndex = 0U;
    changed.arrayChanged.value = initial;
    changed.arrayChanged.value.length = 3U;
    changed.arrayChanged.value.prefixCount = 3U;
    changed.arrayChanged.value.elements[2].stateValue = true;
    Check(
        reducer.Accept(changed) == debug::DebugReductionAction::None
            && reducer.ReadState()->arrays[0].value.length == 3U,
        "array change replaces one complete bounded snapshot");

    changed.header.protocolSequence = 3U;
    changed.arrayChanged.value.elementType = ArrayElementType::Number;
    Check(
        reducer.Accept(changed) == debug::DebugReductionAction::RestartCapture,
        "array element type drift requests a fresh capture");
}

void TestStreamCompletion()
{
    inputweaver::debug::DebugStateReducer reducer;
    reducer.Connected(17U);
    auto completed = MakeMessage(
        inputweaver::debug::MessageKind::StreamCompleted,
        1U);
    Check(
        reducer.Accept(completed)
                == inputweaver::debug::DebugReductionAction::None
            && reducer.ReadState()->streamComplete,
        "terminal stream marker proves final snapshot completeness");

    auto trailing = MakeMessage(
        inputweaver::debug::MessageKind::StreamCompleted,
        2U);
    Check(
        reducer.Accept(trailing)
                == inputweaver::debug::DebugReductionAction::RestartCapture
            && !reducer.ReadState()->streamComplete,
        "messages after the terminal marker invalidate completeness");
}

#include "debug_mouse_client_tests.inc"

} // namespace

int main()
{
    TestOriginsAndPressedState();
    TestMouseStateAndCorrelation();
    TestMouseHistoryKeepsControls();
    TestRetainedTickCorrelation();
    TestRuleCorrelationAndInterleaving();
    TestIssuesAndRecovery();
    TestStrictValidationAndCapacity();
    TestValueState();
    TestArrayState();
    TestStreamCompletion();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " debug client test(s) failed.\n";
        return 1;
    }
    std::cout << "Debug client tests passed.\n";
    return 0;
}
