#include "program/compiled_program.hpp"
#include "program/program_validator.hpp"
#include "program/weavec_codec.hpp"
#include "runtime/artifact_loader.hpp"
#include "runtime/program_runtime.hpp"
#include "../program/compiled_program_fixtures.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_failureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++g_failureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

class FakeClock final : public inputweaver::RuntimeClock {
public:
    [[nodiscard]] std::int64_t NowNanoseconds() const noexcept override
    {
        return now_;
    }

    void Advance(std::int64_t nanoseconds) noexcept
    {
        now_ += nanoseconds;
    }

private:
    std::int64_t now_{};
};

class FakeControlPort final : public inputweaver::RuntimeControlPort {
public:
    bool rejectRepeat{};
    bool rejectPhysicalState{};
    bool rejectAll{};

    [[nodiscard]] inputweaver::RuntimeControlBindResult BindControl(
        inputweaver::ControlRefId controlId,
        const inputweaver::ControlRef& control,
        std::uint8_t requiredUses,
        inputweaver::ActivatedControl& activated) noexcept override
    {
        (void)controlId;
        (void)control;
        activated.capabilities = rejectAll
            ? 0U
            : static_cast<std::uint8_t>(
                  inputweaver::ToControlUseBits(inputweaver::ControlUse::EventSource)
                  | (rejectPhysicalState
                         ? 0U
                         : inputweaver::ToControlUseBits(
                               inputweaver::ControlUse::PhysicalState))
                  | inputweaver::ToControlUseBits(inputweaver::ControlUse::OutputDownUp)
                  | (rejectRepeat
                         ? 0U
                         : inputweaver::ToControlUseBits(
                               inputweaver::ControlUse::OutputRepeat)));
        activated.device = inputweaver::DeviceKind::Keyboard;
        activated.initialStateQueryable = !rejectPhysicalState;
        if (rejectAll) {
            return inputweaver::RuntimeControlBindResult::UnsupportedIdentity;
        }
        return (activated.capabilities & requiredUses) == requiredUses
            ? inputweaver::RuntimeControlBindResult::Bound
            : inputweaver::RuntimeControlBindResult::MissingCapability;
    }
};

class FakeOutputPort final : public inputweaver::RuntimeOutputPort {
public:
    std::vector<inputweaver::RuntimeOutputRequest> requests;
    inputweaver::RuntimeOutputResult nextResult{
        inputweaver::RuntimeOutputResult::Accepted};
    inputweaver::RuntimeOutputResult repeatedFailure{
        inputweaver::RuntimeOutputResult::Failed};
    std::size_t failuresRemaining{};

    [[nodiscard]] inputweaver::RuntimeOutputResult Publish(
        const inputweaver::RuntimeOutputRequest& request) noexcept override
    {
        try {
            requests.push_back(request);
        } catch (...) {
            return inputweaver::RuntimeOutputResult::CapacityRejected;
        }
        if (failuresRemaining != 0U) {
            --failuresRemaining;
            return repeatedFailure;
        }
        const inputweaver::RuntimeOutputResult result = nextResult;
        nextResult = inputweaver::RuntimeOutputResult::Accepted;
        return result;
    }
};

class FakeRoutePort final : public inputweaver::RuntimeRoutePort {
public:
    bool targetValid{true};
    bool dispatchAllowed{true};
    bool injectionAllowed{true};
    inputweaver::TargetSelectorKind validatedKind{
        inputweaver::TargetSelectorKind::Unspecified};
    std::atomic<bool>* dispatchEntered{};
    std::atomic<bool>* dispatchReleased{};

    [[nodiscard]] bool ValidateTarget(
        inputweaver::TargetSelectorKind kind) noexcept override
    {
        validatedKind = kind;
        return targetValid;
    }

    [[nodiscard]] bool CanDispatch(
        inputweaver::TargetSelectorKind kind,
        const inputweaver::RuntimeInputEvent& event) noexcept override
    {
        (void)kind;
        (void)event;
        if (dispatchEntered != nullptr && dispatchReleased != nullptr) {
            dispatchEntered->store(true, std::memory_order_release);
            while (!dispatchReleased->load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        }
        return dispatchAllowed;
    }

    [[nodiscard]] bool TargetValid(
        inputweaver::TargetSelectorKind kind) noexcept override
    {
        (void)kind;
        return targetValid;
    }

    [[nodiscard]] bool CanInject(
        inputweaver::TargetSelectorKind kind,
        const inputweaver::ActivatedControl& control) noexcept override
    {
        (void)kind;
        (void)control;
        return injectionAllowed;
    }
};

class FakeLauncher final : public inputweaver::RuntimeProcessLauncher {
public:
    bool permitted{true};
    inputweaver::RuntimeLaunchOutcome outcome{};
    std::vector<std::string> commands;

