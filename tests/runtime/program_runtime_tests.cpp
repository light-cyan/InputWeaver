#include "program/compiled_program.hpp"
#include "program/program_requirements.hpp"
#include "program/weavec_codec.hpp"
#include "runtime/artifact_loader.hpp"
#include "runtime/array_storage.hpp"
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
    bool rejectAgain{};
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
            | (rejectAgain
                         ? 0U
                         : inputweaver::ToControlUseBits(
                    inputweaver::ControlUse::OutputAgain)));
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
    inputweaver::RuntimeOutputResult againFailure{
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
        return againFailure;
        }
        const inputweaver::RuntimeOutputResult result = nextResult;
        nextResult = inputweaver::RuntimeOutputResult::Accepted;
        return result;
    }
};

class FakeRoutePort final : public inputweaver::RuntimeRoutePort {
public:
    bool targetSupported{true};
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
        return targetSupported;
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
        if (cancellation.Invoke()) {
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

class FakeDebugPort final : public inputweaver::RuntimeDebugEventPort {
public:
    std::vector<inputweaver::RuntimeDebugEvent> events;
    std::atomic<bool>* executionStarted{};

    [[nodiscard]] bool Publish(
        const inputweaver::RuntimeDebugEvent& event) noexcept override
    {
        if (executionStarted != nullptr
            && event.kind == inputweaver::RuntimeDebugEventKind::RuleMatched) {
            executionStarted->store(true, std::memory_order_release);
        }
        try {
            events.push_back(event);
            return true;
        } catch (...) {
            return false;
        }
    }
};

struct RuntimeHarness final {
    FakeClock clock;
    FakeControlPort controls;
    FakeOutputPort output;
    FakeRoutePort route;
    FakeLauncher launcher;
    FakeDebugPort debug;
    std::atomic<bool> fatalStopRequested{false};
    inputweaver::ProgramRuntime runtime;

    explicit RuntimeHarness(inputweaver::RuntimeCapacities capacities = {})
        : runtime(
              capacities,
              controls,
              output,
              route,
              launcher,
              clock,
              &debug,
              {this, &RuntimeHarness::RequestFatalStop})
    {
    }

    static void RequestFatalStop(void* context) noexcept
    {
        static_cast<RuntimeHarness*>(context)->fatalStopRequested.store(
            true,
            std::memory_order_release);
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
        if (instruction.opcode == ExpressionOpcode::ReadControlState) {
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
        add(mapping.target, ControlUse::OutputAgain);
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

#include "program_runtime_core_tests.inc"
#include "program_runtime_task_tests.inc"
#include "program_runtime_integration_tests.inc"

} // namespace

int main()
{
    TestArrayStorage();
    TestEffectiveTargetOverride();
    TestTapFixture();
    TestMappingFixture();
    TestConditionalRepeatFixture();
    TestPauseFixtureAndCancellation();
    TestPauseEffectsAndIdempotence();
    TestExitControlSemantics();
    TestExpressionVm();
    TestArrayRuntime();
    TestArrowFlowAndOverlappingOwnership();
    TestActionVm();
    TestTaskProgressBudgets();
    TestMaximumSynchronousDispatch();
    TestNestedRepeatScheduling();
    TestConcurrentStateLocks();
    TestEventSnapshotAndCancelledMutation();
    TestExecCancellationBoundaries();
    TestTaskExpressionFault();
    TestRuntimeDebugEvents();
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
