void TestArrayStorage()
{
    using namespace inputweaver;
    Check(NormalizeArrayIndex(3.9, 4U).value == 3U,
        "fractional array indexes use floor normalization");
    Check(NormalizeArrayIndex(-0.1, 4U).status == ArrayIndexStatus::Negative,
        "negative array indexes are rejected before conversion");
    Check(NormalizeArrayIndex(
            (std::numeric_limits<double>::infinity)(),
            4U).status == ArrayIndexStatus::NonFinite,
        "non-finite array indexes are rejected");
    Check(NormalizeArrayIndex(
            (std::numeric_limits<double>::max)(),
            4U).status == ArrayIndexStatus::TooLarge,
        "array indexes outside the host range are rejected");
    Check(NormalizeArrayIndex(4.0, 4U).status == ArrayIndexStatus::OutOfBounds,
        "array indexes are checked against logical length");

    std::vector<std::uint8_t> initial(256U, 0U);
    initial.back() = 1U;
    RuntimeArrayStorage array(
        ArrayElementType::State,
        initial,
        std::span<const double>{});
    const std::size_t initialBytes = array.AllocatedBytes();
    const auto growthPlan = array.PlanAppend();
    auto growth = growthPlan
        ? array.PrepareAppend(*growthPlan)
        : std::nullopt;
    RuntimeValue on{};
    on.type = ExpressionType::State;
    on.stateValue = 1U;
    Check(growthPlan.has_value()
            && growth.has_value()
            && growthPlan->projectedBytes > initialBytes
            && array.Append(on, *growthPlan, std::move(*growth))
            && array.Size() == 257U,
        "page-boundary append prepares page and directory growth before commit");
    RuntimeValue read{};
    Check(array.Read(256U, read)
            && read.type == ExpressionType::State
            && read.stateValue == 1U,
        "a newly committed page is immediately addressable");
    RuntimeValue popped{};
    Check(array.Pop(popped)
            && popped.type == ExpressionType::State
            && popped.stateValue == 1U,
        "pop returns the final typed array element");
    const std::size_t retainedBytes = array.AllocatedBytes();
    array.Clear();
    const auto reusePlan = array.PlanAppend();
    auto reused = reusePlan
        ? array.PrepareAppend(*reusePlan)
        : std::nullopt;
    Check(array.Size() == 0U
            && retainedBytes > initialBytes
            && array.AllocatedBytes() == retainedBytes
            && reusePlan.has_value()
            && reused.has_value()
            && reusePlan->projectedBytes == retainedBytes
            && array.Append(on, *reusePlan, std::move(*reused))
            && array.Size() == 1U,
        "clear retains and reuses allocated pages without another growth charge");
    RuntimeValue wrongType{};
    wrongType.type = ExpressionType::Number;
    wrongType.numberValue = 1.0;
    const auto unusedPlan = array.PlanAppend();
    auto unused = unusedPlan
        ? array.PrepareAppend(*unusedPlan)
        : std::nullopt;
    Check(unusedPlan.has_value()
            && unused.has_value()
            && !array.Append(wrongType, *unusedPlan, std::move(*unused))
            && array.Size() == 1U,
        "typed array storage rejects mismatched append values without mutation");
}

void SelectExecutableTarget(inputweaver::CompiledProgramStorage& storage)
{
    storage.strings.push_back("target.exe");
    storage.settings.target.kind = inputweaver::TargetSelectorKind::Executable;
    storage.settings.target.text = inputweaver::StringId{
        static_cast<std::uint32_t>(storage.strings.size() - 1U)};
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
}

