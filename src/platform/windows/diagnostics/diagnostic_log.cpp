#include "diagnostic_log.hpp"

#include <sstream>
#include <iomanip>
#include <string_view>
#include <utility>

namespace inputweaver {
namespace {

const char* DeviceName(DeviceKind value) noexcept {
    return value == DeviceKind::Keyboard ? "Keyboard" : "Mouse";
}

const char* PointerOperationName(PointerOperation operation) noexcept {
    switch (operation) {
    case PointerOperation::MoveBy: return "move_by";
    case PointerOperation::MoveTo: return "move_to";
    case PointerOperation::Scroll: return "scroll";
    case PointerOperation::ScrollHorizontal: return "scroll_horizontal";
    }
    return "unknown";
}

const char* OriginName(InputOrigin value) noexcept {
    switch (value) {
        case InputOrigin::PhysicalCandidate:
            return "PhysicalCandidate";
        case InputOrigin::CurrentInstanceInjected:
            return "CurrentInstanceInjected";
        case InputOrigin::ExternalInjected:
            return "ExternalInjected";
        case InputOrigin::InitialSample:
            return "InitialSample";
    }
    return "Unknown";
}

const char* TransitionName(Transition value) noexcept {
    switch (value) {
        case Transition::Down:
            return "Down";
        case Transition::Up:
            return "Up";
        case Transition::Move:
            return "Move";
        case Transition::VerticalWheel:
            return "VerticalWheel";
        case Transition::HorizontalWheel:
            return "HorizontalWheel";
    }
    return "Unknown";
}

const char* ControlName(DiagnosticControl value) noexcept {
    switch (value) {
        case DiagnosticControl::OtherKeyboard:
            return "OtherKeyboard";
        case DiagnosticControl::OtherMouse:
            return "OtherMouse";
        case DiagnosticControl::MouseMove:
            return "MouseMove";
        case DiagnosticControl::MouseWheel:
            return "MouseWheel";
        case DiagnosticControl::F6:
            return "F6";
        case DiagnosticControl::F7:
            return "F7";
        case DiagnosticControl::F8:
            return "F8";
        case DiagnosticControl::F9:
            return "F9";
        case DiagnosticControl::F10:
            return "F10";
        case DiagnosticControl::F12:
            return "F12";
        case DiagnosticControl::Control:
            return "Control";
        case DiagnosticControl::Shift:
            return "Shift";
        case DiagnosticControl::MiddleButton:
            return "MiddleButton";
    }
    return "Unknown";
}

const char* ExtraInfoName(ExtraInfoCategory value) noexcept {
    switch (value) {
        case ExtraInfoCategory::Zero:
            return "Zero";
        case ExtraInfoCategory::OwnTag:
            return "SelfTag";
        case ExtraInfoCategory::OtherNonzero:
            return "OtherNonzero";
    }
    return "Unknown";
}

const char* RuntimeLaunchResultName(RuntimeLaunchResult value) noexcept {
    switch (value) {
        case RuntimeLaunchResult::Launched:
            return "Launched";
        case RuntimeLaunchResult::Cancelled:
            return "Cancelled";
        case RuntimeLaunchResult::InvalidCommand:
            return "InvalidCommand";
        case RuntimeLaunchResult::ResolutionFailed:
            return "ResolutionFailed";
        case RuntimeLaunchResult::CreationFailed:
            return "CreationFailed";
    }
    return "Unknown";
}

const char* ExecutorEventName(ExecutorDiagnosticKind value) noexcept {
    switch (value) {
    case ExecutorDiagnosticKind::SessionStart: return "SessionStart";
    case ExecutorDiagnosticKind::ConfigurationResolved: return "ConfigurationResolved";
    case ExecutorDiagnosticKind::StartupFailure: return "StartupFailure";
    case ExecutorDiagnosticKind::TargetSearchStarted: return "TargetSearchStarted";
    case ExecutorDiagnosticKind::TargetSearchWaiting: return "TargetSearchWaiting";
    case ExecutorDiagnosticKind::TargetSearchAmbiguous: return "TargetSearchAmbiguous";
    case ExecutorDiagnosticKind::TargetSearchFailure: return "TargetSearchFailure";
    case ExecutorDiagnosticKind::TargetFound: return "TargetFound";
    case ExecutorDiagnosticKind::TargetAttached: return "TargetAttached";
    case ExecutorDiagnosticKind::TargetAttachFailure: return "TargetAttachFailure";
    case ExecutorDiagnosticKind::TargetLost: return "TargetLost";
    case ExecutorDiagnosticKind::SessionStop: return "SessionStop";
    }
    return "Unknown";
}

std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    (void)WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

void AppendJsonString(std::ostringstream& stream, std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    stream << '"';
    for (const char valueCharacter : value) {
        const auto character = static_cast<unsigned char>(valueCharacter);
        switch (character) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\b': stream << "\\b"; break;
        case '\f': stream << "\\f"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (character < 0x20U) {
                stream << "\\u00" << hex[character >> 4U] << hex[character & 0x0fU];
            } else {
                stream << static_cast<char>(character);
            }
        }
    }
    stream << '"';
}

void AppendJsonField(std::ostringstream& stream, std::string_view name, std::string_view value) {
    stream << ",\"" << name << "\":";
    AppendJsonString(stream, value);
}

std::uint64_t UnixMilliseconds() noexcept {
    FILETIME fileTime{};
    GetSystemTimePreciseAsFileTime(&fileTime);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    constexpr std::uint64_t kWindowsEpochTicks = 116'444'736'000'000'000ULL;
    return ticks.QuadPart >= kWindowsEpochTicks
        ? (ticks.QuadPart - kWindowsEpochTicks) / 10'000ULL
        : 0U;
}

std::string MakeSessionId() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(8) << GetCurrentProcessId()
           << '-' << std::setw(16) << UnixMilliseconds()
           << '-' << std::setw(16) << static_cast<std::uint64_t>(counter.QuadPart);
    return stream.str();
}

