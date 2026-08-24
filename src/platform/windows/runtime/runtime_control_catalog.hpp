#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "runtime/runtime_types.hpp"
#include "windows_input_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace inputweaver::win32 {

enum class WindowsControlKind : std::uint8_t {
    Keyboard,
    MouseButton,
};

struct WindowsControlBinding final {
    ControlRefId control{};
    WindowsControlKind kind{WindowsControlKind::Keyboard};
    WindowsVirtualKey virtualKey{};
    WindowsScanCode scanCode{};
    std::uint32_t scanQualifier{};
    WindowsOutputRecipe outputRecipe{};
    std::uint8_t capabilities{};
    std::uint8_t requiredUses{};
    bool matchByScanCode{};
    bool initialStateQueryable{};
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
        const WindowsNativeInputEvent& event) const noexcept;
    [[nodiscard]] std::size_t BindingCount() const noexcept;
#ifdef INPUTWEAVER_TESTING
    [[nodiscard]] std::size_t LastNormalizeVisitCountForTesting() const noexcept;
#endif

private:
    [[nodiscard]] static bool Resolve(
        ControlRefId controlId,
        const ControlRef& control,
        WindowsControlBinding& binding) noexcept;
    [[nodiscard]] static bool InputOverlaps(
        const WindowsControlBinding& left,
        const WindowsControlBinding& right) noexcept;
    std::vector<WindowsControlBinding> staged_;
    std::vector<WindowsControlBinding> committed_;
    std::array<ControlRefId, 256U> keyboardVirtualKeyIndex_{};
    std::array<ControlRefId, 256U> mouseVirtualKeyIndex_{};
    std::array<std::array<ControlRefId, 256U>, 3U> scanCodeIndex_{};
#ifdef INPUTWEAVER_TESTING
    mutable std::size_t lastNormalizeVisitCount_{};
#endif
};

class WindowsForceStopRecognizer final {
public:
    [[nodiscard]] bool Observe(const WindowsNativeInputEvent& event) noexcept;

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
        const WindowsNativeInputEvent& event) noexcept;

private:
    const WindowsControlCatalog& catalog_;
    WindowsForceStopRecognizer forceStop_;
};

using WindowsOutputPublishFunction = RuntimeOutputResult (*)(
    void* context,
    const WindowsOutputItem& item) noexcept;

class WindowsRuntimeOutputPort final : public RuntimeOutputPort {
public:
    WindowsRuntimeOutputPort(
        const WindowsControlCatalog& catalog,
        void* publishContext,
        WindowsOutputPublishFunction publish) noexcept;

    [[nodiscard]] RuntimeOutputResult Publish(
        const RuntimeOutputRequest& request) noexcept override;

private:
    const WindowsControlCatalog& catalog_;
    void* publishContext_;
    WindowsOutputPublishFunction publish_;
};

} // namespace inputweaver::win32
