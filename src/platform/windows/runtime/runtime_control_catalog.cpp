#include "runtime_control_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace inputweaver::win32 {
namespace {

constexpr std::uint8_t kInputCapabilities =
    ToControlUseBits(ControlUse::EventSource)
    | ToControlUseBits(ControlUse::PhysicalState);
constexpr std::uint8_t kOutputCapabilities =
    ToControlUseBits(ControlUse::OutputDownUp)
        | ToControlUseBits(ControlUse::OutputAgain);

[[nodiscard]] WindowsVirtualKey KeyboardUsageToVirtualKey(
    std::uint32_t usage) noexcept
{
    if (usage >= 0x04U && usage <= 0x1dU) {
        return static_cast<WindowsVirtualKey>('A') + usage - 0x04U;
    }
    if (usage >= 0x1eU && usage <= 0x26U) {
        return static_cast<WindowsVirtualKey>('1') + usage - 0x1eU;
    }
    if (usage == 0x27U) {
        return static_cast<WindowsVirtualKey>('0');
    }
    if (usage >= 0x3aU && usage <= 0x45U) {
        return VK_F1 + usage - 0x3aU;
    }
    if (usage >= 0x68U && usage <= 0x73U) {
        return VK_F13 + usage - 0x68U;
    }
    if (usage >= 0x59U && usage <= 0x61U) {
        return VK_NUMPAD1 + usage - 0x59U;
    }
    switch (usage) {
    case 0x28U:
        return VK_RETURN;
    case 0x29U:
        return VK_ESCAPE;
    case 0x2aU:
        return VK_BACK;
    case 0x2bU:
        return VK_TAB;
    case 0x2cU:
        return VK_SPACE;
    case 0x39U:
        return VK_CAPITAL;
    case 0x47U:
        return VK_SCROLL;
    case 0x48U:
        return VK_PAUSE;
    case 0x49U:
        return VK_INSERT;
    case 0x4aU:
        return VK_HOME;
    case 0x4bU:
        return VK_PRIOR;
    case 0x4cU:
        return VK_DELETE;
    case 0x4dU:
        return VK_END;
    case 0x4eU:
        return VK_NEXT;
    case 0x4fU:
        return VK_RIGHT;
    case 0x50U:
        return VK_LEFT;
    case 0x51U:
        return VK_DOWN;
    case 0x52U:
        return VK_UP;
    case 0x53U:
        return VK_NUMLOCK;
    case 0x54U:
        return VK_DIVIDE;
    case 0x55U:
        return VK_MULTIPLY;
    case 0x56U:
        return VK_SUBTRACT;
    case 0x57U:
        return VK_ADD;
    case 0x62U:
        return VK_NUMPAD0;
    case 0x63U:
        return VK_DECIMAL;
    case 0xe0U:
        return VK_LCONTROL;
    case 0xe1U:
        return VK_LSHIFT;
    case 0xe2U:
        return VK_LMENU;
    case 0xe4U:
        return VK_RCONTROL;
    case 0xe5U:
        return VK_RSHIFT;
    case 0xe6U:
        return VK_RMENU;
    default:
        return 0U;
    }
}

[[nodiscard]] WindowsVirtualKey ConsumerUsageToVirtualKey(
    std::uint32_t usage) noexcept
{
    switch (usage) {
    case 0x00cdU:
        return VK_MEDIA_PLAY_PAUSE;
    case 0x00b5U:
        return VK_MEDIA_NEXT_TRACK;
    case 0x00b6U:
        return VK_MEDIA_PREV_TRACK;
    case 0x00b7U:
        return VK_MEDIA_STOP;
    case 0x00e2U:
        return VK_VOLUME_MUTE;
    case 0x00e9U:
        return VK_VOLUME_UP;
    case 0x00eaU:
        return VK_VOLUME_DOWN;
    default:
        return 0U;
    }
}

[[nodiscard]] WindowsVirtualKey MouseUsageToVirtualKey(
    std::uint32_t usage) noexcept
{
    switch (usage) {
    case 1U:
        return VK_LBUTTON;
    case 2U:
        return VK_RBUTTON;
    case 3U:
        return VK_MBUTTON;
    case 4U:
        return VK_XBUTTON1;
    case 5U:
        return VK_XBUTTON2;
    default:
        return 0U;
    }
}

[[nodiscard]] bool IsMouseVirtualKey(WindowsVirtualKey virtualKey) noexcept
{
    return virtualKey == VK_LBUTTON
        || virtualKey == VK_RBUTTON
        || virtualKey == VK_MBUTTON
        || virtualKey == VK_XBUTTON1
        || virtualKey == VK_XBUTTON2;
}

[[nodiscard]] std::uint32_t ScanQualifierFromMappedCode(UINT scanCode) noexcept
{
    const UINT prefix = scanCode & 0xff00U;
    if (prefix == 0xe000U) {
        return kWindowsScanCodeQualifierE0;
    }
    if (prefix == 0xe100U) {
        return kWindowsScanCodeQualifierE1;
    }
    return kControlQualifierNone;
}

[[nodiscard]] std::uint32_t NativeScanQualifier(
    const WindowsNativeInputEvent& event) noexcept
{
    if ((event.hookFlags & LLKHF_EXTENDED) != 0U) {
        return kWindowsScanCodeQualifierE0;
    }
    return event.virtualKey == VK_PAUSE && event.scanCode == 0x45U
        ? kWindowsScanCodeQualifierE1
        : kControlQualifierNone;
}

void FillKeyboardRecipe(
    WindowsControlBinding& binding,
    WindowsVirtualKey virtualKey,
    bool preserveVirtualKey) noexcept
{
    binding.kind = WindowsControlKind::Keyboard;
    binding.virtualKey = virtualKey;
    const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
    binding.scanCode = scanCode & 0xffU;
    binding.scanQualifier = ScanQualifierFromMappedCode(scanCode);
    binding.capabilities = kInputCapabilities;
    if (preserveVirtualKey) {
        binding.outputRecipe.kind = WindowsOutputKind::KeyboardVirtualKey;
        binding.outputRecipe.virtualKey = virtualKey;
    } else if (binding.scanCode != 0U
        && binding.scanQualifier != kWindowsScanCodeQualifierE1) {
        binding.outputRecipe.kind = WindowsOutputKind::KeyboardScanCode;
        binding.outputRecipe.scanCode = binding.scanCode;
        binding.outputRecipe.extendedScanCode = binding.scanQualifier
            == kWindowsScanCodeQualifierE0;
    } else {
        binding.outputRecipe.kind = WindowsOutputKind::KeyboardVirtualKey;
        binding.outputRecipe.virtualKey = virtualKey;
    }
    binding.capabilities = static_cast<std::uint8_t>(
        binding.capabilities | kOutputCapabilities);
    binding.initialStateQueryable = virtualKey != 0U;
}

void FillMouseRecipe(
    WindowsControlBinding& binding,
    WindowsVirtualKey virtualKey) noexcept
{
    binding.kind = WindowsControlKind::MouseButton;
    binding.virtualKey = virtualKey;
    binding.outputRecipe.kind = WindowsOutputKind::MouseButton;
    binding.outputRecipe.virtualKey = virtualKey;
    switch (virtualKey) {
    case VK_LBUTTON:
        binding.outputRecipe.mouseDownFlags = MOUSEEVENTF_LEFTDOWN;
        binding.outputRecipe.mouseUpFlags = MOUSEEVENTF_LEFTUP;
        break;
    case VK_RBUTTON:
        binding.outputRecipe.mouseDownFlags = MOUSEEVENTF_RIGHTDOWN;
        binding.outputRecipe.mouseUpFlags = MOUSEEVENTF_RIGHTUP;
        break;
    case VK_MBUTTON:
        binding.outputRecipe.mouseDownFlags = MOUSEEVENTF_MIDDLEDOWN;
        binding.outputRecipe.mouseUpFlags = MOUSEEVENTF_MIDDLEUP;
        break;
    case VK_XBUTTON1:
        binding.outputRecipe.mouseDownFlags = MOUSEEVENTF_XDOWN;
        binding.outputRecipe.mouseUpFlags = MOUSEEVENTF_XUP;
        binding.outputRecipe.mouseData = XBUTTON1;
        break;
    case VK_XBUTTON2:
        binding.outputRecipe.mouseDownFlags = MOUSEEVENTF_XDOWN;
        binding.outputRecipe.mouseUpFlags = MOUSEEVENTF_XUP;
        binding.outputRecipe.mouseData = XBUTTON2;
        break;
    default:
        binding.outputRecipe = {};
        break;
    }
    binding.capabilities = static_cast<std::uint8_t>(
        kInputCapabilities | kOutputCapabilities);
}

[[nodiscard]] bool RequiresInput(std::uint8_t uses) noexcept
{
    return (uses & kInputCapabilities) != 0U;
}

} // namespace

