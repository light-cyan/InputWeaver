#include "program/compiled_program.hpp"
#include "program/program_validator.hpp"
#include "program/weavec_codec.hpp"
#include "runtime/artifact_loader.hpp"
#include "runtime/program_runtime.hpp"
#include "../program/compiled_program_fixtures.hpp"

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
                  | inputweaver::ToControlUseBits(inputweaver::ControlUse::PhysicalState)
                  | inputweaver::ToControlUseBits(inputweaver::ControlUse::OutputDownUp)
                  | (rejectRepeat
                         ? 0U
                         : inputweaver::ToControlUseBits(
                               inputweaver::ControlUse::OutputRepeat)));
        activated.device = inputweaver::DeviceKind::Keyboard;
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

    [[nodiscard]] bool ValidateTarget(
        inputweaver::TargetSelectorKind kind,
        std::string_view selector) noexcept override
    {
        (void)kind;
        (void)selector;
        return targetValid;
    }

    [[nodiscard]] bool CanDispatch(
        inputweaver::TargetSelectorKind kind,
        const inputweaver::RuntimeInputEvent& event) noexcept override
    {
        (void)kind;
        (void)event;
        return dispatchAllowed;
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
    inputweaver::RuntimeLaunchResult result{
        inputweaver::RuntimeLaunchResult::Launched};
    std::vector<std::string> commands;

    [[nodiscard]] bool Permitted() const noexcept override
    {
        return permitted;
    }

    [[nodiscard]] inputweaver::RuntimeLaunchResult Launch(
        std::string_view command,
        inputweaver::RuntimeCancellationProbe cancellation) noexcept override
    {
        if (cancellation.Cancelled()) {
            return inputweaver::RuntimeLaunchResult::Cancelled;
        }
        try {
            commands.emplace_back(command);
        } catch (...) {
            return inputweaver::RuntimeLaunchResult::CreationFailed;
        }
        return result;
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

[[nodiscard]] std::shared_ptr<const inputweaver::CompiledProgram> Finalize(
    inputweaver::CompiledProgramStorage storage)
{
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
    Check(harness.runtime.Activate(tapProgram).activated, "tap reload succeeds");
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
    storage.controlRequirements[0].uses = static_cast<std::uint8_t>(
        storage.controlRequirements[0].uses
        | ToControlUseBits(ControlUse::PhysicalState));
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

void TestActionVm()
{
    RuntimeHarness harness;
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
    for (std::size_t index = 0U; index < taskCount; ++index) {
        decisionsValid = decisionsValid
            && harness.runtime.HandleInput(KeyboardEvent(
                trigger,
                inputweaver::Transition::Down))
                == inputweaver::InputDecision::Suppress;
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
        decisionsValid
            && harness.runtime.Metrics().completedTasks == taskCount
            && harness.runtime.ReadUserState(0U, finalValue)
            && !finalValue
            && readerPasses.load(std::memory_order_relaxed) != 0U
            && !harness.runtime.FatalShutdownRequested(),
        "blocking state locks preserve every event decision and exclusive toggle");
}

void TestExecCancellationBoundaries()
{
    const auto program = Finalize(MakeActionFixtureStorage());
    const std::uint32_t trigger = TriggerControl(*program);
    RuntimeHarness cancelledBefore;
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

    RuntimeHarness cancelledAfter;
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

    RuntimeHarness failedLaunch;
    failedLaunch.launcher.result = inputweaver::RuntimeLaunchResult::CreationFailed;
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

    inputweaver::RuntimeCapacities controlCapacity{};
    controlCapacity.maximumControls = 1U;
    checkCapacityRejection(
        controlCapacity,
        tap,
        inputweaver::RuntimeActivationErrorCode::ControlCapacity,
        "control capacity rejects activation before publication");
    inputweaver::RuntimeCapacities valueCapacity{};
    valueCapacity.maximumStateSlots = 0U;
    checkCapacityRejection(
        valueCapacity,
        repeat,
        inputweaver::RuntimeActivationErrorCode::ValueCapacity,
        "value capacity rejects activation before publication");
    inputweaver::RuntimeCapacities mappingCapacity{};
    mappingCapacity.maximumMappingSlots = 0U;
    checkCapacityRejection(
        mappingCapacity,
        mapping,
        inputweaver::RuntimeActivationErrorCode::MappingCapacity,
        "mapping capacity rejects activation before publication");
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
    inputweaver::RuntimeCapacities launchPolicy{};
    launchPolicy.permitProcessLaunch = false;
    checkCapacityRejection(
        launchPolicy,
        actions,
        inputweaver::RuntimeActivationErrorCode::ProcessLaunchDenied,
        "process policy rejects exec program before publication");

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
    RuntimeHarness harness;
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

void TestRoutingFailureAndForceStop()
{
    RuntimeHarness harness;
    const auto tap = Finalize(inputweaver::test::MakeTapFixtureStorage());
    const std::uint32_t trigger = TriggerControl(*tap);
    Check(harness.runtime.Activate(tap).activated, "routing test program activates");
    harness.route.dispatchAllowed = false;
    const std::uint64_t generation = harness.runtime.Generation();
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
                == inputweaver::InputDecision::Forward
            && harness.runtime.Generation() == generation + 1U,
        "target routing rejection forwards input and invalidates execution");
    harness.route.dispatchAllowed = true;
    inputweaver::RuntimeInputEvent stop = KeyboardEvent(
        trigger,
        inputweaver::Transition::Up);
    stop.forceStopRequested = true;
    Check(
        harness.runtime.HandleInput(stop) == inputweaver::InputDecision::Suppress,
        "force stop precedes ordinary routing and suppresses its completion event");
    Check(
        harness.runtime.HandleInput(KeyboardEvent(trigger, inputweaver::Transition::Down))
            == inputweaver::InputDecision::Forward,
        "force stop disables further transactions");
}

} // namespace

int main()
{
    TestTapFixture();
    TestMappingFixture();
    TestConditionalRepeatFixture();
    TestPauseFixtureAndCancellation();
    TestPauseEffectsAndIdempotence();
    TestExpressionVm();
    TestArrowFlowAndOverlappingOwnership();
    TestActionVm();
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
    TestRoutingFailureAndForceStop();
    TestProductionTaskThread();

    if (g_failureCount != 0) {
        std::cerr << g_failureCount << " Phase 3 runtime test(s) failed.\n";
        return 1;
    }
    std::cout << "All Phase 3 runtime core tests passed.\n";
    return 0;
}