bool IsExactControl(DiagnosticControl value) noexcept {
    return value != DiagnosticControl::OtherKeyboard && value != DiagnosticControl::OtherMouse &&
           value != DiagnosticControl::MouseMove && value != DiagnosticControl::MouseWheel;
}

}  // namespace

DiagnosticControl ClassifyDiagnosticControl(
    DeviceKind device,
    Transition transition,
    std::uint32_t code) noexcept {
    if (device == DeviceKind::Mouse) {
        if (transition == Transition::Move) {
            return DiagnosticControl::MouseMove;
        }
        if (transition == Transition::VerticalWheel || transition == Transition::HorizontalWheel) {
            return DiagnosticControl::MouseWheel;
        }
        return code == VK_MBUTTON
            ? DiagnosticControl::MiddleButton
            : DiagnosticControl::OtherMouse;
    }

    switch (code) {
        case VK_F6:
            return DiagnosticControl::F6;
        case VK_F7:
            return DiagnosticControl::F7;
        case VK_F8:
            return DiagnosticControl::F8;
        case VK_F9:
            return DiagnosticControl::F9;
        case VK_F10:
            return DiagnosticControl::F10;
        case VK_F12:
            return DiagnosticControl::F12;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return DiagnosticControl::Control;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return DiagnosticControl::Shift;
        default:
            return DiagnosticControl::OtherKeyboard;
    }
}

ExtraInfoCategory CategorizeExtraInfo(
    std::uintptr_t extraInfo,
    std::uintptr_t selfTag) noexcept {
    if (extraInfo == 0) {
        return ExtraInfoCategory::Zero;
    }
    return extraInfo == selfTag
        ? ExtraInfoCategory::OwnTag
        : ExtraInfoCategory::OtherNonzero;
}

void ApplyPrivacyRedaction(HookDiagnosticRecord& record) noexcept {
    record.control = ClassifyDiagnosticControl(record.device, record.transition, record.code);
    if (!IsExactControl(record.control)) {
        record.code = 0;
        record.scanCode = 0;
        if (record.control != DiagnosticControl::MouseWheel) {
            record.mouseData = 0;
        }
    }
}

