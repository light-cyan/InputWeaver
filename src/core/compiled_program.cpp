#include "compiled_program.hpp"

#include "program_validator.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <tuple>

namespace inputweaver {
namespace {

template <typename Id>
void RemapId(Id& id, const std::vector<std::uint32_t>& remap) noexcept
{
    if (id.value < remap.size()) {
        id.value = remap[id.value];
    }
}

template <typename Value, typename Less, typename Equal>
std::vector<std::uint32_t> CanonicalizePool(
    std::vector<Value>& values,
    Less less,
    Equal equal)
{
    std::vector<std::uint32_t> order(values.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&values, &less](std::uint32_t left, std::uint32_t right) {
            return less(values[left], values[right]);
        });

    std::vector<Value> canonical;
    canonical.reserve(values.size());
    std::vector<std::uint32_t> remap(values.size());
    for (const std::uint32_t oldIndex : order) {
        if (canonical.empty() || !equal(canonical.back(), values[oldIndex])) {
            canonical.push_back(std::move(values[oldIndex]));
        }
        remap[oldIndex] = static_cast<std::uint32_t>(canonical.size() - 1U);
    }
    values = std::move(canonical);
    return remap;
}

void CanonicalizeStrings(CompiledProgramStorage& storage)
{
    const std::vector<std::uint32_t> remap = CanonicalizePool(
        storage.strings,
        std::less<std::string>{},
        std::equal_to<std::string>{});

    RemapId(storage.source.displayPath, remap);
    RemapId(storage.settings.target.text, remap);
    for (auto& variable : storage.debugInfo.variables) {
        RemapId(variable.name, remap);
    }
    for (auto& instruction : storage.actionCode) {
        if (instruction.opcode == ActionOpcode::Exec
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }
}

void CanonicalizeControls(CompiledProgramStorage& storage)
{
    const std::vector<std::uint32_t> remap = CanonicalizePool(
        storage.controls,
        std::less<ControlRef>{},
        std::equal_to<ControlRef>{});

    for (auto& requirement : storage.controlRequirements) {
        RemapId(requirement.control, remap);
    }
    for (auto& instruction : storage.expressionCode) {
        if (instruction.opcode == ExpressionOpcode::ReadControlHeld
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }
    for (auto& instruction : storage.actionCode) {
        if ((instruction.opcode == ActionOpcode::Press
             || instruction.opcode == ActionOpcode::Release
             || instruction.opcode == ActionOpcode::Tap)
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }

    std::stable_sort(
        storage.controlRequirements.begin(),
        storage.controlRequirements.end(),
        [](const ControlRequirement& left, const ControlRequirement& right) {
            return left.control.value < right.control.value;
        });
    std::vector<ControlRequirement> merged;
    merged.reserve(storage.controlRequirements.size());
    for (const ControlRequirement requirement : storage.controlRequirements) {
        if (!merged.empty() && merged.back().control == requirement.control) {
            merged.back().uses = static_cast<std::uint8_t>(
                merged.back().uses | requirement.uses);
        } else {
            merged.push_back(requirement);
        }
    }
    storage.controlRequirements = std::move(merged);
}

void CanonicalizeValueRefs(CompiledProgramStorage& storage)
{
    const std::vector<std::uint32_t> remap = CanonicalizePool(
        storage.valueRefs,
        std::less<ValueRef>{},
        std::equal_to<ValueRef>{});

    for (auto& variable : storage.debugInfo.variables) {
        RemapId(variable.value, remap);
    }
    for (auto& instruction : storage.expressionCode) {
        if (instruction.opcode == ExpressionOpcode::LoadValue
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }
    for (auto& instruction : storage.actionCode) {
        if ((instruction.opcode == ActionOpcode::Set
             || instruction.opcode == ActionOpcode::Toggle)
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }
}

void CanonicalizeNumberConstants(CompiledProgramStorage& storage)
{
    for (double& value : storage.numberConstants) {
        if (value == 0.0) {
            value = 0.0;
        }
    }
    const auto bits = [](double value) {
        return std::bit_cast<std::uint64_t>(value);
    };
    const std::vector<std::uint32_t> remap = CanonicalizePool(
        storage.numberConstants,
        [&bits](double left, double right) { return bits(left) < bits(right); },
        [&bits](double left, double right) { return bits(left) == bits(right); });
    for (auto& instruction : storage.expressionCode) {
        if (instruction.opcode == ExpressionOpcode::PushNumber
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }
}

void CanonicalizeDurationConstants(CompiledProgramStorage& storage)
{
    const std::vector<std::uint32_t> remap = CanonicalizePool(
        storage.durationConstants,
        std::less<DurationValue>{},
        std::equal_to<DurationValue>{});
    for (auto& instruction : storage.expressionCode) {
        if (instruction.opcode == ExpressionOpcode::PushDuration
            && instruction.operand0 < remap.size()) {
            instruction.operand0 = remap[instruction.operand0];
        }
    }
}

void CanonicalizeMappingSlots(CompiledProgramStorage& storage)
{
    std::vector<std::uint32_t> order(storage.mappingSlots.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&storage](std::uint32_t left, std::uint32_t right) {
            return storage.mappingSlots[left].source
                < storage.mappingSlots[right].source;
        });

    std::vector<MappingSlotDescriptor> sorted;
    sorted.reserve(storage.mappingSlots.size());
    std::vector<std::uint32_t> remap(storage.mappingSlots.size());
    for (const std::uint32_t oldIndex : order) {
        remap[oldIndex] = static_cast<std::uint32_t>(sorted.size());
        sorted.push_back(storage.mappingSlots[oldIndex]);
    }
    storage.mappingSlots = std::move(sorted);
    for (auto& mapping : storage.mappings) {
        RemapId(mapping.slot, remap);
    }
}

[[nodiscard]] bool BucketRangesCanBeRebuilt(
    const CompiledProgramStorage& storage) noexcept
{
    std::vector<bool> covered(storage.rules.size(), false);
    for (const EventBucket& bucket : storage.eventBuckets) {
        const std::uint64_t end = static_cast<std::uint64_t>(bucket.rules.begin)
            + bucket.rules.count;
        if (end > storage.rules.size()) {
            return false;
        }
        for (std::uint64_t index = bucket.rules.begin; index < end; ++index) {
            const std::size_t position = static_cast<std::size_t>(index);
            if (covered[position]) {
                return false;
            }
            covered[position] = true;
        }
    }
    return std::all_of(covered.begin(), covered.end(), [](bool value) {
        return value;
    });
}

void CanonicalizeEventBuckets(CompiledProgramStorage& storage)
{
    const bool rebuildRules = BucketRangesCanBeRebuilt(storage);
    std::stable_sort(
        storage.eventBuckets.begin(),
        storage.eventBuckets.end(),
        [](const EventBucket& left, const EventBucket& right) {
            return left.key < right.key;
        });
    if (!rebuildRules) {
        return;
    }

    std::vector<CompiledRule> sortedRules;
    sortedRules.reserve(storage.rules.size());
    for (EventBucket& bucket : storage.eventBuckets) {
        const auto begin = storage.rules.begin()
            + static_cast<std::ptrdiff_t>(bucket.rules.begin);
        const auto end = begin + static_cast<std::ptrdiff_t>(bucket.rules.count);
        std::vector<CompiledRule> bucketRules(begin, end);
        std::stable_sort(
            bucketRules.begin(),
            bucketRules.end(),
            [](const CompiledRule& left, const CompiledRule& right) {
                return left.sourceOrdinal < right.sourceOrdinal;
            });
        bucket.rules.begin = static_cast<std::uint32_t>(sortedRules.size());
        sortedRules.insert(
            sortedRules.end(),
            bucketRules.begin(),
            bucketRules.end());
    }
    storage.rules = std::move(sortedRules);
}

void CanonicalizeCompiledProgram(CompiledProgramStorage& storage)
{
    CanonicalizeStrings(storage);
    CanonicalizeControls(storage);
    CanonicalizeValueRefs(storage);
    CanonicalizeNumberConstants(storage);
    CanonicalizeDurationConstants(storage);
    CanonicalizeMappingSlots(storage);
    CanonicalizeEventBuckets(storage);
}

} // namespace

struct CompiledProgramFinalizerAccess final {
    [[nodiscard]] static std::shared_ptr<const CompiledProgram> Make(
        CompiledProgramStorage storage)
    {
        return std::shared_ptr<const CompiledProgram>(
            new CompiledProgram(std::move(storage)));
    }
};

CompiledProgram::CompiledProgram(CompiledProgramStorage storage) noexcept
    : storage_(std::move(storage))
{
}

std::uint32_t CompiledProgram::SchemaVersion() const noexcept
{
    return storage_.schemaVersion;
}

const ProgramSource& CompiledProgram::Source() const noexcept
{
    return storage_.source;
}

const ProgramSettings& CompiledProgram::Settings() const noexcept
{
    return storage_.settings;
}

const ProgramRequirements& CompiledProgram::Requirements() const noexcept
{
    return storage_.requirements;
}

std::span<const std::string> CompiledProgram::Strings() const noexcept
{
    return storage_.strings;
}

std::span<const std::uint32_t> CompiledProgram::LineStarts() const noexcept
{
    return storage_.lineStarts;
}

std::span<const ControlRef> CompiledProgram::Controls() const noexcept
{
    return storage_.controls;
}

std::span<const ControlRequirement> CompiledProgram::ControlRequirements() const noexcept
{
    return storage_.controlRequirements;
}

std::span<const ValueRef> CompiledProgram::ValueRefs() const noexcept
{
    return storage_.valueRefs;
}

const UserValueLayout& CompiledProgram::UserValues() const noexcept
{
    return storage_.userValues;
}

std::span<const double> CompiledProgram::NumberConstants() const noexcept
{
    return storage_.numberConstants;
}

std::span<const DurationValue> CompiledProgram::DurationConstants() const noexcept
{
    return storage_.durationConstants;
}

std::span<const ExpressionDescriptor> CompiledProgram::Expressions() const noexcept
{
    return storage_.expressions;
}

std::span<const ExpressionInstruction> CompiledProgram::ExpressionCode() const noexcept
{
    return storage_.expressionCode;
}

std::span<const ActionProgramDescriptor> CompiledProgram::ActionPrograms() const noexcept
{
    return storage_.actionPrograms;
}

std::span<const ActionInstruction> CompiledProgram::ActionCode() const noexcept
{
    return storage_.actionCode;
}

std::span<const MappingSlotDescriptor> CompiledProgram::MappingSlots() const noexcept
{
    return storage_.mappingSlots;
}

std::span<const MappingDescriptor> CompiledProgram::Mappings() const noexcept
{
    return storage_.mappings;
}

std::span<const EventBucket> CompiledProgram::EventBuckets() const noexcept
{
    return storage_.eventBuckets;
}

std::span<const CompiledRule> CompiledProgram::Rules() const noexcept
{
    return storage_.rules;
}

const ProgramDebugInfo& CompiledProgram::DebugInfo() const noexcept
{
    return storage_.debugInfo;
}

FinalizeResult FinalizeCompiledProgram(CompiledProgramStorage storage)
{
    CanonicalizeCompiledProgram(storage);
    FinalizeResult result{};
    result.errors = ValidateCompiledProgram(storage);
    if (result.errors.empty()) {
        result.program = CompiledProgramFinalizerAccess::Make(std::move(storage));
    }
    return result;
}

CompiledProgramStorage& CompiledProgramBuilder::Storage() noexcept
{
    return storage_;
}

const CompiledProgramStorage& CompiledProgramBuilder::Storage() const noexcept
{
    return storage_;
}

void CompiledProgramBuilder::DeriveRequirements() noexcept
{
    storage_.requirements = ComputeProgramRequirements(storage_);
}

CompiledProgramStorage CompiledProgramBuilder::TakeStorage() && noexcept
{
    return std::move(storage_);
}

FinalizeResult CompiledProgramBuilder::Finalize() &&
{
    return FinalizeCompiledProgram(std::move(storage_));
}

} // namespace inputweaver
