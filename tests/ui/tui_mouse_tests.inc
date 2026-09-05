void TestMouseDebugRendering(inputweaver::ui::tui::TuiController& controller, FakePlatform& platform)
{
    using namespace inputweaver;
    const auto previous = platform.debugState;
    const auto capture = test::MouseDebugCapture();
    auto state = std::make_shared<debug::DebugClientState>();
    state->connected = state->capturing = state->captureTrusted = true;
    state->captureEpoch = 1;
    state->eventSources = capture.sources;
    state->mouse = capture.mouse;
    debug::DebugInputEvent event{};
    event.inputSequence = 4;
    event.captureUnixTimeMilliseconds = capture.captureUnixTimeMilliseconds;
    event.control.device = DeviceKind::Mouse;
    event.transition = Transition::Move;
    event.disposition = debug::InputDisposition::Forward;
    event.position = {-73, 20};
    event.delta.dx = 27;
    state->recentInputEvents.push_back(event);
    event.occurrence = {EventSourceId{0}, capture.mouse.sources[0].completed};
    state->recentInputEvents.push_back(event);
    debug::DebugRuleExecution execution{};
    execution.executionMarker = 7;
    execution.triggerInput = event;
    execution.selectionCount = 3;
    for (const auto& source : capture.mouse.sources) execution.completedSources.push_back(source.completed);
    execution.actionText = "wait(10ms) move_by(@path.dx, @path.dy)";
    state->ruleExecutions.push_back(execution);
    state->mouse.sources[0].completed.sequence = 19;
    platform.debugState = state;
    controller.Tick();
    const auto wide = CanvasText(controller.Render(360, 90));
    Check(wide.find("Mouse x=-73 y=20") != std::string::npos && wide.find("wheel_y=0.25") != std::string::npos
        && wide.find("moving=on idle_time=12ms") != std::string::npos,
        "STATE renders actual Mouse position, latest deltas, and idle status");
    Check(wide.find("progress=3 remaining=21") != std::string::npos
        && wide.find("period=100ms progress=20ms remaining=80ms") != std::string::npos
        && wide.find("vertical valid=off") != std::string::npos,
        "named sources show distance/time phase and explicit completion validity");
    Check(wide.find("path:tick cycle=#12 input=#4") != std::string::npos
        && wide.find("path valid=on cycle=#12") != std::string::npos
        && wide.find("path valid=on cycle=#19") != std::string::npos,
        "EVENTS and ACTION EXECUTIONS identify the fixed older cycle independently of STATE");
    const auto narrow = CanvasText(controller.Render(80, 150));
    Check(narrow.find("start_x=-100") != std::string::npos && narrow.find("remaining=21") != std::string::npos
        && narrow.find("wheel_y=-0.25") != std::string::npos, "narrow Debug viewports wrap mouse fields without losing them");
    state->ruleExecutions[0].completedSources.resize(1);
    platform.debugState = state;
    controller.Tick();
    Check(CanvasText(controller.Render(160, 70)).find("loading source selections") != std::string::npos,
        "partial source records are not presented as a complete task selection");
    platform.debugState = previous;
    controller.Tick();
}
