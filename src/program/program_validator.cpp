#include "program_validator.hpp"

#include "support/utf8.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver {
namespace {

constexpr std::uint8_t kAllControlUseBits =
    ToControlUseBits(ControlUse::EventSource)
    | ToControlUseBits(ControlUse::PhysicalState)
    | ToControlUseBits(ControlUse::OutputDownUp)
    | ToControlUseBits(ControlUse::OutputRepeat);

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

[[nodiscard]] std::string At(std::string_view table, std::size_t index)
{
    return std::string(table) + "[" + std::to_string(index) + "]";
}

template <typename Id>
[[nodiscard]] bool ValidId(Id id, std::size_t size) noexcept
{
    return id.value < size;
}

[[nodiscard]] bool ValidRange(TableRange range, std::size_t size) noexcept
{
    const std::uint64_t end = static_cast<std::uint64_t>(range.begin) + range.count;
    return end <= size;
}

[[nodiscard]] bool ValidSpan(SourceSpan span, std::uint32_t sourceLength) noexcept
{
    const std::uint64_t end = static_cast<std::uint64_t>(span.beginByte)
        + span.byteLength;
    return span.beginByte <= sourceLength && end <= sourceLength;
}

[[nodiscard]] bool HasEmbeddedNul(
    const CompiledProgramStorage& storage,
    StringId id) noexcept
{
    return ValidId(id, storage.strings.size())
        && storage.strings[id.value].find('\0') != std::string::npos;
}

[[nodiscard]] bool InvalidRequiredString(
    const CompiledProgramStorage& storage,
    StringId id) noexcept
{
    return !ValidId(id, storage.strings.size())
        || storage.strings[id.value].empty()
        || storage.strings[id.value].find('\0') != std::string::npos;
}

[[nodiscard]] bool ValidControl(ControlRef control) noexcept
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

[[nodiscard]] bool ValidEventTransition(EventTransition transition) noexcept
{
    return transition == EventTransition::Down
        || transition == EventTransition::Repeat
        || transition == EventTransition::Up;
}

[[nodiscard]] bool ValidExpressionType(ExpressionType type) noexcept
{
    return type == ExpressionType::None
        || type == ExpressionType::Boolean
        || type == ExpressionType::State
        || type == ExpressionType::Number
        || type == ExpressionType::Duration;
}

[[nodiscard]] ExpressionType ToExpressionType(ValueType type) noexcept
{
    switch (type) {
    case ValueType::State:
        return ExpressionType::State;
    case ValueType::Number:
        return ExpressionType::Number;
    case ValueType::Duration:
        return ExpressionType::Duration;
    }
    return ExpressionType::None;
}

[[nodiscard]] bool IsWritableValue(const ValueRef& value) noexcept
{
    return value.domain == ValueDomain::UserState
        || value.domain == ValueDomain::UserNumber
        || value.domain == ValueDomain::UserDuration;
}

[[nodiscard]] bool ValidateValueRefShape(
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
            && value.index <= static_cast<std::uint32_t>(BuiltinDuration::ActionGap);
    }
    return false;
}

[[nodiscard]] bool PopType(
    std::vector<ExpressionType>& stack,
    ExpressionType expected) noexcept
{
    if (stack.empty() || stack.back() != expected) {
        return false;
    }
    stack.pop_back();
    return true;
}

void ValidateExpressionOperator(
    const ExpressionInstruction& instruction,
    std::vector<ExpressionType>& stack,
    ValidationContext& context,
    const std::string& location)
{
    if (instruction.opcode == ExpressionOpcode::Unary) {
        const auto operation = static_cast<UnaryOperator>(instruction.operand0);
        ExpressionType input = ExpressionType::None;
        ExpressionType output = ExpressionType::None;
        switch (operation) {
        case UnaryOperator::NumberIdentity:
        case UnaryOperator::NumberNegate:
            input = ExpressionType::Number;
            output = ExpressionType::Number;
            break;
        case UnaryOperator::BooleanNot:
            input = ExpressionType::Boolean;
            output = ExpressionType::Boolean;
            break;
        default:
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "unknown unary operator");
            return;
        }
        if (!PopType(stack, input) || instruction.type != output) {
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "unary operator stack or result type mismatch");
            return;
        }
        stack.push_back(output);
        return;
    }

    const auto operation = static_cast<BinaryOperator>(instruction.operand0);
    ExpressionType left = ExpressionType::None;
    ExpressionType right = ExpressionType::None;
    ExpressionType output = ExpressionType::None;
    bool equality = false;
    switch (operation) {
    case BinaryOperator::NumberAdd:
    case BinaryOperator::NumberSubtract:
    case BinaryOperator::NumberMultiply:
    case BinaryOperator::NumberDivide:
    case BinaryOperator::NumberModulo:
        left = ExpressionType::Number;
        right = ExpressionType::Number;
        output = ExpressionType::Number;
        break;
    case BinaryOperator::DurationAdd:
    case BinaryOperator::DurationSubtract:
        left = ExpressionType::Duration;
        right = ExpressionType::Duration;
        output = ExpressionType::Duration;
        break;
    case BinaryOperator::DurationMultiplyNumber:
    case BinaryOperator::DurationDivideNumber:
        left = ExpressionType::Duration;
        right = ExpressionType::Number;
        output = ExpressionType::Duration;
        break;
    case BinaryOperator::NumberMultiplyDuration:
        left = ExpressionType::Number;
        right = ExpressionType::Duration;
        output = ExpressionType::Duration;
        break;
    case BinaryOperator::Equal:
    case BinaryOperator::NotEqual:
        equality = true;
        output = ExpressionType::Boolean;
        break;
    case BinaryOperator::NumberLess:
    case BinaryOperator::NumberLessEqual:
    case BinaryOperator::NumberGreater:
    case BinaryOperator::NumberGreaterEqual:
        left = ExpressionType::Number;
        right = ExpressionType::Number;
        output = ExpressionType::Boolean;
        break;
    default:
        context.Add(
            ProgramValidationErrorCode::Expression,
            location,
            "unknown binary operator");
        return;
    }

    if (equality) {
        if (stack.size() < 2U) {
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "equality operator stack underflow");
            return;
        }
        right = stack.back();
        stack.pop_back();
        left = stack.back();
        stack.pop_back();
        if (left != right
            || (left != ExpressionType::State
                && left != ExpressionType::Number
                && left != ExpressionType::Duration)) {
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "equality operands must have the same comparable type");
            return;
        }
    } else if (!PopType(stack, right) || !PopType(stack, left)) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            location,
            "binary operator stack type mismatch");
        return;
    }

    if (instruction.type != output) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            location,
            "binary operator result type mismatch");
        return;
    }
    stack.push_back(output);
}

