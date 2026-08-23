#include "runtime/action_queue.hpp"
#include "runtime/fixed_rules.hpp"
#include "support/producer_done_drain.hpp"
#include "platform/windows/action_scheduler.hpp"
#include "platform/windows/remap_engine.hpp"
#include "platform/windows/runtime_session.hpp"
#include "diagnostics/diagnostic_log.hpp"
#include "platform/windows/input_classifier.hpp"
#include "platform/windows/input_injector.hpp"
#include "platform/windows/low_level_hooks.hpp"
#include "platform/windows/process_locator.hpp"
#include "platform/windows/process_context.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace inputweaver {

struct RuntimeTestAccess final {
    static void SetSendInput(
        WindowsRuntimeSession& runtime,
        SendInputFunction sendInput) noexcept {
        runtime.actionScheduler_->injector_ =
            InputInjector(runtime.options_.selfTag, sendInput);
    }

    static void ExecuteEligible(
        WindowsRuntimeSession& runtime,
        const ActionBatch& batch) noexcept {
        InjectionDiagnosticRecord record{};
        record.sourceSequence = batch.sourceSequence;
        record.targetPid = batch.targetPid;
        runtime.actionScheduler_->ExecuteEligibleActionBatch(batch, record);
    }

    static void Process(WindowsRuntimeSession& runtime, const ActionBatch& batch) noexcept {
        runtime.actionScheduler_->ProcessActionBatch(batch);
    }

    static bool Enqueue(WindowsRuntimeSession& runtime, const ActionBatch& batch) noexcept {
        return runtime.actionScheduler_->actionQueue_.TryPush(batch);
    }

    static ActionQueuePushResult Schedule(
        WindowsRuntimeSession& runtime,
        const ActionBatch& batch) noexcept {
        return runtime.actionScheduler_->TrySchedule(
            batch,
            {nullptr, &RuntimeTestAccess::CommitCapture});
    }

    static bool NewCapturesEnabled(const WindowsRuntimeSession& runtime) noexcept {
        return runtime.remapEngine_->NewCapturesEnabled();
    }

    static bool QueueEmpty(const WindowsRuntimeSession& runtime) noexcept {
        return runtime.actionScheduler_->actionQueue_.Empty();
    }

    static bool ShutdownRequested(const WindowsRuntimeSession& runtime) noexcept {
        return runtime.shutdownRequested_.load(std::memory_order_acquire);
    }

    static bool CreateEvents(
        WindowsRuntimeSession& runtime,
        std::wstring& errorMessage) {
        return runtime.CreateComponents(errorMessage);
    }

    static void SignalProducerDone(WindowsRuntimeSession& runtime) noexcept {
        runtime.actionScheduler_->NotifyProducerDone();
    }

    static void DrainForShutdown(WindowsRuntimeSession& runtime) noexcept {
        runtime.actionScheduler_->DrainForShutdown();
    }

private:
    static bool CommitCapture(void*) noexcept {
        return true;
    }
};

}  // namespace inputweaver

namespace {

int g_failureCount = 0;

enum class FakeSendMode {
    Complete,
    Fail,
    PartialFirstCall,
    PartialThenCleanupFail
};

struct FakeSendState {
    FakeSendMode mode{FakeSendMode::Complete};
    std::size_t callCount{};
    std::array<UINT, 16> counts{};
    std::array<std::array<INPUT, inputweaver::kMaximumPreparedInputs>, 16> inputs{};
};

FakeSendState g_fakeSendState;

void ResetFakeSend(FakeSendMode mode)
{
    g_fakeSendState = {};
    g_fakeSendState.mode = mode;
}

UINT WINAPI FakeSendInput(UINT count, LPINPUT inputs, int inputSize)
{
    const std::size_t callIndex = g_fakeSendState.callCount++;
    if (callIndex < g_fakeSendState.counts.size()) {
        g_fakeSendState.counts[callIndex] = count;
        if (inputs != nullptr && inputSize == static_cast<int>(sizeof(INPUT))) {
            const std::size_t copyCount = (std::min)(
                static_cast<std::size_t>(count),
                g_fakeSendState.inputs[callIndex].size());
            std::copy_n(inputs, copyCount, g_fakeSendState.inputs[callIndex].begin());
        }
    }

    if (g_fakeSendState.mode == FakeSendMode::Fail ||
        (g_fakeSendState.mode == FakeSendMode::PartialThenCleanupFail && callIndex != 0)) {
        SetLastError(
            g_fakeSendState.mode == FakeSendMode::PartialThenCleanupFail
                ? ERROR_RETRY
                : ERROR_ACCESS_DENIED);
        return 0;
    }
    if ((g_fakeSendState.mode == FakeSendMode::PartialFirstCall ||
         g_fakeSendState.mode == FakeSendMode::PartialThenCleanupFail) &&
        callIndex == 0) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return count == 0 ? 0 : 1;
    }
    SetLastError(ERROR_SUCCESS);
    return count;
}

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++g_failureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

inputweaver::InputEvent KeyboardEvent(
    DWORD code,
    inputweaver::Transition transition,
    inputweaver::InputOrigin origin = inputweaver::InputOrigin::PhysicalCandidate)
{
    inputweaver::InputEvent event{};
    event.device = inputweaver::DeviceKind::Keyboard;
    event.origin = origin;
    event.transition = transition;
    event.code = code;
    return event;
}

inputweaver::InputEvent MouseButtonEvent(
    DWORD code,
    inputweaver::Transition transition,
    inputweaver::InputOrigin origin = inputweaver::InputOrigin::PhysicalCandidate)
{
    inputweaver::InputEvent event{};
    event.device = inputweaver::DeviceKind::Mouse;
    event.origin = origin;
    event.transition = transition;
    event.code = code;
    return event;
}

void TestOriginClassification()
{
    constexpr inputweaver::SelfTag selfTag = static_cast<inputweaver::SelfTag>(0x51A7BEEFU);

    KBDLLHOOKSTRUCT keyboard{};
    keyboard.dwExtraInfo = selfTag;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag) == inputweaver::InputOrigin::PhysicalCandidate,
        "non-injected keyboard remains physical with matching extra info");

    keyboard.flags = LLKHF_INJECTED;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag) == inputweaver::InputOrigin::SelfInjected,
        "tagged injected keyboard is self-injected");

    keyboard.flags = LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED;
    keyboard.dwExtraInfo = selfTag + 1;
    Check(
        inputweaver::ClassifyKeyboard(keyboard, selfTag) == inputweaver::InputOrigin::ExternalInjected,
        "other injected keyboard is external despite lower-integrity flag");

    MSLLHOOKSTRUCT mouse{};
    mouse.dwExtraInfo = selfTag;
    Check(
        inputweaver::ClassifyMouse(mouse, selfTag) == inputweaver::InputOrigin::PhysicalCandidate,
        "non-injected mouse remains physical with matching extra info");

    mouse.flags = LLMHF_INJECTED;
    Check(
        inputweaver::ClassifyMouse(mouse, selfTag) == inputweaver::InputOrigin::SelfInjected,
        "tagged injected mouse is self-injected");

    mouse.flags = LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED;
    mouse.dwExtraInfo = selfTag + 1;
    Check(
        inputweaver::ClassifyMouse(mouse, selfTag) == inputweaver::InputOrigin::ExternalInjected,
        "other injected mouse is external despite lower-integrity flag");

