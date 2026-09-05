#include "program_validator.hpp"
#include "program_validator_internal.hpp"

#include "program_requirements.hpp"

#include "support/utf8.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver {
using namespace program_validation;
namespace {

constexpr std::uint8_t kAllControlUseBits =
    ToControlUseBits(ControlUse::EventSource)
    | ToControlUseBits(ControlUse::PhysicalState)
    | ToControlUseBits(ControlUse::OutputDownUp)
    | ToControlUseBits(ControlUse::OutputAgain);

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

#include "program_meter_validator.inc"

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
        || storage.settings.actionGap.nanoseconds < 0
        || storage.settings.mouseIdleTimeout.nanoseconds <= 0) {
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

void ValidateArraysAndDebug(
    const CompiledProgramStorage& storage,
    ValidationContext& context)
{
    std::vector<bool> coveredStates(storage.initialArrayStates.size(), false);
    std::vector<bool> coveredNumbers(storage.initialArrayNumbers.size(), false);
    for (std::size_t index = 0; index < storage.arrays.size(); ++index) {
        const ArrayDescriptor& array = storage.arrays[index];
        const std::string location = At("arrays", index);
        std::vector<bool>* covered = nullptr;
        switch (array.elementType) {
        case ArrayElementType::State:
            covered = &coveredStates;
            break;
        case ArrayElementType::Number:
            covered = &coveredNumbers;
            break;
        default:
            context.Add(
                ProgramValidationErrorCode::Value,
                location + ".elementType",
                "array element type is unknown");
            continue;
        }
        if (!ValidRange(array.initialValues, covered->size())) {
            context.Add(
                ProgramValidationErrorCode::Range,
                location + ".initialValues",
                "array initial-value range is outside its typed pool");
            continue;
        }
        const std::uint64_t end = static_cast<std::uint64_t>(
            array.initialValues.begin) + array.initialValues.count;
        for (std::uint64_t raw = array.initialValues.begin; raw < end; ++raw) {
            const std::size_t position = static_cast<std::size_t>(raw);
            if ((*covered)[position]) {
                context.Add(
                    ProgramValidationErrorCode::Range,
                    location + ".initialValues",
                    "array initial-value ranges overlap");
                break;
            }
            (*covered)[position] = true;
        }
    }
    if (std::any_of(coveredStates.begin(), coveredStates.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Range,
            "initialArrayStates",
            "array descriptors do not cover the complete State initial-value pool");
    }
    if (std::any_of(coveredNumbers.begin(), coveredNumbers.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Range,
            "initialArrayNumbers",
            "array descriptors do not cover the complete Number initial-value pool");
    }

    for (std::size_t index = 0; index < storage.initialArrayStates.size(); ++index) {
        if (storage.initialArrayStates[index] > 1U) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("initialArrayStates", index),
                "array State initial value must be zero or one");
        }
    }
    for (std::size_t index = 0; index < storage.initialArrayNumbers.size(); ++index) {
        const double value = storage.initialArrayNumbers[index];
        if (!std::isfinite(value) || (value == 0.0 && std::signbit(value))) {
            context.Add(
                ProgramValidationErrorCode::Value,
                At("initialArrayNumbers", index),
                "array Number initial value is non-finite or negative zero");
        }
    }

    if (storage.debugInfo.arrays.size() != storage.arrays.size()) {
        context.Add(
            ProgramValidationErrorCode::DebugInfo,
            "debugInfo.arrays",
            "array debug records must cover every array exactly once");
    }
    std::set<std::uint32_t> coveredArrays;
    std::uint32_t previousDeclaration = 0U;
    bool havePreviousDeclaration = false;
    for (std::size_t index = 0; index < storage.debugInfo.arrays.size(); ++index) {
        const ArrayDebugRecord& array = storage.debugInfo.arrays[index];
        const std::string location = At("debugInfo.arrays", index);
        if (!ValidId(array.name, storage.strings.size())
            || !ValidId(array.array, storage.arrays.size())) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                location,
                "array name or ArrayId is invalid");
        } else if (!coveredArrays.insert(array.array.value).second) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                location,
                "multiple debug records reference the same array");
        }
        if (!ValidSpan(array.declaration, storage.source.byteLength)) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                location + ".declaration",
                "array declaration span is outside the source file");
        }
        if (havePreviousDeclaration
            && previousDeclaration >= array.declaration.beginByte) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                location + ".declaration",
                "array debug records are not in declaration order");
        }
        previousDeclaration = array.declaration.beginByte;
        havePreviousDeclaration = true;
    }
}