void MergeExpressionStack(
    std::vector<std::optional<std::vector<ExpressionType>>>& states,
    std::uint32_t target,
    const std::vector<ExpressionType>& stack,
    ValidationContext& context,
    const std::string& location)
{
    if (target >= states.size()) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            location,
            "control flow falls outside the expression program");
        return;
    }
    auto& state = states[target];
    if (!state.has_value()) {
        state = stack;
    } else if (*state != stack) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            location,
            "stack types disagree at a control-flow merge");
    }
}

void ValidateExpressionDescriptor(
    const CompiledProgramStorage& storage,
    std::size_t descriptorIndex,
    ValidationContext& context)
{
    const ExpressionDescriptor& descriptor = storage.expressions[descriptorIndex];
    const std::string descriptorLocation = At("expressions", descriptorIndex);
    if (!ValidRange(descriptor.code, storage.expressionCode.size())
        || descriptor.code.count == 0U) {
        context.Add(
            ProgramValidationErrorCode::Range,
            descriptorLocation,
            "expression code range is empty or outside expressionCode");
        return;
    }
    if (descriptor.resultType == ExpressionType::None
        || !ValidExpressionType(descriptor.resultType)) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            descriptorLocation,
            "expression result type is invalid");
    }
    if (!ValidSpan(descriptor.source, storage.source.byteLength)) {
        context.Add(
            ProgramValidationErrorCode::Source,
            descriptorLocation + ".source",
            "source span is outside the source file");
    }

    const std::size_t count = descriptor.code.count;
    std::vector<std::optional<std::vector<ExpressionType>>> states(count);
    states[0] = std::vector<ExpressionType>{};
    std::size_t maximumDepth = 0;
    bool sawReturn = false;
    for (std::size_t local = 0; local < count; ++local) {
        if (!states[local].has_value()) {
            continue;
        }
        std::vector<ExpressionType> stack = *states[local];
        const std::size_t absolute = static_cast<std::size_t>(descriptor.code.begin) + local;
        const ExpressionInstruction& instruction = storage.expressionCode[absolute];
        const std::string location = At("expressionCode", absolute);
        const auto next = static_cast<std::uint32_t>(local + 1U);
        bool fallthrough = true;

        switch (instruction.opcode) {
        case ExpressionOpcode::PushBoolean:
            if (instruction.type != ExpressionType::Boolean
                || instruction.operand0 > 1U
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "PushBoolean operands or type are invalid");
            }
            stack.push_back(ExpressionType::Boolean);
            break;
        case ExpressionOpcode::PushState:
            if (instruction.type != ExpressionType::State
                || instruction.operand0 > 1U
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "PushState operands or type are invalid");
            }
            stack.push_back(ExpressionType::State);
            break;
        case ExpressionOpcode::PushNumber:
            if (instruction.type != ExpressionType::Number
                || instruction.operand0 >= storage.numberConstants.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "PushNumber operands or type are invalid");
            }
            stack.push_back(ExpressionType::Number);
            break;
        case ExpressionOpcode::PushDuration:
            if (instruction.type != ExpressionType::Duration
                || instruction.operand0 >= storage.durationConstants.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "PushDuration operands or type are invalid");
            }
            stack.push_back(ExpressionType::Duration);
            break;
        case ExpressionOpcode::LoadValue:
            if (instruction.operand0 >= storage.valueRefs.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "LoadValue reference or unused operand is invalid");
            } else {
                const ExpressionType expected = ToExpressionType(
                    storage.valueRefs[instruction.operand0].type);
                if (instruction.type != expected) {
                    context.Add(
                        ProgramValidationErrorCode::Expression,
                        location,
                        "LoadValue result type does not match its value reference");
                }
                stack.push_back(expected);
            }
            break;
        case ExpressionOpcode::ReadControlHeld:
            if (instruction.type != ExpressionType::Boolean
                || instruction.operand0 >= storage.controls.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "ReadControlHeld operands or type are invalid");
            }
            stack.push_back(ExpressionType::Boolean);
            break;
        case ExpressionOpcode::Unary:
        case ExpressionOpcode::Binary:
            if (instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "operator instruction has a nonzero unused operand");
            }
            ValidateExpressionOperator(instruction, stack, context, location);
            break;
        case ExpressionOpcode::Jump:
            fallthrough = false;
            if (instruction.type != ExpressionType::None
                || instruction.operand1 != 0U
                || instruction.operand0 <= local
                || instruction.operand0 >= count) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "Jump must have a forward local target and zero unused fields");
            } else {
                MergeExpressionStack(
                    states,
                    instruction.operand0,
                    stack,
                    context,
                    location);
            }
            break;
        case ExpressionOpcode::JumpIfFalse:
        case ExpressionOpcode::JumpIfTrue:
            if (instruction.type != ExpressionType::None
                || instruction.operand1 != 0U
                || instruction.operand0 <= local
                || instruction.operand0 >= count
                || !PopType(stack, ExpressionType::Boolean)) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "conditional jump target, fields, or Boolean operand are invalid");
            } else {
                MergeExpressionStack(
                    states,
                    instruction.operand0,
                    stack,
                    context,
                    location);
            }
            break;
        case ExpressionOpcode::Return:
            fallthrough = false;
            sawReturn = true;
            if (instruction.type != descriptor.resultType
                || instruction.operand0 != 0U
                || instruction.operand1 != 0U
                || stack.size() != 1U
                || !PopType(stack, descriptor.resultType)) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "Return must consume exactly one descriptor result value");
            }
            break;
        default:
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "unknown expression opcode");
            break;
        }

        maximumDepth = (std::max)(maximumDepth, stack.size());
        if (fallthrough) {
            MergeExpressionStack(states, next, stack, context, location);
        }
    }

    if (!sawReturn) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            descriptorLocation,
            "expression has no reachable Return");
    }
    if (std::any_of(states.begin(), states.end(), [](const auto& state) {
            return !state.has_value();
        })) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            descriptorLocation,
            "expression contains unreachable instructions");
    }
    if (maximumDepth != descriptor.maximumStackDepth) {
        context.Add(
            ProgramValidationErrorCode::Expression,
            descriptorLocation + ".maximumStackDepth",
            "declared maximum stack depth does not match the code");
    }
}

[[nodiscard]] ExpressionType ExpressionResultType(
    const CompiledProgramStorage& storage,
    std::uint32_t expressionIndex) noexcept
{
    if (expressionIndex >= storage.expressions.size()) {
        return ExpressionType::None;
    }
    return storage.expressions[expressionIndex].resultType;
}

void AddActionSuccessor(
    std::vector<bool>& reachable,
    std::vector<std::uint32_t>& pending,
    std::uint32_t target,
    ValidationContext& context,
    const std::string& location)
{
    if (target >= reachable.size()) {
        context.Add(
            ProgramValidationErrorCode::Action,
            location,
            "control flow leaves the action program");
        return;
    }
    if (!reachable[target]) {
        reachable[target] = true;
        pending.push_back(target);
    }
}

