#include "program_dump.hpp"

#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

namespace inputweaver {
namespace {

template <typename Id>
void WriteId(std::ostream& output, char prefix, Id id)
{
    if (id.IsValid()) {
        output << prefix << id.value;
    } else {
        output << "invalid";
    }
}

void WriteRange(std::ostream& output, TableRange range)
{
    output << range.begin << '+' << range.count;
}

void WriteSpan(std::ostream& output, SourceSpan span)
{
    output << span.beginByte << '+' << span.byteLength;
}

[[nodiscard]] std::string Escape(std::string_view value)
{
    std::ostringstream output;
    output << '"';
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        switch (character) {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U || character == 0x7fU) {
                output << "\\x" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<unsigned int>(character)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

void WriteControl(std::ostream& output, ControlRef control)
{
    output << control.namespaceId << ':' << control.familyId << ':'
           << control.code << ':' << control.qualifier;
}

[[nodiscard]] std::string_view TransitionName(EventTransition value) noexcept
{
    switch (value) {
    case EventTransition::Down:
        return "down";
    case EventTransition::Repeat:
        return "repeat";
    case EventTransition::Up:
        return "up";
    }
    return "unknown";
}

[[nodiscard]] std::string_view TargetName(TargetSelectorKind value) noexcept
{
    switch (value) {
    case TargetSelectorKind::Unspecified:
        return "unspecified";
    case TargetSelectorKind::Global:
        return "global";
    case TargetSelectorKind::Executable:
        return "executable";
    }
    return "unknown";
}

[[nodiscard]] std::string_view ValueTypeName(ValueType value) noexcept
{
    switch (value) {
    case ValueType::State:
        return "state";
    case ValueType::Number:
        return "number";
    case ValueType::Duration:
        return "duration";
    }
    return "unknown";
}

[[nodiscard]] std::string_view ExpressionTypeName(ExpressionType value) noexcept
{
    switch (value) {
    case ExpressionType::None:
        return "none";
    case ExpressionType::Boolean:
        return "boolean";
    case ExpressionType::State:
        return "state";
    case ExpressionType::Number:
        return "number";
    case ExpressionType::Duration:
        return "duration";
    }
    return "unknown";
}

[[nodiscard]] std::string_view ValueDomainName(ValueDomain value) noexcept
{
    switch (value) {
    case ValueDomain::UserState:
        return "user-state";
    case ValueDomain::UserNumber:
        return "user-number";
    case ValueDomain::UserDuration:
        return "user-duration";
    case ValueDomain::BuiltinState:
        return "builtin-state";
    case ValueDomain::BuiltinDuration:
        return "builtin-duration";
    }
    return "unknown";
}

[[nodiscard]] std::string_view ExpressionOpcodeName(ExpressionOpcode value) noexcept
{
    switch (value) {
    case ExpressionOpcode::PushBoolean:
        return "push-boolean";
    case ExpressionOpcode::PushState:
        return "push-state";
    case ExpressionOpcode::PushNumber:
        return "push-number";
    case ExpressionOpcode::PushDuration:
        return "push-duration";
    case ExpressionOpcode::LoadValue:
        return "load-value";
    case ExpressionOpcode::ReadControlHeld:
        return "read-control-held";
    case ExpressionOpcode::Unary:
        return "unary";
    case ExpressionOpcode::Binary:
        return "binary";
    case ExpressionOpcode::Jump:
        return "jump";
    case ExpressionOpcode::JumpIfFalse:
        return "jump-if-false";
    case ExpressionOpcode::JumpIfTrue:
        return "jump-if-true";
    case ExpressionOpcode::Return:
        return "return";
    }
    return "unknown";
}

[[nodiscard]] std::string_view ActionOpcodeName(ActionOpcode value) noexcept
{
    switch (value) {
    case ActionOpcode::Press:
        return "press";
    case ActionOpcode::Release:
        return "release";
    case ActionOpcode::Tap:
        return "tap";
    case ActionOpcode::Wait:
        return "wait";
    case ActionOpcode::Gap:
        return "gap";
    case ActionOpcode::Set:
        return "set";
    case ActionOpcode::Toggle:
        return "toggle";
    case ActionOpcode::Exec:
        return "exec";
    case ActionOpcode::Jump:
        return "jump";
    case ActionOpcode::JumpIfFalse:
        return "jump-if-false";
    case ActionOpcode::RepeatInit:
        return "repeat-init";
    case ActionOpcode::RepeatCheck:
        return "repeat-check";
    case ActionOpcode::RepeatNext:
        return "repeat-next";
    case ActionOpcode::Yield:
        return "yield";
    case ActionOpcode::End:
        return "end";
    }
    return "unknown";
}

[[nodiscard]] std::string_view DeliveryName(Delivery value) noexcept
{
    return value == Delivery::Observe ? "observe" : "consume";
}

[[nodiscard]] std::string_view FlowName(MatchFlow value) noexcept
{
    return value == MatchFlow::Stop ? "stop" : "continue";
}

[[nodiscard]] std::string_view RuleKindName(RuleKind value) noexcept
{
    return value == RuleKind::Event ? "event" : "mapping-down";
}

[[nodiscard]] std::string_view PauseEffectName(PauseEffect value) noexcept
{
    switch (value) {
    case PauseEffect::On:
        return "on";
    case PauseEffect::Off:
        return "off";
    case PauseEffect::Toggle:
        return "toggle";
    }
    return "unknown";
}

} // namespace

std::string DumpCompiledProgram(const CompiledProgram& program)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17);

