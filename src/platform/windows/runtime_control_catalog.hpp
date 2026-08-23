#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "runtime/runtime_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace inputweaver::win32 {

class WindowsRuntimeOutputPort;

enum class WindowsControlKind : std::uint8_t {
    Keyboard,
    MouseButton,
};

struct WindowsControlBinding final {
    ControlRefId control{};
    WindowsControlKind kind{WindowsControlKind::Keyboard};
    ControlCode virtualKey{};
    ScanCode scanCode{};
    std::uint32_t scanQualifier{};
    std::uint8_t capabilities{};
    std::uint8_t requiredUses{};
    bool matchByScanCode{};
    bool layoutSensitive{};
    bool exceptionalSequence{};
};

class WindowsControlCatalog final : public RuntimeControlPort {
public:
    void BeginActivation() noexcept override;
    [[nodiscard]] RuntimeControlBindResult BindControl(
        ControlRefId controlId,
        const ControlRef& control,
        std::uint8_t requiredUses,
        ActivatedControl& activated) noexcept override;
    void CommitActivation() noexcept override;
    void AbortActivation() noexcept override;

    [[nodiscard]] const WindowsControlBinding* Binding(
        std::uintptr_t token) const noexcept;
    [[nodiscard]] std::optional<ControlRefId> Normalize(
        const InputEvent& event) const noexcept;
    [[nodiscard]] std::size_t BindingCount() const noexcept;

private:
    [[nodiscard]] static bool Resolve(
        ControlRefId controlId,
        const ControlRef& control,
        WindowsControlBinding& binding) noexcept;
    [[nodiscard]] static bool InputOverlaps(
        const WindowsControlBinding& left,
        const WindowsControlBinding& right) noexcept;
    [[nodiscard]] static bool Matches(
        const WindowsControlBinding& binding,
        const InputEvent& event) noexcept;

    std::vector<WindowsControlBinding> staged_;
    std::vector<WindowsControlBinding> committed_;
};

class WindowsForceStopRecognizer final {
public:
    [[nodiscard]] bool Observe(const InputEvent& event) noexcept;

private:
    bool leftControl_{};
    bool rightControl_{};
    bool genericControl_{};
    bool leftShift_{};
    bool rightShift_{};
    bool genericShift_{};
    bool f12_{};
};

class WindowsRuntimeInputAdapter final {
public:
    explicit WindowsRuntimeInputAdapter(
        const WindowsControlCatalog& catalog) noexcept;

    [[nodiscard]] RuntimeInputEvent Normalize(
        const InputEvent& event) noexcept;

private:
    const WindowsControlCatalog& catalog_;
    WindowsForceStopRecognizer forceStop_;
};

using ActionBatchPublishFunction = RuntimeOutputResult (*)(
    void* context,
    const ActionBatch& batch) noexcept;

[[nodiscard]] RuntimeOutputResult PublishRuntimeBatchToActionQueue(
    void* context,
    const ActionBatch& batch) noexcept;

class WindowsRuntimeOutputPort final : public RuntimeOutputPort {
public:
    WindowsRuntimeOutputPort(
        const WindowsControlCatalog& catalog,
        ProcessId targetPid,
        void* publishContext,
        ActionBatchPublishFunction publish) noexcept;

    [[nodiscard]] RuntimeOutputResult Publish(
        const RuntimeOutputRequest& request) noexcept override;

private:
    const WindowsControlCatalog& catalog_;
    ProcessId targetPid_;
    void* publishContext_;
    ActionBatchPublishFunction publish_;
};

} // namespace inputweaver::win32
