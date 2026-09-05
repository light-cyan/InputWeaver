#include "program_validator_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace inputweaver::program_validation {

class ExpressionStacks final {
public:
    using State = std::uint32_t;
    static constexpr State Empty = 0U;
    static constexpr State Unreachable = (std::numeric_limits<State>::max)();

    explicit ExpressionStacks(std::size_t maximumSize)
        : nodes_(1U)
    {
        nodes_.reserve(maximumSize);
    }

    [[nodiscard]] State Push(State stack, ExpressionType type)
    {
        for (State child = nodes_[stack].firstChild; child != Empty;
             child = nodes_[child].nextSibling) {
            if (nodes_[child].type == type) {
                return child;
            }
        }
        const State child = static_cast<State>(nodes_.size());
        nodes_.push_back({stack, Empty, nodes_[stack].firstChild,
            nodes_[stack].depth + 1U, type});
        nodes_[stack].firstChild = child;
        return child;
    }

    [[nodiscard]] bool Pop(State& stack, ExpressionType expected) const noexcept
    {
        if (stack == Empty || nodes_[stack].type != expected) {
            return false;
        }
        stack = nodes_[stack].parent;
        return true;
    }

    [[nodiscard]] ExpressionType Pop(State& stack) const noexcept
    {
        const Node& node = nodes_[stack];
        stack = node.parent;
        return node.type;
    }

    [[nodiscard]] std::uint32_t Depth(State stack) const noexcept
    {
        return nodes_[stack].depth;
    }

private:
    struct Node final {
        State parent{};
        State firstChild{};
        State nextSibling{};
        std::uint32_t depth{};
        ExpressionType type{ExpressionType::None};
    };
    std::vector<Node> nodes_;
};

void ValidateExpressionOperator(
    const ExpressionInstruction& instruction,
    ExpressionStacks& stacks,
    ExpressionStacks::State& stack,
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
        if (!stacks.Pop(stack, input) || instruction.type != output) {
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "unary operator stack or result type mismatch");
            return;
        }
        stack = stacks.Push(stack, output);
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
        if (stacks.Depth(stack) < 2U) {
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "equality operator stack underflow");
            return;
        }
        right = stacks.Pop(stack);
        left = stacks.Pop(stack);
        if (left != right
            || (left != ExpressionType::State
                && left != ExpressionType::Number
                && left != ExpressionType::Duration
                && left != ExpressionType::ControlState)) {
            context.Add(
                ProgramValidationErrorCode::Expression,
                location,
                "equality operands must have the same comparable type");
            return;
        }
    } else if (!stacks.Pop(stack, right) || !stacks.Pop(stack, left)) {
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
    stack = stacks.Push(stack, output);
}

