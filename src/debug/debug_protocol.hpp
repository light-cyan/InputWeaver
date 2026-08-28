#pragma once

#include "input/input_types.hpp"
#include "program/compiled_program.hpp"
#include "runtime/runtime_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace inputweaver::debug {

inline constexpr std::uint32_t kProtocolMagic = 0x42445749U;
inline constexpr std::uint16_t kProtocolVersion = 3U;
inline constexpr std::size_t kWireHeaderBytes = 44U;
inline constexpr std::uint32_t kMaximumFramePayloadBytes = 16U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumDebugInstructions = 262'144U;
inline constexpr std::uint32_t kMaximumDebugValues = 12'289U;
inline constexpr std::uint32_t kMaximumDebugTextBytes = 16U * 1024U * 1024U;

enum class MessageKind : std::uint16_t {
    Hello = 1U,
    HelloAccepted = 2U,
    StartCapture = 3U,
    StopCapture = 4U,
    RequestExecutorStop = 5U,
    CaptureStarted = 16U,
    InputEvent = 17U,
    RuleMatched = 18U,
    ActionStarted = 19U,
    ExecutionEnded = 20U,
    RuntimeIssue = 21U,
    StateChanged = 22U,
};

enum class InputDisposition : std::uint8_t {
    NotApplicable,
    Forward,
    Suppress,
};

enum class IssueCode : std::uint8_t {
    RuntimeDiagnostic,
    DebugStreamOverflow,
};

struct MessageHeader final {
    MessageKind kind{MessageKind::Hello};
    std::uint32_t payloadBytes{};
    std::uint64_t targetSessionId{};
    std::uint64_t captureEpoch{};
    std::uint64_t protocolSequence{};
    std::int64_t captureTimeNanoseconds{};
};

struct HelloPayload final {
    std::uint16_t minimumVersion{kProtocolVersion};
    std::uint16_t maximumVersion{kProtocolVersion};
};

struct HelloAcceptedPayload final {
    std::uint16_t selectedVersion{kProtocolVersion};
    std::uint32_t processId{};
};

struct DebugValue final {
    ValueType type{ValueType::State};
    bool stateValue{};
    double numberValue{};
    DurationValue durationValue{};
};

struct DebugNamedValue final {
    std::string name;
    DebugValue value{};
};

struct CaptureStartedPayload final {
    std::int64_t captureUnixTimeMilliseconds{};
    std::vector<DebugNamedValue> values;
};

struct InputEventPayload final {
    std::uint64_t inputSequence{};
    DeviceKind device{DeviceKind::Keyboard};
    Transition transition{Transition::Down};
    InputOrigin origin{InputOrigin::PhysicalCandidate};
    InputDisposition disposition{InputDisposition::NotApplicable};
    bool hasCompiledControl{};
    ControlRef compiledIdentity{};
    std::uint32_t virtualKey{};
    std::uint32_t scanCode{};
    std::uint32_t nativeQualifier{};
    std::uint32_t mouseData{};
};

struct RuleMatchedPayload final {
    std::uint64_t executionMarker{};
    std::uint64_t triggerInputSequence{};
    EventTransition eventTransition{EventTransition::Down};
    ControlRef eventControl{};
    std::string conditionText;
    std::string actionText;
    std::vector<ExpressionInstruction> conditionInstructions;
    std::vector<ActionInstruction> actionInstructions;
};

struct ActionStartedPayload final {
    std::uint64_t executionMarker{};
    std::uint32_t instructionIndex{kInvalidProgramIndex};
};

struct ExecutionEndedPayload final {
    std::uint64_t executionMarker{};
    RuntimeExecutionResult result{RuntimeExecutionResult::Completed};
};

struct RuntimeIssuePayload final {
    IssueCode code{IssueCode::RuntimeDiagnostic};
    RuntimeDebugIssue issue{};
    std::uint64_t droppedRecords{};
};

struct StateChangedPayload final {
    std::uint32_t valueIndex{kInvalidProgramIndex};
    DebugValue value{};
};

struct Message final {
    MessageHeader header{};
    HelloPayload hello{};
    HelloAcceptedPayload helloAccepted{};
    CaptureStartedPayload captureStarted{};
    InputEventPayload inputEvent{};
    RuleMatchedPayload ruleMatched{};
    ActionStartedPayload actionStarted{};
    ExecutionEndedPayload executionEnded{};
    RuntimeIssuePayload runtimeIssue{};
    StateChangedPayload stateChanged{};
};

enum class DecodeError : std::uint8_t {
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidMessageKind,
    PayloadTooLarge,
    InvalidPayload,
    TrailingPayload,
};

struct DecodeResult final {
    Message message{};
    DecodeError error{DecodeError::None};

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return error == DecodeError::None;
    }
};

[[nodiscard]] bool EncodeMessage(
    const Message& message,
    std::vector<std::uint8_t>& bytes);

[[nodiscard]] DecodeResult DecodeMessage(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] bool DecodeHeader(
    std::span<const std::uint8_t> bytes,
    MessageHeader& header,
    DecodeError& error) noexcept;

} // namespace inputweaver::debug
