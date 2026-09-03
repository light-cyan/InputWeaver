#pragma once

#include "program_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <semaphore>
#include <shared_mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace inputweaver::program_runtime_detail {

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
    MappingAgain,
    MappingRelease,
};

enum class PauseLockMode : std::uint8_t {
    None,
    Read,
    Write,
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
    std::uint64_t debugCaptureEpoch{};
    std::uint64_t debugExecutionMarker{};
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

[[nodiscard]] inline std::int64_t AddDeadline(
    std::int64_t now,
    DurationValue duration) noexcept
{
    const std::int64_t maximum = (std::numeric_limits<std::int64_t>::max)();
    if (duration.nanoseconds > maximum - now) {
        return maximum;
    }
    return now + duration.nanoseconds;
}

[[nodiscard]] inline bool IsUserDomain(ValueDomain domain) noexcept
{
    return domain == ValueDomain::UserState
        || domain == ValueDomain::UserNumber
        || domain == ValueDomain::UserDuration;
}

struct TaskCancellationContext final {
    const std::atomic<std::uint64_t>* generation{};
    std::uint64_t expected{};
};

[[nodiscard]] inline bool TaskCancelled(void* rawContext) noexcept
{
    const auto* context = static_cast<const TaskCancellationContext*>(rawContext);
    return context == nullptr
        || context->generation == nullptr
        || context->generation->load(std::memory_order_acquire) != context->expected;
}

[[nodiscard]] inline RuntimeDebugArraySnapshot BuildArraySnapshot(
    ArrayId arrayId,
    const RuntimeArrayStorage& array) noexcept
{
    RuntimeDebugArraySnapshot snapshot{};
    snapshot.array = arrayId;
    snapshot.elementType = array.ElementType();
    snapshot.length = static_cast<std::uint64_t>(array.Size());
    const std::size_t prefixCount = array.Size() <= kRuntimeDebugArrayPreviewCount
        ? array.Size()
        : kRuntimeDebugArrayPreviewCount / 2U;
    const std::size_t suffixCount = array.Size() <= kRuntimeDebugArrayPreviewCount
        ? 0U
        : kRuntimeDebugArrayPreviewCount / 2U;
    snapshot.prefixCount = static_cast<std::uint8_t>(prefixCount);
    snapshot.suffixCount = static_cast<std::uint8_t>(suffixCount);
    RuntimeValue value{};
    for (std::size_t index = 0U; index < prefixCount; ++index) {
        (void)array.Read(index, value);
        snapshot.elements[index].stateValue = value.stateValue;
        snapshot.elements[index].numberValue = value.numberValue;
    }
    for (std::size_t index = 0U; index < suffixCount; ++index) {
        (void)array.Read(array.Size() - suffixCount + index, value);
        snapshot.elements[prefixCount + index].stateValue = value.stateValue;
        snapshot.elements[prefixCount + index].numberValue = value.numberValue;
    }
    return snapshot;
}

} // namespace inputweaver::program_runtime_detail

namespace inputweaver {
using namespace program_runtime_detail;

struct ProgramRuntime::Impl final {
    struct MutableState final {
        [[nodiscard]] static std::vector<RuntimeArrayStorage> CreateArrays(
            const CompiledProgram& program)
        {
            std::vector<RuntimeArrayStorage> result;
            result.reserve(program.Arrays().size());
            for (const ArrayDescriptor& descriptor : program.Arrays()) {
                if (descriptor.elementType == ArrayElementType::State) {
                    result.emplace_back(
                        descriptor.elementType,
                        program.InitialArrayStates().subspan(
                            descriptor.initialValues.begin,
                            descriptor.initialValues.count),
                        std::span<const double>{});
                } else {
                    result.emplace_back(
                        descriptor.elementType,
                        std::span<const std::uint8_t>{},
                        program.InitialArrayNumbers().subspan(
                            descriptor.initialValues.begin,
                            descriptor.initialValues.count));
                }
            }
            return result;
        }

