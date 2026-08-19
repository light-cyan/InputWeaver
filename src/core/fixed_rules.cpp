#include "fixed_rules.hpp"

namespace ukr {
namespace {

struct RuleDefinition {
    RuleId id{ RuleId::None };
    DeviceKind outputDevice{};
    DWORD outputCode{};
};

[[nodiscard]] RuleDefinition FindRule(const InputEvent& event) noexcept
{
    if (event.device == DeviceKind::Keyboard) {
        switch (event.code) {
        case VK_F6:
            return { RuleId::F6ToF7, DeviceKind::Keyboard, VK_F7 };
        case VK_F7:
            return { RuleId::F7ToF8, DeviceKind::Keyboard, VK_F8 };
        case VK_F9:
            return { RuleId::F9ToMiddle, DeviceKind::Mouse, VK_MBUTTON };
        default:
            return {};
        }
    }

    if (event.device == DeviceKind::Mouse && event.code == VK_MBUTTON) {
        return { RuleId::MiddleToF10, DeviceKind::Keyboard, VK_F10 };
    }

    return {};
}

} // namespace

ActionBatch MakeTapActionBatch(
    unsigned long long sourceSequence,
    unsigned long long outputStateGeneration,
    DWORD targetPid,
    DeviceKind outputDevice,
    DWORD outputCode,
    bool requiresPointerTarget) noexcept
{
    ActionBatch batch{};
    batch.sourceSequence = sourceSequence;
    batch.outputStateGeneration = outputStateGeneration;
    batch.targetPid = targetPid;
    batch.outputDevice = outputDevice;
    batch.outputCode = outputCode;
    batch.requiresPointerTarget = requiresPointerTarget || outputDevice == DeviceKind::Mouse;
    batch.actions[0] = { outputDevice, Transition::Down, outputCode, 0, 0 };
    batch.actions[1] = { outputDevice, Transition::Up, outputCode, 0, 0 };
    batch.actionCount = 2;
    return batch;
}

ActionBatch MakeRelativeMouseMoveBatch(
    unsigned long long sourceSequence,
    DWORD targetPid,
    LONG valueX,
    LONG valueY) noexcept
{
    ActionBatch batch{};
    batch.sourceSequence = sourceSequence;
    batch.targetPid = targetPid;
    batch.outputDevice = DeviceKind::Mouse;
    batch.requiresPointerTarget = true;
    batch.actions[0] = { DeviceKind::Mouse, Transition::Move, 0, valueX, valueY };
    batch.actionCount = 1;
    return batch;
}

FixedRuleEngine::FixedRuleEngine() noexcept
{
    for (auto& state : publishedOutputState_) {
        state.store(PackPhysicalOutputState(0, false), std::memory_order_relaxed);
    }
}

RuleEvaluation FixedRuleEngine::Evaluate(
    const InputEvent& event,
    bool diagnosticModeActive,
    DWORD targetPid,
    unsigned long long sourceSequence) noexcept
{
    RuleEvaluation evaluation{};
    if (event.origin != InputOrigin::PhysicalCandidate) {
        return evaluation;
    }

    const PhysicalEdge edge = UpdatePhysicalState(event);
    if (!edge.valid) {
        return evaluation;
    }

    if (event.device == DeviceKind::Keyboard
        && event.transition == Transition::Down
        && event.code == VK_F12
        && edge.firstDown
        && ControlDown()
        && ShiftDown()) {
        DisableNewCaptures();
        evaluation.kind = RuleEvaluationKind::EmergencyStop;
        return evaluation;
    }

    if (IsCaptured(event.device, event.code)) {
        evaluation.kind = RuleEvaluationKind::SuppressCaptured;
        evaluation.rule = CapturedRule(event.device, event.code);
        if (event.transition == Transition::Up) {
            SetCaptured(event.device, event.code, false, RuleId::None);
        }
        return evaluation;
    }

    if (!diagnosticModeActive
        || !NewCapturesEnabled()
        || event.transition != Transition::Down
        || !edge.firstDown) {
        return evaluation;
    }

    const RuleDefinition rule = FindRule(event);
    evaluation.rule = rule.id;
    if (rule.id == RuleId::None || IsPhysicalDown(rule.outputDevice, rule.outputCode)) {
        return evaluation;
    }

    evaluation.kind = RuleEvaluationKind::ActionReady;
    evaluation.captureDevice = event.device;
    evaluation.captureCode = event.code;
    evaluation.batch = MakeTapActionBatch(
        sourceSequence,
        PhysicalGeneration(rule.outputDevice, rule.outputCode),
        targetPid,
        rule.outputDevice,
        rule.outputCode,
        event.device == DeviceKind::Mouse);
    return evaluation;
}

bool FixedRuleEngine::CommitCapture(const RuleEvaluation& evaluation) noexcept
{
    if (evaluation.kind != RuleEvaluationKind::ActionReady
        || !NewCapturesEnabled()
        || evaluation.captureCode >= kControlCodeCount
        || !IsPhysicalDown(evaluation.captureDevice, evaluation.captureCode)
        || IsCaptured(evaluation.captureDevice, evaluation.captureCode)) {
        return false;
    }

    SetCaptured(
        evaluation.captureDevice,
        evaluation.captureCode,
        true,
        evaluation.rule);
    return true;
}

void FixedRuleEngine::DisableNewCaptures() noexcept
{
    newCapturesEnabled_.store(false, std::memory_order_release);
}

void FixedRuleEngine::SeedPhysicalState(
    DeviceKind device,
    DWORD code,
    bool down) noexcept
{
    if (code >= kControlCodeCount) {
        return;
    }
    auto& states = device == DeviceKind::Keyboard ? keyboardDown_ : mouseDown_;
    auto& generations = device == DeviceKind::Keyboard
        ? keyboardGeneration_
        : mouseGeneration_;
    const std::size_t index = static_cast<std::size_t>(code);
    if (states[index] != down) {
        states[index] = down;
        ++generations[index];
        PublishOutputState(device, code);
    }
}

bool FixedRuleEngine::NewCapturesEnabled() const noexcept
{
    return newCapturesEnabled_.load(std::memory_order_acquire);
}

bool FixedRuleEngine::HasCapturedInputs() const noexcept
{
    return capturedCount_.load(std::memory_order_acquire) != 0;
}

unsigned long long FixedRuleEngine::PackedOutputState(
    DeviceKind device,
    DWORD code) const noexcept
{
    const std::size_t index = PublishedOutputIndex(device, code);
    if (index == kPublishedOutputCount) {
        return PackPhysicalOutputState(0, false);
    }

    return publishedOutputState_[index].load(std::memory_order_acquire);
}

bool FixedRuleEngine::CanInject(const ActionBatch& batch) const noexcept
{
    const std::size_t index = PublishedOutputIndex(batch.outputDevice, batch.outputCode);
    if (index == kPublishedOutputCount) {
        return true;
    }

    const unsigned long long state =
        publishedOutputState_[index].load(std::memory_order_acquire);
    return !PhysicalOutputIsDown(state)
        && PhysicalOutputGeneration(state) == batch.outputStateGeneration;
}

FixedRuleEngine::PhysicalEdge FixedRuleEngine::UpdatePhysicalState(
    const InputEvent& event) noexcept
{
    if (event.code >= kControlCodeCount
        || (event.transition != Transition::Down && event.transition != Transition::Up)) {
        return {};
    }

    auto& down = event.device == DeviceKind::Keyboard ? keyboardDown_ : mouseDown_;
    auto& generation = event.device == DeviceKind::Keyboard
        ? keyboardGeneration_
        : mouseGeneration_;

    const std::size_t code = static_cast<std::size_t>(event.code);
    if (event.transition == Transition::Down) {
        if (down[code]) {
            return { true, false };
        }
        down[code] = true;
        ++generation[code];
        PublishOutputState(event.device, event.code);
        return { true, true };
    }

    if (down[code]) {
        down[code] = false;
        ++generation[code];
        PublishOutputState(event.device, event.code);
    }
    return { true, false };
}

bool FixedRuleEngine::IsPhysicalDown(DeviceKind device, DWORD code) const noexcept
{
    if (code >= kControlCodeCount) {
        return false;
    }
    return device == DeviceKind::Keyboard
        ? keyboardDown_[static_cast<std::size_t>(code)]
        : mouseDown_[static_cast<std::size_t>(code)];
}

unsigned long long FixedRuleEngine::PhysicalGeneration(
    DeviceKind device,
    DWORD code) const noexcept
{
    if (code >= kControlCodeCount) {
        return 0;
    }
    return device == DeviceKind::Keyboard
        ? keyboardGeneration_[static_cast<std::size_t>(code)]
        : mouseGeneration_[static_cast<std::size_t>(code)];
}

bool FixedRuleEngine::IsCaptured(DeviceKind device, DWORD code) const noexcept
{
    if (code >= kControlCodeCount) {
        return false;
    }
    return device == DeviceKind::Keyboard
        ? keyboardCaptured_[static_cast<std::size_t>(code)]
        : mouseCaptured_[static_cast<std::size_t>(code)];
}

RuleId FixedRuleEngine::CapturedRule(DeviceKind device, DWORD code) const noexcept
{
    if (code >= kControlCodeCount) {
        return RuleId::None;
    }
    return device == DeviceKind::Keyboard
        ? keyboardCapturedRule_[static_cast<std::size_t>(code)]
        : mouseCapturedRule_[static_cast<std::size_t>(code)];
}

void FixedRuleEngine::SetCaptured(
    DeviceKind device,
    DWORD code,
    bool captured,
    RuleId rule) noexcept
{
    if (code >= kControlCodeCount) {
        return;
    }

    auto& capturedState = device == DeviceKind::Keyboard
        ? keyboardCaptured_
        : mouseCaptured_;
    auto& capturedRule = device == DeviceKind::Keyboard
        ? keyboardCapturedRule_
        : mouseCapturedRule_;
    const std::size_t index = static_cast<std::size_t>(code);
    if (capturedState[index] == captured) {
        return;
    }

    capturedState[index] = captured;
    capturedRule[index] = captured ? rule : RuleId::None;
    if (captured) {
        capturedCount_.fetch_add(1, std::memory_order_release);
    } else {
        capturedCount_.fetch_sub(1, std::memory_order_release);
    }
}

void FixedRuleEngine::PublishOutputState(DeviceKind device, DWORD code) noexcept
{
    const std::size_t index = PublishedOutputIndex(device, code);
    if (index == kPublishedOutputCount) {
        return;
    }

    publishedOutputState_[index].store(
        PackPhysicalOutputState(
            PhysicalGeneration(device, code),
            IsPhysicalDown(device, code)),
        std::memory_order_release);
}

bool FixedRuleEngine::ControlDown() const noexcept
{
    return IsPhysicalDown(DeviceKind::Keyboard, VK_CONTROL)
        || IsPhysicalDown(DeviceKind::Keyboard, VK_LCONTROL)
        || IsPhysicalDown(DeviceKind::Keyboard, VK_RCONTROL);
}

bool FixedRuleEngine::ShiftDown() const noexcept
{
    return IsPhysicalDown(DeviceKind::Keyboard, VK_SHIFT)
        || IsPhysicalDown(DeviceKind::Keyboard, VK_LSHIFT)
        || IsPhysicalDown(DeviceKind::Keyboard, VK_RSHIFT);
}

std::size_t FixedRuleEngine::PublishedOutputIndex(
    DeviceKind device,
    DWORD code) noexcept
{
    if (device == DeviceKind::Keyboard) {
        switch (code) {
        case VK_F7:
            return 0;
        case VK_F8:
            return 1;
        case VK_F10:
            return 3;
        default:
            return kPublishedOutputCount;
        }
    }

    return code == VK_MBUTTON ? 2 : kPublishedOutputCount;
}

} // namespace ukr