void WindowsControlCatalog::BeginActivation() noexcept
{
    staged_.clear();
}

RuntimeControlBindResult WindowsControlCatalog::BindControl(
    ControlRefId controlId,
    const ControlRef& control,
    std::uint8_t requiredUses,
    ActivatedControl& activated) noexcept
{
    WindowsControlBinding binding{};
    if (!Resolve(controlId, control, binding)) {
        return RuntimeControlBindResult::UnsupportedIdentity;
    }
    binding.requiredUses = requiredUses;
    activated.capabilities = binding.capabilities;
    if ((binding.capabilities & requiredUses) != requiredUses) {
        return RuntimeControlBindResult::MissingCapability;
    }
    if (RequiresInput(requiredUses)) {
        for (const WindowsControlBinding& existing : staged_) {
            if (RequiresInput(existing.requiredUses)
                && InputOverlaps(existing, binding)) {
                activated.capabilities = 0U;
                return RuntimeControlBindResult::MissingCapability;
            }
        }
    }
    try {
        activated.backendToken = staged_.size();
        activated.device = binding.kind == WindowsControlKind::Keyboard
            ? DeviceKind::Keyboard
            : DeviceKind::Mouse;
        activated.requiresPointerTarget =
            binding.kind == WindowsControlKind::MouseButton;
        activated.initialStateQueryable = binding.initialStateQueryable;
        staged_.push_back(binding);
    } catch (...) {
        return RuntimeControlBindResult::UnsupportedIdentity;
    }
    return RuntimeControlBindResult::Bound;
}

