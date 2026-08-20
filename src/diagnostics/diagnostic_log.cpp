#include "diagnostic_log.hpp"

#include <sstream>
#include <utility>

namespace inputweaver {
namespace {

const char* DeviceName(DeviceKind value) noexcept {
    return value == DeviceKind::Keyboard ? "Keyboard" : "Mouse";
}

const char* OriginName(InputOrigin value) noexcept {
    switch (value) {
        case InputOrigin::PhysicalCandidate:
            return "PhysicalCandidate";
        case InputOrigin::SelfInjected:
            return "SelfInjected";
        case InputOrigin::ExternalInjected:
            return "ExternalInjected";
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

const char* QueueResultName(QueueResult value) noexcept {
    switch (value) {
        case QueueResult::NotAttempted:
            return "NotAttempted";
        case QueueResult::Accepted:
            return "Accepted";
        case QueueResult::Rejected:
            return "Rejected";
        case QueueResult::CommitRejected:
            return "CommitRejected";
    }
    return "Unknown";
}

bool IsExactControl(DiagnosticControl value) noexcept {
    return value != DiagnosticControl::OtherKeyboard && value != DiagnosticControl::OtherMouse &&
           value != DiagnosticControl::MouseMove && value != DiagnosticControl::MouseWheel;
}

}  // namespace

DiagnosticControl ClassifyDiagnosticControl(
    DeviceKind device,
    Transition transition,
    ControlCode code) noexcept {
    if (device == DeviceKind::Mouse) {
        if (transition == Transition::Move) {
            return DiagnosticControl::MouseMove;
        }
        if (transition == Transition::VerticalWheel || transition == Transition::HorizontalWheel) {
            return DiagnosticControl::MouseWheel;
        }
        return code == control::kMouseMiddle
            ? DiagnosticControl::MiddleButton
            : DiagnosticControl::OtherMouse;
    }

    switch (code) {
        case control::kF6:
            return DiagnosticControl::F6;
        case control::kF7:
            return DiagnosticControl::F7;
        case control::kF8:
            return DiagnosticControl::F8;
        case control::kF9:
            return DiagnosticControl::F9;
        case control::kF10:
            return DiagnosticControl::F10;
        case control::kF12:
            return DiagnosticControl::F12;
        case control::kControl:
        case control::kLeftControl:
        case control::kRightControl:
            return DiagnosticControl::Control;
        case control::kShift:
        case control::kLeftShift:
        case control::kRightShift:
            return DiagnosticControl::Shift;
        default:
            return DiagnosticControl::OtherKeyboard;
    }
}

ExtraInfoCategory CategorizeExtraInfo(InputExtraInfo extraInfo, SelfTag selfTag) noexcept {
    if (extraInfo == 0) {
        return ExtraInfoCategory::Zero;
    }
    return static_cast<SelfTag>(extraInfo) == selfTag
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
           record.origin == InputOrigin::SelfInjected ||
           record.ruleId != 0 ||
           record.queueResult != QueueResult::NotAttempted ||
           record.suppressed ||
           (record.device == DeviceKind::Keyboard && record.code == control::kF12);
}

std::string FormatHookDiagnosticJson(const HookDiagnosticRecord& record) {
    std::ostringstream stream;
    stream << "{\"kind\":\"hook\",\"seq\":" << record.sequence << ",\"qpc\":" << record.qpcTimestamp
           << ",\"duration_us\":" << record.processingMicroseconds << ",\"aggregate_count\":" << record.aggregateCount
           << ",\"device\":\"" << DeviceName(record.device)
           << "\",\"transition\":\"" << TransitionName(record.transition) << "\",\"control\":\""
           << ControlName(record.control) << "\",\"code\":" << record.code << ",\"scan\":" << record.scanCode
           << ",\"flags\":" << record.rawFlags << ",\"mouse_data\":" << record.mouseData << ",\"origin\":\""
           << OriginName(record.origin) << "\",\"lower_il\":" << (record.lowerIntegrityInjected ? "true" : "false")
           << ",\"extra\":\"" << ExtraInfoName(record.extraInfo) << "\",\"foreground_pid\":" << record.foregroundPid
           << ",\"rule\":" << record.ruleId << ",\"queue\":\"" << QueueResultName(record.queueResult)
           << "\",\"suppressed\":" << (record.suppressed ? "true" : "false") << "}";
    return stream.str();
}

std::string FormatInjectionDiagnosticJson(const InjectionDiagnosticRecord& record) {
    std::ostringstream stream;
    stream << "{\"kind\":\"injection\",\"source_seq\":" << record.sourceSequence << ",\"qpc\":" << record.qpcTimestamp
           << ",\"target_pid\":" << record.targetPid << ",\"requested\":" << record.requested << ",\"sent\":"
           << record.sent << ",\"error\":" << record.win32Error << ",\"cleanup_requested\":" << record.cleanupRequested
           << ",\"cleanup_sent\":" << record.cleanupSent << ",\"cleanup_error\":" << record.cleanupError
           << ",\"cancelled_target\":"
           << (record.cancelledForTarget ? "true" : "false") << ",\"cancelled_physical\":"
           << (record.cancelledForPhysicalState ? "true" : "false") << ",\"cancelled_circuit\":"
           << (record.cancelledForCircuitBreaker ? "true" : "false") << ",\"cancelled_shutdown\":"
           << (record.cancelledForShutdown ? "true" : "false") << ",\"circuit_open\":"
           << (record.circuitBreakerOpen ? "true" : "false") << "}";
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
    if (!hookRing_.TryPush(record)) {
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
    if (!injectionRing_.TryPush(record)) {
        droppedInjectionRecords_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    SetEvent(wakeEvent_);
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
        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0 && hookRing_.Empty() && injectionRing_.Empty()) {
            break;
        }
        WaitForMultipleObjects(2, handles, FALSE, 250);
    }
}

void DiagnosticLog::DrainRecords() noexcept {
    HookDiagnosticRecord hookRecord;
    while (hookRing_.TryPop(hookRecord)) {
        try {
            EmitLine(FormatHookDiagnosticJson(hookRecord));
        } catch (...) {
            OutputDebugStringA("InputWeaver diagnostic formatting failed.\n");
        }
    }

    InjectionDiagnosticRecord injectionRecord;
    while (injectionRing_.TryPop(injectionRecord)) {
        try {
            EmitLine(FormatInjectionDiagnosticJson(injectionRecord));
        } catch (...) {
            OutputDebugStringA("InputWeaver injection diagnostic formatting failed.\n");
        }
    }
}

void DiagnosticLog::EmitLine(const std::string& line) {
    std::string terminated = line;
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
