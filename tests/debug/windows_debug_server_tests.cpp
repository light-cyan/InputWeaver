#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "debug/debug_protocol.hpp"
#include "platform/windows/debug/debug_server.hpp"
#include "program/compiled_program.hpp"
#include "../program/compiled_program_fixtures.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

[[nodiscard]] bool WriteExact(
    HANDLE pipe,
    std::span<const std::uint8_t> bytes) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        DWORD written{};
        const DWORD requested = static_cast<DWORD>(bytes.size() - offset);
        if (WriteFile(
                pipe,
                bytes.data() + offset,
                requested,
                &written,
                nullptr) == FALSE
            || written == 0U) {
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
        DWORD read{};
        const DWORD requested = static_cast<DWORD>(bytes.size() - offset);
        if (ReadFile(
                pipe,
                bytes.data() + offset,
                requested,
                &read,
                nullptr) == FALSE
            || read == 0U) {
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

[[nodiscard]] bool WaitForAvailableBytes(
    HANDLE pipe,
    DWORD required,
    DWORD timeoutMilliseconds) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    do {
        DWORD available{};
        if (PeekNamedPipe(
                pipe,
                nullptr,
                0U,
                nullptr,
                &available,
                nullptr) == FALSE) {
            return false;
        }
        if (available >= required) {
            return true;
        }
        Sleep(1U);
    } while (GetTickCount64() < deadline);
    return false;
}

[[nodiscard]] inputweaver::debug::DecodeResult Receive(HANDLE pipe)
{
    inputweaver::debug::DecodeResult result{};
    if (!WaitForAvailableBytes(
            pipe,
            static_cast<DWORD>(inputweaver::debug::kWireHeaderBytes),
            5000U)) {
        result.error = inputweaver::debug::DecodeError::Truncated;
        return result;
    }
    std::array<std::uint8_t, inputweaver::debug::kWireHeaderBytes> header{};
    if (!ReadExact(pipe, header)) {
        result.error = inputweaver::debug::DecodeError::Truncated;
        return result;
    }
    inputweaver::debug::MessageHeader decodedHeader{};
    inputweaver::debug::DecodeError error{};
    if (!inputweaver::debug::DecodeHeader(header, decodedHeader, error)) {
        result.error = error;
        return result;
    }
    std::vector<std::uint8_t> frame(
        inputweaver::debug::kWireHeaderBytes
        + static_cast<std::size_t>(decodedHeader.payloadBytes));
    std::copy(header.begin(), header.end(), frame.begin());
    if (decodedHeader.payloadBytes != 0U
        && !ReadExact(
            pipe,
            std::span<std::uint8_t>{frame}.subspan(
                inputweaver::debug::kWireHeaderBytes))) {
        result.error = inputweaver::debug::DecodeError::Truncated;
        return result;
    }
    return inputweaver::debug::DecodeMessage(frame);
}

[[nodiscard]] HANDLE Connect(std::wstring_view pipeName) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 5000U;
    do {
        HANDLE pipe = CreateFileW(
            std::wstring(pipeName).c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0U,
            nullptr,
            OPEN_EXISTING,
            0U,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            return pipe;
        }
        Sleep(2U);
    } while (GetTickCount64() < deadline);
    return INVALID_HANDLE_VALUE;
}

struct CallbackState final {
    std::atomic<bool> wakeRequested{false};
    std::atomic<bool> stopRequested{false};
};

void Wake(void* context) noexcept
{
    static_cast<CallbackState*>(context)->wakeRequested.store(
        true,
        std::memory_order_release);
}

void Stop(void* context) noexcept
{
    static_cast<CallbackState*>(context)->stopRequested.store(
        true,
        std::memory_order_release);
}

[[nodiscard]] inputweaver::win32::DebugCaptureRequest WaitForRequest(
    inputweaver::win32::WindowsDebugServer& server)
{
    const ULONGLONG deadline = GetTickCount64() + 5000U;
    do {
        const auto request = server.TakeCaptureRequest();
        if (request != inputweaver::win32::DebugCaptureRequest::None) {
            return request;
        }
        Sleep(1U);
    } while (GetTickCount64() < deadline);
    return inputweaver::win32::DebugCaptureRequest::None;
}

