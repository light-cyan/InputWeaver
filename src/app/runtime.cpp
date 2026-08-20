#include "runtime.hpp"

#include "action_scheduler.hpp"
#include "remap_engine.hpp"
#include "core/stop_request.hpp"
#include "diagnostics/diagnostic_log.hpp"
#include "platform/windows/low_level_hooks.hpp"
#include "platform/windows/process_context.hpp"

#include <exception>

namespace inputweaver {

AppRuntime::AppRuntime(
    AppRuntimeOptions options,
    TargetProcessContext* targetContext,
    DiagnosticLog& diagnosticLog) noexcept
    : options_(options),
      targetContext_(targetContext),
      diagnosticLog_(diagnosticLog) {}

AppRuntime::~AppRuntime() {
    RequestStop();
    Wait();
    lowLevelHooks_.reset();
    actionScheduler_.reset();
    remapEngine_.reset();
    if (shutdownEvent_ != nullptr) {
        CloseHandle(shutdownEvent_);
        shutdownEvent_ = nullptr;
    }
}

bool AppRuntime::Start(std::wstring& errorMessage) {
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        errorMessage = L"The application runtime has already been started.";
        return false;
    }
    if (options_.selfTag == 0) {
        errorMessage = L"The self-injection tag must be nonzero.";
        return false;
    }
    if (options_.mappingMode &&
        (targetContext_ == nullptr || !targetContext_->IsValid())) {
        errorMessage = L"Mapping mode requires a validated target context.";
        return false;
    }
    if (!CreateComponents(errorMessage)) {
        return false;
    }
    if (!actionScheduler_->Start(errorMessage)) {
        RequestStop();
        actionScheduler_->NotifyProducerDone();
        Wait();
        return false;
    }
    if (!lowLevelHooks_->Start(errorMessage)) {
        RequestStop();
        actionScheduler_->NotifyProducerDone();
        Wait();
        return false;
    }
    return true;
}

void AppRuntime::RequestStop() noexcept {
    shutdownRequested_.store(true, std::memory_order_release);
    if (remapEngine_ != nullptr) {
        remapEngine_->DisableNewCaptures();
    }
    if (shutdownEvent_ != nullptr) {
        SetEvent(shutdownEvent_);
    }
    if (lowLevelHooks_ != nullptr) {
        lowLevelHooks_->Wake();
    }
}

void AppRuntime::Wait() noexcept {
    if (lowLevelHooks_ != nullptr) {
        lowLevelHooks_->Wait();
    } else if (actionScheduler_ != nullptr) {
        actionScheduler_->NotifyProducerDone();
    }
    if (actionScheduler_ != nullptr) {
        actionScheduler_->Wait();
    }
}

HANDLE AppRuntime::StoppedEvent() const noexcept {
    return lowLevelHooks_ == nullptr ? nullptr : lowLevelHooks_->StoppedEvent();
}

HANDLE AppRuntime::ActionQueueErrorEvent() const noexcept {
    return actionScheduler_ == nullptr
        ? nullptr
        : actionScheduler_->ActionQueueErrorEvent();
}

AppRuntimeMetrics AppRuntime::Metrics() const noexcept {
    const RemapEngineMetrics remap = remapEngine_ == nullptr
        ? RemapEngineMetrics{}
        : remapEngine_->Metrics();
    const ActionSchedulerMetrics actions = actionScheduler_ == nullptr
        ? ActionSchedulerMetrics{}
        : actionScheduler_->Metrics();
    return {
        remap.hookEvents,
        remap.suppressedEvents,
        actions.queuedBatches,
        actions.cancelledBatches,
        actions.rejectedActionPushes,
        actions.injectionFailures,
        remap.maximumHookMicroseconds,
        actions.unresolvedSyntheticReleases,
        actions.circuitBreakerOpen};
}

void AppRuntime::RequestStopThunk(void* context) noexcept {
    if (context != nullptr) {
        static_cast<AppRuntime*>(context)->RequestStop();
    }
}

bool AppRuntime::CreateShutdownEvent(std::wstring& errorMessage) noexcept {
    if (shutdownEvent_ != nullptr) {
        return true;
    }
    shutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (shutdownEvent_ != nullptr) {
        return true;
    }
    errorMessage = L"Cannot create the runtime shutdown event. Win32 error " +
                   std::to_wstring(GetLastError()) + L".";
    return false;
}

bool AppRuntime::CreateComponents(std::wstring& errorMessage) {
    if (remapEngine_ != nullptr && actionScheduler_ != nullptr &&
        lowLevelHooks_ != nullptr) {
        return true;
    }
    if (!CreateShutdownEvent(errorMessage)) {
        return false;
    }

    const StopRequest stopRequest{this, &AppRuntime::RequestStopThunk};
    try {
        remapEngine_ = std::make_unique<RemapEngine>(
            RemapEngineOptions{
                options_.mappingMode,
                options_.traceInput,
                options_.selfTag},
            targetContext_,
            diagnosticLog_,
            stopRequest,
            shutdownRequested_);
        actionScheduler_ = std::make_unique<ActionScheduler>(
            options_.selfTag,
            targetContext_,
            diagnosticLog_,
            *remapEngine_,
            stopRequest,
            shutdownEvent_);
        remapEngine_->AttachScheduler(*actionScheduler_);
        std::wstring schedulerError;
        if (!actionScheduler_->CreateEvents(schedulerError)) {
            errorMessage = schedulerError;
            return false;
        }
        lowLevelHooks_ = std::make_unique<LowLevelHooks>(
            options_.selfTag,
            *remapEngine_,
            targetContext_,
            stopRequest,
            shutdownRequested_,
            shutdownEvent_,
            actionScheduler_->ProducerDoneEvent());
    } catch (const std::exception&) {
        errorMessage = L"Cannot allocate the application runtime components.";
        return false;
    } catch (...) {
        errorMessage = L"Cannot create the application runtime components.";
        return false;
    }
    return true;
}

}  // namespace inputweaver
