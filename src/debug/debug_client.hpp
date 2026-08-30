#pragma once

#include "debug_protocol.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::debug {

struct ProcessIdentity final {
    std::uint32_t processId{};

    auto operator<=>(const ProcessIdentity&) const = default;
};

enum class DebugClientError : std::uint8_t {
    None,
    InvalidProcessIdentity,
    InvalidDebugToken,
    ProcessUnavailable,
    PipeUnavailable,
    ProcessMismatch,
    HandshakeFailed,
    ProtocolRejected,
    NotConnected,
    IoFailure,
    AllocationFailure,
};

struct DebugClientResult final {
    DebugClientError error{DebugClientError::None};

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return error == DebugClientError::None;
    }
};

enum class DebugClientFault : std::uint8_t {
    None,
    ConnectionLost,
    CorruptFrame,
    SessionMismatch,
    ProtocolSequenceMismatch,
    CaptureEpochMismatch,
    UnknownExecutionMarker,
    DebugStreamLost,
    CapacityExceeded,
    InconsistentState,
};

struct DebugControlIdentity final {
    DeviceKind device{DeviceKind::Keyboard};
    bool hasCompiledControl{};
    ControlRef compiledIdentity{};
    std::uint32_t virtualKey{};
    std::uint32_t scanCode{};
    std::uint32_t nativeQualifier{};
    std::uint32_t mouseData{};
};

[[nodiscard]] bool SameDebugControl(
    const DebugControlIdentity& left,
    const DebugControlIdentity& right) noexcept;

[[nodiscard]] std::string_view InputOriginLabel(InputOrigin origin) noexcept;

struct DebugInputEvent final {
    std::uint64_t inputSequence{};
    std::int64_t captureTimeNanoseconds{};
    std::int64_t captureUnixTimeMilliseconds{};
    DebugControlIdentity control{};
    Transition transition{Transition::Down};
    InputOrigin origin{InputOrigin::PhysicalCandidate};
    InputDisposition disposition{InputDisposition::NotApplicable};
    bool againDown{};
    bool unmatchedUp{};
};

struct DebugPressedControl final {
    DebugControlIdentity control{};
    InputOrigin origin{InputOrigin::PhysicalCandidate};
};

struct DebugVariableState final {
    std::string name;
    DebugValue value{};
};

struct DebugArrayState final {
    std::string name;
    DebugArrayValue value{};
};

struct DebugRuleExecution final {
    std::uint64_t executionMarker{};
    DebugInputEvent triggerInput{};
    std::string conditionText;
    std::string actionText;
    std::int64_t matchedUnixTimeMilliseconds{};
    std::optional<RuntimeExecutionResult> result;
};

struct DebugRuntimeIssue final {
    std::int64_t captureTimeNanoseconds{};
    std::int64_t captureUnixTimeMilliseconds{};
    RuntimeIssuePayload payload{};
};

struct DebugClientState final {
    std::uint64_t version{};
    bool connected{};
    bool captureRequested{};
    bool capturing{};
    bool captureTrusted{};
    std::uint64_t targetSessionId{};
    std::uint64_t captureEpoch{};
    DebugClientFault lastFault{DebugClientFault::None};
    std::vector<DebugInputEvent> recentInputEvents;
    std::vector<DebugPressedControl> pressedControls;
    std::vector<DebugVariableState> values;
    std::vector<DebugArrayState> arrays;
    std::vector<DebugRuleExecution> ruleExecutions;
    std::vector<DebugRuntimeIssue> runtimeIssues;
};

struct DebugClientCapacities final {
    std::size_t maximumInputEvents{512U};
    std::size_t maximumPressedControls{256U};
    std::size_t maximumValues{kMaximumDebugValues};
    std::size_t maximumArrays{kMaximumDebugArrays};
    std::size_t maximumRuleExecutions{256U};
    std::size_t maximumPendingRuleExecutions{256U};
    std::size_t maximumRuntimeIssues{128U};
};

enum class DebugReductionAction : std::uint8_t {
    None,
    RestartCapture,
};

class DebugStateReducer final {
public:
    explicit DebugStateReducer(DebugClientCapacities capacities = {});
    ~DebugStateReducer();

    DebugStateReducer(const DebugStateReducer&) = delete;
    DebugStateReducer& operator=(const DebugStateReducer&) = delete;

    void Connected(std::uint64_t targetSessionId);
    void CaptureRequested();
    void CaptureStopped();
    void Disconnected(DebugClientFault fault = DebugClientFault::None);
    [[nodiscard]] DebugReductionAction Accept(const Message& message);
    [[nodiscard]] DebugReductionAction RejectFrame(
        const MessageHeader* header = nullptr) noexcept;
    [[nodiscard]] std::shared_ptr<const DebugClientState> ReadState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DebugClient {
public:
    virtual ~DebugClient() = default;
    [[nodiscard]] virtual DebugClientResult Connect(
        ProcessIdentity process,
        std::string_view debugToken) = 0;
    [[nodiscard]] virtual DebugClientResult StartCapture() = 0;
    [[nodiscard]] virtual DebugClientResult StopCapture() = 0;
    [[nodiscard]] virtual DebugClientResult RequestExecutorStop() = 0;
    [[nodiscard]] virtual std::shared_ptr<const DebugClientState> ReadState()
        const = 0;
    virtual void Disconnect() noexcept = 0;
};

} // namespace inputweaver::debug