bool ShouldPublishHookDiagnostic(
    const HookDiagnosticRecord& record,
    bool traceInput) noexcept {
    return traceInput ||
           record.origin == InputOrigin::CurrentInstanceInjected ||
           record.suppressed;
}

bool ShouldPublishProgramHookDiagnostic(
    const HookDiagnosticRecord& record,
    bool traceInput,
    bool activatedControl) noexcept {
    return activatedControl || ShouldPublishHookDiagnostic(record, traceInput);
}

std::string FormatHookDiagnosticJson(const HookDiagnosticRecord& record) {
    std::ostringstream stream;
    stream << std::setprecision(17);
    stream << "{\"kind\":\"hook\",\"seq\":" << record.sequence << ",\"qpc\":" << record.qpcTimestamp
           << ",\"duration_us\":" << record.processingMicroseconds
           << ",\"device\":\"" << DeviceName(record.device)
           << "\",\"transition\":\"" << TransitionName(record.transition) << "\",\"control\":\""
           << ControlName(record.control) << "\",\"code\":" << record.code << ",\"scan\":" << record.scanCode
           << ",\"flags\":" << record.rawFlags << ",\"mouse_data\":" << record.mouseData << ",\"origin\":\""
           << OriginName(record.origin) << "\",\"lower_il\":" << (record.lowerIntegrityInjected ? "true" : "false")
           << ",\"extra\":\"" << ExtraInfoName(record.extraInfo) << "\",\"foreground_pid\":" << record.foregroundPid
           << ",\"suppressed\":" << (record.suppressed ? "true" : "false");
    if (record.device == DeviceKind::Mouse) {
        stream << ",\"x\":" << record.position.x << ",\"y\":" << record.position.y
               << ",\"dx\":" << record.delta.dx << ",\"dy\":" << record.delta.dy
               << ",\"wheel_x\":" << record.delta.wheelX << ",\"wheel_y\":" << record.delta.wheelY;
    }
    stream << '}';
    return stream.str();
}

std::string FormatInjectionDiagnosticJson(const InjectionDiagnosticRecord& record) {
    std::ostringstream stream;
    stream << std::setprecision(17);
    stream << "{\"kind\":\"injection\",\"source_seq\":" << record.sourceSequence
           << ",\"generation\":" << record.outputStateGeneration << ",\"qpc\":" << record.qpcTimestamp
           << ",\"target_pid\":" << record.targetPid << ",\"device\":\"" << DeviceName(record.outputDevice)
           << "\",\"transition\":\"" << TransitionName(record.outputTransition) << "\",\"code\":"
           << record.outputCode << ",\"requested\":" << record.requested << ",\"sent\":"
           << record.sent << ",\"error\":" << record.win32Error << ",\"cancelled_target\":"
           << (record.cancelledForTarget ? "true" : "false") << ",\"cancelled_circuit\":"
           << (record.cancelledForCircuitBreaker ? "true" : "false") << ",\"cancelled_shutdown\":"
           << (record.cancelledForShutdown ? "true" : "false") << ",\"cancelled_generation\":"
           << (record.cancelledForGeneration ? "true" : "false") << ",\"circuit_open\":"
           << (record.circuitBreakerOpen ? "true" : "false");
    if (record.outputKind == RuntimeOutputKind::Pointer) {
        stream << ",\"operation\":\"" << PointerOperationName(record.pointer.operation)
               << "\",\"argument_x\":" << record.pointer.x << ",\"argument_y\":" << record.pointer.y
               << ",\"prepared\":" << (record.pointerPrepared ? "true" : "false");
        if (record.pointerPrepared) {
            stream << ",\"origin_x\":" << record.pointerOrigin.x << ",\"origin_y\":" << record.pointerOrigin.y;
            if (record.pointer.operation <= PointerOperation::MoveTo) {
                stream << ",\"destination_x\":" << record.pointerDestination.x
                       << ",\"destination_y\":" << record.pointerDestination.y;
            } else stream << ",\"wheel_amount\":" << record.preparedWheel;
        }
    }
    stream << '}';
    return stream.str();
}