        MutableState(
            const CompiledProgram& program,
            std::uint64_t randomSeed)
            : userStates(program.UserValues().initialStates),
              userNumbers(program.UserValues().initialNumbers),
              userDurations(program.UserValues().initialDurations),
              arrays(CreateArrays(program)),
              randomStream(randomSeed),
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
            const CompiledProgram& program) noexcept
        {
            return {
                userStates,
                userNumbers,
                userDurations,
                arrays,
                {physicalHeld.get(), program.Controls().size()},
                &randomStream,
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
        std::vector<RuntimeArrayStorage> arrays;
        RuntimeRandomStream randomStream;
        std::unique_ptr<std::atomic<std::uint8_t>[]> physicalHeld;
        std::unique_ptr<std::atomic<std::uint8_t>[]> physicalSynchronized;
    };

    struct EventStateTransaction final {
        EventStateTransaction(MutableState& state, PauseLockMode pauseMode)
            : pauseRead(state.pauseMutex, std::defer_lock),
              pauseWrite(state.pauseMutex, std::defer_lock),
              variables(state.variableMutex, std::defer_lock)
        {
            if (pauseMode == PauseLockMode::Read) {
                pauseRead.lock();
            } else if (pauseMode == PauseLockMode::Write) {
                pauseWrite.lock();
            }
            variables.lock();
        }

        std::shared_lock<std::shared_mutex> pauseRead;
        std::unique_lock<std::shared_mutex> pauseWrite;
        std::shared_lock<std::shared_mutex> variables;
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
        std::mutex pumpMutex;
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
                schedulerBackoffs.load(std::memory_order_relaxed),
                currentArrayBytes.load(std::memory_order_relaxed),
                peakArrayBytes.load(std::memory_order_relaxed),
                rejectedArrayGrowth.load(std::memory_order_relaxed)};
        }

        std::atomic<std::uint64_t> dispatchedEvents{0U};
        std::atomic<std::uint64_t> suppressedEvents{0U};
        std::atomic<std::uint64_t> startedTasks{0U};
        std::atomic<std::uint64_t> completedTasks{0U};
        std::atomic<std::uint64_t> cancelledTasks{0U};
        std::atomic<std::uint64_t> transactionRejections{0U};
        std::atomic<std::uint64_t> outputTransitions{0U};
        std::atomic<std::uint64_t> schedulerBackoffs{0U};
        std::atomic<std::uint64_t> currentArrayBytes{0U};
        std::atomic<std::uint64_t> peakArrayBytes{0U};
        std::atomic<std::uint64_t> rejectedArrayGrowth{0U};
    };

    struct State final {
        State(
            std::shared_ptr<const CompiledProgram> immutableProgram,
            std::uint64_t randomSeed,
            std::uint64_t serial,
            std::atomic<std::uint64_t>& runtimeGeneration,
            const RuntimeCapacities& capacities)
            : program(std::move(immutableProgram)),
              programSerial(serial),
              activatedControls(program->Controls().size()),
              mutableState(*program, randomSeed),
              dispatch(*program, capacities),
              scheduler(*program, capacities),
              inspectionScratch((std::max)(
                  std::size_t{1U},
                  static_cast<std::size_t>(
                      program->Requirements().maximumExpressionStackDepth))),
              output(program->Controls().size()),
              diagnostics(capacities.diagnosticRecordCount),
              generation(runtimeGeneration)
        {
            std::uint64_t initialArrayBytes = 0U;
            for (const RuntimeArrayStorage& array : mutableState.arrays) {
                initialArrayBytes += static_cast<std::uint64_t>(
                    array.AllocatedBytes());
            }
            metrics.currentArrayBytes.store(
                initialArrayBytes,
                std::memory_order_relaxed);
            metrics.peakArrayBytes.store(
                initialArrayBytes,
                std::memory_order_relaxed);
        }

        std::shared_ptr<const CompiledProgram> program;
        std::uint64_t programSerial{};
        TargetSelectorKind targetKind{TargetSelectorKind::Unspecified};
        std::vector<ActivatedControl> activatedControls;

