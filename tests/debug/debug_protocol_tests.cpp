#include "debug/debug_protocol.hpp"
#include "mouse_debug_fixture.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int gFailureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++gFailureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

[[nodiscard]] inputweaver::debug::DecodeResult RoundTrip(
    const inputweaver::debug::Message& message)
{
    std::vector<std::uint8_t> encoded;
    Check(
        inputweaver::debug::EncodeMessage(message, encoded),
        "message encodes");
    return inputweaver::debug::DecodeMessage(encoded);
}

void TestInputEvent()
{
    inputweaver::debug::Message message{};
    message.header.kind = inputweaver::debug::MessageKind::InputEvent;
    message.header.targetSessionId = 17U;
    message.header.captureEpoch = 4U;
    message.header.protocolSequence = 9U;
    message.header.captureTimeNanoseconds = 1234;
    message.inputEvent.inputSequence = 3U;
    message.inputEvent.device = inputweaver::DeviceKind::Keyboard;
    message.inputEvent.transition = inputweaver::Transition::Up;
    message.inputEvent.origin =
        inputweaver::InputOrigin::CurrentInstanceInjected;
    message.inputEvent.disposition =
        inputweaver::debug::InputDisposition::Forward;
    message.inputEvent.hasCompiledControl = true;
    message.inputEvent.compiledIdentity = {256U, 1U, 65U, 0U};
    message.inputEvent.virtualKey = 65U;
    message.inputEvent.scanCode = 30U;

    const auto decoded = RoundTrip(message);
    Check(decoded.Succeeded(), "input event decodes");
    Check(
        decoded.message.header.protocolSequence == 9U,
        "header sequence round trips");
    Check(
        decoded.message.inputEvent.origin
            == inputweaver::InputOrigin::CurrentInstanceInjected,
        "input origin round trips");
    Check(
        decoded.message.inputEvent.compiledIdentity
            == message.inputEvent.compiledIdentity,
        "compiled control identity round trips");
}

void TestCaptureStarted()
{
    inputweaver::debug::Message message{};
    message.header.kind = inputweaver::debug::MessageKind::CaptureStarted;
    message.header.captureTimeNanoseconds = 123'456'789;
    message.captureStarted.captureUnixTimeMilliseconds = 1'725'000'000'123LL;
    message.captureStarted.settings = {
        {45'000'000}, {0}, {123'456'789}, UINT64_MAX};
    message.captureStarted.values = {
        {"PAUSE", {inputweaver::ValueType::State, true, 0.0, {}}},
        {"count", {inputweaver::ValueType::Number, false, 2.5, {}}},
        {"delay", {
            inputweaver::ValueType::Duration,
            false,
            0.0,
            {80'000'000}}},
    };

    const auto decoded = RoundTrip(message);
    Check(
        inputweaver::debug::kProtocolVersion == 7U
            && decoded.Succeeded()
            && decoded.message.captureStarted.captureUnixTimeMilliseconds
                == 1'725'000'000'123LL
            && decoded.message.captureStarted.values.size() == 3U
            && decoded.message.captureStarted.values[0].value.stateValue
            && decoded.message.captureStarted.values[1].value.numberValue == 2.5
            && decoded.message.captureStarted.values[2].value.durationValue
                    .nanoseconds == 80'000'000,
        "capture wall-clock anchor and values round trip in protocol version 7");
    const auto& settings = decoded.message.captureStarted.settings;
    Check(settings.tapDuration.nanoseconds == 45'000'000
            && settings.actionGap.nanoseconds == 0
            && settings.mouseIdleTimeout.nanoseconds == 123'456'789
            && settings.randomSeed == UINT64_MAX,
        "capture preserves duration precision, zero gap, and the full unsigned seed");

    std::vector<std::uint8_t> encoded;
    Check(inputweaver::debug::EncodeMessage(message, encoded),
        "settings capture encodes for corruption checks");
    if (encoded.size() < 32U) return;
    for (std::size_t index = 0U; index < 3U; ++index) {
        auto corrupt = encoded;
        corrupt[corrupt.size() - 32U + index * 8U + 7U] |= 0x80U;
        Check(!inputweaver::debug::DecodeMessage(corrupt).Succeeded(),
            "negative builtin durations are rejected by the decoder");
    }
    encoded.pop_back();
    Check(!inputweaver::debug::DecodeMessage(encoded).Succeeded(),
        "truncated random seed is rejected");
    for (auto* duration : {
             &message.captureStarted.settings.tapDuration,
             &message.captureStarted.settings.actionGap,
             &message.captureStarted.settings.mouseIdleTimeout}) {
        const auto saved = *duration;
        duration->nanoseconds = -1;
        Check(!inputweaver::debug::EncodeMessage(message, encoded),
            "negative builtin durations are rejected by the encoder");
        *duration = saved;
    }
}