    [[nodiscard]] bool Permitted() const noexcept override
    {
        return permitted;
    }

    [[nodiscard]] inputweaver::RuntimeLaunchOutcome Launch(
        std::string_view command,
        inputweaver::RuntimeCancellationProbe cancellation) noexcept override
    {
        if (cancellation.Cancelled()) {
            return {inputweaver::RuntimeLaunchResult::Cancelled, 0U};
        }
        try {
            commands.emplace_back(command);
        } catch (...) {
            return {inputweaver::RuntimeLaunchResult::CreationFailed, 0U};
        }
        return outcome;
    }
};

struct RuntimeHarness final {
    FakeClock clock;
    FakeControlPort controls;
    FakeOutputPort output;
    FakeRoutePort route;
    FakeLauncher launcher;
    inputweaver::ProgramRuntime runtime;

    explicit RuntimeHarness(inputweaver::RuntimeCapacities capacities = {})
        : runtime(capacities, controls, output, route, launcher, clock)
    {
    }
};

void RebuildControlRequirements(inputweaver::CompiledProgramStorage& storage)
{
    using namespace inputweaver;
    std::vector<std::uint8_t> uses(storage.controls.size(), 0U);
    const auto add = [&uses](ControlRefId control, ControlUse use) {
        if (control.value < uses.size()) {
            uses[control.value] = static_cast<std::uint8_t>(
                uses[control.value] | ToControlUseBits(use));
        }
    };
    for (const ExpressionInstruction& instruction : storage.expressionCode) {
        if (instruction.opcode == ExpressionOpcode::ReadControlHeld) {
            add(ControlRefId{instruction.operand0}, ControlUse::PhysicalState);
        }
    }
    for (const ActionInstruction& instruction : storage.actionCode) {
        if (instruction.opcode == ActionOpcode::Press
            || instruction.opcode == ActionOpcode::Release
            || instruction.opcode == ActionOpcode::Tap) {
            add(ControlRefId{instruction.operand0}, ControlUse::OutputDownUp);
        }
    }
    for (const ExitControlBucket& bucket : storage.exitControlBuckets) {
        add(bucket.key.control, ControlUse::EventSource);
    }
    for (const PauseControlBucket& bucket : storage.pauseControlBuckets) {
        add(bucket.key.control, ControlUse::EventSource);
    }
    for (const EventBucket& bucket : storage.eventBuckets) {
        add(bucket.key.control, ControlUse::EventSource);
    }
    for (const MappingSlotDescriptor& slot : storage.mappingSlots) {
        add(slot.source, ControlUse::EventSource);
    }
    for (const MappingDescriptor& mapping : storage.mappings) {
        add(mapping.target, ControlUse::OutputDownUp);
        add(mapping.target, ControlUse::OutputRepeat);
    }
    storage.controlRequirements.clear();
    for (std::size_t index = 0U; index < uses.size(); ++index) {
        if (uses[index] != 0U) {
            storage.controlRequirements.push_back({
                ControlRefId{static_cast<std::uint32_t>(index)},
                uses[index]});
        }
    }
}

[[nodiscard]] inputweaver::RuntimeCapacities ProcessLaunchCapacities() noexcept
{
    inputweaver::RuntimeCapacities capacities{};
    capacities.permitProcessLaunch = true;
    return capacities;
}

[[nodiscard]] std::shared_ptr<const inputweaver::CompiledProgram> Finalize(
    inputweaver::CompiledProgramStorage storage)
{
    for (inputweaver::ExitControlRule& rule : storage.exitControlRules) {
        if (rule.sourceOrdinal == inputweaver::kInvalidProgramIndex) {
            rule.condition = {};
        }
    }
    RebuildControlRequirements(storage);
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    inputweaver::FinalizeResult result = inputweaver::FinalizeCompiledProgram(
        std::move(storage));
    if (!result.errors.empty()) {
        for (const auto& error : result.errors) {
            std::cerr << "Fixture validation error: " << error.location
                      << ": " << error.message << '\n';
        }
    }
    Check(result.program != nullptr, "test fixture finalizes");
    return std::move(result.program);
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
            inputweaver::InputOrigin::SelfInjected))
            == inputweaver::InputDecision::Forward,
        "self-injected input bypasses runtime state and rules");
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
        "active mapping repeat is consumed before ordinary rules");
    (void)harness.runtime.Pump();
    Check(
        harness.output.requests.size() == 2U
            && harness.output.requests.back().transition
                == inputweaver::RuntimeOutputTransition::Repeat,
        "mapping forwards repeat lifecycle output");
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
    const auto program = Finalize(
        inputweaver::test::MakeConditionalRepeatFixtureStorage());
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
    Check(harness.output.requests.size() == 4U, "repeat fixture taps twice");
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
        {{ControlRefId{0U}, EventTransition::Repeat}, {2U, 1U}},
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
        "pause On observes a repeat and recovers while off");
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
    AppendExpression(storage, ExpressionType::Boolean, 1U, {
        {ExpressionOpcode::ReadControlHeld, ExpressionType::Boolean, 0U, 0U},
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
}

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
    bool decisionsValid = true;
    std::size_t acceptedTasks = 0U;
    for (std::size_t index = 0U; index < taskCount; ++index) {
        const inputweaver::InputDecision decision = harness.runtime.HandleInput(
            KeyboardEvent(trigger, inputweaver::Transition::Down));
        decisionsValid = decisionsValid
            && (decision == inputweaver::InputDecision::Suppress
                || decision == inputweaver::InputDecision::Forward);
        if (decision == inputweaver::InputDecision::Suppress) {
            ++acceptedTasks;
        }
        (void)harness.runtime.HandleInput(KeyboardEvent(
            trigger,
            inputweaver::Transition::Up));
    }
    for (std::size_t attempt = 0U;
         attempt < 200U
            && harness.runtime.Metrics().completedTasks < acceptedTasks;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    harness.runtime.StopTaskThread();
    stopReader.store(true, std::memory_order_release);
    reader.join();
    bool finalValue = true;
    Check(
        decisionsValid
            && acceptedTasks != 0U
            && harness.runtime.Metrics().completedTasks == acceptedTasks
            && harness.runtime.ReadUserState(0U, finalValue)
            && finalValue == ((acceptedTasks % 2U) != 0U)
            && readerPasses.load(std::memory_order_relaxed) != 0U
            && !harness.runtime.FatalShutdownRequested(),
        "nonblocking hook locks fail open while accepted toggles remain deterministic");
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
    storage.debugInfo.expressionInstructionSpans.assign(4U, source);
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
            || diagnostic.kind
                == inputweaver::RuntimeDiagnosticKind::TaskExpressionFault;
    }
    Check(
        harness.runtime.FatalShutdownRequested()
            && harness.runtime.ActiveTaskCount() == 0U
            && foundFault,
        "task expression fault requests shutdown and records its action position");
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
            && !failedOutput.runtime.HasOwnedOutputs(),
        "output publication failure requests controlled fatal shutdown without ownership leak");

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
        {ControlRefId{1U}, EventTransition::Repeat}, {1U, 1U}});
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
                == inputweaver::RuntimeOutputTransition::Repeat,
        "mapping repeat and overlapping tap retain one global down owner");
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
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
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
    missingCapability.controls.rejectRepeat = true;
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
    retained.controls.rejectRepeat = true;
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
        "eligible target commits a mapping repeat");
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
                == inputweaver::RuntimeOutputTransition::Repeat
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
        "held source repeat after foreground return does not remap");
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
        "terminal target loss releases ownership before disabling dispatch");
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
        "startup-held source repeat cannot create a mapping");
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

} // namespace

