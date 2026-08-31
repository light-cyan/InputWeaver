#include "program_runtime_internal.hpp"

namespace inputweaver {

ProgramRuntime::ProgramRuntime(
    RuntimeCapacities capacities,
    RuntimeControlPort& controlPort,
    RuntimeOutputPort& outputPort,
    RuntimeRoutePort& routePort,
    RuntimeProcessLauncher& processLauncher,
    RuntimeClock& clock,
    RuntimeDebugEventPort* debugPort,
    support::CallbackRef<void() noexcept> fatalStopRequest)
    : impl_(std::make_unique<Impl>(
          capacities,
          controlPort,
          outputPort,
          routePort,
          processLauncher,
          clock,
          debugPort,
          fatalStopRequest))
{
}

ProgramRuntime::~ProgramRuntime() = default;

RuntimeActivationResult ProgramRuntime::Activate(
    std::shared_ptr<const CompiledProgram> program,
    TargetSelectorKind targetKindOverride)
{
    return impl_->Activate(std::move(program), targetKindOverride);
}

void ProgramRuntime::Deactivate() noexcept
{
    impl_->Deactivate();
}

InputDecision ProgramRuntime::HandleInput(
    const RuntimeInputEvent& event) noexcept
{
    return impl_->HandleInput(event);
}

bool ProgramRuntime::SeedPhysicalState(
    ControlRefId control,
    bool down) noexcept
{
    return impl_->SeedPhysicalState(control, down);
}

bool ProgramRuntime::MarkPhysicalStateUnsynchronized(
    ControlRefId control) noexcept
{
    return impl_->MarkPhysicalStateUnsynchronized(control);
}

void ProgramRuntime::SetTargetEligible(bool eligible) noexcept
{
    impl_->SetTargetEligible(eligible);
}

RuntimePumpResult ProgramRuntime::Pump(std::size_t maximumSlices) noexcept
{
    return impl_->Pump(maximumSlices);
}

bool ProgramRuntime::StartTaskThread()
{
    return impl_->StartTaskThread();
}

void ProgramRuntime::StopTaskThread() noexcept
{
    impl_->StopTaskThread();
}

void ProgramRuntime::NotifyTargetLost() noexcept
{
    if (impl_->active != nullptr
        && impl_->active->targetEligible.load(std::memory_order_acquire)) {
        impl_->Invalidate(
            *impl_->active,
            RuntimeCancellationReason::TargetLoss);
    }
}

void ProgramRuntime::RequestShutdown() noexcept
{
    if (impl_->active != nullptr) {
        impl_->Invalidate(
            *impl_->active,
            RuntimeCancellationReason::Shutdown);
    }
}

bool ProgramRuntime::HasActiveProgram() const noexcept
{
    return impl_->active != nullptr;
}

bool ProgramRuntime::TargetEligible() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->targetEligible.load(std::memory_order_acquire);
}

bool ProgramRuntime::PauseOn() const noexcept
{
    return impl_->active != nullptr
        && (impl_->active->program->PauseControlBuckets().empty()
            || impl_->active->mutableState.PauseEnabled());
}

bool ProgramRuntime::ExitRequested() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->exitRequested.load(std::memory_order_acquire);
}

bool ProgramRuntime::FatalShutdownRequested() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->fatalShutdownRequested.load(std::memory_order_acquire);
}

std::uint64_t ProgramRuntime::Generation() const noexcept
{
    return impl_->observableGeneration.load(std::memory_order_acquire);
}

std::size_t ProgramRuntime::ActiveTaskCount() const noexcept
{
    return impl_->active == nullptr
        ? 0U
        : impl_->active->scheduler.ActiveTaskCount();
}

bool ProgramRuntime::HasActiveMappings() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->dispatch.HasActiveMappings();
}

bool ProgramRuntime::HasOwnedOutputs() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->output.HasOwnedOutputs();
}

RuntimeMetrics ProgramRuntime::Metrics() const noexcept
{
    if (impl_->active == nullptr) {
        return {};
    }
    const Impl::State& state = *impl_->active;
    return state.metrics.Snapshot(state.diagnostics.Dropped());
}

RuntimeEvaluationResult ProgramRuntime::EvaluateExpression(
    ExpressionId expression) noexcept
{
    if (impl_->active == nullptr) {
        RuntimeEvaluationResult result{};
        result.fault = RuntimeEvaluationFault::InvalidExpression;
        return result;
    }
    Impl::State& state = *impl_->active;
    const std::lock_guard inspectionLock(state.inspectionMutex);
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::shared_lock variableLock(state.mutableState.variableMutex);
    return impl_->Evaluate(
        state,
        expression,
        state.inspectionScratch);
}

bool ProgramRuntime::ReadUserState(
    std::uint32_t index,
    bool& value) const noexcept
{
    if (impl_->active == nullptr) {
        return false;
    }
    return impl_->active->mutableState.ReadUserState(index, value);
}

bool ProgramRuntime::TryPopDiagnostic(
    RuntimeDiagnosticRecord& record) noexcept
{
    return impl_->active != nullptr
        && impl_->active->diagnostics.TryPop(record);
}


} // namespace inputweaver
