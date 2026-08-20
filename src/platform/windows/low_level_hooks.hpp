#pragma once

#include "core/input_event.hpp"
#include "core/stop_request.hpp"
#include "process_context.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <windows.h>

namespace inputweaver {

inline constexpr DWORD kCapturedReleaseGraceMilliseconds = 2000;

class ShutdownGraceWindow final {
public:
    explicit constexpr ShutdownGraceWindow(DWORD durationMilliseconds) noexcept
        : durationMilliseconds_(durationMilliseconds) {}

    constexpr void Begin(std::uint64_t nowMilliseconds) noexcept {
        deadlineMilliseconds_ = nowMilliseconds + durationMilliseconds_;
        active_ = true;
    }

    [[nodiscard]] constexpr bool Expired(std::uint64_t nowMilliseconds) const noexcept {
        return active_ && nowMilliseconds >= deadlineMilliseconds_;
    }

    [[nodiscard]] constexpr DWORD RemainingSlice(
        std::uint64_t nowMilliseconds,
        DWORD maximumSliceMilliseconds) const noexcept {
        if (!active_ || nowMilliseconds >= deadlineMilliseconds_) {
            return 0;
        }
        const std::uint64_t remaining = deadlineMilliseconds_ - nowMilliseconds;
        return static_cast<DWORD>((std::min)(
            remaining, static_cast<std::uint64_t>(maximumSliceMilliseconds)));
    }

private:
    DWORD durationMilliseconds_;
    std::uint64_t deadlineMilliseconds_{0};
    bool active_{false};
};

class LowLevelInputSink {
public:
    virtual ~LowLevelInputSink() = default;
    virtual InputDecision HandleInput(
        const InputEvent& event,
        bool lowerIntegrityInjected,
        std::int64_t startCounter) noexcept = 0;
    virtual void SeedPhysicalState(DeviceKind device, ControlCode code, bool down) noexcept = 0;
    virtual bool HasCapturedInputs() const noexcept = 0;
    virtual void FlushDiagnostics(std::int64_t startCounter) noexcept = 0;
};

class LowLevelHooks final {
public:
    LowLevelHooks(
        SelfTag selfTag,
        LowLevelInputSink& sink,
        TargetProcessContext* targetContext,
        StopRequest stopRequest,
        std::atomic<bool>& shutdownRequested,
        HANDLE shutdownEvent,
        HANDLE producerDoneEvent) noexcept;
    ~LowLevelHooks();

    LowLevelHooks(const LowLevelHooks&) = delete;
    LowLevelHooks& operator=(const LowLevelHooks&) = delete;

    bool Start(std::wstring& errorMessage);
    void Wake() noexcept;
    void Wait() noexcept;

    [[nodiscard]] HANDLE StoppedEvent() const noexcept;

private:
    static LRESULT CALLBACK KeyboardHookProcedure(int code, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK MouseHookProcedure(int code, WPARAM wParam, LPARAM lParam) noexcept;

    LRESULT HandleKeyboardHook(int code, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMouseHook(int code, WPARAM wParam, LPARAM lParam) noexcept;
    void ThreadMain() noexcept;
    void SeedObservedPhysicalState() noexcept;
    bool CreateEvents(std::wstring& errorMessage) noexcept;
    void CloseEvents() noexcept;

    SelfTag selfTag_;
    LowLevelInputSink& sink_;
    TargetProcessContext* targetContext_;
    StopRequest stopRequest_;
    std::atomic<bool>& shutdownRequested_;
    HANDLE shutdownEvent_;
    HANDLE producerDoneEvent_;

    HANDLE readyEvent_{nullptr};
    HANDLE stoppedEvent_{nullptr};
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<DWORD> startupError_{ERROR_SUCCESS};
    std::atomic<bool> started_{false};
    HHOOK keyboardHook_{nullptr};
    HHOOK mouseHook_{nullptr};
};

}  // namespace inputweaver
