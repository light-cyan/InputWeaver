#pragma once

#include "compiled_program.hpp"

namespace inputweaver {

enum class MouseField : std::uint8_t {
    X, Y, Dx, Dy, WheelX, WheelY, Moving, IdleTime,
    StartX, StartY, Distance, Period, Progress, Remaining, Valid,
};

struct MouseFieldReference final {
    MeterId source{};
    MouseField field{};
    bool completed{};

    [[nodiscard]] constexpr std::uint32_t Selector() const noexcept
    {
        return static_cast<std::uint32_t>(field) | (completed ? 0x100U : 0U);
    }

    [[nodiscard]] static constexpr MouseFieldReference Decode(
        std::uint32_t source, std::uint32_t selector) noexcept
    {
        return {MeterId{source}, static_cast<MouseField>(selector & 0xffU),
            (selector & 0x100U) != 0U};
    }
};

[[nodiscard]] constexpr bool IsMouseTransition(EventTransition transition) noexcept
{
    return transition == EventTransition::Move
        || transition == EventTransition::Wheel
        || transition == EventTransition::HorizontalWheel;
}

[[nodiscard]] constexpr ExpressionType MouseFieldType(
    MouseFieldReference reference,
    EventTransition transition = EventTransition::Move,
    ExpressionType periodType = ExpressionType::Number) noexcept
{
    const auto field = reference.field;
    if (field > MouseField::Valid) {
        return ExpressionType::None;
    }
    if (!reference.source.IsValid()) {
        if (reference.completed || field > MouseField::IdleTime) return ExpressionType::None;
    } else {
        if (!IsMouseTransition(transition) || field == MouseField::IdleTime) return ExpressionType::None;
        if (reference.completed) {
            if (field == MouseField::Moving || field == MouseField::Progress
                || field == MouseField::Remaining) return ExpressionType::None;
        } else if (field == MouseField::Valid) {
            return ExpressionType::None;
        }
        const bool movement = transition == EventTransition::Move;
        if ((field == MouseField::WheelX || field == MouseField::WheelY) && movement) {
            return ExpressionType::None;
        }
        if ((field == MouseField::Dx || field == MouseField::Dy
             || field == MouseField::StartX || field == MouseField::StartY
             || field == MouseField::Distance || field == MouseField::Moving) && !movement) {
            return ExpressionType::None;
        }
    }
    if (field == MouseField::Moving || field == MouseField::Valid) return ExpressionType::State;
    if (field == MouseField::IdleTime) return ExpressionType::Duration;
    if (field == MouseField::Period || field == MouseField::Progress
        || field == MouseField::Remaining) return periodType;
    return ExpressionType::Number;
}

} // namespace inputweaver
