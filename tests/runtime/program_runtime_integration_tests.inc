void TestProductionTaskThread()
{
    FakeControlPort controls;
    FakeOutputPort output;
    FakeRoutePort route;
    FakeLauncher launcher;
    inputweaver::SteadyRuntimeClock clock;
    inputweaver::ProgramRuntime runtime({}, controls, output, route, launcher, clock);
    const auto program = Finalize(inputweaver::test::MakeTapFixtureStorage());
    Check(runtime.Activate(program).activated, "production task-thread fixture activates");
    Check(runtime.StartTaskThread(), "cooperative production task thread starts");
    (void)runtime.HandleInput(KeyboardEvent(
        TriggerControl(*program),
        inputweaver::Transition::Down));
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(1);
    while (runtime.Metrics().completedTasks == 0U
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runtime.StopTaskThread();
    Check(
        output.requests.size() == 2U
            && output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up,
        "single cooperative task thread services cancellable timed tap");
}

void TestActivationAndTransientCapacity()
{
    const auto tap = Finalize(inputweaver::test::MakeTapFixtureStorage());
    const auto mapping = Finalize(inputweaver::test::MakeMappingFixtureStorage());
    const auto repeat = Finalize(
        inputweaver::test::MakeConditionalRepeatFixtureStorage());
    const auto values = Finalize(MakeExpressionFixtureStorage());
    const auto pause = Finalize(
        inputweaver::test::MakePauseControlFixtureStorage());
    const auto actions = Finalize(MakeActionFixtureStorage());
    const auto checkCapacityRejection = [](
                                            inputweaver::RuntimeCapacities capacities,
                                            const std::shared_ptr<const inputweaver::CompiledProgram>& program,
                                            inputweaver::RuntimeActivationErrorCode expected,
                                            std::string_view name) {
        RuntimeHarness harness(capacities);
        const auto result = harness.runtime.Activate(program);
        Check(!result.activated && result.error.code == expected, name);
    };
    const auto checkExactRejection = [](
                                         inputweaver::RuntimeCapacities capacities,
                                         const std::shared_ptr<const inputweaver::CompiledProgram>& program,
                                         inputweaver::RuntimeActivationError expected,
                                         std::string_view name) {
        RuntimeHarness harness(capacities);
        const auto result = harness.runtime.Activate(program);
        Check(
            !result.activated
                && result.error.code == expected.code
                && result.error.subject == expected.subject
                && result.error.required == expected.required
                && result.error.available == expected.available,
            name);
    };

    inputweaver::RuntimeCapacities controlCapacity{};
    controlCapacity.maximumControls = 1U;
    checkCapacityRejection(
        controlCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::ControlCapacity,
        "control capacity rejects activation before publication");
    for (const auto subject : {
             inputweaver::RuntimeActivationSubject::StateSlots,
             inputweaver::RuntimeActivationSubject::NumberSlots,
             inputweaver::RuntimeActivationSubject::DurationSlots}) {
        inputweaver::RuntimeCapacities valueCapacity{};
        if (subject == inputweaver::RuntimeActivationSubject::StateSlots) {
            valueCapacity.maximumStateSlots = 0U;
        } else if (subject == inputweaver::RuntimeActivationSubject::NumberSlots) {
            valueCapacity.maximumNumberSlots = 0U;
        } else {
            valueCapacity.maximumDurationSlots = 0U;
        }
        checkExactRejection(
            valueCapacity,
            values,
            {inputweaver::RuntimeActivationErrorCode::ValueCapacity,
             static_cast<std::uint32_t>(subject), 1U, 0U},
            "value capacity identifies one exact slot domain");
    }
    inputweaver::RuntimeCapacities mappingCapacity{};
    mappingCapacity.maximumMappingSlots = 0U;
    checkCapacityRejection(
        mappingCapacity,
        mapping,
        inputweaver::RuntimeActivationErrorCode::MappingCapacity,
        "mapping capacity rejects activation before publication");
    inputweaver::RuntimeCapacities exitRuleCapacity{};
    exitRuleCapacity.maximumExitRulesPerEvent =
        tap->Requirements().maximumExitRulesPerEvent - 1U;
    checkCapacityRejection(
        exitRuleCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::ExitRuleCapacity,
        "exit-rule hook-path capacity rejects activation");
    inputweaver::RuntimeCapacities pauseRuleCapacity{};
    pauseRuleCapacity.maximumPauseRulesPerEvent =
        pause->Requirements().maximumPauseRulesPerEvent - 1U;
    checkCapacityRejection(
        pauseRuleCapacity,
        pause,
        inputweaver::RuntimeActivationErrorCode::PauseRuleCapacity,
        "pause-rule hook-path capacity rejects activation");
    inputweaver::RuntimeCapacities ruleCapacity{};
    ruleCapacity.maximumRulesPerEvent =
        tap->Requirements().maximumRulesPerEvent - 1U;
    checkCapacityRejection(
        ruleCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::RuleCapacity,
        "ordinary-rule hook-path capacity rejects activation");
    inputweaver::RuntimeCapacities predicateCapacity{};
    predicateCapacity.maximumPredicateStepsPerEvent =
        repeat->Requirements().maximumPredicateStepsPerEvent - 1U;
    checkCapacityRejection(
        predicateCapacity,
        repeat,
        inputweaver::RuntimeActivationErrorCode::PredicateStepCapacity,
        "predicate-step hook-path capacity rejects activation");
    inputweaver::RuntimeCapacities mappingOperationCapacity{};
    mappingOperationCapacity.maximumMappingOperationsPerEvent =
        mapping->Requirements().maximumMappingOperationsPerEvent - 1U;
    checkCapacityRejection(
        mappingOperationCapacity,
        mapping,
        inputweaver::RuntimeActivationErrorCode::MappingOperationCapacity,
        "mapping-operation hook-path capacity rejects activation");
    inputweaver::RuntimeCapacities expressionCapacity{};
    expressionCapacity.maximumExpressionStackDepth = 1U;
    checkCapacityRejection(
        expressionCapacity,
        repeat,
        inputweaver::RuntimeActivationErrorCode::ExpressionStackCapacity,
        "expression stack capacity rejects activation before publication");
    inputweaver::RuntimeCapacities repeatCapacity{};
    repeatCapacity.maximumRepeatFramesPerTask = 0U;
    checkCapacityRejection(
        repeatCapacity,
        repeat,
        inputweaver::RuntimeActivationErrorCode::RepeatFrameCapacity,
        "repeat-frame capacity rejects activation before publication");
    inputweaver::RuntimeCapacities ownershipCapacity{};
    ownershipCapacity.maximumOwnedControlsPerTask = 0U;
    checkCapacityRejection(
        ownershipCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::OwnershipCapacity,
        "ownership capacity rejects activation before publication");
    inputweaver::RuntimeCapacities insufficient{};
    insufficient.taskSlotCount = 0U;
    checkCapacityRejection(
        insufficient,
        tap,
        inputweaver::RuntimeActivationErrorCode::TaskCapacity,
        "static task insufficiency rejects activation");
    inputweaver::RuntimeCapacities transactionCapacity{};
    transactionCapacity.transactionQueueItemCount = 0U;
    checkCapacityRejection(
        transactionCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::TransactionCapacity,
        "transaction capacity rejects activation before publication");
    inputweaver::RuntimeCapacities diagnosticCapacity{};
    diagnosticCapacity.diagnosticRecordCount = 0U;
    checkCapacityRejection(
        diagnosticCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::DiagnosticCapacity,
        "diagnostic capacity rejects activation before publication");
    inputweaver::RuntimeCapacities instructionBudget{};
    instructionBudget.maximumTaskInstructionsWithoutSuspension = 0U;
    checkCapacityRejection(
        instructionBudget,
        tap,
        inputweaver::RuntimeActivationErrorCode::TaskInstructionCapacity,
        "zero task instruction budget rejects activation");
    inputweaver::RuntimeCapacities outputBudget{};
    outputBudget.maximumTaskOutputsWithoutSuspension = 0U;
    checkCapacityRejection(
        outputBudget,
        tap,
        inputweaver::RuntimeActivationErrorCode::TaskOutputCapacity,
        "zero task output budget rejects activation");
    inputweaver::RuntimeCapacities schedulerCapacity{};
    schedulerCapacity.maximumContinuouslyReadyQuanta = 0U;
    checkExactRejection(
        schedulerCapacity,
        tap,
        {inputweaver::RuntimeActivationErrorCode::InvalidSchedulerConfiguration,
         static_cast<std::uint32_t>(inputweaver::RuntimeActivationSubject::MaximumContinuouslyReadyQuanta), 1U, 0U},
        "invalid scheduler backoff capacity rejects activation");
    inputweaver::RuntimeCapacities schedulerDurationCapacity{};
    schedulerDurationCapacity.continuouslyReadyBackoffNanoseconds = 0;
    checkExactRejection(
        schedulerDurationCapacity,
        tap,
        {inputweaver::RuntimeActivationErrorCode::InvalidSchedulerConfiguration,
         static_cast<std::uint32_t>(inputweaver::RuntimeActivationSubject::ContinuouslyReadyBackoffNanoseconds), 1U, 0U},
        "nonpositive scheduler backoff duration rejects activation");
    inputweaver::RuntimeCapacities outputRateCapacity{};
    outputRateCapacity.maximumOutputTransitionsPerInterval = 0U;
    checkExactRejection(
        outputRateCapacity,
        tap,
        {inputweaver::RuntimeActivationErrorCode::InvalidOutputRateConfiguration,
         static_cast<std::uint32_t>(inputweaver::RuntimeActivationSubject::MaximumOutputTransitionsPerInterval), 1U, 0U},
        "invalid output rate capacity rejects activation");
    inputweaver::RuntimeCapacities outputRateIntervalCapacity{};
    outputRateIntervalCapacity.outputRateIntervalNanoseconds = 0;
    checkExactRejection(
        outputRateIntervalCapacity,
        tap,
        {inputweaver::RuntimeActivationErrorCode::InvalidOutputRateConfiguration,
         static_cast<std::uint32_t>(inputweaver::RuntimeActivationSubject::OutputRateIntervalNanoseconds), 1U, 0U},
        "nonpositive output-rate interval rejects activation");
    inputweaver::RuntimeCapacities launchPolicy{};
    launchPolicy.permitProcessLaunch = false;
    checkCapacityRejection(
        launchPolicy,
        actions,
        inputweaver::RuntimeActivationErrorCode::ProcessLaunchDenied,
        "process policy rejects exec program before publication");

    inputweaver::RuntimeCapacities exactExit{};
    exactExit.maximumExitRulesPerEvent =
        tap->Requirements().maximumExitRulesPerEvent;
    RuntimeHarness exactExitHarness(exactExit);
    Check(
        exactExitHarness.runtime.Activate(tap).activated,
        "exit-rule requirement is accepted at the exact capacity");
    inputweaver::RuntimeCapacities exactPause{};
    exactPause.maximumPauseRulesPerEvent =
        pause->Requirements().maximumPauseRulesPerEvent;
    RuntimeHarness exactPauseHarness(exactPause);
    Check(
        exactPauseHarness.runtime.Activate(pause).activated,
        "pause-rule requirement is accepted at the exact capacity");
    inputweaver::RuntimeCapacities exactRule{};
    exactRule.maximumRulesPerEvent = tap->Requirements().maximumRulesPerEvent;
    RuntimeHarness exactRuleHarness(exactRule);
    Check(
        exactRuleHarness.runtime.Activate(tap).activated,
        "ordinary-rule requirement is accepted at the exact capacity");
    inputweaver::RuntimeCapacities exactPredicate{};
    exactPredicate.maximumPredicateStepsPerEvent =
        repeat->Requirements().maximumPredicateStepsPerEvent;
    RuntimeHarness exactPredicateHarness(exactPredicate);
    Check(
        exactPredicateHarness.runtime.Activate(repeat).activated,
        "predicate-step requirement is accepted at the exact capacity");
    inputweaver::RuntimeCapacities exactMappingOperation{};
    exactMappingOperation.maximumMappingOperationsPerEvent =
        mapping->Requirements().maximumMappingOperationsPerEvent;
    RuntimeHarness exactMappingOperationHarness(exactMappingOperation);
    Check(
        exactMappingOperationHarness.runtime.Activate(mapping).activated,
        "mapping-operation requirement is accepted at the exact capacity");

    RuntimeHarness invalidTarget;
    invalidTarget.route.targetValid = false;
    Check(
        invalidTarget.runtime.Activate(tap).error.code
            == inputweaver::RuntimeActivationErrorCode::InvalidTarget,
        "target validation rejects activation before publication");
    RuntimeHarness unsupportedControl;
    unsupportedControl.controls.rejectAll = true;
    Check(
        unsupportedControl.runtime.Activate(tap).error.code
            == inputweaver::RuntimeActivationErrorCode::UnsupportedControl,
        "unsupported control rejects activation before publication");
    RuntimeHarness missingCapability;
    missingCapability.controls.rejectAgain = true;
    Check(
        missingCapability.runtime.Activate(mapping).error.code
            == inputweaver::RuntimeActivationErrorCode::MissingControlCapability,
        "missing control capability rejects activation before publication");

    inputweaver::RuntimeCapacities oneTask{};
    oneTask.taskSlotCount = 1U;
    RuntimeHarness harness(oneTask);
    Check(harness.runtime.Activate(tap).activated, "single task capacity accepts tap program");
    const std::uint32_t tapTrigger = TriggerControl(*tap);
    (void)harness.runtime.HandleInput(KeyboardEvent(
        tapTrigger,
        inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    (void)harness.runtime.HandleInput(KeyboardEvent(
        tapTrigger,
        inputweaver::Transition::Up));
    const auto second = harness.runtime.HandleInput(
        KeyboardEvent(tapTrigger, inputweaver::Transition::Down));
    Check(
        second == inputweaver::InputDecision::Forward
            && harness.runtime.Metrics().transactionRejections == 1U,
        "transient task exhaustion fails open without partial publication");

    RuntimeHarness retained;
    Check(retained.runtime.Activate(tap).activated, "baseline program activates");
    retained.controls.rejectAgain = true;
    const auto failedReload = retained.runtime.Activate(mapping);
    Check(
        !failedReload.activated && retained.runtime.HasActiveProgram(),
        "failed replacement activation retains the prior program");
    Check(
        retained.runtime.HandleInput(KeyboardEvent(
            tapTrigger,
            inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "retained program remains executable after failed replacement");
}

void TestArtifactLoading()
{
    RuntimeHarness harness;
    const auto tap = Finalize(inputweaver::test::MakeTapFixtureStorage());
    const std::vector<std::uint8_t> encoded = inputweaver::EncodeWeavec(*tap);
    const std::filesystem::path path = std::filesystem::path("bin")
        / "runtime-artifact-test.weavec";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()));
    }
    const auto loaded = inputweaver::LoadAndActivateWeavec(harness.runtime, path);
    Check(loaded.activated, "valid artifact loads and activates without compiler code");
    inputweaver::WeavecDecodeLimits tinyLimits{};
    tinyLimits.maximumPayloadBytes = 1U;
    const auto limited = inputweaver::LoadAndActivateWeavec(
        harness.runtime,
        path,
        tinyLimits);
    Check(
        !limited.activated
            && limited.error
                == inputweaver::RuntimeArtifactLoadErrorCode::LimitExceeded,
        "artifact size limit rejects input before allocation and decoding");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write("BAD", 3);
    }
    const auto rejected = inputweaver::LoadAndActivateWeavec(harness.runtime, path);
    Check(
        !rejected.activated
            && rejected.error
                == inputweaver::RuntimeArtifactLoadErrorCode::DecodeFailed,
        "malformed artifact is rejected before activation");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(
            TriggerControl(*tap),
            inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "artifact load failure preserves the active program");
    std::error_code removeError;
    (void)std::filesystem::remove(path, removeError);
    const auto missing = inputweaver::LoadAndActivateWeavec(harness.runtime, path);
    Check(
        !missing.activated
            && missing.error == inputweaver::RuntimeArtifactLoadErrorCode::OpenFailed,
        "missing artifact reports a bounded open failure");
}

void TestBoundedRuntimeStress()
{
    inputweaver::RuntimeCapacities capacities{};
    capacities.maximumOutputTransitionsPerInterval = 8192U;
    RuntimeHarness harness(capacities);
    const auto program = Finalize(inputweaver::test::MakeMappingFixtureStorage());
    Check(harness.runtime.Activate(program).activated, "mapping stress fixture activates");
    const std::uint32_t trigger = TriggerControl(*program);
    constexpr std::size_t cycles = 2048U;
    bool decisionsValid = true;
    for (std::size_t cycle = 0U; cycle < cycles; ++cycle) {
        decisionsValid = decisionsValid
            && harness.runtime.HandleInput(KeyboardEvent(
                trigger,
                inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress;
        (void)harness.runtime.Pump();
        decisionsValid = decisionsValid
            && harness.runtime.HandleInput(KeyboardEvent(
                trigger,
                inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress;
        (void)harness.runtime.Pump();
        decisionsValid = decisionsValid
            && harness.runtime.HandleInput(KeyboardEvent(
                trigger,
                inputweaver::Transition::Up))
                == inputweaver::InputDecision::Suppress;
        (void)harness.runtime.Pump();
    }
    Check(
        decisionsValid
            && harness.output.requests.size() == cycles * 3U
            && !harness.runtime.HasActiveMappings()
            && !harness.runtime.HasOwnedOutputs()
            && harness.runtime.Metrics().transactionRejections == 0U,
        "bounded mapping transactions remain deterministic across stress cycles");
}

void TestReversibleTargetEligibility()
{
    inputweaver::CompiledProgramStorage mappingStorage =
        inputweaver::test::MakeMappingFixtureStorage();
    SelectExecutableTarget(mappingStorage);
    const auto mapping = Finalize(std::move(mappingStorage));
    RuntimeHarness mappingHarness;
    Check(
        mappingHarness.runtime.Activate(mapping).activated,
        "target-eligibility mapping fixture activates");
    const std::uint32_t source = TriggerControl(*mapping);
    Check(
        mappingHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "eligible target commits a mapping press");
    (void)mappingHarness.runtime.Pump();
    Check(
        mappingHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "eligible target commits a mapping again");
    (void)mappingHarness.runtime.Pump();
    const std::uint64_t generation = mappingHarness.runtime.Generation();
    mappingHarness.runtime.SetTargetEligible(false);
    (void)mappingHarness.runtime.Pump();
    Check(
        !mappingHarness.runtime.TargetEligible()
            && mappingHarness.runtime.Generation() == generation + 1U
            && mappingHarness.output.requests.size() == 3U
            && mappingHarness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && mappingHarness.output.requests[1].transition
            == inputweaver::RuntimeOutputTransition::Again
            && mappingHarness.output.requests[0].generation == generation
            && mappingHarness.output.requests[1].generation == generation
            && mappingHarness.output.requests[2].transition
                == inputweaver::RuntimeOutputTransition::Up
            && mappingHarness.output.requests[2].generation == generation + 1U
            && !mappingHarness.runtime.HasActiveMappings()
            && !mappingHarness.runtime.HasOwnedOutputs(),
        "foreground loss cancels mapping ownership with one release");

    mappingHarness.runtime.SetTargetEligible(true);
    Check(
        mappingHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward,
        "held source again after foreground return does not remap");
    (void)mappingHarness.runtime.Pump();
    Check(
        mappingHarness.output.requests.size() == 3U,
        "foreground return synthesizes no mapping down");
    Check(
        mappingHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Up))
                == inputweaver::InputDecision::Forward,
        "held source release establishes a fresh lifecycle");
    Check(
        mappingHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "fresh source press remaps after foreground return");
    (void)mappingHarness.runtime.Pump();
    Check(
        mappingHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Up))
                == inputweaver::InputDecision::Suppress,
        "fresh remap release remains consumed");
    (void)mappingHarness.runtime.Pump();
    Check(
        mappingHarness.output.requests.size() == 5U
            && !mappingHarness.runtime.HasOwnedOutputs(),
        "fresh target-eligible mapping completes normally");

    inputweaver::CompiledProgramStorage tapStorage =
        inputweaver::test::MakeTapFixtureStorage();
    SelectExecutableTarget(tapStorage);
    const auto tap = Finalize(std::move(tapStorage));
    RuntimeHarness taskHarness;
    Check(
        taskHarness.runtime.Activate(tap).activated,
        "target-eligibility task fixture activates");
    (void)taskHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*tap),
        inputweaver::Transition::Down));
    (void)taskHarness.runtime.Pump();
    taskHarness.runtime.SetTargetEligible(false);
    (void)taskHarness.runtime.Pump();
    taskHarness.clock.Advance(100'000'000);
    taskHarness.runtime.SetTargetEligible(true);
    (void)taskHarness.runtime.Pump();
    Check(
        taskHarness.output.requests.size() == 2U
            && taskHarness.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up
            && taskHarness.runtime.ActiveTaskCount() == 0U
            && taskHarness.runtime.Metrics().cancelledTasks == 1U,
        "foreground loss cancels timed tasks and blocks stale output");

    RuntimeHarness transactionHarness;
    Check(
        transactionHarness.runtime.Activate(mapping).activated,
        "target-transition transaction fixture activates");
    std::atomic<bool> dispatchEntered{false};
    std::atomic<bool> dispatchReleased{false};
    transactionHarness.route.dispatchEntered = &dispatchEntered;
    transactionHarness.route.dispatchReleased = &dispatchReleased;
    std::atomic<inputweaver::InputDecision> racedDecision{
        inputweaver::InputDecision::Suppress};
    std::thread dispatchThread([&]() {
        racedDecision.store(
            transactionHarness.runtime.HandleInput(KeyboardEvent(
                source,
                inputweaver::Transition::Down)),
            std::memory_order_release);
    });
    while (!dispatchEntered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    transactionHarness.runtime.SetTargetEligible(false);
    transactionHarness.runtime.SetTargetEligible(true);
    dispatchReleased.store(true, std::memory_order_release);
    dispatchThread.join();
    transactionHarness.route.dispatchEntered = nullptr;
    transactionHarness.route.dispatchReleased = nullptr;
    (void)transactionHarness.runtime.Pump();
    Check(
        racedDecision.load(std::memory_order_acquire)
                == inputweaver::InputDecision::Forward
            && transactionHarness.output.requests.empty()
            && !transactionHarness.runtime.HasActiveMappings()
            && !transactionHarness.runtime.HasOwnedOutputs(),
        "foreground generation change aborts an in-flight event transaction");
    Check(
        transactionHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Up))
                == inputweaver::InputDecision::Forward
            && transactionHarness.runtime.HandleInput(KeyboardEvent(
                source,
                inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "aborted target transaction requires a fresh source lifecycle");
    (void)transactionHarness.runtime.Pump();
    (void)transactionHarness.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Up));
    (void)transactionHarness.runtime.Pump();

    RuntimeHarness routeFailureHarness;
    Check(
        routeFailureHarness.runtime.Activate(mapping).activated,
        "injection-route transition fixture activates");
    const std::uint64_t routeGeneration =
        routeFailureHarness.runtime.Generation();
    Check(
        routeFailureHarness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "route transition queues an eligible mapping transaction");
    routeFailureHarness.route.injectionAllowed = false;
    (void)routeFailureHarness.runtime.Pump();
    Check(
        !routeFailureHarness.runtime.TargetEligible()
            && routeFailureHarness.runtime.Generation() == routeGeneration + 1U
            && routeFailureHarness.output.requests.empty()
            && !routeFailureHarness.runtime.HasActiveMappings()
            && !routeFailureHarness.runtime.HasOwnedOutputs(),
        "injection-time route rejection enters reversible target ineligibility");
    routeFailureHarness.route.injectionAllowed = true;
    routeFailureHarness.runtime.SetTargetEligible(true);
    (void)routeFailureHarness.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Up));
    (void)routeFailureHarness.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Down));
    (void)routeFailureHarness.runtime.Pump();
    routeFailureHarness.runtime.NotifyTargetLost();
    (void)routeFailureHarness.runtime.Pump();
    Check(
        routeFailureHarness.output.requests.size() == 2U
            && routeFailureHarness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && routeFailureHarness.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up
            && !routeFailureHarness.runtime.HasOwnedOutputs()
            && routeFailureHarness.runtime.HandleInput(KeyboardEvent(
                source,
                inputweaver::Transition::Up))
                == inputweaver::InputDecision::Forward,
        "target loss releases ownership before suspending dispatch");

    inputweaver::CompiledProgramStorage stateStorage =
        MakeActionFixtureStorage();
    stateStorage.actionPrograms[0].code.count = 2U;
    stateStorage.actionPrograms[0].repeatFrameCount = 0U;
    stateStorage.actionPrograms[0].maximumOwnedControlCount = 0U;
    stateStorage.actionCode = {
        {inputweaver::ActionOpcode::Toggle, 0U, 0U},
        {inputweaver::ActionOpcode::End, 0U, 0U}};
    stateStorage.debugInfo.actionInstructionSpans.resize(2U);
    SelectExecutableTarget(stateStorage);
    const auto stateProgram = Finalize(std::move(stateStorage));
    RuntimeHarness stateHarness;
    Check(
        stateHarness.runtime.Activate(stateProgram).activated,
        "target-rebind state fixture activates");
    const std::uint32_t stateSource = TriggerControl(*stateProgram);
    (void)stateHarness.runtime.HandleInput(KeyboardEvent(
        stateSource,
        inputweaver::Transition::Down));
    (void)stateHarness.runtime.Pump();
    bool retainedState = false;
    Check(
        stateHarness.runtime.ReadUserState(0U, retainedState)
            && retainedState,
        "target-rebind fixture changes user state before target loss");
    stateHarness.runtime.NotifyTargetLost();
    (void)stateHarness.runtime.Pump();
    retainedState = false;
    Check(
        !stateHarness.runtime.TargetEligible()
            && stateHarness.runtime.ReadUserState(0U, retainedState)
            && retainedState,
        "target loss preserves user state while dispatch is suspended");
    stateHarness.runtime.SetTargetEligible(true);
    (void)stateHarness.runtime.HandleInput(KeyboardEvent(
        stateSource,
        inputweaver::Transition::Up));
    (void)stateHarness.runtime.HandleInput(KeyboardEvent(
        stateSource,
        inputweaver::Transition::Down));
    (void)stateHarness.runtime.Pump();
    retainedState = true;
    Check(
        stateHarness.runtime.ReadUserState(0U, retainedState)
            && !retainedState,
        "replacement target resumes the same user state");
}