void ValidateActionDescriptor(
    const CompiledProgramStorage& storage,
    std::size_t descriptorIndex,
    ValidationContext& context)
{
    const ActionProgramDescriptor& descriptor = storage.actionPrograms[descriptorIndex];
    const std::string descriptorLocation = At("actionPrograms", descriptorIndex);
    if (!ValidRange(descriptor.code, storage.actionCode.size())
        || descriptor.code.count == 0U) {
        context.Add(
            ProgramValidationErrorCode::Range,
            descriptorLocation,
            "action code range is empty or outside actionCode");
        return;
    }
    if (!ValidSpan(descriptor.source, storage.source.byteLength)) {
        context.Add(
            ProgramValidationErrorCode::Source,
            descriptorLocation + ".source",
            "source span is outside the source file");
    }

    const std::size_t count = descriptor.code.count;
    std::vector<bool> reachable(count, false);
    std::vector<std::uint32_t> pending{0U};
    reachable[0] = true;
    std::set<std::uint32_t> acquiredControls;
    std::uint32_t requiredFrames = 0U;
    bool reachableEnd = false;
    std::set<std::uint32_t> explicitTargets;
    std::set<std::uint32_t> backwardJumpPositions;
    for (std::uint32_t local = 0U; local < count; ++local) {
        const ActionInstruction& instruction = storage.actionCode[
            static_cast<std::size_t>(descriptor.code.begin) + local];
        if (instruction.opcode == ActionOpcode::Jump
            && instruction.operand0 < count) {
            explicitTargets.insert(instruction.operand0);
            if (instruction.operand0 < local) {
                backwardJumpPositions.insert(local);
            }
        } else if ((instruction.opcode == ActionOpcode::JumpIfFalse
                    || instruction.opcode == ActionOpcode::RepeatCheck)
                   && instruction.operand1 < count) {
            explicitTargets.insert(instruction.operand1);
        }
    }
    for (const std::uint32_t backwardJump : backwardJumpPositions) {
        if (explicitTargets.contains(backwardJump)) {
            context.Add(
                ProgramValidationErrorCode::Action,
                descriptorLocation,
                "a backward Jump may be entered only by falling through its preceding Yield");
        }
    }

    while (!pending.empty()) {
        const std::uint32_t local = pending.back();
        pending.pop_back();
        const std::size_t absolute = static_cast<std::size_t>(descriptor.code.begin)
            + local;
        const ActionInstruction& instruction = storage.actionCode[absolute];
        const std::string location = At("actionCode", absolute);
        const std::uint32_t next = local + 1U;
        bool fallthrough = true;

        switch (instruction.opcode) {
        case ActionOpcode::Press:
        case ActionOpcode::Release:
        case ActionOpcode::Tap:
            if (instruction.operand0 >= storage.controls.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "control action reference or unused operand is invalid");
            } else if (instruction.opcode != ActionOpcode::Release) {
                acquiredControls.insert(instruction.operand0);
            }
            break;
        case ActionOpcode::Wait:
            if (ExpressionResultType(storage, instruction.operand0)
                    != ExpressionType::Duration
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Wait requires a Duration expression and a zero unused operand");
            }
            break;
        case ActionOpcode::Gap:
            if (instruction.operand0 != 0U || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Gap has nonzero unused operands");
            }
            break;
        case ActionOpcode::Set:
            if (instruction.operand0 >= storage.valueRefs.size()
                || instruction.operand1 >= storage.expressions.size()) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Set references an unknown value or expression");
            } else {
                const ValueRef& value = storage.valueRefs[instruction.operand0];
                if (!IsWritableValue(value)
                    || ExpressionResultType(storage, instruction.operand1)
                        != ToExpressionType(value.type)) {
                    context.Add(
                        ProgramValidationErrorCode::Action,
                        location,
                        "Set target is read-only or its expression type differs");
                }
            }
            break;
        case ActionOpcode::Toggle:
            if (instruction.operand0 >= storage.valueRefs.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Toggle reference or unused operand is invalid");
            } else {
                const ValueRef& value = storage.valueRefs[instruction.operand0];
                if (!IsWritableValue(value) || value.type != ValueType::State) {
                    context.Add(
                        ProgramValidationErrorCode::Action,
                        location,
                        "Toggle requires a writable State value");
                }
            }
            break;
        case ActionOpcode::Exec:
            if (InvalidRequiredString(
                    storage,
                    StringId{instruction.operand0})
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Exec requires a valid non-empty NUL-free command string");
            }
            break;
        case ActionOpcode::Jump:
            fallthrough = false;
            if (instruction.operand1 != 0U || instruction.operand0 >= count) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Jump target or unused operand is invalid");
            } else {
                if (instruction.operand0 <= local) {
                    const bool precededByYield = local > 0U
                        && storage.actionCode[
                            static_cast<std::size_t>(descriptor.code.begin)
                            + local - 1U].opcode == ActionOpcode::Yield;
                    if (!precededByYield || instruction.operand0 == local) {
                        context.Add(
                            ProgramValidationErrorCode::Action,
                            location,
                            "a backward Jump must be immediately preceded by Yield");
                    }
                }
                AddActionSuccessor(
                    reachable,
                    pending,
                    instruction.operand0,
                    context,
                    location);
            }
            break;
        case ActionOpcode::JumpIfFalse:
            if (ExpressionResultType(storage, instruction.operand0)
                    != ExpressionType::Boolean
                || instruction.operand1 <= local
                || instruction.operand1 >= count) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "JumpIfFalse requires a Boolean expression and a forward target");
            } else {
                AddActionSuccessor(
                    reachable,
                    pending,
                    instruction.operand1,
                    context,
                    location);
            }
            break;
        case ActionOpcode::RepeatInit:
            requiredFrames = (std::max)(requiredFrames, instruction.operand0 + 1U);
            if (instruction.operand0 >= descriptor.repeatFrameCount
                || ExpressionResultType(storage, instruction.operand1)
                    != ExpressionType::Number) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "RepeatInit frame or Number expression is invalid");
            }
            break;
        case ActionOpcode::RepeatCheck:
            requiredFrames = (std::max)(requiredFrames, instruction.operand0 + 1U);
            if (instruction.operand0 >= descriptor.repeatFrameCount
                || instruction.operand1 <= local
                || instruction.operand1 >= count) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "RepeatCheck frame or forward end target is invalid");
            } else {
                AddActionSuccessor(
                    reachable,
                    pending,
                    instruction.operand1,
                    context,
                    location);
            }
            break;
        case ActionOpcode::RepeatNext:
            requiredFrames = (std::max)(requiredFrames, instruction.operand0 + 1U);
            if (instruction.operand0 >= descriptor.repeatFrameCount
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "RepeatNext frame or unused operand is invalid");
            }
            break;
        case ActionOpcode::Yield:
            if (instruction.operand0 != 0U || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "Yield has nonzero unused operands");
            }
            break;
        case ActionOpcode::End:
            fallthrough = false;
            reachableEnd = true;
            if (instruction.operand0 != 0U || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "End has nonzero unused operands");
            }
            break;
        default:
            context.Add(
                ProgramValidationErrorCode::Action,
                location,
                "unknown action opcode");
            break;
        }

        if (fallthrough) {
            AddActionSuccessor(reachable, pending, next, context, location);
        }
    }

    const std::size_t lastAbsolute = static_cast<std::size_t>(descriptor.code.begin)
        + count - 1U;
    if (storage.actionCode[lastAbsolute].opcode != ActionOpcode::End
        || !reachableEnd) {
        context.Add(
            ProgramValidationErrorCode::Action,
            descriptorLocation,
            "action program must end with a reachable End instruction");
    }
    if (std::any_of(reachable.begin(), reachable.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Action,
            descriptorLocation,
            "action program contains unreachable instructions");
    }
    if (requiredFrames != descriptor.repeatFrameCount) {
        context.Add(
            ProgramValidationErrorCode::Action,
            descriptorLocation + ".repeatFrameCount",
            "declared repeat frame count does not match referenced frames");
    }
    if (acquiredControls.size() != descriptor.maximumOwnedControlCount) {
        context.Add(
            ProgramValidationErrorCode::Action,
            descriptorLocation + ".maximumOwnedControlCount",
            "declared owned-control count does not match acquisition instructions");
    }
}

