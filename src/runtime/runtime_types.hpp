#pragma once

#include "input/input_types.hpp"
#include "program/compiled_program.hpp"
#include "support/callback_ref.hpp"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace inputweaver {

struct RuntimeValue final {
    ExpressionType type{ExpressionType::None};
    bool booleanValue{};
    std::uint8_t stateValue{};
    ControlState controlStateValue{ControlState::Idle};
    double numberValue{};
    DurationValue durationValue{};
};

enum class RuntimeEvaluationFault : std::uint8_t {
    None,
    InvalidExpression,
    InvalidInstruction,
    StackUnderflow,
    StackOverflow,
    TypeMismatch,
    DivisionByZero,
    NonFiniteNumber,
    InvalidDuration,
    InvalidArray,
    InvalidArrayIndex,
    ArrayBounds,
    MissingReturn,
};

struct RuntimeEvaluationResult final {
    RuntimeValue value{};
    RuntimeEvaluationFault fault{RuntimeEvaluationFault::None};
    std::uint32_t instructionPosition{};

    [[nodiscard]] bool Succeeded() const noexcept {
        return fault == RuntimeEvaluationFault::None;
    }
};

struct ActivatedControl final {
    std::uintptr_t backendToken{};
    std::uint8_t capabilities{};
    DeviceKind device{DeviceKind::Keyboard};
    bool requiresPointerTarget{};
    bool initialStateQueryable{};
};

enum class RuntimeControlBindResult : std::uint8_t {
    Bound,
    UnsupportedIdentity,
    MissingCapability,
};

class RuntimeControlPort {
public:
    virtual ~RuntimeControlPort() = default;
    virtual void BeginActivation() noexcept {}
    [[nodiscard]] virtual RuntimeControlBindResult BindControl(
        ControlRefId controlId,
        const ControlRef& control,
        std::uint8_t requiredUses,
        ActivatedControl& activated) noexcept = 0;
    virtual void CommitActivation() noexcept {}
    virtual void AbortActivation() noexcept {}
};

enum class RuntimeOutputTransition : std::uint8_t {
    Down,
    Again,
    Up,
};

struct RuntimeOutputRequest final {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    ControlRefId control{};
    ControlRef identity{};
    ActivatedControl activated{};
    RuntimeOutputTransition transition{};
};

enum class RuntimeOutputResult : std::uint8_t {
    Accepted,
    RouteRejected,
    CapacityRejected,
    Failed,
};

class RuntimeOutputPort {
public:
    virtual ~RuntimeOutputPort() = default;
    [[nodiscard]] virtual RuntimeOutputResult Publish(
        const RuntimeOutputRequest& request) noexcept = 0;
};

struct RuntimeInputEvent final {
    ControlRefId control{};
    DeviceKind device{DeviceKind::Keyboard};
    InputOrigin origin{InputOrigin::PhysicalCandidate};
    Transition transition{Transition::Down};
    ScreenPoint position{};
    std::uint64_t debugCaptureEpoch{};
    std::uint64_t debugInputSequence{};
};

class RuntimeRoutePort {
public:
    virtual ~RuntimeRoutePort() = default;
    [[nodiscard]] virtual bool ValidateTarget(
        TargetSelectorKind kind) noexcept = 0;
    [[nodiscard]] virtual bool TargetValid(
        TargetSelectorKind kind) noexcept
    {
        (void)kind;
        return true;
    }
    [[nodiscard]] virtual bool CanDispatch(
        TargetSelectorKind kind,
        const RuntimeInputEvent& event) noexcept = 0;
    [[nodiscard]] virtual bool CanInject(
        TargetSelectorKind kind,
        const ActivatedControl& control) noexcept = 0;
};

enum class RuntimeLaunchResult : std::uint8_t {
    Launched,
    Cancelled,
    InvalidCommand,
    ResolutionFailed,
    CreationFailed,
};

struct RuntimeLaunchOutcome final {
    RuntimeLaunchResult result{RuntimeLaunchResult::Launched};
    std::uint32_t platformError{};

    [[nodiscard]] bool Succeeded() const noexcept {
        return result == RuntimeLaunchResult::Launched;
    }
};

using RuntimeCancellationProbe = support::CallbackRef<bool() noexcept>;

class RuntimeProcessLauncher {
public:
    virtual ~RuntimeProcessLauncher() = default;
    [[nodiscard]] virtual bool Permitted() const noexcept = 0;
    [[nodiscard]] virtual RuntimeLaunchOutcome Launch(
        std::string_view command,
        RuntimeCancellationProbe cancellation) noexcept = 0;
};

