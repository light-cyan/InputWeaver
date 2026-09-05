void ValidateEventSources(const CompiledProgramStorage& storage, ValidationContext& context)
{
    std::set<StringId> names;
    for (std::size_t index = 0; index < storage.eventSources.size(); ++index) {
        const auto& source = storage.eventSources[index];
        const auto location = At("eventSources", index);
        if (InvalidRequiredString(storage, source.name) || !names.insert(source.name).second
            || !ValidSpan(source.declaration, storage.source.byteLength)
            || !IsMouseTransition(source.transition)
            || !ValidId(source.period, storage.expressions.size())) {
            context.Add(ProgramValidationErrorCode::Value, location, "invalid event source descriptor");
            continue;
        }
        const auto& period = storage.expressions[source.period.value];
        if (period.resultType != ExpressionType::Number
            && !(source.transition == EventTransition::Move && period.resultType == ExpressionType::Duration)) {
            context.Add(ProgramValidationErrorCode::Expression, location, "invalid event period type");
        }
        if (!ValidRange(period.code, storage.expressionCode.size())) continue;
        for (std::uint32_t local = 0; local < period.code.count; ++local) {
            const auto opcode = storage.expressionCode[period.code.begin + local].opcode;
            if (opcode == ExpressionOpcode::LoadField || opcode == ExpressionOpcode::ReadControlState) {
                context.Add(ProgramValidationErrorCode::Expression, location,
                    "period expressions use program values, not input observations");
            }
        }
    }
}
