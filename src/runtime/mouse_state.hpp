#pragma once

#include "runtime_types.hpp"
#include "program/event_fields.hpp"

#include <span>
#include <vector>

namespace inputweaver {

struct MousePoint final {
    double x{};
    double y{};
    auto operator<=>(const MousePoint&) const = default;
};

struct MouseObservation final {
    MousePoint position{};
    MouseDelta delta{};
    DurationValue idleTime{};
    bool moving{};
};

struct MouseCycle final {
    RuntimeValue period{};
    double progress{};
    std::int64_t elapsedNanoseconds{};
    MousePoint start{};
    MousePoint point{};
    MousePoint displacement{};
    double distance{};
    std::uint64_t sequence{};
};

struct MouseSourceConfig final {
    EventTransition transition{EventTransition::Move};
    ExpressionType periodType{ExpressionType::Number};
};

struct MouseSourceState final {
    MouseSourceConfig config{};
    MouseCycle current{};
    MouseCycle completed{};
    RuntimeEvaluationResult periodResult{};
    std::int64_t lastMoveNanoseconds{};
    bool opened{};
    bool hasOrigin{};
    bool moving{};
};

struct MouseOccurrence final {
    EventSourceId source{};
    MouseCycle cycle{};
};

using MousePeriodEvaluator = support::CallbackRef<RuntimeEvaluationResult(EventSourceId) noexcept>;

class RuntimeMouseState final {
public:
    RuntimeMouseState(std::span<const MouseSourceConfig> sources, DurationValue idleTimeout,
        std::size_t maximumOccurrences = 1024);

    void Initialize(MousePoint position, std::int64_t now) noexcept;
    void Refresh(MousePoint position, std::int64_t now) noexcept;
    void Observe(const RuntimeInputEvent& event, std::int64_t now) noexcept;
    [[nodiscard]] bool Accumulate(const RuntimeInputEvent& event, std::int64_t now,
        MousePeriodEvaluator evaluatePeriod) noexcept;
    void Restart(EventSourceId source) noexcept;
    void ResetSources() noexcept;
    void SelectCompleted(std::span<MouseCycle> destination,
        const MouseOccurrence* trigger = nullptr) const noexcept;
    [[nodiscard]] RuntimeEvaluationResult Read(EventFieldReference reference,
        std::span<const MouseCycle> selected = {}) const noexcept;

    [[nodiscard]] const MouseObservation& Observation() const noexcept { return observation_; }
    [[nodiscard]] std::span<const MouseSourceState> Sources() const noexcept { return sources_; }
    [[nodiscard]] std::span<const MouseOccurrence> Occurrences() const noexcept { return occurrences_; }

private:
    [[nodiscard]] bool Open(EventSourceId id, MousePeriodEvaluator evaluator) noexcept;
    [[nodiscard]] bool Complete(EventSourceId id, MousePeriodEvaluator evaluator) noexcept;
    [[nodiscard]] bool Move(EventSourceId id, const RuntimeInputEvent& event,
        std::int64_t now, MousePeriodEvaluator evaluator) noexcept;
    [[nodiscard]] bool Wheel(EventSourceId id, const RuntimeInputEvent& event,
        MousePeriodEvaluator evaluator) noexcept;

    MouseObservation observation_{};
    std::vector<MouseSourceState> sources_;
    std::vector<MouseOccurrence> occurrences_;
    DurationValue idleTimeout_{};
    std::int64_t lastPhysicalMove_{};
    bool hasPhysicalMove_{};
    std::uint64_t nextSequence_{1};
};

} // namespace inputweaver