void MergeExpressionStack(
    std::vector<ExpressionStacks::State>& states,
    std::uint32_t target,
    ExpressionStacks::State stack,
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
    if (state == ExpressionStacks::Unreachable) {
        state = stack;
    } else if (state != stack) {
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
    ExpressionStacks stacks(count);
    std::vector<ExpressionStacks::State> states(
        count, ExpressionStacks::Unreachable);
    states[0] = ExpressionStacks::Empty;
    std::size_t maximumDepth = 0;
    bool sawReturn = false;
    for (std::size_t local = 0; local < count; ++local) {
        if (states[local] == ExpressionStacks::Unreachable) {
            continue;
        }
        ExpressionStacks::State stack = states[local];
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
            stack = stacks.Push(stack, ExpressionType::Boolean);
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
            stack = stacks.Push(stack, ExpressionType::State);
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
            stack = stacks.Push(stack, ExpressionType::Number);
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
            stack = stacks.Push(stack, ExpressionType::Duration);
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
                stack = stacks.Push(stack, expected);
            }
            break;
        case ExpressionOpcode::ReadControlState:
            if (instruction.type != ExpressionType::ControlState
                || instruction.operand0 >= storage.controls.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "ReadControlState operands or type are invalid");
            }
            stack = stacks.Push(stack, ExpressionType::ControlState);
            break;
        case ExpressionOpcode::PushControlState:
            if (instruction.type != ExpressionType::ControlState
                || instruction.operand0 > static_cast<std::uint32_t>(ControlState::Held)
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "PushControlState operands or type are invalid");
            }
            stack = stacks.Push(stack, ExpressionType::ControlState);
            break;
        case ExpressionOpcode::LoadField: {
            const auto reference = EventFieldReference::Decode(instruction.operand0, instruction.operand1);
            ExpressionType expected = ExpressionType::None;
            if (!reference.source.IsValid()) {
                expected = EventFieldType(reference);
            } else if (ValidId(reference.source, storage.eventSources.size())) {
                const auto& source = storage.eventSources[reference.source.value];
                if (ValidId(source.period, storage.expressions.size())) {
                    expected = EventFieldType(reference, source.transition,
                        storage.expressions[source.period.value].resultType);
                }
            }
            if (instruction.operand1 != reference.Selector()
                || expected == ExpressionType::None || instruction.type != expected) {
                context.Add(ProgramValidationErrorCode::Expression, location,
                    "field reference or result type is invalid");
            }
            stack = stacks.Push(stack, expected);
            break;
        }
        case ExpressionOpcode::LoadArrayLength:
            if (instruction.type != ExpressionType::Number
                || instruction.operand0 >= storage.arrays.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "LoadArrayLength operands or type are invalid");
            }
            stack = stacks.Push(stack, ExpressionType::Number);
            break;
        case ExpressionOpcode::LoadArrayElement:
            if (instruction.operand0 >= storage.arrays.size()
                || instruction.operand1 != 0U
                || !stacks.Pop(stack, ExpressionType::Number)) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "LoadArrayElement array, index, or unused operand is invalid");
            } else {
                const ExpressionType expected = ToExpressionType(
                    storage.arrays[instruction.operand0].elementType);
                if (instruction.type != expected) {
                    context.Add(
                        ProgramValidationErrorCode::Expression,
                        location,
                        "LoadArrayElement result type differs from the array element type");
                }
                stack = stacks.Push(stack, expected);
            }
            break;
        case ExpressionOpcode::Unary:
        case ExpressionOpcode::Binary:
            if (instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Expression,
                    location,
                    "operator instruction has a nonzero unused operand");
            }
            ValidateExpressionOperator(instruction, stacks, stack, context, location);
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
                || !stacks.Pop(stack, ExpressionType::Boolean)) {
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
                || stacks.Depth(stack) != 1U
                || !stacks.Pop(stack, descriptor.resultType)) {
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

        maximumDepth = (std::max)(maximumDepth,
            static_cast<std::size_t>(stacks.Depth(stack)));
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
    if (std::any_of(states.begin(), states.end(), [](const auto state) {
            return state == ExpressionStacks::Unreachable;
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

        if (instruction.opcode != ActionOpcode::SetArrayElement
            && instruction.opcode != ActionOpcode::Pointer
            && instruction.operand2 != 0U) {
            context.Add(
                ProgramValidationErrorCode::Action,
                location,
                "action has a nonzero unused third operand");
        }

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
        case ActionOpcode::Pointer:
            if (instruction.operand0 > static_cast<std::uint32_t>(PointerOperation::ScrollHorizontal)
                || ExpressionResultType(storage, instruction.operand1) != ExpressionType::Number
                || (instruction.operand0 <= static_cast<std::uint32_t>(PointerOperation::MoveTo)
                    ? ExpressionResultType(storage, instruction.operand2) != ExpressionType::Number
                    : instruction.operand2 != 0U)) {
                context.Add(ProgramValidationErrorCode::Action, location,
                    "pointer output requires numeric arguments");
            }
            break;
        case ActionOpcode::RestartEvent:
            if (instruction.operand0 >= storage.eventSources.size() || instruction.operand1 != 0U) {
                context.Add(ProgramValidationErrorCode::Action, location,
                    "restart requires a declared event source");
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
        case ActionOpcode::SetArrayElement:
            if (instruction.operand0 >= storage.arrays.size()
                || ExpressionResultType(storage, instruction.operand1)
                    != ExpressionType::Number) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array element set requires an array and a Number index expression");
            } else if (ExpressionResultType(storage, instruction.operand2)
                       != ToExpressionType(
                           storage.arrays[instruction.operand0].elementType)) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array element set value type differs from the array element type");
            }
            break;
        case ActionOpcode::ToggleArrayElement:
            if (instruction.operand0 >= storage.arrays.size()
                || ExpressionResultType(storage, instruction.operand1)
                    != ExpressionType::Number) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array element toggle requires an array and a Number index expression");
            } else if (storage.arrays[instruction.operand0].elementType
                       != ArrayElementType::State) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array element toggle requires a State array");
            }
            break;
        case ActionOpcode::AppendArrayElement:
            if (instruction.operand0 >= storage.arrays.size()) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array append references an unknown array");
            } else if (ExpressionResultType(storage, instruction.operand1)
                       != ToExpressionType(
                           storage.arrays[instruction.operand0].elementType)) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array append value type differs from the array element type");
            }
            break;
        case ActionOpcode::PopArrayElement:
            if (instruction.operand0 >= storage.arrays.size()
                || instruction.operand1 >= storage.valueRefs.size()) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array pop references an unknown array or scalar target");
            } else {
                const ValueRef& target = storage.valueRefs[instruction.operand1];
                const ArrayElementType elementType =
                    storage.arrays[instruction.operand0].elementType;
                const bool matchingType = (elementType == ArrayElementType::State
                        && target.type == ValueType::State)
                    || (elementType == ArrayElementType::Number
                        && target.type == ValueType::Number);
                if (!IsWritableValue(target) || !matchingType) {
                    context.Add(
                        ProgramValidationErrorCode::Action,
                        location,
                        "array pop requires a writable scalar target of the element type");
                }
            }
            break;
        case ActionOpcode::ClearArray:
            if (instruction.operand0 >= storage.arrays.size()
                || instruction.operand1 != 0U) {
                context.Add(
                    ProgramValidationErrorCode::Action,
                    location,
                    "array clear reference or unused operand is invalid");
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

} // namespace inputweaver::program_validation
