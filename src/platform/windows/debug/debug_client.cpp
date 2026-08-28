#include "debug_client.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "pipe_transport.hpp"
#include "support/little_endian.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace inputweaver::win32 {
namespace {

inline constexpr DWORD kConnectTimeoutMilliseconds = 5'000U;
inline constexpr DWORD kHandshakeTimeoutMilliseconds = 5'000U;

enum class FrameResult : std::uint8_t {
    Message,
    Corrupt,
    Closed,
    Cancelled,
};

[[nodiscard]] std::uint32_t ReadPayloadLength(
    const std::array<std::uint8_t, debug::kWireHeaderBytes>& bytes) noexcept
{
    std::size_t offset = 8U;
    std::uint32_t value{};
    (void)support::ReadLittleEndian(bytes, offset, value);
    return value;
}

[[nodiscard]] FrameResult ReadFrame(
    HANDLE pipe,
    HANDLE cancelEvent,
    DWORD timeoutMilliseconds,
    debug::Message& message) noexcept
{
    try {
        std::array<std::uint8_t, debug::kWireHeaderBytes> headerBytes{};
        const PipeIoResult headerRead = ReadPipeExact(
            pipe,
            cancelEvent,
            headerBytes,
            timeoutMilliseconds);
        if (headerRead == PipeIoResult::Cancelled) {
            return FrameResult::Cancelled;
        }
        if (headerRead != PipeIoResult::Succeeded) {
            return FrameResult::Closed;
        }
        debug::MessageHeader header{};
        debug::DecodeError headerError{};
        if (!debug::DecodeHeader(headerBytes, header, headerError)) {
            const std::uint32_t payloadBytes = ReadPayloadLength(headerBytes);
            if (payloadBytes > debug::kMaximumFramePayloadBytes) {
                return FrameResult::Closed;
            }
            std::vector<std::uint8_t> discarded(payloadBytes);
            if (!discarded.empty()
                && ReadPipeExact(
                    pipe,
                    cancelEvent,
                    discarded,
                    timeoutMilliseconds) != PipeIoResult::Succeeded) {
                return FrameResult::Closed;
            }
            return FrameResult::Corrupt;
        }
        message.header = header;
        std::vector<std::uint8_t> frame(
            debug::kWireHeaderBytes
            + static_cast<std::size_t>(header.payloadBytes));
        std::copy(headerBytes.begin(), headerBytes.end(), frame.begin());
        if (header.payloadBytes != 0U) {
            const PipeIoResult payloadRead = ReadPipeExact(
                pipe,
                cancelEvent,
                std::span<std::uint8_t>{frame}.subspan(
                    debug::kWireHeaderBytes),
                timeoutMilliseconds);
            if (payloadRead == PipeIoResult::Cancelled) {
                return FrameResult::Cancelled;
            }
            if (payloadRead != PipeIoResult::Succeeded) {
                return FrameResult::Closed;
            }
        }
        debug::DecodeResult decoded = debug::DecodeMessage(frame);
        if (!decoded.Succeeded()) {
            return FrameResult::Corrupt;
        }
        message = std::move(decoded.message);
        return FrameResult::Message;
    } catch (...) {
        return FrameResult::Closed;
    }
}

[[nodiscard]] bool WriteMessage(
    HANDLE pipe,
    HANDLE cancelEvent,
    const debug::Message& message,
    DWORD timeoutMilliseconds) noexcept
{
    try {
        std::vector<std::uint8_t> frame;
        return debug::EncodeMessage(message, frame)
            && WritePipeExact(
                pipe,
                cancelEvent,
                frame,
                timeoutMilliseconds) == PipeIoResult::Succeeded;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] HANDLE ConnectPipe(
    const std::wstring& pipeName,
    DWORD timeoutMilliseconds) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    do {
        HANDLE pipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0U,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            return pipe;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY) {
            return INVALID_HANDLE_VALUE;
        }
        (void)WaitNamedPipeW(pipeName.c_str(), 25U);
    } while (GetTickCount64() < deadline);
    return INVALID_HANDLE_VALUE;
}

[[nodiscard]] bool ProcessIsRunning(HANDLE process) noexcept
{
    return process != nullptr
        && WaitForSingleObject(process, 0U) == WAIT_TIMEOUT;
}

} // namespace

