#include "program_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <semaphore>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace inputweaver {
namespace {

constexpr std::uint32_t kInvalidTaskSlot = kInvalidProgramIndex;

enum class TaskStatus : std::uint8_t {
    Free,
    Initializing,
    Reserved,
    Ready,
    Timed,
    Running,
    Cleanup,
};

enum class TaskResumeKind : std::uint8_t {
    None,
    TapRelease,
};

enum class WorkKind : std::uint8_t {
    TaskStart,
    MappingAcquire,
    MappingRepeat,
    MappingRelease,
};

[[nodiscard]] constexpr bool IsDebugIssue(
    RuntimeDiagnosticKind kind) noexcept
{
    switch (kind) {
    case RuntimeDiagnosticKind::OwnershipChange:
    case RuntimeDiagnosticKind::MappingChange:
    case RuntimeDiagnosticKind::Cancellation:
    case RuntimeDiagnosticKind::TargetEligibilityChange:
        return false;
    default:
        return true;
    }
}

[[nodiscard]] constexpr RuntimeExecutionResult MergeExecutionResult(
    RuntimeExecutionResult existing,
    RuntimeExecutionResult requested) noexcept
{
    if (existing == RuntimeExecutionResult::Failed
        || requested == RuntimeExecutionResult::Failed) {
        return RuntimeExecutionResult::Failed;
    }
    if (existing == RuntimeExecutionResult::Cancelled
        || requested == RuntimeExecutionResult::Cancelled) {
        return RuntimeExecutionResult::Cancelled;
    }
    return RuntimeExecutionResult::Completed;
}

struct RepeatFrame final {
    std::uint64_t index{};
    double limit{};
};

struct TaskOwnershipRecord final {
    ControlRefId control{};
    std::uint64_t count{};
};

struct TaskInstance final {
    std::atomic<TaskStatus> status{TaskStatus::Free};
    ActionProgramId action{};
    std::uint32_t position{};
    std::uint64_t generation{};
    std::uint64_t readyOrder{};
    std::uint64_t timedOrder{};
    std::int64_t deadlineNanoseconds{};
    TaskResumeKind resumeKind{TaskResumeKind::None};
    ControlRefId pendingTap{};
    std::vector<RepeatFrame> repeatFrames;
    std::vector<TaskOwnershipRecord> ownership;
    std::uint32_t instructionsWithoutSuspension{};
    std::uint32_t outputsWithoutSuspension{};
    std::uint64_t debugCaptureEpoch{};
    std::uint64_t debugExecutionMarker{};
    RuntimeExecutionResult cleanupResult{RuntimeExecutionResult::Completed};
    bool cleanupCancelled{};
    bool safetyCancelled{};
};

struct MappingOwner final {
    bool owned{};
    std::uint64_t generation{};
    MappingId mapping{};
    ControlRefId target{};
};

struct WorkItem final {
    WorkKind kind{};
    std::uint64_t generation{};
    std::uint32_t taskSlot{kInvalidTaskSlot};
    ActionProgramId action{};
    MappingId mapping{};
    MappingSlotId mappingSlot{};
    ControlRefId control{};
    std::uint64_t debugCaptureEpoch{};
    std::uint64_t debugInputSequence{};
    EventKey debugEventKey{};
    std::uint32_t debugRuleIndex{kInvalidProgramIndex};
};

class WorkQueue final {
public:
    explicit WorkQueue(std::size_t capacity)
        : entries_(capacity)
    {
    }

    [[nodiscard]] std::size_t Available() const noexcept
    {
        const std::uint64_t write = write_.load(std::memory_order_acquire);
        const std::uint64_t read = read_.load(std::memory_order_acquire);
        const std::uint64_t used = write - read;
        return used >= entries_.size()
            ? 0U
            : entries_.size() - static_cast<std::size_t>(used);
    }

