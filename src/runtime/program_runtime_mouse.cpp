#include "program_runtime_internal.hpp"

namespace inputweaver {

void ProgramRuntime::Impl::RefreshMouse(State& state) noexcept
{
    auto& values = state.mutableState;
    if (!values.mouse) return;
    const auto generation = state.generation.load(std::memory_order_acquire);
    if (values.mouseGeneration != generation) {
        values.mouse->ResetSources();
        values.mouseGeneration = generation;
    }
    MousePoint position = values.mouse->Observation().position;
    ScreenPoint pointer{};
    if (routePort.QueryPointerPosition(pointer)) {
        position = {static_cast<double>(pointer.x), static_cast<double>(pointer.y)};
    }
    values.mouse->Refresh(position, clock.NowNanoseconds());
}

bool ProgramRuntime::Impl::UpdateMouseSources(State& state, const RuntimeInputEvent& event) noexcept
{
    if (!state.mutableState.mouse) return true;
    struct PeriodContext { Impl& runtime; State& state; } context{*this, state};
    const MousePeriodEvaluator evaluator{&context, [](void* raw, EventSourceId source) noexcept {
        auto& evaluation = *static_cast<PeriodContext*>(raw);
        return evaluation.runtime.Evaluate(evaluation.state,
            evaluation.state.program->EventSources()[source.value].period,
            evaluation.state.dispatch.expressionScratch);
    }};
    const bool complete = state.mutableState.mouse->Accumulate(event, clock.NowNanoseconds(), evaluator);
    if (!complete) {
        const auto sources = state.mutableState.mouse->Sources();
        bool periodFault = false;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            if (!sources[index].periodResult.Succeeded()) {
                periodFault = true;
                ReportExpressionFault(state, RuntimeDiagnosticKind::PredicateFault,
                    state.program->EventSources()[index].period, sources[index].periodResult, false);
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
