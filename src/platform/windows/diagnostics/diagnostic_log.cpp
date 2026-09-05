#include "diagnostic_log.hpp"

#include <sstream>
#include <iomanip>
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

bool DiagnosticLog::TryPushRuntime(const RuntimeDiagnosticRecord& record) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!runtimeRing_.TryPush(record)) {
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


    RuntimeDiagnosticRecord runtimeRecord;
    while (runtimeRing_.TryPop(runtimeRecord)) {
        try {
            EmitLine(FormatRuntimeDiagnosticJson(runtimeRecord));
        } catch (...) {
            OutputDebugStringA("InputWeaver runtime diagnostic formatting failed.\n");
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