    [[nodiscard]] bool TryPush(std::span<const WorkItem> items) noexcept
    {
        if (items.size() > Available()) {
            return false;
        }
        const std::uint64_t write = write_.load(std::memory_order_relaxed);
        for (std::size_t index = 0U; index < items.size(); ++index) {
            entries_[static_cast<std::size_t>(
                (write + index) % entries_.size())] = items[index];
        }
        write_.store(write + items.size(), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPop(WorkItem& item) noexcept
    {
        const std::uint64_t read = read_.load(std::memory_order_relaxed);
        const std::uint64_t write = write_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        item = entries_[static_cast<std::size_t>(read % entries_.size())];
        read_.store(read + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return read_.load(std::memory_order_acquire)
            == write_.load(std::memory_order_acquire);
    }

private:
    std::vector<WorkItem> entries_;
    alignas(64) std::atomic<std::uint64_t> write_{0U};
    alignas(64) std::atomic<std::uint64_t> read_{0U};
};

class DiagnosticBuffer final {
public:
    explicit DiagnosticBuffer(std::size_t capacity)
        : records_(capacity)
    {
    }

    void TryPush(const RuntimeDiagnosticRecord& record) noexcept
    {
        if (lock_.test_and_set(std::memory_order_acquire)) {
            dropped_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if (write_ - read_ >= records_.size()) {
            dropped_.fetch_add(1U, std::memory_order_relaxed);
            lock_.clear(std::memory_order_release);
            return;
        }
        records_[static_cast<std::size_t>(write_ % records_.size())] = record;
        ++write_;
        lock_.clear(std::memory_order_release);
    }

    [[nodiscard]] bool TryPop(RuntimeDiagnosticRecord& record) noexcept
    {
        if (lock_.test_and_set(std::memory_order_acquire)) {
            return false;
        }
        if (read_ == write_) {
            lock_.clear(std::memory_order_release);
            return false;
        }
        record = records_[static_cast<std::size_t>(read_ % records_.size())];
        ++read_;
        lock_.clear(std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint64_t Dropped() const noexcept
    {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    std::vector<RuntimeDiagnosticRecord> records_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::uint64_t write_{};
    std::uint64_t read_{};
    std::atomic<std::uint64_t> dropped_{0U};
};

[[nodiscard]] std::int64_t AddDeadline(
    std::int64_t now,
    DurationValue duration) noexcept
{
    const std::int64_t maximum = (std::numeric_limits<std::int64_t>::max)();
    if (duration.nanoseconds > maximum - now) {
        return maximum;
    }
    return now + duration.nanoseconds;
}

[[nodiscard]] bool IsUserDomain(ValueDomain domain) noexcept
{
    return domain == ValueDomain::UserState
        || domain == ValueDomain::UserNumber
        || domain == ValueDomain::UserDuration;
}

struct TaskCancellationContext final {
    const std::atomic<std::uint64_t>* generation{};
    std::uint64_t expected{};
};

[[nodiscard]] bool TaskCancelled(void* rawContext) noexcept
{
    const auto* context = static_cast<const TaskCancellationContext*>(rawContext);
    return context == nullptr
        || context->generation == nullptr
        || context->generation->load(std::memory_order_acquire) != context->expected;
}

} // namespace

struct ProgramRuntime::Impl final {
    struct MutableState final {
        explicit MutableState(const CompiledProgram& program)
            : userStates(program.UserValues().initialStates),
              userNumbers(program.UserValues().initialNumbers),
              userDurations(program.UserValues().initialDurations),
              physicalHeld(std::make_unique<std::atomic<std::uint8_t>[]>(
                  program.Controls().size())),
              physicalSynchronized(
                  std::make_unique<std::atomic<std::uint8_t>[]>(
                      program.Controls().size()))
        {
            for (std::size_t index = 0U; index < program.Controls().size(); ++index) {
                physicalHeld[index].store(0U, std::memory_order_relaxed);
                physicalSynchronized[index].store(1U, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] RuntimeExpressionState ExpressionState(
            const CompiledProgram& program) const noexcept
        {
            return {
                userStates,
                userNumbers,
                userDurations,
                {physicalHeld.get(), program.Controls().size()},
                pauseOn,
                program.Settings().tapDuration,
                program.Settings().actionGap};
        }

        [[nodiscard]] bool PauseEnabled() const noexcept
        {
            std::shared_lock lock(pauseMutex);
            return pauseOn;
        }

        [[nodiscard]] bool ReadUserState(
            std::uint32_t index,
            bool& value) const noexcept
        {
            std::shared_lock lock(variableMutex);
            if (index >= userStates.size()) {
                return false;
            }
            value = userStates[index] != 0U;
            return true;
        }

        mutable std::shared_mutex pauseMutex;
        mutable std::shared_mutex variableMutex;
        bool pauseOn{true};
        std::vector<std::uint8_t> userStates;
        std::vector<double> userNumbers;
        std::vector<DurationValue> userDurations;
        std::unique_ptr<std::atomic<std::uint8_t>[]> physicalHeld;
        std::unique_ptr<std::atomic<std::uint8_t>[]> physicalSynchronized;
    };

    struct DispatchState final {
        DispatchState(
            const CompiledProgram& program,
            const RuntimeCapacities& capacities)
            : activeMappings(
                  std::make_unique<std::atomic<std::uint32_t>[]>(
                      program.MappingSlots().size())),
              mappingOwners(program.MappingSlots().size()),
              workQueue(capacities.transactionQueueItemCount),
              transactionScratch((std::max)(
                  std::size_t{1U},
                  static_cast<std::size_t>(
                      program.Requirements().maximumTransactionItemsPerEvent))),
              reservedTaskScratch((std::max)(
                  std::size_t{1U},
                  static_cast<std::size_t>(
                      program.Requirements().maximumTasksPerEvent))),
              expressionScratch((std::max)(
                  std::size_t{1U},
                  static_cast<std::size_t>(
                      program.Requirements().maximumExpressionStackDepth)))
        {
            for (std::size_t index = 0U;
                 index < program.MappingSlots().size();
                 ++index) {
                activeMappings[index].store(
                    kInvalidProgramIndex,
                    std::memory_order_relaxed);
            }
        }

        [[nodiscard]] bool HasActiveMappings() const noexcept
        {
            for (std::size_t index = 0U; index < mappingOwners.size(); ++index) {
                if (activeMappings[index].load(std::memory_order_acquire)
                    != kInvalidProgramIndex) {
                    return true;
                }
            }
            return false;
        }

        std::unique_ptr<std::atomic<std::uint32_t>[]> activeMappings;
        std::vector<MappingOwner> mappingOwners;
        WorkQueue workQueue;
        std::vector<WorkItem> transactionScratch;
        std::vector<std::uint32_t> reservedTaskScratch;
        RuntimeExpressionScratch expressionScratch;
    };

    struct SchedulerState final {
        SchedulerState(
            const CompiledProgram& program,
            const RuntimeCapacities& capacities)
            : tasks(capacities.taskSlotCount == 0U
                  ? nullptr
                  : std::make_unique<TaskInstance[]>(capacities.taskSlotCount)),
              taskCount(capacities.taskSlotCount),
              expressionScratch((std::max)(
                  std::size_t{1U},
                  static_cast<std::size_t>(
                      program.Requirements().maximumExpressionStackDepth)))
        {
            const std::size_t repeatCount =
                program.Requirements().maximumRepeatFramesPerTask;
            const std::size_t ownershipCount =
                program.Requirements().maximumOwnedControlsPerTask;
            for (std::size_t index = 0U; index < taskCount; ++index) {
                tasks[index].repeatFrames.resize(repeatCount);
                tasks[index].ownership.resize(ownershipCount);
            }
        }

        [[nodiscard]] std::size_t ActiveTaskCount() const noexcept
        {
            std::size_t count = 0U;
            for (std::size_t index = 0U; index < taskCount; ++index) {
                if (tasks[index].status.load(std::memory_order_acquire)
                    != TaskStatus::Free) {
                    ++count;
                }
            }
            return count;
        }

        std::unique_ptr<TaskInstance[]> tasks;
        std::size_t taskCount{};
        RuntimeExpressionScratch expressionScratch;
        std::uint64_t nextReadyOrder{1U};
        std::uint64_t nextTimedOrder{1U};
        std::uint64_t nextDebugExecutionMarker{1U};
        std::atomic<std::uint64_t> positiveSuspensions{0U};
        std::atomic_flag pumpLock = ATOMIC_FLAG_INIT;
    };

    struct OutputState final {
        explicit OutputState(std::size_t controlCount)
            : globalOwnership(controlCount)
        {
        }

        [[nodiscard]] bool HasOwnedOutputs() const noexcept
        {
            const std::lock_guard lock(mutex);
            return std::any_of(
                globalOwnership.begin(),
                globalOwnership.end(),
                [](std::uint64_t count) noexcept { return count != 0U; });
        }

        mutable std::mutex mutex;
        std::vector<std::uint64_t> globalOwnership;
        std::atomic<std::uint64_t> nextSequence{1U};
        std::int64_t rateWindowStartNanoseconds{};
        std::uint32_t rateWindowTransitions{};
    };

    struct MetricCounters final {
        [[nodiscard]] RuntimeMetrics Snapshot(
            std::uint64_t droppedDiagnostics) const noexcept
        {
            return {
                dispatchedEvents.load(std::memory_order_relaxed),
                suppressedEvents.load(std::memory_order_relaxed),
                startedTasks.load(std::memory_order_relaxed),
                completedTasks.load(std::memory_order_relaxed),
                cancelledTasks.load(std::memory_order_relaxed),
                transactionRejections.load(std::memory_order_relaxed),
                droppedDiagnostics,
                outputTransitions.load(std::memory_order_relaxed),
                schedulerBackoffs.load(std::memory_order_relaxed)};
        }

        std::atomic<std::uint64_t> dispatchedEvents{0U};
        std::atomic<std::uint64_t> suppressedEvents{0U};
        std::atomic<std::uint64_t> startedTasks{0U};
        std::atomic<std::uint64_t> completedTasks{0U};
        std::atomic<std::uint64_t> cancelledTasks{0U};
        std::atomic<std::uint64_t> transactionRejections{0U};
        std::atomic<std::uint64_t> outputTransitions{0U};
        std::atomic<std::uint64_t> schedulerBackoffs{0U};
    };

    struct State final {
        State(
            std::shared_ptr<const CompiledProgram> immutableProgram,
            std::uint64_t serial,
            std::atomic<std::uint64_t>& runtimeGeneration,
            const RuntimeCapacities& capacities)
            : program(std::move(immutableProgram)),
              programSerial(serial),
              activatedControls(program->Controls().size()),
              mutableState(*program),
              dispatch(*program, capacities),
              scheduler(*program, capacities),
              output(program->Controls().size()),
              diagnostics(capacities.diagnosticRecordCount),
              generation(runtimeGeneration)
        {
        }

        std::shared_ptr<const CompiledProgram> program;
        std::uint64_t programSerial{};
        TargetSelectorKind targetKind{TargetSelectorKind::Unspecified};
        std::vector<ActivatedControl> activatedControls;

        MutableState mutableState;
        DispatchState dispatch;
        SchedulerState scheduler;
        OutputState output;
        DiagnosticBuffer diagnostics;
        MetricCounters metrics;

        std::atomic<std::uint64_t>& generation;
        std::atomic<bool> accepting{true};
        std::atomic<bool> targetEligible{true};
        std::atomic<bool> exitRequested{false};
        std::atomic<bool> fatalShutdownRequested{false};
    };

    Impl(
        RuntimeCapacities runtimeCapacities,
        RuntimeControlPort& runtimeControlPort,
        RuntimeOutputPort& runtimeOutputPort,
        RuntimeRoutePort& runtimeRoutePort,
        RuntimeProcessLauncher& runtimeProcessLauncher,
        RuntimeClock& runtimeClock,
        RuntimeDebugEventPort* runtimeDebugPort)
        : capacities(runtimeCapacities),
          controlPort(runtimeControlPort),
          outputPort(runtimeOutputPort),
          routePort(runtimeRoutePort),
          processLauncher(runtimeProcessLauncher),
          clock(runtimeClock),
          debugPort(runtimeDebugPort)
    {
    }

    ~Impl()
    {
        StopTaskThread();
        Deactivate();
    }

    RuntimeCapacities capacities;
    RuntimeControlPort& controlPort;
    RuntimeOutputPort& outputPort;
    RuntimeRoutePort& routePort;
    RuntimeProcessLauncher& processLauncher;
    RuntimeClock& clock;
    RuntimeDebugEventPort* debugPort;
    std::unique_ptr<State> active;
    std::atomic<std::uint64_t> observableGeneration{0U};
    std::uint64_t nextProgramSerial{1U};

    std::thread taskThread;
    std::atomic<bool> taskThreadStop{false};
    std::atomic<bool> taskThreadRunning{false};
    std::counting_semaphore<> wakeSemaphore{0};

    [[nodiscard]] RuntimeActivationResult Activate(
        std::shared_ptr<const CompiledProgram> program,
        TargetSelectorKind targetKindOverride);
    void Deactivate() noexcept;
    [[nodiscard]] InputDecision HandleInput(
        const RuntimeInputEvent& event) noexcept;
    [[nodiscard]] bool SeedPhysicalState(
        ControlRefId control,
        bool down) noexcept;
    [[nodiscard]] bool MarkPhysicalStateUnsynchronized(
        ControlRefId control) noexcept;
    void SetTargetEligible(bool eligible) noexcept;
    void TransitionTargetEligibility(State& state, bool eligible) noexcept;
    [[nodiscard]] RuntimePumpResult Pump(std::size_t maximumSlices) noexcept;
    [[nodiscard]] bool StartTaskThread();
    void StopTaskThread() noexcept;
    void TaskThreadMain() noexcept;
    void Wake() noexcept;

    [[nodiscard]] RuntimeActivationError ValidateCapacities(
        const CompiledProgram& program) const noexcept;
    [[nodiscard]] RuntimeEvaluationResult Evaluate(
        State& state,
        ExpressionId expression,
        RuntimeExpressionScratch& scratch) noexcept;
    [[nodiscard]] bool EvaluatePredicate(
        State& state,
        ExpressionId expression,
        SourceSpan source,
        bool& matched) noexcept;

    [[nodiscard]] const ExitControlBucket* FindExitBucket(
        const State& state,
        EventKey key) const noexcept;
    [[nodiscard]] const PauseControlBucket* FindPauseBucket(
        const State& state,
        EventKey key) const noexcept;
    [[nodiscard]] const EventBucket* FindEventBucket(
        const State& state,
        EventKey key) const noexcept;
    [[nodiscard]] std::uint32_t FindMappingSlot(
        const State& state,
        ControlRefId source) const noexcept;
    [[nodiscard]] InputDecision DispatchOrdinary(
        State& state,
        EventKey key,
        std::uint64_t transactionGeneration,
        std::uint64_t debugCaptureEpoch,
        std::uint64_t debugInputSequence) noexcept;
    [[nodiscard]] bool ReserveTasks(
        State& state,
        std::size_t scratchCount,
        std::size_t& reservedCount) noexcept;
    void ReleaseReservedTasks(State& state, std::size_t reservedCount) noexcept;

    void Invalidate(State& state, RuntimeCancellationReason reason) noexcept;
    void RequestFatal(
        State& state,
        RuntimeDiagnosticKind kind,
        SourceSpan source,
        std::uint32_t subject,
        std::uint32_t position,
        std::uint32_t detail) noexcept;
    void PublishDiagnostic(
        State& state,
        RuntimeDiagnosticKind kind,
        SourceSpan source = {},
        std::uint32_t subject = kInvalidProgramIndex,
        std::uint32_t position = kInvalidProgramIndex,
        std::int64_t deadline = 0,
        std::uint32_t detail = 0U,
        std::uint32_t platformError = 0U) noexcept;

    void CleanupStale(State& state) noexcept;
    void CleanupAfterInvalidation(State& state) noexcept;
    void DrainWork(State& state) noexcept;
    void PromoteTimed(State& state) noexcept;
    [[nodiscard]] std::uint32_t SelectReadyTask(State& state) noexcept;
    [[nodiscard]] bool RunTaskSlice(State& state, std::uint32_t slot) noexcept;
    void FinishTask(
        State& state,
        std::uint32_t slot,
        bool cancelled,
        RuntimeExecutionResult result) noexcept;
    [[nodiscard]] RuntimeExecutionResult ClassifyTaskOperationFailure(
        const State& state,
        const TaskInstance& task) const noexcept;
    void ScheduleTimed(
        State& state,
        TaskInstance& task,
        DurationValue duration) noexcept;

    [[nodiscard]] bool AcquireTaskControl(
        State& state,
        TaskInstance& task,
        ControlRefId control) noexcept;
    [[nodiscard]] bool ReleaseTaskControl(
        State& state,
        TaskInstance& task,
        ControlRefId control) noexcept;
    [[nodiscard]] bool ReleaseTaskOwnership(
        State& state,
        TaskInstance& task) noexcept;
    [[nodiscard]] bool AcquireGlobal(
        State& state,
        ControlRefId control,
        std::uint64_t producerGeneration,
        TaskInstance* producer = nullptr,
        bool* rateExceeded = nullptr) noexcept;
    [[nodiscard]] bool ReleaseGlobal(
        State& state,
        ControlRefId control,
        std::uint64_t count) noexcept;
    [[nodiscard]] bool PublishOutput(
        State& state,
        ControlRefId control,
        RuntimeOutputTransition transition,
        std::uint64_t producerGeneration,
        TaskInstance* producer = nullptr,
        bool* rateExceeded = nullptr) noexcept;
    void ProcessMappingWork(State& state, const WorkItem& item) noexcept;
    void CleanupMappingOwners(State& state) noexcept;

    [[nodiscard]] bool ExecuteSet(
        State& state,
        TaskInstance& task,
        const ActionInstruction& instruction,
        std::uint32_t instructionPosition) noexcept;
    [[nodiscard]] bool ExecuteToggle(
        State& state,
        const ActionInstruction& instruction,
        std::uint32_t instructionPosition) noexcept;
    [[nodiscard]] RuntimeEvaluationResult EvaluateTaskExpression(
        State& state,
        ExpressionId expression) noexcept;
    [[nodiscard]] SourceSpan ActionSource(
        const State& state,
        const ActionProgramDescriptor& descriptor,
        std::uint32_t position) const noexcept;
    [[nodiscard]] std::int64_t NextDeadline(const State& state) const noexcept;
};

RuntimeActivationError ProgramRuntime::Impl::ValidateCapacities(
    const CompiledProgram& program) const noexcept
{
    const ProgramRequirements& required = program.Requirements();
    const auto capacityError = [](RuntimeActivationErrorCode code,
                                  std::uint64_t requirement,
                                  std::uint64_t available,
                                  RuntimeActivationSubject subject =
                                      RuntimeActivationSubject::None) noexcept {
        return RuntimeActivationError{
            code,
            static_cast<std::uint32_t>(subject),
            requirement,
            available};
    };
    if (program.Controls().size() > capacities.maximumControls) {
        return capacityError(
            RuntimeActivationErrorCode::ControlCapacity,
            program.Controls().size(),
            capacities.maximumControls);
    }
    if (required.stateSlotCount > capacities.maximumStateSlots) {
        return capacityError(
            RuntimeActivationErrorCode::ValueCapacity,
            required.stateSlotCount,
            capacities.maximumStateSlots,
            RuntimeActivationSubject::StateSlots);
    }
    if (required.numberSlotCount > capacities.maximumNumberSlots) {
        return capacityError(
            RuntimeActivationErrorCode::ValueCapacity,
            required.numberSlotCount,
            capacities.maximumNumberSlots,
            RuntimeActivationSubject::NumberSlots);
    }
    if (required.durationSlotCount > capacities.maximumDurationSlots) {
        return capacityError(
            RuntimeActivationErrorCode::ValueCapacity,
            required.durationSlotCount,
            capacities.maximumDurationSlots,
            RuntimeActivationSubject::DurationSlots);
    }
    if (required.mappingSlotCount > capacities.maximumMappingSlots) {
        return capacityError(
            RuntimeActivationErrorCode::MappingCapacity,
            required.mappingSlotCount,
            capacities.maximumMappingSlots);
    }
    if (required.maximumExitRulesPerEvent
        > capacities.maximumExitRulesPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::ExitRuleCapacity,
            required.maximumExitRulesPerEvent,
            capacities.maximumExitRulesPerEvent);
    }
    if (required.maximumPauseRulesPerEvent
        > capacities.maximumPauseRulesPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::PauseRuleCapacity,
            required.maximumPauseRulesPerEvent,
            capacities.maximumPauseRulesPerEvent);
    }
    if (required.maximumRulesPerEvent > capacities.maximumRulesPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::RuleCapacity,
            required.maximumRulesPerEvent,
            capacities.maximumRulesPerEvent);
    }
    if (required.maximumPredicateStepsPerEvent
        > capacities.maximumPredicateStepsPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::PredicateStepCapacity,
            required.maximumPredicateStepsPerEvent,
            capacities.maximumPredicateStepsPerEvent);
    }
    if (required.maximumMappingOperationsPerEvent
        > capacities.maximumMappingOperationsPerEvent) {
        return capacityError(
            RuntimeActivationErrorCode::MappingOperationCapacity,
            required.maximumMappingOperationsPerEvent,
            capacities.maximumMappingOperationsPerEvent);
    }
    if (required.maximumExpressionStackDepth
        > capacities.maximumExpressionStackDepth) {
        return capacityError(
            RuntimeActivationErrorCode::ExpressionStackCapacity,
            required.maximumExpressionStackDepth,
            capacities.maximumExpressionStackDepth);
    }
    if (required.maximumRepeatFramesPerTask
        > capacities.maximumRepeatFramesPerTask) {
        return capacityError(
            RuntimeActivationErrorCode::RepeatFrameCapacity,
            required.maximumRepeatFramesPerTask,
            capacities.maximumRepeatFramesPerTask);
    }
    if (required.maximumOwnedControlsPerTask
        > capacities.maximumOwnedControlsPerTask) {
        return capacityError(
            RuntimeActivationErrorCode::OwnershipCapacity,
            required.maximumOwnedControlsPerTask,
            capacities.maximumOwnedControlsPerTask);
    }
    if (capacities.maximumTaskInstructionsWithoutSuspension == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::TaskInstructionCapacity,
            1U,
            0U);
    }
    if (capacities.maximumTaskOutputsWithoutSuspension == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::TaskOutputCapacity,
            1U,
            0U);
    }
    if (capacities.maximumContinuouslyReadyQuanta == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidSchedulerConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::MaximumContinuouslyReadyQuanta);
    }
    if (capacities.continuouslyReadyBackoffNanoseconds <= 0) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidSchedulerConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::ContinuouslyReadyBackoffNanoseconds);
    }
    if (capacities.maximumOutputTransitionsPerInterval == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidOutputRateConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::MaximumOutputTransitionsPerInterval);
    }
    if (capacities.outputRateIntervalNanoseconds <= 0) {
        return capacityError(
            RuntimeActivationErrorCode::InvalidOutputRateConfiguration,
            1U,
            0U,
            RuntimeActivationSubject::OutputRateIntervalNanoseconds);
    }
    if (required.maximumTasksPerEvent > capacities.taskSlotCount) {
        return capacityError(
            RuntimeActivationErrorCode::TaskCapacity,
            required.maximumTasksPerEvent,
            capacities.taskSlotCount);
    }
    if (required.maximumTransactionItemsPerEvent
        > capacities.transactionQueueItemCount
        || capacities.transactionQueueItemCount == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::TransactionCapacity,
            required.maximumTransactionItemsPerEvent,
            capacities.transactionQueueItemCount);
    }
    if (capacities.diagnosticRecordCount == 0U) {
        return capacityError(
            RuntimeActivationErrorCode::DiagnosticCapacity,
            1U,
            0U);
    }
    if (required.requiresProcessLaunch
        && (!capacities.permitProcessLaunch || !processLauncher.Permitted())) {
        return {RuntimeActivationErrorCode::ProcessLaunchDenied, 0U, 1U, 0U};
    }
    return {};
}

