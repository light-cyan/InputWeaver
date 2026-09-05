#pragma once

#include "compiled_program.hpp"

#include <array>
#include <string_view>

namespace inputweaver {

enum class EventField : std::uint8_t {
    X, Y, Dx, Dy, WheelX, WheelY, Moving, IdleTime,
    StartX, StartY, Distance, Period, Progress, Remaining, Valid,
};

inline constexpr std::array<std::string_view, 15> kEventFieldNames{
    "x", "y", "dx", "dy", "wheel_x", "wheel_y", "moving", "idle_time",
    "start_x", "start_y", "distance", "period", "progress", "remaining", "valid",
};

struct EventFieldReference final {
    EventSourceId source{};
    EventField field{};
    bool completed{};

    [[nodiscard]] constexpr std::uint32_t Selector() const noexcept
    {
        return static_cast<std::uint32_t>(field) | (completed ? 0x100U : 0U);
    }

    [[nodiscard]] static constexpr EventFieldReference Decode(
        std::uint32_t source, std::uint32_t selector) noexcept
    {
        return {EventSourceId{source}, static_cast<EventField>(selector & 0xffU),
            (selector & 0x100U) != 0U};
    }
};

[[nodiscard]] constexpr bool IsMouseTransition(EventTransition transition) noexcept
{
    return transition == EventTransition::Move
        || transition == EventTransition::Wheel
        || transition == EventTransition::HorizontalWheel;
}

[[nodiscard]] constexpr ExpressionType EventFieldType(
    EventFieldReference reference,
    EventTransition transition = EventTransition::Move,
    ExpressionType periodType = ExpressionType::Number) noexcept
{
    const auto field = reference.field;
    if (static_cast<std::size_t>(field) >= kEventFieldNames.size()) {
        return ExpressionType::None;
    }
    if (!reference.source.IsValid()) {
        if (reference.completed || field > EventField::IdleTime) return ExpressionType::None;
    } else {
        if (!IsMouseTransition(transition) || field == EventField::IdleTime) return ExpressionType::None;
        if (reference.completed) {
            if (field == EventField::Moving || field == EventField::Progress
                || field == EventField::Remaining) return ExpressionType::None;
        } else if (field == EventField::Valid) {
            return ExpressionType::None;
        }
        const bool movement = transition == EventTransition::Move;
        if ((field == EventField::WheelX || field == EventField::WheelY) && movement) {
            return ExpressionType::None;
        }
        if ((field == EventField::Dx || field == EventField::Dy
             || field == EventField::StartX || field == EventField::StartY
             || field == EventField::Distance || field == EventField::Moving) && !movement) {
            return ExpressionType::None;
        }
    }
    if (field == EventField::Moving || field == EventField::Valid) return ExpressionType::State;
    if (field == EventField::IdleTime) return ExpressionType::Duration;
    if (field == EventField::Period || field == EventField::Progress
        || field == EventField::Remaining) return periodType;
    return ExpressionType::Number;
}

} // namespace inputweaver
