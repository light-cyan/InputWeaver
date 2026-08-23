#pragma once

#include "runtime_types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace inputweaver {

struct RuntimeExpressionState final {
    std::span<const std::uint8_t> userStates;
    std::span<const double> userNumbers;
    std::span<const DurationValue> userDurations;
    std::span<const std::atomic<std::uint8_t>> physicalHeld;
    bool pauseOn{true};
    DurationValue tapDuration{};
    DurationValue actionGap{};
};

class RuntimeExpressionScratch final {
public:
    explicit RuntimeExpressionScratch(std::size_t capacity);

    [[nodiscard]] std::span<RuntimeValue> Storage() noexcept;
    [[nodiscard]] std::size_t Capacity() const noexcept;

private:
    std::vector<RuntimeValue> storage_;
};

[[nodiscard]] RuntimeEvaluationResult EvaluateRuntimeExpression(
    const CompiledProgram& program,
    ExpressionId expression,
    const RuntimeExpressionState& state,
    RuntimeExpressionScratch& scratch) noexcept;

} // namespace inputweaver
