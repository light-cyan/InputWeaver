#pragma once

#include "runtime_types.hpp"
#include "program/mouse_fields.hpp"

#include <span>
#include <vector>

namespace inputweaver {

struct MouseMeterConfig final {
    EventTransition transition{EventTransition::Move};
    ExpressionType periodType{ExpressionType::Number};
};

struct MouseMeterState final {
    MouseMeterConfig config{};
    MouseCycle current{};
    MouseCycle completed{};
    RuntimeEvaluationResult periodResult{};
    std::int64_t lastMoveNanoseconds{};
    bool opened{};
    bool hasOrigin{};
    bool moving{};
};

using MousePeriodEvaluator = support::CallbackRef<RuntimeEvaluationResult(MeterId) noexcept>;

class RuntimeMouseState final {
public:
    RuntimeMouseState(std::span<const MouseMeterConfig> sources, DurationValue idleTimeout,
        std::size_t maximumOccurrences = 1024);

    void Initialize(MousePoint position, std::int64_t now) noexcept;
    void Refresh(MousePoint position, std::int64_t now) noexcept;
    void Observe(const RuntimeInputEvent& event, std::int64_t now) noexcept;
    [[nodiscard]] bool Accumulate(const RuntimeInputEvent& event, std::int64_t now,
        MousePeriodEvaluator evaluatePeriod) noexcept;
    void Restart(MeterId source) noexcept;
    void ResetMeters() noexcept;
    void SelectCompleted(std::span<MouseCycle> destination,
        const MouseOccurrence* trigger = nullptr) const noexcept;
    [[nodiscard]] RuntimeEvaluationResult Read(MouseFieldReference reference,
        std::span<const MouseCycle> selected = {}) const noexcept;

    [[nodiscard]] const MouseObservation& Observation() const noexcept { return observation_; }
    [[nodiscard]] std::span<const MouseMeterState> Meters() const noexcept { return meters_; }
    [[nodiscard]] std::span<const MouseOccurrence> Occurrences() const noexcept { return occurrences_; }

private:
    [[nodiscard]] bool Open(MeterId id, MousePeriodEvaluator evaluator) noexcept;
    [[nodiscard]] bool Complete(MeterId id, MousePeriodEvaluator evaluator) noexcept;
    [[nodiscard]] bool Move(MeterId id, const RuntimeInputEvent& event,
        std::int64_t now, MousePeriodEvaluator evaluator) noexcept;
    [[nodiscard]] bool Wheel(MeterId id, const RuntimeInputEvent& event,
        MousePeriodEvaluator evaluator) noexcept;

    MouseObservation observation_{};
    std::vector<MouseMeterState> meters_;
    std::vector<MouseOccurrence> occurrences_;
    DurationValue idleTimeout_{};
    std::int64_t lastPhysicalMove_{};
    bool hasPhysicalMove_{};
    std::uint64_t nextSequence_{1};
};

} // namespace inputweaver
