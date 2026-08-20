#pragma once

#include "core/input_event.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>

namespace inputweaver {

class ActionScheduler;
class DiagnosticLog;
class LowLevelHooks;
class RemapEngine;
class TargetProcessContext;
struct RuntimeTestAccess;

struct AppRuntimeOptions final {
    bool mappingMode{};
    bool traceInput{};
    SelfTag selfTag{};
};

struct AppRuntimeMetrics final {
    std::uint64_t hookEvents{};
    std::uint64_t suppressedEvents{};
    std::uint64_t queuedBatches{};
    std::uint64_t cancelledBatches{};
    std::uint64_t rejectedActionPushes{};
    std::uint64_t injectionFailures{};
    std::uint64_t maximumHookMicroseconds{};
    std::uint64_t unresolvedSyntheticReleases{};
    bool circuitBreakerOpen{};
};

class AppRuntime final {
public:
    AppRuntime(
        AppRuntimeOptions options,
        TargetProcessContext* targetContext,
        DiagnosticLog& diagnosticLog) noexcept;
    ~AppRuntime();

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    bool Start(std::wstring& errorMessage);
    void RequestStop() noexcept;
    void Wait() noexcept;

    [[nodiscard]] HANDLE StoppedEvent() const noexcept;
    [[nodiscard]] HANDLE ActionQueueErrorEvent() const noexcept;
    [[nodiscard]] AppRuntimeMetrics Metrics() const noexcept;

private:
    friend struct RuntimeTestAccess;

    static void RequestStopThunk(void* context) noexcept;
    bool CreateShutdownEvent(std::wstring& errorMessage) noexcept;
    bool CreateComponents(std::wstring& errorMessage);

    AppRuntimeOptions options_;
    TargetProcessContext* targetContext_;
    DiagnosticLog& diagnosticLog_;
    HANDLE shutdownEvent_{nullptr};
    std::atomic<bool> started_{false};
    std::atomic<bool> shutdownRequested_{false};
    std::unique_ptr<RemapEngine> remapEngine_;
    std::unique_ptr<ActionScheduler> actionScheduler_;
    std::unique_ptr<LowLevelHooks> lowLevelHooks_;
};

}  // namespace inputweaver