template <typename Descriptor, typename RangeMember>
void ValidateRangeCoverage(
    const std::vector<Descriptor>& descriptors,
    std::size_t tableSize,
    RangeMember rangeMember,
    std::string_view tableName,
    ValidationContext& context)
{
    std::vector<bool> covered(tableSize, false);
    for (std::size_t descriptorIndex = 0;
         descriptorIndex < descriptors.size();
         ++descriptorIndex) {
        const TableRange range = descriptors[descriptorIndex].*rangeMember;
        if (!ValidRange(range, tableSize)) {
            continue;
        }
        const std::uint64_t end = static_cast<std::uint64_t>(range.begin)
            + range.count;
        for (std::uint64_t index = range.begin; index < end; ++index) {
            const std::size_t position = static_cast<std::size_t>(index);
            if (covered[position]) {
                context.Add(
                    ProgramValidationErrorCode::Range,
                    At(tableName, descriptorIndex),
                    "descriptor ranges overlap");
                break;
            }
            covered[position] = true;
        }
    }
    if (std::any_of(covered.begin(), covered.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Range,
            std::string(tableName),
            "descriptor ranges do not cover the complete instruction table");
    }
}

void AddExpectedControlUse(
    const CompiledProgramStorage& storage,
    std::vector<std::uint8_t>& expected,
    ControlRefId control,
    ControlUse use,
    ValidationContext& context,
    const std::string& location)
{
    if (!ValidId(control, storage.controls.size())) {
        context.Add(
            ProgramValidationErrorCode::ControlRequirement,
            location,
            "referenced ControlRefId is outside the canonical control pool");
        return;
    }
    expected[control.value] = static_cast<std::uint8_t>(
        expected[control.value] | ToControlUseBits(use));
}

void ValidateCanonicalPools(
    const CompiledProgramStorage& storage,
    ValidationContext& context)
{
    for (std::size_t index = 0; index < storage.strings.size(); ++index) {
        if (storage.strings[index].size()
            > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            context.Add(
                ProgramValidationErrorCode::TableSize,
                At("strings", index),
                "string byte length exceeds its 32-bit encoding");
        }
        if (!support::IsValidUtf8(storage.strings[index])) {
            context.Add(
                ProgramValidationErrorCode::String,
                At("strings", index),
                "string is not valid UTF-8");
        }
        if (index > 0U && storage.strings[index - 1U] >= storage.strings[index]) {
            context.Add(
                ProgramValidationErrorCode::String,
                At("strings", index),
                "string pool is not strictly canonical");
        }
    }
    for (std::size_t index = 0; index < storage.controls.size(); ++index) {
        if (!ValidControl(storage.controls[index])) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("controls", index),
                "control identity is invalid");
        }
        if (index > 0U && storage.controls[index - 1U] >= storage.controls[index]) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("controls", index),
                "control pool is not strictly canonical");
        }
    }
    for (std::size_t index = 0; index < storage.valueRefs.size(); ++index) {
        if (!ValidateValueRefShape(storage.valueRefs[index], storage)) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("valueRefs", index),
                "value domain, type, or index is invalid");
        }
        if (index > 0U && storage.valueRefs[index - 1U] >= storage.valueRefs[index]) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("valueRefs", index),
                "value reference pool is not strictly canonical");
        }
    }
    for (std::size_t index = 0; index < storage.numberConstants.size(); ++index) {
        const double value = storage.numberConstants[index];
        if (!std::isfinite(value) || (value == 0.0 && std::signbit(value))) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("numberConstants", index),
                "number constant is non-finite or negative zero");
        }
        if (index > 0U
            && std::bit_cast<std::uint64_t>(storage.numberConstants[index - 1U])
                >= std::bit_cast<std::uint64_t>(value)) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("numberConstants", index),
                "number constant pool is not strictly canonical");
        }
    }
    for (std::size_t index = 0; index < storage.durationConstants.size(); ++index) {
        if (storage.durationConstants[index].nanoseconds < 0) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("durationConstants", index),
                "duration constant is negative");
        }
        if (index > 0U
            && storage.durationConstants[index - 1U]
                >= storage.durationConstants[index]) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("durationConstants", index),
                "duration constant pool is not strictly canonical");
        }
    }
}

