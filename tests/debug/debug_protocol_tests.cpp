#include "debug/debug_protocol.hpp"

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

    const auto decoded = RoundTrip(message);
    Check(
        inputweaver::debug::kProtocolVersion == 2U
            && decoded.Succeeded()
            && decoded.message.captureStarted.captureUnixTimeMilliseconds
                == 1'725'000'000'123LL,
        "capture wall-clock anchor round trips in protocol version 2");
}

void TestRuleMatched()
{
    inputweaver::debug::Message message{};
    message.header.kind = inputweaver::debug::MessageKind::RuleMatched;
    message.header.targetSessionId = 22U;
    message.header.captureEpoch = 7U;
    message.ruleMatched.executionMarker = 11U;
    message.ruleMatched.triggerInputSequence = 5U;
    message.ruleMatched.eventTransition = inputweaver::EventTransition::Down;
    message.ruleMatched.eventControl = {1U, 7U, 4U, 0U};
    message.ruleMatched.conditionInstructions.push_back({
        inputweaver::ExpressionOpcode::PushBoolean,
        inputweaver::ExpressionType::Boolean,
        1U,
        0U});
    message.ruleMatched.actionInstructions.push_back({
        inputweaver::ActionOpcode::Wait,
        2U,
        0U});
    message.ruleMatched.actionInstructions.push_back({
        inputweaver::ActionOpcode::Jump,
        0U,
        0U});
    message.ruleMatched.actionInstructions.push_back({
        inputweaver::ActionOpcode::End,
        0U,
        0U});

    const auto decoded = RoundTrip(message);
    Check(decoded.Succeeded(), "rule match decodes");
    Check(
        decoded.message.ruleMatched.executionMarker == 11U,
        "execution marker round trips");
    Check(
        decoded.message.ruleMatched.conditionInstructions.size() == 1U,
        "condition program round trips");
    Check(
        decoded.message.ruleMatched.actionInstructions.size() == 3U,
        "complete action program round trips");
    Check(
        decoded.message.ruleMatched.actionInstructions[0].opcode
            == inputweaver::ActionOpcode::Wait,
        "wait instruction remains visible");
}

void TestTerminalAndIssueMessages()
{
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

} // namespace

int main()
{
    TestCaptureStarted();
    TestInputEvent();
    TestRuleMatched();
    TestTerminalAndIssueMessages();
    TestMalformedFrame();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " debug protocol test(s) failed.\n";
        return 1;
    }
    std::cout << "Debug protocol tests passed.\n";
    return 0;
}
