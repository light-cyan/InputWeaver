#pragma once

#include "input_event.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ukr {

enum class RuleId : unsigned char {
    None,
    F6ToF7,
    F7ToF8,
    F9ToMiddle,
    MiddleToF10
};

enum class RuleEvaluationKind : unsigned char {
    Forward,
    SuppressCaptured,
    ActionReady,
    EmergencyStop
};

struct RuleEvaluation {
    RuleEvaluationKind kind{ RuleEvaluationKind::Forward };
    RuleId rule{ RuleId::None };
    ActionBatch batch{};
    DeviceKind captureDevice{};
    ControlCode captureCode{};
};

[[nodiscard]] ActionBatch MakeTapActionBatch(
    unsigned long long sourceSequence,
    unsigned long long outputStateGeneration,
    ProcessId targetPid,
    DeviceKind outputDevice,
    ControlCode outputCode,
    bool requiresPointerTarget = false) noexcept;

[[nodiscard]] ActionBatch MakeRelativeMouseMoveBatch(
    unsigned long long sourceSequence,
    ProcessId targetPid,
    InputCoordinate valueX,
    InputCoordinate valueY) noexcept;

class FixedRuleEngine {
public:
    FixedRuleEngine() noexcept;

    FixedRuleEngine(const FixedRuleEngine&) = delete;
    FixedRuleEngine& operator=(const FixedRuleEngine&) = delete;

    [[nodiscard]] RuleEvaluation Evaluate(
        const InputEvent& event,
        bool diagnosticModeActive,
        ProcessId targetPid,
        unsigned long long sourceSequence) noexcept;

    // Call immediately after the corresponding ActionReady batch is accepted.
    [[nodiscard]] bool CommitCapture(const RuleEvaluation& evaluation) noexcept;

    void DisableNewCaptures() noexcept;
    void SeedPhysicalState(DeviceKind device, ControlCode code, bool down) noexcept;

    [[nodiscard]] bool NewCapturesEnabled() const noexcept;
    [[nodiscard]] bool HasCapturedInputs() const noexcept;

    [[nodiscard]] unsigned long long PackedOutputState(
        DeviceKind device,
        ControlCode code) const noexcept;

    [[nodiscard]] bool CanInject(const ActionBatch& batch) const noexcept;

private:
    static constexpr std::size_t kControlCodeCount = 256;
    static constexpr std::size_t kPublishedOutputCount = 4;

    struct PhysicalEdge {
        bool valid{};
        bool firstDown{};
    };

    [[nodiscard]] PhysicalEdge UpdatePhysicalState(const InputEvent& event) noexcept;
    [[nodiscard]] bool IsPhysicalDown(DeviceKind device, ControlCode code) const noexcept;
    [[nodiscard]] unsigned long long PhysicalGeneration(DeviceKind device, ControlCode code) const noexcept;
    [[nodiscard]] bool IsCaptured(DeviceKind device, ControlCode code) const noexcept;
    [[nodiscard]] RuleId CapturedRule(DeviceKind device, ControlCode code) const noexcept;
    void SetCaptured(DeviceKind device, ControlCode code, bool captured, RuleId rule) noexcept;
    void PublishOutputState(DeviceKind device, ControlCode code) noexcept;

    [[nodiscard]] bool ControlDown() const noexcept;
    [[nodiscard]] bool ShiftDown() const noexcept;

    [[nodiscard]] static std::size_t PublishedOutputIndex(
        DeviceKind device,
        ControlCode code) noexcept;

    std::array<bool, kControlCodeCount> keyboardDown_{};
    std::array<bool, kControlCodeCount> mouseDown_{};
    std::array<unsigned long long, kControlCodeCount> keyboardGeneration_{};
    std::array<unsigned long long, kControlCodeCount> mouseGeneration_{};
    std::array<bool, kControlCodeCount> keyboardCaptured_{};
    std::array<bool, kControlCodeCount> mouseCaptured_{};
    std::array<RuleId, kControlCodeCount> keyboardCapturedRule_{};
    std::array<RuleId, kControlCodeCount> mouseCapturedRule_{};
    std::array<std::atomic<unsigned long long>, kPublishedOutputCount> publishedOutputState_{};
    std::atomic<bool> newCapturesEnabled_{ true };
    std::atomic<std::size_t> capturedCount_{ 0 };
};

} // namespace ukr