void ValidateSourceAndSettings(
    const CompiledProgramStorage& storage,
    ValidationContext& context)
{
    if (!ValidId(storage.source.displayPath, storage.strings.size())) {
        context.Add(
            ProgramValidationErrorCode::Identifier,
            "source.displayPath",
            "display path StringId is invalid");
    }
    if (HasEmbeddedNul(storage, storage.source.displayPath)) {
        context.Add(
            ProgramValidationErrorCode::String,
            "source",
            "source display path contains an embedded NUL");
    }
    if (!ValidRange(storage.source.lineStarts, storage.lineStarts.size())
        || storage.source.lineStarts.begin != 0U
        || storage.source.lineStarts.count != storage.lineStarts.size()) {
        context.Add(
            ProgramValidationErrorCode::Range,
            "source.lineStarts",
            "line-start range must cover the complete lineStarts table");
    }
    if (storage.lineStarts.empty() || storage.lineStarts.front() != 0U) {
        context.Add(
            ProgramValidationErrorCode::Source,
            "lineStarts",
            "line-start table must begin with zero");
    }
    for (std::size_t index = 0; index < storage.lineStarts.size(); ++index) {
        if (storage.lineStarts[index] > storage.source.byteLength
            || (index > 0U
                && storage.lineStarts[index - 1U] >= storage.lineStarts[index])) {
            context.Add(
                ProgramValidationErrorCode::Source,
                At("lineStarts", index),
                "line-start offsets must be strictly increasing and in range");
        }
    }

    const TargetSelector& target = storage.settings.target;
    if (!ValidSpan(target.source, storage.source.byteLength)) {
        context.Add(
            ProgramValidationErrorCode::Source,
            "settings.target.source",
            "target source span is outside the source file");
    }
    switch (target.kind) {
    case TargetSelectorKind::Unspecified:
    case TargetSelectorKind::Global:
        if (target.text.IsValid()) {
            context.Add(
                ProgramValidationErrorCode::Identifier,
                "settings.target.text",
                "non-text target kind must use an invalid StringId");
        }
        break;
    case TargetSelectorKind::Executable:
        if (InvalidRequiredString(storage, target.text)) {
            context.Add(
                ProgramValidationErrorCode::Identifier,
                "settings.target.text",
                "text target requires a valid non-empty NUL-free StringId");
        }
        break;
    default:
        context.Add(
            ProgramValidationErrorCode::Value,
            "settings.target.kind",
            "target selector kind is unknown");
        break;
    }
    if (storage.settings.tapDuration.nanoseconds < 0
        || storage.settings.actionGap.nanoseconds < 0) {
        context.Add(
            ProgramValidationErrorCode::Value,
            "settings",
            "program durations must be nonnegative");
    }
}

void ValidateUserValuesAndDebug(
    const CompiledProgramStorage& storage,
    ValidationContext& context)
{
    for (std::size_t index = 0;
         index < storage.userValues.initialStates.size();
         ++index) {
        if (storage.userValues.initialStates[index] > 1U) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("userValues.initialStates", index),
                "state initial value must be zero or one");
        }
    }
    for (std::size_t index = 0;
         index < storage.userValues.initialNumbers.size();
         ++index) {
        if (!std::isfinite(storage.userValues.initialNumbers[index])) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("userValues.initialNumbers", index),
                "number initial value must be finite");
        }
    }
    for (std::size_t index = 0;
         index < storage.userValues.initialDurations.size();
         ++index) {
        if (storage.userValues.initialDurations[index].nanoseconds < 0) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("userValues.initialDurations", index),
                "duration initial value must be nonnegative");
        }
    }

    const std::size_t expectedVariableCount =
        storage.userValues.initialStates.size()
        + storage.userValues.initialNumbers.size()
        + storage.userValues.initialDurations.size();
    if (storage.debugInfo.variables.size() != expectedVariableCount) {
        context.Add(
            ProgramValidationErrorCode::DebugInfo,
            "debugInfo.variables",
            "variable debug records must cover every user slot exactly once");
    }
    std::set<std::pair<ValueDomain, std::uint32_t>> covered;
    std::uint32_t previousDeclaration = 0U;
    bool havePreviousDeclaration = false;
    for (std::size_t index = 0;
         index < storage.debugInfo.variables.size();
         ++index) {
        const VariableDebugRecord& variable = storage.debugInfo.variables[index];
        if (!ValidId(variable.name, storage.strings.size())
            || !ValidId(variable.value, storage.valueRefs.size())) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.variables", index),
                "variable name or value reference is invalid");
        } else {
            const ValueRef& value = storage.valueRefs[variable.value.value];
            if (value.domain != ValueDomain::UserState
                && value.domain != ValueDomain::UserNumber
                && value.domain != ValueDomain::UserDuration) {
                context.Add(
                    ProgramValidationErrorCode::DebugInfo,
                    At("debugInfo.variables", index),
                    "variable debug record must reference a user value");
            } else if (!covered.insert({value.domain, value.index}).second) {
                context.Add(
                    ProgramValidationErrorCode::DebugInfo,
                    At("debugInfo.variables", index),
                    "multiple debug records reference the same user slot");
            }
        }
        if (!ValidSpan(variable.declaration, storage.source.byteLength)) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.variables", index) + ".declaration",
                "variable declaration span is outside the source file");
        }
        if (havePreviousDeclaration
            && previousDeclaration >= variable.declaration.beginByte) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.variables", index) + ".declaration",
                "variable debug records are not in declaration order");
        }
        previousDeclaration = variable.declaration.beginByte;
        havePreviousDeclaration = true;
    }
}

void ValidatePauseControls(
    const CompiledProgramStorage& storage,
    ValidationContext& context,
    std::vector<std::uint8_t>& expectedControlUses,
    std::set<std::uint32_t>& sourceOrdinals)
{
    std::vector<bool> coveredRules(storage.pauseControlRules.size(), false);
    EventKey previousKey{};
    bool havePreviousKey = false;
    for (std::size_t bucketIndex = 0;
         bucketIndex < storage.pauseControlBuckets.size();
         ++bucketIndex) {
        const PauseControlBucket& bucket = storage.pauseControlBuckets[bucketIndex];
        const std::string bucketLocation = At("pauseControlBuckets", bucketIndex);
        if (!ValidId(bucket.key.control, storage.controls.size())
            || !ValidEventTransition(bucket.key.transition)
            || (havePreviousKey && previousKey >= bucket.key)) {
            context.Add(
                ProgramValidationErrorCode::Rule,
                bucketLocation,
                "pause-control buckets must have strictly sorted valid keys");
        }
        previousKey = bucket.key;
        havePreviousKey = true;
        AddExpectedControlUse(
            storage,
            expectedControlUses,
            bucket.key.control,
            ControlUse::EventSource,
            context,
            bucketLocation);

        if (!ValidRange(bucket.rules, storage.pauseControlRules.size())) {
            context.Add(
                ProgramValidationErrorCode::Range,
                bucketLocation + ".rules",
                "pause-control rule range is outside its table");
            continue;
        }
        std::uint32_t previousOrdinal = 0U;
        bool havePreviousOrdinal = false;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const std::size_t ruleIndex = static_cast<std::size_t>(rawIndex);
            if (coveredRules[ruleIndex]) {
                context.Add(
                    ProgramValidationErrorCode::Range,
                    bucketLocation + ".rules",
                    "pause-control bucket rule ranges overlap");
            }
            coveredRules[ruleIndex] = true;
            const PauseControlRule& rule = storage.pauseControlRules[ruleIndex];
            const std::string ruleLocation = At("pauseControlRules", ruleIndex);
            if ((havePreviousOrdinal && previousOrdinal >= rule.sourceOrdinal)
                || !sourceOrdinals.insert(rule.sourceOrdinal).second) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".sourceOrdinal",
                    "rule source ordinals must be globally unique and increase in a bucket");
            }
            previousOrdinal = rule.sourceOrdinal;
            havePreviousOrdinal = true;
            if (!ValidSpan(rule.source, storage.source.byteLength)) {
                context.Add(
                    ProgramValidationErrorCode::Source,
                    ruleLocation + ".source",
                    "pause-control source span is outside the source file");
            }
            if (rule.condition.IsValid()
                && (!ValidId(rule.condition, storage.expressions.size())
                    || storage.expressions[rule.condition.value].resultType
                        != ExpressionType::Boolean)) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".condition",
                    "pause-control condition must be invalid or Boolean");
            }
            if (rule.delivery != Delivery::Observe
                && rule.delivery != Delivery::Consume) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".delivery",
                    "pause-control delivery value is unknown");
            }
            if (rule.effect != PauseEffect::On
                && rule.effect != PauseEffect::Off
                && rule.effect != PauseEffect::Toggle) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".effect",
                    "pause-control effect is unknown");
            }
        }
    }
    if (std::any_of(coveredRules.begin(), coveredRules.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Range,
            "pauseControlBuckets",
            "pause-control bucket ranges do not cover the complete rule table");
    }
}

