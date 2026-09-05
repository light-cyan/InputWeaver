void TestMouseDebugTransport()
{
    using namespace inputweaver;
    const auto capture = test::MouseDebugCapture();
    auto storage = MakeReadableTapStorage();
    for (const auto& source : capture.sources) {
        const StringId name{static_cast<std::uint32_t>(storage.strings.size())};
        storage.strings.push_back(source.name);
        const ExpressionId expression{static_cast<std::uint32_t>(storage.expressions.size())};
        const auto begin = static_cast<std::uint32_t>(storage.expressionCode.size());
        std::uint32_t constant{};
        if (source.periodType == ExpressionType::Duration) {
            constant = static_cast<std::uint32_t>(storage.durationConstants.size());
            storage.durationConstants.push_back({100'000'000});
        } else {
            constant = static_cast<std::uint32_t>(storage.numberConstants.size());
            storage.numberConstants.push_back(source.transition == EventTransition::Move ? 24 : 1);
        }
        storage.expressionCode.push_back({source.periodType == ExpressionType::Duration
            ? ExpressionOpcode::PushDuration : ExpressionOpcode::PushNumber, source.periodType, constant, 0});
        storage.expressionCode.push_back({ExpressionOpcode::Return, source.periodType, 0, 0});
        storage.expressions.push_back({{begin, 2}, source.periodType, 1, {}});
        storage.eventSources.push_back({name, source.transition, expression, {}});
    }
    storage.debugInfo.expressionInstructionSpans.resize(storage.expressionCode.size());
    storage.requirements = ComputeProgramRequirements(storage);
    auto finalized = FinalizeCompiledProgram(std::move(storage));
    Check(finalized.program != nullptr, "mouse transport fixture finalizes");
    if (!finalized.program) return;
    struct Observation {
        RuntimeMouseSnapshot snapshot;
        std::atomic<std::uint64_t> samples{0};
        std::atomic<std::uint64_t> latest{12};
    } observation{capture.mouse};
    win32::WindowsDebugServer server;
    const auto token = L"mouse-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    std::wstring error;
    Check(server.Start(token, finalized.program, {{}, {}, {&observation,
        [](void* context, RuntimeMouseSnapshot& snapshot) noexcept {
            auto& observed = *static_cast<Observation*>(context);
            try { snapshot = observed.snapshot; } catch (...) { return false; }
            snapshot.mouse.idleTime.nanoseconds = static_cast<std::int64_t>(++observed.samples) * 1'000'000;
            snapshot.sources[0].completed.sequence = observed.latest.load();
            return true;
        }}}, error), "mouse Debug server starts with a snapshot callback");
    win32::WindowsDebugClient client;
    Check(client.Connect({GetCurrentProcessId()}, std::string(token.begin(), token.end())).Succeeded()
        && client.StartCapture().Succeeded(), "mouse Debug client connects and requests capture");
    Check(WaitForRequest(server) == win32::DebugCaptureRequest::Start && server.BeginCapture({}),
        "mouse capture boundary is published");
    Check(WaitUntil([&] {
        const auto state = client.ReadState();
        return state->capturing && state->eventSources.size() == 3 && state->mouse.mouse.idleTime.nanoseconds >= 2'000'000;
    }), "writer sampling updates idle STATE without input records");
    const auto correlation = server.BeginInput();
    RuntimeDebugEvent event{};
    event.kind = RuntimeDebugEventKind::MouseCycleCompleted;
    event.captureEpoch = correlation.captureEpoch;
    event.triggerInputSequence = correlation.inputSequence;
    event.occurrence = {EventSourceId{0}, capture.mouse.sources[0].completed};
    Check(server.Publish(event), "completed cycle enters the shared bounded producer queue");
    event.kind = RuntimeDebugEventKind::RuleMatched;
    event.executionMarker = 7;
    event.ruleIndex = 0;
    event.selectionCount = 3;
    Check(server.Publish(event), "tick execution publishes its semantic trigger identity");
    event.kind = RuntimeDebugEventKind::ExecutionSourceSelected;
    for (std::uint32_t index = 0; index < 3; ++index) {
        event.occurrence = {EventSourceId{index}, capture.mouse.sources[index].completed};
        Check(server.Publish(event), "source selection is copied into its own bounded record");
    }
    event.kind = RuntimeDebugEventKind::ExecutionEnded;
    Check(server.Publish(event), "mouse execution completion is published");
    debug::InputEventPayload raw{};
    raw.device = DeviceKind::Mouse;
    raw.transition = Transition::Move;
    raw.disposition = debug::InputDisposition::Forward;
    raw.position = {-73, 20};
    raw.delta.dx = 27;
    Check(server.PublishInput(correlation, raw), "parent raw input follows its runtime records");
    Check(WaitUntil([&] {
        const auto state = client.ReadState();
        return state->captureTrusted && state->recentInputEvents.size() == 2 && state->ruleExecutions.size() == 1
            && state->ruleExecutions[0].completedSources.size() == 3 && state->ruleExecutions[0].result.has_value();
    }), "native pipe transport correlates raw input, tick, and all fixed selections");
    observation.latest.store(19);
    Check(WaitUntil([&] {
        const auto state = client.ReadState();
        return state->mouse.sources.size() == 3 && state->mouse.sources[0].completed.sequence == 19
            && state->ruleExecutions.size() == 1 && state->ruleExecutions[0].completedSources[0].sequence == 12;
    }), "periodic state replacement preserves an older task selection");
    Check(client.StartCapture().Succeeded() && WaitForRequest(server) == win32::DebugCaptureRequest::Start
        && server.BeginCapture({}), "capture can restart during ongoing source statistics");
    Check(WaitUntil([&] {
        const auto state = client.ReadState();
        return state->captureEpoch > correlation.captureEpoch && state->mouse.sources.size() == 3
            && state->mouse.sources[0].current.progress == 3 && state->mouse.sources[0].completed.sequence == 19;
    }), "new capture preserves runtime phase and semantic completion identity");
    server.Stop();
    Check(client.ReadState()->streamComplete, "sampled mouse capture retains the final stream acknowledgment");
    client.Disconnect();
}