void TestMappingCancellationReleasePaths()
{
    const auto mapping = Finalize(
        inputweaver::test::MakeMappingFixtureStorage());
    const auto acquireMapping = [&mapping](RuntimeHarness& harness) {
        const std::uint32_t source = TriggerControl(*mapping);
        const bool consumed = harness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress;
        (void)harness.runtime.Pump();
        return consumed
            && harness.output.requests.size() == 1U
            && harness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && harness.runtime.HasOwnedOutputs();
    };
    const auto released = [](const RuntimeHarness& harness) {
        return harness.output.requests.size() >= 2U
            && harness.output.requests.back().transition
                == inputweaver::RuntimeOutputTransition::Up
            && !harness.runtime.HasActiveMappings()
            && !harness.runtime.HasOwnedOutputs();
    };

    RuntimeHarness pauseHarness;
    const auto pauseProgram = Finalize(MakeMappingPauseStorage());
    Check(
        pauseHarness.runtime.Activate(pauseProgram).activated,
        "mapping PAUSE cancellation fixture activates");
    const std::uint32_t pauseSource = TriggerControl(*pauseProgram);
    const std::uint32_t pauseControl =
        pauseProgram->PauseControlBuckets().front().key.control.value;
    (void)pauseHarness.runtime.HandleInput(KeyboardEvent(
        pauseSource,
        inputweaver::Transition::Down));
    (void)pauseHarness.runtime.Pump();
    Check(
        pauseHarness.runtime.HandleInput(KeyboardEvent(
            pauseControl,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "PAUSE transition consumes its physical control");
    (void)pauseHarness.runtime.Pump();
    Check(
        released(pauseHarness),
        "PAUSE cancellation releases active mapping ownership");

    RuntimeHarness exitHarness;
    Check(
        exitHarness.runtime.Activate(mapping).activated
            && acquireMapping(exitHarness),
        "mapping exit cancellation fixture acquires ownership");
    Check(
        RequestCompiledExit(exitHarness.runtime, *mapping)
            == inputweaver::InputDecision::Suppress,
        "compiled exit is accepted during mapping ownership");
    (void)exitHarness.runtime.Pump();
    Check(
        released(exitHarness),
        "compiled exit releases active mapping ownership");

    RuntimeHarness failureHarness;
    Check(
        failureHarness.runtime.Activate(mapping).activated
            && acquireMapping(failureHarness),
        "mapping runtime-failure cancellation fixture acquires ownership");
    failureHarness.output.nextResult = inputweaver::RuntimeOutputResult::Failed;
    (void)failureHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*mapping),
        inputweaver::Transition::Down));
    (void)failureHarness.runtime.Pump();
    Check(
        failureHarness.runtime.FatalShutdownRequested()
            && failureHarness.output.requests.size() == 3U
            && released(failureHarness),
        "runtime output failure releases active mapping ownership");

    RuntimeHarness shutdownHarness;
    Check(
        shutdownHarness.runtime.Activate(mapping).activated
            && acquireMapping(shutdownHarness),
        "mapping shutdown cancellation fixture acquires ownership");
    shutdownHarness.runtime.RequestShutdown();
    (void)shutdownHarness.runtime.Pump();
    Check(
        released(shutdownHarness),
        "normal shutdown releases active mapping ownership");
}

