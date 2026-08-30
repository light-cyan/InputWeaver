#include "expression_vm.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace inputweaver {
namespace {

[[nodiscard]] RuntimeEvaluationResult Fault(
    RuntimeEvaluationFault fault,
    std::uint32_t position) noexcept
{
    RuntimeEvaluationResult result{};
    result.fault = fault;
    result.instructionPosition = position;
    return result;
}

[[nodiscard]] bool IsFinite(double value) noexcept
{
    return std::isfinite(value) != 0;
}

[[nodiscard]] bool ConvertDurationResult(
    double value,
    DurationValue& result) noexcept
{
    if (!IsFinite(value)) {
        return false;
    }
    if (value <= 0.0) {
        result.nanoseconds = 0;
        return true;
    }
    const double maximum = static_cast<double>(
        (std::numeric_limits<std::int64_t>::max)());
    if (value > maximum) {
        return false;
    }
    result.nanoseconds = static_cast<std::int64_t>(value);
    return result.nanoseconds >= 0;
}

[[nodiscard]] RuntimeEvaluationFault ApplyUnary(
    UnaryOperator operation,
    const RuntimeValue& operand,
    RuntimeValue& result) noexcept
{
    switch (operation) {
    case UnaryOperator::NumberIdentity:
        if (operand.type != ExpressionType::Number) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        result = operand;
        return RuntimeEvaluationFault::None;
    case UnaryOperator::NumberNegate:
        if (operand.type != ExpressionType::Number) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        result.type = ExpressionType::Number;
        result.numberValue = -operand.numberValue;
        return IsFinite(result.numberValue)
            ? RuntimeEvaluationFault::None
            : RuntimeEvaluationFault::NonFiniteNumber;
    case UnaryOperator::BooleanNot:
        if (operand.type != ExpressionType::Boolean) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        result.type = ExpressionType::Boolean;
        result.booleanValue = !operand.booleanValue;
        return RuntimeEvaluationFault::None;
    }
    return RuntimeEvaluationFault::InvalidInstruction;
}

[[nodiscard]] RuntimeEvaluationFault ApplyNumberBinary(
    BinaryOperator operation,
    double left,
    double right,
    RuntimeValue& result) noexcept
{
    result.type = ExpressionType::Number;
    switch (operation) {
    case BinaryOperator::NumberAdd:
        result.numberValue = left + right;
        break;
    case BinaryOperator::NumberSubtract:
        result.numberValue = left - right;
        break;
    case BinaryOperator::NumberMultiply:
        result.numberValue = left * right;
        break;
    case BinaryOperator::NumberDivide:
        if (right == 0.0) {
            return RuntimeEvaluationFault::DivisionByZero;
        }
        result.numberValue = left / right;
        break;
    case BinaryOperator::NumberModulo:
        if (right == 0.0) {
            return RuntimeEvaluationFault::DivisionByZero;
        }
        result.numberValue = std::fmod(left, right);
        break;
    default:
        return RuntimeEvaluationFault::InvalidInstruction;
    }
    return IsFinite(result.numberValue)
        ? RuntimeEvaluationFault::None
        : RuntimeEvaluationFault::NonFiniteNumber;
}

[[nodiscard]] RuntimeEvaluationFault ApplyDurationBinary(
    BinaryOperator operation,
    const RuntimeValue& left,
    const RuntimeValue& right,
    RuntimeValue& result) noexcept
{
    result.type = ExpressionType::Duration;
    const std::int64_t leftDuration = left.durationValue.nanoseconds;
    const std::int64_t rightDuration = right.durationValue.nanoseconds;
    const std::int64_t maximum = (std::numeric_limits<std::int64_t>::max)();
    switch (operation) {
    case BinaryOperator::DurationAdd:
        if (left.type != ExpressionType::Duration
            || right.type != ExpressionType::Duration
            || leftDuration > maximum - rightDuration) {
            return RuntimeEvaluationFault::InvalidDuration;
        }
        result.durationValue.nanoseconds = leftDuration + rightDuration;
        return RuntimeEvaluationFault::None;
    case BinaryOperator::DurationSubtract:
        if (left.type != ExpressionType::Duration
            || right.type != ExpressionType::Duration) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        result.durationValue.nanoseconds = leftDuration <= rightDuration
            ? 0
            : leftDuration - rightDuration;
        return RuntimeEvaluationFault::None;
    case BinaryOperator::DurationMultiplyNumber:
        if (left.type != ExpressionType::Duration
            || right.type != ExpressionType::Number) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        return ConvertDurationResult(
            static_cast<double>(leftDuration) * right.numberValue,
            result.durationValue)
            ? RuntimeEvaluationFault::None
            : RuntimeEvaluationFault::InvalidDuration;
    case BinaryOperator::NumberMultiplyDuration:
        if (left.type != ExpressionType::Number
            || right.type != ExpressionType::Duration) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        return ConvertDurationResult(
            left.numberValue * static_cast<double>(rightDuration),
            result.durationValue)
            ? RuntimeEvaluationFault::None
            : RuntimeEvaluationFault::InvalidDuration;
    case BinaryOperator::DurationDivideNumber:
        if (left.type != ExpressionType::Duration
            || right.type != ExpressionType::Number) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        if (right.numberValue == 0.0) {
            return RuntimeEvaluationFault::DivisionByZero;
        }
        return ConvertDurationResult(
            static_cast<double>(leftDuration) / right.numberValue,
            result.durationValue)
            ? RuntimeEvaluationFault::None
            : RuntimeEvaluationFault::InvalidDuration;
    default:
        return RuntimeEvaluationFault::InvalidInstruction;
    }
}

[[nodiscard]] RuntimeEvaluationFault ApplyComparison(
    BinaryOperator operation,
    const RuntimeValue& left,
    const RuntimeValue& right,
    RuntimeValue& result) noexcept
{
    result.type = ExpressionType::Boolean;
    switch (operation) {
    case BinaryOperator::Equal:
    case BinaryOperator::NotEqual: {
        if (left.type != right.type) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        bool equal = false;
        switch (left.type) {
        case ExpressionType::State:
            equal = left.stateValue == right.stateValue;
            break;
        case ExpressionType::ControlState:
            equal = left.controlStateValue == right.controlStateValue;
            break;
        case ExpressionType::Number:
            equal = left.numberValue == right.numberValue;
            break;
        case ExpressionType::Duration:
            equal = left.durationValue.nanoseconds
                == right.durationValue.nanoseconds;
            break;
        default:
            return RuntimeEvaluationFault::TypeMismatch;
        }
        result.booleanValue = operation == BinaryOperator::Equal ? equal : !equal;
        return RuntimeEvaluationFault::None;
    }
    case BinaryOperator::NumberLess:
    case BinaryOperator::NumberLessEqual:
    case BinaryOperator::NumberGreater:
    case BinaryOperator::NumberGreaterEqual:
        if (left.type != ExpressionType::Number
            || right.type != ExpressionType::Number) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        if (operation == BinaryOperator::NumberLess) {
            result.booleanValue = left.numberValue < right.numberValue;
        } else if (operation == BinaryOperator::NumberLessEqual) {
            result.booleanValue = left.numberValue <= right.numberValue;
        } else if (operation == BinaryOperator::NumberGreater) {
            result.booleanValue = left.numberValue > right.numberValue;
        } else {
            result.booleanValue = left.numberValue >= right.numberValue;
        }
        return RuntimeEvaluationFault::None;
    default:
        return RuntimeEvaluationFault::InvalidInstruction;
    }
}

[[nodiscard]] RuntimeEvaluationFault ApplyBinary(
    BinaryOperator operation,
    const RuntimeValue& left,
    const RuntimeValue& right,
    RuntimeValue& result) noexcept
{
    if (operation >= BinaryOperator::NumberAdd
        && operation <= BinaryOperator::NumberModulo) {
        if (left.type != ExpressionType::Number
            || right.type != ExpressionType::Number) {
            return RuntimeEvaluationFault::TypeMismatch;
        }
        return ApplyNumberBinary(
            operation,
            left.numberValue,
            right.numberValue,
            result);
    }
    if (operation >= BinaryOperator::DurationAdd
        && operation <= BinaryOperator::DurationDivideNumber) {
        return ApplyDurationBinary(operation, left, right, result);
    }
    return ApplyComparison(operation, left, right, result);
}

} // namespace

