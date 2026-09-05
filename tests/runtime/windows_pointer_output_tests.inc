std::vector<INPUT> g_pointerInputs;

UINT WINAPI CapturePointerInput(UINT count, LPINPUT inputs, int size)
{
    Check(size == static_cast<int>(sizeof(INPUT)), "pointer sender receives the native INPUT size");
    g_pointerInputs.insert(g_pointerInputs.end(), inputs, inputs + count);
    return count;
}

inputweaver::win32::PointerDesktop TestPointerDesktop()
{
    return {{-1920, -1080, 1920, 1080}, {-1920, -1080, 1920, 1080},
        {{-1920, 0, 0, 1080}, {0, 0, 1920, 1080}, {0, -1080, 1920, 0}}};
}

void TestPointerPreparation()
{
    using namespace inputweaver;
    auto desktop = TestPointerDesktop();
    win32::WindowsPointerOutput pointer;
    const InputInjector injector(0xabc, &CapturePointerInput);
    g_pointerInputs.clear();
    const auto execute = [&](RuntimePointerOutput request, ScreenPoint origin, std::uint64_t generation = 1) {
        auto prepared = pointer.Prepare(request, generation, origin, desktop);
        Check(prepared.native.Succeeded(), "pointer request prepares");
        const auto result = injector.InjectPrepared(prepared.native);
        Check(result.Succeeded(), "prepared pointer request reaches the native sender");
        if (result.Succeeded()) pointer.Commit(prepared);
        return prepared;
    };
    auto first = execute({PointerOperation::MoveBy, 0.5, -0.5}, {100, 200});
    Check(first.destination == ScreenPoint{100, 200} && g_pointerInputs.empty(),
        "fractional movement is retained before producing a whole pixel");
    auto second = execute({PointerOperation::MoveBy, 0.5, -0.5}, {100, 200});
    Check(second.destination == ScreenPoint{101, 199} && g_pointerInputs.size() == 1,
        "two fractional requests produce one signed pixel on each axis");
    const auto& native = g_pointerInputs.back().mi;
    const auto decodedX = static_cast<int>(std::floor(static_cast<double>(native.dx) * 3840 / 65536)) - 1920;
    const auto decodedY = static_cast<int>(std::floor(static_cast<double>(native.dy) * 2160 / 65536)) - 1080;
    Check(decodedX == 101 && decodedY == 199 && native.dwExtraInfo == 0xabc
        && native.dwFlags == (MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK),
        "pixel-center encoding spans the virtual desktop and carries the self-injection tag");
    struct Targets { std::array<ScreenPoint, 2> positions{}; std::size_t count{}; } targets;
    const support::CallbackRef<bool(ScreenPoint) noexcept> accept{&targets, [](void* raw, ScreenPoint point) noexcept {
        auto& visit = *static_cast<Targets*>(raw);
        visit.positions[visit.count++] = point;
        return true;
    }};
    Check(second.TargetsMatch(accept) && targets.count == 2
        && targets.positions[0] == second.origin && targets.positions[1] == second.destination,
        "movement routing checks its execution origin and normalized destination");
    auto absolute = execute({PointerOperation::MoveTo, -100.4, 200.6}, {0, 0});
    Check(absolute.destination == ScreenPoint{-100, 201}, "absolute coordinates round within a negative-origin monitor");
    auto gap = execute({PointerOperation::MoveTo, -100, -100}, {0, 0});
    Check(gap.destination == ScreenPoint{-100, 0}, "desktop gaps select the nearest monitor pixel");
    auto edge = execute({PointerOperation::MoveTo, -1e30, 1e30}, {0, 0});
    Check(edge.destination == ScreenPoint{-1920, 1079}, "finite distant coordinates reach the desktop edge");
    desktop.clip = {100, 100, 900, 800};
    auto clipped = execute({PointerOperation::MoveTo, 1000, 1000}, {100, 100});
    Check(clipped.destination == ScreenPoint{899, 799}, "cursor clipping participates in reachable target selection");
    desktop = TestPointerDesktop();
    const auto vertical = execute({PointerOperation::Scroll, 0.25, 0}, {100, 200});
    targets.count = 0;
    Check(vertical.native.input.mi.mouseData == 30 && vertical.native.input.mi.dwFlags == MOUSEEVENTF_WHEEL
        && vertical.TargetsMatch(accept) && targets.count == 1 && targets.positions[0] == vertical.origin,
        "vertical wheel uses fractional detents and checks only the current pointer target");
    const auto horizontal = execute({PointerOperation::ScrollHorizontal, -0.5, 0}, {100, 200});
    Check(static_cast<LONG>(horizontal.native.input.mi.mouseData) == -60
        && horizontal.native.input.mi.dwFlags == MOUSEEVENTF_HWHEEL, "horizontal wheel keeps its signed native units");
    const auto subTick = execute({PointerOperation::Scroll, 1.0 / 240, 0}, {100, 200});
    const auto wholeTick = execute({PointerOperation::Scroll, 1.0 / 240, 0}, {100, 200});
    Check(!subTick.native.emit && wholeTick.native.input.mi.mouseData == 1,
        "sub-native wheel units accumulate across requests");
    (void)execute({PointerOperation::MoveBy, 0.75, 0}, {100, 200});
    const auto newGeneration = execute({PointerOperation::MoveBy, 0.5, 0}, {100, 200}, 2);
    Check(!newGeneration.native.emit, "a new generation starts with fresh movement remainders");
}

