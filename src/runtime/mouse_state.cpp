#include "mouse_state.hpp"

#include <algorithm>
#include <cmath>

namespace inputweaver {
namespace {

[[nodiscard]] bool EffectiveMove(const RuntimeInputEvent& event) noexcept
{
    return event.transition == Transition::Move && (event.delta.dx != 0 || event.delta.dy != 0);
}

[[nodiscard]] bool Matches(EventTransition source, Transition input) noexcept
{
    return (source == EventTransition::Move && input == Transition::Move)
        || (source == EventTransition::Wheel && input == Transition::VerticalWheel)
        || (source == EventTransition::HorizontalWheel && input == Transition::HorizontalWheel);
}

void ClearCurrent(MouseSourceState& source) noexcept
{
    source.current = {};
    source.current.period.type = source.config.periodType;
    source.opened = false;
    source.hasOrigin = false;
    source.moving = false;
}

} // namespace

RuntimeMouseState::RuntimeMouseState(std::span<const MouseSourceConfig> sources,
    DurationValue idleTimeout, std::size_t maximumOccurrences)
    : sources_(sources.size()), idleTimeout_(idleTimeout)
{
    if (!sources.empty()) occurrences_.reserve(maximumOccurrences);
    for (std::size_t index = 0; index < sources.size(); ++index) {
        sources_[index].config = sources[index];
        ClearCurrent(sources_[index]);
    }
}

void RuntimeMouseState::Initialize(MousePoint position, std::int64_t now) noexcept
{
    observation_ = {};
    observation_.position = position;
    lastPhysicalMove_ = now;
    hasPhysicalMove_ = false;
    nextSequence_ = 1;
    ResetSources();
}

void RuntimeMouseState::Refresh(MousePoint position, std::int64_t now) noexcept
{
    observation_.position = position;
    observation_.idleTime.nanoseconds = (std::max)(std::int64_t{0}, now - lastPhysicalMove_);
    observation_.moving = hasPhysicalMove_ && observation_.idleTime.nanoseconds < idleTimeout_.nanoseconds;
    for (auto& source : sources_) {
        if (source.moving && now - source.lastMoveNanoseconds >= idleTimeout_.nanoseconds) {
            source.moving = false;
            if (source.config.periodType == ExpressionType::Duration) ClearCurrent(source);
        }
    }
}

void RuntimeMouseState::Observe(const RuntimeInputEvent& event, std::int64_t now) noexcept
{
    if (event.origin != InputOrigin::PhysicalCandidate || event.device != DeviceKind::Mouse) return;
    const MousePoint point{static_cast<double>(event.position.x), static_cast<double>(event.position.y)};
    Refresh(point, now);
    if (event.transition == Transition::Move) {
        observation_.delta = {event.delta.dx, event.delta.dy, 0, 0};
        if (EffectiveMove(event)) {
            lastPhysicalMove_ = now;
            hasPhysicalMove_ = true;
            observation_.idleTime = {};
            observation_.moving = true;
        }
    } else if (event.transition == Transition::VerticalWheel) {
        observation_.delta = {0, 0, 0, event.delta.wheelY};
    } else if (event.transition == Transition::HorizontalWheel) {
        observation_.delta = {0, 0, event.delta.wheelX, 0};
    }
}

void RuntimeMouseState::Restart(EventSourceId id) noexcept
{
    if (id.value >= sources_.size()) return;
    auto& source = sources_[id.value];
    ClearCurrent(source);
    source.completed = {};
    source.periodResult = {};
}

void RuntimeMouseState::ResetSources() noexcept
{
    for (std::size_t index = 0; index < sources_.size(); ++index) {
        Restart(EventSourceId{static_cast<std::uint32_t>(index)});
    }
    occurrences_.clear();
}

bool RuntimeMouseState::Open(EventSourceId id, MousePeriodEvaluator evaluator) noexcept
{
    auto& source = sources_[id.value];
    if (source.opened) return true;
    source.periodResult = evaluator.Invoke(id);
    const auto& period = source.periodResult.value;
    if (!source.periodResult.Succeeded() || period.type != source.config.periodType
        || (period.type == ExpressionType::Duration ? period.durationValue.nanoseconds <= 0
            : !std::isfinite(period.numberValue) || period.numberValue <= 0)) {
        if (source.periodResult.Succeeded()) source.periodResult.fault = RuntimeEvaluationFault::InvalidEventPeriod;
        ClearCurrent(source);
        return false;
    }
    source.current.period = period;
    source.opened = true;
    return true;
}

bool RuntimeMouseState::Complete(EventSourceId id, MousePeriodEvaluator evaluator) noexcept
{
    auto& source = sources_[id.value];
    source.current.sequence = nextSequence_++;
    source.completed = source.current;
    const bool available = occurrences_.size() < occurrences_.capacity();
    if (available) occurrences_.push_back({id, source.completed});
    const auto point = source.current.point;
    source.current = {};
    source.current.start = point;
    source.current.point = point;
    source.opened = false;
    return Open(id, evaluator) && available;
}

bool RuntimeMouseState::Move(EventSourceId id, const RuntimeInputEvent& event,
    std::int64_t now, MousePeriodEvaluator evaluator) noexcept
{
    if (!EffectiveMove(event)) return true;
    auto& source = sources_[id.value];
    const bool timed = source.config.periodType == ExpressionType::Duration;
    const bool continuous = source.moving && now - source.lastMoveNanoseconds < idleTimeout_.nanoseconds;
    if (timed && !continuous) ClearCurrent(source);
    if (!Open(id, evaluator)) return false;
    auto& cycle = source.current;
    if (!source.hasOrigin) {
        cycle.start = {event.position.x - event.delta.dx, event.position.y - event.delta.dy};
        cycle.point = cycle.start;
        source.hasOrigin = true;
    }
    double dx = event.delta.dx;
    double dy = event.delta.dy;
    double distance = std::hypot(dx, dy);
    std::int64_t elapsed = continuous ? now - source.lastMoveNanoseconds : 0;
    source.lastMoveNanoseconds = now;
    source.moving = true;
    while (distance > 0 || (timed && elapsed > 0)) {
        const auto remainingTime = cycle.period.durationValue.nanoseconds - cycle.elapsedNanoseconds;
        const double remainingDistance = cycle.period.numberValue - cycle.progress;
        const bool completes = timed ? elapsed >= remainingTime : distance >= remainingDistance;
        const double fraction = !completes ? 1.0 : timed
            ? static_cast<double>(remainingTime) / static_cast<double>(elapsed)
            : remainingDistance / distance;
        const double partDistance = distance * fraction;
        cycle.point.x += dx * fraction;
        cycle.point.y += dy * fraction;
        cycle.distance += partDistance;
        if (timed) cycle.elapsedNanoseconds += completes ? remainingTime : elapsed;
        else cycle.progress += completes ? remainingDistance : partDistance;
        dx *= 1 - fraction;
        dy *= 1 - fraction;
        distance -= partDistance;
        if (timed) elapsed -= completes ? remainingTime : elapsed;
        if (!completes) break;
        if (!Complete(id, evaluator)) return false;
    }
    cycle.point = {static_cast<double>(event.position.x), static_cast<double>(event.position.y)};
    return true;
}

bool RuntimeMouseState::Wheel(EventSourceId id, const RuntimeInputEvent& event,
    MousePeriodEvaluator evaluator) noexcept
{
    auto& source = sources_[id.value];
    double amount = source.config.transition == EventTransition::Wheel ? event.delta.wheelY : event.delta.wheelX;
    if (amount == 0 || !Open(id, evaluator)) return amount == 0;
    source.current.point = {static_cast<double>(event.position.x), static_cast<double>(event.position.y)};
    source.hasOrigin = true;
    while (amount != 0) {
        auto& cycle = source.current;
        const double next = cycle.progress + amount;
        if (std::abs(next) < cycle.period.numberValue) {
            cycle.progress = next;
            break;
        }
        const double boundary = std::copysign(cycle.period.numberValue, next);
        amount -= boundary - cycle.progress;
        cycle.progress = boundary;
        if (!Complete(id, evaluator)) return false;
    }
    return true;
}

bool RuntimeMouseState::Accumulate(const RuntimeInputEvent& event, std::int64_t now,
    MousePeriodEvaluator evaluator) noexcept
{
    occurrences_.clear();
    if (event.origin != InputOrigin::PhysicalCandidate || event.device != DeviceKind::Mouse) return true;
    bool success = true;
    for (std::size_t index = 0; index < sources_.size(); ++index) {
        auto& source = sources_[index];
        if (!Matches(source.config.transition, event.transition)) continue;
        const EventSourceId id{static_cast<std::uint32_t>(index)};
        const bool advanced = source.config.transition == EventTransition::Move
            ? Move(id, event, now, evaluator) : Wheel(id, event, evaluator);
        success = advanced && success;
    }
    return success;
}

void RuntimeMouseState::SelectCompleted(std::span<MouseCycle> destination,
    const MouseOccurrence* trigger) const noexcept
{
    for (std::size_t index = 0; index < (std::min)(destination.size(), sources_.size()); ++index) {
        destination[index] = trigger && trigger->source.value == index ? trigger->cycle : sources_[index].completed;
    }
}

} // namespace inputweaver
