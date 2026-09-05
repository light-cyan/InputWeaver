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
        matched.ruleMatched = {7, 1, "always", "wait(10ms)", EventSourceId{0}, 12, 3};
        send(matched);
        for (std::uint32_t index = 0; index < 3; ++index) {
            auto selected = MakeMessage(debug::MessageKind::ExecutionSourceSelected, 11 + index);
            selected.executionSourceSelected = {7, {EventSourceId{index}, capture.captureStarted.mouse.sources[index].completed}};
            send(selected);
        }
        auto cycle = MakeMessage(debug::MessageKind::MouseCycleCompleted, 15);
        cycle.mouseCycleCompleted = {1, {EventSourceId{0}, capture.captureStarted.mouse.sources[0].completed}};
        send(cycle);
        if (!rawFirst) send(raw);
        auto state = reducer.ReadState();
        Check(state->captureTrusted && state->pressedControls.empty() && state->recentInputEvents.size() == 2
            && state->recentInputEvents[0].delta.dx == 27 && state->recentInputEvents[1].occurrence.cycle.sequence == 12,
            "raw-first and cycle-first streams both correlate without treating motion as a pressed control");
        Check(state->ruleExecutions.size() == 1 && state->ruleExecutions[0].triggerInput.occurrence.cycle.sequence == 12
            && state->ruleExecutions[0].completedSources.size() == 3
            && state->ruleExecutions[0].completedSources[1].sequence == 0,
            "cycle execution retains its exact trigger and complete selection, including empty sources");
        auto live = MakeMessage(debug::MessageKind::MouseState, 25);
        live.mouseState = capture.captureStarted.mouse;
        live.mouseState.mouse.position.x = 50;
        live.mouseState.mouse.idleTime.nanoseconds = 200'000'000;
        live.mouseState.mouse.moving = false;
        live.mouseState.sources[0].completed.sequence = 19;
        send(live);
        state = reducer.ReadState();
        Check(state->mouse.mouse.position.x == 50 && state->mouse.sources[0].completed.sequence == 19
            && state->ruleExecutions[0].completedSources[0].sequence == 12,
            "replacing current STATE leaves the task's older completion intact");
        reducer.CaptureStopped();
        reducer.CaptureRequested();
        capture.header.captureEpoch = 2;
        capture.captureStarted.mouse = live.mouseState;
        send(capture);
        Check(reducer.ReadState()->mouse.sources[0].completed.sequence == 19
            && reducer.ReadState()->ruleExecutions.empty(), "new capture keeps semantic identities from the fresh runtime snapshot");
    }
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
    cycle.mouseCycleCompleted = {1, {EventSourceId{0}, capture.captureStarted.mouse.sources[0].completed}};
    (void)reducer.Accept(cycle);
    cycle.header.protocolSequence = 4;
    cycle.mouseCycleCompleted.occurrence.cycle.sequence = 13;
    (void)reducer.Accept(cycle);
    auto matched = MakeMessage(debug::MessageKind::RuleMatched, 5);
    matched.ruleMatched = {8, 1, "always", "wait(10ms)", EventSourceId{0}, 13, 3};
    Check(reducer.Accept(matched) == debug::DebugReductionAction::None
        && reducer.ReadState()->ruleExecutions.size() == 1
        && reducer.ReadState()->ruleExecutions[0].triggerInput.occurrence.cycle.sequence == 13,
        "a retained tick supplies its full trigger after the raw event scrolls out of the bounded history");
}
