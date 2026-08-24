#include "platform/windows/diagnostics/diagnostic_log.hpp"
#include "platform/windows/runtime/input_classifier.hpp"
#include "platform/windows/runtime/input_injector.hpp"
#include "platform/windows/runtime/low_level_hooks.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/process_locator.hpp"
#include "runtime/action_queue.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

int g_failureCount = 0;

enum class FakeSendMode {
    Complete,
    Fail,
    PartialFirstCall,
    PartialThenCleanupFail,
};

struct FakeSendState final {
    FakeSendMode mode{FakeSendMode::Complete};
    std::size_t callCount{};
    std::array<UINT, 4> counts{};
    std::array<std::array<INPUT, inputweaver::kMaximumPreparedInputs>, 4> inputs{};
};

FakeSendState g_fakeSendState;

void Check(bool condition, std::string_view name) {
    if (!condition) {
        ++g_failureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void ResetFakeSend(FakeSendMode mode) {
    g_fakeSendState = {};
    g_fakeSendState.mode = mode;
}

UINT WINAPI FakeSendInput(UINT count, LPINPUT inputs, int inputSize) {
    const std::size_t callIndex = g_fakeSendState.callCount++;
    if (callIndex < g_fakeSendState.counts.size()) {
        g_fakeSendState.counts[callIndex] = count;
        if (inputs != nullptr && inputSize == static_cast<int>(sizeof(INPUT))) {
            const std::size_t copyCount = (std::min)(
                static_cast<std::size_t>(count),
                g_fakeSendState.inputs[callIndex].size());
            std::copy_n(inputs, copyCount, g_fakeSendState.inputs[callIndex].begin());
        }
    }
    if (g_fakeSendState.mode == FakeSendMode::Fail
        || (g_fakeSendState.mode == FakeSendMode::PartialThenCleanupFail && callIndex != 0U)) {
        SetLastError(
            g_fakeSendState.mode == FakeSendMode::PartialThenCleanupFail
                ? ERROR_RETRY
                : ERROR_ACCESS_DENIED);
        return 0U;
    }
    if ((g_fakeSendState.mode == FakeSendMode::PartialFirstCall
         || g_fakeSendState.mode == FakeSendMode::PartialThenCleanupFail)
        && callIndex == 0U) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return count == 0U ? 0U : 1U;
    }
    SetLastError(ERROR_SUCCESS);
    return count;
}

inputweaver::ActionBatch MakeKeyboardTap(
    inputweaver::ControlCode code,
    std::uint64_t sourceSequence = 1U) {
    inputweaver::ActionBatch batch{};
    batch.sourceSequence = sourceSequence;
    batch.outputDevice = inputweaver::DeviceKind::Keyboard;
    batch.outputCode = code;
    batch.actions[0] = {
        inputweaver::DeviceKind::Keyboard,
        inputweaver::Transition::Down,
        code,
        0,
        0};
    batch.actions[1] = {
        inputweaver::DeviceKind::Keyboard,
        inputweaver::Transition::Up,
        code,
        0,
        0};
    batch.actionCount = 2U;
    return batch;
}

void TestOriginClassification() {
    constexpr inputweaver::SelfTag selfTag = 0x51A7BEEFU;
    KBDLLHOOKSTRUCT keyboard{};
    keyboard.dwExtraInfo = selfTag;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag)
            == inputweaver::InputOrigin::PhysicalCandidate,
        "non-injected keyboard input remains physical");
    keyboard.flags = LLKHF_INJECTED;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag)
            == inputweaver::InputOrigin::SelfInjected,
        "tagged injected keyboard input is self-injected");
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
            == inputweaver::InputOrigin::SelfInjected,
        "tagged injected mouse input is self-injected");
}

