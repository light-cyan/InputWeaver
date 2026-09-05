#pragma once

#include "debug/debug_protocol.hpp"
#include "runtime/runtime_types.hpp"
#include "support/callback_ref.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace inputweaver {

class CompiledProgram;

namespace win32 {

enum class DebugCaptureRequest : std::uint8_t {
    None,
    Start,
    Stop,
};

struct DebugInputCorrelation final {
    std::uint64_t captureEpoch{};
    std::uint64_t inputSequence{};

    [[nodiscard]] bool Active() const noexcept
    {
        return captureEpoch != 0U && inputSequence != 0U;
    }
};

struct DebugServerCallbacks final {
    support::CallbackRef<void() noexcept> wakeInputThread{};
    support::CallbackRef<void() noexcept> requestExecutorStop{};
    support::CallbackRef<bool(RuntimeMouseSnapshot&) noexcept> readMouseSnapshot{};
};

class WindowsDebugServer final : public RuntimeDebugEventPort {
public:
    WindowsDebugServer();
    ~WindowsDebugServer() override;

    WindowsDebugServer(const WindowsDebugServer&) = delete;
    WindowsDebugServer& operator=(const WindowsDebugServer&) = delete;

    [[nodiscard]] bool Start(
        std::wstring token,
        std::shared_ptr<const CompiledProgram> program,
        DebugServerCallbacks callbacks,
        std::wstring& errorMessage);
    void Stop() noexcept;

    [[nodiscard]] DebugCaptureRequest TakeCaptureRequest() noexcept;
    [[nodiscard]] bool BeginCapture(
        std::span<const debug::InputEventPayload> initialInputs) noexcept;
    void EndCapture() noexcept;

    [[nodiscard]] DebugInputCorrelation BeginInput() noexcept;
    [[nodiscard]] bool PublishInput(
        const DebugInputCorrelation& correlation,
        const debug::InputEventPayload& input) noexcept;
    [[nodiscard]] bool Publish(
        const RuntimeDebugEvent& event) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::wstring MakeDebugPipeName(
    std::uint32_t processId,
    std::wstring_view token);

} // namespace win32
} // namespace inputweaver
