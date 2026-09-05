#include "program_runtime_internal.hpp"

namespace inputweaver {

std::uint64_t ProgramRuntime::Impl::BeginDebugExecution(State& state, const WorkItem& item,
    std::span<const MouseCycle> completed) noexcept
{
    if (!debugPort || item.debugCaptureEpoch == 0 || item.debugInputSequence == 0
        || item.debugRuleIndex == kInvalidProgramIndex) return 0;
    RuntimeDebugEvent event{};
    event.kind = RuntimeDebugEventKind::RuleMatched;
    event.captureEpoch = item.debugCaptureEpoch;
    event.executionMarker = state.scheduler.nextDebugExecutionMarker++;
    if (event.executionMarker == 0) return 0;
    event.triggerInputSequence = item.debugInputSequence;
    event.ruleIndex = item.debugRuleIndex;
    event.occurrence.source = item.debugMeter;
    event.occurrence.cycle.sequence = item.debugCycleSequence;
    event.selectionCount = static_cast<std::uint32_t>(completed.size());
    if (!debugPort->Publish(event)) return 0;
    event.kind = RuntimeDebugEventKind::ExecutionMeterSelected;
    for (std::uint32_t index = 0; index < completed.size(); ++index) {
        event.occurrence = {MeterId{index}, completed[index]};
        if (!debugPort->Publish(event)) return 0;
    }
    return event.executionMarker;
}

bool ProgramRuntime::ReadMouseSnapshot(RuntimeMouseSnapshot& snapshot) noexcept
{
    if (!impl_->active || !impl_->active->mutableState.mouse) return false;
    auto& state = *impl_->active;
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::unique_lock variableLock(state.mutableState.variableMutex);
    impl_->RefreshMouse(state);
    const auto& mouse = *state.mutableState.mouse;
    const auto sources = mouse.Meters();
    try {
        snapshot.meters.resize(sources.size());
    } catch (...) {
        return false;
    }
    snapshot.mouse = mouse.Observation();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto& source = sources[index];
        auto& view = snapshot.meters[index];
        view = {source.current, source.completed, source.moving};
        if (!source.hasOrigin) view.current.start = view.current.point = snapshot.mouse.position;
    }
    return true;
}

void ProgramRuntime::Impl::RefreshMouse(State& state) noexcept
{
    auto& values = state.mutableState;
    if (!values.mouse) return;
    const auto generation = state.generation.load(std::memory_order_acquire);
    if (values.mouseGeneration != generation) {
        values.mouse->ResetMeters();
        values.mouseGeneration = generation;
    }
    MousePoint position = values.mouse->Observation().position;
    ScreenPoint pointer{};
    if (routePort.QueryPointerPosition(pointer)) {
        position = {static_cast<double>(pointer.x), static_cast<double>(pointer.y)};
    }
    values.mouse->Refresh(position, clock.NowNanoseconds());
}

bool ProgramRuntime::Impl::UpdateMouseMeters(State& state, const RuntimeInputEvent& event) noexcept
{
    if (!state.mutableState.mouse) return true;
    struct PeriodContext { Impl& runtime; State& state; } context{*this, state};
    const MousePeriodEvaluator evaluator{&context, [](void* raw, MeterId source) noexcept {
        auto& evaluation = *static_cast<PeriodContext*>(raw);
        return evaluation.runtime.Evaluate(evaluation.state,
            evaluation.state.program->Meters()[source.value].period,
            evaluation.state.dispatch.expressionScratch);
    }};
    const bool complete = state.mutableState.mouse->Accumulate(event, clock.NowNanoseconds(), evaluator);
    if (!complete) {
        const auto sources = state.mutableState.mouse->Meters();
        bool periodFault = false;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            if (!sources[index].periodResult.Succeeded()) {
                periodFault = true;
                ReportExpressionFault(state, RuntimeDiagnosticKind::PredicateFault,
                    state.program->Meters()[index].period, sources[index].periodResult, false);
            }
        }
        if (!periodFault) {
            RequestFatal(state, RuntimeDiagnosticKind::TransactionCapacity, {}, kInvalidProgramIndex,
                kInvalidProgramIndex, 0);
        }
    }
    return complete;
}

bool ProgramRuntime::Impl::ExecutePointer(State& state, TaskInstance& task,
    const ActionInstruction& instruction) noexcept
{
    MutationTransaction transaction(*this, state, task);
    if (!transaction.current) return false;
    RuntimePointerOutput pointer{static_cast<PointerOperation>(instruction.operand0)};
    const auto argument = [&](std::uint32_t expression, double& value) {
        const auto result = Evaluate(state, ExpressionId{expression}, state.scheduler.expressionScratch, task.completed);
        if (!result.Succeeded()) {
            ReportExpressionFault(state, RuntimeDiagnosticKind::TaskExpressionFault,
                ExpressionId{expression}, result, false);
            return false;
        }
        value = result.value.numberValue;
        return true;
    };
    if (!argument(instruction.operand1, pointer.x)) return false;
    if (pointer.operation <= PointerOperation::MoveTo && !argument(instruction.operand2, pointer.y)) return false;
    transaction.Unlock();
    const std::lock_guard outputLock(state.output.mutex);
    return PublishOutput(state, {}, RuntimeOutputTransition::Down, task.generation, &task, nullptr, &pointer);
}

} // namespace inputweaver