class RuntimeClock {
public:
    virtual ~RuntimeClock() = default;
    [[nodiscard]] virtual std::int64_t NowNanoseconds() const noexcept = 0;
};

class SteadyRuntimeClock final : public RuntimeClock {
public:
    [[nodiscard]] std::int64_t NowNanoseconds() const noexcept override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    }
};

struct RuntimeCapacities final {
    std::uint32_t maximumControls{4096U};
    std::uint32_t maximumStateSlots{4096U};
    std::uint32_t maximumNumberSlots{4096U};
    std::uint32_t maximumDurationSlots{4096U};
    std::uint32_t maximumArrayCount{4096U};
    std::uint64_t maximumArrayBytes{64U * 1024U * 1024U};
    std::uint32_t maximumMappingSlots{4096U};
    std::uint32_t maximumExitRulesPerEvent{64U};
    std::uint32_t maximumPauseRulesPerEvent{64U};
    std::uint32_t maximumRulesPerEvent{256U};
    std::uint32_t maximumPredicateStepsPerEvent{4096U};
    std::uint32_t maximumMappingOperationsPerEvent{64U};
    std::uint32_t maximumExpressionStackDepth{1024U};
    std::uint32_t maximumRepeatFramesPerTask{256U};
    std::uint32_t maximumOwnedControlsPerTask{256U};
    std::uint32_t maximumTaskInstructionsWithoutSuspension{65536U};
    std::uint32_t maximumTaskOutputsWithoutSuspension{4096U};
    std::uint32_t maximumContinuouslyReadyQuanta{16U};
    std::int64_t continuouslyReadyBackoffNanoseconds{1'000'000LL};
    std::uint32_t maximumOutputTransitionsPerInterval{2048U};
    std::int64_t outputRateIntervalNanoseconds{1'000'000'000LL};
    std::uint32_t taskSlotCount{256U};
    std::uint32_t transactionQueueItemCount{1024U};
    std::uint32_t diagnosticRecordCount{256U};
    bool permitProcessLaunch{false};
};

enum class RuntimeActivationErrorCode : std::uint8_t {
    None,
    MissingProgram,
    ControlCapacity,
    ValueCapacity,
    ArrayCountCapacity,
    ArrayByteCapacity,
    MappingCapacity,
    ExitRuleCapacity,
    PauseRuleCapacity,
    RuleCapacity,
    PredicateStepCapacity,
    MappingOperationCapacity,
    ExpressionStackCapacity,
    RepeatFrameCapacity,
    OwnershipCapacity,
    TaskInstructionCapacity,
    TaskOutputCapacity,
    InvalidSchedulerConfiguration,
    InvalidOutputRateConfiguration,
    TaskCapacity,
    TransactionCapacity,
    DiagnosticCapacity,
    UnsupportedControl,
    MissingControlCapability,
    ProcessLaunchDenied,
    InvalidTarget,
    CleanupFailure,
    AllocationFailure,
};

enum class RuntimeActivationSubject : std::uint32_t {
    None,
    StateSlots,
    NumberSlots,
    DurationSlots,
    ArrayCount,
    ArrayBytes,
    MaximumContinuouslyReadyQuanta,
    ContinuouslyReadyBackoffNanoseconds,
    MaximumOutputTransitionsPerInterval,
    OutputRateIntervalNanoseconds,
};

struct RuntimeActivationError final {
    RuntimeActivationErrorCode code{RuntimeActivationErrorCode::None};
    std::uint32_t subject{};
    std::uint64_t required{};
    std::uint64_t available{};
};

struct RuntimeActivationResult final {
    bool activated{};
    RuntimeActivationError error{};
};

enum class RuntimeCancellationReason : std::uint8_t {
    Pause,
    Reload,
    TargetIneligible,
    TargetLoss,
    FatalFailure,
    Exit,
    Shutdown,
};

enum class RuntimeDiagnosticKind : std::uint8_t {
    ActivationFailure,
    TransactionCapacity,
    PredicateFault,
    TaskExpressionFault,
    TaskActionFault,
    LaunchFailure,
    OutputFailure,
    OwnershipChange,
    MappingChange,
    Cancellation,
    TargetEligibilityChange,
    PhysicalStateSynchronization,
    TaskBudgetExceeded,
    OutputRateExceeded,
};

[[nodiscard]] constexpr const char* RuntimeDiagnosticKindName(
    RuntimeDiagnosticKind kind) noexcept
{
    switch (kind) {
    case RuntimeDiagnosticKind::ActivationFailure: return "ActivationFailure";
    case RuntimeDiagnosticKind::TransactionCapacity: return "TransactionCapacity";
    case RuntimeDiagnosticKind::PredicateFault: return "PredicateFault";
    case RuntimeDiagnosticKind::TaskExpressionFault: return "TaskExpressionFault";
    case RuntimeDiagnosticKind::TaskActionFault: return "TaskActionFault";
    case RuntimeDiagnosticKind::LaunchFailure: return "LaunchFailure";
    case RuntimeDiagnosticKind::OutputFailure: return "OutputFailure";
    case RuntimeDiagnosticKind::OwnershipChange: return "OwnershipChange";
    case RuntimeDiagnosticKind::MappingChange: return "MappingChange";
    case RuntimeDiagnosticKind::Cancellation: return "Cancellation";
    case RuntimeDiagnosticKind::TargetEligibilityChange: return "TargetEligibilityChange";
    case RuntimeDiagnosticKind::PhysicalStateSynchronization: return "PhysicalStateSynchronization";
    case RuntimeDiagnosticKind::TaskBudgetExceeded: return "TaskBudgetExceeded";
    case RuntimeDiagnosticKind::OutputRateExceeded: return "OutputRateExceeded";
    }
    return "Unknown";
}

struct RuntimeDiagnosticRecord final {
    RuntimeDiagnosticKind kind{};
    std::uint64_t programSerial{};
    std::uint64_t generation{};
    std::uint64_t sequence{};
    SourceSpan source{};
    std::uint32_t subject{kInvalidProgramIndex};
    std::uint32_t position{kInvalidProgramIndex};
    std::int64_t deadlineNanoseconds{};
    std::uint32_t detail{};
    std::uint32_t platformError{};
    std::uint64_t required{};
    std::uint64_t available{};
};

enum class RuntimeExecutionResult : std::uint8_t {
    Completed,
    Failed,
    Cancelled,
};

enum class RuntimeDebugEventKind : std::uint8_t {
    RuleMatched,
    ExecutionEnded,
    RuntimeIssue,
    StateChanged,
    ArrayChanged,
};

struct RuntimeDebugValue final {
    ValueRefId reference{};
    ValueType type{ValueType::State};
    bool stateValue{};
    double numberValue{};
    DurationValue durationValue{};
};

struct RuntimeDebugArrayElement final {
    std::uint8_t stateValue{};
    double numberValue{};
};

inline constexpr std::size_t kRuntimeDebugArrayPreviewCount = 8U;

struct RuntimeDebugArraySnapshot final {
    ArrayId array{};
    ArrayElementType elementType{ArrayElementType::State};
    std::uint64_t length{};
    std::uint8_t prefixCount{};
    std::uint8_t suffixCount{};
    std::array<RuntimeDebugArrayElement, kRuntimeDebugArrayPreviewCount> elements{};
};

struct RuntimeDebugIssue final {
    RuntimeDiagnosticKind kind{};
    SourceSpan source{};
    std::uint32_t subject{kInvalidProgramIndex};
    std::uint32_t position{kInvalidProgramIndex};
    std::int64_t deadlineNanoseconds{};
    std::uint32_t detail{};
    std::uint32_t platformError{};
};

struct RuntimeDebugEvent final {
    RuntimeDebugEventKind kind{};
    std::uint64_t captureEpoch{};
    std::uint64_t executionMarker{};
    std::uint64_t triggerInputSequence{};
    std::uint32_t ruleIndex{kInvalidProgramIndex};
    RuntimeExecutionResult result{RuntimeExecutionResult::Completed};
    RuntimeDebugIssue issue{};
    RuntimeDebugValue value{};
    RuntimeDebugArraySnapshot array{};
};

class RuntimeDebugEventPort {
public:
    virtual ~RuntimeDebugEventPort() = default;
    [[nodiscard]] virtual bool Publish(
        const RuntimeDebugEvent& event) noexcept = 0;
};

struct RuntimeMetrics final {
    std::uint64_t dispatchedEvents{};
    std::uint64_t suppressedEvents{};
    std::uint64_t startedTasks{};
    std::uint64_t completedTasks{};
    std::uint64_t cancelledTasks{};
    std::uint64_t transactionRejections{};
    std::uint64_t droppedDiagnostics{};
    std::uint64_t outputTransitions{};
    std::uint64_t schedulerBackoffs{};
    std::uint64_t currentArrayBytes{};
    std::uint64_t peakArrayBytes{};
    std::uint64_t rejectedArrayGrowth{};
};

struct RuntimePumpResult final {
    std::size_t slices{};
    bool readyWorkRemaining{};
    bool timedWorkRemaining{};
};

} // namespace inputweaver