void TestPhysicalStateInitialization()
{
    const auto mapping = Finalize(
        inputweaver::test::MakeMappingFixtureStorage());
    const std::uint32_t source = TriggerControl(*mapping);
    RuntimeHarness seeded;
    Check(
        seeded.runtime.Activate(mapping).activated
            && seeded.runtime.SeedPhysicalState(
                inputweaver::ControlRefId{source},
                true),
        "startup-held mapping source is seeded");
    Check(
        seeded.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward,
        "startup-held source again cannot create a mapping");
    (void)seeded.runtime.Pump();
    Check(seeded.output.requests.empty(), "physical-state seeding publishes no output");
    (void)seeded.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Up));
    Check(
        seeded.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "fresh press after seeded release dispatches");
    (void)seeded.runtime.Pump();

    RuntimeHarness unsynchronized;
    Check(
        unsynchronized.runtime.Activate(mapping).activated
            && unsynchronized.runtime.MarkPhysicalStateUnsynchronized(
                inputweaver::ControlRefId{source}),
        "unqueryable event source starts unsynchronized");
    Check(
        unsynchronized.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward
            && unsynchronized.runtime.HandleInput(KeyboardEvent(
                source,
                inputweaver::Transition::Up))
                == inputweaver::InputDecision::Forward,
        "unsynchronized source forwards through its first release");
    Check(
        unsynchronized.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "release synchronizes the next fresh source press");

    const auto physicalExpression = Finalize(MakeExpressionFixtureStorage());
    RuntimeHarness missingInitialState;
    missingInitialState.controls.rejectPhysicalState = true;
    Check(
        missingInitialState.runtime.Activate(physicalExpression).error.code
            == inputweaver::RuntimeActivationErrorCode::MissingControlCapability,
        "physical-state requirement rejects an unqueryable backend control");
}

