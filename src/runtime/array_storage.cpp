#include "array_storage.hpp"

#include <cmath>
#include <limits>
#include <new>

namespace inputweaver {

NormalizedArrayIndex NormalizeArrayIndex(
    double value,
    std::size_t length) noexcept
{
    if (std::isfinite(value) == 0) {
        return {ArrayIndexStatus::NonFinite};
    }
    if (value < 0.0) {
        return {ArrayIndexStatus::Negative};
    }
    const double normalized = std::floor(value);
    const double limit = static_cast<double>(
        (std::numeric_limits<std::size_t>::max)());
    if (normalized >= limit) {
        return {ArrayIndexStatus::TooLarge};
    }
    const std::size_t index = static_cast<std::size_t>(normalized);
    if (index >= length) {
        return {ArrayIndexStatus::OutOfBounds, index};
    }
    return {ArrayIndexStatus::Valid, index};
}

RuntimeArrayStorage::RuntimeArrayStorage(
    ArrayElementType elementType,
    std::span<const std::uint8_t> initialStates,
    std::span<const double> initialNumbers)
    : storage_(elementType == ArrayElementType::State
          ? decltype(storage_){std::in_place_index<0>, initialStates}
          : decltype(storage_){std::in_place_index<1>, initialNumbers})
{
}

ArrayElementType RuntimeArrayStorage::ElementType() const noexcept
{
    return storage_.index() == 0U
        ? ArrayElementType::State
        : ArrayElementType::Number;
}

std::size_t RuntimeArrayStorage::Size() const noexcept
{
    return std::visit([](const auto& array) noexcept { return array.Size(); }, storage_);
}

std::size_t RuntimeArrayStorage::AllocatedBytes() const noexcept
{
    return std::visit(
        [](const auto& array) noexcept { return array.AllocatedBytes(); },
        storage_);
}

bool RuntimeArrayStorage::Read(
    std::size_t index,
    RuntimeValue& value) const noexcept
{
    if (index >= Size()) {
        return false;
    }
    if (auto* states = std::get_if<StateArray>(&storage_)) {
        value.type = ExpressionType::State;
        value.stateValue = states->At(index);
    } else {
        value.type = ExpressionType::Number;
        value.numberValue = std::get<NumberArray>(storage_).At(index);
    }
    return true;
}

bool RuntimeArrayStorage::Set(
    std::size_t index,
    const RuntimeValue& value) noexcept
{
    if (index >= Size()) {
        return false;
    }
    if (auto* states = std::get_if<StateArray>(&storage_);
        states != nullptr && value.type == ExpressionType::State) {
        states->At(index) = value.stateValue;
        return true;
    }
    if (auto* numbers = std::get_if<NumberArray>(&storage_);
        numbers != nullptr && value.type == ExpressionType::Number
        && std::isfinite(value.numberValue) != 0) {
        numbers->At(index) = value.numberValue;
        return true;
    }
    return false;
}

bool RuntimeArrayStorage::Toggle(std::size_t index) noexcept
{
    auto* states = std::get_if<StateArray>(&storage_);
    if (states == nullptr || index >= states->Size()) {
        return false;
    }
    std::uint8_t& value = states->At(index);
    value = value == 0U ? 1U : 0U;
    return true;
}

std::optional<ArrayAppendPlan> RuntimeArrayStorage::PlanAppend() const noexcept
{
    return std::visit(
        [](const auto& array) noexcept { return array.PlanAppend(); },
        storage_);
}

std::optional<RuntimeArrayStorage::PreparedAppend>
RuntimeArrayStorage::PrepareAppend(const ArrayAppendPlan& plan) const noexcept
{
    if (const auto* states = std::get_if<StateArray>(&storage_)) {
        auto prepared = states->PrepareAppend(plan);
        return prepared
            ? std::optional<PreparedAppend>{std::in_place, std::in_place_index<0>,
                  std::move(*prepared)}
            : std::nullopt;
    }
    auto prepared = std::get<NumberArray>(storage_).PrepareAppend(plan);
    return prepared
        ? std::optional<PreparedAppend>{std::in_place, std::in_place_index<1>,
              std::move(*prepared)}
        : std::nullopt;
}

bool RuntimeArrayStorage::Append(
    const RuntimeValue& value,
    const ArrayAppendPlan& plan,
    PreparedAppend&& prepared) noexcept
{
    if (auto* states = std::get_if<StateArray>(&storage_);
        states != nullptr && value.type == ExpressionType::State
        && prepared.index() == 0U) {
        return states->Append(
            value.stateValue,
            plan,
            std::get<0>(std::move(prepared)));
    }
    if (auto* numbers = std::get_if<NumberArray>(&storage_);
        numbers != nullptr && value.type == ExpressionType::Number
        && std::isfinite(value.numberValue) != 0
        && prepared.index() == 1U) {
        return numbers->Append(
            value.numberValue,
            plan,
            std::get<1>(std::move(prepared)));
    }
    return false;
}

bool RuntimeArrayStorage::Pop(RuntimeValue& value) noexcept
{
    if (auto* states = std::get_if<StateArray>(&storage_)) {
        value.type = ExpressionType::State;
        return states->Pop(value.stateValue);
    }
    value.type = ExpressionType::Number;
    return std::get<NumberArray>(storage_).Pop(value.numberValue);
}

void RuntimeArrayStorage::Clear() noexcept
{
    std::visit([](auto& array) noexcept { array.Clear(); }, storage_);
}

} // namespace inputweaver