RuntimeActivationResult ProgramRuntime::Impl::Activate(
    std::shared_ptr<const CompiledProgram> program,
    TargetSelectorKind targetKindOverride)
{
    if (!program) {
        return {false, {RuntimeActivationErrorCode::MissingProgram}};
    }
    const RuntimeActivationError capacityError = ValidateCapacities(*program);
    if (capacityError.code != RuntimeActivationErrorCode::None) {
        return {false, capacityError};
    }

    std::unique_ptr<State> candidate;
    try {
        candidate = std::make_unique<State>(
            std::move(program),
            nextProgramSerial,
            observableGeneration,
            capacities);
    } catch (const std::bad_alloc&) {
        return {false, {RuntimeActivationErrorCode::AllocationFailure}};
    }

    const ProgramSettings& settings = candidate->program->Settings();
    candidate->targetKind = targetKindOverride == TargetSelectorKind::Unspecified
        ? settings.target.kind
        : targetKindOverride;
    if (!routePort.ValidateTarget(candidate->targetKind)) {
        return {false, {RuntimeActivationErrorCode::InvalidTarget}};
    }

    controlPort.BeginActivation();
    for (const ControlRequirement& requirement
         : candidate->program->ControlRequirements()) {
        ActivatedControl activated{};
        const RuntimeControlBindResult result = controlPort.BindControl(
            requirement.control,
            candidate->program->Controls()[requirement.control.value],
            requirement.uses,
            activated);
        if (result != RuntimeControlBindResult::Bound) {
            controlPort.AbortActivation();
            return {
                false,
                {result == RuntimeControlBindResult::UnsupportedIdentity
                     ? RuntimeActivationErrorCode::UnsupportedControl
                     : RuntimeActivationErrorCode::MissingControlCapability,
                 requirement.control.value,
                 requirement.uses,
                 activated.capabilities}};
        }
        if ((activated.capabilities & requirement.uses) != requirement.uses) {
            controlPort.AbortActivation();
            return {
                false,
                {RuntimeActivationErrorCode::MissingControlCapability,
                 requirement.control.value,
                 requirement.uses,
                 activated.capabilities}};
        }
        candidate->activatedControls[requirement.control.value] = activated;
    }
    const bool restartWorker = taskThreadRunning.load(std::memory_order_acquire);
    StopTaskThread();
    if (active) {
        Invalidate(*active, RuntimeCancellationReason::Reload);
        CleanupAfterInvalidation(*active);
        if (active->output.HasOwnedOutputs()) {
            controlPort.AbortActivation();
            if (restartWorker) {
                (void)StartTaskThread();
            }
            return {false, {RuntimeActivationErrorCode::CleanupFailure}};
        }
    }
    controlPort.CommitActivation();
    if (active == nullptr
        && observableGeneration.load(std::memory_order_acquire) == 0U) {
        observableGeneration.store(1U, std::memory_order_release);
    }
    active = std::move(candidate);
    ++nextProgramSerial;
    if (restartWorker) {
        (void)StartTaskThread();
    }
    return {true, {}};
}

void ProgramRuntime::Impl::Deactivate() noexcept
{
    StopTaskThread();
    if (!active) {
        return;
    }
    Invalidate(*active, RuntimeCancellationReason::Shutdown);
    CleanupAfterInvalidation(*active);
    observableGeneration.store(0U, std::memory_order_release);
    active.reset();
}

RuntimeEvaluationResult ProgramRuntime::Impl::Evaluate(
    State& state,
    ExpressionId expression,
    RuntimeExpressionScratch& scratch) noexcept
{
    return EvaluateRuntimeExpression(
        *state.program,
        expression,
        state.mutableState.ExpressionState(*state.program),
        scratch);
}

bool ProgramRuntime::Impl::EvaluatePredicate(
    State& state,
    ExpressionId expression,
    SourceSpan source,
    bool& matched) noexcept
{
    if (!expression.IsValid()) {
        matched = true;
        return true;
    }
    const RuntimeEvaluationResult result = Evaluate(
        state,
        expression,
        state.dispatch.expressionScratch);
    if (!result.Succeeded() || result.value.type != ExpressionType::Boolean) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::PredicateFault,
            source,
            expression.value,
            result.instructionPosition,
            static_cast<std::uint32_t>(result.fault));
        matched = false;
        return false;
    }
    matched = result.value.booleanValue;
    return true;
}

const ExitControlBucket* ProgramRuntime::Impl::FindExitBucket(
    const State& state,
    EventKey key) const noexcept
{
    const auto buckets = state.program->ExitControlBuckets();
    const auto found = std::lower_bound(
        buckets.begin(),
        buckets.end(),
        key,
        [](const ExitControlBucket& bucket, EventKey candidate) noexcept {
            return bucket.key < candidate;
        });
    return found != buckets.end() && found->key == key
        ? &*found
        : nullptr;
}

const PauseControlBucket* ProgramRuntime::Impl::FindPauseBucket(
    const State& state,
    EventKey key) const noexcept
{
    const auto buckets = state.program->PauseControlBuckets();
    const auto found = std::lower_bound(
        buckets.begin(),
        buckets.end(),
        key,
        [](const PauseControlBucket& bucket, EventKey candidate) noexcept {
            return bucket.key < candidate;
        });
    return found != buckets.end() && found->key == key
        ? &*found
        : nullptr;
}

const EventBucket* ProgramRuntime::Impl::FindEventBucket(
    const State& state,
    EventKey key) const noexcept
{
    const auto buckets = state.program->EventBuckets();
    const auto found = std::lower_bound(
        buckets.begin(),
        buckets.end(),
        key,
        [](const EventBucket& bucket, EventKey candidate) noexcept {
            return bucket.key < candidate;
        });
    return found != buckets.end() && found->key == key
        ? &*found
        : nullptr;
}

std::uint32_t ProgramRuntime::Impl::FindMappingSlot(
    const State& state,
    ControlRefId source) const noexcept
{
    const auto slots = state.program->MappingSlots();
    const auto found = std::lower_bound(
        slots.begin(),
        slots.end(),
        source,
        [](const MappingSlotDescriptor& slot, ControlRefId candidate) noexcept {
            return slot.source < candidate;
        });
    return found != slots.end() && found->source == source
        ? static_cast<std::uint32_t>(found - slots.begin())
        : kInvalidProgramIndex;
}

void ProgramRuntime::Impl::PublishDiagnostic(
    State& state,
    RuntimeDiagnosticKind kind,
    SourceSpan source,
    std::uint32_t subject,
    std::uint32_t position,
    std::int64_t deadline,
    std::uint32_t detail,
    std::uint32_t platformError) noexcept
{
    const RuntimeDiagnosticRecord record{
        kind,
        state.programSerial,
        state.generation.load(std::memory_order_acquire),
        state.output.nextSequence.load(std::memory_order_relaxed),
        source,
        subject,
        position,
        deadline,
        detail,
        platformError};
    state.diagnostics.TryPush(record);
    if (debugPort != nullptr && IsDebugIssue(kind)) {
        RuntimeDebugEvent event{};
        event.kind = RuntimeDebugEventKind::RuntimeIssue;
        event.issue = {
            record.kind,
            record.source,
            record.subject,
            record.position,
            record.deadlineNanoseconds,
            record.detail,
            record.platformError};
        (void)debugPort->Publish(event);
    }
}