#if UINTPTR_MAX > UINT32_MAX
    mouse.flags = LLMHF_INJECTED;
    mouse.dwExtraInfo = static_cast<ULONG_PTR>(selfTag) |
                        (static_cast<ULONG_PTR>(0xA5A5A5A5U) << 32U);
    Check(
        inputweaver::ClassifyMouse(mouse, selfTag) == inputweaver::InputOrigin::SelfInjected,
        "mouse classification tolerates upper-bit loss or rewriting");
#endif
}

void TestBatchBuilders()
{
    const inputweaver::ActionBatch tap = inputweaver::MakeTapActionBatch(
        17,
        4,
        1234,
        inputweaver::DeviceKind::Keyboard,
        VK_F8);
    Check(tap.sourceSequence == 17, "tap retains source sequence");
    Check(tap.outputStateGeneration == 4, "tap retains output generation");
    Check(tap.targetPid == 1234, "tap retains target PID");
    Check(tap.actionCount == 2, "tap has paired actions");
    Check(
        tap.actions[0].transition == inputweaver::Transition::Down
            && tap.actions[1].transition == inputweaver::Transition::Up,
        "tap has down then up");
    Check(
        tap.actions[0].code == VK_F8 && tap.actions[1].code == VK_F8,
        "tap uses the requested control");
    Check(!tap.requiresPointerTarget, "keyboard-only tap does not require pointer routing");

    const inputweaver::ActionBatch pointerBoundTap = inputweaver::MakeTapActionBatch(
        18,
        0,
        1234,
        inputweaver::DeviceKind::Keyboard,
        VK_F5,
        true);
    Check(pointerBoundTap.requiresPointerTarget, "explicitly pointer-bound tap retains its route guard");

    const inputweaver::ActionBatch move = inputweaver::MakeRelativeMouseMoveBatch(19, 1234, 3, -2);
    Check(move.actionCount == 1, "relative mouse batch has one action");
    Check(
        move.actions[0].device == inputweaver::DeviceKind::Mouse
            && move.actions[0].transition == inputweaver::Transition::Move,
        "relative mouse batch describes movement");
    Check(
        move.actions[0].valueX == 3 && move.actions[0].valueY == -2,
        "relative mouse batch retains deltas");
    Check(move.requiresPointerTarget, "relative mouse batch requires pointer routing");
}

void TestKeyboardRulesAndRecursion()
{
    constexpr DWORD targetPid = 100;
    inputweaver::FixedRuleEngine rules;

    const inputweaver::RuleEvaluation f6 = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 1);
    Check(f6.kind == inputweaver::RuleEvaluationKind::ActionReady, "physical F6 prepares an action");
    Check(f6.rule == inputweaver::RuleId::F6ToF7, "physical F6 selects the F6-to-F7 rule");
    Check(
        f6.batch.outputDevice == inputweaver::DeviceKind::Keyboard
            && f6.batch.outputCode == VK_F7,
        "physical F6 prepares F7 output");
    Check(rules.CanInject(f6.batch), "fresh F6 output state is injectable");
    Check(rules.CommitCapture(f6), "accepted F6 action captures its source");

    const inputweaver::RuleEvaluation repeat = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 2);
    Check(
        repeat.kind == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured F6 repeat is suppressed without another action");

    const inputweaver::RuleEvaluation selfF7Down = rules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Down, inputweaver::InputOrigin::SelfInjected),
        true,
        targetPid,
        3);
    const inputweaver::RuleEvaluation selfF7Up = rules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Up, inputweaver::InputOrigin::SelfInjected),
        true,
        targetPid,
        4);
    Check(
        selfF7Down.kind == inputweaver::RuleEvaluationKind::Forward
            && selfF7Up.kind == inputweaver::RuleEvaluationKind::Forward,
        "self-tagged F7 pair is forwarded and cannot produce F8");

    const inputweaver::RuleEvaluation f6Up = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Up), false, targetPid, 5);
    Check(
        f6Up.kind == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured F6 release stays suppressed after mode change");
    Check(!rules.HasCapturedInputs(), "F6 release clears paired capture");

    const inputweaver::RuleEvaluation f7 = rules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Down), true, targetPid, 6);
    Check(f7.kind == inputweaver::RuleEvaluationKind::ActionReady, "physical F7 prepares an action");
    Check(
        f7.rule == inputweaver::RuleId::F7ToF8 && f7.batch.outputCode == VK_F8,
        "physical F7 prepares F8 output");
    Check(rules.CommitCapture(f7), "accepted F7 action captures its source");
    Check(
        rules.Evaluate(KeyboardEvent(VK_F7, inputweaver::Transition::Up), true, targetPid, 7).kind
            == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured F7 release is suppressed");

    const inputweaver::RuleEvaluation externalF6 = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down, inputweaver::InputOrigin::ExternalInjected),
        true,
        targetPid,
        8);
    Check(
        externalF6.kind == inputweaver::RuleEvaluationKind::Forward,
        "external injected F6 is forwarded without a rule");
}