void TestActionQueueBoundaries() {
    inputweaver::ActionQueue queue;
    for (std::size_t index = 0U; index < inputweaver::kActionQueueCapacity; ++index) {
        inputweaver::ActionBatch batch{};
        batch.sourceSequence = index;
        Check(queue.TryPush(batch), "action queue accepts each capacity slot");
    }
    Check(
        queue.SizeApprox() == inputweaver::kActionQueueCapacity,
        "action queue reports its bounded capacity");
    Check(
        !queue.TryPush({}) && queue.RejectedPushCount() == 1U,
        "action queue rejects and counts overflow");
    for (std::size_t index = 0U; index < inputweaver::kActionQueueCapacity; ++index) {
        inputweaver::ActionBatch batch{};
        Check(
            queue.TryPop(batch) && batch.sourceSequence == index,
            "action queue preserves FIFO order");
    }
    Check(queue.Empty(), "action queue drains completely");

    inputweaver::ActionQueue pairQueue;
    inputweaver::ActionBatch first{};
    first.sourceSequence = 100U;
    inputweaver::ActionBatch second{};
    second.sourceSequence = 101U;
    Check(pairQueue.TryPushPair(first, second), "action queue publishes a pair atomically");
    inputweaver::ActionBatch popped{};
    Check(
        pairQueue.TryPop(popped) && popped.sourceSequence == 100U
            && pairQueue.TryPop(popped) && popped.sourceSequence == 101U,
        "action queue preserves pair order");

    inputweaver::ActionQueue commitQueue;
    Check(
        commitQueue.TryPushWithCommit(first, []() noexcept { return false; })
                == inputweaver::ActionQueuePushResult::CommitRejected
            && commitQueue.Empty(),
        "rejected commit publishes no action");
}

void TestActionQueueConcurrency() {
    constexpr std::size_t itemCount = 50'000U;
    inputweaver::ActionQueue queue;
    std::thread producer([&queue]() {
        for (std::size_t index = 0U; index < itemCount; ++index) {
            inputweaver::ActionBatch batch{};
            batch.sourceSequence = index;
            while (!queue.TryPush(batch)) {
                std::this_thread::yield();
            }
        }
    });
    bool ordered = true;
    std::size_t consumed = 0U;
    while (consumed < itemCount) {
        inputweaver::ActionBatch batch{};
        if (!queue.TryPop(batch)) {
            std::this_thread::yield();
            continue;
        }
        ordered = ordered && batch.sourceSequence == consumed;
        ++consumed;
    }
    producer.join();
    Check(ordered && queue.Empty(), "concurrent action queue remains ordered and drains");
}