void ProgramRuntime::Impl::Invalidate(
    State& state,
    RuntimeCancellationReason reason) noexcept
{
    std::uint64_t generation = state.generation.load(std::memory_order_relaxed);
    while (generation != (std::numeric_limits<std::uint64_t>::max)()
        && !state.generation.compare_exchange_weak(
            generation,
            generation + 1U,
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
    }
    if (generation == (std::numeric_limits<std::uint64_t>::max)()) {
        state.fatalShutdownRequested.store(true, std::memory_order_release);
        state.accepting.store(false, std::memory_order_release);
    }
    if (reason != RuntimeCancellationReason::Pause
        && reason != RuntimeCancellationReason::TargetIneligible) {
        state.accepting.store(false, std::memory_order_release);
    }
    for (std::size_t index = 0U;
         index < state.program->MappingSlots().size();
         ++index) {
        state.dispatch.activeMappings[index].store(
            kInvalidProgramIndex,
            std::memory_order_release);
    }
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::Cancellation,
        {},
        static_cast<std::uint32_t>(reason));
    Wake();
}

void ProgramRuntime::Impl::RequestFatal(
    State& state,
    RuntimeDiagnosticKind kind,
    SourceSpan source,
    std::uint32_t subject,
    std::uint32_t position,
    std::uint32_t detail) noexcept
{
    PublishDiagnostic(
        state,
        kind,
        source,
        subject,
        position,
        0,
        detail);
    if (!state.fatalShutdownRequested.exchange(true, std::memory_order_acq_rel)) {
        Invalidate(state, RuntimeCancellationReason::FatalFailure);
    }
}

InputDecision ProgramRuntime::Impl::HandleInput(
    const RuntimeInputEvent& event) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || event.origin != InputOrigin::PhysicalCandidate) {
        return InputDecision::Forward;
    }
    if (!event.control.IsValid()
        || event.control.value >= state->program->Controls().size()) {
        return InputDecision::Forward;
    }
    EventTransition transition{};
    std::atomic<std::uint8_t>& held =
        state->mutableState.physicalHeld[event.control.value];
    std::atomic<std::uint8_t>& synchronized =
        state->mutableState.physicalSynchronized[event.control.value];
    const bool wasSynchronized = synchronized.load(
        std::memory_order_acquire) != 0U;
    if (event.transition == Transition::Down) {
        const bool wasHeld = held.exchange(1U, std::memory_order_acq_rel) != 0U;
        transition = event.device == DeviceKind::Keyboard && wasHeld
            ? EventTransition::Repeat
            : EventTransition::Down;
    } else if (event.transition == Transition::Up) {
        held.store(0U, std::memory_order_release);
        if (!wasSynchronized) {
            synchronized.store(1U, std::memory_order_release);
            PublishDiagnostic(
                *state,
                RuntimeDiagnosticKind::PhysicalStateSynchronization,
                {},
                event.control.value,
                0U,
                0,
                1U);
        }
        transition = EventTransition::Up;
    } else {
        return InputDecision::Forward;
    }

    const EventKey key{event.control, transition};
    const ExitControlBucket* const exitBucket = FindExitBucket(*state, key);
    if (exitBucket != nullptr
        && !state->exitRequested.load(std::memory_order_acquire)) {
        std::shared_lock pauseLock(
            state->mutableState.pauseMutex,
            std::defer_lock);
        if (!state->program->PauseControlBuckets().empty()
            && !pauseLock.try_lock()) {
            return InputDecision::Forward;
        }
        std::shared_lock variableLock(
            state->mutableState.variableMutex,
            std::try_to_lock);
        if (!variableLock.owns_lock()) {
            return InputDecision::Forward;
        }
        const auto rules = state->program->ExitControlRules().subspan(
            exitBucket->rules.begin,
            exitBucket->rules.count);
        for (const ExitControlRule& rule : rules) {
            bool matched = false;
            if (!EvaluatePredicate(*state, rule.condition, rule.source, matched)) {
                return InputDecision::Forward;
            }
            if (!matched) {
                continue;
            }
            state->exitRequested.store(true, std::memory_order_release);
            Invalidate(*state, RuntimeCancellationReason::Exit);
            state->metrics.dispatchedEvents.fetch_add(
                1U,
                std::memory_order_relaxed);
            state->metrics.suppressedEvents.fetch_add(
                1U,
                std::memory_order_relaxed);
            return InputDecision::Suppress;
        }
    }
    const std::uint64_t transactionGeneration = state->generation.load(
        std::memory_order_acquire);
    if (!state->accepting.load(std::memory_order_acquire)
        || !wasSynchronized
        || !state->targetEligible.load(std::memory_order_acquire)) {
        return InputDecision::Forward;
    }

    state->metrics.dispatchedEvents.fetch_add(1U, std::memory_order_relaxed);
    if (!routePort.TargetValid(state->targetKind)) {
        Invalidate(*state, RuntimeCancellationReason::TargetLoss);
        return InputDecision::Forward;
    }
    if (!routePort.CanDispatch(state->targetKind, event)) {
        if (event.device == DeviceKind::Keyboard
            && state->targetKind != TargetSelectorKind::Global) {
            TransitionTargetEligibility(*state, false);
        }
        return InputDecision::Forward;
    }

    if (state->program->PauseControlBuckets().empty()) {
        std::shared_lock variableLock(
            state->mutableState.variableMutex,
            std::try_to_lock);
        if (!variableLock.owns_lock()) {
            return InputDecision::Forward;
        }
        const InputDecision decision = DispatchOrdinary(
            *state,
            key,
            transactionGeneration,
            event.debugCaptureEpoch,
            event.debugInputSequence);
        if (decision == InputDecision::Suppress) {
            state->metrics.suppressedEvents.fetch_add(
                1U,
                std::memory_order_relaxed);
        }
        return decision;
    }

    const PauseControlBucket* const pauseBucket = FindPauseBucket(*state, key);
    if (pauseBucket != nullptr) {
        std::unique_lock pauseLock(
            state->mutableState.pauseMutex,
            std::try_to_lock);
        if (!pauseLock.owns_lock()) {
            return InputDecision::Forward;
        }
        std::shared_lock variableLock(
            state->mutableState.variableMutex,
            std::try_to_lock);
        if (!variableLock.owns_lock()) {
            return InputDecision::Forward;
        }
        const auto rules = state->program->PauseControlRules().subspan(
            pauseBucket->rules.begin,
            pauseBucket->rules.count);
        for (const PauseControlRule& rule : rules) {
            bool matched = false;
            if (!EvaluatePredicate(*state, rule.condition, rule.source, matched)) {
                return InputDecision::Forward;
            }
            if (!matched) {
                continue;
            }
            if (state->generation.load(std::memory_order_acquire)
                    != transactionGeneration
                || !state->accepting.load(std::memory_order_acquire)
                || !state->targetEligible.load(std::memory_order_acquire)) {
                return InputDecision::Forward;
            }
            const bool before = state->mutableState.pauseOn;
            if (rule.effect == PauseEffect::On) {
                state->mutableState.pauseOn = true;
            } else if (rule.effect == PauseEffect::Off) {
                state->mutableState.pauseOn = false;
            } else {
                state->mutableState.pauseOn = !state->mutableState.pauseOn;
            }
            if (before != state->mutableState.pauseOn) {
                Invalidate(*state, RuntimeCancellationReason::Pause);
            }
            if (rule.delivery == Delivery::Consume) {
                state->metrics.suppressedEvents.fetch_add(
                    1U,
                    std::memory_order_relaxed);
                return InputDecision::Suppress;
            }
            return InputDecision::Forward;
        }
        if (!state->mutableState.pauseOn) {
            return InputDecision::Forward;
        }
        const InputDecision decision = DispatchOrdinary(
            *state,
            key,
            transactionGeneration,
            event.debugCaptureEpoch,
            event.debugInputSequence);
        if (decision == InputDecision::Suppress) {
            state->metrics.suppressedEvents.fetch_add(
                1U,
                std::memory_order_relaxed);
        }
        return decision;
    }

    std::shared_lock pauseLock(
        state->mutableState.pauseMutex,
        std::try_to_lock);
    if (!pauseLock.owns_lock()) {
        return InputDecision::Forward;
    }
    std::shared_lock variableLock(
        state->mutableState.variableMutex,
        std::try_to_lock);
    if (!variableLock.owns_lock()) {
        return InputDecision::Forward;
    }
    if (!state->mutableState.pauseOn) {
        return InputDecision::Forward;
    }
    const InputDecision decision = DispatchOrdinary(
        *state,
        key,
        transactionGeneration,
        event.debugCaptureEpoch,
        event.debugInputSequence);
    if (decision == InputDecision::Suppress) {
        state->metrics.suppressedEvents.fetch_add(
            1U,
            std::memory_order_relaxed);
    }
    return decision;
}

bool ProgramRuntime::Impl::SeedPhysicalState(
    ControlRefId control,
    bool down) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || !control.IsValid()
        || control.value >= state->program->Controls().size()) {
        return false;
    }
    state->mutableState.physicalHeld[control.value].store(
        down ? 1U : 0U,
        std::memory_order_release);
    state->mutableState.physicalSynchronized[control.value].store(
        1U,
        std::memory_order_release);
    return true;
}

bool ProgramRuntime::Impl::MarkPhysicalStateUnsynchronized(
    ControlRefId control) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || !control.IsValid()
        || control.value >= state->program->Controls().size()) {
        return false;
    }
    state->mutableState.physicalHeld[control.value].store(
        0U,
        std::memory_order_release);
    state->mutableState.physicalSynchronized[control.value].store(
        0U,
        std::memory_order_release);
    PublishDiagnostic(
        *state,
        RuntimeDiagnosticKind::PhysicalStateSynchronization,
        {},
        control.value,
        0U,
        0,
        0U);
    return true;
}

void ProgramRuntime::Impl::SetTargetEligible(bool eligible) noexcept
{
    State* const state = active.get();
    if (state == nullptr) {
        return;
    }
    TransitionTargetEligibility(*state, eligible);
}

void ProgramRuntime::Impl::TransitionTargetEligibility(
    State& state,
    bool eligible) noexcept
{
    if (state.targetKind == TargetSelectorKind::Global) {
        return;
    }
    if (eligible && !state.accepting.load(std::memory_order_acquire)) {
        return;
    }
    const bool previous = state.targetEligible.exchange(
        eligible,
        std::memory_order_acq_rel);
    if (previous == eligible) {
        return;
    }
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::TargetEligibilityChange,
        {},
        kInvalidProgramIndex,
        kInvalidProgramIndex,
        0,
        eligible ? 1U : 0U);
    if (!eligible && state.accepting.load(std::memory_order_acquire)) {
        Invalidate(state, RuntimeCancellationReason::TargetIneligible);
    }
    Wake();
}