    output << "source display=";
    WriteId(output, 's', program.Source().displayPath);
    output << " bytes=" << program.Source().byteLength << " lines=";
    WriteRange(output, program.Source().lineStarts);
    output << '\n';
    output << "settings target=" << TargetName(program.Settings().target.kind)
           << " text=";
    WriteId(output, 's', program.Settings().target.text);
    output << " source=";
    WriteSpan(output, program.Settings().target.source);
    output << " tap-ns=" << program.Settings().tapDuration.nanoseconds
           << " gap-ns=" << program.Settings().actionGap.nanoseconds << '\n';

    const ProgramRequirements& requirements = program.Requirements();
    output << "requirements states=" << requirements.stateSlotCount
           << " numbers=" << requirements.numberSlotCount
           << " durations=" << requirements.durationSlotCount
           << " mapping-slots=" << requirements.mappingSlotCount
           << " exit-rules/event="
           << requirements.maximumExitRulesPerEvent
           << " pause-rules/event="
           << requirements.maximumPauseRulesPerEvent
           << " rules/event=" << requirements.maximumRulesPerEvent
           << " predicate-steps/event="
           << requirements.maximumPredicateStepsPerEvent
           << " tasks/event=" << requirements.maximumTasksPerEvent
           << " mapping-ops/event="
           << requirements.maximumMappingOperationsPerEvent
           << " transaction-items/event="
           << requirements.maximumTransactionItemsPerEvent
           << " expression-stack="
           << requirements.maximumExpressionStackDepth
           << " repeat-frames/task="
           << requirements.maximumRepeatFramesPerTask
           << " owned-controls/task="
           << requirements.maximumOwnedControlsPerTask
           << " process-launch="
           << (requirements.requiresProcessLaunch ? "true" : "false") << '\n';

    output << "strings " << program.Strings().size() << '\n';
    for (std::size_t index = 0; index < program.Strings().size(); ++index) {
        output << "  s" << index << ' ' << Escape(program.Strings()[index]) << '\n';
    }
    output << "line-starts " << program.LineStarts().size();
    for (const std::uint32_t lineStart : program.LineStarts()) {
        output << ' ' << lineStart;
    }
    output << '\n';

    output << "controls " << program.Controls().size() << '\n';
    for (std::size_t index = 0; index < program.Controls().size(); ++index) {
        output << "  c" << index << ' ';
        WriteControl(output, program.Controls()[index]);
        output << '\n';
    }
    output << "control-requirements " << program.ControlRequirements().size() << '\n';
    for (const ControlRequirement requirement : program.ControlRequirements()) {
        output << "  control=";
        WriteId(output, 'c', requirement.control);
        output << " uses=" << static_cast<unsigned int>(requirement.uses) << '\n';
    }