void TestOutputRateBudget()
{
    inputweaver::RuntimeCapacities capacities{};
    capacities.maximumOutputTransitionsPerInterval = 1U;
    capacities.outputRateIntervalNanoseconds = 1'000'000'000;
    RuntimeHarness harness(capacities);
    const auto mapping = Finalize(
        inputweaver::test::MakeMappingFixtureStorage());
    const std::uint32_t source = TriggerControl(*mapping);
    Check(harness.runtime.Activate(mapping).activated, "output-rate fixture activates");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    (void)harness.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    bool foundRateLimit = false;
    inputweaver::RuntimeDiagnosticRecord diagnostic{};
    while (harness.runtime.TryPopDiagnostic(diagnostic)) {
        foundRateLimit = foundRateLimit
            || diagnostic.kind
                == inputweaver::RuntimeDiagnosticKind::OutputRateExceeded;
    }
    Check(
        foundRateLimit
            && harness.output.requests.size() == 2U
            && harness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && harness.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up
            && !harness.runtime.HasOwnedOutputs(),
        "mapping rate exhaustion cancels ownership but exempts its release");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        source,
        inputweaver::Transition::Up));
    harness.clock.Advance(1'000'000'000);
    Check(
        harness.runtime.HandleInput(KeyboardEvent(
            source,
            inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress,
        "mapping can resume after the output rate window resets");
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 3U
            && harness.output.requests[2].transition
                == inputweaver::RuntimeOutputTransition::Down,
        "reset output window admits a fresh down transition");

    inputweaver::RuntimeCapacities taskCapacities{};
    taskCapacities.maximumOutputTransitionsPerInterval = 1U;
    taskCapacities.outputRateIntervalNanoseconds = 1'000'000'000;
    RuntimeHarness taskHarness(taskCapacities);
    const auto taskProgram = Finalize(MakeOutputBudgetStorage());
    Check(
        taskHarness.runtime.Activate(taskProgram).activated,
        "task output-rate fixture activates");
    const std::uint64_t taskGeneration = taskHarness.runtime.Generation();
    (void)taskHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*taskProgram),
        inputweaver::Transition::Down));
    (void)taskHarness.runtime.Pump();
    bool foundTaskRateLimit = false;
    while (taskHarness.runtime.TryPopDiagnostic(diagnostic)) {
        foundTaskRateLimit = foundTaskRateLimit
            || diagnostic.kind
                == inputweaver::RuntimeDiagnosticKind::OutputRateExceeded;
    }
    Check(
        foundTaskRateLimit
            && taskHarness.runtime.Generation() == taskGeneration
            && taskHarness.runtime.Metrics().cancelledTasks == 1U
            && taskHarness.output.requests.size() == 2U
            && !taskHarness.runtime.HasOwnedOutputs(),
        "global output rate exhaustion cancels only the producing task");
}

void TestRoutingFailureAndExit()
{
    RuntimeHarness harness;
    inputweaver::CompiledProgramStorage tapStorage =
        inputweaver::test::MakeTapFixtureStorage();
    SelectExecutableTarget(tapStorage);
    const auto tap = Finalize(std::move(tapStorage));
    const std::uint32_t trigger = TriggerControl(*tap);
    Check(harness.runtime.Activate(tap).activated, "routing test program activates");
    harness.route.dispatchAllowed = false;
    const std::uint64_t generation = harness.runtime.Generation();
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward
            && harness.runtime.Generation() == generation + 1U
            && !harness.runtime.TargetEligible()
            && harness.runtime.HasActiveProgram(),
        "keyboard dispatch rejection enters reversible target ineligibility");
    harness.route.dispatchAllowed = true;
    harness.runtime.SetTargetEligible(true);
    Check(
        RequestCompiledExit(harness.runtime, *tap)
            == inputweaver::InputDecision::Suppress,
        "compiled exit precedes ordinary routing and suppresses its event");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Forward,
        "compiled exit disables further transactions");
}