std::string FormatRuntimeDiagnosticJson(const RuntimeDiagnosticRecord& record) {
    std::ostringstream stream;
    stream << "{\"kind\":\"runtime\",\"event\":\"" << RuntimeDiagnosticKindName(record.kind)
           << "\",\"program_serial\":" << record.programSerial << ",\"generation\":" << record.generation
           << ",\"sequence\":" << record.sequence << ",\"source_begin\":" << record.source.beginByte
           << ",\"source_length\":" << record.source.byteLength << ",\"subject\":" << record.subject
           << ",\"position\":" << record.position << ",\"deadline_ns\":" << record.deadlineNanoseconds
           << ",\"detail\":" << record.detail;
    if (record.kind == RuntimeDiagnosticKind::LaunchFailure) {
        stream << ",\"launch_result\":\""
               << RuntimeLaunchResultName(
                      static_cast<RuntimeLaunchResult>(record.detail))
               << "\",\"platform_error\":" << record.platformError;
    } else if (record.kind == RuntimeDiagnosticKind::ActivationFailure) {
        stream << ",\"activation_code\":" << record.detail
               << ",\"required\":" << record.required
               << ",\"available\":" << record.available;
    }
    stream << "}";
    return stream.str();
}

std::string FormatExecutorDiagnosticJson(const ExecutorDiagnosticRecord& record) {
    std::ostringstream stream;
    stream << "{\"kind\":\"executor\",\"event\":\"" << ExecutorEventName(record.kind) << '"';
    if (!record.stage.empty()) AppendJsonField(stream, "stage", record.stage);
    if (!record.reason.empty()) AppendJsonField(stream, "reason", record.reason);
    if (!record.targetMode.empty()) AppendJsonField(stream, "target_mode", record.targetMode);
    if (!record.detail.empty()) AppendJsonField(stream, "detail", Utf8(record.detail));
    if (!record.programPath.empty()) AppendJsonField(stream, "program_path", Utf8(record.programPath));
    if (!record.targetSelector.empty()) AppendJsonField(stream, "target_selector", Utf8(record.targetSelector));
    if (!record.excludedProcessSelector.empty()) {
        AppendJsonField(stream, "excluded_process", Utf8(record.excludedProcessSelector));
    }
    if (!record.imagePath.empty()) AppendJsonField(stream, "image_path", Utf8(record.imagePath));
    if (record.pid != 0U) stream << ",\"pid\":" << record.pid;
    if (record.code != 0U) stream << ",\"code\":" << record.code;
    if (record.win32Error != 0U) stream << ",\"win32_error\":" << record.win32Error;
    if (record.kind == ExecutorDiagnosticKind::SessionStop) stream << ",\"exit_code\":" << record.exitCode;
    if (record.kind == ExecutorDiagnosticKind::TargetSearchAmbiguous) {
        stream << ",\"match_count\":" << record.matchCount;
    }
    if (record.kind == ExecutorDiagnosticKind::SessionStart) {
        stream << ",\"trace_input\":" << (record.traceInput ? "true" : "false")
               << ",\"dry_run\":" << (record.dryRun ? "true" : "false")
               << ",\"allow_exec\":" << (record.allowExec ? "true" : "false")
               << ",\"debug\":" << (record.debug ? "true" : "false");
    }
    stream << '}';
    return stream.str();
}

DiagnosticLog::~DiagnosticLog() {
    Stop();
}