void TestCrossDeviceRulesAndRecursion()
{
    constexpr DWORD targetPid = 200;
    inputweaver::FixedRuleEngine rules;

    const inputweaver::RuleEvaluation f9 = rules.Evaluate(
        KeyboardEvent(VK_F9, inputweaver::Transition::Down), true, targetPid, 10);
    Check(f9.kind == inputweaver::RuleEvaluationKind::ActionReady, "physical F9 prepares an action");
    Check(
        f9.rule == inputweaver::RuleId::F9ToMiddle
            && f9.batch.outputDevice == inputweaver::DeviceKind::Mouse
            && f9.batch.outputCode == VK_MBUTTON
            && f9.batch.requiresPointerTarget,
        "physical F9 prepares a middle-button click");
    Check(rules.CommitCapture(f9), "accepted F9 action captures its source");

    const inputweaver::RuleEvaluation selfMiddleDown = rules.Evaluate(
        MouseButtonEvent(
            VK_MBUTTON,
            inputweaver::Transition::Down,
            inputweaver::InputOrigin::SelfInjected),
        true,
        targetPid,
        11);
    const inputweaver::RuleEvaluation selfMiddleUp = rules.Evaluate(
        MouseButtonEvent(
            VK_MBUTTON,
            inputweaver::Transition::Up,
            inputweaver::InputOrigin::SelfInjected),
        true,
        targetPid,
        12);
    Check(
        selfMiddleDown.kind == inputweaver::RuleEvaluationKind::Forward
            && selfMiddleUp.kind == inputweaver::RuleEvaluationKind::Forward,
        "self-tagged middle click is forwarded and cannot produce F10");

    Check(
        rules.Evaluate(KeyboardEvent(VK_F9, inputweaver::Transition::Up), true, targetPid, 13).kind
            == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured F9 release is suppressed");

    const inputweaver::RuleEvaluation middle = rules.Evaluate(
        MouseButtonEvent(VK_MBUTTON, inputweaver::Transition::Down), true, targetPid, 14);
    Check(middle.kind == inputweaver::RuleEvaluationKind::ActionReady, "physical middle prepares an action");
    Check(
        middle.rule == inputweaver::RuleId::MiddleToF10
            && middle.batch.outputDevice == inputweaver::DeviceKind::Keyboard
            && middle.batch.outputCode == VK_F10
            && middle.batch.requiresPointerTarget,
        "physical middle prepares F10 output");
    Check(rules.CommitCapture(middle), "accepted middle action captures its source");
    Check(
        rules.Evaluate(
            MouseButtonEvent(VK_MBUTTON, inputweaver::Transition::Down), true, targetPid, 15).kind
            == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured middle repeat is suppressed");
    Check(
        rules.Evaluate(
            MouseButtonEvent(VK_MBUTTON, inputweaver::Transition::Up), true, targetPid, 16).kind
            == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured middle release is suppressed");
}

void TestOutputConflictAndGeneration()
{
    constexpr DWORD targetPid = 300;
    inputweaver::FixedRuleEngine conflictRules;

    const inputweaver::RuleEvaluation heldF7 = conflictRules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Down), true, targetPid, 20);
    Check(heldF7.kind == inputweaver::RuleEvaluationKind::ActionReady, "physical F7 state is tracked");

    const inputweaver::RuleEvaluation conflictedF6 = conflictRules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 21);
    Check(
        conflictedF6.kind == inputweaver::RuleEvaluationKind::Forward
            && conflictedF6.rule == inputweaver::RuleId::F6ToF7,
        "F6 is forwarded when physical F7 is held");
    Check(!conflictRules.HasCapturedInputs(), "output conflict creates no source capture");
    Check(
        conflictRules.Evaluate(
            KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 22).kind
            == inputweaver::RuleEvaluationKind::Forward,
        "repeat after output conflict remains forwarded");
    (void)conflictRules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Up), true, targetPid, 23);
    (void)conflictRules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Up), true, targetPid, 24);

    inputweaver::FixedRuleEngine generationRules;
    const inputweaver::RuleEvaluation queuedF6 = generationRules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 25);
    Check(generationRules.CanInject(queuedF6.batch), "unchanged output generation passes worker check");

    (void)generationRules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Down), true, targetPid, 26);
    Check(!generationRules.CanInject(queuedF6.batch), "newly held output cancels queued action");
    const unsigned long long heldState = generationRules.PackedOutputState(
        inputweaver::DeviceKind::Keyboard,
        VK_F7);
    Check(inputweaver::PhysicalOutputIsDown(heldState), "published output state records down bit");

    (void)generationRules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Up), true, targetPid, 27);
    Check(
        !generationRules.CanInject(queuedF6.batch),
        "changed output generation stays cancelled after physical release");
    const unsigned long long releasedState = generationRules.PackedOutputState(
        inputweaver::DeviceKind::Keyboard,
        VK_F7);
    Check(
        !inputweaver::PhysicalOutputIsDown(releasedState)
            && inputweaver::PhysicalOutputGeneration(releasedState)
                > queuedF6.batch.outputStateGeneration,
        "published output generation advances on each physical edge");

    (void)generationRules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Up), true, targetPid, 28);

    inputweaver::FixedRuleEngine seededRules;
    seededRules.SeedPhysicalState(inputweaver::DeviceKind::Keyboard, VK_F7, true);
    Check(
        seededRules.Evaluate(
            KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 33).kind
            == inputweaver::RuleEvaluationKind::Forward,
        "startup-seeded held output prevents an unsafe tap");
    seededRules.SeedPhysicalState(inputweaver::DeviceKind::Keyboard, VK_F6, true);
    Check(
        seededRules.Evaluate(
            KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 34).kind
            == inputweaver::RuleEvaluationKind::Forward,
        "startup-seeded held source treats the first observed down as repeat");
}

void TestQueueCapacityAndCaptureCommit()
{
    inputweaver::ActionQueue queue;
    for (std::size_t index = 0; index < inputweaver::kActionQueueCapacity; ++index) {
        inputweaver::ActionBatch batch{};
        batch.sourceSequence = index;
        Check(queue.TryPush(batch), "queue accepts every documented-capacity slot");
    }
    Check(queue.SizeApprox() == inputweaver::kActionQueueCapacity, "queue reports full documented capacity");

    inputweaver::ActionBatch rejected{};
    rejected.sourceSequence = inputweaver::kActionQueueCapacity;
    Check(!queue.TryPush(rejected), "queue rejects the item after documented capacity");
    Check(queue.RejectedPushCount() == 1, "queue counts rejected pushes");

    for (std::size_t index = 0; index < inputweaver::kActionQueueCapacity / 2; ++index) {
        inputweaver::ActionBatch popped{};
        Check(queue.TryPop(popped), "queue pops initial items");
        Check(popped.sourceSequence == index, "queue preserves initial FIFO order");
    }

    for (std::size_t index = 0; index < inputweaver::kActionQueueCapacity / 2; ++index) {
        inputweaver::ActionBatch batch{};
        batch.sourceSequence = inputweaver::kActionQueueCapacity + index;
        Check(queue.TryPush(batch), "queue reuses wrapped slots");
    }

    for (std::size_t index = inputweaver::kActionQueueCapacity / 2;
         index < inputweaver::kActionQueueCapacity + inputweaver::kActionQueueCapacity / 2;
         ++index) {
        inputweaver::ActionBatch popped{};
        Check(queue.TryPop(popped), "queue pops wrapped items");
        Check(popped.sourceSequence == index, "queue preserves wrapped FIFO order");
    }
    Check(queue.Empty(), "queue is empty after all pops");

    inputweaver::ActionQueue pairQueue;
    inputweaver::ActionBatch firstPair{};
    firstPair.sourceSequence = 900;
    inputweaver::ActionBatch secondPair{};
    secondPair.sourceSequence = 901;
    Check(
        pairQueue.TryPushPair(firstPair, secondPair),
        "queue atomically accepts a two-batch publication pair");
    inputweaver::ActionBatch poppedPair{};
    Check(
        pairQueue.TryPop(poppedPair) && poppedPair.sourceSequence == 900,
        "atomic pair preserves its first batch");
    Check(
        pairQueue.TryPop(poppedPair) && poppedPair.sourceSequence == 901,
        "atomic pair preserves its second batch");

    inputweaver::FixedRuleEngine transactionRules;
    const inputweaver::RuleEvaluation transactionalF6 = transactionRules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, 401, 43);
    transactionRules.DisableNewCaptures();
    inputweaver::ActionQueue transactionQueue;
    const inputweaver::ActionQueuePushResult transactionResult =
        transactionQueue.TryPushWithCommit(
            transactionalF6.batch,
            [&transactionRules, &transactionalF6]() noexcept {
                return transactionRules.CommitCapture(transactionalF6);
            });
    Check(
        transactionResult == inputweaver::ActionQueuePushResult::CommitRejected,
        "capture commit rejects a concurrent disable at the queue publication boundary");
    Check(
        transactionQueue.Empty() && !transactionRules.HasCapturedInputs(),
        "commit rejection publishes no action and creates no capture");

    inputweaver::FixedRuleEngine rules;
    const inputweaver::RuleEvaluation f6 = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, 400, 40);

    inputweaver::ActionQueue fullQueue;
    inputweaver::ActionBatch filler{};
    for (std::size_t index = 0; index < inputweaver::kActionQueueCapacity; ++index) {
        Check(fullQueue.TryPush(filler), "capture test fills action queue");
    }
    Check(!fullQueue.TryPush(f6.batch), "action-ready batch is rejected by a full queue");
    Check(!rules.HasCapturedInputs(), "queue rejection does not capture the source");
    Check(
        rules.Evaluate(KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, 400, 41).kind
            == inputweaver::RuleEvaluationKind::Forward,
        "repeat is forwarded when initial action was not queued");
    Check(
        rules.Evaluate(KeyboardEvent(VK_F6, inputweaver::Transition::Up), true, 400, 42).kind
            == inputweaver::RuleEvaluationKind::Forward,
        "release is forwarded when initial action was not queued");
}

