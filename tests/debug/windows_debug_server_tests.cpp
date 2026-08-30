#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "debug/debug_protocol.hpp"
#include "platform/windows/debug/debug_client.hpp"
#include "platform/windows/debug/debug_server.hpp"
#include "program/compiled_program.hpp"
#include "program/program_validator.hpp"
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

template <typename Predicate>
[[nodiscard]] bool WaitUntil(Predicate predicate)
{
    const ULONGLONG deadline = GetTickCount64() + 5000U;
    do {
        if (predicate()) {
            return true;
        }
        Sleep(1U);
    } while (GetTickCount64() < deadline);
    return predicate();
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeReadableTapStorage()
{
    inputweaver::CompiledProgramStorage storage =
        inputweaver::test::MakeTapFixtureStorage();
    storage.strings.push_back("tap(F7)");
    storage.debugInfo.rules.push_back({
        0U,
        inputweaver::StringId{},
        inputweaver::StringId{1U}});
    return storage;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeReadableMappingStorage()
{
    inputweaver::CompiledProgramStorage storage =
        inputweaver::test::MakeMappingFixtureStorage();
    storage.strings.push_back("F6 -> F7;");
    storage.strings.push_back("combat");
    storage.strings.push_back("flags");
    storage.strings.push_back("samples");
    storage.userValues.initialStates.push_back(0U);
    storage.valueRefs.push_back({
        inputweaver::ValueDomain::UserState,
        inputweaver::ValueType::State,
        0U});
    storage.debugInfo.variables.push_back({
        inputweaver::StringId{2U},
        inputweaver::ValueRefId{0U},
        {0U, 1U}});
    storage.arrays = {
        {inputweaver::ArrayElementType::State, {0U, 3U}},
        {inputweaver::ArrayElementType::Number, {0U, 10U}},
    };
    storage.initialArrayStates = {0U, 1U, 0U};
    storage.initialArrayNumbers = {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    storage.debugInfo.arrays = {
        {inputweaver::StringId{3U}, inputweaver::ArrayId{0U}, {1U, 1U}},
        {inputweaver::StringId{4U}, inputweaver::ArrayId{1U}, {2U, 1U}},
    };
    storage.debugInfo.rules.push_back({
        0U,
        inputweaver::StringId{},
        inputweaver::StringId{1U}});
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    return storage;
}

void TestPipeSession()
{
    inputweaver::FinalizeResult finalized = inputweaver::FinalizeCompiledProgram(
        MakeReadableTapStorage());
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
            {{&callbacks, &Wake}, {&callbacks, &Stop}},
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
    matched.ruleIndex = 0U;
    Check(server.Publish(matched), "rule match enters event stream");
    inputweaver::RuntimeDebugEvent ended{};
    ended.kind = inputweaver::RuntimeDebugEventKind::ExecutionEnded;
    ended.captureEpoch = correlation.captureEpoch;
    ended.executionMarker = 27U;
    ended.result = inputweaver::RuntimeExecutionResult::Completed;
    Check(server.Publish(ended), "execution end enters event stream");
    Check(
        correlation.Active() && server.PublishInput(correlation, input),
        "ordinary input enters event stream");

    const std::array<inputweaver::debug::MessageKind, 5U> expected = {
        inputweaver::debug::MessageKind::CaptureStarted,
        inputweaver::debug::MessageKind::InputEvent,
        inputweaver::debug::MessageKind::RuleMatched,
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
            if (index == 0U) {
                Check(
                    received.message.captureStarted
                            .captureUnixTimeMilliseconds > 0
                        && received.message.captureStarted.values.size() == 1U
                        && received.message.captureStarted.values[0].name
                            == "PAUSE"
                        && received.message.captureStarted.values[0].value
                            .stateValue,
                    "capture start includes wall-clock and PAUSE snapshots");
            }
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
                    received.message.ruleMatched.conditionText == "always"
                        && received.message.ruleMatched.actionText == "tap(F7)",
                    "pipe worker expands readable condition and action data");
            }
            if (index == 3U) {
                Check(
                    received.message.header.captureTimeNanoseconds == 0,
                    "execution end does not sample or publish an end time");
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

void TestDebugClientIntegration()
{
    inputweaver::FinalizeResult finalized = inputweaver::FinalizeCompiledProgram(
        MakeReadableMappingStorage());
    Check(
        finalized.program != nullptr && finalized.errors.empty(),
        "debug client integration fixture finalizes");
    if (finalized.program == nullptr) {
        return;
    }
    CallbackState callbacks;
    inputweaver::win32::WindowsDebugServer server;
    const std::wstring token = L"integration-"
        + std::to_wstring(GetCurrentProcessId())
        + L"-"
        + std::to_wstring(GetTickCount64());
    std::wstring error;
    Check(
        server.Start(
            token,
            finalized.program,
            {{&callbacks, &Wake}, {&callbacks, &Stop}},
            error),
        "debug server starts for DebugClient integration");
    if (!error.empty()) {
        std::wcerr << L"Debug client integration error: " << error << L'\n';
    }

    inputweaver::win32::WindowsDebugClient client;
    const std::string narrowToken(token.begin(), token.end());
    Check(
        client.Connect({GetCurrentProcessId()}, narrowToken).Succeeded(),
        "DebugClient connects to WindowsDebugServer");
    Check(client.StartCapture().Succeeded(), "DebugClient starts capture");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Start,
        "DebugClient StartCapture reaches WindowsDebugServer");

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
        "WindowsDebugServer publishes the integration capture boundary");
    Check(
        WaitUntil([&] {
            const auto state = client.ReadState();
            return state->capturing && state->captureTrusted
                && state->pressedControls.size() == 1U
                && state->pressedControls[0].origin
                    == inputweaver::InputOrigin::InitialSample
                && state->values.size() == 2U
                && state->values[0].name == "PAUSE"
                && state->values[0].value.stateValue
                && state->values[1].name == "combat"
                && !state->values[1].value.stateValue
                && state->arrays.size() == 2U
                && state->arrays[0].name == "flags"
                && state->arrays[0].value.length == 3U
                && state->arrays[0].value.prefixCount == 3U
                && state->arrays[0].value.suffixCount == 0U
                && !state->arrays[0].value.elements[0].stateValue
                && state->arrays[0].value.elements[1].stateValue
                && state->arrays[1].name == "samples"
                && state->arrays[1].value.length == 10U
                && state->arrays[1].value.prefixCount == 4U
                && state->arrays[1].value.suffixCount == 4U
                && state->arrays[1].value.elements[7].numberValue == 9.0;
        }),
        "DebugClient derives scalar and bounded array INIT state from the server");

    inputweaver::RuntimeDebugEvent changed{};
    changed.kind = inputweaver::RuntimeDebugEventKind::StateChanged;
    changed.value.type = inputweaver::ValueType::State;
    changed.value.stateValue = false;
    Check(
        server.Publish(changed),
        "integration PAUSE update is published");
    Check(
        WaitUntil([&] {
            const auto state = client.ReadState();
            return state->values.size() == 2U
                && !state->values[0].value.stateValue;
        }),
        "DebugClient applies the PAUSE value update");

    changed.value.reference = inputweaver::ValueRefId{0U};
    changed.value.stateValue = true;
    Check(
        server.Publish(changed),
        "integration user state update is published");
    Check(
        WaitUntil([&] {
            const auto state = client.ReadState();
            return state->values.size() == 2U
                && state->values[1].value.stateValue;
        }),
        "DebugClient applies the user state value update");

    inputweaver::RuntimeDebugEvent arrayChanged{};
    arrayChanged.kind = inputweaver::RuntimeDebugEventKind::ArrayChanged;
    arrayChanged.array.array = inputweaver::ArrayId{0U};
    arrayChanged.array.elementType = inputweaver::ArrayElementType::State;
    arrayChanged.array.length = 3U;
    arrayChanged.array.prefixCount = 3U;
    arrayChanged.array.elements[0].stateValue = 1U;
    arrayChanged.array.elements[1].stateValue = 0U;
    arrayChanged.array.elements[2].stateValue = 1U;
    Check(server.Publish(arrayChanged),
        "integration array update is published");
    Check(
        WaitUntil([&] {
            const auto state = client.ReadState();
            return state->arrays.size() == 2U
                && state->arrays[0].value.length == 3U
                && state->arrays[0].value.elements[0].stateValue
                && !state->arrays[0].value.elements[1].stateValue
                && state->arrays[0].value.elements[2].stateValue;
        }),
        "DebugClient applies an array value update");

    const auto correlation = server.BeginInput();
    inputweaver::RuntimeDebugEvent matched{};
    matched.kind = inputweaver::RuntimeDebugEventKind::RuleMatched;
    matched.captureEpoch = correlation.captureEpoch;
    matched.executionMarker = 88U;
    matched.triggerInputSequence = correlation.inputSequence;
    matched.ruleIndex = 0U;
    Check(server.Publish(matched), "integration RuleMatched is published");
    inputweaver::RuntimeDebugEvent ended{};
    ended.kind = inputweaver::RuntimeDebugEventKind::ExecutionEnded;
    ended.captureEpoch = correlation.captureEpoch;
    ended.executionMarker = 88U;
    ended.result = inputweaver::RuntimeExecutionResult::Cancelled;
    Check(server.Publish(ended), "integration ExecutionEnded is published");
    inputweaver::debug::InputEventPayload input{};
    input.device = inputweaver::DeviceKind::Keyboard;
    input.transition = inputweaver::Transition::Down;
    input.origin = inputweaver::InputOrigin::ExternalInjected;
    input.disposition = inputweaver::debug::InputDisposition::Forward;
    input.virtualKey = 66U;
    Check(
        correlation.Active() && server.PublishInput(correlation, input),
        "integration trigger input is published after RuleMatched");
    Check(
        WaitUntil([&] {
            const auto state = client.ReadState();
            return state->ruleExecutions.size() == 1U
                && state->ruleExecutions[0].executionMarker == 88U
                && state->ruleExecutions[0].result
                    == inputweaver::RuntimeExecutionResult::Cancelled
                && state->ruleExecutions[0].conditionText == "always"
                && state->ruleExecutions[0].actionText == "F6 -> F7";
        }),
        "DebugClient correlates an early RuleMatched from WindowsDebugServer");

    Check(client.StopCapture().Succeeded(), "DebugClient stops capture");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Stop,
        "DebugClient StopCapture reaches WindowsDebugServer");
    server.EndCapture();

    inputweaver::RuntimeDebugEvent shortArray = arrayChanged;
    shortArray.array.length = 2U;
    shortArray.array.prefixCount = 2U;
    shortArray.array.elements[0].stateValue = 0U;
    shortArray.array.elements[1].stateValue = 1U;
    inputweaver::RuntimeDebugEvent longArray = arrayChanged;
    longArray.array.length = 12U;
    longArray.array.prefixCount = 4U;
    longArray.array.suffixCount = 4U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        longArray.array.elements[index].stateValue = 0U;
        longArray.array.elements[4U + index].stateValue = 1U;
    }
    std::atomic<bool> stopArrayWriter{false};
    std::thread arrayWriter([&] {
        bool useLong = false;
        while (!stopArrayWriter.load(std::memory_order_acquire)) {
            (void)server.Publish(useLong ? longArray : shortArray);
            useLong = !useLong;
            Sleep(1U);
        }
    });
    Check(client.StartCapture().Succeeded(),
        "DebugClient restarts capture during array publication");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Start,
        "restarted capture reaches WindowsDebugServer");
    Check(server.BeginCapture({}),
        "server builds a fresh snapshot during array publication");
    const bool consistentArray = WaitUntil([&] {
        const auto state = client.ReadState();
        if (!state->capturing || state->arrays.size() != 2U) {
            return false;
        }
        const auto& value = state->arrays[0].value;
        const bool shortValue = value.length == 2U
            && value.prefixCount == 2U
            && value.suffixCount == 0U
            && !value.elements[0].stateValue
            && value.elements[1].stateValue;
        const bool longValue = value.length == 12U
            && value.prefixCount == 4U
            && value.suffixCount == 4U
            && std::all_of(
                value.elements.begin(),
                value.elements.begin() + 4U,
                [](const auto& element) { return !element.stateValue; })
            && std::all_of(
                value.elements.begin() + 4U,
                value.elements.end(),
                [](const auto& element) { return element.stateValue; });
        return shortValue || longValue;
    });
    stopArrayWriter.store(true, std::memory_order_release);
    arrayWriter.join();
    Check(consistentArray,
        "array cache snapshots never mix two published sequences");
    Check(client.StopCapture().Succeeded(),
        "DebugClient stops the restarted capture");
    Check(
        WaitForRequest(server)
            == inputweaver::win32::DebugCaptureRequest::Stop,
        "restarted capture stop reaches WindowsDebugServer");
    server.EndCapture();
    Check(
        client.RequestExecutorStop().Succeeded(),
        "DebugClient requests executor stop");
    Check(
        WaitUntil([&] {
            return callbacks.stopRequested.load(std::memory_order_acquire);
        }),
        "DebugClient executor-stop command reaches the callback");
    client.Disconnect();
    server.Stop();
}

} // namespace

int main()
{
    TestPipeSession();
    TestDebugClientIntegration();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " Windows debug server test(s) failed.\n";
        return 1;
    }
    std::cout << "Windows debug server tests passed.\n";
    return 0;
}
