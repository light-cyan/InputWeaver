#include "platform/windows/diagnostics/diagnostic_log.hpp"
#include "platform/windows/runtime/input_classifier.hpp"
#include "platform/windows/runtime/input_injector.hpp"
#include "platform/windows/runtime/low_level_hooks.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/process_locator.hpp"
#include "platform/windows/runtime/windows_output_queue.hpp"
#include "support/bounded_mpmc_queue.hpp"
#include "support/callback_ref.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

int g_failureCount = 0;

struct FakeSendState final {
    bool fail{};
    std::size_t callCount{};
    UINT count{};
    INPUT input{};
};

FakeSendState g_fakeSendState;

void Check(bool condition, std::string_view name) {
    if (!condition) {
        ++g_failureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void ResetFakeSend(bool fail = false) {
    g_fakeSendState = {};
    g_fakeSendState.fail = fail;
}

UINT WINAPI FakeSendInput(UINT count, LPINPUT inputs, int inputSize) {
    ++g_fakeSendState.callCount;
    g_fakeSendState.count = count;
    if (inputs != nullptr && count != 0U
        && inputSize == static_cast<int>(sizeof(INPUT))) {
        g_fakeSendState.input = inputs[0];
    }
    if (g_fakeSendState.fail) {
        SetLastError(ERROR_ACCESS_DENIED);
        return 0U;
    }
    SetLastError(ERROR_SUCCESS);
    return count;
}

inputweaver::WindowsOutputItem MakeVirtualKeyOutput(
    inputweaver::WindowsVirtualKey virtualKey,
    inputweaver::WindowsOutputTransition transition =
        inputweaver::WindowsOutputTransition::Down) {
    inputweaver::WindowsOutputItem item{};
    item.outputCode = virtualKey;
    item.recipe.kind =
        inputweaver::WindowsOutputKind::KeyboardVirtualKey;
    item.recipe.virtualKey = virtualKey;
    item.transition = transition;
    return item;
}

inputweaver::WindowsOutputItem MakeScanCodeOutput(
    inputweaver::WindowsScanCode scanCode,
    bool extended) {
    inputweaver::WindowsOutputItem item{};
    item.recipe.kind =
        inputweaver::WindowsOutputKind::KeyboardScanCode;
    item.recipe.scanCode = scanCode;
    item.recipe.extendedScanCode = extended;
    return item;
}

void TestOriginClassification() {
    constexpr inputweaver::WindowsSelfTag selfTag = 0x51A7BEEFU;
    KBDLLHOOKSTRUCT keyboard{};
    keyboard.dwExtraInfo = selfTag;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag)
            == inputweaver::InputOrigin::PhysicalCandidate,
        "non-injected keyboard input remains physical");
    keyboard.flags = LLKHF_INJECTED;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag)
            == inputweaver::InputOrigin::CurrentInstanceInjected,
        "tagged injected keyboard input is current-instance injected");
    keyboard.dwExtraInfo = selfTag + 1U;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag)
            == inputweaver::InputOrigin::ExternalInjected,
        "other injected keyboard input remains external");

    MSLLHOOKSTRUCT mouse{};
    mouse.flags = LLMHF_INJECTED;
    mouse.dwExtraInfo = selfTag;
    Check(
        inputweaver::ClassifyMouse(mouse, selfTag)
            == inputweaver::InputOrigin::CurrentInstanceInjected,
        "tagged injected mouse input is current-instance injected");
}

void TestWindowsOutputQueueBoundaries() {
    inputweaver::WindowsOutputQueue queue;
    for (std::size_t index = 0U;
         index < inputweaver::kWindowsOutputQueueCapacity;
         ++index) {
        inputweaver::WindowsOutputItem item{};
        item.sourceSequence = index;
        Check(queue.TryPush(item), "output queue accepts each capacity slot");
    }
    Check(!queue.TryPush({}), "output queue rejects overflow");
    for (std::size_t index = 0U;
         index < inputweaver::kWindowsOutputQueueCapacity;
         ++index) {
        inputweaver::WindowsOutputItem item{};
        Check(
            queue.TryPop(item) && item.sourceSequence == index,
            "output queue preserves FIFO order");
    }
    Check(queue.Empty(), "output queue drains completely");

}

