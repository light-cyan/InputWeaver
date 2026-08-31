#include "weavec_codec.hpp"

#include "support/little_endian.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace inputweaver {
namespace {

constexpr std::array<std::uint8_t, 8U> kWeavecMagicV4{
    0x57U,
    0x45U,
    0x41U,
    0x56U,
    0x45U,
    0x43U,
    0x00U,
    0x04U,
};

class ByteWriter final {
public:
    void U8(std::uint8_t value)
    {
        bytes_.push_back(value);
    }

    void U32(std::uint32_t value)
    {
        support::AppendLittleEndian(bytes_, value);
    }

    void U64(std::uint64_t value)
    {
        support::AppendLittleEndian(bytes_, value);
    }

    void I64(std::int64_t value)
    {
        U64(std::bit_cast<std::uint64_t>(value));
    }

    void Number(double value)
    {
        U64(std::bit_cast<std::uint64_t>(value));
    }

    void Raw(std::span<const std::uint8_t> bytes)
    {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    void String(const std::string& value)
    {
        U32(static_cast<std::uint32_t>(value.size()));
        for (const char byte : value) {
            U8(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        }
    }

    void PatchU64(std::size_t offset, std::uint64_t value)
    {
        support::StoreLittleEndian<std::uint64_t>(bytes_, offset, value);
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return bytes_.size();
    }

    [[nodiscard]] std::vector<std::uint8_t> Take() &&
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader final {
public:
    ByteReader(
        std::span<const std::uint8_t> bytes,
        std::size_t baseOffset,
        std::optional<WeavecDecodeError>& error) noexcept
        : bytes_(bytes), baseOffset_(baseOffset), error_(error)
    {
    }

    [[nodiscard]] bool U8(std::uint8_t& value)
    {
        if (Remaining() < 1U) {
            return Fail(
                WeavecDecodeErrorCode::Truncated,
                "field extends beyond the declared payload");
        }
        value = bytes_[cursor_];
        ++cursor_;
        return true;
    }

    [[nodiscard]] bool U32(std::uint32_t& value)
    {
        if (!support::ReadLittleEndian(bytes_, cursor_, value)) {
            return Fail(
                WeavecDecodeErrorCode::Truncated,
                "32-bit field extends beyond the declared payload");
        }
        return true;
    }

    [[nodiscard]] bool U64(std::uint64_t& value)
    {
        if (!support::ReadLittleEndian(bytes_, cursor_, value)) {
            return Fail(
                WeavecDecodeErrorCode::Truncated,
                "64-bit field extends beyond the declared payload");
        }
        return true;
    }

    [[nodiscard]] bool I64(std::int64_t& value)
    {
        std::uint64_t bits = 0U;
        if (!U64(bits)) {
            return false;
        }
        value = std::bit_cast<std::int64_t>(bits);
        return true;
    }

    [[nodiscard]] bool Number(double& value)
    {
        std::uint64_t bits = 0U;
        if (!U64(bits)) {
            return false;
        }
        value = std::bit_cast<double>(bits);
        return true;
    }

    [[nodiscard]] bool String(
        std::string& value,
        const WeavecDecodeLimits& limits)
    {
        std::uint32_t byteCount = 0U;
        if (!U32(byteCount)) {
            return false;
        }
        if (byteCount > limits.maximumStringBytes) {
            return Fail(
                WeavecDecodeErrorCode::LimitExceeded,
                "string byte count exceeds the configured limit");
        }
        const std::size_t size = static_cast<std::size_t>(byteCount);
        if (size > Remaining()) {
            return Fail(
                WeavecDecodeErrorCode::Truncated,
                "string extends beyond the declared payload");
        }
        const char* begin = reinterpret_cast<const char*>(bytes_.data() + cursor_);
        value.assign(begin, size);
        cursor_ += size;
        return true;
    }

    [[nodiscard]] bool Fail(
        WeavecDecodeErrorCode code,
        std::string message)
    {
        if (!error_.has_value()) {
            error_ = WeavecDecodeError{code, Offset(), std::move(message)};
        }
        return false;
    }

    [[nodiscard]] std::size_t Remaining() const noexcept
    {
        return bytes_.size() - cursor_;
    }

    [[nodiscard]] std::size_t Offset() const noexcept
    {
        return baseOffset_ + cursor_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t baseOffset_{};
    std::size_t cursor_{};
    std::optional<WeavecDecodeError>& error_;
};

template <typename Id>
void WriteId(ByteWriter& writer, Id id)
{
    writer.U32(id.value);
}

template <typename Enum>
void WriteEnum(ByteWriter& writer, Enum value)
{
    static_assert(std::is_same_v<std::underlying_type_t<Enum>, std::uint8_t>);
    writer.U8(static_cast<std::uint8_t>(value));
}

template <typename Range, typename WriteElement>
void WriteVector(ByteWriter& writer, const Range& values, WriteElement writeElement)
{
    writer.U32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writeElement(writer, value);
    }
}

void WriteRange(ByteWriter& writer, TableRange range)
{
    writer.U32(range.begin);
    writer.U32(range.count);
}

void WriteSpan(ByteWriter& writer, SourceSpan span)
{
    writer.U32(span.beginByte);
    writer.U32(span.byteLength);
}

void WriteDuration(ByteWriter& writer, DurationValue value)
{
    writer.I64(value.nanoseconds);
}

void WriteControl(ByteWriter& writer, ControlRef control)
{
    writer.U32(control.namespaceId);
    writer.U32(control.familyId);
    writer.U32(control.code);
    writer.U32(control.qualifier);
}

void WriteEventKey(ByteWriter& writer, EventKey key)
{
    WriteId(writer, key.control);
    WriteEnum(writer, key.transition);
}

void WriteSource(ByteWriter& writer, const ProgramSource& source)
{
    WriteId(writer, source.displayPath);
    writer.U32(source.byteLength);
    WriteRange(writer, source.lineStarts);
}

void WriteSettings(ByteWriter& writer, const ProgramSettings& settings)
{
    WriteEnum(writer, settings.target.kind);
    WriteId(writer, settings.target.text);
    WriteSpan(writer, settings.target.source);
    WriteDuration(writer, settings.tapDuration);
    WriteDuration(writer, settings.actionGap);
    writer.U64(settings.randomSeed);
}

void WriteRequirements(ByteWriter& writer, const ProgramRequirements& requirements)
{
    writer.U32(requirements.stateSlotCount);
    writer.U32(requirements.numberSlotCount);
    writer.U32(requirements.durationSlotCount);
    writer.U32(requirements.arrayCount);
    writer.U64(requirements.initialArrayElementBytes);
    writer.U32(requirements.mappingSlotCount);
    writer.U32(requirements.maximumExitRulesPerEvent);
    writer.U32(requirements.maximumPauseRulesPerEvent);
    writer.U32(requirements.maximumRulesPerEvent);
    writer.U32(requirements.maximumPredicateStepsPerEvent);
    writer.U32(requirements.maximumTasksPerEvent);
    writer.U32(requirements.maximumMappingOperationsPerEvent);
    writer.U32(requirements.maximumTransactionItemsPerEvent);
    writer.U32(requirements.maximumExpressionStackDepth);
    writer.U32(requirements.maximumRepeatFramesPerTask);
    writer.U32(requirements.maximumOwnedControlsPerTask);
    writer.U8(requirements.requiresProcessLaunch ? 1U : 0U);
}

template <typename Id>
[[nodiscard]] bool ReadId(ByteReader& reader, Id& id)
{
    return reader.U32(id.value);
}

template <typename Enum>
[[nodiscard]] bool ReadEnum(ByteReader& reader, Enum& value, Enum last)
{
    static_assert(std::is_same_v<std::underlying_type_t<Enum>, std::uint8_t>);
    std::uint8_t raw = 0U;
    if (!reader.U8(raw)) {
        return false;
    }
    if (raw > static_cast<std::uint8_t>(last)) {
        return reader.Fail(
            WeavecDecodeErrorCode::InvalidScalar,
            "enumeration field has an unknown numeric value");
    }
    value = static_cast<Enum>(raw);
    return true;
}

template <typename Value, typename ReadElement>
[[nodiscard]] bool ReadVector(
    ByteReader& reader,
    std::vector<Value>& values,
    const WeavecDecodeLimits& limits,
    std::size_t minimumElementBytes,
    ReadElement readElement)
{
    std::uint32_t count = 0U;
    if (!reader.U32(count)) {
        return false;
    }
    if (count == kInvalidProgramIndex
        || count > limits.maximumCollectionElements) {
        return reader.Fail(
            WeavecDecodeErrorCode::LimitExceeded,
            "collection element count exceeds the configured limit");
    }
    const std::size_t size = static_cast<std::size_t>(count);
    if (minimumElementBytes > 0U
        && size > reader.Remaining() / minimumElementBytes) {
        return reader.Fail(
            WeavecDecodeErrorCode::Truncated,
            "collection exceeds the remaining payload capacity");
    }
    values.clear();
    values.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        Value value{};
        if (!readElement(reader, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

[[nodiscard]] bool ReadRange(ByteReader& reader, TableRange& range)
{
    return reader.U32(range.begin) && reader.U32(range.count);
}

[[nodiscard]] bool ReadSpan(ByteReader& reader, SourceSpan& span)
{
    return reader.U32(span.beginByte) && reader.U32(span.byteLength);
}

[[nodiscard]] bool ReadDuration(ByteReader& reader, DurationValue& value)
{
    return reader.I64(value.nanoseconds);
}

[[nodiscard]] bool ReadControl(ByteReader& reader, ControlRef& control)
{
    return reader.U32(control.namespaceId)
        && reader.U32(control.familyId)
        && reader.U32(control.code)
        && reader.U32(control.qualifier);
}

[[nodiscard]] bool ReadEventKey(ByteReader& reader, EventKey& key)
{
    return ReadId(reader, key.control)
        && ReadEnum(reader, key.transition, EventTransition::Up);
}

[[nodiscard]] bool ReadSource(ByteReader& reader, ProgramSource& source)
{
    return ReadId(reader, source.displayPath)
        && reader.U32(source.byteLength)
        && ReadRange(reader, source.lineStarts);
}

[[nodiscard]] bool ReadSettings(ByteReader& reader, ProgramSettings& settings)
{
    return ReadEnum(
            reader,
            settings.target.kind,
            TargetSelectorKind::Executable)
        && ReadId(reader, settings.target.text)
        && ReadSpan(reader, settings.target.source)
        && ReadDuration(reader, settings.tapDuration)
        && ReadDuration(reader, settings.actionGap)
        && reader.U64(settings.randomSeed);
}

[[nodiscard]] bool ReadRequirements(
    ByteReader& reader,
    ProgramRequirements& requirements)
{
    std::uint8_t requiresProcessLaunch = 0U;
    if (!reader.U32(requirements.stateSlotCount)
        || !reader.U32(requirements.numberSlotCount)
        || !reader.U32(requirements.durationSlotCount)) {
        return false;
    }
    if (!reader.U32(requirements.arrayCount)
        || !reader.U64(requirements.initialArrayElementBytes)) {
        return false;
    }
    if (!reader.U32(requirements.mappingSlotCount)
        || !reader.U32(requirements.maximumExitRulesPerEvent)
        || !reader.U32(requirements.maximumPauseRulesPerEvent)
        || !reader.U32(requirements.maximumRulesPerEvent)
        || !reader.U32(requirements.maximumPredicateStepsPerEvent)
        || !reader.U32(requirements.maximumTasksPerEvent)
        || !reader.U32(requirements.maximumMappingOperationsPerEvent)
        || !reader.U32(requirements.maximumTransactionItemsPerEvent)
        || !reader.U32(requirements.maximumExpressionStackDepth)
        || !reader.U32(requirements.maximumRepeatFramesPerTask)
        || !reader.U32(requirements.maximumOwnedControlsPerTask)
        || !reader.U8(requiresProcessLaunch)) {
        return false;
    }
    if (requiresProcessLaunch > 1U) {
        return reader.Fail(
            WeavecDecodeErrorCode::InvalidScalar,
            "Boolean field must be encoded as zero or one");
    }
    requirements.requiresProcessLaunch = requiresProcessLaunch != 0U;
    return true;
}

void EncodePayload(ByteWriter& writer, const CompiledProgram& program)
{
    WriteSource(writer, program.Source());
    WriteSettings(writer, program.Settings());
    WriteRequirements(writer, program.Requirements());

    WriteVector(writer, program.Strings(), [](ByteWriter& output, const std::string& value) {
        output.String(value);
    });
    WriteVector(writer, program.LineStarts(), [](ByteWriter& output, std::uint32_t value) {
        output.U32(value);
    });
    WriteVector(writer, program.Controls(), [](ByteWriter& output, ControlRef value) {
        WriteControl(output, value);
    });
    WriteVector(
        writer,
        program.ControlRequirements(),
        [](ByteWriter& output, ControlRequirement value) {
            WriteId(output, value.control);
            output.U8(value.uses);
        });
    WriteVector(writer, program.ValueRefs(), [](ByteWriter& output, ValueRef value) {
        WriteEnum(output, value.domain);
        WriteEnum(output, value.type);
        output.U32(value.index);
    });

    WriteVector(
        writer,
        program.UserValues().initialStates,
        [](ByteWriter& output, std::uint8_t value) {
            output.U8(value);
        });
    WriteVector(
        writer,
        program.UserValues().initialNumbers,
        [](ByteWriter& output, double value) {
            output.Number(value);
        });
    WriteVector(
        writer,
        program.UserValues().initialDurations,
        [](ByteWriter& output, DurationValue value) {
            WriteDuration(output, value);
        });
    WriteVector(
        writer,
        program.Arrays(),
        [](ByteWriter& output, const ArrayDescriptor& value) {
            WriteEnum(output, value.elementType);
            WriteRange(output, value.initialValues);
        });
    WriteVector(
        writer,
        program.InitialArrayStates(),
        [](ByteWriter& output, std::uint8_t value) {
            output.U8(value);
        });
    WriteVector(
        writer,
        program.InitialArrayNumbers(),
        [](ByteWriter& output, double value) {
            output.Number(value);
        });

    WriteVector(writer, program.NumberConstants(), [](ByteWriter& output, double value) {
        output.Number(value);
    });
    WriteVector(
        writer,
        program.DurationConstants(),
        [](ByteWriter& output, DurationValue value) {
            WriteDuration(output, value);
        });
    WriteVector(
        writer,
        program.Expressions(),
        [](ByteWriter& output, const ExpressionDescriptor& value) {
            WriteRange(output, value.code);
            WriteEnum(output, value.resultType);
            output.U32(value.maximumStackDepth);
            WriteSpan(output, value.source);
        });
    WriteVector(
        writer,
        program.ExpressionCode(),
        [](ByteWriter& output, const ExpressionInstruction& value) {
            WriteEnum(output, value.opcode);
            WriteEnum(output, value.type);
            output.U32(value.operand0);
            output.U32(value.operand1);
        });

    WriteVector(
        writer,
        program.ActionPrograms(),
        [](ByteWriter& output, const ActionProgramDescriptor& value) {
            WriteRange(output, value.code);
            output.U32(value.repeatFrameCount);
            output.U32(value.maximumOwnedControlCount);
            WriteSpan(output, value.source);
        });
    WriteVector(
        writer,
        program.ActionCode(),
        [](ByteWriter& output, const ActionInstruction& value) {
            WriteEnum(output, value.opcode);
            output.U32(value.operand0);
            output.U32(value.operand1);
            output.U32(value.operand2);
        });

    WriteVector(
        writer,
        program.MappingSlots(),
        [](ByteWriter& output, MappingSlotDescriptor value) {
            WriteId(output, value.source);
        });
    WriteVector(
        writer,
        program.Mappings(),
        [](ByteWriter& output, const MappingDescriptor& value) {
            WriteId(output, value.slot);
            WriteId(output, value.target);
            WriteSpan(output, value.source);
        });
    WriteVector(
        writer,
        program.ExitControlBuckets(),
        [](ByteWriter& output, const ExitControlBucket& value) {
            WriteEventKey(output, value.key);
            WriteRange(output, value.rules);
        });
    WriteVector(
        writer,
        program.ExitControlRules(),
        [](ByteWriter& output, const ExitControlRule& value) {
            WriteId(output, value.condition);
            output.U32(value.sourceOrdinal);
            WriteSpan(output, value.source);
        });
    WriteVector(
        writer,
        program.PauseControlBuckets(),
        [](ByteWriter& output, const PauseControlBucket& value) {
            WriteEventKey(output, value.key);
            WriteRange(output, value.rules);
        });
    WriteVector(
        writer,
        program.PauseControlRules(),
        [](ByteWriter& output, const PauseControlRule& value) {
            WriteId(output, value.condition);
            WriteEnum(output, value.delivery);
            WriteEnum(output, value.effect);
            output.U32(value.sourceOrdinal);
            WriteSpan(output, value.source);
        });
    WriteVector(
        writer,
        program.EventBuckets(),
        [](ByteWriter& output, const EventBucket& value) {
            WriteEventKey(output, value.key);
            WriteRange(output, value.rules);
        });
    WriteVector(writer, program.Rules(), [](ByteWriter& output, const CompiledRule& value) {
        WriteId(output, value.condition);
        WriteId(output, value.action);
        WriteId(output, value.mapping);
        WriteEnum(output, value.delivery);
        WriteEnum(output, value.flow);
        WriteEnum(output, value.kind);
        output.U32(value.sourceOrdinal);
        WriteSpan(output, value.source);
    });

    WriteVector(
        writer,
        program.DebugInfo().variables,
        [](ByteWriter& output, const VariableDebugRecord& value) {
            WriteId(output, value.name);
            WriteId(output, value.value);
            WriteSpan(output, value.declaration);
        });
    WriteVector(
        writer,
        program.DebugInfo().arrays,
        [](ByteWriter& output, const ArrayDebugRecord& value) {
            WriteId(output, value.name);
            WriteId(output, value.array);
            WriteSpan(output, value.declaration);
        });
    WriteVector(
        writer,
        program.DebugInfo().expressionInstructionSpans,
        [](ByteWriter& output, SourceSpan value) {
            WriteSpan(output, value);
        });
    WriteVector(
        writer,
        program.DebugInfo().actionInstructionSpans,
        [](ByteWriter& output, SourceSpan value) {
            WriteSpan(output, value);
        });
    WriteVector(
        writer,
        program.DebugInfo().rules,
        [](ByteWriter& output, const RuleDebugRecord& value) {
            output.U32(value.sourceOrdinal);
            WriteId(output, value.conditionText);
            WriteId(output, value.actionText);
        });
}

[[nodiscard]] bool DecodePayload(
    ByteReader& reader,
    const WeavecDecodeLimits& limits,
    CompiledProgramStorage& storage)
{
    if (!ReadSource(reader, storage.source)
        || !ReadSettings(reader, storage.settings)
        || !ReadRequirements(reader, storage.requirements)) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.strings,
            limits,
            4U,
            [&limits](ByteReader& input, std::string& value) {
                return input.String(value, limits);
            })
        || !ReadVector(
            reader,
            storage.lineStarts,
            limits,
            4U,
            [](ByteReader& input, std::uint32_t& value) {
                return input.U32(value);
            })
        || !ReadVector(
            reader,
            storage.controls,
            limits,
            16U,
            [](ByteReader& input, ControlRef& value) {
                return ReadControl(input, value);
            })
        || !ReadVector(
            reader,
            storage.controlRequirements,
            limits,
            5U,
            [](ByteReader& input, ControlRequirement& value) {
                return ReadId(input, value.control) && input.U8(value.uses);
            })
        || !ReadVector(
            reader,
            storage.valueRefs,
            limits,
            6U,
            [](ByteReader& input, ValueRef& value) {
                return ReadEnum(input, value.domain, ValueDomain::BuiltinNumber)
                    && ReadEnum(input, value.type, ValueType::Duration)
                    && input.U32(value.index);
            })) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.userValues.initialStates,
            limits,
            1U,
            [](ByteReader& input, std::uint8_t& value) {
                return input.U8(value);
            })
        || !ReadVector(
            reader,
            storage.userValues.initialNumbers,
            limits,
            8U,
            [](ByteReader& input, double& value) {
                return input.Number(value);
            })
        || !ReadVector(
            reader,
            storage.userValues.initialDurations,
            limits,
            8U,
            [](ByteReader& input, DurationValue& value) {
                return ReadDuration(input, value);
            })) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.arrays,
            limits,
            9U,
            [](ByteReader& input, ArrayDescriptor& value) {
                return ReadEnum(
                           input,
                           value.elementType,
                           ArrayElementType::Number)
                    && ReadRange(input, value.initialValues);
            })
        || !ReadVector(
            reader,
            storage.initialArrayStates,
            limits,
            1U,
            [](ByteReader& input, std::uint8_t& value) {
                return input.U8(value);
            })
        || !ReadVector(
            reader,
            storage.initialArrayNumbers,
            limits,
            8U,
            [](ByteReader& input, double& value) {
                return input.Number(value);
            })) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.numberConstants,
            limits,
            8U,
            [](ByteReader& input, double& value) {
                return input.Number(value);
            })
        || !ReadVector(
            reader,
            storage.durationConstants,
            limits,
            8U,
            [](ByteReader& input, DurationValue& value) {
                return ReadDuration(input, value);
            })
        || !ReadVector(
            reader,
            storage.expressions,
            limits,
            21U,
            [](ByteReader& input, ExpressionDescriptor& value) {
                return ReadRange(input, value.code)
                    && ReadEnum(
                        input,
                        value.resultType,
                        ExpressionType::ControlState)
                    && input.U32(value.maximumStackDepth)
                    && ReadSpan(input, value.source);
            })
        || !ReadVector(
            reader,
            storage.expressionCode,
            limits,
            10U,
            [](ByteReader& input, ExpressionInstruction& value) {
                if (!ReadEnum(
                        input,
                        value.opcode,
                        ExpressionOpcode::LoadArrayElement)
                    || !ReadEnum(
                        input,
                        value.type,
                        ExpressionType::ControlState)
                    || !input.U32(value.operand0)
                    || !input.U32(value.operand1)) {
                    return false;
                }
                if (value.opcode == ExpressionOpcode::ReadControlState
                    && value.type != ExpressionType::ControlState) {
                    return input.Fail(
                        WeavecDecodeErrorCode::InvalidScalar,
                        "V4 control-state reads require the ControlState result type");
                }
                return true;
            })) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.actionPrograms,
            limits,
            24U,
            [](ByteReader& input, ActionProgramDescriptor& value) {
                return ReadRange(input, value.code)
                    && input.U32(value.repeatFrameCount)
                    && input.U32(value.maximumOwnedControlCount)
                    && ReadSpan(input, value.source);
            })
        || !ReadVector(
            reader,
            storage.actionCode,
            limits,
            13U,
            [](ByteReader& input, ActionInstruction& value) {
                if (!ReadEnum(input, value.opcode, ActionOpcode::ClearArray)
                    || !input.U32(value.operand0)
                    || !input.U32(value.operand1)) {
                    return false;
                }
                return input.U32(value.operand2);
            })) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.mappingSlots,
            limits,
            4U,
            [](ByteReader& input, MappingSlotDescriptor& value) {
                return ReadId(input, value.source);
            })
        || !ReadVector(
            reader,
            storage.mappings,
            limits,
            16U,
            [](ByteReader& input, MappingDescriptor& value) {
                return ReadId(input, value.slot)
                    && ReadId(input, value.target)
                    && ReadSpan(input, value.source);
            })
        || !ReadVector(
            reader,
            storage.exitControlBuckets,
            limits,
            13U,
            [](ByteReader& input, ExitControlBucket& value) {
                return ReadEventKey(input, value.key)
                    && ReadRange(input, value.rules);
            })
        || !ReadVector(
            reader,
            storage.exitControlRules,
            limits,
            16U,
            [](ByteReader& input, ExitControlRule& value) {
                return ReadId(input, value.condition)
                    && input.U32(value.sourceOrdinal)
                    && ReadSpan(input, value.source);
            })
        || !ReadVector(
            reader,
            storage.pauseControlBuckets,
            limits,
            13U,
            [](ByteReader& input, PauseControlBucket& value) {
                return ReadEventKey(input, value.key)
                    && ReadRange(input, value.rules);
            })
        || !ReadVector(
            reader,
            storage.pauseControlRules,
            limits,
            18U,
            [](ByteReader& input, PauseControlRule& value) {
                return ReadId(input, value.condition)
                    && ReadEnum(input, value.delivery, Delivery::Consume)
                    && ReadEnum(input, value.effect, PauseEffect::Toggle)
                    && input.U32(value.sourceOrdinal)
                    && ReadSpan(input, value.source);
            })
        || !ReadVector(
            reader,
            storage.eventBuckets,
            limits,
            13U,
            [](ByteReader& input, EventBucket& value) {
                return ReadEventKey(input, value.key)
                    && ReadRange(input, value.rules);
            })
        || !ReadVector(
            reader,
            storage.rules,
            limits,
            27U,
            [](ByteReader& input, CompiledRule& value) {
                return ReadId(input, value.condition)
                    && ReadId(input, value.action)
                    && ReadId(input, value.mapping)
                    && ReadEnum(input, value.delivery, Delivery::Consume)
                    && ReadEnum(input, value.flow, MatchFlow::Continue)
                    && ReadEnum(input, value.kind, RuleKind::MappingDown)
                    && input.U32(value.sourceOrdinal)
                    && ReadSpan(input, value.source);
            })) {
        return false;
    }

    if (!ReadVector(
            reader,
            storage.debugInfo.variables,
            limits,
            16U,
            [](ByteReader& input, VariableDebugRecord& value) {
                return ReadId(input, value.name)
                    && ReadId(input, value.value)
                    && ReadSpan(input, value.declaration);
            })) {
        return false;
    }
    if (!ReadVector(
            reader,
            storage.debugInfo.arrays,
            limits,
            16U,
            [](ByteReader& input, ArrayDebugRecord& value) {
                return ReadId(input, value.name)
                    && ReadId(input, value.array)
                    && ReadSpan(input, value.declaration);
            })) {
        return false;
    }
    if (!ReadVector(
            reader,
            storage.debugInfo.expressionInstructionSpans,
            limits,
            8U,
            [](ByteReader& input, SourceSpan& value) {
                return ReadSpan(input, value);
            })
        || !ReadVector(
            reader,
            storage.debugInfo.actionInstructionSpans,
            limits,
            8U,
            [](ByteReader& input, SourceSpan& value) {
                return ReadSpan(input, value);
            })) {
        return false;
    }
    return ReadVector(
        reader,
        storage.debugInfo.rules,
        limits,
        12U,
        [](ByteReader& input, RuleDebugRecord& value) {
            return input.U32(value.sourceOrdinal)
                && ReadId(input, value.conditionText)
                && ReadId(input, value.actionText);
        });
}