InputDecision ProgramRuntime::Impl::DispatchOrdinary(
    State& state,
    EventKey key,
    std::uint64_t transactionGeneration,
    std::uint64_t debugCaptureEpoch,
    std::uint64_t debugInputSequence) noexcept
{
    const auto transactionCurrent = [&state, transactionGeneration]() noexcept {
        return state.generation.load(std::memory_order_acquire)
                == transactionGeneration
            && state.accepting.load(std::memory_order_acquire)
            && state.targetEligible.load(std::memory_order_acquire);
    };
    if (!transactionCurrent()) {
        return InputDecision::Forward;
    }
    std::size_t scratchCount = 0U;
    bool consumed = false;
    std::uint32_t mappingActivationSlot = kInvalidProgramIndex;
    std::uint32_t mappingActivationId = kInvalidProgramIndex;
    std::uint32_t mappingClearSlot = kInvalidProgramIndex;

    const std::uint32_t slot = FindMappingSlot(state, key.control);
    if (slot != kInvalidProgramIndex
        && (key.transition == EventTransition::Repeat
            || key.transition == EventTransition::Up)) {
        const std::uint32_t mapping = state.dispatch.activeMappings[slot].load(
            std::memory_order_acquire);
        if (mapping != kInvalidProgramIndex) {
            state.dispatch.transactionScratch[scratchCount++] = {
                key.transition == EventTransition::Repeat
                    ? WorkKind::MappingRepeat
                    : WorkKind::MappingRelease,
                transactionGeneration,
                kInvalidTaskSlot,
                {},
                MappingId{mapping},
                MappingSlotId{slot},
                state.program->Mappings()[mapping].target};
            consumed = true;
            if (key.transition == EventTransition::Up) {
                mappingClearSlot = slot;
            }
        }
    }

    const EventBucket* const bucket = FindEventBucket(state, key);
    if (bucket != nullptr) {
        const auto rules = state.program->Rules().subspan(
            bucket->rules.begin,
            bucket->rules.count);
        for (std::size_t ruleOffset = 0U; ruleOffset < rules.size(); ++ruleOffset) {
            const CompiledRule& rule = rules[ruleOffset];
            bool matched = false;
            if (!EvaluatePredicate(state, rule.condition, rule.source, matched)) {
                return InputDecision::Forward;
            }
            if (!matched) {
                continue;
            }
            if (rule.kind == RuleKind::MappingDown) {
                const MappingDescriptor& mapping =
                    state.program->Mappings()[rule.mapping.value];
                const std::uint32_t mappingSlot = mapping.slot.value;
                if (state.dispatch.activeMappings[mappingSlot].load(
                        std::memory_order_acquire) == kInvalidProgramIndex) {
                    state.dispatch.transactionScratch[scratchCount++] = {
                        WorkKind::MappingAcquire,
                        transactionGeneration,
                        kInvalidTaskSlot,
                        {},
                        rule.mapping,
                        mapping.slot,
                        mapping.target};
                    mappingActivationSlot = mappingSlot;
                    mappingActivationId = rule.mapping.value;
                }
            } else if (rule.action.IsValid()) {
                WorkItem& item = state.dispatch.transactionScratch[scratchCount++];
                item = {
                    WorkKind::TaskStart,
                    transactionGeneration,
                    kInvalidTaskSlot,
                    rule.action};
                item.debugCaptureEpoch = debugCaptureEpoch;
                item.debugInputSequence = debugInputSequence;
                item.debugEventKey = key;
                item.debugRuleIndex = bucket->rules.begin
                    + static_cast<std::uint32_t>(ruleOffset);
            }
            consumed = consumed || rule.delivery == Delivery::Consume;
            if (rule.flow == MatchFlow::Stop) {
                break;
            }
        }
    }

    if (scratchCount > state.dispatch.workQueue.Available()) {
        state.metrics.transactionRejections.fetch_add(
            1U,
            std::memory_order_relaxed);
        PublishDiagnostic(state, RuntimeDiagnosticKind::TransactionCapacity);
        return InputDecision::Forward;
    }

    std::size_t reservedCount = 0U;
    if (!ReserveTasks(state, scratchCount, reservedCount)) {
        state.metrics.transactionRejections.fetch_add(
            1U,
            std::memory_order_relaxed);
        PublishDiagnostic(state, RuntimeDiagnosticKind::TransactionCapacity);
        return InputDecision::Forward;
    }
    if (!transactionCurrent()) {
        ReleaseReservedTasks(state, reservedCount);
        return InputDecision::Forward;
    }
    if (scratchCount != 0U
        && !state.dispatch.workQueue.TryPush(
            {state.dispatch.transactionScratch.data(), scratchCount})) {
        ReleaseReservedTasks(state, reservedCount);
        state.metrics.transactionRejections.fetch_add(
            1U,
            std::memory_order_relaxed);
        PublishDiagnostic(state, RuntimeDiagnosticKind::TransactionCapacity);
        return InputDecision::Forward;
    }

    if (mappingActivationSlot != kInvalidProgramIndex) {
        state.dispatch.activeMappings[mappingActivationSlot].store(
            mappingActivationId,
            std::memory_order_release);
        PublishDiagnostic(
            state,
            RuntimeDiagnosticKind::MappingChange,
            {},
            mappingActivationId,
            0U,
            0,
            1U);
    }
    if (mappingClearSlot != kInvalidProgramIndex) {
        state.dispatch.activeMappings[mappingClearSlot].store(
            kInvalidProgramIndex,
            std::memory_order_release);
        PublishDiagnostic(
            state,
            RuntimeDiagnosticKind::MappingChange,
            {},
            mappingClearSlot,
            0U,
            0,
            0U);
    }
    if (scratchCount != 0U) {
        Wake();
    }
    if (!transactionCurrent()) {
        if (mappingActivationSlot != kInvalidProgramIndex) {
            std::uint32_t expected = mappingActivationId;
            (void)state.dispatch.activeMappings[mappingActivationSlot]
                .compare_exchange_strong(
                    expected,
                    kInvalidProgramIndex,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
        }
        return InputDecision::Forward;
    }
    return consumed ? InputDecision::Suppress : InputDecision::Forward;
}

bool ProgramRuntime::Impl::ReserveTasks(
    State& state,
    std::size_t scratchCount,
    std::size_t& reservedCount) noexcept
{
    reservedCount = 0U;
    for (std::size_t itemIndex = 0U;
         itemIndex < scratchCount;
         ++itemIndex) {
        WorkItem& item = state.dispatch.transactionScratch[itemIndex];
        if (item.kind != WorkKind::TaskStart) {
            continue;
        }
        bool reserved = false;
        for (std::size_t slot = 0U; slot < state.scheduler.taskCount; ++slot) {
            TaskStatus expected = TaskStatus::Free;
            if (!state.scheduler.tasks[slot].status.compare_exchange_strong(
                    expected,
                    TaskStatus::Initializing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                continue;
            }
            TaskInstance& task = state.scheduler.tasks[slot];
            task.action = item.action;
            task.position = 0U;
            task.generation = item.generation;
            task.readyOrder = 0U;
            task.timedOrder = 0U;
            task.deadlineNanoseconds = 0;
            task.resumeKind = TaskResumeKind::None;
            task.pendingTap = {};
            task.instructionsWithoutSuspension = 0U;
            task.outputsWithoutSuspension = 0U;
            task.debugCaptureEpoch = 0U;
            task.debugExecutionMarker = 0U;
            task.cleanupResult = RuntimeExecutionResult::Completed;
            task.cleanupCancelled = false;
            task.safetyCancelled = false;
            std::fill(task.repeatFrames.begin(), task.repeatFrames.end(), RepeatFrame{});
            std::fill(
                task.ownership.begin(),
                task.ownership.end(),
                TaskOwnershipRecord{});
            task.status.store(TaskStatus::Reserved, std::memory_order_release);
            item.taskSlot = static_cast<std::uint32_t>(slot);
            state.dispatch.reservedTaskScratch[reservedCount++] = item.taskSlot;
            reserved = true;
            break;
        }
        if (!reserved) {
            ReleaseReservedTasks(state, reservedCount);
            reservedCount = 0U;
            return false;
        }
    }
    return true;
}

void ProgramRuntime::Impl::ReleaseReservedTasks(
    State& state,
    std::size_t reservedCount) noexcept
{
    for (std::size_t index = 0U; index < reservedCount; ++index) {
        state.scheduler.tasks[state.dispatch.reservedTaskScratch[index]].status.store(
            TaskStatus::Free,
            std::memory_order_release);
    }
}

bool ProgramRuntime::Impl::PublishOutput(
    State& state,
    ControlRefId control,
    RuntimeOutputTransition transition,
    std::uint64_t producerGeneration,
    TaskInstance* producer,
    bool* rateExceeded) noexcept
{
    if (rateExceeded != nullptr) {
        *rateExceeded = false;
    }
    const std::uint64_t currentGeneration = state.generation.load(
        std::memory_order_acquire);
    if (transition != RuntimeOutputTransition::Up
        && producerGeneration != currentGeneration) {
        return false;
    }
    if (!control.IsValid() || control.value >= state.activatedControls.size()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::OutputFailure,
            {},
            control.value,
            kInvalidProgramIndex,
            static_cast<std::uint32_t>(RuntimeOutputResult::Failed));
        return false;
    }
    const ActivatedControl& activated = state.activatedControls[control.value];
    if (transition != RuntimeOutputTransition::Up) {
        if (!state.targetEligible.load(std::memory_order_acquire)) {
            return false;
        }
        if (!routePort.CanInject(state.targetKind, activated)) {
            if (state.targetKind == TargetSelectorKind::Global) {
                Invalidate(state, RuntimeCancellationReason::TargetLoss);
            } else {
                TransitionTargetEligibility(state, false);
            }
            return false;
        }

        SourceSpan producerSource{};
        std::uint32_t producerSubject = control.value;
        std::uint32_t producerPosition = kInvalidProgramIndex;
        if (producer != nullptr
            && producer->action.IsValid()
            && producer->action.value < state.program->ActionPrograms().size()) {
            const ActionProgramDescriptor& descriptor =
                state.program->ActionPrograms()[producer->action.value];
            producerSource = ActionSource(
                state,
                descriptor,
                producer->position);
            producerSubject = producer->action.value;
            producerPosition = producer->position;
        }
        if (producer != nullptr
            && producer->outputsWithoutSuspension
                >= capacities.maximumTaskOutputsWithoutSuspension) {
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::TaskBudgetExceeded,
                producerSource,
                producerSubject,
                producerPosition,
                0,
                2U);
            producer->safetyCancelled = true;
            return false;
        }

        const std::int64_t now = (std::max)(
            clock.NowNanoseconds(),
            std::int64_t{0});
        if (now < state.output.rateWindowStartNanoseconds
            || now - state.output.rateWindowStartNanoseconds
                >= capacities.outputRateIntervalNanoseconds) {
            state.output.rateWindowStartNanoseconds = now;
            state.output.rateWindowTransitions = 0U;
        }
        if (state.output.rateWindowTransitions
            >= capacities.maximumOutputTransitionsPerInterval) {
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::OutputRateExceeded,
                producerSource,
                producerSubject,
                producerPosition,
                0,
                capacities.maximumOutputTransitionsPerInterval);
            if (rateExceeded != nullptr) {
                *rateExceeded = true;
            }
            if (producer != nullptr) {
                producer->safetyCancelled = true;
            }
            return false;
        }
    }
    const RuntimeOutputResult result = outputPort.Publish({
        transition == RuntimeOutputTransition::Up
            ? currentGeneration
            : producerGeneration,
        state.output.nextSequence.fetch_add(1U, std::memory_order_relaxed),
        control,
        state.program->Controls()[control.value],
        activated,
        transition});
    if (result == RuntimeOutputResult::Accepted) {
        if (transition != RuntimeOutputTransition::Up) {
            ++state.output.rateWindowTransitions;
            if (producer != nullptr) {
                ++producer->outputsWithoutSuspension;
            }
        }
        state.metrics.outputTransitions.fetch_add(1U, std::memory_order_relaxed);
        return true;
    }
    if (result == RuntimeOutputResult::RouteRejected) {
        if (transition == RuntimeOutputTransition::Up
            || state.targetKind == TargetSelectorKind::Global) {
            Invalidate(state, RuntimeCancellationReason::TargetLoss);
        } else {
            TransitionTargetEligibility(state, false);
        }
        return false;
    }
    RequestFatal(
        state,
        RuntimeDiagnosticKind::OutputFailure,
        {},
        control.value,
        kInvalidProgramIndex,
        static_cast<std::uint32_t>(result));
    return false;
}

bool ProgramRuntime::Impl::AcquireGlobal(
    State& state,
    ControlRefId control,
    std::uint64_t producerGeneration,
    TaskInstance* producer,
    bool* rateExceeded) noexcept
{
    const std::lock_guard lock(state.output.mutex);
    if (producerGeneration
        != state.generation.load(std::memory_order_acquire)) {
        return false;
    }
    std::uint64_t& count = state.output.globalOwnership[control.value];
    if (count == (std::numeric_limits<std::uint64_t>::max)()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            control.value,
            kInvalidProgramIndex,
            1U);
        return false;
    }
    if (count == 0U
        && !PublishOutput(
            state,
             control,
             RuntimeOutputTransition::Down,
             producerGeneration,
             producer,
             rateExceeded)) {
        return false;
    }
    ++count;
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::OwnershipChange,
        {},
        control.value,
        kInvalidProgramIndex,
        0,
        1U);
    return true;
}