        MutableState mutableState;
        DispatchState dispatch;
        SchedulerState scheduler;
        std::mutex inspectionMutex;
        RuntimeExpressionScratch inspectionScratch;
        OutputState output;
        DiagnosticBuffer diagnostics;
        MetricCounters metrics;

        std::atomic<std::uint64_t>& generation;
        std::atomic<bool> accepting{true};
        std::atomic<bool> targetEligible{true};
        std::atomic<bool> exitRequested{false};
        std::atomic<bool> fatalShutdownRequested{false};
    };

    struct MutationTransaction final {
        MutationTransaction(State& state, const TaskInstance& task)
            : pause(state.mutableState.pauseMutex),
              variables(state.mutableState.variableMutex),
              current(task.generation
                  == state.generation.load(std::memory_order_acquire))
        {
        }

        void Unlock() noexcept
        {
            variables.unlock();
            pause.unlock();
        }

        std::shared_lock<std::shared_mutex> pause;
        std::unique_lock<std::shared_mutex> variables;
        bool current{};
    };

    struct MutationPublication final {
        RuntimeDebugValue scalar{};
        RuntimeDebugArraySnapshot array{};
        bool hasScalar{};
        bool hasArray{};
    };

    Impl(
        RuntimeCapacities runtimeCapacities,
        RuntimeControlPort& runtimeControlPort,
        RuntimeOutputPort& runtimeOutputPort,
        RuntimeRoutePort& runtimeRoutePort,
        RuntimeProcessLauncher& runtimeProcessLauncher,
        RuntimeClock& runtimeClock,
        RuntimeDebugEventPort* runtimeDebugPort,
        support::CallbackRef<void() noexcept> runtimeFatalStopRequest)
        : capacities(runtimeCapacities),
          controlPort(runtimeControlPort),
          outputPort(runtimeOutputPort),
          routePort(runtimeRoutePort),
          processLauncher(runtimeProcessLauncher),
          clock(runtimeClock),
          debugPort(runtimeDebugPort),
          fatalStopRequest(runtimeFatalStopRequest)
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
    support::CallbackRef<void() noexcept> fatalStopRequest;
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
    void PublishStateChanged(RuntimeDebugValue value) noexcept;
    void PublishArrayChanged(RuntimeDebugArraySnapshot array) noexcept;
    [[nodiscard]] bool CompleteMutation(
        MutationTransaction& transaction,
        const MutationPublication& publication) noexcept;
    [[nodiscard]] std::uint64_t BeginDebugExecution(
        State& state,
        const WorkItem& item) noexcept;
    void PublishDebugExecutionEnded(
        std::uint64_t captureEpoch,
        std::uint64_t executionMarker,
        RuntimeExecutionResult result) noexcept;

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
    [[nodiscard]] bool ScheduleTimed(
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
        TaskInstance& task,
        const ActionInstruction& instruction,
        std::uint32_t instructionPosition) noexcept;
    [[nodiscard]] bool ExecuteArrayAction(
        State& state,
        TaskInstance& task,
        const ActionInstruction& instruction,
        std::uint32_t instructionPosition,
        SourceSpan source) noexcept;
    [[nodiscard]] RuntimeEvaluationResult EvaluateTaskExpression(
        State& state,
        ExpressionId expression) noexcept;
    [[nodiscard]] SourceSpan ActionSource(
        const State& state,
        const ActionProgramDescriptor& descriptor,
        std::uint32_t position) const noexcept;
    [[nodiscard]] SourceSpan ExpressionSource(
        const State& state,
        ExpressionId expression,
        std::uint32_t position,
        std::uint32_t& globalPosition) const noexcept;
    void ReportExpressionFault(
        State& state,
        RuntimeDiagnosticKind kind,
        ExpressionId expression,
        const RuntimeEvaluationResult& result,
        bool fatal) noexcept;
    [[nodiscard]] std::int64_t NextDeadline(const State& state) const noexcept;
};

} // namespace inputweaver