void WindowsControlCatalog::CommitActivation() noexcept
{
    committed_.swap(staged_);
    staged_.clear();
    keyboardVirtualKeyIndex_.fill(ControlRefId{});
    mouseVirtualKeyIndex_.fill(ControlRefId{});
    for (auto& index : scanCodeIndex_) {
        index.fill(ControlRefId{});
    }
    for (const WindowsControlBinding& binding : committed_) {
        if (!RequiresInput(binding.requiredUses)) {
            continue;
        }
        if (binding.kind == WindowsControlKind::MouseButton) {
            mouseVirtualKeyIndex_[binding.virtualKey] = binding.control;
        } else if (binding.matchByScanCode) {
            scanCodeIndex_[binding.scanQualifier][binding.scanCode] =
                binding.control;
        } else {
            keyboardVirtualKeyIndex_[binding.virtualKey] = binding.control;
        }
    }
}

void WindowsControlCatalog::AbortActivation() noexcept
{
    staged_.clear();
}

const WindowsControlBinding* WindowsControlCatalog::Binding(
    std::uintptr_t token) const noexcept
{
    return token < committed_.size() ? &committed_[token] : nullptr;
}

std::optional<ControlRefId> WindowsControlCatalog::Normalize(
    const WindowsNativeInputEvent& event) const noexcept
{
#ifdef INPUTWEAVER_TESTING
    lastNormalizeVisitCount_ = 0U;
#endif
    if (event.virtualKey > 0xffU || event.scanCode > 0xffU) {
        return std::nullopt;
    }
    if (event.device == DeviceKind::Mouse) {
#ifdef INPUTWEAVER_TESTING
        ++lastNormalizeVisitCount_;
#endif
        const ControlRefId control = mouseVirtualKeyIndex_[event.virtualKey];
        return control.IsValid()
            ? std::optional<ControlRefId>{control}
            : std::nullopt;
    }
    if (event.device != DeviceKind::Keyboard) {
        return std::nullopt;
    }
    const std::uint32_t qualifier = NativeScanQualifier(event);
#ifdef INPUTWEAVER_TESTING
    ++lastNormalizeVisitCount_;
#endif
    const ControlRefId scanControl = scanCodeIndex_[qualifier][event.scanCode];
    if (scanControl.IsValid()) {
        return scanControl;
    }
#ifdef INPUTWEAVER_TESTING
    ++lastNormalizeVisitCount_;
#endif
    const ControlRefId virtualKeyControl =
        keyboardVirtualKeyIndex_[event.virtualKey];
    if (virtualKeyControl.IsValid()) {
        return virtualKeyControl;
    }
    return std::nullopt;
}