void TestEffectiveTargetOverride()
{
    using namespace inputweaver;
    const auto globalProgram = Finalize(test::MakeTapFixtureStorage());
    RuntimeHarness executableOverride;
    Check(
        executableOverride.runtime.Activate(
            globalProgram,
            TargetSelectorKind::Executable).activated
            && executableOverride.route.validatedKind
                == TargetSelectorKind::Executable,
        "executable command-line target replaces compiled GLOBAL routing");

    CompiledProgramStorage executableStorage = test::MakeTapFixtureStorage();
    SelectExecutableTarget(executableStorage);
    const auto executableProgram = Finalize(std::move(executableStorage));
    RuntimeHarness globalOverride;
    Check(
        globalOverride.runtime.Activate(
            executableProgram,
            TargetSelectorKind::Global).activated
            && globalOverride.route.validatedKind == TargetSelectorKind::Global,
        "global command-line target replaces compiled executable routing");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeMappingPauseStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeMappingFixtureStorage();
    const ControlRefId pauseControl{
        static_cast<std::uint32_t>(storage.controls.size())};
    storage.controls.push_back({
        kControlNamespaceUsbHid,
        0x07U,
        0x41U,
        kControlQualifierNone});
    storage.controlRequirements.push_back({
        pauseControl,
        ToControlUseBits(ControlUse::EventSource)});
    storage.pauseControlRules.push_back({
        ExpressionId{},
        Delivery::Consume,
        PauseEffect::Toggle,
        1U,
        storage.mappings[0].source});
    storage.pauseControlBuckets.push_back({
        {pauseControl, EventTransition::Down},
        {0U, 1U}});
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

[[nodiscard]] inputweaver::RuntimeInputEvent KeyboardEvent(
    std::uint32_t control,
    inputweaver::Transition transition,
    inputweaver::InputOrigin origin = inputweaver::InputOrigin::PhysicalCandidate)
{
    inputweaver::RuntimeInputEvent event{};
    event.control = inputweaver::ControlRefId{control};
    event.device = inputweaver::DeviceKind::Keyboard;
    event.origin = origin;
    event.transition = transition;
    return event;
}

[[nodiscard]] inputweaver::ControlRefId FindKeyboardControl(
    const inputweaver::CompiledProgram& program,
    std::uint32_t usage)
{
    const auto controls = program.Controls();
    const auto found = std::find(
        controls.begin(),
        controls.end(),
        inputweaver::ControlRef{
            inputweaver::kControlNamespaceUsbHid,
            0x07U,
            usage,
            inputweaver::kControlQualifierNone});
    return found == controls.end()
        ? inputweaver::ControlRefId{}
        : inputweaver::ControlRefId{
              static_cast<std::uint32_t>(found - controls.begin())};
}

[[nodiscard]] inputweaver::InputDecision RequestCompiledExit(
    inputweaver::ProgramRuntime& runtime,
    const inputweaver::CompiledProgram& program)
{
    const inputweaver::ControlRefId control = FindKeyboardControl(program, 0xe0U);
    const inputweaver::ControlRefId shift = FindKeyboardControl(program, 0xe1U);
    const inputweaver::ControlRefId f12 = FindKeyboardControl(program, 0x45U);
    if (!runtime.SeedPhysicalState(control, true)
        || !runtime.SeedPhysicalState(shift, true)
        || !f12.IsValid()) {
        return inputweaver::InputDecision::Forward;
    }
    return runtime.HandleInput(KeyboardEvent(
        f12.value,
        inputweaver::Transition::Down));
}

[[nodiscard]] std::uint32_t TriggerControl(
    const inputweaver::CompiledProgram& program)
{
    return program.EventBuckets().empty()
        ? program.PauseControlBuckets().front().key.control.value
        : program.EventBuckets().front().key.control.value;
}

void TestTapFixture()
{
    RuntimeHarness harness;
    const auto program = Finalize(inputweaver::test::MakeTapFixtureStorage());
    const std::uint32_t trigger = TriggerControl(*program);
    Check(harness.runtime.Activate(program).activated, "tap fixture activates");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "tap rule suppresses its physical down event");
    Check(harness.output.requests.empty(), "hook dispatch performs no output");
    const inputweaver::RuntimePumpResult first = harness.runtime.Pump();
    Check(first.slices == 1U, "tap task runs one initial slice");
    Check(
        harness.output.requests.size() == 1U
            && harness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down,
        "tap starts with one output down");
    Check(harness.runtime.HasOwnedOutputs(), "tap hold owns its output");
    harness.clock.Advance(29'999'999);
    (void)harness.runtime.Pump();
    Check(harness.output.requests.size() == 1U, "tap remains held before deadline");
    harness.clock.Advance(1);
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 2U
            && harness.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up,
        "tap emits its paired release at the deadline");
    Check(
        harness.runtime.ActiveTaskCount() == 0U
            && !harness.runtime.HasOwnedOutputs(),
        "tap task ends without retained state");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(
            trigger,
            inputweaver::Transition::Up,
            inputweaver::InputOrigin::CurrentInstanceInjected))
            == inputweaver::InputDecision::Forward,
        "current-instance injected input bypasses runtime state and rules");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(
            trigger,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::ExternalInjected))
            == inputweaver::InputDecision::Forward,
        "external injected input bypasses runtime state and rules");
}

void TestMappingFixture()
{
    RuntimeHarness harness;
    const auto program = Finalize(inputweaver::test::MakeMappingFixtureStorage());
    const std::uint32_t trigger = TriggerControl(*program);
    Check(harness.runtime.Activate(program).activated, "mapping fixture activates");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "mapping down is consumed");
    Check(harness.runtime.HasActiveMappings(), "mapping latches after commit");
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 1U
            && harness.output.requests.back().transition
                == inputweaver::RuntimeOutputTransition::Down,
        "mapping acquires its target");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "active mapping again is consumed before ordinary rules");
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 2U
            && harness.output.requests.back().transition
            == inputweaver::RuntimeOutputTransition::Again,
        "mapping forwards again lifecycle output");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Up))
            == inputweaver::InputDecision::Suppress,
        "active mapping release is consumed");
    Check(!harness.runtime.HasActiveMappings(), "mapping slot clears after release commit");
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 3U
            && harness.output.requests.back().transition
                == inputweaver::RuntimeOutputTransition::Up,
        "mapping releases its target");
    Check(!harness.runtime.HasOwnedOutputs(), "mapping release clears ownership");
}

