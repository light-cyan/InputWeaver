#pragma once

#include "input/input_types.hpp"
#include "runtime/runtime_types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

enum class QueueResult : std::uint8_t {
    NotAttempted,
    Accepted,
    Rejected,
    CommitRejected
};

struct HookDiagnosticRecord {
    std::uint64_t sequence{};
    std::int64_t qpcTimestamp{};
    std::uint32_t processingMicroseconds{};
    std::uint32_t aggregateCount{1};
    ProcessId foregroundPid{};
    RawInputFlags rawFlags{};
    ControlCode code{};
    ScanCode scanCode{};
    MouseData mouseData{};
    DeviceKind device{DeviceKind::Keyboard};
    InputOrigin origin{InputOrigin::PhysicalCandidate};
    Transition transition{Transition::Down};
    DiagnosticControl control{DiagnosticControl::OtherKeyboard};
    ExtraInfoCategory extraInfo{ExtraInfoCategory::Zero};
    QueueResult queueResult{QueueResult::NotAttempted};
    std::uint32_t ruleId{};
    bool lowerIntegrityInjected{};
    bool suppressed{};
};

struct InjectionDiagnosticRecord {
    std::uint64_t sourceSequence{};
    std::uint64_t outputStateGeneration{};
    std::int64_t qpcTimestamp{};
    ProcessId targetPid{};
    ControlCode outputCode{};
    DeviceKind outputDevice{DeviceKind::Keyboard};
    Transition outputTransition{Transition::Down};
    DWORD win32Error{};
    DWORD cleanupError{};
    std::uint32_t requested{};
    std::uint32_t sent{};
    std::uint32_t cleanupRequested{};
    std::uint32_t cleanupSent{};
    bool cancelledForTarget{};
    bool cancelledForPhysicalState{};
    bool cancelledForCircuitBreaker{};
    bool cancelledForShutdown{};
    bool cancelledForGeneration{};
    bool circuitBreakerOpen{};
};

DiagnosticControl ClassifyDiagnosticControl(
    DeviceKind device,
    Transition transition,
    ControlCode code) noexcept;
ExtraInfoCategory CategorizeExtraInfo(InputExtraInfo extraInfo, SelfTag selfTag) noexcept;
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

[[nodiscard]] constexpr bool JsonlAppendFits(
    std::uint64_t currentBytes,
    std::uint64_t appendBytes,
    std::uint64_t maximumBytes = kMaximumJsonlBytes) noexcept {
    return currentBytes <= maximumBytes &&
           appendBytes <= maximumBytes - currentBytes;
}

template <typename T, std::size_t Capacity>
class SpscDiagnosticRing final {
public:
    static_assert(Capacity > 0);

    bool TryPush(const T& value) noexcept {
        const std::uint64_t write = writeIndex_.load(std::memory_order_relaxed);
        const std::uint64_t read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= Capacity) {
            return false;
        }
        records_[write % Capacity] = value;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& value) noexcept {
        const std::uint64_t read = readIndex_.load(std::memory_order_relaxed);
        const std::uint64_t write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        value = records_[read % Capacity];
        readIndex_.store(read + 1, std::memory_order_release);
        return true;
    }

    bool Empty() const noexcept {
        return readIndex_.load(std::memory_order_acquire) == writeIndex_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> records_{};
    alignas(64) std::atomic<std::uint64_t> writeIndex_{0};
    alignas(64) std::atomic<std::uint64_t> readIndex_{0};
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
    bool TryPushHook(HookDiagnosticRecord record) noexcept;
    bool TryPushInjection(const InjectionDiagnosticRecord& record) noexcept;
    bool TryPushRuntime(const RuntimeDiagnosticRecord& record) noexcept;

    bool Enabled() const noexcept;
    std::uint64_t DroppedHookRecords() const noexcept;
    std::uint64_t DroppedInjectionRecords() const noexcept;
    std::uint64_t DroppedRuntimeRecords() const noexcept;
    std::uint64_t JsonlBytesWritten() const noexcept;
    bool JsonlTruncated() const noexcept;

private:
    void WorkerMain() noexcept;
    void DrainRecords() noexcept;
    void EmitLine(const std::string& line);

    SpscDiagnosticRing<HookDiagnosticRecord, kHookDiagnosticCapacity> hookRing_;
    SpscDiagnosticRing<InjectionDiagnosticRecord, kInjectionDiagnosticCapacity> injectionRing_;
    SpscDiagnosticRing<RuntimeDiagnosticRecord, kRuntimeDiagnosticCapacity> runtimeRing_;
    std::atomic<std::uint64_t> droppedHookRecords_{0};
    std::atomic<std::uint64_t> droppedInjectionRecords_{0};
    std::atomic<std::uint64_t> droppedRuntimeRecords_{0};
    std::atomic<std::uint64_t> jsonlBytesWritten_{0};
    std::atomic<bool> jsonlTruncated_{false};
    std::atomic<bool> enabled_{false};
    std::uint64_t maximumJsonlBytes_{kMaximumJsonlBytes};
    HANDLE wakeEvent_{nullptr};
    HANDLE stopEvent_{nullptr};
    HANDLE readyEvent_{nullptr};
    HANDLE jsonlFile_{INVALID_HANDLE_VALUE};
    std::thread worker_;
};

}  // namespace inputweaver
