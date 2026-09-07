#pragma once

#include "input/input_types.hpp"
#include "runtime/runtime_types.hpp"
#include "support/fixed_spsc_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace inputweaver {

inline constexpr std::size_t kHookDiagnosticCapacity = 4096;
inline constexpr std::size_t kInjectionDiagnosticCapacity = 512;
inline constexpr std::size_t kRuntimeDiagnosticCapacity = 512;
inline constexpr std::uint64_t kMaximumJsonlBytes = 8ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t kDiagnosticSchemaVersion = 1U;

enum class ExecutorDiagnosticKind : std::uint8_t {
    SessionStart,
    ConfigurationResolved,
    StartupFailure,
    TargetSearchStarted,
    TargetSearchWaiting,
    TargetSearchAmbiguous,
    TargetSearchFailure,
    TargetFound,
    TargetAttached,
    TargetAttachFailure,
    TargetLost,
    SessionStop
};

struct ExecutorDiagnosticRecord final {
    ExecutorDiagnosticKind kind{ExecutorDiagnosticKind::SessionStart};
    std::string stage;
    std::string reason;
    std::string targetMode;
    std::wstring detail;
    std::wstring programPath;
    std::wstring targetSelector;
    std::wstring excludedProcessSelector;
    std::wstring imagePath;
    std::uint32_t pid{};
    std::uint32_t code{};
    std::uint32_t win32Error{};
    std::uint32_t exitCode{};
    std::uint32_t matchCount{};
    bool traceInput{};
    bool dryRun{};
    bool allowExec{};
    bool debug{};
};

template <typename Record>
struct TimestampedDiagnosticRecord final {
    Record record{};
    std::uint64_t timeUnixMilliseconds{};
};

enum class DiagnosticControl : std::uint8_t {
    OtherKeyboard,
    OtherMouse,
    MouseMove,
    MouseWheel,
    F6,
    F7,
    F8,
    F9,
    F10,
    F12,
    Control,
    Shift,
    MiddleButton
};

enum class ExtraInfoCategory : std::uint8_t {
    Zero,
    OwnTag,
    OtherNonzero
};

struct HookDiagnosticRecord {
    std::uint64_t sequence{};
    std::int64_t qpcTimestamp{};
    std::uint32_t processingMicroseconds{};
    std::uint32_t foregroundPid{};
    std::uint32_t rawFlags{};
    std::uint32_t code{};
    std::uint32_t scanCode{};
    std::uint32_t mouseData{};
    DeviceKind device{DeviceKind::Keyboard};
    InputOrigin origin{InputOrigin::PhysicalCandidate};
    Transition transition{Transition::Down};
    DiagnosticControl control{DiagnosticControl::OtherKeyboard};
    ExtraInfoCategory extraInfo{ExtraInfoCategory::Zero};
    bool lowerIntegrityInjected{};
    bool suppressed{};
    ScreenPoint position{};
    MouseDelta delta{};
};

struct InjectionDiagnosticRecord {
    std::uint64_t sourceSequence{};
    std::uint64_t outputStateGeneration{};
    std::int64_t qpcTimestamp{};
    std::uint32_t targetPid{};
    std::uint32_t outputCode{};
    DeviceKind outputDevice{DeviceKind::Keyboard};
    Transition outputTransition{Transition::Down};
    DWORD win32Error{};
    std::uint32_t requested{};
    std::uint32_t sent{};
    bool cancelledForTarget{};
    bool cancelledForCircuitBreaker{};
    bool cancelledForShutdown{};
    bool cancelledForGeneration{};
    bool circuitBreakerOpen{};
    RuntimeOutputKind outputKind{RuntimeOutputKind::Control};
    RuntimePointerOutput pointer{};
    bool pointerPrepared{};
    ScreenPoint pointerOrigin{};
    ScreenPoint pointerDestination{};
    double preparedWheel{};
};

DiagnosticControl ClassifyDiagnosticControl(
    DeviceKind device,
    Transition transition,
    std::uint32_t code) noexcept;
ExtraInfoCategory CategorizeExtraInfo(
    std::uintptr_t extraInfo,
    std::uintptr_t selfTag) noexcept;
void ApplyPrivacyRedaction(HookDiagnosticRecord& record) noexcept;
[[nodiscard]] bool ShouldPublishHookDiagnostic(
    const HookDiagnosticRecord& record,
    bool traceInput) noexcept;
[[nodiscard]] bool ShouldPublishProgramHookDiagnostic(
    const HookDiagnosticRecord& record,
    bool traceInput,
    bool activatedControl) noexcept;