std::size_t WindowsControlCatalog::BindingCount() const noexcept
{
    return committed_.size();
}

#ifdef INPUTWEAVER_TESTING
std::size_t WindowsControlCatalog::LastNormalizeVisitCountForTesting() const noexcept
{
    return lastNormalizeVisitCount_;
}
#endif

bool WindowsControlCatalog::Resolve(
    ControlRefId controlId,
    const ControlRef& control,
    WindowsControlBinding& binding) noexcept
{
    binding.control = controlId;
    if (control.namespaceId == kControlNamespaceUsbHid
        && control.qualifier == kControlQualifierNone) {
        if (control.familyId == 0x07U) {
            const WindowsVirtualKey virtualKey = KeyboardUsageToVirtualKey(
                control.code);
            if (virtualKey == 0U) {
                return false;
            }
            FillKeyboardRecipe(binding, virtualKey, false);
            return true;
        }
        if (control.familyId == 0x09U) {
            const WindowsVirtualKey virtualKey = MouseUsageToVirtualKey(
                control.code);
            if (virtualKey == 0U) {
                return false;
            }
            FillMouseRecipe(binding, virtualKey);
            return true;
        }
        if (control.familyId == 0x0cU) {
            const WindowsVirtualKey virtualKey = ConsumerUsageToVirtualKey(
                control.code);
            if (virtualKey == 0U) {
                return false;
            }
            FillKeyboardRecipe(binding, virtualKey, false);
            return true;
        }
        return false;
    }
    if (control.namespaceId != kControlNamespaceWindows) {
        return false;
    }
    if (control.familyId == kWindowsVirtualKeyFamily
        && control.qualifier == kControlQualifierNone
        && control.code != 0U
        && control.code <= 0xffU) {
        if (IsMouseVirtualKey(control.code)) {
            FillMouseRecipe(binding, control.code);
        } else {
            FillKeyboardRecipe(binding, control.code, true);
        }
        return true;
    }
    if (control.familyId != kWindowsScanCodeFamily
        || control.code == 0U
        || control.code > 0xffU
        || control.qualifier > kWindowsScanCodeQualifierE1) {
        return false;
    }
    binding.kind = WindowsControlKind::Keyboard;
    binding.scanCode = control.code;
    binding.scanQualifier = control.qualifier;
    binding.matchByScanCode = true;
    const UINT prefix = control.qualifier == kWindowsScanCodeQualifierE0
        ? 0xe000U
        : control.qualifier == kWindowsScanCodeQualifierE1
            ? 0xe100U
            : 0U;
    binding.virtualKey = MapVirtualKeyW(
        prefix | control.code,
        MAPVK_VSC_TO_VK_EX);
    if (binding.virtualKey == 0U
        && control.qualifier == kWindowsScanCodeQualifierE1
        && control.code == 0x45U) {
        binding.virtualKey = VK_PAUSE;
    }
    binding.capabilities = kInputCapabilities;
    binding.initialStateQueryable = binding.virtualKey != 0U;
    if (!binding.initialStateQueryable) {
        binding.capabilities = ToControlUseBits(ControlUse::EventSource);
    }
    if (control.qualifier != kWindowsScanCodeQualifierE1) {
        binding.outputRecipe.kind = WindowsOutputKind::KeyboardScanCode;
        binding.outputRecipe.scanCode = control.code;
        binding.outputRecipe.extendedScanCode = control.qualifier
            == kWindowsScanCodeQualifierE0;
        binding.capabilities = static_cast<std::uint8_t>(
            binding.capabilities | kOutputCapabilities);
    }
    return true;
}

