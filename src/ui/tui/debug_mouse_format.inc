bool IsNumericMouseEvent(const debug::DebugInputEvent& event)
{
    return event.occurrence.source.IsValid() || (event.control.device == DeviceKind::Mouse
        && event.transition != Transition::Down && event.transition != Transition::Up);
}

std::string MousePointText(MousePoint point, std::string_view prefix = {})
{
    return std::string{prefix} + "x=" + DebugNumberText(point.x) + ' '
        + std::string{prefix} + "y=" + DebugNumberText(point.y);
}

std::string MousePeriodText(const RuntimeValue& period)
{
    return period.type == ExpressionType::Duration
        ? DebugValueText({ValueType::Duration, false, 0, period.durationValue}) : DebugNumberText(period.numberValue);
}

std::string MouseCycleText(const debug::DebugEventSource& source, const MouseCycle& cycle)
{
    std::string text = MousePointText(cycle.point);
    if (source.transition == EventTransition::Move) {
        text = MousePointText(cycle.start, "start_") + ' ' + text + ' '
            + MousePointText(cycle.displacement, "d") + " distance=" + DebugNumberText(cycle.distance);
    } else {
        text += " wheel_x=" + DebugNumberText(source.transition == EventTransition::HorizontalWheel ? cycle.progress : 0)
            + " wheel_y=" + DebugNumberText(source.transition == EventTransition::Wheel ? cycle.progress : 0);
    }
    return text + " period=" + MousePeriodText(cycle.period);
}

std::string CompletedMouseText(const debug::DebugEventSource& source, const MouseCycle& cycle)
{
    return source.name + (cycle.sequence == 0 ? " valid=off" : " valid=on cycle=#" + std::to_string(cycle.sequence)
        + ' ' + MouseCycleText(source, cycle));
}

std::string MouseEventText(const debug::DebugInputEvent& event, const debug::DebugClientState& state)
{
    const auto& occurrence = event.occurrence;
    const std::string parent = "input=#" + std::to_string(event.inputSequence);
    if (occurrence.source.value < state.eventSources.size()) {
        const auto& source = state.eventSources[occurrence.source.value];
        return source.name + ":tick cycle=#" + std::to_string(occurrence.cycle.sequence) + ' ' + parent
            + ' ' + MouseCycleText(source, occurrence.cycle);
    }
    const std::string_view transition = event.transition == Transition::Move ? "move"
        : event.transition == Transition::VerticalWheel ? "wheel" : "horizontalwheel";
    return "Mouse:" + std::string{transition} + ' ' + parent + ' ' + MousePointText(event.position)
        + ' ' + MousePointText({event.delta.dx, event.delta.dy}, "d")
        + " wheel_x=" + DebugNumberText(event.delta.wheelX) + " wheel_y=" + DebugNumberText(event.delta.wheelY)
        + ' ' + std::string{debug::InputOriginLabel(event.origin)} + ' ' + std::string{DispositionText(event.disposition)};
}

void AppendMouseStateCells(std::vector<std::string>& cells, const debug::DebugClientState& state)
{
    if (state.captureEpoch == 0) return;
    const auto& mouse = state.mouse.mouse;
    cells.push_back("[Mouse " + MousePointText(mouse.position) + ']');
    cells.push_back("[Mouse " + MousePointText({mouse.delta.dx, mouse.delta.dy}, "d")
        + " wheel_x=" + DebugNumberText(mouse.delta.wheelX) + " wheel_y=" + DebugNumberText(mouse.delta.wheelY) + ']');
    cells.push_back(std::string{"[Mouse moving="} + (mouse.moving ? "on" : "off") + " idle_time="
        + DebugValueText({ValueType::Duration, false, 0, mouse.idleTime}) + ']');
    for (std::size_t index = 0; index < state.eventSources.size() && index < state.mouse.sources.size(); ++index) {
        const auto& source = state.eventSources[index];
        const auto& view = state.mouse.sources[index];
        auto progress = view.current.period;
        progress.numberValue = view.current.progress;
        progress.durationValue.nanoseconds = view.current.elapsedNanoseconds;
        auto remaining = view.current.period;
        remaining.numberValue = (std::max)(0.0, remaining.numberValue - std::abs(progress.numberValue));
        remaining.durationValue.nanoseconds = (std::max)(std::int64_t{0},
            remaining.durationValue.nanoseconds - progress.durationValue.nanoseconds);
        std::string text = '[' + source.name + ' ' + MouseCycleText(source, view.current)
            + " progress=" + MousePeriodText(progress) + " remaining=" + MousePeriodText(remaining);
        if (source.transition == EventTransition::Move) text += view.moving ? " moving=on" : " moving=off";
        cells.push_back(text + ']');
        cells.push_back("[@" + CompletedMouseText(source, view.completed) + ']');
    }
}
