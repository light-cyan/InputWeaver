[[nodiscard]] inputweaver::CompiledProgramStorage MakeActionFixtureStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.strings.push_back("tool.exe --flag");
    storage.strings.push_back("enabled");
    storage.userValues.initialStates = {0U};
    storage.valueRefs = {
        {ValueDomain::UserState, ValueType::State, 0U},
    };
    storage.debugInfo.variables = {
        {StringId{2U}, ValueRefId{0U}, source},
    };
    storage.numberConstants = {2.0};
    storage.durationConstants = {{5}};
    storage.expressions.clear();
    storage.expressionCode.clear();
    AppendExpression(storage, ExpressionType::Duration, 1U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::State, 1U, {
        {ExpressionOpcode::PushState, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 1U, {
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);

    storage.actionPrograms = {{{0U, 16U}, 1U, 1U, source}};
    storage.actionCode = {
        {ActionOpcode::Press, 0U, 0U},
        {ActionOpcode::Release, 0U, 0U},
        {ActionOpcode::Toggle, 0U, 0U},
        {ActionOpcode::Set, 0U, 1U},
        {ActionOpcode::Wait, 0U, 0U},
        {ActionOpcode::Gap, 0U, 0U},
        {ActionOpcode::Exec, 1U, 0U},
        {ActionOpcode::JumpIfFalse, 3U, 9U},
        {ActionOpcode::Press, 0U, 0U},
        {ActionOpcode::RepeatInit, 0U, 2U},
        {ActionOpcode::RepeatCheck, 0U, 15U},
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::RepeatNext, 0U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 10U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(), source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeInstructionBudgetStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.numberConstants = {1.0e12};
    storage.durationConstants = {{0}};
    storage.expressions.clear();
    storage.expressionCode.clear();
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 1U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    storage.actionPrograms = {{{0U, 7U}, 1U, 0U, source}};
    storage.actionCode = {
        {ActionOpcode::RepeatInit, 0U, 0U},
        {ActionOpcode::RepeatCheck, 0U, 6U},
        {ActionOpcode::Wait, 1U, 0U},
        {ActionOpcode::RepeatNext, 0U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 1U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.controlRequirements = {
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(), source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeOutputBudgetStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.numberConstants = {1.0e12};
    storage.expressions.clear();
    storage.expressionCode.clear();
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    storage.actionPrograms = {{{0U, 8U}, 1U, 1U, source}};
    storage.actionCode = {
        {ActionOpcode::RepeatInit, 0U, 0U},
        {ActionOpcode::RepeatCheck, 0U, 7U},
        {ActionOpcode::Press, 0U, 0U},
        {ActionOpcode::Release, 0U, 0U},
        {ActionOpcode::RepeatNext, 0U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 1U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(), source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeMaximumDispatchStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.actionPrograms.clear();
    storage.actionCode.clear();
    storage.expressions = {{{0U, 16U}, ExpressionType::Boolean, 1U, source}};
    storage.expressionCode = {
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
    };
    for (std::size_t index = 0U; index < 14U; ++index) {
        storage.expressionCode.push_back({
            ExpressionOpcode::Unary,
            ExpressionType::Boolean,
            static_cast<std::uint32_t>(UnaryOperator::BooleanNot),
            0U});
    }
    storage.expressionCode.push_back({
        ExpressionOpcode::Return,
        ExpressionType::Boolean,
        0U,
        0U});
    storage.rules.clear();
    for (std::uint32_t index = 0U; index < 256U; ++index) {
        storage.rules.push_back({
            ExpressionId{0U},
            ActionProgramId{},
            MappingId{},
            Delivery::Observe,
            index + 1U == 256U ? MatchFlow::Stop : MatchFlow::Continue,
            RuleKind::Event,
            index,
            source});
    }
    storage.eventBuckets = {{
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 256U}}};
    storage.controlRequirements = {
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);
    storage.debugInfo.actionInstructionSpans.clear();
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeManyReadyTasksStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = MakeInstructionBudgetStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.rules.clear();
    for (std::uint32_t index = 0U; index < 16U; ++index) {
        storage.rules.push_back({
            ExpressionId{},
            ActionProgramId{0U},
            MappingId{},
            Delivery::Consume,
            index + 1U == 16U ? MatchFlow::Stop : MatchFlow::Continue,
            RuleKind::Event,
            index,
            source});
    }
    storage.eventBuckets = {{
        {ControlRefId{1U}, EventTransition::Down},
        {0U, 16U}}};
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestActionVm()
{
    RuntimeHarness harness(ProcessLaunchCapacities());
    const auto program = Finalize(MakeActionFixtureStorage());
    const std::uint32_t trigger = TriggerControl(*program);
    Check(harness.runtime.Activate(program).activated, "action fixture activates");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        trigger,
        inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    Check(harness.output.requests.size() == 2U, "adjacent press and release execute in one slice");
    harness.clock.Advance(5);
    (void)harness.runtime.Pump();
    harness.clock.Advance(10'000'000);
    (void)harness.runtime.Pump();
    Check(
        harness.launcher.commands.size() == 1U
            && harness.launcher.commands[0] == "tool.exe --flag",
        "exec preserves authored command and continues immediately");
    harness.clock.Advance(30'000'000);
    (void)harness.runtime.Pump();
    harness.clock.Advance(30'000'000);
    (void)harness.runtime.Pump();
    bool state = true;
    Check(
        harness.runtime.ReadUserState(0U, state) && !state,
        "toggle and set publish lock-protected state changes");
    Check(
        harness.output.requests.size() == 6U
            && harness.runtime.ActiveTaskCount() == 0U,
        "action VM completes two cooperative repeat iterations");
}

void TestZeroDurationActionsStayInCurrentSlice()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = MakeActionFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.durationConstants[0U] = {0};
    storage.settings.tapDuration = {0};
    storage.settings.actionGap = {0};
    storage.actionPrograms = {{{0U, 4U}, 0U, 1U, source}};
    storage.actionCode = {
        {ActionOpcode::Wait, 0U, 0U},
        {ActionOpcode::Gap, 0U, 0U},
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans.assign(4U, source);

    RuntimeHarness harness;
    const auto program = Finalize(std::move(storage));
    Check(harness.runtime.Activate(program).activated, "zero-duration fixture activates");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*program),
        Transition::Down));
    const RuntimePumpResult result = harness.runtime.Pump(1U);
    Check(
        result.slices == 1U
            && !result.readyWorkRemaining
            && !result.timedWorkRemaining
            && harness.runtime.ActiveTaskCount() == 0U
            && harness.runtime.Metrics().completedTasks == 1U
            && harness.output.requests.size() == 2U
            && harness.output.requests[0U].transition == RuntimeOutputTransition::Down
            && harness.output.requests[1U].transition == RuntimeOutputTransition::Up,
        "zero-duration wait, gap, and tap complete in the current task slice");
}

void TestTaskProgressBudgets()
{
    const inputweaver::RuntimeCapacities defaults{};
    Check(
        defaults.maximumTaskInstructionsWithoutSuspension == 65'536U
            && defaults.maximumTaskOutputsWithoutSuspension == 4'096U
            && defaults.maximumContinuouslyReadyQuanta == 16U
            && defaults.continuouslyReadyBackoffNanoseconds == 1'000'000
            && defaults.maximumOutputTransitionsPerInterval == 2'048U
            && defaults.outputRateIntervalNanoseconds == 1'000'000'000
            && !defaults.permitProcessLaunch,
        "Phase 4 asynchronous safety capacities remain frozen");
    const auto instructionProgram = Finalize(MakeInstructionBudgetStorage());
    inputweaver::RuntimeCapacities instructionCapacities{};
    instructionCapacities.maximumTaskInstructionsWithoutSuspension = 12U;
    RuntimeHarness instructionHarness(instructionCapacities);
    Check(
        instructionHarness.runtime.Activate(instructionProgram).activated,
        "instruction-budget fixture activates");
    (void)instructionHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*instructionProgram),
        inputweaver::Transition::Down));
    (void)instructionHarness.runtime.Pump();
    bool foundInstructionBudget = false;
    inputweaver::RuntimeDiagnosticRecord diagnostic{};
    while (instructionHarness.runtime.TryPopDiagnostic(diagnostic)) {
        foundInstructionBudget = foundInstructionBudget
            || (diagnostic.kind
                    == inputweaver::RuntimeDiagnosticKind::TaskBudgetExceeded
                && diagnostic.detail == 1U);
    }
    Check(
        foundInstructionBudget
            && instructionHarness.runtime.ActiveTaskCount() == 0U
            && instructionHarness.runtime.Metrics().cancelledTasks == 1U
            && !instructionHarness.runtime.FatalShutdownRequested(),
        "zero-duration waits do not reset the task instruction budget");

    inputweaver::CompiledProgramStorage zeroGapStorage =
        MakeInstructionBudgetStorage();
    zeroGapStorage.actionCode[2U] = {
        inputweaver::ActionOpcode::Gap,
        0U,
        0U};
    zeroGapStorage.settings.actionGap = {0};
    zeroGapStorage.requirements = inputweaver::ComputeProgramRequirements(
        zeroGapStorage);
    const auto zeroGapProgram = Finalize(std::move(zeroGapStorage));
    RuntimeHarness zeroGapHarness(instructionCapacities);
    Check(
        zeroGapHarness.runtime.Activate(zeroGapProgram).activated,
        "zero-gap instruction-budget fixture activates");
    (void)zeroGapHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*zeroGapProgram),
        inputweaver::Transition::Down));
    (void)zeroGapHarness.runtime.Pump();
    Check(
        zeroGapHarness.runtime.ActiveTaskCount() == 0U
            && zeroGapHarness.runtime.Metrics().cancelledTasks == 1U,
        "zero-duration action gap does not reset the instruction budget");

    inputweaver::CompiledProgramStorage positiveWaitStorage =
        MakeInstructionBudgetStorage();
    positiveWaitStorage.numberConstants[0U] = 3.0;
    positiveWaitStorage.durationConstants[0U] = {1};
    positiveWaitStorage.requirements = inputweaver::ComputeProgramRequirements(
        positiveWaitStorage);
    const auto positiveWaitProgram = Finalize(std::move(positiveWaitStorage));
    inputweaver::RuntimeCapacities positiveWaitCapacities{};
    positiveWaitCapacities.maximumTaskInstructionsWithoutSuspension = 5U;
    RuntimeHarness positiveWaitHarness(positiveWaitCapacities);
    Check(
        positiveWaitHarness.runtime.Activate(positiveWaitProgram).activated,
        "positive-wait budget fixture activates");
    (void)positiveWaitHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*positiveWaitProgram),
        inputweaver::Transition::Down));
    for (std::size_t iteration = 0U;
         iteration < 10U
            && positiveWaitHarness.runtime.ActiveTaskCount() != 0U;
         ++iteration) {
        (void)positiveWaitHarness.runtime.Pump();
        positiveWaitHarness.clock.Advance(1);
    }
    (void)positiveWaitHarness.runtime.Pump();
    Check(
        positiveWaitHarness.runtime.Metrics().completedTasks == 1U
            && positiveWaitHarness.runtime.Metrics().cancelledTasks == 0U,
        "positive-duration suspension resets progress for a bounded long loop");

    const auto outputProgram = Finalize(MakeOutputBudgetStorage());
    inputweaver::RuntimeCapacities outputCapacities{};
    outputCapacities.maximumTaskOutputsWithoutSuspension = 2U;
    RuntimeHarness outputHarness(outputCapacities);
    Check(
        outputHarness.runtime.Activate(outputProgram).activated,
        "output-budget fixture activates");
    (void)outputHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*outputProgram),
        inputweaver::Transition::Down));
    (void)outputHarness.runtime.Pump();
    bool foundOutputBudget = false;
    while (outputHarness.runtime.TryPopDiagnostic(diagnostic)) {
        foundOutputBudget = foundOutputBudget
            || (diagnostic.kind
                    == inputweaver::RuntimeDiagnosticKind::TaskBudgetExceeded
                && diagnostic.detail == 2U);
    }
    Check(
        foundOutputBudget
            && outputHarness.output.requests.size() == 4U
            && outputHarness.runtime.ActiveTaskCount() == 0U
            && outputHarness.runtime.Metrics().cancelledTasks == 1U
            && !outputHarness.runtime.HasOwnedOutputs(),
        "task output exhaustion cancels only its owner and preserves releases");

    inputweaver::RuntimeCapacities schedulerCapacities{};
    schedulerCapacities.maximumTaskInstructionsWithoutSuspension = 1'000'000U;
    schedulerCapacities.maximumContinuouslyReadyQuanta = 1U;
    schedulerCapacities.continuouslyReadyBackoffNanoseconds = 5'000'000;
    RuntimeHarness schedulerHarness(schedulerCapacities);
    Check(
        schedulerHarness.runtime.Activate(instructionProgram).activated
            && schedulerHarness.runtime.StartTaskThread(),
        "scheduler-backoff fixture starts");
    (void)schedulerHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*instructionProgram),
        inputweaver::Transition::Down));
    for (std::size_t attempt = 0U;
         attempt < 100U
            && schedulerHarness.runtime.Metrics().schedulerBackoffs == 0U;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    schedulerHarness.runtime.StopTaskThread();
    Check(
        schedulerHarness.runtime.Metrics().schedulerBackoffs != 0U,
        "continuously ready work enters bounded scheduler backoff");
    schedulerHarness.runtime.RequestShutdown();
    (void)schedulerHarness.runtime.Pump();

    const auto manyReadyProgram = Finalize(MakeManyReadyTasksStorage());
    inputweaver::RuntimeCapacities manyReadyCapacities{};
    manyReadyCapacities.taskSlotCount = 16U;
    manyReadyCapacities.maximumTaskInstructionsWithoutSuspension = 1'000'000U;
    manyReadyCapacities.maximumContinuouslyReadyQuanta = 1U;
    manyReadyCapacities.continuouslyReadyBackoffNanoseconds = 5'000'000;
    RuntimeHarness manyReadyHarness(manyReadyCapacities);
    Check(
        manyReadyHarness.runtime.Activate(manyReadyProgram).activated
            && manyReadyHarness.runtime.StartTaskThread(),
        "maximum configured ready-task fixture starts");
    const std::uint32_t readyTrigger = TriggerControl(*manyReadyProgram);
    Check(
        manyReadyHarness.runtime.HandleInput(KeyboardEvent(
            readyTrigger,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "maximum configured ready tasks commit atomically");
    for (std::size_t attempt = 0U;
         attempt < 100U
            && manyReadyHarness.runtime.Metrics().schedulerBackoffs == 0U;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(
        RequestCompiledExit(manyReadyHarness.runtime, *manyReadyProgram)
            == inputweaver::InputDecision::Suppress,
        "compiled exit interrupts maximum continuously ready work");
    for (std::size_t attempt = 0U;
         attempt < 100U
            && manyReadyHarness.runtime.ActiveTaskCount() != 0U;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    manyReadyHarness.runtime.StopTaskThread();
    (void)manyReadyHarness.runtime.Pump();
    Check(
        manyReadyHarness.runtime.Metrics().schedulerBackoffs != 0U
            && manyReadyHarness.runtime.ActiveTaskCount() == 0U
            && manyReadyHarness.runtime.Metrics().cancelledTasks == 16U,
        "scheduler backoff remains responsive to exit cancellation");
}

void TestMaximumSynchronousDispatch()
{
    const inputweaver::RuntimeCapacities defaults{};
    Check(
        defaults.maximumExitRulesPerEvent == 64U
            && defaults.maximumPauseRulesPerEvent == 64U
            && defaults.maximumRulesPerEvent == 256U
            && defaults.maximumPredicateStepsPerEvent == 4096U
            && defaults.maximumMappingOperationsPerEvent == 64U,
        "synchronous safety capacities remain frozen");
    const auto program = Finalize(MakeMaximumDispatchStorage());
    Check(
        program->Requirements().maximumRulesPerEvent
                == defaults.maximumRulesPerEvent
            && program->Requirements().maximumPredicateStepsPerEvent
                == defaults.maximumPredicateStepsPerEvent,
        "worst-case fixture derives the frozen rule and predicate limits");
    RuntimeHarness harness;
    Check(
        harness.runtime.Activate(program).activated,
        "worst-case synchronous fixture activates at exact limits");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(
            TriggerControl(*program),
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward
            && harness.runtime.Metrics().dispatchedEvents == 1U
            && !harness.runtime.FatalShutdownRequested(),
        "worst-case accepted event completes within its derived predicate work");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeNestedRepeatStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.numberConstants = {2.0, 3.0};
    storage.expressions.clear();
    storage.expressionCode.clear();
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    storage.actionPrograms = {{{0U, 13U}, 2U, 1U, source}};
    storage.actionCode = {
        {ActionOpcode::RepeatInit, 0U, 0U},
        {ActionOpcode::RepeatCheck, 0U, 12U},
        {ActionOpcode::RepeatInit, 1U, 1U},
        {ActionOpcode::RepeatCheck, 1U, 9U},
        {ActionOpcode::Press, 0U, 0U},
        {ActionOpcode::Release, 0U, 0U},
        {ActionOpcode::RepeatNext, 1U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 3U, 0U},
        {ActionOpcode::RepeatNext, 0U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 1U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(), source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestNestedRepeatScheduling()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakeNestedRepeatStorage());
    Check(harness.runtime.Activate(program).activated, "nested repeat fixture activates");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*program),
        inputweaver::Transition::Down));
    const auto result = harness.runtime.Pump();
    Check(
        !result.readyWorkRemaining
            && harness.runtime.ActiveTaskCount() == 0U
            && harness.output.requests.size() == 12U,
        "nested repeat frames preserve cooperative yielded loop ordering");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeConcurrentStateStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.strings.push_back("sharedState");
    storage.userValues.initialStates = {0U};
    storage.valueRefs = {{ValueDomain::UserState, ValueType::State, 0U}};
    storage.debugInfo.variables = {{StringId{1U}, ValueRefId{0U}, source}};
    storage.expressions.clear();
    storage.expressionCode.clear();
    AppendExpression(storage, ExpressionType::State, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
    });
    storage.actionPrograms = {{{0U, 2U}, 0U, 0U, source}};
    storage.actionCode = {
        {ActionOpcode::Toggle, 0U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.controlRequirements = {
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.debugInfo.expressionInstructionSpans.assign(2U, source);
    storage.debugInfo.actionInstructionSpans.assign(2U, source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestConcurrentStateLocks()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakeConcurrentStateStorage());
    Check(harness.runtime.Activate(program).activated, "concurrent state fixture activates");
    Check(harness.runtime.StartTaskThread(), "concurrent state task thread starts");
    std::atomic<bool> stopReader{false};
    std::atomic<std::size_t> readerPasses{0U};
    std::thread reader([&harness, &stopReader, &readerPasses]() {
        while (!stopReader.load(std::memory_order_acquire)) {
            bool value = false;
            const auto evaluated = harness.runtime.EvaluateExpression(
                inputweaver::ExpressionId{0U});
            if (harness.runtime.ReadUserState(0U, value)
                && evaluated.Succeeded()
                && evaluated.value.type == inputweaver::ExpressionType::State
                && evaluated.value.stateValue <= 1U) {
                readerPasses.fetch_add(1U, std::memory_order_relaxed);
            }
        }
    });
    const std::uint32_t trigger = TriggerControl(*program);
    constexpr std::size_t taskCount = 128U;
    bool allInputsAccepted = true;
    for (std::size_t index = 0U; index < taskCount; ++index) {
        const inputweaver::InputDecision decision = harness.runtime.HandleInput(
            KeyboardEvent(trigger, inputweaver::Transition::Down));
        allInputsAccepted = allInputsAccepted
            && decision == inputweaver::InputDecision::Suppress;
        (void)harness.runtime.HandleInput(KeyboardEvent(
            trigger,
            inputweaver::Transition::Up));
    }
    for (std::size_t attempt = 0U;
         attempt < 200U
            && harness.runtime.Metrics().completedTasks < taskCount;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    harness.runtime.StopTaskThread();
    stopReader.store(true, std::memory_order_release);
    reader.join();
    bool finalValue = true;
    Check(
        allInputsAccepted
            && harness.runtime.Metrics().completedTasks == taskCount
            && harness.runtime.ReadUserState(0U, finalValue)
            && !finalValue
            && readerPasses.load(std::memory_order_relaxed) != 0U
            && !harness.runtime.FatalShutdownRequested(),
        "blocking input snapshots retain every concurrent state event");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeEventSnapshotStorage(
    bool arrayFirst = false)
{
    using namespace inputweaver;
    CompiledProgramStorage storage = MakeConcurrentStateStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.strings.push_back("sharedArray");
    storage.arrays = {{ArrayElementType::State, {0U, 1U}}};
    storage.initialArrayStates = {0U};
    storage.debugInfo.arrays = {{StringId{2U}, ArrayId{0U}, source}};
    storage.numberConstants = {0.0};
    const ExpressionId index{
        static_cast<std::uint32_t>(storage.expressions.size())};
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    const ExpressionId flagOn{
        static_cast<std::uint32_t>(storage.expressions.size())};
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::LoadValue, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    const ExpressionId arrayOn{
        static_cast<std::uint32_t>(storage.expressions.size())};
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::LoadArrayElement, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    const ExpressionId arrayValue{
        static_cast<std::uint32_t>(storage.expressions.size())};
    AppendExpression(storage, ExpressionType::State, 1U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::LoadArrayElement, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
    });
    storage.debugInfo.expressionInstructionSpans.insert(
        storage.debugInfo.expressionInstructionSpans.end(),
        14U,
        source);
    storage.actionPrograms = {{{0U, 3U}, 0U, 0U, source}};
    storage.actionCode = arrayFirst
        ? std::vector<ActionInstruction>{
            {ActionOpcode::ToggleArrayElement, 0U, index.value, 0U},
            {ActionOpcode::Toggle, 0U, 0U, 0U},
            {ActionOpcode::End, 0U, 0U, 0U}}
        : std::vector<ActionInstruction>{
            {ActionOpcode::Toggle, 0U, 0U, 0U},
            {ActionOpcode::ToggleArrayElement, 0U, index.value, 0U},
            {ActionOpcode::End, 0U, 0U, 0U}};
    storage.debugInfo.actionInstructionSpans.assign(3U, source);
    storage.rules.push_back({
        flagOn,
        ActionProgramId{},
        MappingId{},
        Delivery::Consume,
        MatchFlow::Stop,
        RuleKind::Event,
        1U,
        source});
    storage.eventBuckets = {
        {{ControlRefId{0U}, EventTransition::Down}, {1U, 1U}},
        {{ControlRefId{1U}, EventTransition::Down}, {0U, 1U}},
    };
    storage.exitControlRules.push_back({flagOn, 2U, source});
    storage.exitControlBuckets.insert(
        storage.exitControlBuckets.begin(),
        {{ControlRefId{0U}, EventTransition::Down}, {1U, 1U}});
    storage.pauseControlRules = {
        {arrayOn, Delivery::Consume, PauseEffect::On, 3U, source},
    };
    storage.pauseControlBuckets = {
        {{ControlRefId{0U}, EventTransition::Down}, {0U, 1U}},
    };
    (void)arrayValue;
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestEventSnapshotAndCancelledMutation()
{
    using namespace inputweaver;
    const auto program = Finalize(MakeEventSnapshotStorage());
    const auto arrayFirstProgram = Finalize(MakeEventSnapshotStorage(true));
    const auto run = [&](const std::shared_ptr<const CompiledProgram>& candidate,
                         bool cancel) {
        struct Result final {
            InputDecision decision{InputDecision::Suppress};
            bool taskBlocked{};
            bool value{};
            bool arrayValue{};
            std::uint64_t cancelledTasks{};
        } result;
        RuntimeHarness harness;
        Check(harness.runtime.Activate(candidate).activated,
            "event snapshot fixture activates");
        const ControlRefId taskControl = FindKeyboardControl(*candidate, 0x3fU);
        const ControlRefId snapshotControl = FindKeyboardControl(*candidate, 0x40U);
        RuntimeInputEvent queued = KeyboardEvent(taskControl.value, Transition::Down);
        queued.debugCaptureEpoch = 1U;
        queued.debugInputSequence = 1U;
        Check(harness.runtime.HandleInput(queued) == InputDecision::Suppress,
            "snapshot fixture queues its mutation task");

        std::atomic<bool> dispatchEntered{false};
        std::atomic<bool> dispatchReleased{false};
        std::atomic<bool> executionStarted{false};
        std::atomic<bool> pumpDone{false};
        harness.route.dispatchEntered = &dispatchEntered;
        harness.route.dispatchReleased = &dispatchReleased;
        harness.debug.executionStarted = &executionStarted;
        std::thread eventThread([&] {
            result.decision = harness.runtime.HandleInput(
                KeyboardEvent(snapshotControl.value, Transition::Down));
        });
        const auto waitFor = [](const std::atomic<bool>& flag) {
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(1);
            while (!flag.load(std::memory_order_acquire)
                && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            return flag.load(std::memory_order_acquire);
        };
        Check(waitFor(dispatchEntered),
            "snapshot event reaches the controlled routing boundary");
        std::thread pumpThread([&] {
            (void)harness.runtime.Pump();
            pumpDone.store(true, std::memory_order_release);
        });
        Check(waitFor(executionStarted),
            "queued mutation starts while the event is in flight");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        result.taskBlocked = !pumpDone.load(std::memory_order_acquire);
        if (cancel) {
            harness.runtime.RequestShutdown();
        }
        dispatchReleased.store(true, std::memory_order_release);
        eventThread.join();
        pumpThread.join();
        (void)harness.runtime.ReadUserState(0U, result.value);
        const RuntimeEvaluationResult array = harness.runtime.EvaluateExpression(
            ExpressionId{4U});
        result.arrayValue = array.Succeeded() && array.value.stateValue != 0U;
        result.cancelledTasks = harness.runtime.Metrics().cancelledTasks;
        return result;
    };

    const auto committed = run(program, false);
    Check(
        committed.taskBlocked
            && committed.decision == InputDecision::Forward
            && committed.value
            && committed.arrayValue,
        "exit, PAUSE, and ordinary predicates share one event-start state");
    const auto cancelledScalar = run(program, true);
    const auto cancelledArray = run(arrayFirstProgram, true);
    Check(
        cancelledScalar.taskBlocked
            && cancelledScalar.decision == InputDecision::Forward
            && !cancelledScalar.value
            && !cancelledScalar.arrayValue
            && cancelledScalar.cancelledTasks == 1U
            && cancelledArray.taskBlocked
            && cancelledArray.decision == InputDecision::Forward
            && !cancelledArray.value
            && !cancelledArray.arrayValue
            && cancelledArray.cancelledTasks == 1U,
        "scalar and array mutations recheck generation after taking state locks");
}

void TestExecCancellationBoundaries()
{
    const auto program = Finalize(MakeActionFixtureStorage());
    const std::uint32_t trigger = TriggerControl(*program);
    RuntimeHarness cancelledBefore(ProcessLaunchCapacities());
    Check(
        cancelledBefore.runtime.Activate(program).activated,
        "pre-launch cancellation fixture activates");
    (void)cancelledBefore.runtime.HandleInput(KeyboardEvent(
        trigger,
        inputweaver::Transition::Down));
    cancelledBefore.runtime.RequestShutdown();
    (void)cancelledBefore.runtime.Pump();
    Check(
        cancelledBefore.launcher.commands.empty(),
        "cancellation before task execution prevents process creation");

    RuntimeHarness cancelledAfter(ProcessLaunchCapacities());
    Check(
        cancelledAfter.runtime.Activate(program).activated,
        "post-launch cancellation fixture activates");
    (void)cancelledAfter.runtime.HandleInput(KeyboardEvent(
        trigger,
        inputweaver::Transition::Down));
    (void)cancelledAfter.runtime.Pump();
    cancelledAfter.clock.Advance(5);
    (void)cancelledAfter.runtime.Pump();
    cancelledAfter.clock.Advance(10'000'000);
    (void)cancelledAfter.runtime.Pump();
    Check(
        cancelledAfter.launcher.commands.size() == 1U,
        "exec creates one child before later cancellation");
    cancelledAfter.runtime.RequestShutdown();
    (void)cancelledAfter.runtime.Pump();
    Check(
        cancelledAfter.launcher.commands.size() == 1U,
        "cancellation after creation does not acquire or terminate child ownership");

    RuntimeHarness failedLaunch(ProcessLaunchCapacities());
    failedLaunch.launcher.outcome = {
        inputweaver::RuntimeLaunchResult::CreationFailed,
        1234U};
    Check(
        failedLaunch.runtime.Activate(program).activated,
        "failed launch fixture activates");
    (void)failedLaunch.runtime.HandleInput(KeyboardEvent(
        trigger,
        inputweaver::Transition::Down));
    (void)failedLaunch.runtime.Pump();
    failedLaunch.clock.Advance(5);
    (void)failedLaunch.runtime.Pump();
    failedLaunch.clock.Advance(10'000'000);
    (void)failedLaunch.runtime.Pump();
    Check(
        failedLaunch.launcher.commands.size() == 1U
            && failedLaunch.runtime.ActiveTaskCount() == 0U
            && !failedLaunch.runtime.FatalShutdownRequested(),
        "native launch failure ends only the current task");
    inputweaver::RuntimeDiagnosticRecord launchDiagnostic{};
    bool foundLaunchFailure = false;
    while (failedLaunch.runtime.TryPopDiagnostic(launchDiagnostic)) {
        foundLaunchFailure = foundLaunchFailure
            || (launchDiagnostic.kind
                    == inputweaver::RuntimeDiagnosticKind::LaunchFailure
                && launchDiagnostic.detail == static_cast<std::uint32_t>(
                    inputweaver::RuntimeLaunchResult::CreationFailed)
                && launchDiagnostic.platformError == 1234U);
    }
    Check(foundLaunchFailure, "launch diagnostics retain the platform error");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeTaskExpressionFaultStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.numberConstants = {0.0};
    storage.durationConstants = {{1}};
    storage.expressions = {{{0U, 4U}, ExpressionType::Duration, 2U, source}};
    storage.expressionCode = {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Duration,
            static_cast<std::uint32_t>(BinaryOperator::DurationDivideNumber), 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    };
    storage.actionPrograms = {{{0U, 2U}, 0U, 0U, source}};
    storage.actionCode = {
        {ActionOpcode::Wait, 0U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.controlRequirements = {
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.debugInfo.expressionInstructionSpans = {
        {1U, 1U},
        {3U, 1U},
        {7U, 3U},
        {11U, 1U},
    };
    storage.debugInfo.actionInstructionSpans.assign(2U, source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestTaskExpressionFault()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakeTaskExpressionFaultStorage());
    Check(harness.runtime.Activate(program).activated, "task expression fault fixture activates");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*program),
        inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    inputweaver::RuntimeDiagnosticRecord diagnostic{};
    bool foundFault = false;
    while (harness.runtime.TryPopDiagnostic(diagnostic)) {
        foundFault = foundFault
            || (diagnostic.kind
                    == inputweaver::RuntimeDiagnosticKind::TaskExpressionFault
                && diagnostic.subject == 0U
                && diagnostic.position == 2U
                && diagnostic.source == inputweaver::SourceSpan{7U, 3U}
                && diagnostic.detail == static_cast<std::uint32_t>(
                    inputweaver::RuntimeEvaluationFault::DivisionByZero));
    }
    Check(
        harness.runtime.FatalShutdownRequested()
            && harness.runtime.ActiveTaskCount() == 0U
            && foundFault,
        "task expression faults report the exact expression instruction span");
}

void TestRuntimeDebugEvents()
{
    FakeClock clock;
    FakeControlPort controls;
    FakeOutputPort output;
    FakeRoutePort route;
    FakeLauncher launcher;
    FakeDebugPort debug;
    inputweaver::ProgramRuntime runtime(
        {},
        controls,
        output,
        route,
        launcher,
        clock,
        &debug);
    const auto program = Finalize(inputweaver::test::MakeTapFixtureStorage());
    Check(runtime.Activate(program).activated, "debug event fixture activates");
    inputweaver::RuntimeInputEvent event = KeyboardEvent(
        TriggerControl(*program),
        inputweaver::Transition::Down);
    event.debugCaptureEpoch = 4U;
    event.debugInputSequence = 9U;
    (void)runtime.HandleInput(event);
    (void)runtime.Pump();
    clock.Advance(30'000'000);
    (void)runtime.Pump();
    Check(
        debug.events.size() == 2U,
        "accepted task emits one match and one terminal event");
    if (debug.events.size() == 2U) {
        const std::uint64_t marker = debug.events[0].executionMarker;
        Check(
            debug.events[0].kind
                    == inputweaver::RuntimeDebugEventKind::RuleMatched
                && debug.events[0].captureEpoch == 4U
                && debug.events[0].triggerInputSequence == 9U
                && debug.events[0].ruleIndex == 0U
                && marker != 0U,
            "rule match carries capture, input, rule, and marker correlation");
        Check(
            debug.events[1].kind
                    == inputweaver::RuntimeDebugEventKind::ExecutionEnded
                && debug.events[1].executionMarker == marker
                && debug.events[1].result
                    == inputweaver::RuntimeExecutionResult::Completed,
            "completed task emits one correlated terminal result");
    }

    FakeClock mappingClock;
    FakeControlPort mappingControls;
    FakeOutputPort mappingOutput;
    FakeRoutePort mappingRoute;
    FakeLauncher mappingLauncher;
    FakeDebugPort mappingDebug;
    inputweaver::ProgramRuntime mappingRuntime(
        {},
        mappingControls,
        mappingOutput,
        mappingRoute,
        mappingLauncher,
        mappingClock,
        &mappingDebug);
    const auto mappingProgram = Finalize(
        inputweaver::test::MakeMappingFixtureStorage());
    Check(
        mappingRuntime.Activate(mappingProgram).activated,
        "mapping debug event fixture activates");
    inputweaver::RuntimeInputEvent mappingDown = KeyboardEvent(
        TriggerControl(*mappingProgram),
        inputweaver::Transition::Down);
    mappingDown.debugCaptureEpoch = 7U;
    mappingDown.debugInputSequence = 3U;
    (void)mappingRuntime.HandleInput(mappingDown);
    (void)mappingRuntime.Pump();
    inputweaver::RuntimeInputEvent mappingUp = mappingDown;
    mappingUp.transition = inputweaver::Transition::Up;
    mappingUp.debugInputSequence = 4U;
    (void)mappingRuntime.HandleInput(mappingUp);
    (void)mappingRuntime.Pump();
    Check(
        mappingDebug.events.size() == 2U
            && mappingDebug.events[0].kind
                == inputweaver::RuntimeDebugEventKind::RuleMatched
            && mappingDebug.events[0].captureEpoch == 7U
            && mappingDebug.events[0].triggerInputSequence == 3U
            && mappingDebug.events[1].kind
                == inputweaver::RuntimeDebugEventKind::ExecutionEnded
            && mappingDebug.events[1].result
                == inputweaver::RuntimeExecutionResult::Completed,
        "mapping lifetime emits one correlated debug execution");

    FakeClock pauseClock;
    FakeControlPort pauseControls;
    FakeOutputPort pauseOutput;
    FakeRoutePort pauseRoute;
    FakeLauncher pauseLauncher;
    FakeDebugPort pauseDebug;
    inputweaver::ProgramRuntime pauseRuntime(
        {},
        pauseControls,
        pauseOutput,
        pauseRoute,
        pauseLauncher,
        pauseClock,
        &pauseDebug);
    const auto pauseProgram = Finalize(
        inputweaver::test::MakePauseControlFixtureStorage());
    Check(
        pauseRuntime.Activate(pauseProgram).activated,
        "pause debug event fixture activates");
    inputweaver::RuntimeInputEvent pauseEvent = KeyboardEvent(
        TriggerControl(*pauseProgram),
        inputweaver::Transition::Down);
    pauseEvent.debugCaptureEpoch = 8U;
    (void)pauseRuntime.HandleInput(pauseEvent);
    Check(
        pauseDebug.events.size() == 1U
            && pauseDebug.events[0].kind
                == inputweaver::RuntimeDebugEventKind::StateChanged
            && !pauseDebug.events[0].value.reference.IsValid()
            && pauseDebug.events[0].value.type == inputweaver::ValueType::State
            && !pauseDebug.events[0].value.stateValue,
        "PAUSE changes publish the current state value");

    FakeClock failedClock;
    FakeControlPort failedControls;
    FakeOutputPort failedOutput;
    FakeRoutePort failedRoute;
    FakeLauncher failedLauncher;
    FakeDebugPort failedDebug;
    inputweaver::ProgramRuntime failedRuntime(
        {},
        failedControls,
        failedOutput,
        failedRoute,
        failedLauncher,
        failedClock,
        &failedDebug);
    const auto failedProgram = Finalize(MakeTaskExpressionFaultStorage());
    Check(
        failedRuntime.Activate(failedProgram).activated,
        "failed debug event fixture activates");
    inputweaver::RuntimeInputEvent failedEvent = KeyboardEvent(
        TriggerControl(*failedProgram),
        inputweaver::Transition::Down);
    failedEvent.debugCaptureEpoch = 5U;
    failedEvent.debugInputSequence = 1U;
    (void)failedRuntime.HandleInput(failedEvent);
    (void)failedRuntime.Pump();
    const auto terminal = std::find_if(
        failedDebug.events.begin(),
        failedDebug.events.end(),
        [](const inputweaver::RuntimeDebugEvent& current) {
            return current.kind
                == inputweaver::RuntimeDebugEventKind::ExecutionEnded;
        });
    Check(
        terminal != failedDebug.events.end()
            && terminal->result == inputweaver::RuntimeExecutionResult::Failed
            && failedRuntime.Metrics().cancelledTasks == 1U,
        "debug failure remains distinct without changing runtime metrics");

    FakeClock cancelledClock;
    FakeControlPort cancelledControls;
    FakeOutputPort cancelledOutput;
    FakeRoutePort cancelledRoute;
    FakeLauncher cancelledLauncher;
    FakeDebugPort cancelledDebug;
    inputweaver::ProgramRuntime cancelledRuntime(
        {},
        cancelledControls,
        cancelledOutput,
        cancelledRoute,
        cancelledLauncher,
        cancelledClock,
        &cancelledDebug);
    Check(
        cancelledRuntime.Activate(program).activated,
        "cancelled debug event fixture activates");
    inputweaver::RuntimeInputEvent cancelledEvent = KeyboardEvent(
        TriggerControl(*program),
        inputweaver::Transition::Down);
    cancelledEvent.debugCaptureEpoch = 6U;
    cancelledEvent.debugInputSequence = 1U;
    (void)cancelledRuntime.HandleInput(cancelledEvent);
    (void)cancelledRuntime.Pump();
    cancelledRuntime.RequestShutdown();
    (void)cancelledRuntime.Pump();
    const auto cancelledTerminal = std::find_if(
        cancelledDebug.events.begin(),
        cancelledDebug.events.end(),
        [](const inputweaver::RuntimeDebugEvent& current) {
            return current.kind
                == inputweaver::RuntimeDebugEventKind::ExecutionEnded;
        });
    Check(
        cancelledTerminal != cancelledDebug.events.end()
            && cancelledTerminal->result
                == inputweaver::RuntimeExecutionResult::Cancelled
            && cancelledRuntime.Metrics().cancelledTasks == 1U,
        "runtime invalidation emits a cancelled terminal result");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeRepeatedOwnershipStorage(
    bool unownedRelease)
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    if (unownedRelease) {
        storage.actionPrograms = {{{0U, 2U}, 0U, 0U, source}};
        storage.actionCode = {
            {ActionOpcode::Release, 0U, 0U},
            {ActionOpcode::End, 0U, 0U},
        };
    } else {
        storage.actionPrograms = {{{0U, 4U}, 0U, 1U, source}};
        storage.actionCode = {
            {ActionOpcode::Press, 0U, 0U},
            {ActionOpcode::Press, 0U, 0U},
            {ActionOpcode::Release, 0U, 0U},
            {ActionOpcode::End, 0U, 0U},
        };
    }
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(), source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestOwnershipFaultsAndOutputFailure()
{
    RuntimeHarness repeated;
    const auto repeatedProgram = Finalize(MakeRepeatedOwnershipStorage(false));
    const std::uint32_t trigger = TriggerControl(*repeatedProgram);
    Check(repeated.runtime.Activate(repeatedProgram).activated, "repeated ownership fixture activates");
    (void)repeated.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down));
    (void)repeated.runtime.Pump();
    Check(
        repeated.output.requests.size() == 2U
            && repeated.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && repeated.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up,
        "repeated task acquisition emits only zero-to-one and one-to-zero edges");

    RuntimeHarness unowned;
    const auto unownedProgram = Finalize(MakeRepeatedOwnershipStorage(true));
    Check(unowned.runtime.Activate(unownedProgram).activated, "unowned release fixture activates");
    (void)unowned.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*unownedProgram),
        inputweaver::Transition::Down));
    (void)unowned.runtime.Pump();
    Check(
        !unowned.runtime.FatalShutdownRequested()
            && unowned.runtime.ActiveTaskCount() == 0U
            && unowned.output.requests.empty(),
        "unowned release ends only its task without synthesizing output");

    RuntimeHarness failedOutput;
    const auto tap = Finalize(inputweaver::test::MakeTapFixtureStorage());
    Check(failedOutput.runtime.Activate(tap).activated, "output failure fixture activates");
    failedOutput.output.nextResult = inputweaver::RuntimeOutputResult::Failed;
    (void)failedOutput.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*tap),
        inputweaver::Transition::Down));
    (void)failedOutput.runtime.Pump();
    Check(
        failedOutput.runtime.FatalShutdownRequested()
            && failedOutput.fatalStopRequested.load(std::memory_order_acquire)
            && !failedOutput.runtime.HasOwnedOutputs(),
        "output publication failure notifies the host and releases ownership");

    RuntimeHarness retriedCleanup;
    Check(
        retriedCleanup.runtime.Activate(tap).activated,
        "cleanup retry fixture activates");
    (void)retriedCleanup.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*tap),
        inputweaver::Transition::Down));
    (void)retriedCleanup.runtime.Pump();
    retriedCleanup.output.nextResult = inputweaver::RuntimeOutputResult::Failed;
    retriedCleanup.runtime.RequestShutdown();
    (void)retriedCleanup.runtime.Pump();
    Check(
        retriedCleanup.runtime.FatalShutdownRequested()
            && retriedCleanup.output.requests.size() == 3U
            && retriedCleanup.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up
            && retriedCleanup.output.requests[2].transition
                == inputweaver::RuntimeOutputTransition::Up
            && retriedCleanup.runtime.ActiveTaskCount() == 0U
            && !retriedCleanup.runtime.HasOwnedOutputs(),
        "failed release remains cleanup-owned until a retry publishes its up edge");

    RuntimeHarness blockedReload;
    Check(
        blockedReload.runtime.Activate(tap).activated,
        "reload cleanup fixture activates");
    (void)blockedReload.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*tap),
        inputweaver::Transition::Down));
    (void)blockedReload.runtime.Pump();
    blockedReload.output.failuresRemaining = 6U;
    const auto mapping = Finalize(inputweaver::test::MakeMappingFixtureStorage());
    const auto blocked = blockedReload.runtime.Activate(mapping);
    Check(
        !blocked.activated
            && blocked.error.code
                == inputweaver::RuntimeActivationErrorCode::CleanupFailure
            && blockedReload.runtime.HasActiveProgram()
            && blockedReload.runtime.HasOwnedOutputs(),
        "reload cannot publish a replacement while prior release cleanup remains");
    (void)blockedReload.runtime.Pump();
    Check(
        !blockedReload.runtime.HasOwnedOutputs()
            && blockedReload.runtime.Activate(mapping).activated,
        "reload succeeds after retained cleanup ownership publishes its release");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeMappingTaskOverlapStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeMappingFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.actionPrograms = {{{0U, 2U}, 0U, 1U, source}};
    storage.actionCode = {
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans.assign(2U, source);
    storage.rules.push_back({
        ExpressionId{}, ActionProgramId{0U}, MappingId{}, Delivery::Observe,
        MatchFlow::Stop, RuleKind::Event, 1U, source});
    storage.eventBuckets.push_back({
        {ControlRefId{1U}, EventTransition::Again}, {1U, 1U}});
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestMappingAndTaskOwnershipOverlap()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakeMappingTaskOverlapStorage());
    Check(harness.runtime.Activate(program).activated, "mapping-task overlap fixture activates");
    const std::uint32_t trigger = TriggerControl(*program);
    (void)harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    (void)harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 2U
            && harness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && harness.output.requests[1].transition
            == inputweaver::RuntimeOutputTransition::Again,
        "mapping again and overlapping tap retain one global down owner");
    harness.clock.Advance(30'000'000);
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 2U,
        "tap release cannot release an active mapping owner");
    (void)harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Up));
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 3U
            && harness.output.requests.back().transition
                == inputweaver::RuntimeOutputTransition::Up,
        "mapping release emits the final global up edge");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakePredicateFaultStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.numberConstants = {1.0, 0.0};
    storage.expressions = {{{0U, 6U}, ExpressionType::Boolean, 2U, source}};
    storage.expressionCode = {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Number,
            static_cast<std::uint32_t>(BinaryOperator::NumberDivide), 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    };
    storage.debugInfo.expressionInstructionSpans.assign(6U, source);
    storage.rules[0].condition = ExpressionId{0U};
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestPredicateFaultAndDiagnostics()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakePredicateFaultStorage());
    Check(harness.runtime.Activate(program).activated, "predicate fault fixture activates");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(
            TriggerControl(*program),
            inputweaver::Transition::Down)) == inputweaver::InputDecision::Forward,
        "predicate fault forwards the undecided physical event");
    Check(
        harness.runtime.FatalShutdownRequested()
            && harness.runtime.ActiveTaskCount() == 0U
            && harness.output.requests.empty(),
        "predicate fault requests shutdown without task or output publication");
    inputweaver::RuntimeDiagnosticRecord diagnostic{};
    bool foundPredicateFault = false;
    while (harness.runtime.TryPopDiagnostic(diagnostic)) {
        foundPredicateFault = foundPredicateFault
            || diagnostic.kind == inputweaver::RuntimeDiagnosticKind::PredicateFault;
    }
    Check(foundPredicateFault, "predicate fault is visible through bounded diagnostics");
}
