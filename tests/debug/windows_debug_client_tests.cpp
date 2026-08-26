#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "debug/debug_protocol.hpp"
#include "platform/windows/debug/debug_client.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int gFailureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++gFailureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

[[nodiscard]] std::wstring MakePipeName(std::wstring_view token)
{
    return L"\\\\.\\pipe\\InputWeaver.Debug."
        + std::to_wstring(GetCurrentProcessId())
        + L"."
        + std::wstring(token);
}

[[nodiscard]] bool WriteExact(
    HANDLE pipe,
    std::span<const std::uint8_t> bytes) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD written{};
        const BOOL started = WriteFile(
            pipe,
            bytes.data() + offset,
            static_cast<DWORD>(bytes.size() - offset),
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
        CloseHandle(event);
        if (!completed || written == 0U) {
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] bool ReadExact(
    HANDLE pipe,
    std::span<std::uint8_t> bytes) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD read{};
        const BOOL started = ReadFile(
            pipe,
            bytes.data() + offset,
            static_cast<DWORD>(bytes.size() - offset),
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
        CloseHandle(event);
        if (!completed || read == 0U) {
            return false;
        }
        offset += read;
    }
    return true;
}

[[nodiscard]] bool Send(
    HANDLE pipe,
    const inputweaver::debug::Message& message)
{
    std::vector<std::uint8_t> frame;
    return inputweaver::debug::EncodeMessage(message, frame)
        && WriteExact(pipe, frame);
}

[[nodiscard]] bool Receive(
    HANDLE pipe,
    inputweaver::debug::Message& message)
{
    std::array<std::uint8_t, inputweaver::debug::kWireHeaderBytes> headerBytes{};
    if (!ReadExact(pipe, headerBytes)) {
        return false;
    }
    inputweaver::debug::MessageHeader header{};
    inputweaver::debug::DecodeError error{};
    if (!inputweaver::debug::DecodeHeader(headerBytes, header, error)) {
        return false;
    }
    std::vector<std::uint8_t> frame(
        inputweaver::debug::kWireHeaderBytes
        + static_cast<std::size_t>(header.payloadBytes));
    std::copy(headerBytes.begin(), headerBytes.end(), frame.begin());
    if (header.payloadBytes != 0U
        && !ReadExact(
            pipe,
            std::span<std::uint8_t>{frame}.subspan(
                inputweaver::debug::kWireHeaderBytes))) {
        return false;
    }
    auto decoded = inputweaver::debug::DecodeMessage(frame);
    if (!decoded.Succeeded()) {
        return false;
    }
    message = std::move(decoded.message);
    return true;
}

class FakeDebugServer final {
public:
    explicit FakeDebugServer(std::wstring token)
        : token_(std::move(token))
    {
    }

    ~FakeDebugServer()
    {
        Stop();
    }

    [[nodiscard]] bool Start()
    {
        try {
            thread_ = std::thread(&FakeDebugServer::Run, this);
        } catch (...) {
            return false;
        }
        const ULONGLONG deadline = GetTickCount64() + 5'000U;
        while (!ready_.load(std::memory_order_acquire)
            && !failed_.load(std::memory_order_acquire)
            && GetTickCount64() < deadline) {
            Sleep(1U);
        }
        return ready_.load(std::memory_order_acquire)
            && !failed_.load(std::memory_order_acquire);
    }

    void Stop() noexcept
    {
        stopping_.store(true, std::memory_order_release);
        if (thread_.joinable()) {
            (void)CancelSynchronousIo(thread_.native_handle());
            const HANDLE pipe = pipe_.load(std::memory_order_acquire);
            if (pipe != INVALID_HANDLE_VALUE) {
                (void)CancelIoEx(pipe, nullptr);
                DisconnectNamedPipe(pipe);
            }
            thread_.join();
        }
    }

    [[nodiscard]] bool SendCorruptInput()
    {
        inputweaver::debug::Message input{};
        input.header.kind = inputweaver::debug::MessageKind::InputEvent;
        input.header.targetSessionId = kSessionId;
        input.header.captureEpoch = 1U;
        input.header.protocolSequence = 3U;
        input.inputEvent.inputSequence = 2U;
        input.inputEvent.device = inputweaver::DeviceKind::Keyboard;
        input.inputEvent.transition = inputweaver::Transition::Down;
        input.inputEvent.origin = inputweaver::InputOrigin::PhysicalCandidate;
        input.inputEvent.disposition =
            inputweaver::debug::InputDisposition::Forward;
        input.inputEvent.virtualKey = 66U;
        std::vector<std::uint8_t> frame;
        if (!inputweaver::debug::EncodeMessage(input, frame)) {
            return false;
        }
        const std::size_t originOffset =
            inputweaver::debug::kWireHeaderBytes + 10U;
        if (originOffset >= frame.size()) {
            return false;
        }
        frame[originOffset] = 0xffU;
        std::lock_guard lock(writeMutex_);
        const HANDLE pipe = pipe_.load(std::memory_order_acquire);
        return pipe != INVALID_HANDLE_VALUE && WriteExact(pipe, frame);
    }