void ValidateRulesAndMappings(
    const CompiledProgramStorage& storage,
    ValidationContext& context,
    std::vector<std::uint8_t>& expectedControlUses,
    std::set<std::uint32_t>& sourceOrdinals)
{
    std::vector<std::uint32_t> mappingRuleCounts(storage.mappings.size(), 0U);
    std::vector<std::uint32_t> slotMappingCounts(storage.mappingSlots.size(), 0U);
    for (std::size_t index = 0; index < storage.mappingSlots.size(); ++index) {
        const MappingSlotDescriptor& slot = storage.mappingSlots[index];
        if (!ValidId(slot.source, storage.controls.size())
            || (index > 0U
                && storage.mappingSlots[index - 1U].source >= slot.source)) {
            context.Add(
                ProgramValidationErrorCode::Mapping,
                At("mappingSlots", index),
                "mapping slots must contain strictly sorted valid source controls");
        }
        AddExpectedControlUse(
            storage,
            expectedControlUses,
            slot.source,
            ControlUse::EventSource,
            context,
            At("mappingSlots", index));
    }
    for (std::size_t index = 0; index < storage.mappings.size(); ++index) {
        const MappingDescriptor& mapping = storage.mappings[index];
        if (!ValidId(mapping.slot, storage.mappingSlots.size())
            || !ValidId(mapping.target, storage.controls.size())
            || !ValidSpan(mapping.source, storage.source.byteLength)) {
            context.Add(
                ProgramValidationErrorCode::Mapping,
                At("mappings", index),
                "mapping slot, target, or source span is invalid");
        } else {
            ++slotMappingCounts[mapping.slot.value];
        }
        AddExpectedControlUse(
            storage,
            expectedControlUses,
            mapping.target,
            ControlUse::OutputDownUp,
            context,
            At("mappings", index));
        AddExpectedControlUse(
            storage,
            expectedControlUses,
            mapping.target,
            ControlUse::OutputRepeat,
            context,
            At("mappings", index));
    }

    std::vector<bool> coveredRules(storage.rules.size(), false);
    EventKey previousKey{};
    bool havePreviousKey = false;
    for (std::size_t bucketIndex = 0;
         bucketIndex < storage.eventBuckets.size();
         ++bucketIndex) {
        const EventBucket& bucket = storage.eventBuckets[bucketIndex];
        const std::string bucketLocation = At("eventBuckets", bucketIndex);
        if (!ValidId(bucket.key.control, storage.controls.size())
            || !ValidEventTransition(bucket.key.transition)
            || (havePreviousKey && previousKey >= bucket.key)) {
            context.Add(
                ProgramValidationErrorCode::Rule,
                bucketLocation,
                "event buckets must have strictly sorted valid keys");
        }
        previousKey = bucket.key;
        havePreviousKey = true;
        AddExpectedControlUse(
            storage,
            expectedControlUses,
            bucket.key.control,
            ControlUse::EventSource,
            context,
            bucketLocation);

        if (!ValidRange(bucket.rules, storage.rules.size())) {
            context.Add(
                ProgramValidationErrorCode::Range,
                bucketLocation + ".rules",
                "rule range is outside the rule table");
            continue;
        }
        std::uint32_t previousOrdinal = 0U;
        bool havePreviousOrdinal = false;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const std::size_t ruleIndex = static_cast<std::size_t>(rawIndex);
            if (coveredRules[ruleIndex]) {
                context.Add(
                    ProgramValidationErrorCode::Range,
                    bucketLocation + ".rules",
                    "event bucket rule ranges overlap");
            }
            coveredRules[ruleIndex] = true;
            const CompiledRule& rule = storage.rules[ruleIndex];
            const std::string ruleLocation = At("rules", ruleIndex);
            if ((havePreviousOrdinal && previousOrdinal >= rule.sourceOrdinal)
                || !sourceOrdinals.insert(rule.sourceOrdinal).second) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".sourceOrdinal",
                    "rule source ordinals must be globally unique and increase in a bucket");
            }
            previousOrdinal = rule.sourceOrdinal;
            havePreviousOrdinal = true;
            if (!ValidSpan(rule.source, storage.source.byteLength)) {
                context.Add(
                    ProgramValidationErrorCode::Source,
                    ruleLocation + ".source",
                    "rule source span is outside the source file");
            }
            if (rule.condition.IsValid()
                && (!ValidId(rule.condition, storage.expressions.size())
                    || storage.expressions[rule.condition.value].resultType
                        != ExpressionType::Boolean)) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".condition",
                    "rule condition must be invalid or a Boolean expression");
            }
            if (rule.delivery != Delivery::Observe
                && rule.delivery != Delivery::Consume) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".delivery",
                    "rule delivery value is unknown");
            }
            if (rule.flow != MatchFlow::Stop
                && rule.flow != MatchFlow::Continue) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".flow",
                    "rule flow value is unknown");
            }

            if (rule.kind == RuleKind::Event) {
                if (rule.mapping.IsValid()
                    || (rule.action.IsValid()
                        && !ValidId(rule.action, storage.actionPrograms.size()))) {
                    context.Add(
                        ProgramValidationErrorCode::Rule,
                        ruleLocation,
                        "event rule has an invalid action or a mapping reference");
                }
            } else if (rule.kind == RuleKind::MappingDown) {
                if (!ValidId(rule.mapping, storage.mappings.size())
                    || rule.action.IsValid()
                    || rule.delivery != Delivery::Consume
                    || rule.flow != MatchFlow::Stop
                    || bucket.key.transition != EventTransition::Down) {
                    context.Add(
                        ProgramValidationErrorCode::Rule,
                        ruleLocation,
                        "mapping-down rule fields or containing event are invalid");
                } else {
                    ++mappingRuleCounts[rule.mapping.value];
                    const MappingDescriptor& mapping = storage.mappings[rule.mapping.value];
                    if (!ValidId(mapping.slot, storage.mappingSlots.size())
                        || storage.mappingSlots[mapping.slot.value].source
                            != bucket.key.control) {
                        context.Add(
                            ProgramValidationErrorCode::Mapping,
                            ruleLocation,
                            "mapping-down rule source does not match its mapping slot");
                    }
                }
            } else {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".kind",
                    "rule kind is unknown");
            }
        }
    }
    if (std::any_of(coveredRules.begin(), coveredRules.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Range,
            "eventBuckets",
            "event bucket ranges do not cover the complete rule table");
    }
    for (std::size_t index = 0; index < mappingRuleCounts.size(); ++index) {
        if (mappingRuleCounts[index] != 1U) {
            context.Add(
                ProgramValidationErrorCode::Mapping,
                At("mappings", index),
                "each mapping descriptor must have exactly one mapping-down rule");
        }
    }
    for (std::size_t index = 0; index < slotMappingCounts.size(); ++index) {
        if (slotMappingCounts[index] == 0U) {
            context.Add(
                ProgramValidationErrorCode::Mapping,
                At("mappingSlots", index),
                "each mapping slot must be referenced by a mapping descriptor");
        }
    }
}