void TestConditionalRepeatFixture()
{
    RuntimeHarness harness;
    auto repeatStorage = inputweaver::test::MakeConditionalRepeatFixtureStorage();
    repeatStorage.numberConstants[0] = 2.5;
    const auto program = Finalize(std::move(repeatStorage));
    const std::uint32_t trigger = TriggerControl(*program);
    Check(harness.runtime.Activate(program).activated, "repeat fixture activates");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "true repeat predicate consumes the event");
    (void)harness.runtime.Pump();
    harness.clock.Advance(30'000'000);
    (void)harness.runtime.Pump();
    harness.clock.Advance(10'000'000);
    (void)harness.runtime.Pump();
    harness.clock.Advance(30'000'000);
    (void)harness.runtime.Pump();
    harness.clock.Advance(10'000'000);
    (void)harness.runtime.Pump();
    Check(harness.output.requests.size() == 4U, "fractional repeat limit floors to two taps");
    Check(
        harness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down
            && harness.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up
            && harness.output.requests[2].transition
                == inputweaver::RuntimeOutputTransition::Down
            && harness.output.requests[3].transition
                == inputweaver::RuntimeOutputTransition::Up,
        "repeat fixture preserves paired output order");
    Check(harness.runtime.ActiveTaskCount() == 0U, "repeat task completes");
}

void TestPauseFixtureAndCancellation()
{
    RuntimeHarness harness;
    const auto pauseProgram = Finalize(
        inputweaver::test::MakePauseControlFixtureStorage());
    Check(harness.runtime.Activate(pauseProgram).activated, "pause fixture activates");
    const std::uint64_t before = harness.runtime.Generation();
    Check(
        harness.runtime.HandleInput(KeyboardEvent(0U, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "pause control applies its delivery synchronously");
    Check(
        !harness.runtime.PauseOn() && harness.runtime.Generation() == before + 1U,
        "pause toggle advances cancellation generation");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(0U, inputweaver::Transition::Up))
            == inputweaver::InputDecision::Forward,
        "ordinary dispatch is bypassed while paused");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(0U, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress
            && harness.runtime.PauseOn(),
        "pause control can recover while pause is off");

    const auto tapProgram = Finalize(inputweaver::test::MakeTapFixtureStorage());
    const std::uint32_t tapTrigger = TriggerControl(*tapProgram);
    const std::uint64_t beforeReload = harness.runtime.Generation();
    Check(
        harness.runtime.Activate(tapProgram).activated
            && harness.runtime.Generation() > beforeReload,
        "tap reload succeeds with a fresh output generation");
    (void)harness.runtime.HandleInput(KeyboardEvent(
        tapTrigger,
        inputweaver::Transition::Down));
    (void)harness.runtime.Pump();
    harness.runtime.RequestShutdown();
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() >= 2U
            && harness.output.requests.back().transition
                == inputweaver::RuntimeOutputTransition::Up,
        "cancellation during tap publishes required release");
    Check(
        harness.runtime.ActiveTaskCount() == 0U
            && !harness.runtime.HasOwnedOutputs(),
        "shutdown leaves no task or ownership");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakePauseEffectsStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakePauseControlFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.pauseControlRules = {
        {ExpressionId{}, Delivery::Consume, PauseEffect::Off, 0U, source},
        {ExpressionId{}, Delivery::Observe, PauseEffect::On, 1U, source},
        {ExpressionId{}, Delivery::Observe, PauseEffect::On, 2U, source},
        {ExpressionId{}, Delivery::Consume, PauseEffect::Toggle, 3U, source},
    };
    storage.pauseControlBuckets = {
        {{ControlRefId{0U}, EventTransition::Down}, {0U, 2U}},
        {{ControlRefId{0U}, EventTransition::Again}, {2U, 1U}},
        {{ControlRefId{0U}, EventTransition::Up}, {3U, 1U}},
    };
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestPauseEffectsAndIdempotence()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakePauseEffectsStorage());
    Check(harness.runtime.Activate(program).activated, "pause effects fixture activates");
    const std::uint32_t trigger = TriggerControl(*program);
    const std::uint64_t initial = harness.runtime.Generation();
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress
            && !harness.runtime.PauseOn()
            && harness.runtime.Generation() == initial + 1U,
        "first matching pause Off rule consumes and invalidates synchronously");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward
            && harness.runtime.PauseOn()
            && harness.runtime.Generation() == initial + 2U,
        "pause On observes again and recovers while off");
    const std::uint64_t onGeneration = harness.runtime.Generation();
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward
            && harness.runtime.Generation() == onGeneration,
        "idempotent pause On retains the cancellation generation");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Up))
                == inputweaver::InputDecision::Suppress
            && !harness.runtime.PauseOn()
            && harness.runtime.Generation() == onGeneration + 1U,
        "pause Toggle consumes release and invalidates once");
}