void TestPipeSession()
{
    inputweaver::FinalizeResult finalized = inputweaver::FinalizeCompiledProgram(
        inputweaver::test::MakeTapFixtureStorage());
    Check(
        finalized.program != nullptr && finalized.errors.empty(),
        "debug server fixture finalizes");
    if (finalized.program == nullptr) {
        return;
    }
    inputweaver::win32::WindowsDebugServer invalidServer;
    std::wstring invalidError;
    Check(
        !invalidServer.Start(
            L"bad\\token",
            finalized.program,
            {},
            invalidError),
        "unsafe pipe token is rejected");
    invalidServer.Stop();

    CallbackState callbacks;
    inputweaver::win32::WindowsDebugServer server;
    const std::wstring token = L"test-"
        + std::to_wstring(GetCurrentProcessId())
        + L"-"
        + std::to_wstring(GetTickCount64());
    std::wstring error;
    Check(
        server.Start(
            token,
            finalized.program,
            {&callbacks, &Wake, &Stop},
            error),
        "debug server starts");
    if (!error.empty()) {
        std::wcerr << L"Debug server start error: " << error << L'\n';
    }
    HANDLE pipe = Connect(inputweaver::win32::MakeDebugPipeName(
        GetCurrentProcessId(),
        token));
    Check(pipe != INVALID_HANDLE_VALUE, "same-user local client connects");
    if (pipe == INVALID_HANDLE_VALUE) {
        server.Stop();
        return;
    }

    inputweaver::debug::Message hello{};
    hello.header.kind = inputweaver::debug::MessageKind::Hello;
    Check(Send(pipe, hello), "client sends hello");
    const auto accepted = Receive(pipe);
    Check(
        accepted.Succeeded()
            && accepted.message.header.kind
                == inputweaver::debug::MessageKind::HelloAccepted
            && accepted.message.helloAccepted.processId
                == GetCurrentProcessId(),
        "server accepts protocol and identifies executor");
    const std::uint64_t sessionId = accepted.message.header.targetSessionId;

    inputweaver::debug::Message start{};
    start.header.kind = inputweaver::debug::MessageKind::StartCapture;
    start.header.targetSessionId = sessionId;
    Check(Send(pipe, start), "client requests capture");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Start,
        "capture command reaches input-thread boundary");

    inputweaver::debug::InputEventPayload initial{};
    initial.device = inputweaver::DeviceKind::Keyboard;
    initial.virtualKey = 65U;
    initial.scanCode = 30U;
    initial.hasCompiledControl = true;
    initial.compiledIdentity = finalized.program->Controls()[1U];
    Check(
        server.BeginCapture(std::span<const inputweaver::debug::InputEventPayload>{
            &initial,
            1U}),
        "input thread starts capture with initial state");

    const auto correlation = server.BeginInput();
    inputweaver::debug::InputEventPayload input{};
    input.device = inputweaver::DeviceKind::Keyboard;
    input.transition = inputweaver::Transition::Down;
    input.origin = inputweaver::InputOrigin::PhysicalCandidate;
    input.disposition = inputweaver::debug::InputDisposition::Suppress;
    input.virtualKey = 66U;

    inputweaver::RuntimeDebugEvent matched{};
    matched.kind = inputweaver::RuntimeDebugEventKind::RuleMatched;
    matched.captureEpoch = correlation.captureEpoch;
    matched.executionMarker = 27U;
    matched.triggerInputSequence = correlation.inputSequence;
    matched.eventKey = finalized.program->EventBuckets()[0U].key;
    matched.ruleIndex = 0U;
    Check(server.Publish(matched), "rule match enters event stream");
    inputweaver::RuntimeDebugEvent action{};
    action.kind = inputweaver::RuntimeDebugEventKind::ActionStarted;
    action.captureEpoch = correlation.captureEpoch;
    action.executionMarker = 27U;
    action.instructionIndex = 0U;
    Check(server.Publish(action), "action start enters event stream");
    inputweaver::RuntimeDebugEvent ended{};
    ended.kind = inputweaver::RuntimeDebugEventKind::ExecutionEnded;
    ended.captureEpoch = correlation.captureEpoch;
    ended.executionMarker = 27U;
    ended.result = inputweaver::RuntimeExecutionResult::Completed;
    Check(server.Publish(ended), "execution end enters event stream");
    Check(
        correlation.Active() && server.PublishInput(correlation, input),
        "ordinary input enters event stream");

    const std::array<inputweaver::debug::MessageKind, 6U> expected = {
        inputweaver::debug::MessageKind::CaptureStarted,
        inputweaver::debug::MessageKind::InputEvent,
        inputweaver::debug::MessageKind::RuleMatched,
        inputweaver::debug::MessageKind::ActionStarted,
        inputweaver::debug::MessageKind::ExecutionEnded,
        inputweaver::debug::MessageKind::InputEvent};
    std::uint64_t previousSequence{};
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto received = Receive(pipe);
        Check(
            received.Succeeded()
                && received.message.header.kind == expected[index]
                && received.message.header.protocolSequence
                    == previousSequence + 1U,
            "producer stream preserves queue order");
        if (received.Succeeded()) {
            previousSequence = received.message.header.protocolSequence;
            if (index == 1U) {
                Check(
                    received.message.inputEvent.origin
                            == inputweaver::InputOrigin::InitialSample
                        && received.message.inputEvent.disposition
                            == inputweaver::debug::InputDisposition::NotApplicable,
                    "initial held key is represented as INIT Down");
            }
            if (index == 2U) {
                Check(
                    received.message.ruleMatched.actionInstructions.size() == 2U,
                    "pipe worker expands the complete compiled action program");
            }
        }
    }

    bool overflowed = false;
    for (std::size_t index = 0U; index < 10'000U; ++index) {
        const auto floodCorrelation = server.BeginInput();
        if (!floodCorrelation.Active()
            || !server.PublishInput(floodCorrelation, input)) {
            overflowed = true;
            break;
        }
    }
    Check(
        overflowed && !server.BeginInput().Active(),
        "queue overflow invalidates the current capture without blocking input");
    bool receivedOverflow = false;
    for (std::size_t index = 0U; index < 10'000U; ++index) {
        const auto received = Receive(pipe);
        if (!received.Succeeded()) {
            break;
        }
        if (received.message.header.kind
                == inputweaver::debug::MessageKind::RuntimeIssue
            && received.message.runtimeIssue.code
                == inputweaver::debug::IssueCode::DebugStreamOverflow) {
            receivedOverflow = true;
            break;
        }
    }
    Check(receivedOverflow, "overflow is reported as a stream-loss issue");

    callbacks.wakeRequested.store(false, std::memory_order_release);
    Check(Send(pipe, start), "client requests a fresh capture after overflow");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Start,
        "fresh capture command reaches the input thread");
    Check(server.BeginCapture({}), "fresh capture starts without a snapshot");
    const auto restarted = Receive(pipe);
    Check(
        restarted.Succeeded()
            && restarted.message.header.kind
                == inputweaver::debug::MessageKind::CaptureStarted
            && restarted.message.header.captureEpoch
                > correlation.captureEpoch,
        "capture restarts with a new epoch");

    inputweaver::debug::Message stopCapture{};
    stopCapture.header.kind = inputweaver::debug::MessageKind::StopCapture;
    stopCapture.header.targetSessionId = sessionId;
    Check(Send(pipe, stopCapture), "client requests capture stop");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Stop,
        "capture stop reaches the input thread");
    server.EndCapture();
    Check(!server.BeginInput().Active(), "capture stop disables event production");

    inputweaver::debug::Message requestStop{};
    requestStop.header.kind =
        inputweaver::debug::MessageKind::RequestExecutorStop;
    requestStop.header.targetSessionId = sessionId;
    Check(Send(pipe, requestStop), "client requests executor stop");
    const ULONGLONG stopDeadline = GetTickCount64() + 5000U;
    while (!callbacks.stopRequested.load(std::memory_order_acquire)
        && GetTickCount64() < stopDeadline) {
        Sleep(1U);
    }
    Check(
        callbacks.stopRequested.load(std::memory_order_acquire),
        "executor stop callback is invoked");

    CloseHandle(pipe);
    server.Stop();
}

} // namespace

int main()
{
    TestPipeSession();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " Windows debug server test(s) failed.\n";
        return 1;
    }
    std::cout << "Windows debug server tests passed.\n";
    return 0;
}