void TestQueueSpscConcurrency()
{
    constexpr std::size_t itemCount = 50'000;
    inputweaver::ActionQueue queue;

    std::thread producer([&queue]() {
        for (std::size_t index = 0; index < itemCount; ++index) {
            inputweaver::ActionBatch batch{};
            batch.sourceSequence = index;
            while (!queue.TryPush(batch)) {
                std::this_thread::yield();
            }
        }
    });

    bool ordered = true;
    std::size_t consumed = 0;
    while (consumed < itemCount) {
        inputweaver::ActionBatch batch{};
        if (!queue.TryPop(batch)) {
            std::this_thread::yield();
            continue;
        }
        ordered = ordered && batch.sourceSequence == consumed;
        ++consumed;
    }

    producer.join();
    Check(ordered, "concurrent SPSC queue preserves FIFO order");
    Check(queue.Empty(), "concurrent SPSC queue drains completely");
}

void TestRuntimeQueueFullErrorSignal()
{
    inputweaver::DiagnosticLog diagnosticLog;
    inputweaver::WindowsRuntimeSessionOptions options{};
    options.selfTag = static_cast<inputweaver::SelfTag>(0x51554555U);
    inputweaver::WindowsRuntimeSession runtime(options, nullptr, diagnosticLog);
    std::wstring componentError;
    const bool componentsCreated =
        inputweaver::RuntimeTestAccess::CreateEvents(runtime, componentError);
    Check(componentsCreated, "queue error test creates runtime components");
    if (!componentsCreated) {
        return;
    }

    for (std::size_t index = 0; index < inputweaver::kActionQueueCapacity; ++index) {
        inputweaver::ActionBatch batch{};
        batch.sourceSequence = index;
        Check(
            inputweaver::RuntimeTestAccess::Enqueue(runtime, batch),
            "queue error test fills the action queue");
    }

    inputweaver::ActionBatch rejected{};
    rejected.sourceSequence = inputweaver::kActionQueueCapacity;
    Check(
        inputweaver::RuntimeTestAccess::Schedule(runtime, rejected)
            == inputweaver::ActionQueuePushResult::Full,
        "runtime scheduler reports a full action queue");
    Check(
        WaitForSingleObject(runtime.ActionQueueErrorEvent(), 0) == WAIT_OBJECT_0,
        "first full action queue incident signals a visible runtime error");
    Check(
        runtime.Metrics().rejectedActionPushes == 1,
        "visible queue error retains the rejection metric");

    Check(
        inputweaver::RuntimeTestAccess::Schedule(runtime, rejected)
            == inputweaver::ActionQueuePushResult::Full,
        "later full action queue attempts remain fail-open");
    Check(
        WaitForSingleObject(runtime.ActionQueueErrorEvent(), 0) == WAIT_TIMEOUT,
        "repeated queue errors do not flood the console notification path");
}

void TestProducerDoneDrainCoordinator()
{
    inputweaver::ActionQueue queue;
    inputweaver::ActionBatch initial{};
    initial.sourceSequence = 100;
    Check(queue.TryPush(initial), "producer-done test publishes its initial batch");

    std::array<unsigned int, 3> cancellationCounts{};
    bool unexpectedSequence = false;
    bool firstLatePublished = false;
    bool finalLatePublished = false;
    unsigned int waitCount = 0;
    inputweaver::DrainUntilProducerDone(
        [&]() noexcept {
            inputweaver::ActionBatch batch{};
            while (queue.TryPop(batch)) {
                if (batch.sourceSequence >= 100 && batch.sourceSequence <= 102) {
                    ++cancellationCounts[static_cast<std::size_t>(batch.sourceSequence - 100)];
                } else {
                    unexpectedSequence = true;
                }
            }
        },
        [&]() noexcept {
            inputweaver::ActionBatch late{};
            if (waitCount++ == 0) {
                late.sourceSequence = 101;
                firstLatePublished = queue.TryPush(late);
                return inputweaver::ProducerDrainWaitResult::Continue;
            }
            late.sourceSequence = 102;
            finalLatePublished = queue.TryPush(late);
            return inputweaver::ProducerDrainWaitResult::ProducerDone;
        });

    Check(
        firstLatePublished && finalLatePublished && waitCount == 2,
        "producer-done test publishes work after both preceding drain passes");
    Check(
        !unexpectedSequence && cancellationCounts[0] == 1
            && cancellationCounts[1] == 1 && cancellationCounts[2] == 1,
        "producer-done final drain cancels every published batch exactly once");
    Check(queue.Empty(), "producer-done final drain leaves the queue empty");
}

void TestEmergencyStopAndCapturedRelease()
{
    constexpr DWORD targetPid = 500;
    inputweaver::FixedRuleEngine rules;

    const inputweaver::RuleEvaluation f6 = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Down), true, targetPid, 50);
    Check(rules.CommitCapture(f6), "emergency test starts with a captured F6");

    (void)rules.Evaluate(
        KeyboardEvent(VK_LCONTROL, inputweaver::Transition::Down), false, targetPid, 51);
    (void)rules.Evaluate(
        KeyboardEvent(VK_LSHIFT, inputweaver::Transition::Down), false, targetPid, 52);
    Check(
        rules.Evaluate(
            KeyboardEvent(VK_F12, inputweaver::Transition::Down, inputweaver::InputOrigin::SelfInjected),
            false,
            targetPid,
            53).kind == inputweaver::RuleEvaluationKind::Forward,
        "injected F12 cannot activate emergency stop");

    const inputweaver::RuleEvaluation emergency = rules.Evaluate(
        KeyboardEvent(VK_F12, inputweaver::Transition::Down), false, targetPid, 54);
    Check(
        emergency.kind == inputweaver::RuleEvaluationKind::EmergencyStop,
        "physical Ctrl+Shift+F12 requests emergency stop");
    Check(!rules.NewCapturesEnabled(), "emergency stop disables new captures");

    const inputweaver::RuleEvaluation f6Up = rules.Evaluate(
        KeyboardEvent(VK_F6, inputweaver::Transition::Up), false, targetPid, 55);
    Check(
        f6Up.kind == inputweaver::RuleEvaluationKind::SuppressCaptured,
        "captured release remains suppressed after emergency stop");
    Check(!rules.HasCapturedInputs(), "captured release drains after emergency stop");

    const inputweaver::RuleEvaluation f7 = rules.Evaluate(
        KeyboardEvent(VK_F7, inputweaver::Transition::Down), true, targetPid, 56);
    Check(
        f7.kind == inputweaver::RuleEvaluationKind::Forward,
        "new diagnostic action remains disabled after emergency stop");
}