bool ProgramRuntime::Impl::ReleaseGlobal(
    State& state,
    ControlRefId control,
    std::uint64_t count) noexcept
{
    const std::lock_guard lock(state.output.mutex);
    std::uint64_t& globalCount = state.output.globalOwnership[control.value];
    if (count == 0U) {
        return true;
    }
    if (globalCount < count) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            control.value,
            kInvalidProgramIndex,
            2U);
        return false;
    }
    if (globalCount == count
        && !PublishOutput(
            state,
            control,
            RuntimeOutputTransition::Up,
            state.generation.load(std::memory_order_acquire))) {
        return false;
    }
    globalCount -= count;
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::OwnershipChange,
        {},
        control.value,
        kInvalidProgramIndex,
        0,
        0U);
    return true;
}

bool ProgramRuntime::Impl::AcquireTaskControl(
    State& state,
    TaskInstance& task,
    ControlRefId control) noexcept
{
    TaskOwnershipRecord* freeRecord = nullptr;
    TaskOwnershipRecord* record = nullptr;
    for (TaskOwnershipRecord& candidate : task.ownership) {
        if (candidate.count != 0U && candidate.control == control) {
            record = &candidate;
            break;
        }
        if (candidate.count == 0U && freeRecord == nullptr) {
            freeRecord = &candidate;
        }
    }
    if (record == nullptr) {
        record = freeRecord;
    }
    if (record == nullptr
        || record->count == (std::numeric_limits<std::uint64_t>::max)()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            control.value,
            task.position,
            3U);
        return false;
    }
    if (!AcquireGlobal(state, control, task.generation, &task)) {
        return false;
    }
    if (record->count == 0U) {
        record->control = control;
    }
    ++record->count;
    return true;
}

bool ProgramRuntime::Impl::ReleaseTaskControl(
    State& state,
    TaskInstance& task,
    ControlRefId control) noexcept
{
    for (TaskOwnershipRecord& record : task.ownership) {
        if (record.count == 0U || record.control != control) {
            continue;
        }
        if (!ReleaseGlobal(state, control, 1U)) {
            return false;
        }
        --record.count;
        if (record.count == 0U) {
            record.control = {};
        }
        return true;
    }
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::TaskActionFault,
        {},
        control.value,
        task.position,
        4U);
    return false;
}

bool ProgramRuntime::Impl::ReleaseTaskOwnership(
    State& state,
    TaskInstance& task) noexcept
{
    bool succeeded = true;
    for (std::size_t index = task.ownership.size(); index != 0U; --index) {
        TaskOwnershipRecord& record = task.ownership[index - 1U];
        if (record.count == 0U) {
            continue;
        }
        if (!ReleaseGlobal(state, record.control, record.count)) {
            succeeded = false;
            continue;
        }
        record = {};
    }
    return succeeded;
}

void ProgramRuntime::Impl::ProcessMappingWork(
    State& state,
    const WorkItem& item) noexcept
{
    if (!item.mappingSlot.IsValid()
        || item.mappingSlot.value >= state.dispatch.mappingOwners.size()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            item.mappingSlot.value,
            kInvalidProgramIndex,
            5U);
        return;
    }
    MappingOwner& owner = state.dispatch.mappingOwners[item.mappingSlot.value];
    if (item.kind == WorkKind::MappingAcquire) {
        if (owner.owned) {
            if (owner.generation == item.generation) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    {},
                    item.mapping.value,
                    kInvalidProgramIndex,
                    6U);
                return;
            }
            if (!ReleaseGlobal(state, owner.target, 1U)) {
                return;
            }
            owner = {};
        }
        bool rateExceeded = false;
        if (AcquireGlobal(
                state,
                item.control,
                item.generation,
                nullptr,
                &rateExceeded)) {
            owner.owned = true;
            owner.generation = item.generation;
            owner.mapping = item.mapping;
            owner.target = item.control;
        } else if (rateExceeded) {
            state.dispatch.activeMappings[item.mappingSlot.value].store(
                kInvalidProgramIndex,
                std::memory_order_release);
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::MappingChange,
                {},
                item.mapping.value,
                0U,
                0,
                0U);
        }
        return;
    }
    if (item.kind == WorkKind::MappingRepeat) {
        if (owner.owned) {
            bool rateExceeded = false;
            const bool published = PublishOutput(
                state,
                owner.target,
                RuntimeOutputTransition::Repeat,
                item.generation,
                nullptr,
                &rateExceeded);
            if (!published && rateExceeded) {
                state.dispatch.activeMappings[item.mappingSlot.value].store(
                    kInvalidProgramIndex,
                    std::memory_order_release);
                if (ReleaseGlobal(state, owner.target, 1U)) {
                    owner = {};
                }
                PublishDiagnostic(
                    state,
                    RuntimeDiagnosticKind::MappingChange,
                    {},
                    item.mapping.value,
                    0U,
                    0,
                    0U);
            }
        }
        return;
    }
    if (item.kind == WorkKind::MappingRelease && owner.owned) {
        if (ReleaseGlobal(state, owner.target, 1U)) {
            owner = {};
        }
    }
}

void ProgramRuntime::Impl::CleanupMappingOwners(State& state) noexcept
{
    for (MappingOwner& owner : state.dispatch.mappingOwners) {
        if (!owner.owned) {
            continue;
        }
        if (ReleaseGlobal(state, owner.target, 1U)) {
            owner = {};
        }
    }
}

void ProgramRuntime::Impl::FinishTask(
    State& state,
    std::uint32_t slot,
    bool cancelled,
    RuntimeExecutionResult result) noexcept
{
    TaskInstance& task = state.scheduler.tasks[slot];
    bool requestedCancelled = task.safetyCancelled || cancelled;
    RuntimeExecutionResult requested = task.safetyCancelled
        ? RuntimeExecutionResult::Cancelled
        : result;
    if (task.status.load(std::memory_order_acquire) == TaskStatus::Cleanup) {
        requestedCancelled = requestedCancelled || task.cleanupCancelled;
        requested = MergeExecutionResult(task.cleanupResult, requested);
    }
    if (!ReleaseTaskOwnership(state, task)) {
        requested = MergeExecutionResult(
            requested,
            ClassifyTaskOperationFailure(state, task));
        task.cleanupCancelled = requestedCancelled;
        task.cleanupResult = requested;
        task.resumeKind = TaskResumeKind::None;
        task.pendingTap = {};
        task.status.store(TaskStatus::Cleanup, std::memory_order_release);
        return;
    }
    const RuntimeExecutionResult finalResult = requested;
    if (debugPort != nullptr
        && task.debugCaptureEpoch != 0U
        && task.debugExecutionMarker != 0U) {
        RuntimeDebugEvent event{};
        event.kind = RuntimeDebugEventKind::ExecutionEnded;
        event.captureEpoch = task.debugCaptureEpoch;
        event.executionMarker = task.debugExecutionMarker;
        event.result = finalResult;
        (void)debugPort->Publish(event);
    }
    task.debugCaptureEpoch = 0U;
    task.debugExecutionMarker = 0U;
    task.cleanupResult = RuntimeExecutionResult::Completed;
    task.cleanupCancelled = false;
    task.safetyCancelled = false;
    task.resumeKind = TaskResumeKind::None;
    task.pendingTap = {};
    task.instructionsWithoutSuspension = 0U;
    task.outputsWithoutSuspension = 0U;
    task.status.store(TaskStatus::Free, std::memory_order_release);
    if (requestedCancelled) {
        state.metrics.cancelledTasks.fetch_add(1U, std::memory_order_relaxed);
    } else {
        state.metrics.completedTasks.fetch_add(1U, std::memory_order_relaxed);
    }
}

RuntimeExecutionResult ProgramRuntime::Impl::ClassifyTaskOperationFailure(
    const State& state,
    const TaskInstance& task) const noexcept
{
    if (task.safetyCancelled) {
        return RuntimeExecutionResult::Cancelled;
    }
    if (state.fatalShutdownRequested.load(std::memory_order_acquire)) {
        return RuntimeExecutionResult::Failed;
    }
    return task.generation != state.generation.load(std::memory_order_acquire)
        ? RuntimeExecutionResult::Cancelled
        : RuntimeExecutionResult::Failed;
}

void ProgramRuntime::Impl::CleanupStale(State& state) noexcept
{
    const std::uint64_t generation = state.generation.load(std::memory_order_acquire);
    for (std::size_t index = 0U; index < state.scheduler.taskCount; ++index) {
        TaskInstance& task = state.scheduler.tasks[index];
        const TaskStatus status = task.status.load(std::memory_order_acquire);
        if (status == TaskStatus::Free
            || status == TaskStatus::Initializing
            || task.generation == generation) {
            continue;
        }
        FinishTask(
            state,
            static_cast<std::uint32_t>(index),
            true,
            RuntimeExecutionResult::Cancelled);
    }
    for (MappingOwner& owner : state.dispatch.mappingOwners) {
        if (!owner.owned || owner.generation == generation) {
            continue;
        }
        if (ReleaseGlobal(state, owner.target, 1U)) {
            owner = {};
        }
    }
}

void ProgramRuntime::Impl::CleanupAfterInvalidation(State& state) noexcept
{
    constexpr std::size_t cleanupAttempts = 3U;
    for (std::size_t attempt = 0U; attempt < cleanupAttempts; ++attempt) {
        CleanupStale(state);
        DrainWork(state);
        CleanupStale(state);
        CleanupMappingOwners(state);
    }
}

void ProgramRuntime::Impl::DrainWork(State& state) noexcept
{
    WorkItem item{};
    while (state.dispatch.workQueue.TryPop(item)) {
        const std::uint64_t generation = state.generation.load(
            std::memory_order_acquire);
        const bool accepting = state.accepting.load(std::memory_order_acquire);
        const bool targetEligible = state.targetEligible.load(
            std::memory_order_acquire);
        if (item.kind == WorkKind::TaskStart) {
            if (item.taskSlot >= state.scheduler.taskCount) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    {},
                    item.taskSlot,
                    kInvalidProgramIndex,
                    7U);
                continue;
            }
            TaskInstance& task = state.scheduler.tasks[item.taskSlot];
            if (task.status.load(std::memory_order_acquire) != TaskStatus::Reserved) {
                continue;
            }
            if (item.generation != generation
                || !accepting
                || !targetEligible) {
                task.status.store(TaskStatus::Free, std::memory_order_release);
                state.metrics.cancelledTasks.fetch_add(
                    1U,
                    std::memory_order_relaxed);
                continue;
            }
            if (debugPort != nullptr
                && item.debugCaptureEpoch != 0U
                && item.debugInputSequence != 0U
                && item.debugRuleIndex != kInvalidProgramIndex) {
                const std::uint64_t marker =
                    state.scheduler.nextDebugExecutionMarker++;
                if (marker != 0U) {
                    RuntimeDebugEvent event{};
                    event.kind = RuntimeDebugEventKind::RuleMatched;
                    event.captureEpoch = item.debugCaptureEpoch;
                    event.executionMarker = marker;
                    event.triggerInputSequence = item.debugInputSequence;
                    event.eventKey = item.debugEventKey;
                    event.ruleIndex = item.debugRuleIndex;
                    if (debugPort->Publish(event)) {
                        task.debugCaptureEpoch = item.debugCaptureEpoch;
                        task.debugExecutionMarker = marker;
                    }
                }
            }
            task.readyOrder = state.scheduler.nextReadyOrder++;
            task.status.store(TaskStatus::Ready, std::memory_order_release);
            state.metrics.startedTasks.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        if (item.generation == generation
            && accepting
            && (targetEligible || item.kind == WorkKind::MappingRelease)) {
            ProcessMappingWork(state, item);
        }
    }
}