void ValidateControlRequirements(
    const CompiledProgramStorage& storage,
    const std::vector<std::uint8_t>& expected,
    ValidationContext& context)
{
    std::vector<std::uint8_t> actual(storage.controls.size(), 0U);
    std::uint32_t previous = 0U;
    bool havePrevious = false;
    for (std::size_t index = 0;
         index < storage.controlRequirements.size();
         ++index) {
        const ControlRequirement& requirement = storage.controlRequirements[index];
        if (!ValidId(requirement.control, storage.controls.size())
            || requirement.uses == 0U
            || (requirement.uses & static_cast<std::uint8_t>(~kAllControlUseBits))
                != 0U
            || (havePrevious && previous >= requirement.control.value)) {
            context.Add(
                ProgramValidationErrorCode::ControlRequirement,
                At("controlRequirements", index),
                "control requirement ID, use bits, or ordering is invalid");
            continue;
        }
        previous = requirement.control.value;
        havePrevious = true;
        actual[requirement.control.value] = requirement.uses;
    }
    if (actual != expected) {
        context.Add(
            ProgramValidationErrorCode::ControlRequirement,
            "controlRequirements",
            "control requirements do not exactly match compiled control uses");
    }
}

void ValidateDebugSpans(
    const CompiledProgramStorage& storage,
    ValidationContext& context)
{
    if (storage.debugInfo.expressionInstructionSpans.size()
        != storage.expressionCode.size()) {
        context.Add(
            ProgramValidationErrorCode::DebugInfo,
            "debugInfo.expressionInstructionSpans",
            "expression span table length differs from expressionCode");
    }
    if (storage.debugInfo.actionInstructionSpans.size()
        != storage.actionCode.size()) {
        context.Add(
            ProgramValidationErrorCode::DebugInfo,
            "debugInfo.actionInstructionSpans",
            "action span table length differs from actionCode");
    }
    for (std::size_t index = 0;
         index < storage.debugInfo.expressionInstructionSpans.size();
         ++index) {
        if (!ValidSpan(
                storage.debugInfo.expressionInstructionSpans[index],
                storage.source.byteLength)) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.expressionInstructionSpans", index),
                "expression instruction span is outside the source file");
        }
    }
    for (std::size_t index = 0;
         index < storage.debugInfo.actionInstructionSpans.size();
         ++index) {
        if (!ValidSpan(
                storage.debugInfo.actionInstructionSpans[index],
                storage.source.byteLength)) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.actionInstructionSpans", index),
                "action instruction span is outside the source file");
        }
    }
}

[[nodiscard]] std::uint32_t ToCount(std::size_t value) noexcept
{
    return value > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t SaturatingAdd(
    std::uint32_t left,
    std::uint32_t right) noexcept
{
    const std::uint64_t sum = static_cast<std::uint64_t>(left) + right;
    return sum > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(sum);
}

[[nodiscard]] bool HasMappingSource(
    const CompiledProgramStorage& storage,
    ControlRefId control) noexcept
{
    return std::any_of(
        storage.mappingSlots.begin(),
        storage.mappingSlots.end(),
        [control](const MappingSlotDescriptor& slot) {
            return slot.source == control;
        });
}

} // namespace

