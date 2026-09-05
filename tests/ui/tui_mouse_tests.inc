void TestMouseDebugRendering(inputweaver::ui::tui::TuiController& controller, FakePlatform& platform)
{
    using namespace inputweaver;
    const auto previous = platform.debugState;
    const auto capture = test::MouseDebugCapture();
    auto state = std::make_shared<debug::DebugClientState>();
    state->connected = state->capturing = state->captureTrusted = true;
    state->captureEpoch = 1;
    state->meters = capture.meters;
    state->mouse = capture.mouse;
    debug::DebugInputEvent event{};
    event.inputSequence = 4;
    event.captureUnixTimeMilliseconds = capture.captureUnixTimeMilliseconds;
    event.control.device = DeviceKind::Mouse;
    event.transition = Transition::Move;
    event.disposition = debug::InputDisposition::Forward;
    event.position = {-73, 20};
    event.delta.dx = 27;
    event.occurrence = {MeterId{0}, capture.mouse.meters[0].completed};
    debug::DebugRuleExecution execution{};
    execution.executionMarker = 7;
    execution.triggerInput = event;
    execution.selectionCount = 3;
    for (const auto& source : capture.mouse.meters) execution.completedMeters.push_back(source.completed);
    execution.actionText = "wait(10ms) move_by(@path.dx, @path.dy)";
    state->ruleExecutions.push_back(execution);
    state->mouse.meters[0].completed.sequence = 19;
    platform.debugState = state;
    controller.Tick();
    const auto wide = CanvasText(controller.Render(360, 90));
    Check(wide.find("Mouse pos(-73,20) d(0,0) wheel(0,0.25) moving idle(0.012s)") != std::string::npos,
        "STATE groups Mouse coordinates, deltas, wheel axes, and idle time");
    Check(wide.find("path 3/24 start(-76,20) pos(-73,20) d(3,0)") != std::string::npos
        && wide.find("pulse 20ms/100ms") != std::string::npos && wide.find("vertical empty") != std::string::npos,
        "meters show compact progress/period pairs and empty completed views");
    Check(wide.find("path (tick)") != std::string::npos
        && wide.find("[@path 24 start(-100,20) pos(-76,20) d(24,0)]") != std::string::npos,
        "actions name the triggering meter while STATE owns its grouped data");
    const auto actions = wide.substr(wide.find("ACTION EXECUTIONS"));
    Check(actions.find("start(") == std::string::npos && wide.find("input=#") == std::string::npos
        && wide.find("cycle=#") == std::string::npos,
        "the action view shows trigger, condition, and action without expanded snapshots or transport identities");
    const auto narrow = CanvasText(controller.Render(80, 150));
    Check(narrow.find("start(-100,20)") != std::string::npos && narrow.find("path 3/24") != std::string::npos
        && narrow.find("vertical -0.25/1") != std::string::npos, "narrow Debug viewports retain compact meter groups");
    state->ruleExecutions[0].completedMeters.resize(1);
    state->ruleExecutions[0].triggerInput.occurrence = {};
    platform.debugState = state;
    controller.Tick();
    const auto raw = CanvasText(controller.Render(160, 70));
    Check(raw.find("Mouse (move)") != std::string::npos && raw.find("loading source selections") == std::string::npos,
        "raw mouse triggers use a short parenthesized name independently of snapshot arrival");
    const std::array<std::pair<std::int64_t, std::string_view>, 6> idleCases{{
        {0, "idle(0s)"},
        {123'456, "idle(0.000123456s)"},
        {12'000'000, "idle(0.012s)"},
        {1'000'000'000, "idle(1s)"},
        {60'000'000'000, "idle(60s)"},
        {(std::numeric_limits<std::int64_t>::max)(), "idle(9.22337e+09s)"},
    }};
    for (const auto& [nanoseconds, expected] : idleCases) {
        state->mouse.mouse.idleTime.nanoseconds = nanoseconds;
        platform.debugState = state;
        controller.Tick();
        Check(CanvasText(controller.Render(160, 70)).find(expected) != std::string::npos,
            "Mouse idle time consistently uses seconds from fractional intervals through the duration limit");
    }
    platform.debugState = previous;
    controller.Tick();
}