bool DiagnosticLog::Start(
    const std::wstring& jsonlPath,
    std::wstring& errorMessage,
    std::uint64_t maximumJsonlBytes) {
    if (worker_.joinable()) {
        errorMessage = L"The diagnostic worker is already running.";
        return false;
    }
    maximumJsonlBytes_ = maximumJsonlBytes;
    enabled_.store(false, std::memory_order_release);
    if (jsonlPath.empty()) {
        return true;
    }
    droppedHookRecords_.store(0U, std::memory_order_relaxed);
    droppedInjectionRecords_.store(0U, std::memory_order_relaxed);
    jsonlBytesWritten_.store(0U, std::memory_order_relaxed);
    jsonlTruncated_.store(false, std::memory_order_relaxed);
    sessionId_ = MakeSessionId();

    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (wakeEvent_ == nullptr || stopEvent_ == nullptr || readyEvent_ == nullptr) {
        const DWORD error = GetLastError();
        Stop();
        errorMessage = L"Cannot create diagnostic events. Win32 error " + std::to_wstring(error) + L".";
        return false;
    }

    jsonlFile_ = CreateFileW(jsonlPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (jsonlFile_ == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        Stop();
        errorMessage = L"Cannot create the diagnostic JSONL file. Win32 error " + std::to_wstring(error) + L".";
        return false;
    }

    try {
        worker_ = std::thread(&DiagnosticLog::WorkerMain, this);
    } catch (...) {
        Stop();
        errorMessage = L"Cannot create the diagnostic worker thread.";
        return false;
    }
    if (WaitForSingleObject(readyEvent_, 5000) != WAIT_OBJECT_0) {
        Stop();
        errorMessage = L"The diagnostic worker did not become ready within five seconds.";
        return false;
    }
    enabled_.store(true, std::memory_order_release);
    return true;
}

void DiagnosticLog::Stop() noexcept {
    StopImpl(nullptr);
}

void DiagnosticLog::Stop(const ExecutorDiagnosticRecord& finalRecord) noexcept {
    StopImpl(&finalRecord);
}

void DiagnosticLog::StopImpl(const ExecutorDiagnosticRecord* finalRecord) noexcept {
    enabled_.store(false, std::memory_order_release);
    if (stopEvent_ != nullptr) {
        SetEvent(stopEvent_);
    }
    if (wakeEvent_ != nullptr) {
        SetEvent(wakeEvent_);
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (finalRecord != nullptr && jsonlFile_ != INVALID_HANDLE_VALUE) {
        try {
            EmitLine(
                FormatExecutorDiagnosticJson(*finalRecord),
                UnixMilliseconds());
        } catch (...) {
            OutputDebugStringA("InputWeaver final diagnostic formatting failed.\n");
        }
    }
    if (jsonlFile_ != INVALID_HANDLE_VALUE) {
        CloseHandle(jsonlFile_);
        jsonlFile_ = INVALID_HANDLE_VALUE;
    }
    if (wakeEvent_ != nullptr) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (readyEvent_ != nullptr) {
        CloseHandle(readyEvent_);
        readyEvent_ = nullptr;
    }
}

bool DiagnosticLog::TryPushHook(HookDiagnosticRecord record) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) {
        return true;
    }
    ApplyPrivacyRedaction(record);
    const TimestampedDiagnosticRecord<HookDiagnosticRecord> timestamped{
        record,
        UnixMilliseconds()};
    if (!hookRing_.TryPush(timestamped)) {
        droppedHookRecords_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    SetEvent(wakeEvent_);
    return true;
}

bool DiagnosticLog::TryPushInjection(const InjectionDiagnosticRecord& record) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) {
        return true;
    }
    const TimestampedDiagnosticRecord<InjectionDiagnosticRecord> timestamped{
        record,
        UnixMilliseconds()};
    if (!injectionRing_.TryPush(timestamped)) {
        droppedInjectionRecords_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    SetEvent(wakeEvent_);
    return true;
}

bool DiagnosticLog::TryPushRuntime(const RuntimeDiagnosticRecord& record) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) {
        return true;
    }
    const TimestampedDiagnosticRecord<RuntimeDiagnosticRecord> timestamped{
        record,
        UnixMilliseconds()};
    if (!runtimeRing_.TryPush(timestamped)) {
        return false;
    }
    SetEvent(wakeEvent_);
    return true;
}

bool DiagnosticLog::WriteExecutor(const ExecutorDiagnosticRecord& record) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) return true;
    try {
        EmitLine(FormatExecutorDiagnosticJson(record), UnixMilliseconds());
    } catch (...) {
        OutputDebugStringA("InputWeaver executor diagnostic formatting failed.\n");
        return false;
    }
    return true;
}

bool DiagnosticLog::Enabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
}

std::uint64_t DiagnosticLog::DroppedHookRecords() const noexcept {
    return droppedHookRecords_.load(std::memory_order_relaxed);
}

std::uint64_t DiagnosticLog::DroppedInjectionRecords() const noexcept {
    return droppedInjectionRecords_.load(std::memory_order_relaxed);
}