int main()
{
    TestEffectiveTargetOverride();
    TestTapFixture();
    TestMappingFixture();
    TestConditionalRepeatFixture();
    TestPauseFixtureAndCancellation();
    TestPauseEffectsAndIdempotence();
    TestExitControlSemantics();
    TestExpressionVm();
    TestArrowFlowAndOverlappingOwnership();
    TestActionVm();
    TestTaskProgressBudgets();
    TestMaximumSynchronousDispatch();
    TestNestedRepeatScheduling();
    TestConcurrentStateLocks();
    TestExecCancellationBoundaries();
    TestTaskExpressionFault();
    TestOwnershipFaultsAndOutputFailure();
    TestMappingAndTaskOwnershipOverlap();
    TestPredicateFaultAndDiagnostics();
    TestActivationAndTransientCapacity();
    TestArtifactLoading();
    TestBoundedRuntimeStress();
    TestReversibleTargetEligibility();
    TestMappingCancellationReleasePaths();
    TestPhysicalStateInitialization();
    TestOutputRateBudget();
    TestRoutingFailureAndExit();
    TestProductionTaskThread();

    if (g_failureCount != 0) {
        std::cerr << g_failureCount << " Phase 4 runtime test(s) failed.\n";
        return 1;
    }
    std::cout << "All Phase 4 runtime core tests passed.\n";
    return 0;
}