RuntimeExpressionScratch::RuntimeExpressionScratch(std::size_t capacity)
    : storage_(capacity)
{
}

std::span<RuntimeValue> RuntimeExpressionScratch::Storage() noexcept
{
    return storage_;
}

std::size_t RuntimeExpressionScratch::Capacity() const noexcept
{
    return storage_.size();
}

RuntimeEvaluationResult EvaluateRuntimeExpression(
    const CompiledProgram& program,
    ExpressionId expression,
    const RuntimeExpressionState& state,
    RuntimeExpressionScratch& scratch) noexcept
{
    const auto expressions = program.Expressions();
    if (!expression.IsValid() || expression.value >= expressions.size()) {
        return Fault(RuntimeEvaluationFault::InvalidExpression, 0U);
    }
    const ExpressionDescriptor& descriptor = expressions[expression.value];
    const auto code = program.ExpressionCode().subspan(
        descriptor.code.begin,
        descriptor.code.count);
    std::span<RuntimeValue> stack = scratch.Storage();
    if (descriptor.maximumStackDepth > stack.size()) {
        return Fault(RuntimeEvaluationFault::StackOverflow, 0U);
    }

    std::size_t stackSize = 0U;
    std::uint32_t position = 0U;
    const auto push = [&stack, &stackSize](const RuntimeValue& value) noexcept {
        if (stackSize >= stack.size()) {
            return false;
        }
        stack[stackSize++] = value;
        return true;
    };
    const auto pop = [&stack, &stackSize](RuntimeValue& value) noexcept {
        if (stackSize == 0U) {
            return false;
        }
        value = stack[--stackSize];
        return true;
    };

    while (position < code.size()) {
        const ExpressionInstruction& instruction = code[position];
        RuntimeValue value{};
        switch (instruction.opcode) {
        case ExpressionOpcode::PushBoolean:
            value.type = ExpressionType::Boolean;
            value.booleanValue = instruction.operand0 != 0U;
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::PushState:
            value.type = ExpressionType::State;
            value.stateValue = static_cast<std::uint8_t>(instruction.operand0);
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::PushControlState:
            value.type = ExpressionType::ControlState;
            value.controlStateValue = static_cast<ControlState>(
                instruction.operand0);
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::PushNumber:
            if (instruction.operand0 >= program.NumberConstants().size()) {
                return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
            }
            value.type = ExpressionType::Number;
            value.numberValue = program.NumberConstants()[instruction.operand0];
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::PushDuration:
            if (instruction.operand0 >= program.DurationConstants().size()) {
                return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
            }
            value.type = ExpressionType::Duration;
            value.durationValue = program.DurationConstants()[instruction.operand0];
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::LoadValue: {
            const auto refs = program.ValueRefs();
            if (instruction.operand0 >= refs.size()) {
                return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
            }
            const ValueRef& ref = refs[instruction.operand0];
            value.type = ToExpressionType(ref.type);
            switch (ref.domain) {
            case ValueDomain::UserState:
                if (ref.index >= state.userStates.size()) {
                    return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
                }
                value.stateValue = state.userStates[ref.index];
                break;
            case ValueDomain::UserNumber:
                if (ref.index >= state.userNumbers.size()) {
                    return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
                }
                value.numberValue = state.userNumbers[ref.index];
                break;
            case ValueDomain::UserDuration:
                if (ref.index >= state.userDurations.size()) {
                    return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
                }
                value.durationValue = state.userDurations[ref.index];
                break;
            case ValueDomain::BuiltinState:
                if (ref.index != static_cast<std::uint32_t>(BuiltinState::Pause)) {
                    return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
                }
                value.stateValue = state.pauseOn ? 1U : 0U;
                break;
            case ValueDomain::BuiltinDuration:
                if (ref.index == static_cast<std::uint32_t>(BuiltinDuration::TapDuration)) {
                    value.durationValue = state.tapDuration;
                } else if (ref.index
                    == static_cast<std::uint32_t>(BuiltinDuration::ActionGap)) {
                    value.durationValue = state.actionGap;
                } else {
                    return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
                }
                break;
            }
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        }
        case ExpressionOpcode::ReadControlState:
            if (instruction.operand0 >= state.physicalHeld.size()) {
                return Fault(RuntimeEvaluationFault::InvalidInstruction, position);
            }
            if (instruction.type == ExpressionType::Boolean) {
                value.type = ExpressionType::Boolean;
                value.booleanValue = state.physicalHeld[instruction.operand0].load(
                    std::memory_order_acquire) != 0U;
            } else if (instruction.type == ExpressionType::ControlState) {
                value.type = ExpressionType::ControlState;
                value.controlStateValue = state.physicalHeld[instruction.operand0].load(
                    std::memory_order_acquire) != 0U
                    ? ControlState::Held
                    : ControlState::Idle;
            } else {
                return Fault(RuntimeEvaluationFault::TypeMismatch, position);
            }
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::LoadArrayLength:
            if (instruction.operand0 >= state.arrays.size()) {
                return Fault(RuntimeEvaluationFault::InvalidArray, position);
            }
            value.type = ExpressionType::Number;
            value.numberValue = static_cast<double>(
                state.arrays[instruction.operand0].Size());
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        case ExpressionOpcode::LoadArrayElement: {
            RuntimeValue indexValue{};
            if (!pop(indexValue)) {
                return Fault(RuntimeEvaluationFault::StackUnderflow, position);
            }
            if (indexValue.type != ExpressionType::Number) {
                return Fault(RuntimeEvaluationFault::TypeMismatch, position);
            }
            if (instruction.operand0 >= state.arrays.size()) {
                return Fault(RuntimeEvaluationFault::InvalidArray, position);
            }
            const RuntimeArrayStorage& array = state.arrays[instruction.operand0];
            const NormalizedArrayIndex index = NormalizeArrayIndex(
                indexValue.numberValue,
                array.Size());
            if (!index.Valid()) {
                return Fault(
                    index.status == ArrayIndexStatus::OutOfBounds
                        ? RuntimeEvaluationFault::ArrayBounds
                        : RuntimeEvaluationFault::InvalidArrayIndex,
                    position);
            }
            if (!array.Read(index.value, value)) {
                return Fault(RuntimeEvaluationFault::ArrayBounds, position);
            }
            if (value.type != instruction.type) {
                return Fault(RuntimeEvaluationFault::TypeMismatch, position);
            }
            if (!push(value)) {
                return Fault(RuntimeEvaluationFault::StackOverflow, position);
            }
            ++position;
            break;
        }
        case ExpressionOpcode::Unary: {
            RuntimeValue operand{};
            if (!pop(operand)) {
                return Fault(RuntimeEvaluationFault::StackUnderflow, position);
            }
            const RuntimeEvaluationFault fault = ApplyUnary(
                static_cast<UnaryOperator>(instruction.operand0),
                operand,
                value);
            if (fault != RuntimeEvaluationFault::None) {
                return Fault(fault, position);
            }
            if (value.type != instruction.type || !push(value)) {
                return Fault(
                    value.type != instruction.type
                        ? RuntimeEvaluationFault::TypeMismatch
                        : RuntimeEvaluationFault::StackOverflow,
                    position);
            }
            ++position;
            break;
        }
        case ExpressionOpcode::Binary: {
            RuntimeValue right{};
            RuntimeValue left{};
            if (!pop(right) || !pop(left)) {
                return Fault(RuntimeEvaluationFault::StackUnderflow, position);
            }
            const RuntimeEvaluationFault fault = ApplyBinary(
                static_cast<BinaryOperator>(instruction.operand0),
                left,
                right,
                value);
            if (fault != RuntimeEvaluationFault::None) {
                return Fault(fault, position);
            }
            if (value.type != instruction.type || !push(value)) {
                return Fault(
                    value.type != instruction.type
                        ? RuntimeEvaluationFault::TypeMismatch
                        : RuntimeEvaluationFault::StackOverflow,
                    position);
            }
            ++position;
            break;
        }
        case ExpressionOpcode::Jump:
            position = instruction.operand0;
            break;
        case ExpressionOpcode::JumpIfFalse:
        case ExpressionOpcode::JumpIfTrue: {
            RuntimeValue condition{};
            if (!pop(condition)) {
                return Fault(RuntimeEvaluationFault::StackUnderflow, position);
            }
            if (condition.type != ExpressionType::Boolean) {
                return Fault(RuntimeEvaluationFault::TypeMismatch, position);
            }
            const bool jump = instruction.opcode == ExpressionOpcode::JumpIfTrue
                ? condition.booleanValue
                : !condition.booleanValue;
            position = jump ? instruction.operand0 : position + 1U;
            break;
        }
        case ExpressionOpcode::Return:
            if (!pop(value)) {
                return Fault(RuntimeEvaluationFault::StackUnderflow, position);
            }
            if (value.type != descriptor.resultType || stackSize != 0U) {
                return Fault(RuntimeEvaluationFault::TypeMismatch, position);
            }
            return {value, RuntimeEvaluationFault::None, position};
        }
    }
    return Fault(RuntimeEvaluationFault::MissingReturn, position);
}

} // namespace inputweaver
