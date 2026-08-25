#include "debug_protocol.hpp"

#include <limits>
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
        U8(static_cast<std::uint8_t>(value & 0xffU));
        U8(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    }

    void U32(std::uint32_t value)
    {
        for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
            U8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void U64(std::uint64_t value)
    {
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            U8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void I64(std::int64_t value)
    {
        U64(static_cast<std::uint64_t>(value));
    }

    void Boolean(bool value)
    {
        U8(value ? 1U : 0U);
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
        std::uint8_t low{};
        std::uint8_t high{};
        if (!U8(low) || !U8(high)) {
            return false;
        }
        value = static_cast<std::uint16_t>(low)
            | static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(high) << 8U);
        return true;
    }

    [[nodiscard]] bool U32(std::uint32_t& value) noexcept
    {
        value = 0U;
        for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
            std::uint8_t byte{};
            if (!U8(byte)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    [[nodiscard]] bool U64(std::uint64_t& value) noexcept
    {
        value = 0U;
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            std::uint8_t byte{};
            if (!U8(byte)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
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

    [[nodiscard]] bool Boolean(bool& value) noexcept
    {
        std::uint8_t encoded{};
        if (!U8(encoded) || encoded > 1U) {
            return false;
        }
        value = encoded != 0U;
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
    case MessageKind::CaptureStarted:
        return true;
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
        return true;
    }
    case MessageKind::RuleMatched: {
        const RuleMatchedPayload& matched = message.ruleMatched;
        if (matched.conditionInstructions.size() > kMaximumDebugInstructions
            || matched.actionInstructions.size() > kMaximumDebugInstructions) {
            return false;
        }
        writer.U64(matched.executionMarker);
        writer.U64(matched.triggerInputSequence);
        WriteEnum(writer, matched.eventTransition);
        WriteControl(writer, matched.eventControl);
        writer.U32(static_cast<std::uint32_t>(
            matched.conditionInstructions.size()));
        for (const ExpressionInstruction& instruction
             : matched.conditionInstructions) {
            WriteEnum(writer, instruction.opcode);
            WriteEnum(writer, instruction.type);
            writer.U32(instruction.operand0);
            writer.U32(instruction.operand1);
        }
        writer.U32(static_cast<std::uint32_t>(
            matched.actionInstructions.size()));
        for (const ActionInstruction& instruction : matched.actionInstructions) {
            WriteEnum(writer, instruction.opcode);
            writer.U32(instruction.operand0);
            writer.U32(instruction.operand1);
        }
        return true;
    }
    case MessageKind::ActionStarted:
        writer.U64(message.actionStarted.executionMarker);
        writer.U32(message.actionStarted.instructionIndex);
        return true;
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
    case MessageKind::CaptureStarted:
        return true;
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
            && reader.U32(input.mouseData);
    }
    case MessageKind::RuleMatched: {
        RuleMatchedPayload& matched = message.ruleMatched;
        if (!reader.U64(matched.executionMarker)
            || !reader.U64(matched.triggerInputSequence)
            || !ReadEnum(reader, matched.eventTransition, 2U)
            || !ReadControl(reader, matched.eventControl)) {
            return false;
        }
        std::uint32_t conditionCount{};
        if (!reader.U32(conditionCount)
            || conditionCount > kMaximumDebugInstructions) {
            return false;
        }
        matched.conditionInstructions.resize(conditionCount);
        for (std::uint32_t index = 0U; index < conditionCount; ++index) {
            ExpressionInstruction& instruction =
                matched.conditionInstructions[index];
            if (!ReadEnum(reader, instruction.opcode, 11U)
                || !ReadEnum(reader, instruction.type, 4U)
                || !reader.U32(instruction.operand0)
                || !reader.U32(instruction.operand1)) {
                return false;
            }
        }
        std::uint32_t actionCount{};
        if (!reader.U32(actionCount)
            || actionCount > kMaximumDebugInstructions) {
            return false;
        }
        matched.actionInstructions.resize(actionCount);
        for (std::uint32_t index = 0U; index < actionCount; ++index) {
            ActionInstruction& instruction = matched.actionInstructions[index];
            if (!ReadEnum(reader, instruction.opcode, 14U)
                || !reader.U32(instruction.operand0)
                || !reader.U32(instruction.operand1)) {
                return false;
            }
        }
        return true;
    }
    case MessageKind::ActionStarted:
        return reader.U64(message.actionStarted.executionMarker)
            && reader.U32(message.actionStarted.instructionIndex);
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
    case MessageKind::CaptureStarted:
    case MessageKind::InputEvent:
    case MessageKind::RuleMatched:
    case MessageKind::ActionStarted:
    case MessageKind::ExecutionEnded:
    case MessageKind::RuntimeIssue:
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