bool WindowsControlCatalog::InputOverlaps(
    const WindowsControlBinding& left,
    const WindowsControlBinding& right) noexcept
{
    if (left.kind != right.kind) {
        return false;
    }
    if (left.kind == WindowsControlKind::MouseButton) {
        return left.virtualKey == right.virtualKey;
    }
    if (left.matchByScanCode && right.matchByScanCode) {
        return left.scanCode == right.scanCode
            && left.scanQualifier == right.scanQualifier;
    }
    if (left.virtualKey != 0U && left.virtualKey == right.virtualKey) {
        return true;
    }
    return left.scanCode != 0U
        && left.scanCode == right.scanCode
        && left.scanQualifier == right.scanQualifier;
}

WindowsRuntimeInputAdapter::WindowsRuntimeInputAdapter(
    const WindowsControlCatalog& catalog) noexcept
    : catalog_(catalog)
{
}

RuntimeInputEvent WindowsRuntimeInputAdapter::Normalize(
    const WindowsNativeInputEvent& event) noexcept
{
    RuntimeInputEvent normalized{};
    normalized.device = event.device;
    normalized.origin = event.origin;
    normalized.transition = event.transition;
    normalized.position = event.position;
    const std::optional<ControlRefId> control = catalog_.Normalize(event);
    if (control.has_value()) {
        normalized.control = *control;
    }
    return normalized;
}

WindowsRuntimeOutputPort::WindowsRuntimeOutputPort(
    const WindowsControlCatalog& catalog,
    void* publishContext,
    WindowsOutputPublishFunction publish) noexcept
    : catalog_(catalog),
      publishContext_(publishContext),
      publish_(publish)
{
}

RuntimeOutputResult WindowsRuntimeOutputPort::Publish(
    const RuntimeOutputRequest& request) noexcept
{
    const WindowsControlBinding* const binding = catalog_.Binding(
        request.activated.backendToken);
    if (binding == nullptr
        || binding->outputRecipe.kind == WindowsOutputKind::None
        || publish_ == nullptr) {
        return RuntimeOutputResult::Failed;
    }
    WindowsOutputItem item{};
    item.sourceSequence = request.sequence;
    item.outputStateGeneration = request.generation;
    item.outputCode = binding->virtualKey;
    item.requiresPointerTarget = request.activated.requiresPointerTarget;
    item.recipe = binding->outputRecipe;
    item.transition = request.transition == RuntimeOutputTransition::Up
        ? WindowsOutputTransition::Up
        : WindowsOutputTransition::Down;
    return publish_(publishContext_, item);
}

} // namespace inputweaver::win32