std::uint64_t DiagnosticLog::DroppedRuntimeRecords() const noexcept {
    return runtimeRing_.RejectedPushCount();
}

std::uint64_t DiagnosticLog::JsonlBytesWritten() const noexcept {
    return jsonlBytesWritten_.load(std::memory_order_relaxed);
}

bool DiagnosticLog::JsonlTruncated() const noexcept {
    return jsonlTruncated_.load(std::memory_order_relaxed);
}

void DiagnosticLog::WorkerMain() noexcept {
    const HANDLE handles[] = {stopEvent_, wakeEvent_};
    SetEvent(readyEvent_);
    for (;;) {
        DrainRecords();
        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0
            && hookRing_.Empty() && injectionRing_.Empty() && runtimeRing_.Empty()) {
            break;
        }
        WaitForMultipleObjects(2, handles, FALSE, 250);
    }
}

void DiagnosticLog::DrainRecords() noexcept {
    TimestampedDiagnosticRecord<HookDiagnosticRecord> hookRecord;
    while (hookRing_.TryPop(hookRecord)) {
        try {
            EmitLine(
                FormatHookDiagnosticJson(hookRecord.record),
                hookRecord.timeUnixMilliseconds);
        } catch (...) {
            OutputDebugStringA("InputWeaver diagnostic formatting failed.\n");
        }
    }

    TimestampedDiagnosticRecord<InjectionDiagnosticRecord> injectionRecord;
    while (injectionRing_.TryPop(injectionRecord)) {
        try {
            EmitLine(
                FormatInjectionDiagnosticJson(injectionRecord.record),
                injectionRecord.timeUnixMilliseconds);
        } catch (...) {
            OutputDebugStringA("InputWeaver injection diagnostic formatting failed.\n");
        }
    }


    TimestampedDiagnosticRecord<RuntimeDiagnosticRecord> runtimeRecord;
    while (runtimeRing_.TryPop(runtimeRecord)) {
        try {
            EmitLine(
                FormatRuntimeDiagnosticJson(runtimeRecord.record),
                runtimeRecord.timeUnixMilliseconds);
        } catch (...) {
            OutputDebugStringA("InputWeaver runtime diagnostic formatting failed.\n");
        }
    }

}

void DiagnosticLog::EmitLine(
    const std::string& line,
    std::uint64_t timeUnixMilliseconds) {
    const std::lock_guard lock(writeMutex_);
    std::ostringstream envelope;
    envelope << "{\"schema\":\"inputweaver.diagnostic\",\"schema_version\":"
             << kDiagnosticSchemaVersion << ",\"session_id\":";
    AppendJsonString(envelope, sessionId_);
    envelope << ",\"time_unix_ms\":" << timeUnixMilliseconds;
    if (line.size() >= 2U && line.front() == '{' && line.back() == '}') {
        envelope << ',' << std::string_view{line}.substr(1U);
    } else {
        envelope << ",\"kind\":\"invalid\"}";
    }
    std::string terminated = envelope.str();
    terminated.push_back('\n');
    OutputDebugStringA(terminated.c_str());

    if (jsonlFile_ == INVALID_HANDLE_VALUE || jsonlTruncated_.load(std::memory_order_relaxed)) {
        return;
    }

    const std::uint64_t current = jsonlBytesWritten_.load(std::memory_order_relaxed);
    if (!JsonlAppendFits(current, terminated.size(), maximumJsonlBytes_)) {
        jsonlTruncated_.store(true, std::memory_order_relaxed);
        OutputDebugStringA("InputWeaver diagnostic JSONL limit reached.\n");
        return;
    }

    DWORD written = 0;
    if (!WriteFile(jsonlFile_, terminated.data(), static_cast<DWORD>(terminated.size()), &written, nullptr) ||
        written != terminated.size()) {
        jsonlTruncated_.store(true, std::memory_order_relaxed);
        OutputDebugStringA("InputWeaver diagnostic JSONL write failed.\n");
        return;
    }
    jsonlBytesWritten_.store(current + written, std::memory_order_relaxed);
}

}  // namespace inputweaver
