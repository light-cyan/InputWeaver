void TestMouseStateAndCorrelation()
{
    using namespace inputweaver;
    for (const bool rawFirst : {true, false}) {
        debug::DebugStateReducer reducer;
        reducer.Connected(17);
        reducer.CaptureRequested();
        std::uint64_t sequence = 1;
        const auto send = [&](debug::Message message) {
            message.header.protocolSequence = sequence++;
            Check(reducer.Accept(message) == debug::DebugReductionAction::None, "mouse stream record is accepted");
        };
        auto capture = MakeMessage(debug::MessageKind::CaptureStarted, 1);
        capture.captureStarted = test::MouseDebugCapture();
        send(capture);
        auto raw = MakeInput(20, 1, 0, Transition::Move, InputOrigin::PhysicalCandidate);
        raw.inputEvent.device = DeviceKind::Mouse;
        raw.inputEvent.position = {-73, 20};
        raw.inputEvent.delta.dx = 27;
        if (rawFirst) send(raw);
        auto matched = MakeMessage(debug::MessageKind::RuleMatched, 10);
        matched.ruleMatched = {7, 1, "always", "wait(10ms)", MeterId{0}, 12, 3};
        send(matched);
        for (std::uint32_t index = 0; index < 3; ++index) {
            auto selected = MakeMessage(debug::MessageKind::ExecutionMeterSelected, 11 + index);
            selected.executionMeterSelected = {7, {MeterId{index}, capture.captureStarted.mouse.meters[index].completed}};
            send(selected);
        }
        auto cycle = MakeMessage(debug::MessageKind::MouseCycleCompleted, 15);
        cycle.mouseCycleCompleted = {1, {MeterId{0}, capture.captureStarted.mouse.meters[0].completed}};
        send(cycle);
        if (!rawFirst) send(raw);
        auto state = reducer.ReadState();
        Check(state->captureTrusted && state->pressedControls.empty() && state->recentInputEvents.empty(),
            "mouse records stay in internal correlation history in either arrival order");
        Check(state->ruleExecutions.size() == 1 && state->ruleExecutions[0].triggerInput.occurrence.cycle.sequence == 12
            && state->ruleExecutions[0].completedMeters.size() == 3
            && state->ruleExecutions[0].completedMeters[1].sequence == 0,
            "cycle execution retains its exact trigger and complete selection, including empty sources");
        auto live = MakeMessage(debug::MessageKind::MouseState, 25);
        live.mouseState = capture.captureStarted.mouse;
        live.mouseState.mouse.position.x = 50;
        live.mouseState.mouse.idleTime.nanoseconds = 200'000'000;
        live.mouseState.mouse.moving = false;
        live.mouseState.meters[0].completed.sequence = 19;
        send(live);
        state = reducer.ReadState();
        Check(state->mouse.mouse.position.x == 50 && state->mouse.meters[0].completed.sequence == 19
            && state->ruleExecutions[0].completedMeters[0].sequence == 12,
            "replacing current STATE leaves the task's older completion intact");
        reducer.CaptureStopped();
        reducer.CaptureRequested();
        capture.header.captureEpoch = 2;
        capture.captureStarted.mouse = live.mouseState;
        send(capture);
        Check(reducer.ReadState()->mouse.meters[0].completed.sequence == 19
            && reducer.ReadState()->ruleExecutions.empty(), "new capture keeps semantic identities from the fresh runtime snapshot");
        matched.header.captureEpoch = 2;
        matched.ruleMatched = {9, 1, "always", "wait(10ms)", {}, 0, 3};
        send(matched);
        Check(reducer.ReadState()->ruleExecutions.empty(),
            "new capture cannot correlate a reused sequence with hidden input from the previous capture");
        raw.header.captureEpoch = 2;
        raw.inputEvent.position = {100, 50};
        send(raw);
        state = reducer.ReadState();
        Check(state->captureTrusted && state->ruleExecutions.size() == 1
            && state->ruleExecutions[0].triggerInput.position.x == 100
            && state->ruleExecutions[0].triggerInput.position.y == 50,
            "new capture resolves the pending mouse execution using only its own input");
    }
}