void TestInjectorPreparationAndFailureHandling()
{
    constexpr ULONG_PTR selfTag = static_cast<ULONG_PTR>(0x6B524D31U);
    inputweaver::InputInjector injector(selfTag, &FakeSendInput);

    const inputweaver::ActionBatch keyboardBatch = inputweaver::MakeTapActionBatch(
        60, 0, 600, inputweaver::DeviceKind::Keyboard, VK_F7);
    const inputweaver::PreparedInputBatch keyboard = injector.Prepare(keyboardBatch);
    Check(keyboard.Succeeded() && keyboard.count == 2, "injector prepares a keyboard tap");
    Check(
        keyboard.inputs[0].type == INPUT_KEYBOARD
            && keyboard.inputs[1].type == INPUT_KEYBOARD,
        "keyboard tap uses keyboard INPUT records");
    Check(
        keyboard.inputs[0].ki.dwExtraInfo == selfTag
            && keyboard.inputs[1].ki.dwExtraInfo == selfTag,
        "every keyboard INPUT carries the self tag");
    Check(
        keyboard.inputs[0].ki.wScan != 0
            && (keyboard.inputs[0].ki.dwFlags & KEYEVENTF_SCANCODE) != 0
            && (keyboard.inputs[1].ki.dwFlags & KEYEVENTF_KEYUP) != 0,
        "keyboard tap has a scan code and paired release");

    const inputweaver::ActionBatch extendedBatch = inputweaver::MakeTapActionBatch(
        61, 0, 600, inputweaver::DeviceKind::Keyboard, VK_RIGHT);
    const inputweaver::PreparedInputBatch extended = injector.Prepare(extendedBatch);
    Check(
        extended.Succeeded()
            && (extended.inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0
            && (extended.inputs[1].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0,
        "extended keyboard controls carry the extended-key flag");

    const inputweaver::ActionBatch mouseBatch = inputweaver::MakeTapActionBatch(
        62, 0, 600, inputweaver::DeviceKind::Mouse, VK_MBUTTON);
    const inputweaver::PreparedInputBatch mouse = injector.Prepare(mouseBatch);
    Check(mouse.Succeeded() && mouse.count == 2, "injector prepares a middle-button tap");
    Check(
        mouse.inputs[0].mi.dwExtraInfo == selfTag
            && mouse.inputs[1].mi.dwExtraInfo == selfTag,
        "every mouse-button INPUT carries the self tag");
    Check(
        (mouse.inputs[0].mi.dwFlags & MOUSEEVENTF_MIDDLEDOWN) != 0
            && (mouse.inputs[1].mi.dwFlags & MOUSEEVENTF_MIDDLEUP) != 0,
        "middle-button tap has paired button flags");

    const inputweaver::ActionBatch moveBatch = inputweaver::MakeRelativeMouseMoveBatch(63, 600, 2, -1);
    const inputweaver::PreparedInputBatch move = injector.Prepare(moveBatch);
    Check(
        move.Succeeded() && move.count == 1
            && move.inputs[0].mi.dwExtraInfo == selfTag
            && (move.inputs[0].mi.dwFlags & MOUSEEVENTF_MOVE) != 0,
        "relative mouse INPUT carries the self tag");

    ResetFakeSend(FakeSendMode::Complete);
    const inputweaver::InjectionResult complete = injector.Inject(keyboardBatch);
    Check(complete.Succeeded(), "complete fake SendInput succeeds");
    Check(
        g_fakeSendState.callCount == 1 && g_fakeSendState.counts[0] == 2,
        "complete batch uses one SendInput call");

    ResetFakeSend(FakeSendMode::PartialFirstCall);
    const inputweaver::InjectionResult partial = injector.Inject(keyboardBatch);
    Check(
        partial.outcome == inputweaver::InjectionOutcome::SendPartial
            && partial.sent == 1 && partial.cleanupAttempted,
        "partial SendInput result requests cleanup");
    Check(
        partial.cleanupRequested == 1 && partial.cleanupSent == 1
            && g_fakeSendState.callCount == 2,
        "partial keyboard down is released by a second SendInput call");
    Check(
        (g_fakeSendState.inputs[1][0].ki.dwFlags & KEYEVENTF_KEYUP) != 0
            && g_fakeSendState.inputs[1][0].ki.dwExtraInfo == selfTag,
        "cleanup release retains the self tag");

    ResetFakeSend(FakeSendMode::Fail);
    const inputweaver::InjectionResult failed = injector.Inject(keyboardBatch);
    Check(
        failed.outcome == inputweaver::InjectionOutcome::SendFailed
            && failed.sent == 0 && failed.error == ERROR_ACCESS_DENIED,
        "failed SendInput preserves the immediate Win32 error");
    Check(!failed.cleanupAttempted, "zero inserted inputs require no cleanup");

    const inputweaver::InputInjector invalidInjector(0, &FakeSendInput);
    Check(
        !invalidInjector.Prepare(keyboardBatch).Succeeded(),
        "zero self tag is rejected before injection");

    ResetFakeSend(FakeSendMode::PartialThenCleanupFail);
    const inputweaver::InjectionResult unresolvedCleanup = injector.Inject(mouseBatch);
    Check(
        unresolvedCleanup.outcome == inputweaver::InjectionOutcome::SendPartial
            && unresolvedCleanup.error == ERROR_NOT_ENOUGH_MEMORY,
        "partial mouse send preserves its primary failure");
    Check(
        unresolvedCleanup.cleanupAttempted
            && unresolvedCleanup.cleanupRequested == 1
            && unresolvedCleanup.cleanupSent == 0
            && unresolvedCleanup.cleanupError == ERROR_RETRY,
        "cleanup failure is reported separately from the primary send error");

    inputweaver::ActionBatch invalidTransition = keyboardBatch;
    invalidTransition.actions[0].transition = inputweaver::Transition::Move;
    Check(
        !injector.Prepare(invalidTransition).Succeeded(),
        "keyboard movement is rejected as an invalid action");
    inputweaver::ActionBatch invalidMouse = mouseBatch;
    invalidMouse.actions[0].code = 0xFEU;
    Check(
        !injector.Prepare(invalidMouse).Succeeded(),
        "unsupported mouse buttons are rejected before SendInput");

    inputweaver::ActionBatch wheelBatch{};
    wheelBatch.actions[0] = {
        inputweaver::DeviceKind::Mouse, inputweaver::Transition::VerticalWheel, 0, 0, WHEEL_DELTA};
    wheelBatch.actions[1] = {
        inputweaver::DeviceKind::Mouse, inputweaver::Transition::HorizontalWheel, 0, -WHEEL_DELTA, 0};
    wheelBatch.actionCount = 2;
    const inputweaver::PreparedInputBatch wheels = injector.Prepare(wheelBatch);
    Check(
        wheels.Succeeded()
            && (wheels.inputs[0].mi.dwFlags & MOUSEEVENTF_WHEEL) != 0
            && (wheels.inputs[1].mi.dwFlags & MOUSEEVENTF_HWHEEL) != 0,
        "vertical and horizontal wheel actions prepare successfully");

    KBDLLHOOKSTRUCT preparedKeyboardHook{};
    preparedKeyboardHook.flags = LLKHF_INJECTED;
    preparedKeyboardHook.dwExtraInfo = keyboard.inputs[0].ki.dwExtraInfo;
    Check(
        inputweaver::ClassifyKeyboard(preparedKeyboardHook, selfTag)
            == inputweaver::InputOrigin::SelfInjected,
        "prepared keyboard tag closes the injector-to-classifier contract");
    MSLLHOOKSTRUCT preparedMouseHook{};
    preparedMouseHook.flags = LLMHF_INJECTED;
    preparedMouseHook.dwExtraInfo = mouse.inputs[0].mi.dwExtraInfo;
    Check(
        inputweaver::ClassifyMouse(preparedMouseHook, selfTag)
            == inputweaver::InputOrigin::SelfInjected,
        "prepared mouse tag closes the injector-to-classifier contract");
}

void TestInjectionCircuitBreaker()
{
    inputweaver::InjectionCircuitBreaker breaker(3);
    Check(!breaker.RecordFailure(), "first injection failure keeps the circuit closed");
    Check(!breaker.RecordFailure(), "second injection failure keeps the circuit closed");
    breaker.RecordSuccess();
    Check(
        breaker.ConsecutiveFailures() == 0 && !breaker.IsOpen(),
        "successful complete send resets the consecutive failure count");
    Check(!breaker.RecordFailure(), "failure count restarts after success");
    Check(!breaker.RecordFailure(), "second restarted failure keeps circuit closed");
    Check(breaker.RecordFailure(), "third consecutive failure opens the circuit");
    Check(breaker.IsOpen(), "opened injection circuit remains observable");
    breaker.RecordSuccess();
    Check(breaker.IsOpen(), "success does not silently rearm an opened circuit");
}

void TestWindowsRuntimeSessionFailureCircuit()
{
    constexpr ULONG_PTR selfTag = static_cast<ULONG_PTR>(0x554B5232U);
    inputweaver::DiagnosticLog diagnosticLog;
    inputweaver::WindowsRuntimeSessionOptions options{};
    options.selfTag = selfTag;
    inputweaver::WindowsRuntimeSession runtime(options, nullptr, diagnosticLog);
    std::wstring componentError;
    const bool componentsCreated =
        inputweaver::RuntimeTestAccess::CreateEvents(runtime, componentError);
    Check(componentsCreated, "runtime failure test creates components");
    if (!componentsCreated) {
        return;
    }
    inputweaver::RuntimeTestAccess::SetSendInput(runtime, &FakeSendInput);
    ResetFakeSend(FakeSendMode::Fail);

    const inputweaver::ActionBatch first = inputweaver::MakeTapActionBatch(
        80, 0, 0, inputweaver::DeviceKind::Keyboard, VK_F7);
    const inputweaver::ActionBatch second = inputweaver::MakeTapActionBatch(
        81, 0, 0, inputweaver::DeviceKind::Keyboard, VK_F7);
    const inputweaver::ActionBatch third = inputweaver::MakeTapActionBatch(
        82, 0, 0, inputweaver::DeviceKind::Keyboard, VK_F7);
    inputweaver::RuntimeTestAccess::ExecuteEligible(runtime, first);
    inputweaver::RuntimeTestAccess::ExecuteEligible(runtime, second);

    inputweaver::ActionBatch queuedFirst{};
    queuedFirst.sourceSequence = 83;
    inputweaver::ActionBatch queuedSecond{};
    queuedSecond.sourceSequence = 84;
    Check(
        inputweaver::RuntimeTestAccess::Enqueue(runtime, queuedFirst)
            && inputweaver::RuntimeTestAccess::Enqueue(runtime, queuedSecond),
        "runtime failure test queues work behind the third injection");
    inputweaver::RuntimeTestAccess::ExecuteEligible(runtime, third);

    const inputweaver::WindowsRuntimeSessionMetrics opened = runtime.Metrics();
    Check(
        g_fakeSendState.callCount == 3 && opened.injectionFailures == 3,
        "three failed runtime injections are counted once each");
    Check(
        opened.circuitBreakerOpen
            && !inputweaver::RuntimeTestAccess::NewCapturesEnabled(runtime),
        "third consecutive runtime failure opens the circuit and disables captures");
    Check(
        opened.cancelledBatches == 2
            && inputweaver::RuntimeTestAccess::QueueEmpty(runtime),
        "opening the runtime circuit cancels all remaining queued batches");

    const std::size_t callsBeforeRejectedBatch = g_fakeSendState.callCount;
    const inputweaver::ActionBatch rejected = inputweaver::MakeTapActionBatch(
        85, 0, 0, inputweaver::DeviceKind::Keyboard, VK_F7);
    inputweaver::RuntimeTestAccess::Process(runtime, rejected);
    const inputweaver::WindowsRuntimeSessionMetrics afterRejectedBatch = runtime.Metrics();
    Check(
        g_fakeSendState.callCount == callsBeforeRejectedBatch
            && afterRejectedBatch.cancelledBatches == 3,
        "an open runtime circuit rejects later work before SendInput");
}

void TestWindowsRuntimeSessionPersistentCleanupFailure()
{
    constexpr ULONG_PTR selfTag = static_cast<ULONG_PTR>(0x554B5233U);
    inputweaver::DiagnosticLog diagnosticLog;
    inputweaver::WindowsRuntimeSessionOptions options{};
    options.selfTag = selfTag;
    inputweaver::WindowsRuntimeSession runtime(options, nullptr, diagnosticLog);

    std::wstring eventError;
    const bool eventsCreated =
        inputweaver::RuntimeTestAccess::CreateEvents(runtime, eventError);
    Check(eventsCreated, "persistent cleanup test creates runtime events");
    if (!eventsCreated) {
        return;
    }
    inputweaver::RuntimeTestAccess::SetSendInput(runtime, &FakeSendInput);

    ResetFakeSend(FakeSendMode::PartialThenCleanupFail);
    const inputweaver::ActionBatch batch = inputweaver::MakeTapActionBatch(
        90, 0, 0, inputweaver::DeviceKind::Keyboard, VK_F7);
    inputweaver::RuntimeTestAccess::ExecuteEligible(runtime, batch);

    const inputweaver::WindowsRuntimeSessionMetrics beforeShutdownDrain = runtime.Metrics();
    Check(
        g_fakeSendState.callCount == 6
            && beforeShutdownDrain.injectionFailures == 5,
        "partial send and four failed owned-release attempts are counted before shutdown");
    Check(
        beforeShutdownDrain.circuitBreakerOpen
            && beforeShutdownDrain.unresolvedSyntheticReleases == 1
            && inputweaver::RuntimeTestAccess::ShutdownRequested(runtime)
            && !inputweaver::RuntimeTestAccess::NewCapturesEnabled(runtime),
        "persistent cleanup failure opens the circuit and requests shutdown with owned state");

    bool everyCleanupIsTaggedRelease = true;
    for (std::size_t callIndex = 1; callIndex < g_fakeSendState.callCount; ++callIndex) {
        const INPUT& input = g_fakeSendState.inputs[callIndex][0];
        everyCleanupIsTaggedRelease = everyCleanupIsTaggedRelease
            && g_fakeSendState.counts[callIndex] == 1
            && input.type == INPUT_KEYBOARD
            && (input.ki.dwFlags & KEYEVENTF_KEYUP) != 0
            && input.ki.dwExtraInfo == selfTag;
    }
    Check(
        everyCleanupIsTaggedRelease,
        "every immediate and owned cleanup attempt is a tagged key release");

    inputweaver::RuntimeTestAccess::SignalProducerDone(runtime);
    inputweaver::RuntimeTestAccess::DrainForShutdown(runtime);
    const inputweaver::WindowsRuntimeSessionMetrics afterShutdownDrain = runtime.Metrics();
    Check(
        g_fakeSendState.callCount == 9
            && afterShutdownDrain.injectionFailures == 8,
        "shutdown performs exactly three final bounded owned-release attempts");
    Check(
        afterShutdownDrain.unresolvedSyntheticReleases == 1
            && afterShutdownDrain.circuitBreakerOpen,
        "persistent shutdown cleanup failure remains visible in final runtime metrics");
}

void TestDiagnosticPrivacyAndBounds()
{
    inputweaver::HookDiagnosticRecord ordinary{};
    ordinary.sequence = 71;
    ordinary.device = inputweaver::DeviceKind::Keyboard;
    ordinary.transition = inputweaver::Transition::Down;
    ordinary.code = 'A';
    ordinary.scanCode = 30;
    ordinary.extraInfo = inputweaver::ExtraInfoCategory::OtherNonzero;
    inputweaver::ApplyPrivacyRedaction(ordinary);
    Check(
        ordinary.control == inputweaver::DiagnosticControl::OtherKeyboard
            && ordinary.code == 0 && ordinary.scanCode == 0,
        "ordinary letter diagnostics clear virtual key and scan code");
    const std::string ordinaryJson = inputweaver::FormatHookDiagnosticJson(ordinary);
    Check(
        ordinaryJson.find("OtherKeyboard") != std::string::npos
            && ordinaryJson.find("\"code\":65") == std::string::npos
            && ordinaryJson.find("\"scan\":30") == std::string::npos,
        "formatted diagnostics cannot reconstruct the ordinary letter code");

    constexpr inputweaver::SelfTag selfTag = static_cast<inputweaver::SelfTag>(0x7100U);
    Check(
        inputweaver::CategorizeExtraInfo(0, selfTag) == inputweaver::ExtraInfoCategory::Zero
            && inputweaver::CategorizeExtraInfo(selfTag, selfTag) == inputweaver::ExtraInfoCategory::OwnTag
            && inputweaver::CategorizeExtraInfo(static_cast<ULONG_PTR>(selfTag + 1U), selfTag)
                == inputweaver::ExtraInfoCategory::OtherNonzero,
        "extra information is reduced to three non-raw categories");
#if UINTPTR_MAX > UINT32_MAX
    const ULONG_PTR widenedSelfTag = static_cast<ULONG_PTR>(selfTag) |
                                     (static_cast<ULONG_PTR>(0xDEADBEEFU) << 32U);
    Check(
        inputweaver::CategorizeExtraInfo(widenedSelfTag, selfTag) ==
            inputweaver::ExtraInfoCategory::OwnTag,
        "diagnostic tag classification compares the portable low 32 bits");
#endif

    inputweaver::HookDiagnosticRecord ordinaryMouseMove{};
    ordinaryMouseMove.device = inputweaver::DeviceKind::Mouse;
    ordinaryMouseMove.transition = inputweaver::Transition::Move;
    Check(
        !inputweaver::ShouldPublishHookDiagnostic(ordinaryMouseMove, false) &&
            inputweaver::ShouldPublishHookDiagnostic(ordinaryMouseMove, true),
        "ordinary mouse movement is logged only in explicit input trace mode");
    ordinaryMouseMove.origin = inputweaver::InputOrigin::SelfInjected;
    Check(
        inputweaver::ShouldPublishHookDiagnostic(ordinaryMouseMove, false),
        "operational logging retains self-injected mouse movement");
    inputweaver::HookDiagnosticRecord matchedRule{};
    matchedRule.ruleId = 1;
    Check(
        inputweaver::ShouldPublishHookDiagnostic(matchedRule, false),
        "operational logging retains matched rules");

    inputweaver::SpscDiagnosticRing<inputweaver::HookDiagnosticRecord, 3> ring;
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        inputweaver::HookDiagnosticRecord record{};
        record.sequence = sequence;
        Check(ring.TryPush(record), "diagnostic ring accepts every capacity slot");
    }
    inputweaver::HookDiagnosticRecord overflow{};
    Check(!ring.TryPush(overflow), "diagnostic ring rejects the item after capacity");
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        inputweaver::HookDiagnosticRecord record{};
        Check(ring.TryPop(record) && record.sequence == sequence, "diagnostic ring preserves FIFO order");
    }
    Check(ring.Empty(), "diagnostic ring drains completely");

    Check(
        inputweaver::JsonlAppendFits(inputweaver::kMaximumJsonlBytes - 10, 10),
        "JSONL writer may fill exactly to its byte limit");
    Check(
        !inputweaver::JsonlAppendFits(inputweaver::kMaximumJsonlBytes - 10, 11)
            && !inputweaver::JsonlAppendFits(inputweaver::kMaximumJsonlBytes + 1, 0),
        "JSONL writer rejects all appends beyond its byte limit");

    inputweaver::DiagnosticLog disabledLog;
    std::wstring disabledError;
    Check(
        disabledLog.Start(L"", disabledError) && !disabledLog.Enabled(),
        "an omitted log path does not start the logging worker");
    Check(
        disabledLog.TryPushHook({}) && disabledLog.TryPushInjection({}) &&
            disabledLog.DroppedHookRecords() == 0 &&
            disabledLog.DroppedInjectionRecords() == 0,
        "disabled logging accepts no-op publications without drop noise");
    disabledLog.Stop();

    wchar_t temporaryDirectory[MAX_PATH]{};
    wchar_t temporaryFile[MAX_PATH]{};
    const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporaryDirectory);
    const bool temporaryPathReady = temporaryLength != 0 && temporaryLength < MAX_PATH &&
                                    GetTempFileNameW(
                                        temporaryDirectory, L"iwv", 0, temporaryFile) != 0;
    Check(temporaryPathReady, "test obtains a temporary JSONL path");
    if (temporaryPathReady) {
        inputweaver::DiagnosticLog boundedLog;
        std::wstring errorMessage;
        const bool started = boundedLog.Start(temporaryFile, errorMessage, 1024);
        Check(started, "bounded JSONL diagnostic worker starts");
        if (started) {
            for (std::uint64_t sequence = 0; sequence < 20; ++sequence) {
                inputweaver::HookDiagnosticRecord record{};
                record.sequence = sequence;
                record.device = inputweaver::DeviceKind::Keyboard;
                record.transition = inputweaver::Transition::Down;
                record.code = VK_F6;
                (void)boundedLog.TryPushHook(record);
            }
            boundedLog.Stop();
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            const bool sizeAvailable = GetFileAttributesExW(
                temporaryFile, GetFileExInfoStandard, &attributes) != FALSE;
            const std::uint64_t fileSize = sizeAvailable
                ? (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32U) |
                      attributes.nFileSizeLow
                : 0;
            Check(sizeAvailable, "bounded JSONL file size is readable");
            Check(
                boundedLog.JsonlTruncated() && boundedLog.JsonlBytesWritten() <= 1024 &&
                    fileSize == boundedLog.JsonlBytesWritten(),
                "JSONL writer stops without exceeding its configured acceptance limit");
        }
        DeleteFileW(temporaryFile);
    }
}