struct WindowsDebugClient::Impl final {
    explicit Impl(debug::DebugClientCapacities capacities)
        : reducer(capacities)
    {
    }

    void BreakConnection() noexcept
    {
        if (connected.exchange(false, std::memory_order_acq_rel)) {
            try {
                reducer.Disconnected(debug::DebugClientFault::ConnectionLost);
            } catch (...) {
            }
        }
        if (cancelEvent != nullptr) {
            SetEvent(cancelEvent);
        }
        if (pipe != INVALID_HANDLE_VALUE) {
            (void)CancelIoEx(pipe, nullptr);
        }
    }

    [[nodiscard]] debug::DebugClientResult SendCommand(
        debug::MessageKind kind) noexcept
    {
        std::lock_guard lock(writeMutex);
        if (!connected.load(std::memory_order_acquire)
            || pipe == INVALID_HANDLE_VALUE
            || cancelEvent == nullptr) {
            return {debug::DebugClientError::NotConnected};
        }
        debug::Message command{};
        command.header.kind = kind;
        command.header.targetSessionId = targetSessionId;
        if (!WriteMessage(
                pipe,
                cancelEvent,
                command,
                kHandshakeTimeoutMilliseconds)) {
            BreakConnection();
            return {debug::DebugClientError::IoFailure};
        }
        return {};
    }

    void ReaderMain() noexcept
    {
        while (!stopping.load(std::memory_order_acquire)) {
            debug::Message message{};
            const FrameResult result = ReadFrame(
                pipe,
                cancelEvent,
                INFINITE,
                message);
            if (result == FrameResult::Cancelled) {
                break;
            }
            debug::DebugReductionAction action =
                debug::DebugReductionAction::None;
            if (result == FrameResult::Message) {
                try {
                    action = reducer.Accept(message);
                } catch (...) {
                    action = reducer.RejectFrame();
                }
            } else if (result == FrameResult::Corrupt) {
                action = reducer.RejectFrame(
                    message.header.targetSessionId == 0U
                        ? nullptr
                        : &message.header);
            } else {
                BreakConnection();
                break;
            }
            if (action == debug::DebugReductionAction::RestartCapture
                && !SendCommand(debug::MessageKind::StartCapture).Succeeded()) {
                break;
            }
        }
        if (!stopping.load(std::memory_order_acquire)) {
            BreakConnection();
        }
    }