void ProgramRuntime::Impl::PromoteTimed(State& state) noexcept
{
    const std::int64_t now = clock.NowNanoseconds();
    while (true) {
        std::uint32_t selected = kInvalidTaskSlot;
        std::int64_t selectedDeadline = (std::numeric_limits<std::int64_t>::max)();
        std::uint64_t selectedOrder = (std::numeric_limits<std::uint64_t>::max)();
        for (std::size_t index = 0U;
             index < state.scheduler.taskCount;
             ++index) {
            const TaskInstance& task = state.scheduler.tasks[index];
            if (task.status.load(std::memory_order_acquire) != TaskStatus::Timed
                || task.deadlineNanoseconds > now) {
                continue;
            }
            if (task.deadlineNanoseconds < selectedDeadline
                || (task.deadlineNanoseconds == selectedDeadline
                    && task.timedOrder < selectedOrder)) {
                selected = static_cast<std::uint32_t>(index);
                selectedDeadline = task.deadlineNanoseconds;
                selectedOrder = task.timedOrder;
            }
        }
        if (selected == kInvalidTaskSlot) {
            return;
        }
        TaskInstance& task = state.scheduler.tasks[selected];
        task.readyOrder = state.scheduler.nextReadyOrder++;
        task.status.store(TaskStatus::Ready, std::memory_order_release);
    }
}

std::uint32_t ProgramRuntime::Impl::SelectReadyTask(State& state) noexcept
{
    std::uint32_t selected = kInvalidTaskSlot;
    std::uint64_t selectedOrder = (std::numeric_limits<std::uint64_t>::max)();
    for (std::size_t index = 0U;
         index < state.scheduler.taskCount;
         ++index) {
        const TaskInstance& task = state.scheduler.tasks[index];
        if (task.status.load(std::memory_order_acquire) == TaskStatus::Ready
            && task.readyOrder < selectedOrder) {
            selected = static_cast<std::uint32_t>(index);
            selectedOrder = task.readyOrder;
        }
    }
    return selected;
}

void ProgramRuntime::Impl::ScheduleTimed(
    State& state,
    TaskInstance& task,
    DurationValue duration) noexcept
{
    const std::int64_t now = (std::max)(clock.NowNanoseconds(), std::int64_t{0});
    task.deadlineNanoseconds = AddDeadline(now, duration);
    if (task.deadlineNanoseconds > now) {
        task.instructionsWithoutSuspension = 0U;
        task.outputsWithoutSuspension = 0U;
        state.scheduler.positiveSuspensions.fetch_add(
            1U,
            std::memory_order_relaxed);
    }
    task.timedOrder = state.scheduler.nextTimedOrder++;
    task.status.store(TaskStatus::Timed, std::memory_order_release);
    PublishDiagnostic(
        state,
        RuntimeDiagnosticKind::OwnershipChange,
        {},
        task.action.value,
        task.position,
        task.deadlineNanoseconds,
        2U);
}

RuntimeEvaluationResult ProgramRuntime::Impl::EvaluateTaskExpression(
    State& state,
    ExpressionId expression) noexcept
{
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::shared_lock variableLock(state.mutableState.variableMutex);
    return Evaluate(state, expression, state.scheduler.expressionScratch);
}

SourceSpan ProgramRuntime::Impl::ActionSource(
    const State& state,
    const ActionProgramDescriptor& descriptor,
    std::uint32_t position) const noexcept
{
    const std::uint32_t globalPosition = descriptor.code.begin + position;
    const auto spans = state.program->DebugInfo().actionInstructionSpans;
    return globalPosition < spans.size()
        ? spans[globalPosition]
        : descriptor.source;
}

bool ProgramRuntime::Impl::ExecuteSet(
    State& state,
    TaskInstance& task,
    const ActionInstruction& instruction,
    std::uint32_t instructionPosition) noexcept
{
    const auto refs = state.program->ValueRefs();
    if (instruction.operand0 >= refs.size()) {
        return false;
    }
    const ValueRef& target = refs[instruction.operand0];
    if (!IsUserDomain(target.domain)) {
        return false;
    }
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::unique_lock variableLock(state.mutableState.variableMutex);
    const RuntimeEvaluationResult result = Evaluate(
        state,
        ExpressionId{instruction.operand1},
        state.scheduler.expressionScratch);
    if (!result.Succeeded()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskExpressionFault,
            {},
            task.action.value,
            instructionPosition,
            static_cast<std::uint32_t>(result.fault));
        return false;
    }
    if (target.domain == ValueDomain::UserState
        && result.value.type == ExpressionType::State) {
        state.mutableState.userStates[target.index] = result.value.stateValue;
        return true;
    }
    if (target.domain == ValueDomain::UserNumber
        && result.value.type == ExpressionType::Number) {
        state.mutableState.userNumbers[target.index] = result.value.numberValue;
        return true;
    }
    if (target.domain == ValueDomain::UserDuration
        && result.value.type == ExpressionType::Duration) {
        state.mutableState.userDurations[target.index] = result.value.durationValue;
        return true;
    }
    return false;
}

bool ProgramRuntime::Impl::ExecuteToggle(
    State& state,
    const ActionInstruction& instruction,
    std::uint32_t instructionPosition) noexcept
{
    const auto refs = state.program->ValueRefs();
    if (instruction.operand0 >= refs.size()) {
        return false;
    }
    const ValueRef& target = refs[instruction.operand0];
    if (target.domain != ValueDomain::UserState
        || target.index >= state.mutableState.userStates.size()) {
        return false;
    }
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::unique_lock variableLock(state.mutableState.variableMutex);
    std::uint8_t& value = state.mutableState.userStates[target.index];
    value = value == 0U ? 1U : 0U;
    (void)instructionPosition;
    return true;
}