void TestInjectorSafety() {
    constexpr inputweaver::SelfTag selfTag = 0x6B524D31U;
    inputweaver::InputInjector injector(selfTag, &FakeSendInput);
    const inputweaver::ActionBatch keyboardBatch = MakeKeyboardTap(VK_F7, 60U);
    const inputweaver::PreparedInputBatch keyboard = injector.Prepare(keyboardBatch);
    Check(
        keyboard.Succeeded() && keyboard.count == 2U
            && keyboard.inputs[0].type == INPUT_KEYBOARD
            && keyboard.inputs[1].type == INPUT_KEYBOARD,
        "injector prepares a paired keyboard tap");
    Check(
        keyboard.inputs[0].ki.dwExtraInfo == selfTag
            && keyboard.inputs[1].ki.dwExtraInfo == selfTag
            && (keyboard.inputs[0].ki.dwFlags & KEYEVENTF_SCANCODE) != 0U
            && (keyboard.inputs[1].ki.dwFlags & KEYEVENTF_KEYUP) != 0U,
        "prepared keyboard transitions carry the self tag and release");

    const inputweaver::PreparedInputBatch extended =
        injector.Prepare(MakeKeyboardTap(VK_RIGHT));
    Check(
        extended.Succeeded()
            && (extended.inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0U
            && (extended.inputs[1].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0U,
        "extended keyboard transitions retain their native flag");

    ResetFakeSend(FakeSendMode::Complete);
    Check(
        injector.Inject(keyboardBatch).Succeeded()
            && g_fakeSendState.callCount == 1U
            && g_fakeSendState.counts[0] == 2U,
        "complete SendInput batch succeeds in one call");

    ResetFakeSend(FakeSendMode::PartialFirstCall);
    const inputweaver::InjectionResult partial = injector.Inject(keyboardBatch);
    Check(
        partial.outcome == inputweaver::InjectionOutcome::SendPartial
            && partial.sent == 1U
            && partial.cleanupAttempted
            && partial.cleanupRequested == 1U
            && partial.cleanupSent == 1U
            && g_fakeSendState.callCount == 2U,
        "partial keyboard injection immediately releases inserted state");
    Check(
        (g_fakeSendState.inputs[1][0].ki.dwFlags & KEYEVENTF_KEYUP) != 0U
            && g_fakeSendState.inputs[1][0].ki.dwExtraInfo == selfTag,
        "cleanup release retains the self tag");

    ResetFakeSend(FakeSendMode::PartialThenCleanupFail);
    const inputweaver::InjectionResult unresolved = injector.Inject(keyboardBatch);
    Check(
        unresolved.outcome == inputweaver::InjectionOutcome::SendPartial
            && unresolved.error == ERROR_NOT_ENOUGH_MEMORY
            && unresolved.cleanupAttempted
            && unresolved.cleanupSent == 0U
            && unresolved.cleanupError == ERROR_RETRY,
        "cleanup failure remains separately observable");

    ResetFakeSend(FakeSendMode::Fail);
    const inputweaver::InjectionResult failed = injector.Inject(keyboardBatch);
    Check(
        failed.outcome == inputweaver::InjectionOutcome::SendFailed
            && failed.sent == 0U
            && failed.error == ERROR_ACCESS_DENIED
            && !failed.cleanupAttempted,
        "zero-insert failure preserves the native error without cleanup");
    Check(
        !inputweaver::InputInjector(0U, &FakeSendInput)
             .Prepare(keyboardBatch)
             .Succeeded(),
        "zero self tag is rejected before injection");

    inputweaver::ActionBatch invalid = keyboardBatch;
    invalid.actions[0].transition = inputweaver::Transition::Move;
    Check(!injector.Prepare(invalid).Succeeded(), "invalid keyboard movement is rejected");

    KBDLLHOOKSTRUCT hook{};
    hook.flags = LLKHF_INJECTED;
    hook.dwExtraInfo = keyboard.inputs[0].ki.dwExtraInfo;
    Check(
        inputweaver::ClassifyKeyboard(hook, selfTag)
            == inputweaver::InputOrigin::SelfInjected,
        "injector tag closes the classification contract");
}

void TestInjectionCircuitBreaker() {
    inputweaver::InjectionCircuitBreaker breaker(3U);
    Check(!breaker.RecordFailure(), "first injection failure keeps circuit closed");
    Check(!breaker.RecordFailure(), "second injection failure keeps circuit closed");
    breaker.RecordSuccess();
    Check(
        breaker.ConsecutiveFailures() == 0U && !breaker.IsOpen(),
        "successful injection resets a closed circuit");
    Check(!breaker.RecordFailure(), "failure count restarts after success");
    Check(!breaker.RecordFailure(), "second restarted failure keeps circuit closed");
    Check(breaker.RecordFailure() && breaker.IsOpen(), "third failure opens the circuit");
    breaker.RecordSuccess();
    Check(breaker.IsOpen(), "opened injection circuit does not silently rearm");
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

    constexpr inputweaver::SelfTag selfTag = 0x7100U;
    Check(
        inputweaver::CategorizeExtraInfo(0U, selfTag)
                == inputweaver::ExtraInfoCategory::Zero
            && inputweaver::CategorizeExtraInfo(selfTag, selfTag)
                == inputweaver::ExtraInfoCategory::OwnTag
            && inputweaver::CategorizeExtraInfo(selfTag + 1U, selfTag)
                == inputweaver::ExtraInfoCategory::OtherNonzero,
        "diagnostic extra information is reduced to non-raw categories");

    inputweaver::SpscDiagnosticRing<inputweaver::HookDiagnosticRecord, 3U> ring;
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
    const inputweaver::IntegrityLevelResult integrity =
        inputweaver::QueryProcessIntegrityLevel(GetCurrentProcess());
    Check(integrity.succeeded, "current process integrity level is queryable");
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

}  // namespace

int main() {
    TestOriginClassification();
    TestActionQueueBoundaries();
    TestActionQueueConcurrency();
    TestInjectorSafety();
    TestInjectionCircuitBreaker();
    TestDiagnosticPrivacyAndBounds();
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