void TestRuleMatched()
{
    inputweaver::debug::Message message{};
    message.header.kind = inputweaver::debug::MessageKind::RuleMatched;
    message.header.targetSessionId = 22U;
    message.header.captureEpoch = 7U;
    message.ruleMatched.executionMarker = 11U;
    message.ruleMatched.triggerInputSequence = 5U;
    message.ruleMatched.conditionText = "combat == on and LCtrl == held";
    message.ruleMatched.actionText = "wait(80ms)";

    const auto decoded = RoundTrip(message);
    Check(decoded.Succeeded(), "rule match decodes");
    Check(
        decoded.message.ruleMatched.executionMarker == 11U,
        "execution marker round trips");
    Check(
        decoded.message.ruleMatched.conditionText
            == "combat == on and LCtrl == held"
            && decoded.message.ruleMatched.actionText == "wait(80ms)",
        "human-readable condition and action text round trip");
}

void TestTerminalAndIssueMessages()
{
    inputweaver::debug::Message completed{};
    completed.header.kind = inputweaver::debug::MessageKind::StreamCompleted;
    const auto decodedCompleted = RoundTrip(completed);
    Check(
        decodedCompleted.Succeeded()
            && decodedCompleted.message.header.kind
                == inputweaver::debug::MessageKind::StreamCompleted
            && decodedCompleted.message.header.payloadBytes == 0U,
        "stream completion marker round trips without a payload");

    inputweaver::debug::Message ended{};
    ended.header.kind = inputweaver::debug::MessageKind::ExecutionEnded;
    ended.executionEnded.executionMarker = 41U;
    ended.executionEnded.result = inputweaver::RuntimeExecutionResult::Failed;
    const auto decodedEnded = RoundTrip(ended);
    Check(
        decodedEnded.Succeeded()
            && decodedEnded.message.executionEnded.result
                == inputweaver::RuntimeExecutionResult::Failed,
        "failed execution result round trips");

    inputweaver::debug::Message issue{};
    issue.header.kind = inputweaver::debug::MessageKind::RuntimeIssue;
    issue.runtimeIssue.issue.kind =
        inputweaver::RuntimeDiagnosticKind::TaskBudgetExceeded;
    issue.runtimeIssue.issue.position = 7U;
    issue.runtimeIssue.issue.deadlineNanoseconds = 9000;
    const auto decodedDiagnostic = RoundTrip(issue);
    Check(
        decodedDiagnostic.Succeeded()
            && decodedDiagnostic.message.runtimeIssue.issue.kind
                == inputweaver::RuntimeDiagnosticKind::TaskBudgetExceeded
            && decodedDiagnostic.message.runtimeIssue.issue.position == 7U
            && decodedDiagnostic.message.runtimeIssue.issue.deadlineNanoseconds == 9000,
        "runtime issue context round trips");

    issue.runtimeIssue.code =
        inputweaver::debug::IssueCode::DebugStreamOverflow;
    issue.runtimeIssue.droppedRecords = 12U;
    const auto decodedIssue = RoundTrip(issue);
    Check(
        decodedIssue.Succeeded()
            && decodedIssue.message.runtimeIssue.droppedRecords == 12U,
        "stream overflow issue round trips");

    inputweaver::debug::Message changed{};
    changed.header.kind = inputweaver::debug::MessageKind::StateChanged;
    changed.stateChanged.valueIndex = 2U;
    changed.stateChanged.value = {
        inputweaver::ValueType::Duration,
        false,
        0.0,
        {250'000'000}};
    const auto decodedChanged = RoundTrip(changed);
    Check(
        decodedChanged.Succeeded()
            && decodedChanged.message.stateChanged.valueIndex == 2U
            && decodedChanged.message.stateChanged.value.durationValue.nanoseconds
                == 250'000'000,
        "state change payload round trips");
}