bool ProgramRuntime::Impl::RunTaskSlice(
    State& state,
    std::uint32_t slot) noexcept
{
    TaskInstance& task = state.scheduler.tasks[slot];
    TaskStatus expected = TaskStatus::Ready;
    if (!task.status.compare_exchange_strong(
            expected,
            TaskStatus::Running,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return false;
    }
    const std::uint64_t generation = state.generation.load(std::memory_order_acquire);
    if (task.generation != generation) {
        FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
        return true;
    }
    const auto descriptors = state.program->ActionPrograms();
    if (!task.action.IsValid() || task.action.value >= descriptors.size()) {
        RequestFatal(
            state,
            RuntimeDiagnosticKind::TaskActionFault,
            {},
            task.action.value,
            task.position,
            8U);
        FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
        return true;
    }
    const ActionProgramDescriptor& descriptor = descriptors[task.action.value];
    const auto code = state.program->ActionCode().subspan(
        descriptor.code.begin,
        descriptor.code.count);

    if (task.resumeKind == TaskResumeKind::TapRelease) {
        if (!ReleaseTaskControl(state, task, task.pendingTap)) {
            FinishTask(
                state,
                slot,
                task.generation != state.generation.load(std::memory_order_acquire),
                ClassifyTaskOperationFailure(state, task));
            return true;
        }
        task.resumeKind = TaskResumeKind::None;
        task.pendingTap = {};
        ++task.position;
    }

    while (task.position < code.size()) {
        if (task.generation != state.generation.load(std::memory_order_acquire)) {
            FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
            return true;
        }
        const std::uint32_t position = task.position;
        if (task.instructionsWithoutSuspension
            >= capacities.maximumTaskInstructionsWithoutSuspension) {
            PublishDiagnostic(
                state,
                RuntimeDiagnosticKind::TaskBudgetExceeded,
                ActionSource(state, descriptor, position),
                task.action.value,
                position,
                0,
                1U);
            task.safetyCancelled = true;
            FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
            return true;
        }
        ++task.instructionsWithoutSuspension;
        const ActionInstruction& instruction = code[position];
        const SourceSpan source = ActionSource(state, descriptor, position);
        if (debugPort != nullptr
            && task.debugCaptureEpoch != 0U
            && task.debugExecutionMarker != 0U) {
            RuntimeDebugEvent event{};
            event.kind = RuntimeDebugEventKind::ActionStarted;
            event.captureEpoch = task.debugCaptureEpoch;
            event.executionMarker = task.debugExecutionMarker;
            event.instructionIndex = position;
            (void)debugPort->Publish(event);
        }
        switch (instruction.opcode) {
        case ActionOpcode::Press:
            if (!AcquireTaskControl(state, task, ControlRefId{instruction.operand0})) {
                FinishTask(
                    state,
                    slot,
                    task.generation
                        != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            ++task.position;
            break;
        case ActionOpcode::Release:
            if (!ReleaseTaskControl(state, task, ControlRefId{instruction.operand0})) {
                FinishTask(
                    state,
                    slot,
                    task.generation
                        != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            ++task.position;
            break;
        case ActionOpcode::Tap:
            if (!AcquireTaskControl(state, task, ControlRefId{instruction.operand0})) {
                FinishTask(
                    state,
                    slot,
                    task.generation
                        != state.generation.load(std::memory_order_acquire),
                    ClassifyTaskOperationFailure(state, task));
                return true;
            }
            task.resumeKind = TaskResumeKind::TapRelease;
            task.pendingTap = ControlRefId{instruction.operand0};
            ScheduleTimed(state, task, state.program->Settings().tapDuration);
            return true;
        case ActionOpcode::Wait: {
            const RuntimeEvaluationResult result = EvaluateTaskExpression(
                state,
                ExpressionId{instruction.operand0});
            if (!result.Succeeded()
                || result.value.type != ExpressionType::Duration) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskExpressionFault,
                    source,
                    task.action.value,
                    position,
                    static_cast<std::uint32_t>(result.fault));
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            ScheduleTimed(state, task, result.value.durationValue);
            return true;
        }
        case ActionOpcode::Gap:
            ++task.position;
            ScheduleTimed(state, task, state.program->Settings().actionGap);
            return true;
        case ActionOpcode::Set:
            if (!ExecuteSet(state, task, instruction, position)) {
                if (!state.fatalShutdownRequested.load(std::memory_order_acquire)) {
                    PublishDiagnostic(
                        state,
                        RuntimeDiagnosticKind::TaskActionFault,
                        source,
                        task.action.value,
                        position,
                        0,
                        9U);
                }
                FinishTask(state, slot, false, RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            break;
        case ActionOpcode::Toggle:
            if (!ExecuteToggle(state, instruction, position)) {
                PublishDiagnostic(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    source,
                    task.action.value,
                    position,
                    0,
                    10U);
                FinishTask(state, slot, false, RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            break;
        case ActionOpcode::Exec: {
            if (task.generation != state.generation.load(std::memory_order_acquire)) {
                FinishTask(state, slot, true, RuntimeExecutionResult::Cancelled);
                return true;
            }
            TaskCancellationContext cancellationContext{
                &state.generation,
                task.generation};
            const RuntimeLaunchOutcome outcome = processLauncher.Launch(
                state.program->Strings()[instruction.operand0],
                RuntimeCancellationProbe{
                    &cancellationContext,
                    &TaskCancelled});
            if (!outcome.Succeeded()) {
                PublishDiagnostic(
                    state,
                    RuntimeDiagnosticKind::LaunchFailure,
                    source,
                    task.action.value,
                    position,
                    0,
                    static_cast<std::uint32_t>(outcome.result),
                    outcome.platformError);
                FinishTask(
                    state,
                    slot,
                    outcome.result == RuntimeLaunchResult::Cancelled,
                    outcome.result == RuntimeLaunchResult::Cancelled
                        ? RuntimeExecutionResult::Cancelled
                        : RuntimeExecutionResult::Failed);
                return true;
            }
            ++task.position;
            break;
        }
        case ActionOpcode::Jump:
            task.position = instruction.operand0;
            break;
        case ActionOpcode::JumpIfFalse: {
            const RuntimeEvaluationResult result = EvaluateTaskExpression(
                state,
                ExpressionId{instruction.operand0});
            if (!result.Succeeded()
                || result.value.type != ExpressionType::Boolean) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskExpressionFault,
                    source,
                    task.action.value,
                    position,
                    static_cast<std::uint32_t>(result.fault));
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            task.position = result.value.booleanValue
                ? position + 1U
                : instruction.operand1;
            break;
        }
        case ActionOpcode::RepeatInit: {
            const RuntimeEvaluationResult result = EvaluateTaskExpression(
                state,
                ExpressionId{instruction.operand1});
            if (!result.Succeeded()
                || result.value.type != ExpressionType::Number) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskExpressionFault,
                    source,
                    task.action.value,
                    position,
                    static_cast<std::uint32_t>(result.fault));
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            task.repeatFrames[instruction.operand0] = {
                0U,
                result.value.numberValue};
            ++task.position;
            break;
        }
        case ActionOpcode::RepeatCheck: {
            const RepeatFrame& frame = task.repeatFrames[instruction.operand0];
            task.position = static_cast<double>(frame.index) < frame.limit
                ? position + 1U
                : instruction.operand1;
            break;
        }
        case ActionOpcode::RepeatNext: {
            RepeatFrame& frame = task.repeatFrames[instruction.operand0];
            if (frame.index == (std::numeric_limits<std::uint64_t>::max)()) {
                RequestFatal(
                    state,
                    RuntimeDiagnosticKind::TaskActionFault,
                    source,
                    task.action.value,
                    position,
                    11U);
                FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
                return true;
            }
            ++frame.index;
            ++task.position;
            break;
        }
        case ActionOpcode::Yield:
            ++task.position;
            task.readyOrder = state.scheduler.nextReadyOrder++;
            task.status.store(TaskStatus::Ready, std::memory_order_release);
            return true;
        case ActionOpcode::End:
            FinishTask(state, slot, false, RuntimeExecutionResult::Completed);
            return true;
        }
    }

    RequestFatal(
        state,
        RuntimeDiagnosticKind::TaskActionFault,
        descriptor.source,
        task.action.value,
        task.position,
        12U);
    FinishTask(state, slot, true, RuntimeExecutionResult::Failed);
    return true;
}

RuntimePumpResult ProgramRuntime::Impl::Pump(std::size_t maximumSlices) noexcept
{
    State* const state = active.get();
    if (state == nullptr
        || state->scheduler.pumpLock.test_and_set(std::memory_order_acquire)) {
        return {};
    }

    CleanupStale(*state);
    DrainWork(*state);
    CleanupStale(*state);
    PromoteTimed(*state);

    std::size_t slices = 0U;
    while (slices < maximumSlices) {
        const std::uint32_t slot = SelectReadyTask(*state);
        if (slot == kInvalidTaskSlot) {
            break;
        }
        (void)RunTaskSlice(*state, slot);
        ++slices;
        CleanupStale(*state);
        DrainWork(*state);
        CleanupStale(*state);
        PromoteTimed(*state);
    }

    bool ready = false;
    bool timed = false;
    for (std::size_t index = 0U;
         index < state->scheduler.taskCount;
         ++index) {
        const TaskStatus status = state->scheduler.tasks[index].status.load(
            std::memory_order_acquire);
        ready = ready || status == TaskStatus::Ready;
        timed = timed || status == TaskStatus::Timed;
    }
    state->scheduler.pumpLock.clear(std::memory_order_release);
    return {slices, ready || !state->dispatch.workQueue.Empty(), timed};
}

std::int64_t ProgramRuntime::Impl::NextDeadline(const State& state) const noexcept
{
    std::int64_t deadline = (std::numeric_limits<std::int64_t>::max)();
    for (std::size_t index = 0U;
         index < state.scheduler.taskCount;
         ++index) {
        const TaskInstance& task = state.scheduler.tasks[index];
        if (task.status.load(std::memory_order_acquire) == TaskStatus::Timed) {
            deadline = (std::min)(deadline, task.deadlineNanoseconds);
        }
    }
    return deadline;
}

void ProgramRuntime::Impl::Wake() noexcept
{
    wakeSemaphore.release();
}

bool ProgramRuntime::Impl::StartTaskThread()
{
    if (active == nullptr) {
        return false;
    }
    bool expected = false;
    if (!taskThreadRunning.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return true;
    }
    taskThreadStop.store(false, std::memory_order_release);
    try {
        taskThread = std::thread(&Impl::TaskThreadMain, this);
    } catch (...) {
        taskThreadRunning.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void ProgramRuntime::Impl::StopTaskThread() noexcept
{
    if (!taskThreadRunning.load(std::memory_order_acquire)) {
        return;
    }
    taskThreadStop.store(true, std::memory_order_release);
    Wake();
    if (taskThread.joinable()) {
        taskThread.join();
    }
    taskThreadRunning.store(false, std::memory_order_release);
}

void ProgramRuntime::Impl::TaskThreadMain() noexcept
{
    std::uint32_t continuouslyReadyQuanta = 0U;
    while (!taskThreadStop.load(std::memory_order_acquire)) {
        const State* const pumpState = active.get();
        const std::uint64_t suspensionsBefore = pumpState == nullptr
            ? 0U
            : pumpState->scheduler.positiveSuspensions.load(
                  std::memory_order_relaxed);
        const RuntimePumpResult result = Pump(1024U);
        const std::uint64_t suspensionsAfter = pumpState == nullptr
            ? 0U
            : pumpState->scheduler.positiveSuspensions.load(
                  std::memory_order_relaxed);
        if (result.readyWorkRemaining) {
            if (suspensionsAfter != suspensionsBefore) {
                continuouslyReadyQuanta = 0U;
                continue;
            }
            ++continuouslyReadyQuanta;
            if (continuouslyReadyQuanta
                < capacities.maximumContinuouslyReadyQuanta) {
                continue;
            }
            continuouslyReadyQuanta = 0U;
            State* const state = active.get();
            if (state != nullptr) {
                state->metrics.schedulerBackoffs.fetch_add(
                    1U,
                    std::memory_order_relaxed);
            }
            std::int64_t backoffNanoseconds =
                capacities.continuouslyReadyBackoffNanoseconds;
            if (state != nullptr) {
                const std::int64_t deadline = NextDeadline(*state);
                if (deadline != (std::numeric_limits<std::int64_t>::max)()) {
                    const std::int64_t now = (std::max)(
                        clock.NowNanoseconds(),
                        std::int64_t{0});
                    const std::int64_t remaining = deadline > now
                        ? deadline - now
                        : 0;
                    backoffNanoseconds = (std::min)(
                        backoffNanoseconds,
                        remaining);
                }
            }
            (void)wakeSemaphore.try_acquire_for(
                std::chrono::nanoseconds(backoffNanoseconds));
            while (wakeSemaphore.try_acquire()) {
            }
            continue;
        }
        continuouslyReadyQuanta = 0U;
        const State* const state = active.get();
        const std::int64_t deadline = state == nullptr
            ? (std::numeric_limits<std::int64_t>::max)()
            : NextDeadline(*state);
        if (deadline == (std::numeric_limits<std::int64_t>::max)()) {
            wakeSemaphore.acquire();
        } else {
            const std::int64_t now = clock.NowNanoseconds();
            const std::int64_t remaining = (std::max)(deadline - now, std::int64_t{0});
            (void)wakeSemaphore.try_acquire_for(
                std::chrono::nanoseconds(remaining));
        }
        while (wakeSemaphore.try_acquire()) {
        }
    }
}

ProgramRuntime::ProgramRuntime(
    RuntimeCapacities capacities,
    RuntimeControlPort& controlPort,
    RuntimeOutputPort& outputPort,
    RuntimeRoutePort& routePort,
    RuntimeProcessLauncher& processLauncher,
    RuntimeClock& clock,
    RuntimeDebugEventPort* debugPort)
    : impl_(std::make_unique<Impl>(
          capacities,
          controlPort,
          outputPort,
          routePort,
          processLauncher,
          clock,
          debugPort))
{
}

ProgramRuntime::~ProgramRuntime() = default;

RuntimeActivationResult ProgramRuntime::Activate(
    std::shared_ptr<const CompiledProgram> program,
    TargetSelectorKind targetKindOverride)
{
    return impl_->Activate(std::move(program), targetKindOverride);
}

void ProgramRuntime::Deactivate() noexcept
{
    impl_->Deactivate();
}

InputDecision ProgramRuntime::HandleInput(
    const RuntimeInputEvent& event) noexcept
{
    return impl_->HandleInput(event);
}

bool ProgramRuntime::SeedPhysicalState(
    ControlRefId control,
    bool down) noexcept
{
    return impl_->SeedPhysicalState(control, down);
}

bool ProgramRuntime::MarkPhysicalStateUnsynchronized(
    ControlRefId control) noexcept
{
    return impl_->MarkPhysicalStateUnsynchronized(control);
}

void ProgramRuntime::SetTargetEligible(bool eligible) noexcept
{
    impl_->SetTargetEligible(eligible);
}

RuntimePumpResult ProgramRuntime::Pump(std::size_t maximumSlices) noexcept
{
    return impl_->Pump(maximumSlices);
}

bool ProgramRuntime::StartTaskThread()
{
    return impl_->StartTaskThread();
}

void ProgramRuntime::StopTaskThread() noexcept
{
    impl_->StopTaskThread();
}

void ProgramRuntime::NotifyTargetLost() noexcept
{
    if (impl_->active != nullptr) {
        impl_->Invalidate(
            *impl_->active,
            RuntimeCancellationReason::TargetLoss);
    }
}

void ProgramRuntime::RequestShutdown() noexcept
{
    if (impl_->active != nullptr) {
        impl_->Invalidate(
            *impl_->active,
            RuntimeCancellationReason::Shutdown);
    }
}

bool ProgramRuntime::HasActiveProgram() const noexcept
{
    return impl_->active != nullptr;
}

bool ProgramRuntime::TargetEligible() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->targetEligible.load(std::memory_order_acquire);
}

bool ProgramRuntime::PauseOn() const noexcept
{
    return impl_->active != nullptr
        && (impl_->active->program->PauseControlBuckets().empty()
            || impl_->active->mutableState.PauseEnabled());
}

bool ProgramRuntime::ExitRequested() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->exitRequested.load(std::memory_order_acquire);
}

bool ProgramRuntime::FatalShutdownRequested() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->fatalShutdownRequested.load(std::memory_order_acquire);
}

std::uint64_t ProgramRuntime::Generation() const noexcept
{
    return impl_->observableGeneration.load(std::memory_order_acquire);
}

std::size_t ProgramRuntime::ActiveTaskCount() const noexcept
{
    return impl_->active == nullptr
        ? 0U
        : impl_->active->scheduler.ActiveTaskCount();
}

bool ProgramRuntime::HasActiveMappings() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->dispatch.HasActiveMappings();
}

bool ProgramRuntime::HasOwnedOutputs() const noexcept
{
    return impl_->active != nullptr
        && impl_->active->output.HasOwnedOutputs();
}

RuntimeMetrics ProgramRuntime::Metrics() const noexcept
{
    if (impl_->active == nullptr) {
        return {};
    }
    const Impl::State& state = *impl_->active;
    return state.metrics.Snapshot(state.diagnostics.Dropped());
}

RuntimeEvaluationResult ProgramRuntime::EvaluateExpression(
    ExpressionId expression) noexcept
{
    if (impl_->active == nullptr) {
        RuntimeEvaluationResult result{};
        result.fault = RuntimeEvaluationFault::InvalidExpression;
        return result;
    }
    Impl::State& state = *impl_->active;
    std::shared_lock pauseLock(state.mutableState.pauseMutex);
    std::shared_lock variableLock(state.mutableState.variableMutex);
    return impl_->Evaluate(
        state,
        expression,
        state.dispatch.expressionScratch);
}

bool ProgramRuntime::ReadUserState(
    std::uint32_t index,
    bool& value) const noexcept
{
    if (impl_->active == nullptr) {
        return false;
    }
    return impl_->active->mutableState.ReadUserState(index, value);
}

bool ProgramRuntime::TryPopDiagnostic(
    RuntimeDiagnosticRecord& record) noexcept
{
    return impl_->active != nullptr
        && impl_->active->diagnostics.TryPop(record);
}

} // namespace inputweaver