    output << "user-states " << program.UserValues().initialStates.size();
    for (const std::uint8_t value : program.UserValues().initialStates) {
        output << ' ' << static_cast<unsigned int>(value);
    }
    output << '\n';
    output << "user-numbers " << program.UserValues().initialNumbers.size();
    for (const double value : program.UserValues().initialNumbers) {
        output << ' ' << value;
    }
    output << '\n';
    output << "user-durations " << program.UserValues().initialDurations.size();
    for (const DurationValue value : program.UserValues().initialDurations) {
        output << ' ' << value.nanoseconds;
    }
    output << '\n';
    output << "value-refs " << program.ValueRefs().size() << '\n';
    for (std::size_t index = 0; index < program.ValueRefs().size(); ++index) {
        const ValueRef& value = program.ValueRefs()[index];
        output << "  v" << index << " domain=" << ValueDomainName(value.domain)
               << " type=" << ValueTypeName(value.type)
               << " index=" << value.index << '\n';
    }

    output << "number-constants " << program.NumberConstants().size();
    for (const double value : program.NumberConstants()) {
        output << ' ' << value;
    }
    output << '\n';
    output << "duration-constants " << program.DurationConstants().size();
    for (const DurationValue value : program.DurationConstants()) {
        output << ' ' << value.nanoseconds;
    }
    output << '\n';

    output << "expressions " << program.Expressions().size() << '\n';
    for (std::size_t index = 0; index < program.Expressions().size(); ++index) {
        const ExpressionDescriptor& expression = program.Expressions()[index];
        output << "  e" << index << " code=";
        WriteRange(output, expression.code);
        output << " result=" << ExpressionTypeName(expression.resultType)
               << " stack=" << expression.maximumStackDepth << " source=";
        WriteSpan(output, expression.source);
        output << '\n';
    }
    output << "expression-code " << program.ExpressionCode().size() << '\n';
    for (std::size_t index = 0; index < program.ExpressionCode().size(); ++index) {
        const ExpressionInstruction& instruction = program.ExpressionCode()[index];
        output << "  x" << index << ' '
               << ExpressionOpcodeName(instruction.opcode)
               << " type=" << ExpressionTypeName(instruction.type)
               << " operand0=" << instruction.operand0
               << " operand1=" << instruction.operand1 << " source=";
        WriteSpan(output, program.DebugInfo().expressionInstructionSpans[index]);
        output << '\n';
    }

    output << "actions " << program.ActionPrograms().size() << '\n';
    for (std::size_t index = 0; index < program.ActionPrograms().size(); ++index) {
        const ActionProgramDescriptor& action = program.ActionPrograms()[index];
        output << "  a" << index << " code=";
        WriteRange(output, action.code);
        output << " frames=" << action.repeatFrameCount
               << " owned=" << action.maximumOwnedControlCount << " source=";
        WriteSpan(output, action.source);
        output << '\n';
    }
    output << "action-code " << program.ActionCode().size() << '\n';
    for (std::size_t index = 0; index < program.ActionCode().size(); ++index) {
        const ActionInstruction& instruction = program.ActionCode()[index];
        output << "  i" << index << ' ' << ActionOpcodeName(instruction.opcode)
               << " operand0=" << instruction.operand0
               << " operand1=" << instruction.operand1 << " source=";
        WriteSpan(output, program.DebugInfo().actionInstructionSpans[index]);
        output << '\n';
    }

    output << "mapping-slots " << program.MappingSlots().size() << '\n';
    for (std::size_t index = 0; index < program.MappingSlots().size(); ++index) {
        output << "  ms" << index << " source=";
        WriteId(output, 'c', program.MappingSlots()[index].source);
        output << '\n';
    }
    output << "mappings " << program.Mappings().size() << '\n';
    for (std::size_t index = 0; index < program.Mappings().size(); ++index) {
        const MappingDescriptor& mapping = program.Mappings()[index];
        output << "  m" << index << " slot=";
        WriteId(output, 'q', mapping.slot);
        output << " target=";
        WriteId(output, 'c', mapping.target);
        output << " source=";
        WriteSpan(output, mapping.source);
        output << '\n';
    }