void TestProcessLocator()
{
    constexpr DWORD pathCapacity = 32768;
    std::array<wchar_t, pathCapacity> modulePathBuffer{};
    const DWORD modulePathLength = GetModuleFileNameW(
        nullptr, modulePathBuffer.data(), pathCapacity);
    Check(
        modulePathLength != 0 && modulePathLength < pathCapacity,
        "test resolves its executable path");
    if (modulePathLength == 0 || modulePathLength >= pathCapacity) {
        return;
    }

    const std::wstring modulePath(
        modulePathBuffer.data(), static_cast<std::size_t>(modulePathLength));
    const std::size_t separator = modulePath.find_last_of(L"\\/");
    const std::wstring basename = separator == std::wstring::npos
        ? modulePath
        : modulePath.substr(separator + 1U);

    const inputweaver::win32::LocateResult byPath =
        inputweaver::win32::LocateExecutable(modulePath);
    const auto containsCurrentProcess = [](const inputweaver::win32::LocateResult& result) {
        return std::any_of(
            result.matches.begin(),
            result.matches.end(),
            [](const inputweaver::win32::LocatedProcess& process) {
                return process.processId == GetCurrentProcessId();
            });
    };
    Check(
        byPath.status != inputweaver::win32::LocateStatus::Error &&
            containsCurrentProcess(byPath),
        "absolute-path target discovery finds the current executable");

    const inputweaver::win32::LocateResult byBasename =
        inputweaver::win32::LocateExecutable(basename);
    Check(
        byBasename.status != inputweaver::win32::LocateStatus::Error &&
            containsCurrentProcess(byBasename),
        "basename target discovery is case-insensitive and finds the current executable");

    const std::wstring missing = L"InputWeaver.NoSuchProcess." +
                                 std::to_wstring(GetCurrentProcessId()) + L".exe";
    Check(
        inputweaver::win32::LocateExecutable(missing).status ==
            inputweaver::win32::LocateStatus::None,
        "a missing executable selector returns the waiting state");
    Check(
        inputweaver::win32::LocateExecutable(L"relative\\target.exe").status ==
            inputweaver::win32::LocateStatus::Error,
        "a path selector must be absolute");

    std::wstring embeddedNull = L"InputWeaverTests.exe";
    embeddedNull.push_back(L'\0');
    embeddedNull += L"ignored";
    Check(
        inputweaver::win32::LocateExecutable(embeddedNull).status ==
            inputweaver::win32::LocateStatus::Error,
        "an executable selector cannot contain an embedded NUL");
}