void TestArrayValues()
{
    using namespace inputweaver;
    debug::DebugArrayValue shortArray{};
    shortArray.elementType = ArrayElementType::State;
    shortArray.length = 2U;
    shortArray.prefixCount = 2U;
    shortArray.elements[0].stateValue = true;
    shortArray.elements[1].stateValue = false;
    debug::Message started{};
    started.header.kind = debug::MessageKind::CaptureStarted;
    started.captureStarted.captureUnixTimeMilliseconds = 1;
    started.captureStarted.arrays.push_back({"gates", shortArray});
    const auto decodedStarted = RoundTrip(started);
    Check(
        decodedStarted.Succeeded()
            && decodedStarted.message.captureStarted.arrays.size() == 1U
            && decodedStarted.message.captureStarted.arrays[0].name == "gates"
            && decodedStarted.message.captureStarted.arrays[0].value.length == 2U
            && decodedStarted.message.captureStarted.arrays[0]
                .value.elements[0].stateValue,
        "named short array snapshot round trips");

    debug::DebugArrayValue longArray{};
    longArray.elementType = ArrayElementType::Number;
    longArray.length = 12U;
    longArray.prefixCount = 4U;
    longArray.suffixCount = 4U;
    for (std::size_t index = 0U; index < longArray.elements.size(); ++index) {
        longArray.elements[index].numberValue = static_cast<double>(index) + 0.5;
    }
    debug::Message changed{};
    changed.header.kind = debug::MessageKind::ArrayChanged;
    changed.arrayChanged.arrayIndex = 3U;
    changed.arrayChanged.value = longArray;
    const auto decodedChanged = RoundTrip(changed);
    Check(
        decodedChanged.Succeeded()
            && decodedChanged.message.arrayChanged.arrayIndex == 3U
            && decodedChanged.message.arrayChanged.value.length == 12U
            && decodedChanged.message.arrayChanged.value.prefixCount == 4U
            && decodedChanged.message.arrayChanged.value.suffixCount == 4U
            && decodedChanged.message.arrayChanged.value.elements[7].numberValue
                == 7.5,
        "long array change carries exact length and bounded prefix and suffix");

    longArray.prefixCount = 3U;
    changed.arrayChanged.value = longArray;
    std::vector<std::uint8_t> invalid;
    Check(
        !debug::EncodeMessage(changed, invalid),
        "inconsistent long array preview is rejected");
}

void TestMalformedFrame()
{
    inputweaver::debug::Message message{};
    message.header.kind = inputweaver::debug::MessageKind::StartCapture;
    std::vector<std::uint8_t> encoded;
    Check(
        inputweaver::debug::EncodeMessage(message, encoded),
        "command encodes");
    encoded[0] ^= 0xffU;
    Check(
        inputweaver::debug::DecodeMessage(encoded).error
            == inputweaver::debug::DecodeError::InvalidMagic,
        "invalid magic is rejected");
    encoded.pop_back();
    Check(
        inputweaver::debug::DecodeMessage(encoded).error
            == inputweaver::debug::DecodeError::Truncated,
        "truncated header is rejected");
}

#include "debug_mouse_protocol_tests.inc"

} // namespace

int main()
{
    TestCaptureStarted();
    TestMouseMessages();
    TestInputEvent();
    TestRuleMatched();
    TestArrayValues();
    TestTerminalAndIssueMessages();
    TestMalformedFrame();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " debug protocol test(s) failed.\n";
        return 1;
    }
    std::cout << "Debug protocol tests passed.\n";
    return 0;
}