std::string FormatHookDiagnosticJson(const HookDiagnosticRecord& record);
std::string FormatInjectionDiagnosticJson(const InjectionDiagnosticRecord& record);
std::string FormatRuntimeDiagnosticJson(const RuntimeDiagnosticRecord& record);
std::string FormatExecutorDiagnosticJson(const ExecutorDiagnosticRecord& record);

[[nodiscard]] constexpr bool JsonlAppendFits(
    std::uint64_t currentBytes,
    std::uint64_t appendBytes,
    std::uint64_t maximumBytes = kMaximumJsonlBytes) noexcept {
    return currentBytes <= maximumBytes &&
           appendBytes <= maximumBytes - currentBytes;
}

class RuntimeDiagnosticRing final {
public:
    bool TryPush(
        const TimestampedDiagnosticRecord<RuntimeDiagnosticRecord>& value) noexcept {
        if (producerAdmission_.test_and_set(std::memory_order_acquire)) {
            rejectedPushCount_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        const bool accepted = ring_.TryPush(value);
        producerAdmission_.clear(std::memory_order_release);
        if (!accepted) {
            rejectedPushCount_.fetch_add(1U, std::memory_order_relaxed);
        }
        return accepted;
    }

    bool TryPop(
        TimestampedDiagnosticRecord<RuntimeDiagnosticRecord>& value) noexcept {
        return ring_.TryPop(value);
    }

    bool Empty() const noexcept {
        return ring_.Empty();
    }

    std::uint64_t RejectedPushCount() const noexcept {
        return rejectedPushCount_.load(std::memory_order_relaxed);
    }

private:
    std::atomic_flag producerAdmission_ = ATOMIC_FLAG_INIT;
    support::FixedSpscRing<
        TimestampedDiagnosticRecord<RuntimeDiagnosticRecord>,
        kRuntimeDiagnosticCapacity> ring_;
    std::atomic<std::uint64_t> rejectedPushCount_{0U};
};

class DiagnosticLog final {
public:
    DiagnosticLog() = default;
    ~DiagnosticLog();

    DiagnosticLog(const DiagnosticLog&) = delete;
    DiagnosticLog& operator=(const DiagnosticLog&) = delete;

    bool Start(
        const std::wstring& jsonlPath,
        std::wstring& errorMessage,
        std::uint64_t maximumJsonlBytes = kMaximumJsonlBytes);
    void Stop() noexcept;
    void Stop(const ExecutorDiagnosticRecord& finalRecord) noexcept;
    bool TryPushHook(HookDiagnosticRecord record) noexcept;
    bool TryPushInjection(const InjectionDiagnosticRecord& record) noexcept;
    bool TryPushRuntime(const RuntimeDiagnosticRecord& record) noexcept;
    bool WriteExecutor(const ExecutorDiagnosticRecord& record) noexcept;

    bool Enabled() const noexcept;
    std::uint64_t DroppedHookRecords() const noexcept;
    std::uint64_t DroppedInjectionRecords() const noexcept;
    std::uint64_t DroppedRuntimeRecords() const noexcept;
    std::uint64_t JsonlBytesWritten() const noexcept;
    bool JsonlTruncated() const noexcept;

private:
    void WorkerMain() noexcept;
    void DrainRecords() noexcept;
    void EmitLine(
        const std::string& line,
        std::uint64_t timeUnixMilliseconds);
    void StopImpl(const ExecutorDiagnosticRecord* finalRecord) noexcept;

    // The hook and injection rings each have one owning producer thread.
    support::FixedSpscRing<
        TimestampedDiagnosticRecord<HookDiagnosticRecord>,
        kHookDiagnosticCapacity> hookRing_;
    support::FixedSpscRing<
        TimestampedDiagnosticRecord<InjectionDiagnosticRecord>,
        kInjectionDiagnosticCapacity> injectionRing_;
    // Hook and output paths can both drain runtime records; producer admission is nonblocking.
    RuntimeDiagnosticRing runtimeRing_;
    std::mutex writeMutex_;
    std::atomic<std::uint64_t> droppedHookRecords_{0};
    std::atomic<std::uint64_t> droppedInjectionRecords_{0};
    std::atomic<std::uint64_t> jsonlBytesWritten_{0};
    std::atomic<bool> jsonlTruncated_{false};
    std::atomic<bool> enabled_{false};
    std::uint64_t maximumJsonlBytes_{kMaximumJsonlBytes};
    std::string sessionId_;
    HANDLE wakeEvent_{nullptr};
    HANDLE stopEvent_{nullptr};
    HANDLE readyEvent_{nullptr};
    HANDLE jsonlFile_{INVALID_HANDLE_VALUE};
    std::thread worker_;
};

}  // namespace inputweaver