void TestProcessContextValidation()
{
    inputweaver::TargetProcessContext context;
    const inputweaver::ProcessContextResult invalid = context.Initialize(0);
    Check(
        invalid.error == inputweaver::ProcessContextError::InvalidPid && !context.IsValid(),
        "zero target PID is rejected");

    const inputweaver::IntegrityLevelResult currentIntegrity =
        inputweaver::QueryProcessIntegrityLevel(GetCurrentProcess());
    if (!currentIntegrity.succeeded) {
        std::cerr << "Current integrity query Win32 error: "
                  << currentIntegrity.win32Error << '\n';
    }
    Check(currentIntegrity.succeeded, "current process integrity level is queryable");
    Check(
        inputweaver::IsTargetIntegrityCompatible(
            SECURITY_MANDATORY_MEDIUM_RID,
            SECURITY_MANDATORY_LOW_RID),
        "a lower-integrity target is compatible");
    Check(
        inputweaver::IsTargetIntegrityCompatible(
            SECURITY_MANDATORY_MEDIUM_RID,
            SECURITY_MANDATORY_MEDIUM_RID),
        "an equal-integrity target is compatible");
    Check(
        !inputweaver::IsTargetIntegrityCompatible(
            SECURITY_MANDATORY_MEDIUM_RID,
            SECURITY_MANDATORY_HIGH_RID),
        "a higher-integrity target is rejected");

    const inputweaver::ProcessContextResult currentProcess =
        context.Initialize(GetCurrentProcessId());
    Check(
        currentProcess.Succeeded() && context.IsValid()
            && context.TargetPid() == GetCurrentProcessId(),
        "a valid same-integrity process can become the target");
    context.Reset();
}