    [[nodiscard]] debug::DebugClientResult Connect(
        debug::ProcessIdentity identity,
        std::string_view token)
    {
        if (identity.processId == 0U) {
            return {debug::DebugClientError::InvalidProcessIdentity};
        }
        if (!IsValidDebugToken(token)) {
            return {debug::DebugClientError::InvalidDebugToken};
        }
        process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
            FALSE,
            identity.processId);
        if (!ProcessIsRunning(process)) {
            if (process != nullptr) {
                CloseHandle(process);
                process = nullptr;
            }
            return {debug::DebugClientError::ProcessUnavailable};
        }
        std::wstring pipeName;
        try {
            pipeName = BuildDebugPipeName(identity.processId, token);
        } catch (...) {
            CloseHandle(process);
            process = nullptr;
            return {debug::DebugClientError::AllocationFailure};
        }
        cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (cancelEvent == nullptr) {
            CloseHandle(process);
            process = nullptr;
            return {debug::DebugClientError::AllocationFailure};
        }
        pipe = ConnectPipe(pipeName, kConnectTimeoutMilliseconds);
        if (pipe == INVALID_HANDLE_VALUE) {
            CloseHandle(cancelEvent);
            cancelEvent = nullptr;
            CloseHandle(process);
            process = nullptr;
            return {debug::DebugClientError::PipeUnavailable};
        }
        ULONG serverProcessId{};
        if (GetNamedPipeServerProcessId(pipe, &serverProcessId) == FALSE
            || serverProcessId != identity.processId
            || !ProcessIsRunning(process)) {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            CloseHandle(cancelEvent);
            cancelEvent = nullptr;
            CloseHandle(process);
            process = nullptr;
            return {debug::DebugClientError::ProcessMismatch};
        }
        debug::Message hello{};
        hello.header.kind = debug::MessageKind::Hello;
        if (!WriteMessage(
                pipe,
                cancelEvent,
                hello,
                kHandshakeTimeoutMilliseconds)) {
            CleanupHandles();
            return {debug::DebugClientError::HandshakeFailed};
        }
        debug::Message accepted{};
        if (ReadFrame(
                pipe,
                cancelEvent,
                kHandshakeTimeoutMilliseconds,
                accepted) != FrameResult::Message) {
            CleanupHandles();
            return {debug::DebugClientError::HandshakeFailed};
        }
        if (accepted.header.kind != debug::MessageKind::HelloAccepted
            || accepted.header.targetSessionId == 0U
            || accepted.header.captureEpoch != 0U
            || accepted.header.protocolSequence != 0U
            || accepted.helloAccepted.selectedVersion != debug::kProtocolVersion
            || accepted.helloAccepted.processId != identity.processId) {
            CleanupHandles();
            return {debug::DebugClientError::ProtocolRejected};
        }
        targetSessionId = accepted.header.targetSessionId;
        stopping.store(false, std::memory_order_release);
        connected.store(true, std::memory_order_release);
        try {
            reducer.Connected(targetSessionId);
        } catch (...) {
            connected.store(false, std::memory_order_release);
            CleanupHandles();
            return {debug::DebugClientError::AllocationFailure};
        }
        try {
            reader = std::thread(&Impl::ReaderMain, this);
        } catch (...) {
            connected.store(false, std::memory_order_release);
            try {
                reducer.Disconnected(debug::DebugClientFault::ConnectionLost);
            } catch (...) {
            }
            CleanupHandles();
            return {debug::DebugClientError::AllocationFailure};
        }
        return {};
    }

    void CleanupHandles() noexcept
    {
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
        }
        if (cancelEvent != nullptr) {
            CloseHandle(cancelEvent);
            cancelEvent = nullptr;
        }
        if (process != nullptr) {
            CloseHandle(process);
            process = nullptr;
        }
        targetSessionId = 0U;
    }

    void Disconnect() noexcept
    {
        stopping.store(true, std::memory_order_release);
        connected.store(false, std::memory_order_release);
        if (cancelEvent != nullptr) {
            SetEvent(cancelEvent);
        }
        if (pipe != INVALID_HANDLE_VALUE) {
            (void)CancelIoEx(pipe, nullptr);
        }
        if (reader.joinable()) {
            reader.join();
        }
        {
            std::lock_guard lock(writeMutex);
            CleanupHandles();
        }
        try {
            reducer.Disconnected();
        } catch (...) {
        }
    }

    mutable std::mutex lifecycleMutex;
    std::mutex writeMutex;
    debug::DebugStateReducer reducer;
    HANDLE pipe{INVALID_HANDLE_VALUE};
    HANDLE cancelEvent{};
    HANDLE process{};
    std::uint64_t targetSessionId{};
    std::atomic<bool> connected{false};
    std::atomic<bool> stopping{true};
    std::thread reader;
};

WindowsDebugClient::WindowsDebugClient(debug::DebugClientCapacities capacities)
    : impl_(std::make_unique<Impl>(capacities))
{
}

WindowsDebugClient::~WindowsDebugClient()
{
    Disconnect();
}

debug::DebugClientResult WindowsDebugClient::Connect(
    debug::ProcessIdentity process,
    std::string_view debugToken)
{
    std::lock_guard lock(impl_->lifecycleMutex);
    impl_->Disconnect();
    return impl_->Connect(process, debugToken);
}

debug::DebugClientResult WindowsDebugClient::StartCapture()
{
    try {
        impl_->reducer.CaptureRequested();
    } catch (...) {
        return {debug::DebugClientError::AllocationFailure};
    }
    return impl_->SendCommand(debug::MessageKind::StartCapture);
}

debug::DebugClientResult WindowsDebugClient::StopCapture()
{
    try {
        impl_->reducer.CaptureStopped();
    } catch (...) {
        return {debug::DebugClientError::AllocationFailure};
    }
    return impl_->SendCommand(debug::MessageKind::StopCapture);
}

debug::DebugClientResult WindowsDebugClient::RequestExecutorStop()
{
    return impl_->SendCommand(debug::MessageKind::RequestExecutorStop);
}

std::shared_ptr<const debug::DebugClientState> WindowsDebugClient::ReadState()
    const
{
    return impl_->reducer.ReadState();
}

void WindowsDebugClient::Disconnect() noexcept
{
    std::lock_guard lock(impl_->lifecycleMutex);
    impl_->Disconnect();
}

} // namespace inputweaver::win32
