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
    state->values.push_back({"counter", {ValueType::Number, false, 42.0, {}}});
    debug::DebugPressedControl held{};
    held.control.virtualKey = 0xa2U;
    state->pressedControls.push_back(held);
    held.control.device = DeviceKind::Mouse;
    held.control.virtualKey = 0x01U;
    state->pressedControls.push_back(held);
    debug::DebugInputEvent event{};
    event.inputSequence = 4;
    event.captureUnixTimeMilliseconds = capture.captureUnixTimeMilliseconds;
    event.control.device = DeviceKind::Mouse;
    event.transition = Transition::Move;
    event.disposition = debug::InputDisposition::Forward;
    event.position = {-73, 20};
    event.delta.dx = 27;
    event.occurrence = {MeterId{0}, capture.mouse.meters[0].completed};
    event.repeatCount = 1000U;
    state->recentInputEvents.push_back(event);
    debug::DebugInputEvent repeated{};
    repeated.control.virtualKey = 65U;
    repeated.againDown = true;
    repeated.repeatCount = 100U;
    repeated.captureUnixTimeMilliseconds = event.captureUnixTimeMilliseconds;
    repeated.disposition = debug::InputDisposition::Forward;
    state->recentInputEvents.push_back(repeated);
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
    Check(wide.find("Mouse pos(-73,20)") != std::string::npos
            && wide.find("d(0,0)") != std::string::npos
            && wide.find("wheel(0,0.2)") != std::string::npos
            && wide.find("idle(0s)") != std::string::npos,
        "INPUT STATE separates Mouse fields and rounds mouse numbers to one decimal place");
    Check(wide.find("path 3/24 start(-76,20) pos(-73,20) d(3,0)") != std::string::npos
        && wide.find("pulse 20ms/100ms") != std::string::npos && wide.find("vertical empty") != std::string::npos,
        "meters show compact progress/period pairs and empty completed views");
    Check(wide.find("path (tick)") != std::string::npos
        && wide.find("[@path 24 start(-100,20) pos(-76,20) d(24,0)]") != std::string::npos,
        "actions name the triggering meter while METERS owns its grouped data");
    const auto separated = controller.Render(360U, 90U);
    Check(BoxText(separated, "VARIABLES").find("[counter=42]") != std::string::npos
            && BoxText(separated, "INPUT STATE").find("[LCtrl PHY]") != std::string::npos
            && BoxText(separated, "INPUT STATE").find("[Mouse.Left PHY]") != std::string::npos
            && BoxText(separated, "INPUT STATE").find("Mouse pos") != std::string::npos
            && BoxText(separated, "METERS").find("[@path 24") != std::string::npos
            && BoxText(separated, "VARIABLES").find("Mouse") == std::string::npos
            && BoxText(separated, "INPUT STATE").find("@path") == std::string::npos,
        "variables, held inputs, Mouse observation, and meter cycles occupy their owning boxes");
    const auto meterPosition = FindAscii(separated, "[path 3/24");
    const auto snapshotPosition = FindAscii(separated, "[@path 24");
    const auto nextMeterPosition = FindAscii(separated, "[pulse 20ms");
    Check(snapshotPosition == meterPosition + separated.Width()
            && nextMeterPosition == snapshotPosition + separated.Width(),
        "each meter and completed snapshot occupies exactly one separate row at large widths");
    const auto minimum = controller.Render(80U, 24U);
    for (const auto& canvas : {minimum, separated}) {
        std::istringstream rows(BoxText(canvas, "EVENTS"));
        std::string header, tick, again;
        std::getline(rows, header);
        std::getline(rows, tick);
        std::getline(rows, again);
        Check(tick.find("path") == header.find("SOURCE")
                && tick.find("tick") == header.find("EVENT")
                && again.find("AGAIN") == header.find("EVENT")
                && again.find("PHY") == header.find("ORIG")
                && tick.find("1000") + 4U == header.find("COUNT") + 5U
                && again.find('_') + 1U == header.find("COUNT") + 5U,
            "meter ticks show aligned counts while key repeats use an underscore at all widths");
    }
    Check(BoxText(minimum, "INPUT STATE").find("[LCtrl PHY]") != std::string::npos
            && BoxText(minimum, "INPUT STATE").find("[Mouse.Left PHY]") != std::string::npos,
        "held keyboard keys and mouse buttons remain visible at minimum size");
    const auto actions = wide.substr(wide.find("ACTION EXECUTIONS"));
    Check(actions.find("start(") == std::string::npos && wide.find("input=#") == std::string::npos
        && wide.find("cycle=#") == std::string::npos,
        "the action view shows trigger, condition, and action without expanded snapshots or transport identities");
    const auto narrow = controller.Render(80, 150);
    Check(FindAscii(narrow, "[@path 24") == FindAscii(narrow, "[path 3/24") + narrow.Width()
            && BoxText(narrow, "METERS").find("vertical -0.2/1") != std::string::npos,
        "narrow Debug viewports keep one meter entry per row");
    state->ruleExecutions[0].completedMeters.resize(1);
    state->ruleExecutions[0].triggerInput.occurrence = {};
    platform.debugState = state;
    controller.Tick();
    const auto raw = CanvasText(controller.Render(160, 70));
    Check(raw.find("Mouse (move)") != std::string::npos && raw.find("loading source selections") == std::string::npos,
        "raw mouse triggers use a short parenthesized name independently of snapshot arrival");
    const std::array<std::pair<std::int64_t, std::string_view>, 6> idleCases{{
        {0, "idle(0s)"},
        {123'456, "idle(0s)"},
        {123'456'789, "idle(0.1s)"},
        {1'000'000'000, "idle(1s)"},
        {60'000'000'000, "idle(60s)"},
        {(std::numeric_limits<std::int64_t>::max)(), "idle(9223372036.9s)"},
    }};
    for (const auto& [nanoseconds, expected] : idleCases) {
        state->mouse.mouse.idleTime.nanoseconds = nanoseconds;
        platform.debugState = state;
        controller.Tick();
        Check(CanvasText(controller.Render(160, 70)).find(expected) != std::string::npos,
            "Mouse idle time consistently uses seconds from fractional intervals through the duration limit");
    }
    for (std::size_t index = 0U; index < 12U; ++index) {
        state->values.push_back({"variable" + std::to_string(index), {}});
    }
    platform.debugState = state;
    controller.Tick();
    using ui::tui::Key;
    (void)controller.Render(80U, 24U);
    controller.Handle({Key::Tab});
    controller.Handle({Key::End});
    const auto inputsEnd = BoxText(controller.Render(80U, 24U), "INPUT STATE");
    controller.Handle({Key::Tab});
    controller.Handle({Key::End});
    const auto metersEnd = BoxText(controller.Render(80U, 24U), "METERS");
    controller.Handle({Key::Tab});
    controller.Handle({Key::End});
    const auto scrolled = controller.Render(80U, 24U);
    Check(BoxText(scrolled, "VARIABLES").find("variable11") != std::string::npos
            && BoxText(scrolled, "METERS") == metersEnd
            && BoxText(scrolled, "INPUT STATE") == inputsEnd
            && BoxText(scrolled, "METERS").find("vertical") != std::string::npos,
        "Tab activates each state region and scrolling preserves the other viewports");
    for (std::size_t index = 0U; index < 5U; ++index) {
        controller.Handle({Key::Home});
        controller.Handle({Key::Tab});
    }
    controller.Handle({Key::Tab});
    controller.Handle({Key::Tab});
    controller.Handle({Key::Escape});
    platform.debugState = previous;
    controller.Tick();
}