void TestPointerSimulation()
{
    using namespace inputweaver;
    const auto desktop = TestPointerDesktop();
    win32::WindowsPointerOutput pointer(true);
    const InputInjector injector(0xabc, &CapturePointerInput, true);
    g_pointerInputs.clear();
    const auto execute = [&](RuntimePointerOutput request) {
        auto prepared = pointer.Prepare(request, 1, {100, 200}, desktop);
        Check(injector.InjectPrepared(prepared.native).Succeeded(), "dry-run pointer preparation succeeds");
        pointer.Commit(prepared);
        return prepared;
    };
    (void)execute({PointerOperation::MoveBy, 10, 0});
    const auto next = execute({PointerOperation::MoveBy, 10, 0});
    Check(next.origin == ScreenPoint{110, 200} && next.destination == ScreenPoint{120, 200}
        && g_pointerInputs.empty(), "dry-run relative outputs use the simulated predecessor without native input");
    pointer.ObservePhysical({500, 300});
    const auto rebased = execute({PointerOperation::MoveBy, -20, 0});
    Check(rebased.origin == ScreenPoint{500, 300} && rebased.destination == ScreenPoint{480, 300},
        "physical input rebases the simulated pointer");
    auto inFlight = pointer.Prepare({PointerOperation::MoveBy, 5, 0}, 1, {100, 200}, desktop);
    pointer.ObservePhysical({700, 400});
    pointer.Commit(inFlight);
    const auto newer = execute({PointerOperation::MoveBy, 1, 0});
    Check(newer.origin == ScreenPoint{700, 400}, "an output completion preserves a newer physical rebase");
}

void TestMouseWindowsPipeline()
{
    using namespace inputweaver;
    auto storage = test::MakeTapFixtureStorage();
    storage.numberConstants = {0.5, 0};
    const auto firstExpression = static_cast<std::uint32_t>(storage.expressions.size());
    for (std::uint32_t index = 0; index < 2; ++index) {
        const auto begin = static_cast<std::uint32_t>(storage.expressionCode.size());
        storage.expressions.push_back({{begin, 2}, ExpressionType::Number, 1, {}});
        storage.expressionCode.push_back({ExpressionOpcode::PushNumber, ExpressionType::Number, index, 0});
        storage.expressionCode.push_back({ExpressionOpcode::Return, ExpressionType::Number});
    }
    storage.actionCode = {{ActionOpcode::Pointer, static_cast<std::uint32_t>(PointerOperation::MoveBy), firstExpression, firstExpression + 1},
        {ActionOpcode::End}};
    storage.actionPrograms = {{{0, 2}, 0, 0, {}}};
    storage.eventBuckets[0].key = {{}, EventTransition::Move};
    storage.rules[0].delivery = Delivery::Observe;
    storage.debugInfo.expressionInstructionSpans.resize(storage.expressionCode.size());
    storage.debugInfo.actionInstructionSpans.assign(2, {});
    storage.controlRequirements.erase(storage.controlRequirements.begin(), storage.controlRequirements.begin() + 2);
    storage.requirements = ComputeProgramRequirements(storage);
    const auto program = Finalize(std::move(storage));
    if (!program) return;
    win32::WindowsControlCatalog catalog;
    WindowsOutputQueue queue;
    win32::WindowsRuntimeOutputPort output(catalog, &queue, &PublishToQueue);
    win32::WindowsRuntimeRoutePort route(nullptr);
    FakeClock clock;
    NoLaunch launcher;
    ProgramRuntime runtime({}, catalog, output, route, launcher, clock);
    Check(runtime.Activate(program).activated, "compiled mouse program activates through Windows ports");
    win32::WindowsRuntimeInputAdapter input(catalog);
    input.SeedPointerPosition({100, 200});
    win32::WindowsPointerOutput pointer;
    const InputInjector injector(0xabc, &CapturePointerInput);
    const auto desktop = TestPointerDesktop();
    ScreenPoint position{100, 200};
    g_pointerInputs.clear();
    std::uint64_t previousSequence{};
    for (int index = 0; index < 2; ++index) {
        WindowsNativeInputEvent event{};
        event.device = DeviceKind::Mouse;
        event.origin = InputOrigin::PhysicalCandidate;
        event.transition = Transition::Move;
        event.position = {101 + index, 200};
        const auto decision = runtime.HandleInput(input.Normalize(event));
        input.CompleteInput(event, decision);
        position = event.position;
        (void)runtime.Pump();
        WindowsOutputItem item{};
        Check(queue.TryPop(item) && item.recipe.kind == WindowsOutputKind::Pointer
            && item.pointer.x == 0.5 && item.sourceSequence > previousSequence,
            "each mouse rule task enters the shared ordered Windows queue");
        previousSequence = item.sourceSequence;
        auto prepared = pointer.Prepare(item.pointer, item.outputStateGeneration, position, desktop);
        Check(injector.InjectPrepared(prepared.native).Succeeded(), "queued pointer output reaches the fake native sender");
        pointer.Commit(prepared);
        position = prepared.destination;
    }
    Check(position == ScreenPoint{103, 200} && g_pointerInputs.size() == 1 && !runtime.HasOwnedOutputs(),
        "two short mouse tasks share fractional output without control ownership");
}
