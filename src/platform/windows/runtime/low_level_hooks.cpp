#include "low_level_hooks.hpp"

#include "input_classifier.hpp"

namespace inputweaver {
namespace {

inline constexpr UINT kWakeMessage = WM_APP + 1U;

std::atomic<LowLevelHooks*> gActiveHooks{nullptr};

[[nodiscard]] std::int64_t ReadPerformanceCounter() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

[[nodiscard]] bool NormalizeKeyboardMessage(
    WPARAM message,
    const KBDLLHOOKSTRUCT& source,
    SelfTag selfTag,
    InputEvent& event) noexcept {
    switch (message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            event.transition = Transition::Down;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            event.transition = Transition::Up;
            break;
        default:
            return false;
    }

    event.device = DeviceKind::Keyboard;
    event.origin = ClassifyKeyboard(source, selfTag);
    event.code = static_cast<ControlCode>(source.vkCode);
    event.scanCode = static_cast<ScanCode>(source.scanCode);
    event.flags = static_cast<RawInputFlags>(source.flags);
    event.timestamp = static_cast<InputTimestamp>(source.time);
    event.extraInfo = static_cast<InputExtraInfo>(source.dwExtraInfo);
    return true;
}

[[nodiscard]] bool NormalizeMouseMessage(
    WPARAM message,
    const MSLLHOOKSTRUCT& source,
    SelfTag selfTag,
    InputEvent& event) noexcept {
    event.device = DeviceKind::Mouse;
    event.origin = ClassifyMouse(source, selfTag);
    event.flags = static_cast<RawInputFlags>(source.flags);
    event.mouseData = static_cast<MouseData>(source.mouseData);
    event.position = {
        static_cast<InputCoordinate>(source.pt.x),
        static_cast<InputCoordinate>(source.pt.y)};
    event.timestamp = static_cast<InputTimestamp>(source.time);
    event.extraInfo = static_cast<InputExtraInfo>(source.dwExtraInfo);

    switch (message) {
        case WM_LBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = control::kMouseLeft;
            return true;
        case WM_LBUTTONUP:
            event.transition = Transition::Up;
            event.code = control::kMouseLeft;
            return true;
        case WM_RBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = control::kMouseRight;
            return true;
        case WM_RBUTTONUP:
            event.transition = Transition::Up;
            event.code = control::kMouseRight;
            return true;
        case WM_MBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = control::kMouseMiddle;
            return true;
        case WM_MBUTTONUP:
            event.transition = Transition::Up;
            event.code = control::kMouseMiddle;
            return true;
        case WM_XBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = HIWORD(source.mouseData) == XBUTTON1
                ? control::kMouseX1
                : control::kMouseX2;
            return true;
        case WM_XBUTTONUP:
            event.transition = Transition::Up;
            event.code = HIWORD(source.mouseData) == XBUTTON1
                ? control::kMouseX1
                : control::kMouseX2;
            return true;
        case WM_MOUSEMOVE:
            event.transition = Transition::Move;
            return true;
        case WM_MOUSEWHEEL:
            event.transition = Transition::VerticalWheel;
            return true;
        case WM_MOUSEHWHEEL:
            event.transition = Transition::HorizontalWheel;
            return true;
        default:
            return false;
    }
}

}  // namespace

LowLevelHooks::LowLevelHooks(
    SelfTag selfTag,
    LowLevelInputSink& sink,
    TargetProcessContext* targetContext,
    StopRequest stopRequest,
    std::atomic<bool>& shutdownRequested,
    HANDLE shutdownEvent,
    HANDLE producerDoneEvent) noexcept
    : selfTag_(selfTag),
      sink_(sink),
      targetContext_(targetContext),
      stopRequest_(stopRequest),
      shutdownRequested_(shutdownRequested),
      shutdownEvent_(shutdownEvent),
      producerDoneEvent_(producerDoneEvent) {}

LowLevelHooks::~LowLevelHooks() {
    if (thread_.joinable()) {
        stopRequest_.Request();
        Wake();
        Wait();
    }
    CloseEvents();
}

bool LowLevelHooks::Start(std::wstring& errorMessage) {
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        errorMessage = L"The low-level hooks have already been started.";
        return false;
    }
    if (selfTag_ == 0 || shutdownEvent_ == nullptr || producerDoneEvent_ == nullptr) {
        errorMessage = L"The low-level hook dependencies are invalid.";
        return false;
    }
    if (!CreateEvents(errorMessage)) {
        return false;
    }

    try {
        thread_ = std::thread(&LowLevelHooks::ThreadMain, this);
    } catch (...) {
        SetEvent(producerDoneEvent_);
        errorMessage = L"Cannot create the low-level hook thread.";
        return false;
    }

    if (WaitForSingleObject(readyEvent_, 10000) != WAIT_OBJECT_0) {
        stopRequest_.Request();
        Wake();
        Wait();
        errorMessage = L"The low-level hook thread did not become ready within ten seconds.";
        return false;
    }

