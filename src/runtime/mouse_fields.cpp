#include "mouse_state.hpp"

#include <algorithm>
#include <cmath>

namespace inputweaver {

RuntimeEvaluationResult RuntimeMouseState::Read(EventFieldReference reference,
    std::span<const MouseCycle> selected) const noexcept
{
    RuntimeValue value{};
    const auto fault = [](RuntimeEvaluationFault reason) { return RuntimeEvaluationResult{{}, reason, 0}; };
    if (!reference.source.IsValid()) {
        value.type = EventFieldType(reference);
        if (value.type == ExpressionType::None) return fault(RuntimeEvaluationFault::InvalidInstruction);
        switch (reference.field) {
        case EventField::X: value.numberValue = observation_.position.x; break;
        case EventField::Y: value.numberValue = observation_.position.y; break;
        case EventField::Dx: value.numberValue = observation_.delta.dx; break;
        case EventField::Dy: value.numberValue = observation_.delta.dy; break;
        case EventField::WheelX: value.numberValue = observation_.delta.wheelX; break;
        case EventField::WheelY: value.numberValue = observation_.delta.wheelY; break;
        case EventField::Moving: value.stateValue = observation_.moving ? 1U : 0U; break;
        case EventField::IdleTime: value.durationValue = observation_.idleTime; break;
        default: return fault(RuntimeEvaluationFault::InvalidInstruction);
        }
        return {value, RuntimeEvaluationFault::None, 0};
    }
    const auto index = reference.source.value;
    if (index >= sources_.size()) return fault(RuntimeEvaluationFault::InvalidInstruction);
    const auto& source = sources_[index];
    value.type = EventFieldType(reference, source.config.transition, source.config.periodType);
    if (value.type == ExpressionType::None) return fault(RuntimeEvaluationFault::InvalidInstruction);
    const MouseCycle& cycle = !reference.completed ? source.current
        : index < selected.size() ? selected[index] : source.completed;
    if (reference.field == EventField::Valid) {
        value.stateValue = cycle.sequence != 0 ? 1U : 0U;
        return {value, RuntimeEvaluationFault::None, 0};
    }
    if (reference.completed && cycle.sequence == 0) return fault(RuntimeEvaluationFault::MissingCompletedEvent);
    const auto start = !reference.completed && !source.hasOrigin ? observation_.position : cycle.start;
    const auto point = !reference.completed && !source.hasOrigin ? observation_.position : cycle.point;
    switch (reference.field) {
    case EventField::StartX: value.numberValue = start.x; break;
    case EventField::StartY: value.numberValue = start.y; break;
    case EventField::X: value.numberValue = point.x; break;
    case EventField::Y: value.numberValue = point.y; break;
    case EventField::Dx: value.numberValue = cycle.displacement.x; break;
    case EventField::Dy: value.numberValue = cycle.displacement.y; break;
    case EventField::Distance: value.numberValue = cycle.distance; break;
    case EventField::WheelX:
        value.numberValue = source.config.transition == EventTransition::HorizontalWheel ? cycle.progress : 0;
        break;
    case EventField::WheelY:
        value.numberValue = source.config.transition == EventTransition::Wheel ? cycle.progress : 0;
        break;
    case EventField::Moving: value.stateValue = source.moving ? 1U : 0U; break;
    case EventField::Period: value = cycle.period; break;
    case EventField::Progress:
        value.numberValue = cycle.progress;
        value.durationValue.nanoseconds = cycle.elapsedNanoseconds;
        break;
    case EventField::Remaining:
        value.numberValue = (std::max)(0.0, cycle.period.numberValue - std::abs(cycle.progress));
        value.durationValue.nanoseconds = (std::max)(std::int64_t{0},
            cycle.period.durationValue.nanoseconds - cycle.elapsedNanoseconds);
        break;
    default: return fault(RuntimeEvaluationFault::InvalidInstruction);
    }
    return {value, RuntimeEvaluationFault::None, 0};
}

} // namespace inputweaver
