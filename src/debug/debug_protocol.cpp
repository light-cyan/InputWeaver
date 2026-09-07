#include "debug_protocol.hpp"

#include "support/little_endian.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace inputweaver::debug {
namespace {

class ByteWriter final {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& bytes) noexcept
        : bytes_(bytes)
    {
    }

    void U8(std::uint8_t value)
    {
        bytes_.push_back(value);
    }

    void U16(std::uint16_t value)
    {
        support::AppendLittleEndian(bytes_, value);
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
        U64(static_cast<std::uint64_t>(value));
    }

    void Number(double value)
    {
        U64(std::bit_cast<std::uint64_t>(value));
    }

    void Boolean(bool value)
    {
        U8(value ? 1U : 0U);
    }

    void String(std::string_view value)
    {
        U32(static_cast<std::uint32_t>(value.size()));
        for (const char byte : value) {
            U8(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        }
    }

private:
    std::vector<std::uint8_t>& bytes_;
};

class ByteReader final {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] bool U8(std::uint8_t& value) noexcept
    {
        if (position_ >= bytes_.size()) {
            return false;
        }
        value = bytes_[position_++];
        return true;
    }

    [[nodiscard]] bool U16(std::uint16_t& value) noexcept
    {
        return support::ReadLittleEndian(bytes_, position_, value);
    }

    [[nodiscard]] bool U32(std::uint32_t& value) noexcept
    {
        return support::ReadLittleEndian(bytes_, position_, value);
    }

    [[nodiscard]] bool U64(std::uint64_t& value) noexcept
    {
        return support::ReadLittleEndian(bytes_, position_, value);
    }

    [[nodiscard]] bool I64(std::int64_t& value) noexcept
    {
        std::uint64_t encoded{};
        if (!U64(encoded)) {
            return false;
        }
        value = static_cast<std::int64_t>(encoded);
        return true;
    }

    [[nodiscard]] bool Number(double& value) noexcept
    {
        std::uint64_t encoded{};
        if (!U64(encoded)) {
            return false;
        }
        value = std::bit_cast<double>(encoded);
        return true;
    }

    [[nodiscard]] bool Boolean(bool& value) noexcept
    {
        std::uint8_t encoded{};
        if (!U8(encoded) || encoded > 1U) {
            return false;
        }
        value = encoded != 0U;
        return true;
    }

    [[nodiscard]] bool String(std::string& value)
    {
        std::uint32_t size{};
        if (!U32(size)
            || size > kMaximumDebugTextBytes
            || size > Remaining()) {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + position_),
            size);
        position_ += size;
        return true;
    }

    [[nodiscard]] std::size_t Remaining() const noexcept
    {
        return bytes_.size() - position_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
};

template <typename Enum>
void WriteEnum(ByteWriter& writer, Enum value)
{
    using Raw = std::underlying_type_t<Enum>;
    if constexpr (sizeof(Raw) == 1U) {
        writer.U8(static_cast<std::uint8_t>(value));
    } else {
        writer.U16(static_cast<std::uint16_t>(value));
    }
}

template <typename Enum>
[[nodiscard]] bool ReadEnum(
    ByteReader& reader,
    Enum& value,
    std::uint16_t maximum) noexcept
{
    std::uint16_t encoded{};
    if constexpr (sizeof(std::underlying_type_t<Enum>) == 1U) {
        std::uint8_t byte{};
        if (!reader.U8(byte)) {
            return false;
        }
        encoded = byte;
    } else if (!reader.U16(encoded)) {
        return false;
    }
    if (encoded > maximum) {
        return false;
    }
    value = static_cast<Enum>(encoded);
    return true;
}

void WriteSource(ByteWriter& writer, SourceSpan source)
{
    writer.U32(source.beginByte);
    writer.U32(source.byteLength);
}

[[nodiscard]] bool ReadSource(ByteReader& reader, SourceSpan& source) noexcept
{
    return reader.U32(source.beginByte)
        && reader.U32(source.byteLength);
}

void WriteControl(ByteWriter& writer, const ControlRef& control)
{
    writer.U32(control.namespaceId);
    writer.U32(control.familyId);
    writer.U32(control.code);
    writer.U32(control.qualifier);
}

[[nodiscard]] bool ReadControl(ByteReader& reader, ControlRef& control) noexcept
{
    return reader.U32(control.namespaceId)
        && reader.U32(control.familyId)
        && reader.U32(control.code)
        && reader.U32(control.qualifier);
}