    [[nodiscard]] std::uint32_t StartCount() const noexcept
    {
        return startCount_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool StopCaptureReceived() const noexcept
    {
        return stopCaptureReceived_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool ExecutorStopReceived() const noexcept
    {
        return executorStopReceived_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] bool SendCapture(
        HANDLE pipe,
        std::uint64_t epoch,
        std::uint64_t protocolSequence)
    {
        inputweaver::debug::Message started{};
        started.header.kind = inputweaver::debug::MessageKind::CaptureStarted;
        started.header.targetSessionId = kSessionId;
        started.header.captureEpoch = epoch;
        started.header.protocolSequence = protocolSequence;
        started.header.captureTimeNanoseconds = 1'000'000;
        started.captureStarted.captureUnixTimeMilliseconds =
            1'725'000'000'000LL;
        std::lock_guard lock(writeMutex_);
        if (!Send(pipe, started)) {
            return false;
        }
        if (epoch != 1U) {
            return true;
        }
        inputweaver::debug::Message input{};
        input.header.kind = inputweaver::debug::MessageKind::InputEvent;
        input.header.targetSessionId = kSessionId;
        input.header.captureEpoch = epoch;
        input.header.protocolSequence = protocolSequence + 1U;
        input.header.captureTimeNanoseconds = 2'000'000;
        input.inputEvent.inputSequence = 1U;
        input.inputEvent.device = inputweaver::DeviceKind::Keyboard;
        input.inputEvent.transition = inputweaver::Transition::Down;
        input.inputEvent.origin = inputweaver::InputOrigin::InitialSample;
        input.inputEvent.disposition =
            inputweaver::debug::InputDisposition::NotApplicable;
        input.inputEvent.virtualKey = 65U;
        return Send(pipe, input);
    }

    void Run() noexcept
    {
        const std::wstring pipeName = MakePipeName(token_);
        HANDLE pipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
                | PIPE_REJECT_REMOTE_CLIENTS,
            1U,
            64U * 1024U,
            64U * 1024U,
            0U,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            failed_.store(true, std::memory_order_release);
            return;
        }
        pipe_.store(pipe, std::memory_order_release);
        ready_.store(true, std::memory_order_release);
        const HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (connectEvent == nullptr) {
            failed_.store(true, std::memory_order_release);
            CloseHandle(pipe);
            pipe_.store(INVALID_HANDLE_VALUE, std::memory_order_release);
            return;
        }
        OVERLAPPED connectOverlapped{};
        connectOverlapped.hEvent = connectEvent;
        const BOOL connected = ConnectNamedPipe(pipe, &connectOverlapped);
        bool connectionReady = connected != FALSE;
        const DWORD connectError = connectionReady ? ERROR_SUCCESS : GetLastError();
        if (!connectionReady && connectError == ERROR_PIPE_CONNECTED) {
            connectionReady = true;
        } else if (!connectionReady && connectError == ERROR_IO_PENDING) {
            DWORD transferred{};
            connectionReady = WaitForSingleObject(connectEvent, INFINITE)
                    == WAIT_OBJECT_0
                && GetOverlappedResult(
                    pipe,
                    &connectOverlapped,
                    &transferred,
                    FALSE) != FALSE;
        }
        CloseHandle(connectEvent);
        if (!connectionReady) {
            if (!stopping_.load(std::memory_order_acquire)) {
                failed_.store(true, std::memory_order_release);
            }
            CloseHandle(pipe);
            pipe_.store(INVALID_HANDLE_VALUE, std::memory_order_release);
            return;
        }
        inputweaver::debug::Message hello{};
        if (!Receive(pipe, hello)
            || hello.header.kind != inputweaver::debug::MessageKind::Hello) {
            failed_.store(true, std::memory_order_release);
        } else {
            inputweaver::debug::Message accepted{};
            accepted.header.kind =
                inputweaver::debug::MessageKind::HelloAccepted;
            accepted.header.targetSessionId = kSessionId;
            accepted.header.protocolSequence = 0U;
            accepted.helloAccepted.selectedVersion =
                inputweaver::debug::kProtocolVersion;
            accepted.helloAccepted.processId = GetCurrentProcessId();
            {
                std::lock_guard lock(writeMutex_);
                if (!Send(pipe, accepted)) {
                    failed_.store(true, std::memory_order_release);
                }
            }
        }
        while (!stopping_.load(std::memory_order_acquire)
            && !failed_.load(std::memory_order_acquire)) {
            inputweaver::debug::Message command{};
            if (!Receive(pipe, command)) {
                break;
            }
            if (command.header.targetSessionId != kSessionId) {
                failed_.store(true, std::memory_order_release);
                break;
            }
            if (command.header.kind
                == inputweaver::debug::MessageKind::StartCapture) {
                const std::uint32_t count = startCount_.fetch_add(
                    1U,
                    std::memory_order_acq_rel) + 1U;
                const std::uint64_t epoch = count == 1U ? 1U : 2U;
                const std::uint64_t sequence = count == 1U ? 1U : 4U;
                if (!SendCapture(pipe, epoch, sequence)) {
                    break;
                }
            } else if (command.header.kind
                == inputweaver::debug::MessageKind::StopCapture) {
                stopCaptureReceived_.store(true, std::memory_order_release);
            } else if (command.header.kind
                == inputweaver::debug::MessageKind::RequestExecutorStop) {
                executorStopReceived_.store(true, std::memory_order_release);
            } else {
                failed_.store(true, std::memory_order_release);
                break;
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        pipe_.store(INVALID_HANDLE_VALUE, std::memory_order_release);
    }

    inline static constexpr std::uint64_t kSessionId = 7001U;
    std::wstring token_;
    std::mutex writeMutex_;
    std::atomic<HANDLE> pipe_{INVALID_HANDLE_VALUE};
    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint32_t> startCount_{0U};
    std::atomic<bool> stopCaptureReceived_{false};
    std::atomic<bool> executorStopReceived_{false};
    std::thread thread_;
};

template <typename Predicate>
[[nodiscard]] bool WaitUntil(Predicate predicate, DWORD timeoutMilliseconds)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    do {
        if (predicate()) {
            return true;
        }
        Sleep(1U);
    } while (GetTickCount64() < deadline);
    return predicate();
}

void TestFakeServerSession()
{
    const std::wstring token = L"client-test-"
        + std::to_wstring(GetCurrentProcessId())
        + L"-"
        + std::to_wstring(GetTickCount64());
    FakeDebugServer server(token);
    Check(server.Start(), "fake debug server starts");
    inputweaver::win32::WindowsDebugClient client;
    const std::string narrowToken(token.begin(), token.end());
    Check(
        client.Connect({GetCurrentProcessId()}, narrowToken).Succeeded(),
        "client validates process and completes handshake");
    Check(client.StartCapture().Succeeded(), "client sends StartCapture");
    Check(
        WaitUntil(
            [&] {
                const auto state = client.ReadState();
                return state->capturing && state->captureTrusted
                    && state->captureEpoch == 1U
                    && state->pressedControls.size() == 1U
                    && state->recentInputEvents.size() == 1U
                    && state->recentInputEvents[0]
                            .captureUnixTimeMilliseconds
                        == 1'725'000'000'001LL;
            },
            5'000U),
        "client reads CaptureStarted and INIT from fake server");
    Check(server.SendCorruptInput(), "fake server sends a corrupt frame");
    Check(
        WaitUntil([&] { return server.StartCount() >= 2U; }, 5'000U),
        "client restarts capture after frame corruption");
    Check(
        WaitUntil(
            [&] {
                const auto state = client.ReadState();
                return state->capturing && state->captureTrusted
                    && state->captureEpoch == 2U
                    && state->pressedControls.empty();
            },
            5'000U),
        "fresh CaptureStarted restores trusted derived state");
    Check(client.StopCapture().Succeeded(), "client sends StopCapture");
    Check(
        WaitUntil([&] { return server.StopCaptureReceived(); }, 5'000U),
        "fake server receives StopCapture");
    Check(
        client.RequestExecutorStop().Succeeded(),
        "client sends RequestExecutorStop");
    Check(
        WaitUntil([&] { return server.ExecutorStopReceived(); }, 5'000U),
        "fake server receives RequestExecutorStop");
    const ULONGLONG disconnectStart = GetTickCount64();
    client.Disconnect();
    Check(
        GetTickCount64() - disconnectStart < 2'000U
            && !client.ReadState()->connected,
        "Disconnect cancels the pending pipe read");
    server.Stop();

    const std::wstring reconnectToken = token + L"-reconnect";
    FakeDebugServer reconnectServer(reconnectToken);
    Check(reconnectServer.Start(), "second fake server starts");
    const std::string narrowReconnect(
        reconnectToken.begin(),
        reconnectToken.end());
    Check(
        client.Connect({GetCurrentProcessId()}, narrowReconnect).Succeeded(),
        "client reconnects after disconnect");
    client.Disconnect();
    reconnectServer.Stop();
}

void TestValidationFailures()
{
    inputweaver::win32::WindowsDebugClient client;
    Check(
        client.Connect({0U}, "token").error
            == inputweaver::debug::DebugClientError::InvalidProcessIdentity,
        "zero process identity is rejected");
    Check(
        client.Connect({GetCurrentProcessId()}, "bad\\token").error
            == inputweaver::debug::DebugClientError::InvalidDebugToken,
        "unsafe debug token is rejected");
    Check(
        client.StartCapture().error
            == inputweaver::debug::DebugClientError::NotConnected,
        "capture command requires a connection");
}

} // namespace

int main()
{
    TestFakeServerSession();
    TestValidationFailures();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " Windows debug client test(s) failed.\n";
        return 1;
    }
    std::cout << "Windows debug client tests passed.\n";
    return 0;
}