[[nodiscard]] std::uint64_t ReadHeaderPayloadLength(
    std::span<const std::uint8_t> bytes) noexcept
{
    std::size_t offset = 8U;
    std::uint64_t value{};
    (void)support::ReadLittleEndian(bytes, offset, value);
    return value;
}

} // namespace

std::vector<std::uint8_t> EncodeWeavec(const CompiledProgram& program)
{
    ByteWriter writer;
    writer.Raw(kWeavecMagicV4);
    writer.U64(0U);
    EncodePayload(writer, program);
    const std::uint64_t payloadSize = static_cast<std::uint64_t>(
        writer.Size() - kWeavecHeaderSize);
    writer.PatchU64(8U, payloadSize);
    return std::move(writer).Take();
}

DecodeWeavecResult DecodeWeavec(
    std::span<const std::uint8_t> bytes,
    const WeavecDecodeLimits& limits)
{
    DecodeWeavecResult result{};
    if (bytes.size() < kWeavecHeaderSize) {
        result.decodeError = WeavecDecodeError{
            WeavecDecodeErrorCode::InvalidHeader,
            bytes.size(),
            "artifact is shorter than the 16-byte header"};
        return result;
    }
    if (!std::equal(
            kWeavecMagicV4.begin(),
            kWeavecMagicV4.end(),
            bytes.begin())) {
        std::size_t mismatch = 0U;
        while (mismatch < kWeavecMagicV4.size()
            && bytes[mismatch] == kWeavecMagicV4[mismatch]) {
            ++mismatch;
        }
        result.decodeError = WeavecDecodeError{
            WeavecDecodeErrorCode::InvalidHeader,
            mismatch,
            "artifact magic or format version differs from the current WEAVEC format"};
        return result;
    }

    const std::uint64_t payloadSize = ReadHeaderPayloadLength(bytes);
    if (payloadSize > limits.maximumPayloadBytes) {
        result.decodeError = WeavecDecodeError{
            WeavecDecodeErrorCode::LimitExceeded,
            8U,
            "declared payload exceeds the configured byte limit"};
        return result;
    }
    const std::size_t availablePayload = bytes.size() - kWeavecHeaderSize;
    if (payloadSize != static_cast<std::uint64_t>(availablePayload)) {
        result.decodeError = WeavecDecodeError{
            WeavecDecodeErrorCode::LengthMismatch,
            8U,
            "declared payload length differs from the artifact size"};
        return result;
    }

    try {
        CompiledProgramStorage storage{};
        ByteReader reader(
            bytes.subspan(kWeavecHeaderSize),
            kWeavecHeaderSize,
            result.decodeError);
        if (!DecodePayload(reader, limits, storage)) {
            return result;
        }
        if (reader.Remaining() != 0U) {
            static_cast<void>(reader.Fail(
                WeavecDecodeErrorCode::TrailingData,
                "payload contains trailing fields"));
            return result;
        }

        FinalizeResult finalized = FinalizeCompiledProgram(std::move(storage));
        result.program = std::move(finalized.program);
        result.validationErrors = std::move(finalized.errors);
        return result;
    } catch (const std::bad_alloc&) {
        result.decodeError = WeavecDecodeError{
            WeavecDecodeErrorCode::AllocationFailure,
            0U,
            "artifact storage allocation failed"};
    } catch (const std::length_error&) {
        result.decodeError = WeavecDecodeError{
            WeavecDecodeErrorCode::AllocationFailure,
            0U,
            "artifact requested an impossible allocation"};
    }
    return result;
}

} // namespace inputweaver