[[nodiscard]] bool WriteValue(ByteWriter& writer, const DebugValue& value)
{
    WriteEnum(writer, value.type);
    switch (value.type) {
    case ValueType::State:
        writer.Boolean(value.stateValue);
        return true;
    case ValueType::Number:
        if (!std::isfinite(value.numberValue)) {
            return false;
        }
        writer.Number(value.numberValue);
        return true;
    case ValueType::Duration:
        if (value.durationValue.nanoseconds < 0) {
            return false;
        }
        writer.I64(value.durationValue.nanoseconds);
        return true;
    }
    return false;
}

[[nodiscard]] bool ReadValue(ByteReader& reader, DebugValue& value)
{
    if (!ReadEnum(reader, value.type, static_cast<std::uint16_t>(ValueType::Duration))) {
        return false;
    }
    switch (value.type) {
    case ValueType::State:
        return reader.Boolean(value.stateValue);
    case ValueType::Number:
        return reader.Number(value.numberValue)
            && std::isfinite(value.numberValue);
    case ValueType::Duration:
        return reader.I64(value.durationValue.nanoseconds)
            && value.durationValue.nanoseconds >= 0;
    }
    return false;
}

[[nodiscard]] bool ValidArrayValue(const DebugArrayValue& value) noexcept
{
    const std::size_t prefix = value.prefixCount;
    const std::size_t suffix = value.suffixCount;
    if (prefix + suffix > kRuntimeDebugArrayPreviewCount) {
        return false;
    }
    if (value.length <= kRuntimeDebugArrayPreviewCount) {
        if (prefix != value.length || suffix != 0U) {
            return false;
        }
    } else if (prefix != kRuntimeDebugArrayPreviewCount / 2U
        || suffix != kRuntimeDebugArrayPreviewCount / 2U) {
        return false;
    }
    if (value.elementType == ArrayElementType::Number) {
        for (std::size_t index = 0U; index < prefix + suffix; ++index) {
            if (std::isfinite(value.elements[index].numberValue) == 0) {
                return false;
            }
        }
    }
    return value.elementType == ArrayElementType::State
        || value.elementType == ArrayElementType::Number;
}

[[nodiscard]] bool WriteArrayValue(
    ByteWriter& writer,
    const DebugArrayValue& value)
{
    if (!ValidArrayValue(value)) {
        return false;
    }
    WriteEnum(writer, value.elementType);
    writer.U64(value.length);
    writer.U8(value.prefixCount);
    writer.U8(value.suffixCount);
    const std::size_t count = static_cast<std::size_t>(value.prefixCount)
        + static_cast<std::size_t>(value.suffixCount);
    for (std::size_t index = 0U; index < count; ++index) {
        if (value.elementType == ArrayElementType::State) {
            writer.Boolean(value.elements[index].stateValue);
        } else {
            writer.Number(value.elements[index].numberValue);
        }
    }
    return true;
}

