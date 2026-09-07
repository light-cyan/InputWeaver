bool IsNumericMouseEvent(const debug::DebugInputEvent& event)
{
    return event.occurrence.source.IsValid() || (event.control.device == DeviceKind::Mouse
        && event.transition != Transition::Down && event.transition != Transition::Up);
}

std::string MouseNumberText(double value)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value;
    std::string text = output.str();
    if (text.ends_with(".0")) text.resize(text.size() - 2U);
    return text == "-0" ? "0" : text;
}

std::string MousePointText(MousePoint point)
{
    return '(' + MouseNumberText(point.x) + ',' + MouseNumberText(point.y) + ')';
}

std::string MousePeriodText(const RuntimeValue& period)
{
    if (period.type != ExpressionType::Duration) return MouseNumberText(period.numberValue);
    const auto nanoseconds = period.durationValue.nanoseconds;
    return nanoseconds >= 1'000'000'000 || nanoseconds <= -1'000'000'000
        ? MouseNumberText(static_cast<double>(nanoseconds) / 1'000'000'000.0) + 's'
        : MouseNumberText(static_cast<double>(nanoseconds) / 1'000'000.0) + "ms";
}

std::string MouseProgressText(const MouseCycle& cycle)
{
    auto progress = cycle.period;
    progress.numberValue = cycle.progress;
    progress.durationValue.nanoseconds = cycle.elapsedNanoseconds;
    return MousePeriodText(progress);
}

std::string MouseCycleText(const debug::DebugMeter& meter, const MouseCycle& cycle)
{
    const std::string position = "pos" + MousePointText(cycle.point);
    if (meter.transition != EventTransition::Move) return position;
    std::string text = "start" + MousePointText(cycle.start) + ' ' + position
        + " d" + MousePointText(cycle.displacement);
    if (cycle.period.type == ExpressionType::Duration) text += " dist(" + MouseNumberText(cycle.distance) + ')';
    return text;
}

std::string MouseEventText(const debug::DebugInputEvent& event, const debug::DebugClientState& state)
{
    const auto source = event.occurrence.source.value;
    if (source < state.meters.size()) return state.meters[source].name + " (tick)";
    const std::string_view transition = event.transition == Transition::Move ? "move"
        : event.transition == Transition::VerticalWheel ? "wheel" : "horizontalwheel";
    return "Mouse (" + std::string{transition} + ')';
}

void AppendMouseInputCells(StyledLine& cells, const debug::DebugClientState& state, TextStyle style)
{
    if (state.captureEpoch == 0) return;
    const auto& mouse = state.mouse.mouse;
    cells.push_back({"Mouse pos" + MousePointText(mouse.position), style});
    cells.push_back({"d" + MousePointText({mouse.delta.dx, mouse.delta.dy}), style});
    cells.push_back({"wheel" + MousePointText({mouse.delta.wheelX, mouse.delta.wheelY}), style});
    cells.push_back({mouse.moving ? "moving" : "idle", style});
    cells.push_back({"idle(" + MouseNumberText(static_cast<double>(mouse.idleTime.nanoseconds) / 1'000'000'000.0) + "s)", style});
}

void AppendMeterCells(std::vector<std::string>& cells, const debug::DebugClientState& state)
{
    if (state.captureEpoch == 0) return;
    for (std::size_t index = 0; index < state.meters.size() && index < state.mouse.meters.size(); ++index) {
        const auto& meter = state.meters[index];
        const auto& view = state.mouse.meters[index];
        cells.push_back('[' + meter.name + ' ' + MouseProgressText(view.current) + '/'
            + MousePeriodText(view.current.period) + ' ' + MouseCycleText(meter, view.current) + ']');
        cells.push_back("[@" + meter.name + (view.completed.sequence == 0 ? " empty"
            : ' ' + MouseProgressText(view.completed) + ' ' + MouseCycleText(meter, view.completed)) + ']');
    }
}