void TestWindowsOutputQueueConcurrency() {
    constexpr std::size_t itemCount = 50'000U;
    inputweaver::WindowsOutputQueue queue;
    std::thread producer([&queue]() {
        for (std::size_t index = 0U; index < itemCount; ++index) {
            inputweaver::WindowsOutputItem item{};
            item.sourceSequence = index;
            while (!queue.TryPush(item)) {
                std::this_thread::yield();
            }
        }
    });
    bool ordered = true;
    std::size_t consumed = 0U;
    while (consumed < itemCount) {
        inputweaver::WindowsOutputItem item{};
        if (!queue.TryPop(item)) {
            std::this_thread::yield();
            continue;
        }
        ordered = ordered && item.sourceSequence == consumed;
        ++consumed;
    }
    producer.join();
    Check(ordered && queue.Empty(), "concurrent output queue remains ordered and drains");
}

void TestCallbackRef() {
    inputweaver::support::CallbackRef<void() noexcept> empty;
    empty.Invoke();
    bool invoked = false;
    const inputweaver::support::CallbackRef<void() noexcept> callback{
        &invoked,
        [](void* context) noexcept {
            *static_cast<bool*>(context) = true;
        }};
    callback.Invoke();
    const inputweaver::support::CallbackRef<bool() noexcept> emptyResult;
    Check(
        invoked && !emptyResult.Invoke(),
        "callback references invoke bound targets and default empty results");
}