void TestMouseHistoryKeepsControls()
{
    using namespace inputweaver;
    debug::DebugClientCapacities capacities;
    capacities.maximumInputEvents = 2;
    debug::DebugStateReducer reducer(capacities);
    reducer.Connected(17);
    reducer.CaptureRequested();
    auto capture = MakeMessage(debug::MessageKind::CaptureStarted, 1);
    capture.captureStarted = test::MouseDebugCapture();
    (void)reducer.Accept(capture);
    (void)reducer.Accept(MakeInput(2, 1, 65, Transition::Down, InputOrigin::PhysicalCandidate));
    auto button = MakeInput(3, 2, 1, Transition::Down, InputOrigin::PhysicalCandidate);
    button.inputEvent.device = DeviceKind::Mouse;
    (void)reducer.Accept(button);
    constexpr std::array transitions{Transition::VerticalWheel, Transition::HorizontalWheel, Transition::Move};
    for (std::uint64_t input = 3; input <= 14; ++input) {
        auto raw = MakeInput(input + 1, input, 0, transitions[(input - 3) % transitions.size()], InputOrigin::PhysicalCandidate);
        raw.inputEvent.device = DeviceKind::Mouse;
        raw.inputEvent.position = {-73, 20};
        if (raw.inputEvent.transition == Transition::Move) raw.inputEvent.delta.dx = 27;
        Check(reducer.Accept(raw) == debug::DebugReductionAction::None, "numeric history rotates within its own capacity");
    }
    auto cycle = MakeMessage(debug::MessageKind::MouseCycleCompleted, 16);
    cycle.mouseCycleCompleted = {14, {MeterId{0}, capture.captureStarted.mouse.meters[0].completed}};
    (void)reducer.Accept(cycle);
    auto matched = MakeMessage(debug::MessageKind::RuleMatched, 17);
    matched.ruleMatched = {7, 14, "always", "wait(10ms)", MeterId{0}, 12, 3};
    (void)reducer.Accept(matched);
    const auto state = reducer.ReadState();
    Check(state->captureTrusted && state->recentInputEvents.size() == 2
        && state->recentInputEvents[0].inputSequence == 1 && state->recentInputEvents[1].inputSequence == 2,
        "mouse movement, wheel reports, and ticks preserve keyboard and mouse-button history");
    Check(state->ruleExecutions.size() == 1 && state->ruleExecutions[0].triggerInput.inputSequence == 14
        && state->ruleExecutions[0].triggerInput.occurrence.cycle.sequence == 12,
        "hidden numeric history still resolves exact action triggers");
    button = MakeInput(18, 15, 1, Transition::Up, InputOrigin::PhysicalCandidate);
    button.inputEvent.device = DeviceKind::Mouse;
    (void)reducer.Accept(button);
    (void)reducer.Accept(MakeInput(19, 16, 65, Transition::Up, InputOrigin::PhysicalCandidate));
    const auto released = reducer.ReadState();
    Check(released->captureTrusted && released->pressedControls.empty()
        && released->recentInputEvents.size() == 2
        && released->recentInputEvents[0].transition == Transition::Up
        && released->recentInputEvents[1].transition == Transition::Up
        && !released->recentInputEvents[0].unmatchedUp && !released->recentInputEvents[1].unmatchedUp,
        "mouse traffic preserves the pressed state used to pair later button and keyboard releases");
}

void TestRetainedTickCorrelation()
{
    using namespace inputweaver;
    debug::DebugClientCapacities capacities;
    capacities.maximumInputEvents = 2;
    debug::DebugStateReducer reducer(capacities);
    reducer.Connected(17);
    reducer.CaptureRequested();
    auto capture = MakeMessage(debug::MessageKind::CaptureStarted, 1);
    capture.captureStarted = test::MouseDebugCapture();
    (void)reducer.Accept(capture);
    auto raw = MakeInput(2, 1, 0, Transition::Move, InputOrigin::PhysicalCandidate);
    raw.inputEvent.device = DeviceKind::Mouse;
    (void)reducer.Accept(raw);
    auto cycle = MakeMessage(debug::MessageKind::MouseCycleCompleted, 3);
    cycle.mouseCycleCompleted = {1, {MeterId{0}, capture.captureStarted.mouse.meters[0].completed}};
    (void)reducer.Accept(cycle);
    cycle.header.protocolSequence = 4;
    cycle.mouseCycleCompleted.occurrence.cycle.sequence = 13;
    (void)reducer.Accept(cycle);
    auto matched = MakeMessage(debug::MessageKind::RuleMatched, 5);
    matched.ruleMatched = {8, 1, "always", "wait(10ms)", MeterId{0}, 13, 3};
    Check(reducer.Accept(matched) == debug::DebugReductionAction::None
        && reducer.ReadState()->ruleExecutions.size() == 1
        && reducer.ReadState()->ruleExecutions[0].triggerInput.occurrence.cycle.sequence == 13,
        "a retained tick supplies its full trigger after the raw event scrolls out of the bounded history");
}
