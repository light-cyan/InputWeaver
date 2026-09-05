#pragma once

#include "program_validator.hpp"
#include "mouse_fields.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::program_validation {

class ValidationContext final {
public:
    void Add(
        ProgramValidationErrorCode code,
        std::string location,
        std::string message)
    {
        if (errors_.size() < kMaximumProgramValidationErrors) {
            errors_.push_back({code, std::move(location), std::move(message)});
        }
    }

    [[nodiscard]] bool Full() const noexcept
    {
        return errors_.size() >= kMaximumProgramValidationErrors;
    }

    [[nodiscard]] std::vector<ProgramValidationError> Take() &&
    {
        return std::move(errors_);
    }

private:
    std::vector<ProgramValidationError> errors_;
};

[[nodiscard]] inline std::string At(std::string_view table, std::size_t index)
{
    return std::string(table) + "[" + std::to_string(index) + "]";
}

template <typename Id>
[[nodiscard]] bool ValidId(Id id, std::size_t size) noexcept
{
    return id.value < size;
}

[[nodiscard]] inline bool ValidRange(TableRange range, std::size_t size) noexcept
{
    const std::uint64_t end = static_cast<std::uint64_t>(range.begin) + range.count;
    return end <= size;
}

[[nodiscard]] inline bool ValidSpan(SourceSpan span, std::uint32_t sourceLength) noexcept
{
    const std::uint64_t end = static_cast<std::uint64_t>(span.beginByte)
        + span.byteLength;
    return span.beginByte <= sourceLength && end <= sourceLength;
}

[[nodiscard]] inline bool HasEmbeddedNul(
    const CompiledProgramStorage& storage,
    StringId id) noexcept
{
    return ValidId(id, storage.strings.size())
        && storage.strings[id.value].find('\0') != std::string::npos;
}

[[nodiscard]] inline bool InvalidRequiredString(
    const CompiledProgramStorage& storage,
    StringId id) noexcept
{
    return !ValidId(id, storage.strings.size())
        || storage.strings[id.value].empty()
        || storage.strings[id.value].find('\0') != std::string::npos;
}

[[nodiscard]] inline bool ValidControl(ControlRef control) noexcept
{
    if (control.namespaceId == 0U
        || control.namespaceId == kInvalidProgramIndex
        || control.familyId == 0U
        || control.familyId == kInvalidProgramIndex
        || control.code == kInvalidProgramIndex
        || control.qualifier == kInvalidProgramIndex) {
        return false;
    }

    switch (control.namespaceId) {
    case kControlNamespaceUsbHid:
        return control.familyId <= kMaximumHidUsagePage
            && control.code <= kMaximumHidUsageId
            && control.qualifier == kControlQualifierNone;
    case kControlNamespaceWeave:
        return false;
    case kControlNamespaceWindows:
        if (control.familyId == kWindowsVirtualKeyFamily) {
            return control.code <= kMaximumWindowsNativeCode
                && control.qualifier == kControlQualifierNone;
        }
        if (control.familyId == kWindowsScanCodeFamily) {
            return control.code <= kMaximumWindowsNativeCode
                && control.qualifier <= kWindowsScanCodeQualifierE1;
        }
        return false;
    case kControlNamespaceLinux:
        return control.familyId == kLinuxEvKeyFamily
            && control.code <= kMaximumLinuxEvKeyCode
            && control.qualifier == kControlQualifierNone;
    case kControlNamespaceMacOs:
        return control.familyId == kMacOsKeyCodeFamily
            && control.code <= kMaximumMacOsKeyCode
            && control.qualifier == kControlQualifierNone;
    default:
        return false;
    }
}

[[nodiscard]] inline bool ValidEventTransition(EventTransition transition) noexcept
{
    return transition == EventTransition::Down
        || transition == EventTransition::Again
        || transition == EventTransition::Up;
}

[[nodiscard]] inline bool ValidExpressionType(ExpressionType type) noexcept
{
    return type == ExpressionType::None
        || type == ExpressionType::Boolean
        || type == ExpressionType::State
        || type == ExpressionType::Number
        || type == ExpressionType::Duration
        || type == ExpressionType::ControlState;
}

[[nodiscard]] inline bool IsWritableValue(const ValueRef& value) noexcept
{
    return value.domain == ValueDomain::UserState
        || value.domain == ValueDomain::UserNumber
        || value.domain == ValueDomain::UserDuration;
}

[[nodiscard]] inline bool ValidateValueRefShape(
    const ValueRef& value,
    const CompiledProgramStorage& storage) noexcept
{
    switch (value.domain) {
    case ValueDomain::UserState:
        return value.type == ValueType::State
            && value.index < storage.userValues.initialStates.size();
    case ValueDomain::UserNumber:
        return value.type == ValueType::Number
            && value.index < storage.userValues.initialNumbers.size();
    case ValueDomain::UserDuration:
        return value.type == ValueType::Duration
            && value.index < storage.userValues.initialDurations.size();
    case ValueDomain::BuiltinState:
        return value.type == ValueType::State
            && value.index == static_cast<std::uint32_t>(BuiltinState::Pause);
    case ValueDomain::BuiltinDuration:
        return value.type == ValueType::Duration
            && value.index <= static_cast<std::uint32_t>(BuiltinDuration::MouseIdleTimeout);
    case ValueDomain::BuiltinNumber:
        return value.type == ValueType::Number
            && value.index == static_cast<std::uint32_t>(BuiltinNumber::Rand01);
    }
    return false;
}

void ValidateExpressionDescriptor(
    const CompiledProgramStorage& storage,
    std::size_t descriptorIndex,
    ValidationContext& context);

void ValidateActionDescriptor(
    const CompiledProgramStorage& storage,
    std::size_t descriptorIndex,
    ValidationContext& context);

} // namespace inputweaver::program_validation
