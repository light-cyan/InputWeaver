void TestMouseMessages()
{
    using namespace inputweaver;
    debug::Message message;
    message.header.kind = debug::MessageKind::CaptureStarted;
    message.captureStarted = test::MouseDebugCapture();
    const auto capture = RoundTrip(message);
    Check(capture.Succeeded() && capture.message.captureStarted.meters[0].name == "path"
        && capture.message.captureStarted.mouse.meters[0].completed.sequence == 12
        && capture.message.captureStarted.mouse.meters[1].current.elapsedNanoseconds == 20'000'000
        && capture.message.captureStarted.mouse.meters[2].current.progress == -0.25,
        "named distance, duration, and fractional wheel views share the capture codec");
    message.header.kind = debug::MessageKind::MouseState;
    message.mouseState = message.captureStarted.mouse;
    const auto state = RoundTrip(message);
    Check(state.Succeeded() && state.message.mouseState.mouse.position.x == -73
        && state.message.mouseState.mouse.delta.wheelY == 0.25
        && state.message.mouseState.mouse.idleTime.nanoseconds == 12'000'000,
        "live mouse position, delta tuple, and idle state round trip");
    message.header.kind = debug::MessageKind::InputEvent;
    message.inputEvent.device = DeviceKind::Mouse;
    message.inputEvent.transition = Transition::HorizontalWheel;
    message.inputEvent.position = {-73, 20};
    message.inputEvent.delta.wheelX = -0.25;
    const auto raw = RoundTrip(message);
    Check(raw.Succeeded() && raw.message.inputEvent.position.x == -73 && raw.message.inputEvent.delta.wheelX == -0.25,
        "raw mouse coordinates and signed deltas round trip");
    message.header.kind = debug::MessageKind::MouseCycleCompleted;
    message.mouseCycleCompleted = {4, {MeterId{0}, message.mouseState.meters[0].completed}};
    const auto completed = RoundTrip(message);
    Check(completed.Succeeded() && completed.message.mouseCycleCompleted.triggerInputSequence == 4
        && completed.message.mouseCycleCompleted.occurrence.cycle.displacement.x == 24,
        "cycle payload preserves its raw correlation and boundary values");
    message.header.kind = debug::MessageKind::RuleMatched;
    message.ruleMatched = {7, 4, "always", "wait(10ms)", MeterId{0}, 12, 3};
    const auto rule = RoundTrip(message);
    Check(rule.Succeeded() && rule.message.ruleMatched.triggerCycleSequence == 12
        && rule.message.ruleMatched.selectionCount == 3, "execution trigger names a semantic cycle and complete selection count");
    message.header.kind = debug::MessageKind::ExecutionMeterSelected;
    message.executionMeterSelected = {7, {MeterId{0}, message.mouseState.meters[0].completed}};
    const auto selected = RoundTrip(message);
    Check(selected.Succeeded() && selected.message.executionMeterSelected.occurrence.cycle.sequence == 12,
        "task selections use the same completed cycle codec");
    message.executionMeterSelected.occurrence = {MeterId{1}, {}};
    const auto empty = RoundTrip(message);
    Check(empty.Succeeded() && empty.message.executionMeterSelected.occurrence.cycle.sequence == 0,
        "an empty fixed source selection is explicit on the wire");
}