struct RuleBucketValidationText final {
    std::string_view buckets;
    std::string_view rules;
    std::string_view invalidKey;
    std::string_view invalidRange;
    std::string_view overlappingRanges;
    std::string_view invalidSource;
    std::string_view invalidCondition;
    std::string_view incompleteCoverage;
};

template <typename Buckets, typename Rules, typename ValidateRule>
void ValidateRuleBuckets(
    const CompiledProgramStorage& storage,
    ValidationContext& context,
    std::vector<std::uint8_t>& expectedControlUses,
    std::set<std::uint32_t>& sourceOrdinals,
    const Buckets& buckets,
    const Rules& rules,
    const RuleBucketValidationText& text,
    ValidateRule&& validateRule)
{
    std::vector<bool> coveredRules(rules.size(), false);
    EventKey previousKey{};
    bool havePreviousKey = false;
    for (std::size_t bucketIndex = 0; bucketIndex < buckets.size(); ++bucketIndex) {
        const auto& bucket = buckets[bucketIndex];
        const std::string bucketLocation = At(text.buckets, bucketIndex);
        const bool controlEvent = ValidEventTransition(bucket.key.transition);
        const bool validKey = controlEvent
            ? ValidId(bucket.key.control, storage.controls.size()) && !bucket.key.source.IsValid()
            : !bucket.key.control.IsValid() && (IsMouseTransition(bucket.key.transition)
                ? !bucket.key.source.IsValid()
                : bucket.key.transition == EventTransition::Tick
                    && ValidId(bucket.key.source, storage.meters.size()));
        if (!validKey
            || (havePreviousKey && previousKey >= bucket.key)) {
            context.Add(
                ProgramValidationErrorCode::Rule,
                bucketLocation,
                std::string(text.invalidKey));
        }
        previousKey = bucket.key;
        havePreviousKey = true;
        if (controlEvent) AddExpectedControlUse(
            storage,
            expectedControlUses,
            bucket.key.control,
            ControlUse::EventSource,
            context,
            bucketLocation);

        if (!ValidRange(bucket.rules, rules.size())) {
            context.Add(
                ProgramValidationErrorCode::Range,
                bucketLocation + ".rules",
                std::string(text.invalidRange));
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
                    std::string(text.overlappingRanges));
            }
            coveredRules[ruleIndex] = true;
            const auto& rule = rules[ruleIndex];
            const std::string ruleLocation = At(text.rules, ruleIndex);
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
                    std::string(text.invalidSource));
            }
            if (rule.condition.IsValid()
                && (!ValidId(rule.condition, storage.expressions.size())
                    || storage.expressions[rule.condition.value].resultType
                        != ExpressionType::Boolean)) {
                context.Add(
                    ProgramValidationErrorCode::Rule,
                    ruleLocation + ".condition",
                    std::string(text.invalidCondition));
            }
            validateRule(bucket, rule, ruleLocation);
        }
    }
    if (std::any_of(coveredRules.begin(), coveredRules.end(), [](bool value) {
            return !value;
        })) {
        context.Add(
            ProgramValidationErrorCode::Range,
            std::string(text.buckets),
            std::string(text.incompleteCoverage));
    }
}

void ValidateExitControls(
    const CompiledProgramStorage& storage,
    ValidationContext& context,
    std::vector<std::uint8_t>& expectedControlUses,
    std::set<std::uint32_t>& sourceOrdinals)
{
    if (storage.exitControlBuckets.empty() || storage.exitControlRules.empty()) {
        context.Add(
            ProgramValidationErrorCode::Rule,
            "exitControlBuckets",
            "a compiled program must contain at least one exit-control rule");
    }

    ValidateRuleBuckets(
        storage,
        context,
        expectedControlUses,
        sourceOrdinals,
        storage.exitControlBuckets,
        storage.exitControlRules,
        {
            "exitControlBuckets",
            "exitControlRules",
            "exit-control buckets must have strictly sorted valid keys",
            "exit-control rule range is outside its table",
            "exit-control bucket rule ranges overlap",
            "exit-control source span is outside the source file",
            "exit-control condition must be invalid or Boolean",
            "exit-control bucket ranges do not cover the complete rule table",
        },
        [&context](const auto& bucket, const auto&, const std::string& location) {
            if (!ValidEventTransition(bucket.key.transition)) {
                context.Add(ProgramValidationErrorCode::Rule, location, "exit requires a control event");
            }
        });
}