void TestBoundedMpmcQueue() {
    inputweaver::support::BoundedMpmcQueue<std::size_t, 4U> bounded;
    std::size_t item{};
    Check(!bounded.TryPop(item), "MPMC queue starts empty");
    for (std::size_t value = 0U; value < 4U; ++value) {
        Check(bounded.TryPush(value), "MPMC queue accepts each capacity slot");
    }
    Check(!bounded.TryPush(4U), "MPMC queue rejects overflow");
    for (std::size_t value = 0U; value < 4U; ++value) {
        Check(
            bounded.TryPop(item) && item == value,
            "MPMC queue preserves single-thread FIFO order");
    }

    constexpr std::size_t itemCount = 20'000U;
    inputweaver::support::BoundedMpmcQueue<std::size_t, 64U> concurrent;
    std::array<std::atomic<std::uint8_t>, itemCount> observed{};
    std::atomic<std::size_t> nextProduced{};
    std::atomic<std::size_t> consumed{};
    std::atomic<bool> valid{true};
    std::array<std::thread, 2U> consumers;
    for (auto& consumer : consumers) {
        consumer = std::thread([&] {
            while (consumed.load(std::memory_order_acquire) < itemCount) {
                std::size_t value{};
                if (!concurrent.TryPop(value)) {
                    std::this_thread::yield();
                    continue;
                }
                if (value >= itemCount
                    || observed[value].fetch_add(
                        1U,
                        std::memory_order_relaxed) != 0U) {
                    valid.store(false, std::memory_order_relaxed);
                }
                consumed.fetch_add(1U, std::memory_order_release);
            }
        });
    }
    std::array<std::thread, 2U> producers;
    for (auto& producer : producers) {
        producer = std::thread([&] {
            for (;;) {
                const std::size_t value = nextProduced.fetch_add(
                    1U,
                    std::memory_order_relaxed);
                if (value >= itemCount) {
                    break;
                }
                while (!concurrent.TryPush(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }
    Check(
        valid.load(std::memory_order_relaxed)
            && consumed.load(std::memory_order_relaxed) == itemCount,
        "concurrent MPMC queue delivers every item exactly once");
}

void TestInjectorSafety() {
    constexpr inputweaver::WindowsSelfTag selfTag = 0x6B524D31U;
    inputweaver::InputInjector injector(selfTag, &FakeSendInput);
    const inputweaver::WindowsOutputItem keyboardOutput =
        MakeVirtualKeyOutput(VK_F7);
    const inputweaver::PreparedInput keyboard = injector.Prepare(keyboardOutput);
    Check(
        keyboard.Succeeded() && keyboard.input.type == INPUT_KEYBOARD,
        "injector prepares one keyboard output");
    Check(
        keyboard.input.ki.dwExtraInfo == selfTag
            && keyboard.input.ki.wVk == VK_F7
            && (keyboard.input.ki.dwFlags & KEYEVENTF_SCANCODE) == 0U,
        "prepared keyboard output carries its identity and self tag");

    const inputweaver::PreparedInput release = injector.Prepare(
        MakeVirtualKeyOutput(
            VK_F7,
            inputweaver::WindowsOutputTransition::Up));
    Check(
        release.Succeeded()
            && (release.input.ki.dwFlags & KEYEVENTF_KEYUP) != 0U,
        "keyboard release retains its transition");

    const inputweaver::PreparedInput extended =
        injector.Prepare(MakeScanCodeOutput(0x4dU, true));
    Check(
        extended.Succeeded()
            && (extended.input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0U,
        "extended keyboard output retains its native flag");

    ResetFakeSend();
    Check(
        injector.Inject(keyboardOutput).Succeeded()
            && g_fakeSendState.callCount == 1U
            && g_fakeSendState.count == 1U,
        "one output uses one SendInput call");

    ResetFakeSend();
    const inputweaver::InputInjector dryRunInjector(
        selfTag,
        &FakeSendInput,
        true);
    const inputweaver::InjectionResult simulated =
        dryRunInjector.Inject(keyboardOutput);
    Check(
        simulated.Succeeded()
            && simulated.requested == 0U
            && simulated.sent == 0U
            && g_fakeSendState.callCount == 0U,
        "dry-run output succeeds without calling SendInput");

    ResetFakeSend(true);
    const inputweaver::InjectionResult failed = injector.Inject(keyboardOutput);
    Check(
        failed.outcome == inputweaver::InjectionOutcome::SendFailed
            && failed.sent == 0U
            && failed.error == ERROR_ACCESS_DENIED,
        "failed output preserves the native error");
    Check(
        !inputweaver::InputInjector(0U, &FakeSendInput)
             .Prepare(keyboardOutput)
             .Succeeded(),
        "zero self tag is rejected before injection");

    inputweaver::WindowsOutputItem invalid = keyboardOutput;
    invalid.recipe.kind = inputweaver::WindowsOutputKind::None;
    Check(!injector.Prepare(invalid).Succeeded(), "missing output recipe is rejected");

    KBDLLHOOKSTRUCT hook{};
    hook.flags = LLKHF_INJECTED;
    hook.dwExtraInfo = keyboard.input.ki.dwExtraInfo;
    Check(
        inputweaver::ClassifyKeyboard(hook, selfTag)
            == inputweaver::InputOrigin::CurrentInstanceInjected,
        "injector tag closes the classification contract");
}

void TestInjectionCircuitBreaker() {
    inputweaver::InjectionCircuitBreaker breaker(3U);
    Check(!breaker.RecordFailure(), "first injection failure keeps circuit closed");
    Check(!breaker.RecordFailure(), "second injection failure keeps circuit closed");
    breaker.RecordSuccess();
    Check(!breaker.RecordFailure(), "successful injection resets a closed circuit");
    Check(!breaker.RecordFailure(), "failure count restarts after success");
    Check(breaker.RecordFailure(), "third failure opens the circuit");
    breaker.RecordSuccess();
    Check(breaker.RecordFailure(), "opened injection circuit does not silently rearm");
}

void TestDiagnosticPrivacyAndBounds() {
    inputweaver::HookDiagnosticRecord ordinary{};
    ordinary.device = inputweaver::DeviceKind::Keyboard;
    ordinary.transition = inputweaver::Transition::Down;
    ordinary.code = 'A';
    ordinary.scanCode = 30U;
    inputweaver::ApplyPrivacyRedaction(ordinary);
    const std::string json = inputweaver::FormatHookDiagnosticJson(ordinary);
    Check(
        ordinary.control == inputweaver::DiagnosticControl::OtherKeyboard
            && ordinary.code == 0U
            && ordinary.scanCode == 0U
            && json.find("\"code\":65") == std::string::npos
            && json.find("\"scan\":30") == std::string::npos,
        "ordinary keyboard diagnostics cannot reconstruct typed content");

    inputweaver::HookDiagnosticRecord f12{};
    f12.device = inputweaver::DeviceKind::Keyboard;
    f12.transition = inputweaver::Transition::Down;
    f12.code = VK_F12;
    Check(
        !inputweaver::ShouldPublishHookDiagnostic(f12, false)
            && inputweaver::ShouldPublishProgramHookDiagnostic(f12, false, true),
        "diagnostic publication follows compiled control activation instead of F12");

    constexpr inputweaver::WindowsSelfTag selfTag = 0x7100U;
    Check(
        inputweaver::CategorizeExtraInfo(0U, selfTag)
                == inputweaver::ExtraInfoCategory::Zero
            && inputweaver::CategorizeExtraInfo(selfTag, selfTag)
                == inputweaver::ExtraInfoCategory::OwnTag
            && inputweaver::CategorizeExtraInfo(selfTag + 1U, selfTag)
                == inputweaver::ExtraInfoCategory::OtherNonzero,
        "diagnostic extra information is reduced to non-raw categories");

    inputweaver::support::FixedSpscRing<
        inputweaver::HookDiagnosticRecord,
        3U> ring;
    for (std::uint64_t sequence = 1U; sequence <= 3U; ++sequence) {
        inputweaver::HookDiagnosticRecord record{};
        record.sequence = sequence;
        Check(ring.TryPush(record), "diagnostic ring accepts each capacity slot");
    }
    Check(!ring.TryPush({}), "diagnostic ring rejects overflow");
    for (std::uint64_t sequence = 1U; sequence <= 3U; ++sequence) {
        inputweaver::HookDiagnosticRecord record{};
        Check(
            ring.TryPop(record) && record.sequence == sequence,
            "diagnostic ring preserves FIFO order");
    }
    Check(ring.Empty(), "diagnostic ring drains completely");
    Check(
        inputweaver::JsonlAppendFits(inputweaver::kMaximumJsonlBytes - 10U, 10U)
            && !inputweaver::JsonlAppendFits(inputweaver::kMaximumJsonlBytes - 10U, 11U),
        "JSONL size boundary accepts the limit and rejects overflow");

    inputweaver::RuntimeDiagnosticRecord launchFailure{};
    launchFailure.kind = inputweaver::RuntimeDiagnosticKind::LaunchFailure;
    launchFailure.detail = static_cast<std::uint32_t>(
        inputweaver::RuntimeLaunchResult::CreationFailed);
    launchFailure.platformError = ERROR_ACCESS_DENIED;
    const std::string launchJson =
        inputweaver::FormatRuntimeDiagnosticJson(launchFailure);
    Check(
        launchJson.find("\"launch_result\":\"CreationFailed\"")
                != std::string::npos
            && launchJson.find("\"platform_error\":5")
                != std::string::npos,
        "runtime launch JSON preserves the portable category and platform error");

    inputweaver::RuntimeDiagnosticRecord activationFailure{};
    activationFailure.kind = inputweaver::RuntimeDiagnosticKind::ActivationFailure;
    activationFailure.subject = static_cast<std::uint32_t>(
        inputweaver::RuntimeActivationSubject::ArrayBytes);
    activationFailure.detail = static_cast<std::uint32_t>(
        inputweaver::RuntimeActivationErrorCode::ArrayByteCapacity);
    activationFailure.required = 8192U;
    activationFailure.available = 4096U;
    const std::string activationJson =
        inputweaver::FormatRuntimeDiagnosticJson(activationFailure);
    Check(
        activationJson.find("\"event\":\"ActivationFailure\"")
                != std::string::npos
            && activationJson.find("\"subject\":5") != std::string::npos
            && activationJson.find("\"activation_code\":5")
                != std::string::npos
            && activationJson.find("\"required\":8192")
                != std::string::npos
            && activationJson.find("\"available\":4096")
                != std::string::npos,
        "array activation JSON preserves its capacity dimension and values");

    inputweaver::DiagnosticLog disabledLog;
    std::wstring errorMessage;
    Check(
        disabledLog.Start(L"", errorMessage) && !disabledLog.Enabled(),
        "omitted log path creates no logging worker");
    Check(
        disabledLog.TryPushHook({})
            && disabledLog.TryPushInjection({})
            && disabledLog.TryPushRuntime({})
            && disabledLog.DroppedHookRecords() == 0U
            && disabledLog.DroppedInjectionRecords() == 0U
            && disabledLog.DroppedRuntimeRecords() == 0U,
        "disabled logging accepts no-op publications without drop noise");
    disabledLog.Stop();

    wchar_t temporaryDirectory[MAX_PATH]{};
    wchar_t temporaryFile[MAX_PATH]{};
    const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporaryDirectory);
    const bool temporaryPathReady = temporaryLength != 0U
        && temporaryLength < MAX_PATH
        && GetTempFileNameW(temporaryDirectory, L"iwv", 0U, temporaryFile) != 0U;
    Check(temporaryPathReady, "test obtains a temporary JSONL path");
    if (!temporaryPathReady) {
        return;
    }
    inputweaver::DiagnosticLog boundedLog;
    const bool started = boundedLog.Start(temporaryFile, errorMessage, 1024U);
    Check(started, "bounded JSONL diagnostic worker starts");
    if (started) {
        for (std::uint64_t sequence = 0U; sequence < 20U; ++sequence) {
            inputweaver::HookDiagnosticRecord record{};
            record.sequence = sequence;
            record.device = inputweaver::DeviceKind::Keyboard;
            record.transition = inputweaver::Transition::Down;
            record.code = VK_F6;
            (void)boundedLog.TryPushHook(record);
        }
        boundedLog.Stop();
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        const bool sizeAvailable = GetFileAttributesExW(
            temporaryFile, GetFileExInfoStandard, &attributes) != FALSE;
        const std::uint64_t fileSize = sizeAvailable
            ? (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32U)
                | attributes.nFileSizeLow
            : 0U;
        Check(sizeAvailable, "bounded JSONL file size is readable");
        Check(
            boundedLog.JsonlTruncated()
                && boundedLog.JsonlBytesWritten() <= 1024U
                && fileSize == boundedLog.JsonlBytesWritten(),
            "JSONL writer never exceeds its configured byte limit");
    }
    (void)DeleteFileW(temporaryFile);
}

void TestConcurrentRuntimeDiagnosticPublication() {
    constexpr std::size_t producerCount = 2U;
    constexpr std::uint64_t attemptsPerProducer = 10'000U;
    constexpr std::size_t attemptCount = producerCount * attemptsPerProducer;
    inputweaver::RuntimeDiagnosticRing ring;
    std::atomic<unsigned int> ready{0U};
    std::atomic<unsigned int> finished{0U};
    std::atomic<std::uint64_t> accepted{0U};
    std::atomic<bool> begin{false};
    std::array<std::thread, producerCount> producers;
    for (std::size_t producer = 0U; producer < producers.size(); ++producer) {
        producers[producer] = std::thread([producer, &ring, &ready, &finished, &accepted, &begin]() {
            ready.fetch_add(1U, std::memory_order_release);
            while (!begin.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const std::uint64_t base = producer * attemptsPerProducer;
            for (std::uint64_t index = 1U; index <= attemptsPerProducer; ++index) {
                inputweaver::RuntimeDiagnosticRecord record{};
                record.kind = inputweaver::RuntimeDiagnosticKind::OwnershipChange;
                record.sequence = base + index;
                if (ring.TryPush(record)) {
                    accepted.fetch_add(1U, std::memory_order_relaxed);
                }
            }
            finished.fetch_add(1U, std::memory_order_release);
        });
    }
    while (ready.load(std::memory_order_acquire) != producers.size()) {
        std::this_thread::yield();
    }
    begin.store(true, std::memory_order_release);
    std::array<std::uint8_t, attemptCount> observed{};
    std::size_t observedCount = 0U;
    bool duplicateOrInvalid = false;
    while (finished.load(std::memory_order_acquire) != producerCount
        || !ring.Empty()) {
        inputweaver::RuntimeDiagnosticRecord record{};
        if (!ring.TryPop(record)) {
            std::this_thread::yield();
            continue;
        }
        if (record.sequence == 0U || record.sequence > attemptCount
            || observed[record.sequence - 1U] != 0U) {
            duplicateOrInvalid = true;
            continue;
        }
        observed[record.sequence - 1U] = 1U;
        ++observedCount;
    }
    for (auto& producer : producers) {
        producer.join();
    }
    Check(
        accepted.load(std::memory_order_relaxed) + ring.RejectedPushCount()
                == attemptCount
            && observedCount == accepted.load(std::memory_order_relaxed)
            && !duplicateOrInvalid,
        "concurrent diagnostic admission counts every rejection and loses no accepted record");
}

void TestProcessLocatorAndContext() {
    constexpr DWORD pathCapacity = 32768U;
    std::array<wchar_t, pathCapacity> modulePathBuffer{};
    const DWORD modulePathLength = GetModuleFileNameW(
        nullptr, modulePathBuffer.data(), pathCapacity);
    Check(
        modulePathLength != 0U && modulePathLength < pathCapacity,
        "test resolves its executable path");
    if (modulePathLength == 0U || modulePathLength >= pathCapacity) {
        return;
    }
    const std::wstring modulePath(
        modulePathBuffer.data(), static_cast<std::size_t>(modulePathLength));
    const std::size_t separator = modulePath.find_last_of(L"\\/");
    const std::wstring basename = separator == std::wstring::npos
        ? modulePath
        : modulePath.substr(separator + 1U);
    const auto containsCurrentProcess = [](const inputweaver::win32::LocateResult& result) {
        return std::any_of(
            result.matches.begin(),
            result.matches.end(),
            [](const inputweaver::win32::LocatedProcess& process) {
                return process.processId == GetCurrentProcessId();
            });
    };
    Check(
        containsCurrentProcess(inputweaver::win32::LocateExecutable(modulePath)),
        "absolute-path target discovery finds the current executable");
    Check(
        containsCurrentProcess(inputweaver::win32::LocateExecutable(basename)),
        "basename target discovery finds the current executable");
    Check(
        inputweaver::win32::LocateExecutable(L"relative\\target.exe").status
            == inputweaver::win32::LocateStatus::Error,
        "target path selector must be absolute");
    std::wstring embeddedNull = L"WindowsPlatformTests.exe";
    embeddedNull.push_back(L'\0');
    embeddedNull += L"ignored";
    Check(
        inputweaver::win32::LocateExecutable(embeddedNull).status
            == inputweaver::win32::LocateStatus::Error,
        "target selector rejects embedded NUL");

    inputweaver::TargetProcessContext context;
    Check(
        context.Initialize(0U).error == inputweaver::ProcessContextError::InvalidPid,
        "zero target PID is rejected");
    Check(
        inputweaver::IsTargetIntegrityCompatible(
            SECURITY_MANDATORY_MEDIUM_RID,
            SECURITY_MANDATORY_LOW_RID)
            && inputweaver::IsTargetIntegrityCompatible(
                SECURITY_MANDATORY_MEDIUM_RID,
                SECURITY_MANDATORY_MEDIUM_RID)
            && !inputweaver::IsTargetIntegrityCompatible(
                SECURITY_MANDATORY_MEDIUM_RID,
                SECURITY_MANDATORY_HIGH_RID),
        "target integrity cannot exceed executor integrity");
    const inputweaver::ProcessContextResult current =
        context.Initialize(GetCurrentProcessId(), modulePath);
    Check(
        current.Succeeded()
            && context.IsValid()
            && context.TargetPid() == GetCurrentProcessId(),
        "current same-integrity process becomes a validated target");
    context.Reset();
}

void TestTargetProcessLifecycle() {
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT directoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    Check(
        directoryLength != 0U && directoryLength < MAX_PATH,
        "test resolves the Windows system directory");
    if (directoryLength == 0U || directoryLength >= MAX_PATH) {
        return;
    }
    std::wstring targetPath(systemDirectory, directoryLength);
    targetPath += L"\\cmd.exe";
    std::wstring commandLine = L"\"" + targetPath + L"\" /d /c exit 0";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool created = CreateProcessW(
        targetPath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        nullptr,
        &startup,
        &process) != FALSE;
    Check(created, "test starts a suspended target process");
    if (!created) {
        return;
    }

    inputweaver::TargetProcessContext context;
    const inputweaver::ProcessContextResult mismatched = context.Initialize(
        process.dwProcessId, targetPath + L".wrong");
    Check(
        mismatched.error == inputweaver::ProcessContextError::TargetImageMismatch,
        "target context rejects a matching PID with the wrong image path");
    const inputweaver::ProcessContextResult initialized =
        context.Initialize(process.dwProcessId, targetPath);
    Check(initialized.Succeeded(), "target context accepts the live target process");
    Check(
        ResumeThread(process.hThread) != static_cast<DWORD>(-1),
        "test resumes the target process");
    DWORD processWait = WaitForSingleObject(process.hProcess, 5000U);
    if (processWait != WAIT_OBJECT_0) {
        (void)TerminateProcess(process.hProcess, 10U);
        processWait = WaitForSingleObject(process.hProcess, 5000U);
    }
    Check(processWait == WAIT_OBJECT_0, "target process exits");
    if (initialized.Succeeded()) {
        DWORD livenessError = ERROR_SUCCESS;
        Check(
            !context.IsTargetAlive(&livenessError)
                && livenessError == ERROR_PROCESS_ABORTED,
            "retained target handle observes process exit");
        Check(
            !context.IsTargetForeground(),
            "exited target disables foreground routing");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void TestShutdownGraceWindow() {
    inputweaver::ShutdownGraceWindow grace(2000U);
    Check(!grace.Expired(100U), "inactive shutdown grace is not expired");
    grace.Begin(100U);
    Check(!grace.Expired(2099U), "shutdown grace remains active before its deadline");
    Check(grace.RemainingSlice(150U, 50U) == 50U, "shutdown grace caps polling slices");
    Check(grace.RemainingSlice(2080U, 50U) == 20U, "shutdown grace reports final slice");
    Check(
        grace.Expired(2100U) && grace.RemainingSlice(2100U, 50U) == 0U,
        "shutdown grace expires at its deadline");
}

#include "windows_mouse_diagnostic_tests.inc"

}  // namespace

int main() {
    TestOriginClassification();
    TestWindowsOutputQueueBoundaries();
    TestWindowsOutputQueueConcurrency();
    TestCallbackRef();
    TestBoundedMpmcQueue();
    TestInjectorSafety();
    TestInjectionCircuitBreaker();
    TestDiagnosticPrivacyAndBounds();
    TestMouseNumericDiagnostics();
    TestConcurrentRuntimeDiagnosticPublication();
    TestProcessLocatorAndContext();
    TestTargetProcessLifecycle();
    TestShutdownGraceWindow();

    if (g_failureCount != 0) {
        std::cerr << g_failureCount << " Windows platform test(s) failed.\n";
        return 1;
    }
    std::cout << "All Windows platform tests passed.\n";
    return 0;
}