[[nodiscard]] bool ReadArrayValue(
    ByteReader& reader,
    DebugArrayValue& value) noexcept
{
    if (!ReadEnum(
            reader,
            value.elementType,
            static_cast<std::uint16_t>(ArrayElementType::Number))
        || !reader.U64(value.length)
        || !reader.U8(value.prefixCount)
        || !reader.U8(value.suffixCount)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(value.prefixCount)
        + static_cast<std::size_t>(value.suffixCount);
    if (count > kRuntimeDebugArrayPreviewCount) {
        return false;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        if (value.elementType == ArrayElementType::State) {
            if (!reader.Boolean(value.elements[index].stateValue)) {
                return false;
            }
        } else if (!reader.Number(value.elements[index].numberValue)) {
            return false;
        }
    }
    return ValidArrayValue(value);
}

#include "debug_mouse_codec.inc"

[[nodiscard]] bool ValidSettings(const DebugProgramSettings& settings) noexcept
{
    return settings.tapDuration.nanoseconds >= 0
        && settings.actionGap.nanoseconds >= 0
        && settings.mouseIdleTimeout.nanoseconds >= 0;
}

[[nodiscard]] bool WriteSettings(ByteWriter& writer, const DebugProgramSettings& settings)
{
    if (!ValidSettings(settings)) {
        return false;
    }
    writer.I64(settings.tapDuration.nanoseconds);
    writer.I64(settings.actionGap.nanoseconds);
    writer.I64(settings.mouseIdleTimeout.nanoseconds);
    writer.U64(settings.randomSeed);
    return true;
}

[[nodiscard]] bool ReadSettings(ByteReader& reader, DebugProgramSettings& settings)
{
    return reader.I64(settings.tapDuration.nanoseconds)
        && reader.I64(settings.actionGap.nanoseconds)
        && reader.I64(settings.mouseIdleTimeout.nanoseconds)
        && reader.U64(settings.randomSeed)
        && ValidSettings(settings);
}

[[nodiscard]] bool EncodePayload(
    const Message& message,
    std::vector<std::uint8_t>& payload)
{
    ByteWriter writer(payload);
    switch (message.header.kind) {
    case MessageKind::Hello:
        writer.U16(message.hello.minimumVersion);
        writer.U16(message.hello.maximumVersion);
        return true;
    case MessageKind::HelloAccepted:
        writer.U16(message.helloAccepted.selectedVersion);
        writer.U32(message.helloAccepted.processId);
        return true;
    case MessageKind::StartCapture:
    case MessageKind::StopCapture:
    case MessageKind::RequestExecutorStop:
    case MessageKind::StreamCompletedAck:
    case MessageKind::StreamCompleted:
        return true;
    case MessageKind::CaptureStarted:
        if (message.captureStarted.values.size() > kMaximumDebugValues
            || message.captureStarted.arrays.size() > kMaximumDebugArrays) {
            return false;
        }
        writer.I64(message.captureStarted.captureUnixTimeMilliseconds);
        writer.U32(static_cast<std::uint32_t>(
            message.captureStarted.values.size()));
        for (const DebugNamedValue& value : message.captureStarted.values) {
            if (value.name.size() > kMaximumDebugTextBytes) {
                return false;
            }
            writer.String(value.name);
            if (!WriteValue(writer, value.value)) {
                return false;
            }
        }
        writer.U32(static_cast<std::uint32_t>(
            message.captureStarted.arrays.size()));
        for (const DebugNamedArray& array : message.captureStarted.arrays) {
            if (array.name.size() > kMaximumDebugTextBytes) {
                return false;
            }
            writer.String(array.name);
            if (!WriteArrayValue(writer, array.value)) {
                return false;
            }
        }
        return WriteMouseCapture(writer, message.captureStarted)
            && WriteSettings(writer, message.captureStarted.settings);
    case MessageKind::InputEvent: {
        const InputEventPayload& input = message.inputEvent;
        writer.U64(input.inputSequence);
        WriteEnum(writer, input.device);
        WriteEnum(writer, input.transition);
        WriteEnum(writer, input.origin);
        WriteEnum(writer, input.disposition);
        writer.Boolean(input.hasCompiledControl);
        WriteControl(writer, input.compiledIdentity);
        writer.U32(input.virtualKey);
        writer.U32(input.scanCode);
        writer.U32(input.nativeQualifier);
        writer.U32(input.mouseData);
        WritePoint(writer, input.position);
        WriteMouseDelta(writer, input.delta);
        return true;
    }
    case MessageKind::RuleMatched: {
        const RuleMatchedPayload& matched = message.ruleMatched;
        if (matched.conditionText.size() > kMaximumDebugTextBytes
            || matched.actionText.size() > kMaximumDebugTextBytes) {
            return false;
        }
        writer.U64(matched.executionMarker);
        writer.U64(matched.triggerInputSequence);
        writer.String(matched.conditionText);
        writer.String(matched.actionText);
        writer.U32(matched.triggerMeter.value);
        writer.U64(matched.triggerCycleSequence);
        writer.U32(matched.selectionCount);
        return true;
    }
    case MessageKind::ExecutionEnded:
        writer.U64(message.executionEnded.executionMarker);
        WriteEnum(writer, message.executionEnded.result);
        return true;
    case MessageKind::RuntimeIssue:
        WriteEnum(writer, message.runtimeIssue.code);
        WriteEnum(writer, message.runtimeIssue.issue.kind);
        WriteSource(writer, message.runtimeIssue.issue.source);
        writer.U32(message.runtimeIssue.issue.subject);
        writer.U32(message.runtimeIssue.issue.position);
        writer.I64(message.runtimeIssue.issue.deadlineNanoseconds);
        writer.U32(message.runtimeIssue.issue.detail);
        writer.U32(message.runtimeIssue.issue.platformError);
        writer.U64(message.runtimeIssue.droppedRecords);
        return true;
    case MessageKind::StateChanged:
        writer.U32(message.stateChanged.valueIndex);
        return WriteValue(writer, message.stateChanged.value);
    case MessageKind::ArrayChanged:
        writer.U32(message.arrayChanged.arrayIndex);
        return WriteArrayValue(writer, message.arrayChanged.value);
    case MessageKind::MouseState:
        WriteMouseSnapshot(writer, message.mouseState);
        return true;
    case MessageKind::MouseCycleCompleted:
        writer.U64(message.mouseCycleCompleted.triggerInputSequence);
        WriteOccurrence(writer, message.mouseCycleCompleted.occurrence);
        return true;
    case MessageKind::ExecutionMeterSelected:
        writer.U64(message.executionMeterSelected.executionMarker);
        WriteOccurrence(writer, message.executionMeterSelected.occurrence);
        return true;
    }
    return false;
}

[[nodiscard]] bool DecodePayload(ByteReader& reader, Message& message)
{
    switch (message.header.kind) {
    case MessageKind::Hello:
        return reader.U16(message.hello.minimumVersion)
            && reader.U16(message.hello.maximumVersion);
    case MessageKind::HelloAccepted:
        return reader.U16(message.helloAccepted.selectedVersion)
            && reader.U32(message.helloAccepted.processId);
    case MessageKind::StartCapture:
    case MessageKind::StopCapture:
    case MessageKind::RequestExecutorStop:
    case MessageKind::StreamCompletedAck:
    case MessageKind::StreamCompleted:
        return true;
    case MessageKind::CaptureStarted: {
        if (!reader.I64(message.captureStarted.captureUnixTimeMilliseconds)) {
            return false;
        }
        std::uint32_t valueCount{};
        if (!reader.U32(valueCount) || valueCount > kMaximumDebugValues) {
            return false;
        }
        message.captureStarted.values.resize(valueCount);
        for (DebugNamedValue& value : message.captureStarted.values) {
            if (!reader.String(value.name) || !ReadValue(reader, value.value)) {
                return false;
            }
        }
        std::uint32_t arrayCount{};
        if (!reader.U32(arrayCount) || arrayCount > kMaximumDebugArrays) {
            return false;
        }
        message.captureStarted.arrays.resize(arrayCount);
        for (DebugNamedArray& array : message.captureStarted.arrays) {
            if (!reader.String(array.name)
                || !ReadArrayValue(reader, array.value)) {
                return false;
            }
        }
        return ReadMouseCapture(reader, message.captureStarted)
            && ReadSettings(reader, message.captureStarted.settings);
    }
    case MessageKind::InputEvent: {
        InputEventPayload& input = message.inputEvent;
        return reader.U64(input.inputSequence)
            && ReadEnum(reader, input.device, 1U)
            && ReadEnum(reader, input.transition, 4U)
            && ReadEnum(reader, input.origin, 3U)
            && ReadEnum(reader, input.disposition, 2U)
            && reader.Boolean(input.hasCompiledControl)
            && ReadControl(reader, input.compiledIdentity)
            && reader.U32(input.virtualKey)
            && reader.U32(input.scanCode)
            && reader.U32(input.nativeQualifier)
            && reader.U32(input.mouseData)
            && ReadPoint(reader, input.position) && ReadMouseDelta(reader, input.delta);
    }
    case MessageKind::RuleMatched: {
        RuleMatchedPayload& matched = message.ruleMatched;
        return reader.U64(matched.executionMarker)
            && reader.U64(matched.triggerInputSequence)
            && reader.String(matched.conditionText)
            && reader.String(matched.actionText)
            && reader.U32(matched.triggerMeter.value)
            && reader.U64(matched.triggerCycleSequence) && reader.U32(matched.selectionCount);
    }
    case MessageKind::ExecutionEnded:
        return reader.U64(message.executionEnded.executionMarker)
            && ReadEnum(reader, message.executionEnded.result, 2U);
    case MessageKind::RuntimeIssue:
        return ReadEnum(reader, message.runtimeIssue.code, 1U)
            && ReadEnum(reader, message.runtimeIssue.issue.kind, 13U)
            && ReadSource(reader, message.runtimeIssue.issue.source)
            && reader.U32(message.runtimeIssue.issue.subject)
            && reader.U32(message.runtimeIssue.issue.position)
            && reader.I64(message.runtimeIssue.issue.deadlineNanoseconds)
            && reader.U32(message.runtimeIssue.issue.detail)
            && reader.U32(message.runtimeIssue.issue.platformError)
            && reader.U64(message.runtimeIssue.droppedRecords);
    case MessageKind::StateChanged:
        return reader.U32(message.stateChanged.valueIndex)
            && ReadValue(reader, message.stateChanged.value);
    case MessageKind::ArrayChanged:
        return reader.U32(message.arrayChanged.arrayIndex)
            && ReadArrayValue(reader, message.arrayChanged.value);
    case MessageKind::MouseState:
        return ReadMouseSnapshot(reader, message.mouseState);
    case MessageKind::MouseCycleCompleted:
        return reader.U64(message.mouseCycleCompleted.triggerInputSequence)
            && ReadOccurrence(reader, message.mouseCycleCompleted.occurrence);
    case MessageKind::ExecutionMeterSelected:
        return reader.U64(message.executionMeterSelected.executionMarker)
            && ReadOccurrence(reader, message.executionMeterSelected.occurrence);
    }
    return false;
}

[[nodiscard]] bool IsMessageKind(std::uint16_t value) noexcept
{
    switch (static_cast<MessageKind>(value)) {
    case MessageKind::Hello:
    case MessageKind::HelloAccepted:
    case MessageKind::StartCapture:
    case MessageKind::StopCapture:
    case MessageKind::RequestExecutorStop:
    case MessageKind::StreamCompletedAck:
    case MessageKind::CaptureStarted:
    case MessageKind::InputEvent:
    case MessageKind::RuleMatched:
    case MessageKind::ExecutionEnded:
    case MessageKind::RuntimeIssue:
    case MessageKind::StateChanged:
    case MessageKind::ArrayChanged:
    case MessageKind::StreamCompleted:
    case MessageKind::MouseState:
    case MessageKind::MouseCycleCompleted:
    case MessageKind::ExecutionMeterSelected:
        return true;
    }
    return false;
}

} // namespace

