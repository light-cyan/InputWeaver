    bool AddMouseCycle(DebugInputEvent cycle, const DebugInputEvent& raw)
    {
        cycle.control = raw.control;
        cycle.origin = raw.origin;
        cycle.disposition = raw.disposition;
        cycle.transition = raw.transition;
        cycle.position = cycle.occurrence.cycle.point;
        if (!MaterializeForInput(cycle)) return false;
        AppendRecent(state.recentInputEvents, std::move(cycle), capacities.maximumInputEvents);
        return true;
    }

    bool MaterializeCycles(const DebugInputEvent& raw)
    {
        for (std::size_t index = 0; index < pendingCycles.size();) {
            if (pendingCycles[index].inputSequence != raw.inputSequence) {
                ++index;
                continue;
            }
            if (!AddMouseCycle(pendingCycles[index], raw)) return false;
            pendingCycles.erase(pendingCycles.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }

    DebugReductionAction AcceptMouseCycle(const Message& message)
    {
        const auto& payload = message.mouseCycleCompleted;
        if (payload.triggerInputSequence == 0 || payload.occurrence.source.value >= state.eventSources.size()
            || payload.occurrence.cycle.sequence == 0) return Recover(DebugClientFault::InconsistentState);
        if (capacities.maximumInputEvents == 0) return Recover(DebugClientFault::CapacityExceeded);
        DebugInputEvent cycle{};
        cycle.inputSequence = payload.triggerInputSequence;
        cycle.occurrence = payload.occurrence;
        cycle.captureTimeNanoseconds = message.header.captureTimeNanoseconds;
        if (!CaptureUnixTime(cycle.captureTimeNanoseconds, cycle.captureUnixTimeMilliseconds)) {
            return Recover(DebugClientFault::InconsistentState);
        }
        if (const auto* raw = FindInput(cycle.inputSequence)) {
            const auto parent = *raw;
            if (!AddMouseCycle(std::move(cycle), parent)) return Recover(DebugClientFault::CapacityExceeded);
        } else {
            if (cycle.inputSequence <= lastInputSequence || pendingCycles.size() >= capacities.maximumInputEvents) {
                return Recover(DebugClientFault::CapacityExceeded);
            }
            pendingCycles.push_back(std::move(cycle));
        }
        Publish();
        return DebugReductionAction::None;
    }

    DebugReductionAction AcceptSourceSelection(const Message& message)
    {
        const auto& selected = message.executionSourceSelected;
        DebugRuleExecution* execution = FindExecution(selected.executionMarker);
        if (!execution) execution = FindPending(selected.executionMarker);
        if (!execution) return Recover(DebugClientFault::UnknownExecutionMarker);
        if (selected.occurrence.source.value != execution->completedSources.size()
            || execution->completedSources.size() >= execution->selectionCount) {
            return Recover(DebugClientFault::InconsistentState);
        }
        execution->completedSources.push_back(selected.occurrence.cycle);
        Publish();
        return DebugReductionAction::None;
    }

    DebugReductionAction AcceptMouseState(const Message& message)
    {
        if (message.mouseState.sources.size() != state.eventSources.size()) {
            return Recover(DebugClientFault::InconsistentState);
        }
        state.mouse = message.mouseState;
        Publish();
        return DebugReductionAction::None;
    }
