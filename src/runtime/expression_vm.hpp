#pragma once

#include "array_storage.hpp"
#include "runtime_types.hpp"
#include "mouse_state.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace inputweaver {

class RuntimeRandomStream final {
public:
    explicit RuntimeRandomStream(std::uint64_t seed) noexcept;

    [[nodiscard]] double Next01() noexcept;

private:
    std::atomic<std::uint64_t> state_;
};

struct RuntimeExpressionState final {
    std::span<const std::uint8_t> userStates;
    std::span<const double> userNumbers;
    std::span<const DurationValue> userDurations;
    std::span<const RuntimeArrayStorage> arrays;
    std::span<const std::atomic<std::uint8_t>> physicalHeld;
    RuntimeRandomStream* randomStream{};
    bool pauseOn{true};
    DurationValue tapDuration{};
    DurationValue actionGap{};
    const RuntimeMouseState* mouse{};
    std::span<const MouseCycle> completed{};
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
