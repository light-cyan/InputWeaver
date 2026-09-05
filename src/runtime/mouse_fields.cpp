#include "mouse_state.hpp"

#include <algorithm>
#include <cmath>

namespace inputweaver {

RuntimeEvaluationResult RuntimeMouseState::Read(MouseFieldReference reference,
    std::span<const MouseCycle> selected) const noexcept
{
    RuntimeValue value{};
    const auto fault = [](RuntimeEvaluationFault reason) { return RuntimeEvaluationResult{{}, reason, 0}; };
    if (!reference.source.IsValid()) {
        value.type = MouseFieldType(reference);
        if (value.type == ExpressionType::None) return fault(RuntimeEvaluationFault::InvalidInstruction);
        switch (reference.field) {
        case MouseField::X: value.numberValue = observation_.position.x; break;
        case MouseField::Y: value.numberValue = observation_.position.y; break;
        case MouseField::Dx: value.numberValue = observation_.delta.dx; break;
        case MouseField::Dy: value.numberValue = observation_.delta.dy; break;
        case MouseField::WheelX: value.numberValue = observation_.delta.wheelX; break;
        case MouseField::WheelY: value.numberValue = observation_.delta.wheelY; break;
        case MouseField::Moving: value.stateValue = observation_.moving ? 1U : 0U; break;
        case MouseField::IdleTime: value.durationValue = observation_.idleTime; break;
        default: return fault(RuntimeEvaluationFault::InvalidInstruction);
        }
        return {value, RuntimeEvaluationFault::None, 0};
    }
    const auto index = reference.source.value;
    if (index >= meters_.size()) return fault(RuntimeEvaluationFault::InvalidInstruction);
    const auto& source = meters_[index];
    value.type = MouseFieldType(reference, source.config.transition, source.config.periodType);
    if (value.type == ExpressionType::None) return fault(RuntimeEvaluationFault::InvalidInstruction);
    const MouseCycle& cycle = !reference.completed ? source.current
        : index < selected.size() ? selected[index] : source.completed;
    if (reference.field == MouseField::Valid) {
        value.stateValue = cycle.sequence != 0 ? 1U : 0U;
        return {value, RuntimeEvaluationFault::None, 0};
    }
    if (reference.completed && cycle.sequence == 0) return fault(RuntimeEvaluationFault::MissingCompletedMeter);
    const auto start = !reference.completed && !source.hasOrigin ? observation_.position : cycle.start;
    const auto point = !reference.completed && !source.hasOrigin ? observation_.position : cycle.point;
    switch (reference.field) {
    case MouseField::StartX: value.numberValue = start.x; break;
    case MouseField::StartY: value.numberValue = start.y; break;
    case MouseField::X: value.numberValue = point.x; break;
    case MouseField::Y: value.numberValue = point.y; break;
    case MouseField::Dx: value.numberValue = cycle.displacement.x; break;
    case MouseField::Dy: value.numberValue = cycle.displacement.y; break;
    case MouseField::Distance: value.numberValue = cycle.distance; break;
    case MouseField::WheelX:
        value.numberValue = source.config.transition == EventTransition::HorizontalWheel ? cycle.progress : 0;
        break;
    case MouseField::WheelY:
        value.numberValue = source.config.transition == EventTransition::Wheel ? cycle.progress : 0;
        break;
    case MouseField::Moving: value.stateValue = source.moving ? 1U : 0U; break;
    case MouseField::Period: value = cycle.period; break;
    case MouseField::Progress:
        value.numberValue = cycle.progress;
        value.durationValue.nanoseconds = cycle.elapsedNanoseconds;
        break;
    case MouseField::Remaining:
        value.numberValue = (std::max)(0.0, cycle.period.numberValue - std::abs(cycle.progress));
        value.durationValue.nanoseconds = (std::max)(std::int64_t{0},
            cycle.period.durationValue.nanoseconds - cycle.elapsedNanoseconds);
        break;
    default: return fault(RuntimeEvaluationFault::InvalidInstruction);
    }
    return {value, RuntimeEvaluationFault::None, 0};
}

} // namespace inputweaver