void TestTargetProcessLifecycle()
{
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT directoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    Check(
        directoryLength != 0 && directoryLength < MAX_PATH,
        "test resolves the Windows system directory");
    if (directoryLength == 0 || directoryLength >= MAX_PATH) {
        return;
    }
    std::wstring targetPath(systemDirectory, directoryLength);
    targetPath += L"\\cmd.exe";
    std::wstring commandLine = L"\"" + targetPath + L"\" /d /c exit 0";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool created = CreateProcessW(
        targetPath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        nullptr,
        &startup,
        &process) != FALSE;
    Check(created, "test starts a suspended target process");
    if (!created) {
        return;
    }

    inputweaver::TargetProcessContext context;
    const inputweaver::ProcessContextResult mismatched = context.Initialize(
        process.dwProcessId, targetPath + L".wrong");
    Check(
        mismatched.error == inputweaver::ProcessContextError::TargetImageMismatch,
        "target context rejects a reused PID with the wrong image path");
    const inputweaver::ProcessContextResult initialized =
        context.Initialize(process.dwProcessId, targetPath);
    Check(initialized.Succeeded(), "target context accepts a live same-integrity process");
    const DWORD resumeResult = ResumeThread(process.hThread);
    Check(resumeResult != static_cast<DWORD>(-1), "test resumes the target process");
    DWORD processWait = WaitForSingleObject(process.hProcess, 5000);
    if (processWait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 10);
        processWait = WaitForSingleObject(process.hProcess, 5000);
    }
    Check(processWait == WAIT_OBJECT_0, "target process exits normally");
    if (initialized.Succeeded()) {
        DWORD livenessError = ERROR_SUCCESS;
        Check(
            !context.IsTargetAlive(&livenessError)
                && livenessError == ERROR_PROCESS_ABORTED,
            "retained target handle becomes signaled after the process exits");
        Check(
            !context.IsTargetForeground(),
            "a signaled retained target disables foreground routing");
        const inputweaver::ScreenPoint point{0, 0};
        Check(
            !context.IsTargetPointerTarget(point),
            "a signaled retained target disables pointer routing");
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void TestShutdownGraceWithFakeClock()
{
    inputweaver::ShutdownGraceWindow grace(2000);
    Check(!grace.Expired(100), "inactive shutdown grace is not expired");
    grace.Begin(100);
    Check(!grace.Expired(2099), "captured-release grace remains active before its deadline");
    Check(grace.RemainingSlice(150, 50) == 50, "shutdown grace caps polling slices");
    Check(grace.RemainingSlice(2080, 50) == 20, "shutdown grace reports its final partial slice");
    Check(grace.Expired(2100), "captured-release grace expires at its deadline");
    Check(grace.RemainingSlice(2100, 50) == 0, "expired shutdown grace has no remaining wait");
}

void TestWindowsRuntimeSessionLifecycle()
{
    inputweaver::DiagnosticLog diagnosticLog;
    std::wstring errorMessage;
    const bool logStarted = diagnosticLog.Start(L"", errorMessage);
    Check(logStarted && !diagnosticLog.Enabled(), "runtime lifecycle starts without a logging worker");
    if (!logStarted) {
        return;
    }

    {
        constexpr ULONG_PTR selfTag = static_cast<ULONG_PTR>(0x49575631U);
        inputweaver::WindowsRuntimeSessionOptions options{};
        options.selfTag = selfTag;
        inputweaver::WindowsRuntimeSession runtime(options, nullptr, diagnosticLog);
        const bool runtimeStarted = runtime.Start(errorMessage);
        Check(runtimeStarted, "observer runtime installs both low-level hooks");
        if (runtimeStarted) {
            runtime.RequestStop();
            runtime.Wait();
            Check(
                WaitForSingleObject(runtime.StoppedEvent(), 0) == WAIT_OBJECT_0,
                "observer runtime signals hook-thread termination");
            const inputweaver::WindowsRuntimeSessionMetrics metrics = runtime.Metrics();
            Check(
                metrics.suppressedEvents == 0 && metrics.queuedBatches == 0 &&
                    metrics.unresolvedSyntheticReleases == 0,
                "observer runtime stops without suppression, actions, or owned state");
        }
    }
    diagnosticLog.Stop();
}

} // namespace

int main()
{
    TestOriginClassification();
    TestBatchBuilders();
    TestKeyboardRulesAndRecursion();
    TestCrossDeviceRulesAndRecursion();
    TestOutputConflictAndGeneration();
    TestQueueCapacityAndCaptureCommit();
    TestQueueSpscConcurrency();
    TestRuntimeQueueFullErrorSignal();
    TestProducerDoneDrainCoordinator();
    TestEmergencyStopAndCapturedRelease();
    TestInjectorPreparationAndFailureHandling();
    TestInjectionCircuitBreaker();
    TestWindowsRuntimeSessionFailureCircuit();
    TestWindowsRuntimeSessionPersistentCleanupFailure();
    TestDiagnosticPrivacyAndBounds();
    TestProcessLocator();
    TestProcessContextValidation();
    TestTargetProcessLifecycle();
    TestShutdownGraceWithFakeClock();
    TestWindowsRuntimeSessionLifecycle();

    if (g_failureCount != 0) {
        std::cerr << g_failureCount << " Phase 1 automated test(s) failed.\n";
        return 1;
    }

    std::cout << "All Phase 1 automated tests passed.\n";
    return 0;
}
