#pragma once

#include "input/input_types.hpp"
#include "runtime/runtime_types.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>

namespace inputweaver {

class CompiledProgram;
class DiagnosticLog;
class TargetProcessContext;

struct WindowsProgramRuntimeSessionOptions final {
    bool traceInput{};
    SelfTag selfTag{};
};

struct WindowsProgramRuntimeSessionMetrics final {
    RuntimeMetrics runtime{};
    std::uint64_t hookEvents{};
    std::uint64_t queuedBatches{};
    std::uint64_t cancelledBatches{};
    std::uint64_t injectionFailures{};
    std::uint64_t maximumHookMicroseconds{};
    std::uint64_t forwardedOutsideTarget{};
    bool circuitBreakerOpen{};
};

class WindowsProgramRuntimeSession final {
public:
    WindowsProgramRuntimeSession(
        WindowsProgramRuntimeSessionOptions options,
        TargetProcessContext* targetContext,
        DiagnosticLog& diagnosticLog);
    ~WindowsProgramRuntimeSession();

    WindowsProgramRuntimeSession(const WindowsProgramRuntimeSession&) = delete;
    WindowsProgramRuntimeSession& operator=(const WindowsProgramRuntimeSession&) = delete;

    bool Start(
        std::shared_ptr<const CompiledProgram> program,
        std::wstring& errorMessage);
    void RequestStop() noexcept;
    void Wait() noexcept;

    [[nodiscard]] HANDLE StoppedEvent() const noexcept;
    [[nodiscard]] WindowsProgramRuntimeSessionMetrics Metrics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inputweaver