ProgramRequirements ComputeProgramRequirements(
    const CompiledProgramStorage& storage)
{
    ProgramRequirements requirements{};
    requirements.stateSlotCount = ToCount(storage.userValues.initialStates.size());
    requirements.numberSlotCount = ToCount(storage.userValues.initialNumbers.size());
    requirements.durationSlotCount = ToCount(storage.userValues.initialDurations.size());
    requirements.mappingSlotCount = ToCount(storage.mappingSlots.size());

    for (const ExpressionDescriptor& expression : storage.expressions) {
        requirements.maximumExpressionStackDepth = (std::max)(
            requirements.maximumExpressionStackDepth,
            expression.maximumStackDepth);
    }
    for (const ActionProgramDescriptor& action : storage.actionPrograms) {
        requirements.maximumRepeatFramesPerTask = (std::max)(
            requirements.maximumRepeatFramesPerTask,
            action.repeatFrameCount);
        requirements.maximumOwnedControlsPerTask = (std::max)(
            requirements.maximumOwnedControlsPerTask,
            action.maximumOwnedControlCount);
    }
    requirements.requiresProcessLaunch = std::any_of(
        storage.actionCode.begin(),
        storage.actionCode.end(),
        [](const ActionInstruction& instruction) {
            return instruction.opcode == ActionOpcode::Exec;
        });
    requirements.maximumMappingOperationsPerEvent = storage.mappings.empty()
        ? 0U
        : 1U;
    requirements.maximumTransactionItemsPerEvent =
        requirements.maximumMappingOperationsPerEvent;

    std::map<EventKey, std::uint32_t> predicateStepsByEvent;
    for (const PauseControlBucket& bucket : storage.pauseControlBuckets) {
        requirements.maximumPauseRulesPerEvent = (std::max)(
            requirements.maximumPauseRulesPerEvent,
            bucket.rules.count);
        if (!ValidRange(bucket.rules, storage.pauseControlRules.size())) {
            continue;
        }
        std::uint32_t predicateSteps = 0U;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const PauseControlRule& rule = storage.pauseControlRules[
                static_cast<std::size_t>(rawIndex)];
            if (ValidId(rule.condition, storage.expressions.size())) {
                predicateSteps = SaturatingAdd(
                    predicateSteps,
                    storage.expressions[rule.condition.value].code.count);
            }
        }
        predicateStepsByEvent[bucket.key] = SaturatingAdd(
            predicateStepsByEvent[bucket.key],
            predicateSteps);
    }

    for (const EventBucket& bucket : storage.eventBuckets) {
        requirements.maximumRulesPerEvent = (std::max)(
            requirements.maximumRulesPerEvent,
            bucket.rules.count);
        if (!ValidRange(bucket.rules, storage.rules.size())) {
            continue;
        }
        std::uint32_t predicateSteps = 0U;
        std::uint32_t tasks = 0U;
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        for (std::uint64_t rawIndex = bucket.rules.begin;
             rawIndex < end;
             ++rawIndex) {
            const CompiledRule& rule = storage.rules[static_cast<std::size_t>(rawIndex)];
            if (ValidId(rule.condition, storage.expressions.size())) {
                predicateSteps = SaturatingAdd(
                    predicateSteps,
                    storage.expressions[rule.condition.value].code.count);
            }
            if (rule.kind == RuleKind::Event
                && ValidId(rule.action, storage.actionPrograms.size())) {
                tasks = SaturatingAdd(tasks, 1U);
            }
        }
        const std::uint32_t mappingOperations = HasMappingSource(
            storage,
            bucket.key.control) ? 1U : 0U;
        predicateStepsByEvent[bucket.key] = SaturatingAdd(
            predicateStepsByEvent[bucket.key],
            predicateSteps);
        requirements.maximumTasksPerEvent = (std::max)(
            requirements.maximumTasksPerEvent,
            tasks);
        requirements.maximumTransactionItemsPerEvent = (std::max)(
            requirements.maximumTransactionItemsPerEvent,
            SaturatingAdd(tasks, mappingOperations));
    }
    for (const auto& entry : predicateStepsByEvent) {
        requirements.maximumPredicateStepsPerEvent = (std::max)(
            requirements.maximumPredicateStepsPerEvent,
            entry.second);
    }
    return requirements;
}

std::vector<ProgramValidationError> ValidateCompiledProgram(
    const CompiledProgramStorage& storage)
{
    ValidationContext context;
    const std::vector<std::pair<std::string_view, std::size_t>> tableSizes{
        {"strings", storage.strings.size()},
        {"lineStarts", storage.lineStarts.size()},
        {"controls", storage.controls.size()},
        {"controlRequirements", storage.controlRequirements.size()},
        {"valueRefs", storage.valueRefs.size()},
        {"userValues.initialStates", storage.userValues.initialStates.size()},
        {"userValues.initialNumbers", storage.userValues.initialNumbers.size()},
        {"userValues.initialDurations", storage.userValues.initialDurations.size()},
        {"numberConstants", storage.numberConstants.size()},
        {"durationConstants", storage.durationConstants.size()},
        {"expressions", storage.expressions.size()},
        {"expressionCode", storage.expressionCode.size()},
        {"actionPrograms", storage.actionPrograms.size()},
        {"actionCode", storage.actionCode.size()},
        {"mappingSlots", storage.mappingSlots.size()},
        {"mappings", storage.mappings.size()},
        {"pauseControlBuckets", storage.pauseControlBuckets.size()},
        {"pauseControlRules", storage.pauseControlRules.size()},
        {"eventBuckets", storage.eventBuckets.size()},
        {"rules", storage.rules.size()},
        {"debugInfo.variables", storage.debugInfo.variables.size()},
        {"debugInfo.expressionInstructionSpans",
            storage.debugInfo.expressionInstructionSpans.size()},
        {"debugInfo.actionInstructionSpans",
            storage.debugInfo.actionInstructionSpans.size()},
    };
    for (const auto& [name, size] : tableSizes) {
        if (size >= kInvalidProgramIndex) {
            context.Add(
                ProgramValidationErrorCode::TableSize,
                std::string(name),
                "table length reaches the reserved invalid ID");
        }
    }

    ValidateCanonicalPools(storage, context);
    ValidateSourceAndSettings(storage, context);
    ValidateUserValuesAndDebug(storage, context);

    ValidateRangeCoverage(
        storage.expressions,
        storage.expressionCode.size(),
        &ExpressionDescriptor::code,
        "expressions",
        context);
    for (std::size_t index = 0;
         index < storage.expressions.size() && !context.Full();
         ++index) {
        ValidateExpressionDescriptor(storage, index, context);
    }

    ValidateRangeCoverage(
        storage.actionPrograms,
        storage.actionCode.size(),
        &ActionProgramDescriptor::code,
        "actionPrograms",
        context);
    for (std::size_t index = 0;
         index < storage.actionPrograms.size() && !context.Full();
         ++index) {
        ValidateActionDescriptor(storage, index, context);
    }

    std::vector<std::uint8_t> expectedControlUses(storage.controls.size(), 0U);
    for (std::size_t index = 0;
         index < storage.expressionCode.size();
         ++index) {
        const ExpressionInstruction& instruction = storage.expressionCode[index];
        if (instruction.opcode == ExpressionOpcode::ReadControlHeld
            && instruction.operand0 < storage.controls.size()) {
            expectedControlUses[instruction.operand0] = static_cast<std::uint8_t>(
                expectedControlUses[instruction.operand0]
                | ToControlUseBits(ControlUse::PhysicalState));
        }
    }
    for (std::size_t index = 0; index < storage.actionCode.size(); ++index) {
        const ActionInstruction& instruction = storage.actionCode[index];
        if ((instruction.opcode == ActionOpcode::Press
             || instruction.opcode == ActionOpcode::Release
             || instruction.opcode == ActionOpcode::Tap)
            && instruction.operand0 < storage.controls.size()) {
            expectedControlUses[instruction.operand0] = static_cast<std::uint8_t>(
                expectedControlUses[instruction.operand0]
                | ToControlUseBits(ControlUse::OutputDownUp));
        }
    }
    std::set<std::uint32_t> sourceOrdinals;
    ValidatePauseControls(
        storage,
        context,
        expectedControlUses,
        sourceOrdinals);
    ValidateRulesAndMappings(
        storage,
        context,
        expectedControlUses,
        sourceOrdinals);
    ValidateControlRequirements(storage, expectedControlUses, context);
    ValidateDebugSpans(storage, context);

    if (ComputeProgramRequirements(storage) != storage.requirements) {
        context.Add(
            ProgramValidationErrorCode::Requirements,
            "requirements",
            "stored requirements do not match the final program tables");
    }
    return std::move(context).Take();
}

} // namespace inputweaver
