std::shared_ptr<const inputweaver::CompiledProgram> MakeMouseTaskProgram(bool executable = false)
{
    using namespace inputweaver;
    auto storage = test::MakeTapFixtureStorage();
    if (executable) SelectExecutableTarget(storage);
    storage.actionCode.clear();
    storage.actionPrograms.clear();
    storage.rules.clear();
    storage.eventBuckets.clear();
    storage.debugInfo.rules.clear();
    storage.numberConstants = {8, 24, 0.25, -0.5};
    storage.durationConstants = {{10'000'000}};
    const auto string = [&](std::string name) {
        const StringId id{static_cast<std::uint32_t>(storage.strings.size())};
        storage.strings.push_back(std::move(name));
        return id;
    };
    const auto expression = [&](ExpressionInstruction instruction) {
        const ExpressionId id{static_cast<std::uint32_t>(storage.expressions.size())};
        AppendExpression(storage, instruction.type, 1,
            {instruction, {ExpressionOpcode::Return, instruction.type, 0, 0}});
        return id.value;
    };
    const auto field = [&](std::uint32_t source, EventField name, bool completed) {
        const EventFieldReference ref{EventSourceId{source}, name, completed};
        return expression({ExpressionOpcode::LoadField, EventFieldType(ref), source, ref.Selector()});
    };
    const auto finePeriod = expression({ExpressionOpcode::PushNumber, ExpressionType::Number, 0, 0});
    const auto coarsePeriod = expression({ExpressionOpcode::PushNumber, ExpressionType::Number, 1, 0});
    storage.eventSources = {
        {string("fine"), EventTransition::Move, ExpressionId{finePeriod}, {}},
        {string("coarse"), EventTransition::Move, ExpressionId{coarsePeriod}, {}},
    };
    const auto delay = expression({ExpressionOpcode::PushDuration, ExpressionType::Duration, 0, 0});
    const auto fineStart = field(0, EventField::StartX, true);
    const auto fineEnd = field(0, EventField::X, true);
    const auto coarseEnd = field(1, EventField::X, true);
    const auto liveDelta = field(kInvalidProgramIndex, EventField::Dx, false);
    const auto progress = field(0, EventField::Progress, false);
    const auto mouseX = field(kInvalidProgramIndex, EventField::X, false);
    const auto mouseY = field(kInvalidProgramIndex, EventField::Y, false);
    const auto fineDx = field(0, EventField::Dx, true);
    const auto quarter = expression({ExpressionOpcode::PushNumber, ExpressionType::Number, 2, 0});
    const auto halfBack = expression({ExpressionOpcode::PushNumber, ExpressionType::Number, 3, 0});
    (void)field(0, EventField::Valid, true);
    for (std::uint32_t index = 0; index < 8; ++index) {
        storage.arrays.push_back({ArrayElementType::Number, {}});
        storage.debugInfo.arrays.push_back({string("samples" + std::to_string(index)), ArrayId{index}, {index + 1, 1}});
    }
    const auto rule = [&](EventKey key, Delivery delivery, std::initializer_list<ActionInstruction> actions) {
        const auto ordinal = static_cast<std::uint32_t>(storage.rules.size());
        const auto begin = static_cast<std::uint32_t>(storage.actionCode.size());
        const auto action = static_cast<std::uint32_t>(storage.actionPrograms.size());
        storage.actionCode.insert(storage.actionCode.end(), actions);
        storage.actionCode.push_back({ActionOpcode::End, 0, 0, 0});
        storage.actionPrograms.push_back({{begin, static_cast<std::uint32_t>(actions.size() + 1)}, 0, 0, {}});
        storage.rules.push_back({{}, ActionProgramId{action}, {}, delivery, MatchFlow::Stop, RuleKind::Event, ordinal, {}});
        storage.eventBuckets.push_back({key, {ordinal, 1}});
        storage.debugInfo.rules.push_back({ordinal, {}, {}});
    };
    rule({{}, EventTransition::Tick, EventSourceId{0}}, Delivery::Observe, {
        {ActionOpcode::AppendArrayElement, 0, fineStart},
        {ActionOpcode::Wait, delay, 0},
        {ActionOpcode::AppendArrayElement, 1, fineStart},
        {ActionOpcode::AppendArrayElement, 2, coarseEnd},
        {ActionOpcode::AppendArrayElement, 3, liveDelta},
        {ActionOpcode::AppendArrayElement, 4, progress},
    });
    storage.rules.back().flow = MatchFlow::Continue;
    rule({{}, EventTransition::Tick, EventSourceId{0}}, Delivery::Observe,
        {{ActionOpcode::AppendArrayElement, 0, fineStart}});
    storage.eventBuckets.pop_back();
    storage.eventBuckets.back().rules.count = 2;
    rule({{}, EventTransition::Tick, EventSourceId{1}}, Delivery::Observe,
        {{ActionOpcode::AppendArrayElement, 5, fineEnd}});
    rule({{}, EventTransition::Move}, Delivery::Consume,
        {{ActionOpcode::AppendArrayElement, 6, liveDelta}});
    rule({ControlRefId{1}, EventTransition::Down}, Delivery::Observe,
        {{ActionOpcode::Wait, delay, 0}, {ActionOpcode::AppendArrayElement, 7, fineEnd}});
    rule({ControlRefId{0}, EventTransition::Down}, Delivery::Observe,
        {{ActionOpcode::RestartEvent, 0, 0},
         {ActionOpcode::Pointer, static_cast<std::uint32_t>(PointerOperation::MoveTo), mouseX, mouseY},
         {ActionOpcode::Pointer, static_cast<std::uint32_t>(PointerOperation::MoveBy), fineDx, liveDelta},
         {ActionOpcode::Pointer, static_cast<std::uint32_t>(PointerOperation::Scroll), quarter, 0},
         {ActionOpcode::Pointer, static_cast<std::uint32_t>(PointerOperation::ScrollHorizontal), halfBack, 0}});
    storage.debugInfo.expressionInstructionSpans.resize(storage.expressionCode.size());
    storage.debugInfo.actionInstructionSpans.resize(storage.actionCode.size());
    const auto pauseControl = ControlRefId{static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back({kControlNamespaceUsbHid, 0x07U, 0x41U, kControlQualifierNone});
    storage.pauseControlBuckets.push_back({{pauseControl, EventTransition::Down}, {0, 1}});
    storage.pauseControlRules.push_back({{}, Delivery::Observe, PauseEffect::Toggle, 100, {}});
    return Finalize(std::move(storage));
}

void TestMouseTaskSnapshots()
{
    using namespace inputweaver;
    const auto program = MakeMouseTaskProgram();
    if (!program) return;
    RuntimeHarness harness;
    harness.route.pointerAvailable = true;
    harness.output.pointerSupported = true;
    harness.route.pointer = {100, 200};
    Check(harness.runtime.Activate(program).activated, "mouse sources activate against an observation port");
    const auto move = [&](InputCoordinate x, double dx) {
        RuntimeInputEvent event{};
        event.device = DeviceKind::Mouse;
        event.transition = Transition::Move;
        event.position = {x, 200};
        event.delta.dx = dx;
        harness.route.pointer = event.position;
        return harness.runtime.HandleInput(event);
    };
    Check(move(127, 27) == InputDecision::Suppress, "raw consumption leaves cycle accumulation active");
    (void)harness.runtime.Pump();
    const auto samples = [&](std::uint32_t array) {
        std::vector<double> result;
        for (auto it = harness.debug.events.rbegin(); it != harness.debug.events.rend(); ++it) {
            if (it->kind == RuntimeDebugEventKind::ArrayChanged && it->array.array.value == array) {
                for (std::uint8_t index = 0; index < it->array.prefixCount; ++index) {
                    result.push_back(it->array.elements[index].numberValue);
                }
                break;
            }
        }
        return result;
    };
    Check(samples(0) == std::vector<double>({100, 100, 108, 108, 116, 116})
        && samples(5) == std::vector<double>({124}),
        "subscriptions share each tick's exact cycle and cross-source reads select the last same-input completion");
    const auto f6 = FindKeyboardControl(*program, 0x3f);
    const auto f7 = FindKeyboardControl(*program, 0x40);
    (void)harness.runtime.HandleInput(KeyboardEvent(f6.value, Transition::Down));
    (void)harness.runtime.Pump();
    (void)harness.runtime.HandleInput(KeyboardEvent(f7.value, Transition::Down));
    (void)harness.runtime.Pump();
    const auto& requests = harness.output.requests;
    Check(requests.size() == 4, "all four pointer operations publish through the common output port");
    if (requests.size() == 4) {
        Check(requests[0].kind == RuntimeOutputKind::Pointer && requests[0].pointer.x == 127
            && requests[0].pointer.y == 200 && requests[1].pointer.x == 8 && requests[1].pointer.y == 27
            && requests[2].pointer.x == 0.25 && requests[3].pointer.x == -0.5,
            "pointer arguments read live position and frozen completion fields without discarding fractions");
        Check(requests[0].sequence < requests[1].sequence && requests[1].sequence < requests[2].sequence
            && requests[2].sequence < requests[3].sequence && !harness.runtime.HasOwnedOutputs(),
            "pointer requests preserve shared output ordering without held-control ownership");
    }
    (void)move(129, 2);
    (void)harness.runtime.Pump();
    harness.clock.Advance(10'000'000);
    (void)harness.runtime.Pump();
    Check(samples(1) == std::vector<double>({100, 108, 116})
        && samples(2) == std::vector<double>({124, 124, 124}),
        "wait and restart preserve task-local own-source and cross-source selections");
    Check(samples(3) == std::vector<double>({2, 2, 2}) && samples(4) == std::vector<double>({2, 2, 2}),
        "after waiting tasks read new Mouse deltas and current source progress");
    Check(samples(7) == std::vector<double>({124}), "a keyboard task's first completed-field read after wait is frozen");
    Check(!harness.runtime.FatalShutdownRequested(), "mouse task flow completes normally");
}

void TestMouseDebugSnapshots()
{
    using namespace inputweaver;
    const auto program = MakeMouseTaskProgram();
    if (!program) return;
    RuntimeHarness harness;
    harness.route.pointerAvailable = true;
    harness.output.pointerSupported = true;
    harness.route.pointer = {100, -200};
    Check(harness.runtime.Activate(program).activated, "mouse Debug program activates");
    RuntimeMouseSnapshot snapshot;
    Check(harness.runtime.ReadMouseSnapshot(snapshot) && snapshot.sources.size() == 2
        && snapshot.sources[0].current.start == MousePoint{100, -200}
        && snapshot.sources[0].current.period.type == ExpressionType::Number
        && snapshot.sources[0].current.period.numberValue == 0,
        "unopened Debug sources expose observed origins and typed zero periods");
    RuntimeInputEvent input{};
    input.device = DeviceKind::Mouse;
    input.transition = Transition::Move;
    input.position = {127, -200};
    input.delta.dx = 27;
    input.debugCaptureEpoch = 1;
    input.debugInputSequence = 10;
    harness.route.pointer = input.position;
    (void)harness.runtime.HandleInput(input);
    Check(harness.runtime.ReadMouseSnapshot(snapshot) && snapshot.mouse.delta.dx == 27
        && snapshot.sources[0].current.start.x == 124 && snapshot.sources[0].current.progress == 3
        && snapshot.sources[1].completed.start.x == 100 && snapshot.sources[1].completed.point.x == 124,
        "Debug snapshots reuse interpolated boundaries, remainder, and latest observation");
    const auto firstCompletion = snapshot.sources[0].completed.sequence;
    std::vector<MouseOccurrence> occurrences;
    for (const auto& event : harness.debug.events) {
        if (event.kind != RuntimeDebugEventKind::MouseCycleCompleted) continue;
        Check(event.captureEpoch == 1 && event.triggerInputSequence == 10,
            "every cycle retains its parent raw-input identity");
        occurrences.push_back(event.occurrence);
    }
    Check(occurrences.size() == 4 && occurrences[0].cycle.start.x == 100
        && occurrences[1].cycle.start.x == 108 && occurrences[2].cycle.start.x == 116,
        "one input publishes each named completion separately");
    (void)harness.runtime.Pump();
    std::size_t subscriptions = 0;
    for (const auto& event : harness.debug.events) {
        if (event.kind != RuntimeDebugEventKind::RuleMatched) continue;
        Check(event.selectionCount == 2, "each task publishes its full fixed source selection");
        for (const auto& selected : harness.debug.events) {
            if (selected.kind != RuntimeDebugEventKind::ExecutionSourceSelected
                || selected.executionMarker != event.executionMarker) continue;
            Check(selected.occurrence.cycle.sequence != 0,
                "same-input selections include all completed sources");
            if (event.occurrence.source == selected.occurrence.source) {
                Check(selected.occurrence.cycle.sequence == event.occurrence.cycle.sequence,
                    "tick execution names its own exact cycle, not the latest cycle");
                ++subscriptions;
            }
        }
    }
    Check(subscriptions == 7, "shared subscriptions retain independent execution identities");
    harness.clock.Advance(100'000'000);
    Check(harness.runtime.ReadMouseSnapshot(snapshot) && !snapshot.mouse.moving
        && snapshot.mouse.idleTime.nanoseconds == 100'000'000 && snapshot.mouse.delta.dx == 27
        && snapshot.sources[0].completed.sequence == firstCompletion
        && snapshot.sources[0].current.progress == 3,
        "STATE polling updates idle state without clearing deltas, distance, or completion identity");
    input.debugCaptureEpoch = 2;
    input.debugInputSequence = 1;
    input.position.x = 132;
    input.delta.dx = 5;
    harness.route.pointer = input.position;
    (void)harness.runtime.HandleInput(input);
    Check(harness.runtime.ReadMouseSnapshot(snapshot)
        && snapshot.sources[0].completed.sequence > firstCompletion
        && snapshot.sources[0].completed.start.x == 124 && snapshot.sources[0].completed.point.x == 132,
        "a new capture epoch preserves the ongoing cycle and semantic identity sequence");
}

void TestKeyProgramMouseDebugObservation()
{
    using namespace inputweaver;
    RuntimeHarness harness;
    harness.route.pointerAvailable = true;
    harness.route.pointer = {-80, 20};
    Check(harness.runtime.Activate(Finalize(test::MakeTapFixtureStorage())).activated,
        "key-only program activates with a Debug observation port");
    RuntimeMouseSnapshot snapshot;
    Check(harness.runtime.ReadMouseSnapshot(snapshot) && snapshot.sources.empty()
        && snapshot.mouse.position == MousePoint{-80, 20}, "Debug observes Mouse for key-only programs");
    RuntimeInputEvent input{};
    input.device = DeviceKind::Mouse;
    input.transition = Transition::VerticalWheel;
    input.position = {-80, 20};
    input.delta.wheelY = 0.25;
    (void)harness.runtime.HandleInput(input);
    Check(harness.runtime.ReadMouseSnapshot(snapshot) && snapshot.mouse.delta.wheelY == 0.25,
        "key-only Debug observation retains normalized fractional wheel input");
}

void TestMouseQualificationLifecycle()
{
    using namespace inputweaver;
    const auto program = MakeMouseTaskProgram(true);
    if (!program) return;
    RuntimeHarness harness;
    harness.route.pointerAvailable = true;
    harness.output.pointerSupported = true;
    Check(harness.runtime.Activate(program).activated, "qualified mouse program activates");
    const auto read = [&](EventField field, std::uint32_t source, bool completed) {
        const auto selector = EventFieldReference{EventSourceId{source}, field, completed}.Selector();
        for (std::size_t index = 0; index < program->Expressions().size(); ++index) {
            const auto& code = program->ExpressionCode()[program->Expressions()[index].code.begin];
            if (code.opcode == ExpressionOpcode::LoadField && code.operand0 == source && code.operand1 == selector) {
                return harness.runtime.EvaluateExpression(ExpressionId{static_cast<std::uint32_t>(index)}).value;
            }
        }
        Check(false, "lifecycle inspection expression exists");
        return RuntimeValue{};
    };
    const auto move = [&](InputCoordinate x, double dx) {
        RuntimeInputEvent input{};
        input.device = DeviceKind::Mouse;
        input.transition = Transition::Move;
        input.position.x = x;
        input.delta.dx = dx;
        harness.route.pointer = input.position;
        (void)harness.runtime.HandleInput(input);
    };
    move(27, 27);
    (void)harness.runtime.Pump();
    Check(read(EventField::Valid, 0, true).stateValue == 1, "qualified movement produces a completion");
    harness.runtime.SetTargetEligible(false);
    move(35, 8);
    (void)harness.runtime.Pump();
    Check(read(EventField::Valid, 0, true).stateValue == 0 && read(EventField::Progress, 0, false).numberValue == 0
        && read(EventField::Dx, kInvalidProgramIndex, false).numberValue == 8,
        "target loss resets sources while global physical observation continues");
    harness.runtime.SetTargetEligible(true);
    move(38, 3);
    Check(read(EventField::Progress, 0, false).numberValue == 3, "restored target starts a fresh source cycle");
    const auto pause = FindKeyboardControl(*program, 0x41);
    (void)harness.runtime.HandleInput(KeyboardEvent(pause.value, Transition::Down));
    move(43, 5);
    Check(!harness.runtime.PauseOn() && read(EventField::Progress, 0, false).numberValue == 0
        && read(EventField::Dx, kInvalidProgramIndex, false).numberValue == 5,
        "pause resets accumulation without freezing global deltas");
    (void)harness.runtime.HandleInput(KeyboardEvent(pause.value, Transition::Up));
    (void)harness.runtime.HandleInput(KeyboardEvent(pause.value, Transition::Down));
    move(51, 8);
    Check(harness.runtime.PauseOn() && read(EventField::Valid, 0, true).stateValue == 1,
        "resuming pause permits a newly completed cycle");
    harness.runtime.Deactivate();
    Check(harness.runtime.Activate(program).activated
        && read(EventField::Valid, 0, true).stateValue == 0
        && read(EventField::Dx, kInvalidProgramIndex, false).numberValue == 0,
        "program activation starts fresh completion and physical observation state");
}