void ValidatePauseControls(
    const CompiledProgramStorage& storage,
    ValidationContext& context,
    std::vector<std::uint8_t>& expectedControlUses,
    std::set<std::uint32_t>& sourceOrdinals)
{
    ValidateRuleBuckets(
        storage,
        context,
        expectedControlUses,
        sourceOrdinals,
        storage.pauseControlBuckets,
        storage.pauseControlRules,
        {
            "pauseControlBuckets",
            "pauseControlRules",
            "pause-control buckets must have strictly sorted valid keys",
            "pause-control rule range is outside its table",
            "pause-control bucket rule ranges overlap",
            "pause-control source span is outside the source file",
            "pause-control condition must be invalid or Boolean",
            "pause-control bucket ranges do not cover the complete rule table",
        },
        [&context](
            const PauseControlBucket& bucket,
            const PauseControlRule& rule,
            const std::string& ruleLocation) {
            if (!ValidEventTransition(bucket.key.transition)) {
                context.Add(ProgramValidationErrorCode::Rule, ruleLocation, "pause requires a control event");
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
        });
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
                    ControlUse::OutputAgain,
            context,
            At("mappings", index));
    }

    ValidateRuleBuckets(
        storage,
        context,
        expectedControlUses,
        sourceOrdinals,
        storage.eventBuckets,
        storage.rules,
        {
            "eventBuckets",
            "rules",
            "event buckets must have strictly sorted valid keys",
            "rule range is outside the rule table",
            "event bucket rule ranges overlap",
            "rule source span is outside the source file",
            "rule condition must be invalid or a Boolean expression",
            "event bucket ranges do not cover the complete rule table",
        },
        [&storage, &context, &mappingRuleCounts](
            const EventBucket& bucket,
            const CompiledRule& rule,
            const std::string& ruleLocation) {
            if (bucket.key.transition == EventTransition::Tick && rule.delivery != Delivery::Observe) {
                context.Add(ProgramValidationErrorCode::Rule, ruleLocation, "tick rules observe their source");
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
        });
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
    std::set<std::uint32_t> debugOrdinals;
    for (std::size_t index = 0;
         index < storage.debugInfo.rules.size();
         ++index) {
        const RuleDebugRecord& rule = storage.debugInfo.rules[index];
        if (!debugOrdinals.insert(rule.sourceOrdinal).second) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.rules", index),
                "rule debug source ordinal is duplicated");
        }
        if ((rule.conditionText.IsValid()
                && !ValidId(rule.conditionText, storage.strings.size()))
            || (rule.actionText.IsValid()
                && !ValidId(rule.actionText, storage.strings.size()))) {
            context.Add(
                ProgramValidationErrorCode::DebugInfo,
                At("debugInfo.rules", index),
                "rule debug text is invalid");
        }
    }
}

} // namespace
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
        {"arrays", storage.arrays.size()},
        {"initialArrayStates", storage.initialArrayStates.size()},
        {"initialArrayNumbers", storage.initialArrayNumbers.size()},
        {"numberConstants", storage.numberConstants.size()},
        {"durationConstants", storage.durationConstants.size()},
        {"expressions", storage.expressions.size()},
        {"expressionCode", storage.expressionCode.size()},
        {"meters", storage.meters.size()},
        {"actionPrograms", storage.actionPrograms.size()},
        {"actionCode", storage.actionCode.size()},
        {"mappingSlots", storage.mappingSlots.size()},
        {"mappings", storage.mappings.size()},
        {"exitControlBuckets", storage.exitControlBuckets.size()},
        {"exitControlRules", storage.exitControlRules.size()},
        {"pauseControlBuckets", storage.pauseControlBuckets.size()},
        {"pauseControlRules", storage.pauseControlRules.size()},
        {"eventBuckets", storage.eventBuckets.size()},
        {"rules", storage.rules.size()},
        {"debugInfo.variables", storage.debugInfo.variables.size()},
        {"debugInfo.arrays", storage.debugInfo.arrays.size()},
        {"debugInfo.rules", storage.debugInfo.rules.size()},
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
    ValidateArraysAndDebug(storage, context);
    ValidateMeters(storage, context);

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
        if (instruction.opcode == ExpressionOpcode::ReadControlState
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
    ValidateExitControls(
        storage,
        context,
        expectedControlUses,
        sourceOrdinals);
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