void TestExitControlSemantics()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    storage.exitControlRules[0].sourceOrdinal = 1U;
    const auto program = Finalize(std::move(storage));
    RuntimeHarness routed;
    routed.route.dispatchAllowed = false;
    Check(routed.runtime.Activate(program).activated,
        "conditional exit fixture activates");
    const ControlRefId f12 = FindKeyboardControl(*program, 0x45U);
    Check(
        routed.runtime.HandleInput(KeyboardEvent(f12.value, Transition::Down))
                == InputDecision::Forward
            && !routed.runtime.ExitRequested(),
        "an unmatched exit condition forwards the physical event");
    (void)routed.runtime.HandleInput(KeyboardEvent(f12.value, Transition::Up));
    Check(
        routed.runtime.SeedPhysicalState(FindKeyboardControl(*program, 0xe0U), true)
            && routed.runtime.SeedPhysicalState(
                FindKeyboardControl(*program, 0xe1U), true)
            && routed.runtime.HandleInput(KeyboardEvent(f12.value, Transition::Down))
                == InputDecision::Suppress
            && routed.runtime.ExitRequested(),
        "a matched exit bypasses ordinary target routing and stops synchronously");

    const auto pauseProgram = Finalize(MakePauseEffectsStorage());
    RuntimeHarness paused;
    Check(paused.runtime.Activate(pauseProgram).activated,
        "paused exit fixture activates");
    const std::uint32_t pauseTrigger = TriggerControl(*pauseProgram);
    (void)paused.runtime.HandleInput(KeyboardEvent(pauseTrigger, Transition::Down));
    Check(
        !paused.runtime.PauseOn()
            && RequestCompiledExit(paused.runtime, *pauseProgram)
                == InputDecision::Suppress
            && paused.runtime.ExitRequested(),
        "compiled exit remains available while PAUSE is off");
}