bool EncodeMessage(
    const Message& message,
    std::vector<std::uint8_t>& bytes)
{
    try {
        std::vector<std::uint8_t> payload;
        payload.reserve(256U);
        if (!EncodePayload(message, payload)
            || payload.size() > kMaximumFramePayloadBytes
            || payload.size()
                > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        bytes.clear();
        bytes.reserve(kWireHeaderBytes + payload.size());
        ByteWriter writer(bytes);
        writer.U32(kProtocolMagic);
        writer.U16(kProtocolVersion);
        WriteEnum(writer, message.header.kind);
        writer.U32(static_cast<std::uint32_t>(payload.size()));
        writer.U64(message.header.targetSessionId);
        writer.U64(message.header.captureEpoch);
        writer.U64(message.header.protocolSequence);
        writer.I64(message.header.captureTimeNanoseconds);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return true;
    } catch (...) {
        bytes.clear();
        return false;
    }
}

bool DecodeHeader(
    std::span<const std::uint8_t> bytes,
    MessageHeader& header,
    DecodeError& error) noexcept
{
    error = DecodeError::None;
    if (bytes.size() < kWireHeaderBytes) {
        error = DecodeError::Truncated;
        return false;
    }
    ByteReader reader(bytes.first(kWireHeaderBytes));
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t kind{};
    if (!reader.U32(magic)
        || !reader.U16(version)
        || !reader.U16(kind)
        || !reader.U32(header.payloadBytes)
        || !reader.U64(header.targetSessionId)
        || !reader.U64(header.captureEpoch)
        || !reader.U64(header.protocolSequence)
        || !reader.I64(header.captureTimeNanoseconds)) {
        error = DecodeError::Truncated;
        return false;
    }
    if (magic != kProtocolMagic) {
        error = DecodeError::InvalidMagic;
        return false;
    }
    if (version != kProtocolVersion) {
        error = DecodeError::UnsupportedVersion;
        return false;
    }
    if (!IsMessageKind(kind)) {
        error = DecodeError::InvalidMessageKind;
        return false;
    }
    if (header.payloadBytes > kMaximumFramePayloadBytes) {
        error = DecodeError::PayloadTooLarge;
        return false;
    }
    header.kind = static_cast<MessageKind>(kind);
    return true;
}

DecodeResult DecodeMessage(std::span<const std::uint8_t> bytes)
{
    DecodeResult result{};
    if (!DecodeHeader(bytes, result.message.header, result.error)) {
        return result;
    }
    const std::size_t expected = kWireHeaderBytes
        + static_cast<std::size_t>(result.message.header.payloadBytes);
    if (bytes.size() < expected) {
        result.error = DecodeError::Truncated;
        return result;
    }
    if (bytes.size() > expected) {
        result.error = DecodeError::TrailingPayload;
        return result;
    }
    try {
        ByteReader payload(bytes.subspan(kWireHeaderBytes));
        if (!DecodePayload(payload, result.message)) {
            result.error = DecodeError::InvalidPayload;
            return result;
        }
        if (payload.Remaining() != 0U) {
            result.error = DecodeError::TrailingPayload;
        }
    } catch (...) {
        result.error = DecodeError::InvalidPayload;
    }
    return result;
}

} // namespace inputweaver::debug