    const DWORD startupError = startupError_.load(std::memory_order_acquire);
    if (startupError != ERROR_SUCCESS) {
        stopRequest_.Request();
        Wake();
        Wait();
        errorMessage = L"Cannot start the low-level hooks. Win32 error " +
                       std::to_wstring(startupError) + L".";
        return false;
    }
    return true;
}

void LowLevelHooks::Wake() noexcept {
    const DWORD threadId = threadId_.load(std::memory_order_acquire);
    if (threadId != 0) {
        PostThreadMessageW(threadId, kWakeMessage, 0, 0);
    }
}

void LowLevelHooks::Wait() noexcept {
    if (thread_.joinable()) {
        thread_.join();
    }
}

HANDLE LowLevelHooks::StoppedEvent() const noexcept {
    return stoppedEvent_;
}

LRESULT CALLBACK LowLevelHooks::KeyboardHookProcedure(
    int code,
    WPARAM wParam,
    LPARAM lParam) noexcept {
    LowLevelHooks* hooks = gActiveHooks.load(std::memory_order_acquire);
    if (hooks == nullptr || code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    return hooks->HandleKeyboardHook(code, wParam, lParam);
}

LRESULT CALLBACK LowLevelHooks::MouseHookProcedure(
    int code,
    WPARAM wParam,
    LPARAM lParam) noexcept {
    LowLevelHooks* hooks = gActiveHooks.load(std::memory_order_acquire);
    if (hooks == nullptr || code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    return hooks->HandleMouseHook(code, wParam, lParam);
}

void CALLBACK LowLevelHooks::ForegroundEventProcedure(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND window,
    LONG objectId,
    LONG childId,
    DWORD eventThread,
    DWORD eventTime) noexcept
{
    (void)hook;
    (void)window;
    (void)objectId;
    (void)childId;
    (void)eventThread;
    (void)eventTime;
    if (event != EVENT_SYSTEM_FOREGROUND) {
        return;
    }
    LowLevelHooks* hooks = gActiveHooks.load(std::memory_order_acquire);
    if (hooks != nullptr) {
        hooks->HandleForegroundChange();
    }
}

LRESULT LowLevelHooks::HandleKeyboardHook(int code, WPARAM wParam, LPARAM lParam) noexcept {
    const std::int64_t startCounter = ReadPerformanceCounter();
    const auto* source = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (source == nullptr) {
        return CallNextHookEx(keyboardHook_, code, wParam, lParam);
    }

    InputEvent event{};
    if (!NormalizeKeyboardMessage(wParam, *source, selfTag_, event)) {
        return CallNextHookEx(keyboardHook_, code, wParam, lParam);
    }
    const bool lowerIntegrity = (source->flags & LLKHF_LOWER_IL_INJECTED) != 0;
    return sink_.HandleInput(event, lowerIntegrity, startCounter) == InputDecision::Suppress
        ? 1
        : CallNextHookEx(keyboardHook_, code, wParam, lParam);
}

LRESULT LowLevelHooks::HandleMouseHook(int code, WPARAM wParam, LPARAM lParam) noexcept {
    const std::int64_t startCounter = ReadPerformanceCounter();
    const auto* source = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    if (source == nullptr) {
        return CallNextHookEx(mouseHook_, code, wParam, lParam);
    }

    InputEvent event{};
    if (!NormalizeMouseMessage(wParam, *source, selfTag_, event)) {
        return CallNextHookEx(mouseHook_, code, wParam, lParam);
    }
    const bool lowerIntegrity = (source->flags & LLMHF_LOWER_IL_INJECTED) != 0;
    return sink_.HandleInput(event, lowerIntegrity, startCounter) == InputDecision::Suppress
        ? 1
        : CallNextHookEx(mouseHook_, code, wParam, lParam);
}

void LowLevelHooks::HandleForegroundChange() noexcept
{
    sink_.SetTargetEligible(
        targetContext_ == nullptr || targetContext_->IsTargetForeground());
}

void LowLevelHooks::ThreadMain() noexcept {
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);
    MSG initialMessage{};
    PeekMessageW(&initialMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    LowLevelHooks* expected = nullptr;
    if (!gActiveHooks.compare_exchange_strong(
            expected, this, std::memory_order_acq_rel, std::memory_order_acquire)) {
        startupError_.store(ERROR_ALREADY_EXISTS, std::memory_order_release);
        SetEvent(readyEvent_);
        SetEvent(producerDoneEvent_);
        SetEvent(stoppedEvent_);
        return;
    }

    const HINSTANCE module = GetModuleHandleW(nullptr);
    keyboardHook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL, &LowLevelHooks::KeyboardHookProcedure, module, 0);
    if (keyboardHook_ == nullptr) {
        startupError_.store(GetLastError(), std::memory_order_release);
    } else {
        mouseHook_ = SetWindowsHookExW(
            WH_MOUSE_LL, &LowLevelHooks::MouseHookProcedure, module, 0);
        if (mouseHook_ == nullptr) {
            startupError_.store(GetLastError(), std::memory_order_release);
        }
    }

    if (startupError_.load(std::memory_order_acquire) == ERROR_SUCCESS
        && targetContext_ != nullptr
        && sink_.RequiresTargetEligibilityNotifications()) {
        foregroundHook_ = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            &LowLevelHooks::ForegroundEventProcedure,
            0U,
            0U,
            WINEVENT_OUTOFCONTEXT);
        if (foregroundHook_ == nullptr) {
            startupError_.store(GetLastError(), std::memory_order_release);
        }
    }

    if (startupError_.load(std::memory_order_acquire) == ERROR_SUCCESS) {
        if (sink_.RequiresTargetEligibilityNotifications()) {
            HandleForegroundChange();
        }
        if (!sink_.SeedActivatedPhysicalState()) {
            startupError_.store(ERROR_INVALID_STATE, std::memory_order_release);
        }
    }
    if (startupError_.load(std::memory_order_acquire) == ERROR_SUCCESS) {
        SeedObservedPhysicalState();
    } else {
        stopRequest_.Request();
    }
    SetEvent(readyEvent_);

    bool shuttingDown =
        startupError_.load(std::memory_order_acquire) != ERROR_SUCCESS;
    ShutdownGraceWindow shutdownGrace(kCapturedReleaseGraceMilliseconds);
    if (shuttingDown) {
        shutdownGrace.Begin(GetTickCount64());
    }

    while (!shuttingDown || sink_.HasCapturedInputs()) {
        if (!shuttingDown) {
            HANDLE handles[2] = {shutdownEvent_, nullptr};
            DWORD handleCount = 1;
            if (targetContext_ != nullptr && targetContext_->IsValid()) {
                handles[handleCount++] = targetContext_->TargetHandle();
            }
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(
                handleCount, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (waitResult == WAIT_OBJECT_0) {
                shuttingDown = true;
                shutdownGrace.Begin(GetTickCount64());
            } else if (handleCount == 2 && waitResult == WAIT_OBJECT_0 + 1) {
                stopRequest_.Request();
                shuttingDown = true;
                shutdownGrace.Begin(GetTickCount64());
            } else if (waitResult == WAIT_FAILED) {
                stopRequest_.Request();
                shuttingDown = true;
                shutdownGrace.Begin(GetTickCount64());
            }
        } else {
            const ULONGLONG now = GetTickCount64();
            if (shutdownGrace.Expired(now)) {
                break;
            }
            const DWORD remaining = shutdownGrace.RemainingSlice(now, 50);
            MsgWaitForMultipleObjectsEx(
                0, nullptr, remaining, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT || message.message == kWakeMessage) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!shuttingDown && shutdownRequested_.load(std::memory_order_acquire)) {
            shuttingDown = true;
            shutdownGrace.Begin(GetTickCount64());
        }
    }

    sink_.FlushDiagnostics(ReadPerformanceCounter());
    if (foregroundHook_ != nullptr) {
        UnhookWinEvent(foregroundHook_);
        foregroundHook_ = nullptr;
    }
    if (mouseHook_ != nullptr) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    expected = this;
    gActiveHooks.compare_exchange_strong(
        expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);
    threadId_.store(0, std::memory_order_release);
    SetEvent(producerDoneEvent_);
    SetEvent(stoppedEvent_);
}

void LowLevelHooks::SeedObservedPhysicalState() noexcept {
    struct KeyboardControlSeed final {
        int virtualKey;
        ControlCode control;
    };
    constexpr KeyboardControlSeed keyboardControls[] = {
        {VK_F6, control::kF6},
        {VK_F7, control::kF7},
        {VK_F8, control::kF8},
        {VK_F9, control::kF9},
        {VK_F10, control::kF10},
        {VK_F12, control::kF12},
        {VK_LCONTROL, control::kLeftControl},
        {VK_RCONTROL, control::kRightControl},
        {VK_LSHIFT, control::kLeftShift},
        {VK_RSHIFT, control::kRightShift}};
    for (const KeyboardControlSeed& seed : keyboardControls) {
        sink_.SeedPhysicalState(
            DeviceKind::Keyboard,
            seed.control,
            (GetAsyncKeyState(seed.virtualKey) & 0x8000) != 0);
    }
    sink_.SeedPhysicalState(
        DeviceKind::Mouse,
        control::kMouseMiddle,
        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
}

bool LowLevelHooks::CreateEvents(std::wstring& errorMessage) noexcept {
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stoppedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (readyEvent_ == nullptr || stoppedEvent_ == nullptr) {
        const DWORD error = GetLastError();
        CloseEvents();
        errorMessage = L"Cannot create low-level hook events. Win32 error " +
                       std::to_wstring(error) + L".";
        return false;
    }
    return true;
}

void LowLevelHooks::CloseEvents() noexcept {
    HANDLE* events[] = {&readyEvent_, &stoppedEvent_};
    for (HANDLE* event : events) {
        if (*event != nullptr) {
            CloseHandle(*event);
            *event = nullptr;
        }
    }
}

}  // namespace inputweaver