    output << "exit-control-buckets "
           << program.ExitControlBuckets().size() << '\n';
    for (std::size_t index = 0;
         index < program.ExitControlBuckets().size();
         ++index) {
        const ExitControlBucket& bucket = program.ExitControlBuckets()[index];
        output << "  e" << index << " key=";
        WriteId(output, 'c', bucket.key.control);
        output << ':' << TransitionName(bucket.key.transition) << " rules=";
        WriteRange(output, bucket.rules);
        output << '\n';
    }
    output << "exit-control-rules " << program.ExitControlRules().size() << '\n';
    for (std::size_t index = 0;
         index < program.ExitControlRules().size();
         ++index) {
        const ExitControlRule& rule = program.ExitControlRules()[index];
        output << "  e" << index << " condition=";
        WriteId(output, 'e', rule.condition);
        output << " ordinal=" << rule.sourceOrdinal << " source=";
        WriteSpan(output, rule.source);
        output << '\n';
    }

    output << "pause-control-buckets "
           << program.PauseControlBuckets().size() << '\n';
    for (std::size_t index = 0;
         index < program.PauseControlBuckets().size();
         ++index) {
        const PauseControlBucket& bucket = program.PauseControlBuckets()[index];
        output << "  p" << index << " key=";
        WriteId(output, 'c', bucket.key.control);
        output << ':' << TransitionName(bucket.key.transition) << " rules=";
        WriteRange(output, bucket.rules);
        output << '\n';
    }
    output << "pause-control-rules " << program.PauseControlRules().size() << '\n';
    for (std::size_t index = 0;
         index < program.PauseControlRules().size();
         ++index) {
        const PauseControlRule& rule = program.PauseControlRules()[index];
        output << "  p" << index << " condition=";
        WriteId(output, 'e', rule.condition);
        output << " delivery=" << DeliveryName(rule.delivery)
               << " effect=" << PauseEffectName(rule.effect)
               << " ordinal=" << rule.sourceOrdinal << " source=";
        WriteSpan(output, rule.source);
        output << '\n';
    }

    output << "event-buckets " << program.EventBuckets().size() << '\n';
    for (std::size_t index = 0; index < program.EventBuckets().size(); ++index) {
        const EventBucket& bucket = program.EventBuckets()[index];
        output << "  b" << index << " key=";
        WriteId(output, 'c', bucket.key.control);
        output << ':' << TransitionName(bucket.key.transition) << " rules=";
        WriteRange(output, bucket.rules);
        output << '\n';
    }
    output << "rules " << program.Rules().size() << '\n';
    for (std::size_t index = 0; index < program.Rules().size(); ++index) {
        const CompiledRule& rule = program.Rules()[index];
        output << "  r" << index << " condition=";
        WriteId(output, 'e', rule.condition);
        output << " action=";
        WriteId(output, 'a', rule.action);
        output << " mapping=";
        WriteId(output, 'm', rule.mapping);
        output << " delivery=" << DeliveryName(rule.delivery)
               << " flow=" << FlowName(rule.flow)
               << " kind=" << RuleKindName(rule.kind)
               << " ordinal=" << rule.sourceOrdinal << " source=";
        WriteSpan(output, rule.source);
        output << '\n';
    }

    output << "variables " << program.DebugInfo().variables.size() << '\n';
    for (std::size_t index = 0;
         index < program.DebugInfo().variables.size();
         ++index) {
        const VariableDebugRecord& variable = program.DebugInfo().variables[index];
        output << "  d" << index << " name=";
        WriteId(output, 's', variable.name);
        output << " value=";
        WriteId(output, 'v', variable.value);
        output << " source=";
        WriteSpan(output, variable.declaration);
        output << '\n';
    }
    return output.str();
}

} // namespace inputweaver