void AppendExpression(
    inputweaver::CompiledProgramStorage& storage,
    inputweaver::ExpressionType resultType,
    std::uint32_t maximumStackDepth,
    std::initializer_list<inputweaver::ExpressionInstruction> code)
{
    const std::uint32_t begin = static_cast<std::uint32_t>(
        storage.expressionCode.size());
    storage.expressionCode.insert(storage.expressionCode.end(), code);
    storage.expressions.push_back({
        {begin, static_cast<std::uint32_t>(code.size())},
        resultType,
        maximumStackDepth,
        {0U, storage.source.byteLength}});
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeExpressionFixtureStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    storage.userValues.initialStates = {1U};
    storage.userValues.initialNumbers = {7.0};
    storage.userValues.initialDurations = {{8}};
    storage.valueRefs = {
        {ValueDomain::UserState, ValueType::State, 0U},
        {ValueDomain::UserNumber, ValueType::Number, 0U},
        {ValueDomain::UserDuration, ValueType::Duration, 0U},
        {ValueDomain::BuiltinState, ValueType::State,
            static_cast<std::uint32_t>(BuiltinState::Pause)},
        {ValueDomain::BuiltinDuration, ValueType::Duration,
            static_cast<std::uint32_t>(BuiltinDuration::TapDuration)},
        {ValueDomain::BuiltinDuration, ValueType::Duration,
            static_cast<std::uint32_t>(BuiltinDuration::ActionGap)},
    };
    storage.strings.insert(storage.strings.end(), {"stateValue", "numberValue", "durationValue"});
    const SourceSpan source{0U, storage.source.byteLength};
    storage.debugInfo.variables = {
        {StringId{1U}, ValueRefId{0U}, {0U, 1U}},
        {StringId{2U}, ValueRefId{1U}, {1U, 1U}},
        {StringId{3U}, ValueRefId{2U}, {2U, 1U}},
    };
    storage.numberConstants = {6.0, 3.0, 0.0, 1.0e308};
    storage.durationConstants = {
        {10},
        {4},
        {(std::numeric_limits<std::int64_t>::max)()}};
    storage.expressions.clear();
    storage.expressionCode.clear();
    storage.exitControlRules[0].condition = ExpressionId{};
    storage.controlRequirements = {
        {ControlRefId{0U}, static_cast<std::uint8_t>(
            ToControlUseBits(ControlUse::OutputDownUp)
            | ToControlUseBits(ControlUse::PhysicalState))},
        {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
        {storage.exitControlBuckets[0].key.control,
            ToControlUseBits(ControlUse::EventSource)},
    };

    AppendExpression(storage, ExpressionType::Boolean, 1U, {
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::JumpIfFalse, ExpressionType::None, 4U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::Jump, ExpressionType::None, 5U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 1U, {
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::JumpIfTrue, ExpressionType::None, 4U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::Jump, ExpressionType::None, 5U, 0U},
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::ReadControlState, ExpressionType::ControlState, 0U, 0U},
        {ExpressionOpcode::PushControlState, ExpressionType::ControlState,
            static_cast<std::uint32_t>(ControlState::Held), 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::State, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Number, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::Unary, ExpressionType::Number,
            static_cast<std::uint32_t>(UnaryOperator::NumberIdentity), 0U},
        {ExpressionOpcode::Unary, ExpressionType::Number,
            static_cast<std::uint32_t>(UnaryOperator::NumberNegate), 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::Duration, 2U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::State, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::State, 3U, 0U},
        {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::Duration, 4U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 1U, {
        {ExpressionOpcode::LoadValue, ExpressionType::Duration, 5U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 1U, {
        {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::Unary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(UnaryOperator::BooleanNot), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });

    const auto addNumberBinary = [&storage](BinaryOperator operation,
                                            ExpressionType resultType) {
        AppendExpression(storage, resultType, 2U, {
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
            {ExpressionOpcode::Binary, resultType,
                static_cast<std::uint32_t>(operation), 0U},
            {ExpressionOpcode::Return, resultType, 0U, 0U},
        });
    };
    addNumberBinary(BinaryOperator::NumberAdd, ExpressionType::Number);
    addNumberBinary(BinaryOperator::NumberSubtract, ExpressionType::Number);
    addNumberBinary(BinaryOperator::NumberMultiply, ExpressionType::Number);
    addNumberBinary(BinaryOperator::NumberDivide, ExpressionType::Number);
    addNumberBinary(BinaryOperator::NumberModulo, ExpressionType::Number);

    const auto addDurationPair = [&storage](BinaryOperator operation) {
        AppendExpression(storage, ExpressionType::Duration, 2U, {
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 1U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Duration,
                static_cast<std::uint32_t>(operation), 0U},
            {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
        });
    };
    addDurationPair(BinaryOperator::DurationAdd);
    addDurationPair(BinaryOperator::DurationSubtract);
    AppendExpression(storage, ExpressionType::Duration, 2U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Duration,
            static_cast<std::uint32_t>(BinaryOperator::DurationMultiplyNumber), 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 2U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Duration,
            static_cast<std::uint32_t>(BinaryOperator::NumberMultiplyDuration), 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 2U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Duration,
            static_cast<std::uint32_t>(BinaryOperator::DurationDivideNumber), 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    addNumberBinary(BinaryOperator::Equal, ExpressionType::Boolean);
    addNumberBinary(BinaryOperator::NotEqual, ExpressionType::Boolean);
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::PushState, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::PushState, ExpressionType::State, 0U, 0U},
        {ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::NotEqual), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::NotEqual), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    addNumberBinary(BinaryOperator::NumberLess, ExpressionType::Boolean);
    addNumberBinary(BinaryOperator::NumberLessEqual, ExpressionType::Boolean);
    addNumberBinary(BinaryOperator::NumberGreater, ExpressionType::Boolean);
    addNumberBinary(BinaryOperator::NumberGreaterEqual, ExpressionType::Boolean);
    AppendExpression(storage, ExpressionType::Number, 2U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 2U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Number,
            static_cast<std::uint32_t>(BinaryOperator::NumberDivide), 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Number, 2U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 2U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Number,
            static_cast<std::uint32_t>(BinaryOperator::NumberModulo), 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 2U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 2U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Duration,
            static_cast<std::uint32_t>(BinaryOperator::DurationDivideNumber), 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Number, 2U, {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 3U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 3U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Number,
            static_cast<std::uint32_t>(BinaryOperator::NumberMultiply), 0U},
        {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Duration, 2U, {
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 2U, 0U},
        {ExpressionOpcode::PushDuration, ExpressionType::Duration, 1U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Duration,
            static_cast<std::uint32_t>(BinaryOperator::DurationAdd), 0U},
        {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
    });
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestExpressionVm()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakeExpressionFixtureStorage());
    Check(harness.runtime.Activate(program).activated, "expression fixture activates");
    constexpr std::array expectedFaults = {
        inputweaver::RuntimeEvaluationFault::DivisionByZero,
        inputweaver::RuntimeEvaluationFault::DivisionByZero,
        inputweaver::RuntimeEvaluationFault::DivisionByZero,
        inputweaver::RuntimeEvaluationFault::NonFiniteNumber,
        inputweaver::RuntimeEvaluationFault::InvalidDuration,
    };
    const std::size_t firstFault =
        program->Expressions().size() - expectedFaults.size();
    for (std::size_t index = 0U; index < firstFault; ++index) {
        const auto result = harness.runtime.EvaluateExpression(
            inputweaver::ExpressionId{static_cast<std::uint32_t>(index)});
        Check(result.Succeeded(), "valid expression opcode and operator evaluates");
    }
    for (std::size_t index = 0U; index < expectedFaults.size(); ++index) {
        const auto fault = harness.runtime.EvaluateExpression(
            inputweaver::ExpressionId{static_cast<std::uint32_t>(
                firstFault + index)});
        Check(
            fault.fault == expectedFaults[index],
            "numeric and duration boundary produces its evaluation fault");
    }
    const auto heldBefore = harness.runtime.EvaluateExpression(
        inputweaver::ExpressionId{2U});
    Check(
        heldBefore.Succeeded() && !heldBefore.value.booleanValue,
        "physical expression initially reads idle");
    const inputweaver::ExpressionDescriptor& heldExpression = program->Expressions()[2U];
    const std::uint32_t heldControl =
        program->ExpressionCode()[heldExpression.code.begin].operand0;
    (void)harness.runtime.HandleInput(KeyboardEvent(
        heldControl,
        inputweaver::Transition::Down));
    const auto heldAfter = harness.runtime.EvaluateExpression(
        inputweaver::ExpressionId{2U});
    Check(
        heldAfter.Succeeded() && heldAfter.value.booleanValue,
        "physical state updates before later expression evaluation");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeArrayActionStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    const StringId stateArrayName{
        static_cast<std::uint32_t>(storage.strings.size())};
    storage.strings.push_back("gates");
    const StringId numberArrayName{
        static_cast<std::uint32_t>(storage.strings.size())};
    storage.strings.push_back("values");
    const StringId resultName{
        static_cast<std::uint32_t>(storage.strings.size())};
    storage.strings.push_back("result");
    storage.arrays = {
        {ArrayElementType::State, {0U, 2U}},
        {ArrayElementType::Number, {0U, 2U}},
    };
    storage.initialArrayStates = {0U, 1U};
    storage.initialArrayNumbers = {1.5, 2.5};
    storage.debugInfo.arrays = {
        {stateArrayName, ArrayId{0U}, {0U, 1U}},
        {numberArrayName, ArrayId{1U}, {1U, 1U}},
    };
    storage.userValues.initialStates = {1U};
    storage.valueRefs.push_back({
        ValueDomain::UserState,
        ValueType::State,
        0U});
    storage.debugInfo.variables.push_back({
        resultName,
        ValueRefId{static_cast<std::uint32_t>(storage.valueRefs.size() - 1U)},
        {2U, 1U}});

    const auto appendExpression = [&](ExpressionType type,
                                      std::uint32_t maximumStack,
                                      std::initializer_list<ExpressionInstruction> code) {
        const ExpressionId id{
            static_cast<std::uint32_t>(storage.expressions.size())};
        const std::uint32_t begin = static_cast<std::uint32_t>(
            storage.expressionCode.size());
        storage.expressionCode.insert(
            storage.expressionCode.end(),
            code.begin(),
            code.end());
        storage.expressions.push_back({
            {begin, static_cast<std::uint32_t>(code.size())},
            type,
            maximumStack,
            source});
        storage.debugInfo.expressionInstructionSpans.insert(
            storage.debugInfo.expressionInstructionSpans.end(),
            code.size(),
            source);
        return id;
    };
    const std::uint32_t indexZero = static_cast<std::uint32_t>(
        storage.numberConstants.size());
    storage.numberConstants.push_back(0.8);
    const std::uint32_t indexOne = static_cast<std::uint32_t>(
        storage.numberConstants.size());
    storage.numberConstants.push_back(1.2);
    const std::uint32_t appendedNumber = static_cast<std::uint32_t>(
        storage.numberConstants.size());
    storage.numberConstants.push_back(3.5);
    const ExpressionId firstIndex = appendExpression(
        ExpressionType::Number,
        1U,
        {{ExpressionOpcode::PushNumber, ExpressionType::Number, indexZero, 0U},
         {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U}});
    const ExpressionId secondIndex = appendExpression(
        ExpressionType::Number,
        1U,
        {{ExpressionOpcode::PushNumber, ExpressionType::Number, indexOne, 0U},
         {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U}});
    const ExpressionId stateOn = appendExpression(
        ExpressionType::State,
        1U,
        {{ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
         {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U}});
    const ExpressionId numberValue = appendExpression(
        ExpressionType::Number,
        1U,
        {{ExpressionOpcode::PushNumber, ExpressionType::Number, appendedNumber, 0U},
         {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U}});
    static_cast<void>(appendExpression(
        ExpressionType::State,
        1U,
        {{ExpressionOpcode::PushNumber, ExpressionType::Number, indexZero, 0U},
         {ExpressionOpcode::LoadArrayElement, ExpressionType::State, 0U, 0U},
         {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U}}));
    static_cast<void>(appendExpression(
        ExpressionType::Number,
        1U,
        {{ExpressionOpcode::LoadArrayLength, ExpressionType::Number, 0U, 0U},
         {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U}}));
    static_cast<void>(appendExpression(
        ExpressionType::Number,
        1U,
        {{ExpressionOpcode::LoadArrayLength, ExpressionType::Number, 1U, 0U},
         {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U}}));
    static_cast<void>(appendExpression(
        ExpressionType::State,
        1U,
        {{ExpressionOpcode::PushNumber, ExpressionType::Number, appendedNumber, 0U},
         {ExpressionOpcode::LoadArrayElement, ExpressionType::State, 0U, 0U},
         {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U}}));

    storage.actionPrograms = {{{0U, 6U}, 0U, 0U, source}};
    storage.actionCode = {
        {ActionOpcode::SetArrayElement, 0U, firstIndex.value, stateOn.value},
        {ActionOpcode::ToggleArrayElement, 0U, secondIndex.value, 0U},
        {ActionOpcode::AppendArrayElement, 1U, numberValue.value, 0U},
        {ActionOpcode::PopArrayElement, 0U, 0U, 0U},
        {ActionOpcode::ClearArray, 1U, 0U, 0U},
        {ActionOpcode::End, 0U, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans.assign(
        storage.actionCode.size(),
        source);
    return storage;
}

void TestArrayRuntime()
{
    using namespace inputweaver;
    RuntimeHarness harness;
    const auto program = Finalize(MakeArrayActionStorage());
    Check(harness.runtime.Activate(program).activated, "array fixture activates");
    const std::uint32_t trigger = TriggerControl(*program);
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, Transition::Down))
            == InputDecision::Suppress,
        "array action rule consumes its event");
    (void)harness.runtime.Pump();
    const std::size_t expressionBase = program->Expressions().size() - 4U;
    const RuntimeEvaluationResult first = harness.runtime.EvaluateExpression(
        ExpressionId{static_cast<std::uint32_t>(expressionBase)});
    const RuntimeEvaluationResult stateLength = harness.runtime.EvaluateExpression(
        ExpressionId{static_cast<std::uint32_t>(expressionBase + 1U)});
    const RuntimeEvaluationResult numberLength = harness.runtime.EvaluateExpression(
        ExpressionId{static_cast<std::uint32_t>(expressionBase + 2U)});
    const RuntimeEvaluationResult bounds = harness.runtime.EvaluateExpression(
        ExpressionId{static_cast<std::uint32_t>(expressionBase + 3U)});
    bool poppedState = true;
    Check(
        first.Succeeded() && first.value.type == ExpressionType::State
            && first.value.stateValue == 1U
            && stateLength.Succeeded()
            && stateLength.value.numberValue == 1.0
            && numberLength.Succeeded()
            && numberLength.value.numberValue == 0.0
            && bounds.fault == RuntimeEvaluationFault::ArrayBounds
            && harness.runtime.ReadUserState(0U, poppedState)
            && !poppedState,
        "array actions use floor indexes and commit set, toggle, append, pop, and clear");
    const std::size_t arrayChanges = static_cast<std::size_t>(std::count_if(
        harness.debug.events.begin(),
        harness.debug.events.end(),
        [](const RuntimeDebugEvent& event) {
            return event.kind == RuntimeDebugEventKind::ArrayChanged;
        }));
    Check(arrayChanges == 5U, "each successful array mutation publishes a snapshot");
    const RuntimeMetrics metrics = harness.runtime.Metrics();
    Check(
        metrics.currentArrayBytes != 0U
            && metrics.peakArrayBytes == metrics.currentArrayBytes,
        "array allocation metrics expose current and peak storage");

    RuntimeCapacities limited{};
    limited.maximumArrayBytes = program->Requirements().initialArrayElementBytes;
    RuntimeHarness limitedHarness(limited);
    const RuntimeActivationResult rejected = limitedHarness.runtime.Activate(program);
    Check(
        !rejected.activated
            && rejected.error.code == RuntimeActivationErrorCode::ArrayByteCapacity
            && rejected.error.subject
                == static_cast<std::uint32_t>(RuntimeActivationSubject::ArrayBytes),
        "activation accounts for array pages and directory storage");

    CompiledProgramStorage growthStorage = MakeArrayActionStorage();
    growthStorage.arrays[1].initialValues.count = 256U;
    growthStorage.initialArrayNumbers.assign(256U, 2.5);
    const SourceSpan rejectedAppendSpan{7U, 3U};
    growthStorage.debugInfo.actionInstructionSpans[2U] = rejectedAppendSpan;
    const auto growthProgram = Finalize(std::move(growthStorage));
    RuntimeArrayStorage initialStates(
        ArrayElementType::State,
        growthProgram->InitialArrayStates(),
        std::span<const double>{});
    RuntimeArrayStorage initialNumbers(
        ArrayElementType::Number,
        std::span<const std::uint8_t>{},
        growthProgram->InitialArrayNumbers());
    RuntimeCapacities growthLimit{};
    growthLimit.maximumArrayBytes = initialStates.AllocatedBytes()
        + initialNumbers.AllocatedBytes();
    RuntimeHarness growthHarness(growthLimit);
    Check(growthHarness.runtime.Activate(growthProgram).activated,
        "an exact initial array allocation fits its byte capacity");
    (void)growthHarness.runtime.HandleInput(KeyboardEvent(
        TriggerControl(*growthProgram),
        Transition::Down));
    (void)growthHarness.runtime.Pump();
    const std::size_t growthExpressionBase = growthProgram->Expressions().size() - 4U;
    const RuntimeEvaluationResult retainedLength =
        growthHarness.runtime.EvaluateExpression(ExpressionId{
            static_cast<std::uint32_t>(growthExpressionBase + 2U)});
    RuntimeDiagnosticRecord growthDiagnostic{};
    bool foundGrowthRejection = false;
    while (growthHarness.runtime.TryPopDiagnostic(growthDiagnostic)) {
        foundGrowthRejection = foundGrowthRejection
            || (growthDiagnostic.kind == RuntimeDiagnosticKind::TaskActionFault
                && growthDiagnostic.subject == 1U
                && growthDiagnostic.position == 2U
                && growthDiagnostic.source == rejectedAppendSpan
                && growthDiagnostic.detail == 25U);
    }
    const RuntimeMetrics growthMetrics = growthHarness.runtime.Metrics();
    Check(
        retainedLength.Succeeded()
            && retainedLength.value.numberValue == 256.0
            && growthMetrics.currentArrayBytes == growthLimit.maximumArrayBytes
            && growthMetrics.peakArrayBytes == growthLimit.maximumArrayBytes
            && growthMetrics.rejectedArrayGrowth == 1U
            && foundGrowthRejection,
        "a rejected page growth rolls back and identifies its array action");
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeArrowFixtureStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.rules = {
        {ExpressionId{}, ActionProgramId{}, MappingId{}, Delivery::Observe,
            MatchFlow::Continue, RuleKind::Event, 0U, source},
        {ExpressionId{}, ActionProgramId{0U}, MappingId{}, Delivery::Consume,
            MatchFlow::Continue, RuleKind::Event, 1U, source},
        {ExpressionId{}, ActionProgramId{0U}, MappingId{}, Delivery::Observe,
            MatchFlow::Stop, RuleKind::Event, 2U, source},
        {ExpressionId{}, ActionProgramId{}, MappingId{}, Delivery::Consume,
            MatchFlow::Stop, RuleKind::Event, 3U, source},
    };
    storage.eventBuckets = {
        {{ControlRefId{1U}, EventTransition::Down}, {0U, 3U}},
        {{ControlRefId{1U}, EventTransition::Up}, {3U, 1U}},
    };
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeConditionalArrowFixtureStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.strings.push_back("a");
    storage.userValues.initialNumbers = {5.0};
    storage.valueRefs = {{ValueDomain::UserNumber, ValueType::Number, 0U}};
    storage.debugInfo.variables = {{StringId{1U}, ValueRefId{0U}, source}};
    storage.numberConstants = {5.0};
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::LoadValue, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::NumberLess), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    AppendExpression(storage, ExpressionType::Boolean, 2U, {
        {ExpressionOpcode::LoadValue, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::NumberGreaterEqual), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), source);
    storage.rules = {
        {ExpressionId{1U}, ActionProgramId{}, MappingId{}, Delivery::Observe,
            MatchFlow::Stop, RuleKind::Event, 0U, source},
        {ExpressionId{2U}, ActionProgramId{0U}, MappingId{}, Delivery::Observe,
            MatchFlow::Stop, RuleKind::Event, 1U, source},
    };
    storage.eventBuckets = {
        {{ControlRefId{1U}, EventTransition::Down}, {0U, 2U}},
    };
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestArrowFlowAndOverlappingOwnership()
{
    RuntimeHarness harness;
    const auto program = Finalize(MakeArrowFixtureStorage());
    Check(harness.runtime.Activate(program).activated, "arrow fixture activates");
    const std::uint32_t trigger = TriggerControl(*program);
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Suppress,
        "consume-continue combines with observe-stop to suppress the event");
    (void)harness.runtime.Pump();
    Check(
        harness.runtime.Metrics().startedTasks == 2U
            && harness.output.requests.size() == 1U
            && harness.output.requests[0].transition
                == inputweaver::RuntimeOutputTransition::Down,
        "source-ordered tasks merge overlapping output ownership");
    harness.clock.Advance(30'000'000);
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 2U
            && harness.output.requests[1].transition
                == inputweaver::RuntimeOutputTransition::Up
            && harness.runtime.Metrics().completedTasks == 2U,
        "last overlapping owner emits the single paired release");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Up))
            == inputweaver::InputDecision::Suppress,
        "consume-stop empty action rule applies delivery without a task");
    (void)harness.runtime.Pump();
    Check(
        harness.runtime.Metrics().startedTasks == 2U,
        "empty action rule does not allocate a task");

    RuntimeHarness conditional;
    const auto conditionalProgram = Finalize(MakeConditionalArrowFixtureStorage());
    Check(
        conditional.runtime.Activate(conditionalProgram).activated,
        "conditional arrow fixture activates");
    Check(
        conditional.runtime.HandleInput(KeyboardEvent(
            TriggerControl(*conditionalProgram), inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward,
        "an observe-stop match forwards the event");
    (void)conditional.runtime.Pump();
    Check(
        conditional.runtime.Metrics().startedTasks == 1U
            && conditional.output.requests.size() == 1U,
        "a false stop rule continues to the later greater-equal match");
}

