#include "debug_server.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/windows/support/unique_handle.hpp"
#include "program/compiled_program.hpp"
#include "support/bit_mix.hpp"
#include "support/bounded_mpmc_queue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace inputweaver::win32 {
namespace {

inline constexpr std::size_t kMaximumInitialInputs = 256U;
inline constexpr std::size_t kDebugQueueCapacity = 4096U;
inline constexpr DWORD kPipeBufferBytes = 64U * 1024U;

enum class ProducerRecordKind : std::uint8_t {
    CaptureStarted,
    InputEvent,
    RuntimeEvent,
};

struct ProducerRecord final {
    ProducerRecordKind kind{ProducerRecordKind::RuntimeEvent};
    std::uint64_t captureEpoch{};
    std::int64_t captureTimeNanoseconds{};
    std::int64_t captureUnixTimeMilliseconds{};
    debug::InputEventPayload input{};
    RuntimeDebugEvent runtime{};
};

[[nodiscard]] bool ReadExact(
    HANDLE pipe,
    std::span<std::uint8_t> destination) noexcept
{
    std::size_t offset = 0U;
    while (offset < destination.size()) {
        const std::size_t remaining = destination.size() - offset;
        const DWORD requested = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        const UniqueHandle eventOwner(event);
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD read{};
        const BOOL started = ReadFile(
            pipe,
            destination.data() + offset,
            requested,
            &read,
            &overlapped);
        bool completed = started != FALSE;
        if (!completed && GetLastError() == ERROR_IO_PENDING) {
            completed = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0
                && GetOverlappedResult(
                    pipe,
                    &overlapped,
                    &read,
                    FALSE) != FALSE;
        }
        if (!completed || read == 0U) {
            return false;
        }
        offset += read;
    }
    return true;
}

[[nodiscard]] bool WriteExact(
    HANDLE pipe,
    std::span<const std::uint8_t> source) noexcept
{
    std::size_t offset = 0U;
    while (offset < source.size()) {
        const std::size_t remaining = source.size() - offset;
        const DWORD requested = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        const UniqueHandle eventOwner(event);
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD written{};
        const BOOL started = WriteFile(
            pipe,
            source.data() + offset,
            requested,
            &written,
            &overlapped);
        bool completed = started != FALSE;
        if (!completed && GetLastError() == ERROR_IO_PENDING) {
            completed = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0
                && GetOverlappedResult(
                    pipe,
                    &overlapped,
                    &written,
                    FALSE) != FALSE;
        }
        if (!completed || written == 0U) {
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] bool ConnectClient(HANDLE pipe) noexcept
{
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }
    const UniqueHandle eventOwner(event);
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    const BOOL started = ConnectNamedPipe(pipe, &overlapped);
    bool connected = started != FALSE;
    if (!connected) {
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            connected = true;
        } else if (error == ERROR_IO_PENDING) {
            DWORD transferred{};
            connected = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0
                && GetOverlappedResult(
                    pipe,
                    &overlapped,
                    &transferred,
                    FALSE) != FALSE;
        }
    }
    return connected;
}

[[nodiscard]] bool ReadMessage(
    HANDLE pipe,
    debug::Message& message) noexcept
{
    try {
        std::array<std::uint8_t, debug::kWireHeaderBytes> headerBytes{};
        if (!ReadExact(pipe, headerBytes)) {
            return false;
        }
        debug::MessageHeader header{};
        debug::DecodeError error{};
        if (!debug::DecodeHeader(headerBytes, header, error)) {
            return false;
        }
        std::vector<std::uint8_t> frame;
        frame.resize(
            debug::kWireHeaderBytes
            + static_cast<std::size_t>(header.payloadBytes));
        std::copy(headerBytes.begin(), headerBytes.end(), frame.begin());
        if (header.payloadBytes != 0U
            && !ReadExact(
                pipe,
                std::span<std::uint8_t>{frame}.subspan(
                    debug::kWireHeaderBytes))) {
            return false;
        }
        debug::DecodeResult decoded = debug::DecodeMessage(frame);
        if (!decoded.Succeeded()) {
            return false;
        }
        message = std::move(decoded.message);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool WriteMessage(
    HANDLE pipe,
    const debug::Message& message) noexcept
{
    try {
        std::vector<std::uint8_t> frame;
        if (!debug::EncodeMessage(message, frame)
            || frame.size() < debug::kWireHeaderBytes) {
            return false;
        }
        return WriteExact(pipe, frame);
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool ReadTokenUser(
    HANDLE process,
    std::vector<std::uint8_t>& storage,
    TOKEN_USER*& user) noexcept
{
    HANDLE token{};
    if (OpenProcessToken(process, TOKEN_QUERY, &token) == FALSE) {
        return false;
    }
    const UniqueHandle tokenOwner(token);
    DWORD required{};
    (void)GetTokenInformation(token, TokenUser, nullptr, 0U, &required);
    if (required == 0U) {
        return false;
    }
    try {
        storage.resize(required);
    } catch (...) {
        return false;
    }
    const BOOL read = GetTokenInformation(
        token,
        TokenUser,
        storage.data(),
        required,
        &required);
    if (read == FALSE) {
        return false;
    }
    user = reinterpret_cast<TOKEN_USER*>(storage.data());
    return true;
}

[[nodiscard]] bool ClientRunsAsCurrentUser(HANDLE pipe) noexcept
{
    ULONG clientProcessId{};
    if (GetNamedPipeClientProcessId(pipe, &clientProcessId) == FALSE) {
        return false;
    }
    HANDLE client = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        clientProcessId);
    if (client == nullptr) {
        return false;
    }
    const UniqueHandle clientOwner(client);
    std::vector<std::uint8_t> currentStorage;
    std::vector<std::uint8_t> clientStorage;
    TOKEN_USER* currentUser{};
    TOKEN_USER* clientUser{};
    const bool currentRead = ReadTokenUser(
        GetCurrentProcess(),
        currentStorage,
        currentUser);
    const bool clientRead = ReadTokenUser(client, clientStorage, clientUser);
    return currentRead
        && clientRead
        && EqualSid(currentUser->User.Sid, clientUser->User.Sid) != FALSE;
}

[[nodiscard]] bool ProgramFitsProtocol(
    const CompiledProgram& program) noexcept
{
    const auto textBytes = [&program](StringId text) noexcept -> std::uint64_t {
        return text.IsValid() && text.value < program.Strings().size()
            ? program.Strings()[text.value].size()
            : 0U;
    };
    if (program.DebugInfo().variables.size() + 1U
        > debug::kMaximumDebugValues) {
        return false;
    }
    std::uint64_t captureBytes = 8U + 4U + 4U + 5U + 2U;
    for (const VariableDebugRecord& variable : program.DebugInfo().variables) {
        const std::uint64_t nameBytes = textBytes(variable.name);
        if (nameBytes > debug::kMaximumDebugTextBytes) {
            return false;
        }
        captureBytes += 4U + nameBytes + 9U;
    }
    if (captureBytes > debug::kMaximumFramePayloadBytes) {
        return false;
    }
    for (const CompiledRule& rule : program.Rules()) {
        const auto debugRule = std::find_if(
            program.DebugInfo().rules.begin(),
            program.DebugInfo().rules.end(),
            [&rule](const RuleDebugRecord& candidate) noexcept {
                return candidate.sourceOrdinal == rule.sourceOrdinal;
            });
        const std::uint64_t conditionTextBytes = debugRule
                == program.DebugInfo().rules.end()
            ? 6U
            : textBytes(debugRule->conditionText);
        const std::uint64_t actionTextBytes = debugRule
                == program.DebugInfo().rules.end()
            ? (rule.kind == RuleKind::MappingDown ? 7U : 0U)
            : textBytes(debugRule->actionText);
        if (rule.condition.IsValid()
            && program.Expressions()[rule.condition.value].code.count
                > debug::kMaximumDebugInstructions) {
            return false;
        }
        if (rule.action.IsValid()
            && program.ActionPrograms()[rule.action.value].code.count
                > debug::kMaximumDebugInstructions) {
            return false;
        }
        const std::uint64_t conditionCount = rule.condition.IsValid()
            ? program.Expressions()[rule.condition.value].code.count
            : 0U;
        const std::uint64_t actionCount = rule.kind == RuleKind::MappingDown
            ? 1U
            : rule.action.IsValid()
                ? program.ActionPrograms()[rule.action.value].code.count
                : 0U;
        std::uint64_t payloadBytes = 8U + 8U + 1U + 16U
            + 4U + conditionTextBytes + 4U + actionTextBytes
            + 4U + conditionCount * 10U
            + 4U + actionCount * 9U;
        if (conditionTextBytes > debug::kMaximumDebugTextBytes
            || actionTextBytes > debug::kMaximumDebugTextBytes
            || payloadBytes > debug::kMaximumFramePayloadBytes) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string NormalizeDebugText(std::string_view source)
{
    std::string text;
    text.reserve(source.size());
    bool pendingSpace = false;
    for (const char byte : source) {
        if (std::isspace(static_cast<unsigned char>(byte)) != 0) {
            pendingSpace = !text.empty();
            continue;
        }
        if (pendingSpace) {
            text.push_back(' ');
            pendingSpace = false;
        }
        text.push_back(byte);
    }
    while (!text.empty()
        && (text.back() == ' ' || text.back() == ';')) {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] bool IsValidDebugSessionToken(std::wstring_view token) noexcept
{
    if (token.empty() || token.size() > 64U) {
        return false;
    }
    for (const wchar_t character : token) {
        const bool digit = character >= L'0' && character <= L'9';
        const bool lower = character >= L'a' && character <= L'z';
        const bool upper = character >= L'A' && character <= L'Z';
        if (!digit && !lower && !upper
            && character != L'-'
            && character != L'_'
            && character != L'.') {
            return false;
        }
    }
    return true;
}

} // namespace

struct WindowsDebugServer::Impl final {
    struct StateDescriptor final {
        std::string name;
        ValueRefId reference{};
        ValueType type{ValueType::State};
    };

    [[nodiscard]] static std::uint64_t EncodeStateBits(
        const debug::DebugValue& value) noexcept
    {
        switch (value.type) {
        case ValueType::State:
            return value.stateValue ? 1U : 0U;
        case ValueType::Number:
            return std::bit_cast<std::uint64_t>(value.numberValue);
        case ValueType::Duration:
            return std::bit_cast<std::uint64_t>(
                value.durationValue.nanoseconds);
        }
        return 0U;
    }

    [[nodiscard]] debug::DebugValue ReadStateValue(
        std::size_t index) const noexcept
    {
        debug::DebugValue value{};
        if (index >= stateDescriptors.size() || stateBits == nullptr) {
            return value;
        }
        value.type = stateDescriptors[index].type;
        const std::uint64_t bits = stateBits[index].load(
            std::memory_order_acquire);
        switch (value.type) {
        case ValueType::State:
            value.stateValue = bits != 0U;
            break;
        case ValueType::Number:
            value.numberValue = std::bit_cast<double>(bits);
            break;
        case ValueType::Duration:
            value.durationValue.nanoseconds = std::bit_cast<std::int64_t>(bits);
            break;
        }
        return value;
    }

    [[nodiscard]] std::uint32_t StateIndex(
        const RuntimeDebugValue& value) const noexcept
    {
        if (!value.reference.IsValid()) {
            return value.type == ValueType::State ? 0U : kInvalidProgramIndex;
        }
        return value.reference.value < valueRefStateIndices.size()
            ? valueRefStateIndices[value.reference.value]
            : kInvalidProgramIndex;
    }

    [[nodiscard]] debug::DebugValue ProtocolValue(
        const RuntimeDebugValue& source) const noexcept
    {
        debug::DebugValue value{};
        value.type = source.type;
        value.stateValue = source.stateValue;
        value.numberValue = source.numberValue;
        value.durationValue = source.durationValue;
        return value;
    }

    [[nodiscard]] bool UpdateState(const RuntimeDebugValue& value) noexcept
    {
        const std::uint32_t index = StateIndex(value);
        if (index == kInvalidProgramIndex
            || index >= stateDescriptors.size()
            || stateDescriptors[index].type != value.type
            || stateBits == nullptr) {
            return false;
        }
        stateBits[index].store(
            EncodeStateBits(ProtocolValue(value)),
            std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool InitializeState()
    {
        if (program == nullptr) {
            return false;
        }
        stateDescriptors.clear();
        valueRefStateIndices.assign(
            program->ValueRefs().size(),
            kInvalidProgramIndex);
        stateDescriptors.push_back({"PAUSE", {}, ValueType::State});
        for (const VariableDebugRecord& variable : program->DebugInfo().variables) {
            if (!variable.name.IsValid()
                || variable.name.value >= program->Strings().size()
                || !variable.value.IsValid()
                || variable.value.value >= program->ValueRefs().size()) {
                return false;
            }
            const ValueRef& value = program->ValueRefs()[variable.value.value];
            const std::uint32_t index = static_cast<std::uint32_t>(
                stateDescriptors.size());
            stateDescriptors.push_back({
                program->Strings()[variable.name.value],
                variable.value,
                value.type});
            valueRefStateIndices[variable.value.value] = index;
        }
        stateBits = std::make_unique<std::atomic<std::uint64_t>[]>(
            stateDescriptors.size());
        stateBits[0].store(1U, std::memory_order_relaxed);
        for (std::size_t index = 1U; index < stateDescriptors.size(); ++index) {
            const ValueRef& value = program->ValueRefs()[
                stateDescriptors[index].reference.value];
            debug::DebugValue initial{};
            initial.type = value.type;
            if (value.domain == ValueDomain::UserState
                && value.index < program->UserValues().initialStates.size()) {
                initial.stateValue =
                    program->UserValues().initialStates[value.index] != 0U;
            } else if (value.domain == ValueDomain::UserNumber
                && value.index < program->UserValues().initialNumbers.size()) {
                initial.numberValue =
                    program->UserValues().initialNumbers[value.index];
            } else if (value.domain == ValueDomain::UserDuration
                && value.index < program->UserValues().initialDurations.size()) {
                initial.durationValue =
                    program->UserValues().initialDurations[value.index];
            } else {
                return false;
            }
            stateBits[index].store(
                EncodeStateBits(initial),
                std::memory_order_relaxed);
        }
        return true;
    }

    [[nodiscard]] bool PopulateStateSnapshot(
        std::vector<debug::DebugNamedValue>& values) const
    {
        try {
            values.clear();
            values.reserve(stateDescriptors.size());
            for (std::size_t index = 0U; index < stateDescriptors.size(); ++index) {
                values.push_back({
                    stateDescriptors[index].name,
                    ReadStateValue(index)});
            }
            return true;
        } catch (...) {
            values.clear();
            return false;
        }
    }

    [[nodiscard]] std::int64_t CaptureTimeNanoseconds() const noexcept
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        const std::int64_t whole = counter.QuadPart / performanceFrequency;
        const std::int64_t remainder = counter.QuadPart % performanceFrequency;
        return whole * 1'000'000'000LL
            + (remainder * 1'000'000'000LL) / performanceFrequency;
    }

    [[nodiscard]] std::int64_t CaptureUnixTimeMilliseconds() const noexcept
    {
        FILETIME fileTime{};
        GetSystemTimePreciseAsFileTime(&fileTime);
        ULARGE_INTEGER ticks{};
        ticks.LowPart = fileTime.dwLowDateTime;
        ticks.HighPart = fileTime.dwHighDateTime;
        constexpr std::uint64_t kUnixEpochFileTimeTicks =
            116'444'736'000'000'000ULL;
        if (ticks.QuadPart < kUnixEpochFileTimeTicks) {
            return 0;
        }
        return static_cast<std::int64_t>(
            (ticks.QuadPart - kUnixEpochFileTimeTicks) / 10'000ULL);
    }

    [[nodiscard]] HANDLE CreateServerPipe(std::wstring& errorMessage) const
    {
        const HANDLE pipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE
                | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
                | PIPE_REJECT_REMOTE_CLIENTS,
            1U,
            kPipeBufferBytes,
            kPipeBufferBytes,
            0U,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            errorMessage = L"Cannot create the input debug pipe. Win32 error "
                + std::to_wstring(GetLastError()) + L".";
        }
        return pipe;
    }

    [[nodiscard]] bool PushRecord(
        const ProducerRecord& record,
        bool notify = true) noexcept
    {
        if (!queue.TryPush(record)) {
            MarkOverflow(record.captureEpoch);
            return false;
        }
        if (notify) {
            SetEvent(queueEvent);
        }
        return true;
    }

    void MarkOverflow(std::uint64_t epoch) noexcept
    {
        if (epoch == 0U) {
            return;
        }
        captureReady.store(false, std::memory_order_release);
        captureActive.store(false, std::memory_order_release);
        lostEpoch.store(epoch, std::memory_order_release);
        droppedRecords.fetch_add(1U, std::memory_order_relaxed);
        overflowPending.store(true, std::memory_order_release);
        SetEvent(queueEvent);
    }

    void DrainQueue() noexcept
    {
        ProducerRecord ignored{};
        while (queue.TryPop(ignored)) {
        }
    }

    [[nodiscard]] debug::Message MakeMessage(
        debug::MessageKind kind,
        std::uint64_t epoch,
        std::int64_t time,
        std::uint64_t& sequence) const noexcept
    {
        debug::Message message{};
        message.header.kind = kind;
        message.header.targetSessionId = targetSessionId;
        message.header.captureEpoch = epoch;
        message.header.protocolSequence = sequence++;
        message.header.captureTimeNanoseconds = time;
        return message;
    }

    [[nodiscard]] bool PopulateRuleMatched(
        const RuntimeDebugEvent& event,
        debug::RuleMatchedPayload& payload) const
    {
        if (program == nullptr
            || event.ruleIndex >= program->Rules().size()
            || !event.eventKey.control.IsValid()
            || event.eventKey.control.value >= program->Controls().size()) {
            return false;
        }
        payload.executionMarker = event.executionMarker;
        payload.triggerInputSequence = event.triggerInputSequence;
        payload.eventTransition = event.eventKey.transition;
        payload.eventControl = program->Controls()[event.eventKey.control.value];
        const CompiledRule& rule = program->Rules()[event.ruleIndex];
        const auto debugRules = program->DebugInfo().rules;
        const auto debugRule = std::find_if(
            debugRules.begin(),
            debugRules.end(),
            [&rule](const RuleDebugRecord& candidate) noexcept {
                return candidate.sourceOrdinal == rule.sourceOrdinal;
            });
        const auto readText = [this](StringId text) {
            return text.IsValid() && text.value < program->Strings().size()
                ? NormalizeDebugText(program->Strings()[text.value])
                : std::string{};
        };
        payload.conditionText = debugRule != debugRules.end()
            ? readText(debugRule->conditionText)
            : std::string{};
        if (payload.conditionText.empty()) {
            payload.conditionText = "always";
        }
        payload.actionText = debugRule != debugRules.end()
            ? readText(debugRule->actionText)
            : std::string{};
        if (rule.condition.IsValid()) {
            if (rule.condition.value >= program->Expressions().size()) {
                return false;
            }
            const ExpressionDescriptor& descriptor =
                program->Expressions()[rule.condition.value];
            const TableRange range = descriptor.code;
            if (range.count > debug::kMaximumDebugInstructions) {
                return false;
            }
            const auto code = program->ExpressionCode().subspan(
                range.begin,
                range.count);
            payload.conditionInstructions.assign(code.begin(), code.end());
        }
        if (rule.kind == RuleKind::MappingDown) {
            payload.actionInstructions.push_back({
                ActionOpcode::End,
                0U,
                0U});
            if (payload.actionText.empty()) {
                payload.actionText = "mapping";
            }
            return true;
        }
        if (!rule.action.IsValid()
            || rule.action.value >= program->ActionPrograms().size()) {
            return false;
        }
        const ActionProgramDescriptor& descriptor =
            program->ActionPrograms()[rule.action.value];
        const TableRange range = descriptor.code;
        if (range.count > debug::kMaximumDebugInstructions) {
            return false;
        }
        const auto code = program->ActionCode().subspan(range.begin, range.count);
        payload.actionInstructions.assign(code.begin(), code.end());
        return true;
    }

    [[nodiscard]] bool SendRuntimeRecord(
        HANDLE pipe,
        const ProducerRecord& record,
        std::uint64_t& protocolSequence) const
    {
        const RuntimeDebugEvent& event = record.runtime;
        if (event.kind == RuntimeDebugEventKind::RuleMatched) {
            debug::Message message = MakeMessage(
                debug::MessageKind::RuleMatched,
                record.captureEpoch,
                record.captureTimeNanoseconds,
                protocolSequence);
            if (!PopulateRuleMatched(event, message.ruleMatched)
                || !WriteMessage(pipe, message)) {
                return false;
            }
            return true;
        }
        if (event.kind == RuntimeDebugEventKind::ActionStarted) {
            debug::Message message = MakeMessage(
                debug::MessageKind::ActionStarted,
                record.captureEpoch,
                record.captureTimeNanoseconds,
                protocolSequence);
            message.actionStarted.executionMarker = event.executionMarker;
            message.actionStarted.instructionIndex = event.instructionIndex;
            return WriteMessage(pipe, message);
        }
        if (event.kind == RuntimeDebugEventKind::ExecutionEnded) {
            debug::Message message = MakeMessage(
                debug::MessageKind::ExecutionEnded,
                record.captureEpoch,
                record.captureTimeNanoseconds,
                protocolSequence);
            message.executionEnded.executionMarker = event.executionMarker;
            message.executionEnded.result = event.result;
            return WriteMessage(pipe, message);
        }
        if (event.kind == RuntimeDebugEventKind::StateChanged) {
            const std::uint32_t valueIndex = StateIndex(event.value);
            if (valueIndex == kInvalidProgramIndex) {
                return false;
            }
            debug::Message message = MakeMessage(
                debug::MessageKind::StateChanged,
                record.captureEpoch,
                record.captureTimeNanoseconds,
                protocolSequence);
            message.stateChanged.valueIndex = valueIndex;
            message.stateChanged.value = ProtocolValue(event.value);
            return WriteMessage(pipe, message);
        }
        debug::Message message = MakeMessage(
            debug::MessageKind::RuntimeIssue,
            record.captureEpoch,
            record.captureTimeNanoseconds,
            protocolSequence);
        message.runtimeIssue.code = debug::IssueCode::RuntimeDiagnostic;
        message.runtimeIssue.issue = event.issue;
        return WriteMessage(pipe, message);
    }

    [[nodiscard]] bool SendOverflowIssue(
        HANDLE pipe,
        std::uint64_t& protocolSequence) noexcept
    {
        if (!overflowPending.exchange(false, std::memory_order_acq_rel)) {
            return true;
        }
        DrainQueue();
        const std::uint64_t epoch = lostEpoch.load(std::memory_order_acquire);
        debug::Message message = MakeMessage(
            debug::MessageKind::RuntimeIssue,
            epoch,
            CaptureTimeNanoseconds(),
            protocolSequence);
        message.runtimeIssue.code = debug::IssueCode::DebugStreamOverflow;
        message.runtimeIssue.droppedRecords = droppedRecords.exchange(
            0U,
            std::memory_order_acq_rel);
        return WriteMessage(pipe, message);
    }

    [[nodiscard]] bool SendProducerRecord(
        HANDLE pipe,
        const ProducerRecord& record,
        std::uint64_t& protocolSequence,
        std::uint64_t& writerEpoch)
    {
        const std::uint64_t activeEpoch = captureEpoch.load(
            std::memory_order_acquire);
        if (!captureActive.load(std::memory_order_acquire)
            || record.captureEpoch != activeEpoch) {
            return true;
        }
        if (record.kind == ProducerRecordKind::CaptureStarted) {
            writerEpoch = record.captureEpoch;
            debug::Message message = MakeMessage(
                debug::MessageKind::CaptureStarted,
                record.captureEpoch,
                record.captureTimeNanoseconds,
                protocolSequence);
            message.captureStarted.captureUnixTimeMilliseconds =
                record.captureUnixTimeMilliseconds;
            if (!PopulateStateSnapshot(message.captureStarted.values)) {
                return false;
            }
            return WriteMessage(pipe, message);
        }
        if (record.captureEpoch != writerEpoch) {
            return true;
        }
        if (record.kind == ProducerRecordKind::InputEvent) {
            debug::Message message = MakeMessage(
                debug::MessageKind::InputEvent,
                record.captureEpoch,
                record.captureTimeNanoseconds,
                protocolSequence);
            message.inputEvent = record.input;
            return WriteMessage(pipe, message);
        }
        return SendRuntimeRecord(pipe, record, protocolSequence);
    }

    void WriterMain(HANDLE pipe) noexcept
    {
        std::uint64_t protocolSequence = 1U;
        std::uint64_t writerEpoch{};
        const HANDLE waitHandles[] = {connectionStopEvent, queueEvent};
        bool healthy = true;
        while (healthy
            && WaitForSingleObject(connectionStopEvent, 0U) != WAIT_OBJECT_0) {
            if (!captureActive.load(std::memory_order_acquire)) {
                writerEpoch = 0U;
            }
            healthy = SendOverflowIssue(pipe, protocolSequence);
            ProducerRecord record{};
            while (healthy && queue.TryPop(record)) {
                healthy = SendProducerRecord(
                    pipe,
                    record,
                    protocolSequence,
                    writerEpoch);
            }
            if (healthy) {
                const DWORD wait = WaitForMultipleObjects(
                    2U,
                    waitHandles,
                    FALSE,
                    INFINITE);
                healthy = wait == WAIT_OBJECT_0 + 1U;
            }
        }
        if (!healthy
            && WaitForSingleObject(connectionStopEvent, 0U) != WAIT_OBJECT_0) {
            writerBroken.store(true, std::memory_order_release);
            (void)CancelIoEx(pipe, nullptr);
            const HANDLE threadHandle = serverThreadHandle.load(
                std::memory_order_acquire);
            if (threadHandle != nullptr) {
                (void)CancelSynchronousIo(threadHandle);
            }
        }
    }

    [[nodiscard]] bool PerformHandshake(HANDLE pipe) const noexcept
    {
        debug::Message hello{};
        if (!ReadMessage(pipe, hello)
            || hello.header.kind != debug::MessageKind::Hello
            || hello.hello.minimumVersion > debug::kProtocolVersion
            || hello.hello.maximumVersion < debug::kProtocolVersion) {
            return false;
        }
        debug::Message accepted{};
        accepted.header.kind = debug::MessageKind::HelloAccepted;
        accepted.header.targetSessionId = targetSessionId;
        accepted.header.protocolSequence = 0U;
        accepted.helloAccepted.selectedVersion = debug::kProtocolVersion;
        accepted.helloAccepted.processId = GetCurrentProcessId();
        return WriteMessage(pipe, accepted);
    }

    void RequestCapture(DebugCaptureRequest request) noexcept
    {
        pendingCaptureRequest.store(request, std::memory_order_release);
        callbacks.wakeInputThread.Invoke();
    }

    void HandleConnection(HANDLE pipe) noexcept
    {
        if (!ClientRunsAsCurrentUser(pipe)) {
            return;
        }
        if (!PerformHandshake(pipe)) {
            return;
        }
        clientConnected.store(true, std::memory_order_release);
        DrainQueue();
        captureReady.store(false, std::memory_order_release);
        captureActive.store(false, std::memory_order_release);
        ResetEvent(connectionStopEvent);
        writerBroken.store(false, std::memory_order_release);
        std::thread writer;
        try {
            writer = std::thread(
                &Impl::WriterMain,
                this,
                pipe);
        } catch (...) {
            return;
        }

        debug::Message command{};
        while (!stopping.load(std::memory_order_acquire)
            && !writerBroken.load(std::memory_order_acquire)
            && ReadMessage(pipe, command)) {
            if (command.header.targetSessionId != 0U
                && command.header.targetSessionId != targetSessionId) {
                break;
            }
            if (command.header.kind == debug::MessageKind::StartCapture) {
                RequestCapture(DebugCaptureRequest::Start);
            } else if (command.header.kind == debug::MessageKind::StopCapture) {
                RequestCapture(DebugCaptureRequest::Stop);
            } else if (command.header.kind
                    == debug::MessageKind::RequestExecutorStop) {
                callbacks.requestExecutorStop.Invoke();
            } else {
                break;
            }
        }
        clientConnected.store(false, std::memory_order_release);
        captureReady.store(false, std::memory_order_release);
        captureActive.store(false, std::memory_order_release);
        RequestCapture(DebugCaptureRequest::Stop);
        SetEvent(connectionStopEvent);
        SetEvent(queueEvent);
        (void)CancelIoEx(pipe, nullptr);
        if (writer.joinable()) {
            (void)CancelSynchronousIo(writer.native_handle());
            writer.join();
        }
        DrainQueue();
    }

    void ServerMain(HANDLE firstPipe) noexcept
    {
        HANDLE pipe = firstPipe;
        while (!stopping.load(std::memory_order_acquire)) {
            currentPipe.store(pipe, std::memory_order_release);
            if (ConnectClient(pipe)) {
                HandleConnection(pipe);
            }
            currentPipe.store(INVALID_HANDLE_VALUE, std::memory_order_release);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            if (stopping.load(std::memory_order_acquire)) {
                break;
            }
            std::wstring ignored;
            pipe = CreateServerPipe(ignored);
            if (pipe == INVALID_HANDLE_VALUE) {
                break;
            }
        }
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
        serverThreadHandle.store(nullptr, std::memory_order_release);
    }

    std::wstring pipeName;
    std::shared_ptr<const CompiledProgram> program;
    std::vector<StateDescriptor> stateDescriptors;
    std::vector<std::uint32_t> valueRefStateIndices;
    std::unique_ptr<std::atomic<std::uint64_t>[]> stateBits;
    DebugServerCallbacks callbacks{};
    std::uint64_t targetSessionId{};
    std::int64_t performanceFrequency{1};
    HANDLE queueEvent{};
    HANDLE connectionStopEvent{};
    support::BoundedMpmcQueue<ProducerRecord, kDebugQueueCapacity> queue;
    std::atomic<DebugCaptureRequest> pendingCaptureRequest{
        DebugCaptureRequest::None};
    std::atomic<bool> captureActive{false};
    std::atomic<bool> captureReady{false};
    std::atomic<bool> clientConnected{false};
    std::atomic<std::uint64_t> captureEpoch{0U};
    std::atomic<std::uint64_t> nextInputSequence{1U};
    std::atomic<bool> overflowPending{false};
    std::atomic<std::uint64_t> lostEpoch{0U};
    std::atomic<std::uint64_t> droppedRecords{0U};
    std::atomic<bool> stopping{false};
    std::atomic<bool> started{false};
    std::atomic<bool> writerBroken{false};
    std::atomic<HANDLE> currentPipe{INVALID_HANDLE_VALUE};
    std::atomic<HANDLE> serverThreadHandle{nullptr};
    std::thread serverThread;
};

WindowsDebugServer::WindowsDebugServer()
    : impl_(std::make_unique<Impl>())
{
}

WindowsDebugServer::~WindowsDebugServer()
{
    Stop();
}

bool WindowsDebugServer::Start(
    std::wstring token,
    std::shared_ptr<const CompiledProgram> program,
    DebugServerCallbacks callbacks,
    std::wstring& errorMessage)
{
    if (!IsValidDebugSessionToken(token)) {
        errorMessage = L"The debug session token is invalid.";
        return false;
    }
    if (program == nullptr) {
        errorMessage = L"The input debug server requires a compiled program.";
        return false;
    }
    if (!ProgramFitsProtocol(*program)) {
        errorMessage = L"The compiled program exceeds input debug protocol limits.";
        return false;
    }
    if (impl_->started.exchange(true, std::memory_order_acq_rel)) {
        errorMessage = L"The input debug server has already been started.";
        return false;
    }
    impl_->pipeName = MakeDebugPipeName(GetCurrentProcessId(), token);
    impl_->program = std::move(program);
    impl_->callbacks = callbacks;
    try {
        if (!impl_->InitializeState()) {
            errorMessage = L"Cannot initialize input debug variable state.";
            Stop();
            return false;
        }
    } catch (...) {
        errorMessage = L"Cannot allocate input debug variable state.";
        Stop();
        return false;
    }
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) != FALSE
        && frequency.QuadPart > 0) {
        impl_->performanceFrequency = frequency.QuadPart;
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    impl_->targetSessionId = support::Mix64(
        static_cast<std::uint64_t>(counter.QuadPart)
        ^ (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32U)
        ^ static_cast<std::uint64_t>(GetTickCount64()));
    if (impl_->targetSessionId == 0U) {
        impl_->targetSessionId = 1U;
    }
    impl_->queueEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    impl_->connectionStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (impl_->queueEvent == nullptr || impl_->connectionStopEvent == nullptr) {
        errorMessage = L"Cannot create input debug synchronization events. Win32 error "
            + std::to_wstring(GetLastError()) + L".";
        Stop();
        return false;
    }
    HANDLE firstPipe = impl_->CreateServerPipe(errorMessage);
    if (firstPipe == INVALID_HANDLE_VALUE) {
        Stop();
        return false;
    }
    try {
        impl_->serverThread = std::thread(
            &Impl::ServerMain,
            impl_.get(),
            firstPipe);
        impl_->serverThreadHandle.store(
            impl_->serverThread.native_handle(),
            std::memory_order_release);
    } catch (...) {
        CloseHandle(firstPipe);
        errorMessage = L"Cannot create the input debug server thread.";
        Stop();
        return false;
    }
    return true;
}

void WindowsDebugServer::Stop() noexcept
{
    if (!impl_->started.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    impl_->stopping.store(true, std::memory_order_release);
    impl_->captureReady.store(false, std::memory_order_release);
    impl_->captureActive.store(false, std::memory_order_release);
    if (impl_->connectionStopEvent != nullptr) {
        SetEvent(impl_->connectionStopEvent);
    }
    if (impl_->queueEvent != nullptr) {
        SetEvent(impl_->queueEvent);
    }
    const HANDLE threadHandle = impl_->serverThreadHandle.load(
        std::memory_order_acquire);
    if (threadHandle != nullptr) {
        (void)CancelSynchronousIo(threadHandle);
    }
    const HANDLE pipe = impl_->currentPipe.load(std::memory_order_acquire);
    if (pipe != INVALID_HANDLE_VALUE) {
        (void)CancelIoEx(pipe, nullptr);
    }
    if (impl_->serverThread.joinable()) {
        impl_->serverThread.join();
    }
    if (impl_->queueEvent != nullptr) {
        CloseHandle(impl_->queueEvent);
        impl_->queueEvent = nullptr;
    }
    if (impl_->connectionStopEvent != nullptr) {
        CloseHandle(impl_->connectionStopEvent);
        impl_->connectionStopEvent = nullptr;
    }
    impl_->program.reset();
    impl_->stateDescriptors.clear();
    impl_->valueRefStateIndices.clear();
    impl_->stateBits.reset();
}

DebugCaptureRequest WindowsDebugServer::TakeCaptureRequest() noexcept
{
    return impl_->pendingCaptureRequest.exchange(
        DebugCaptureRequest::None,
        std::memory_order_acq_rel);
}

bool WindowsDebugServer::BeginCapture(
    std::span<const debug::InputEventPayload> initialInputs) noexcept
{
    if (initialInputs.size() > kMaximumInitialInputs
        || impl_->stopping.load(std::memory_order_acquire)
        || !impl_->clientConnected.load(std::memory_order_acquire)) {
        return false;
    }
    impl_->captureReady.store(false, std::memory_order_release);
    impl_->captureActive.store(false, std::memory_order_release);
    std::uint64_t epoch = impl_->captureEpoch.fetch_add(
        1U,
        std::memory_order_acq_rel) + 1U;
    if (epoch == 0U) {
        epoch = impl_->captureEpoch.fetch_add(
            1U,
            std::memory_order_acq_rel) + 1U;
    }
    impl_->captureActive.store(true, std::memory_order_release);
    std::uint64_t inputSequence = 1U;
    ProducerRecord started{};
    started.kind = ProducerRecordKind::CaptureStarted;
    started.captureEpoch = epoch;
    started.captureTimeNanoseconds = impl_->CaptureTimeNanoseconds();
    started.captureUnixTimeMilliseconds =
        impl_->CaptureUnixTimeMilliseconds();
    if (!impl_->PushRecord(started, false)) {
        return false;
    }
    for (const debug::InputEventPayload& source : initialInputs) {
        debug::InputEventPayload input = source;
        input.inputSequence = inputSequence++;
        input.origin = InputOrigin::InitialSample;
        input.transition = Transition::Down;
        input.disposition = debug::InputDisposition::NotApplicable;
        ProducerRecord record{};
        record.kind = ProducerRecordKind::InputEvent;
        record.captureEpoch = epoch;
        record.captureTimeNanoseconds = impl_->CaptureTimeNanoseconds();
        record.input = input;
        if (!impl_->PushRecord(record, false)) {
            return false;
        }
    }
    impl_->nextInputSequence.store(inputSequence, std::memory_order_release);
    if (!impl_->clientConnected.load(std::memory_order_acquire)
        || impl_->stopping.load(std::memory_order_acquire)) {
        impl_->captureActive.store(false, std::memory_order_release);
        SetEvent(impl_->queueEvent);
        return false;
    }
    impl_->captureReady.store(true, std::memory_order_release);
    SetEvent(impl_->queueEvent);
    return true;
}

void WindowsDebugServer::EndCapture() noexcept
{
    impl_->captureReady.store(false, std::memory_order_release);
    impl_->captureActive.store(false, std::memory_order_release);
    if (impl_->queueEvent != nullptr) {
        SetEvent(impl_->queueEvent);
    }
}

DebugInputCorrelation WindowsDebugServer::BeginInput() noexcept
{
    if (!impl_->captureReady.load(std::memory_order_acquire)
        || !impl_->captureActive.load(std::memory_order_acquire)) {
        return {};
    }
    const std::uint64_t epoch = impl_->captureEpoch.load(
        std::memory_order_acquire);
    const std::uint64_t sequence = impl_->nextInputSequence.fetch_add(
        1U,
        std::memory_order_acq_rel);
    if (epoch == 0U || sequence == 0U
        || !impl_->captureActive.load(std::memory_order_acquire)
        || !impl_->captureReady.load(std::memory_order_acquire)
        || epoch != impl_->captureEpoch.load(std::memory_order_acquire)) {
        return {};
    }
    return {epoch, sequence};
}

bool WindowsDebugServer::PublishInput(
    const DebugInputCorrelation& correlation,
    const debug::InputEventPayload& input) noexcept
{
    if (!correlation.Active()
        || !impl_->captureActive.load(std::memory_order_acquire)
        || !impl_->captureReady.load(std::memory_order_acquire)
        || correlation.captureEpoch
            != impl_->captureEpoch.load(std::memory_order_acquire)) {
        return false;
    }
    ProducerRecord record{};
    record.kind = ProducerRecordKind::InputEvent;
    record.captureEpoch = correlation.captureEpoch;
    record.captureTimeNanoseconds = impl_->CaptureTimeNanoseconds();
    record.input = input;
    record.input.inputSequence = correlation.inputSequence;
    return impl_->PushRecord(record);
}

bool WindowsDebugServer::Publish(const RuntimeDebugEvent& event) noexcept
{
    if (event.kind == RuntimeDebugEventKind::StateChanged
        && !impl_->UpdateState(event.value)) {
        return false;
    }
    if (!impl_->captureReady.load(std::memory_order_acquire)
        || !impl_->captureActive.load(std::memory_order_acquire)) {
        return event.kind == RuntimeDebugEventKind::StateChanged;
    }
    const std::uint64_t activeEpoch = impl_->captureEpoch.load(
        std::memory_order_acquire);
    const std::uint64_t eventEpoch =
        (event.kind == RuntimeDebugEventKind::RuntimeIssue
            || event.kind == RuntimeDebugEventKind::StateChanged)
        && event.captureEpoch == 0U
        ? activeEpoch
        : event.captureEpoch;
    if (eventEpoch == 0U || eventEpoch != activeEpoch) {
        return false;
    }
    ProducerRecord record{};
    record.kind = ProducerRecordKind::RuntimeEvent;
    record.captureEpoch = eventEpoch;
    if (event.kind != RuntimeDebugEventKind::ExecutionEnded) {
        record.captureTimeNanoseconds = impl_->CaptureTimeNanoseconds();
    }
    record.runtime = event;
    record.runtime.captureEpoch = eventEpoch;
    return impl_->PushRecord(record);
}

std::wstring MakeDebugPipeName(
    std::uint32_t processId,
    std::wstring_view token)
{
    return L"\\\\.\\pipe\\InputWeaver.Debug."
        + std::to_wstring(processId)
        + L"."
        + std::wstring(token);
}

} // namespace inputweaver::win32
