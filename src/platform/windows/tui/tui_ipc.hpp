#pragma once

#include "platform/windows/support/unique_handle.hpp"
#include "ui/tui/support/canvas.hpp"
#include "ui/tui/tui_input.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace inputweaver::win32 {

inline constexpr std::size_t kMinimumTuiColumns = 80U;
inline constexpr std::size_t kMinimumTuiRows = 24U;
inline constexpr std::size_t kMaximumTuiColumns = 300U;
inline constexpr std::size_t kMaximumTuiRows = 120U;

enum class TuiIpcMessageType : std::uint32_t {
    Frame = 1U,
    Key = 2U,
    Resize = 3U,
    Close = 4U,
    Background = 5U,
    Window = 6U,
    Heartbeat = 7U,
};

struct TuiIpcMessage final {
    TuiIpcMessageType type{TuiIpcMessageType::Close};
    std::vector<std::uint8_t> payload;
};

struct TuiFrame final {
    std::size_t width{};
    std::size_t height{};
    std::vector<ui::tui::Cell> cells;
};

class TuiIpcChannel final {
public:
    TuiIpcChannel() = default;
    TuiIpcChannel(UniqueHandle readHandle, UniqueHandle writeHandle) noexcept;

    TuiIpcChannel(const TuiIpcChannel&) = delete;
    TuiIpcChannel& operator=(const TuiIpcChannel&) = delete;
    TuiIpcChannel(TuiIpcChannel&&) noexcept = default;
    TuiIpcChannel& operator=(TuiIpcChannel&&) noexcept = default;

    void Reset(
        UniqueHandle readHandle = {},
        UniqueHandle writeHandle = {}) noexcept;
    [[nodiscard]] bool Connected() const noexcept;
    [[nodiscard]] bool Send(
        TuiIpcMessageType type,
        std::span<const std::uint8_t> payload,
        std::string& error) noexcept;
    [[nodiscard]] bool Poll(
        std::vector<TuiIpcMessage>& messages,
        bool& disconnected,
        std::string& error) noexcept;

private:
    UniqueHandle readHandle_;
    UniqueHandle writeHandle_;
    std::vector<std::uint8_t> receiveBuffer_;
};

[[nodiscard]] std::vector<std::uint8_t> EncodeKeyEvent(
    const ui::tui::KeyEvent& event);
[[nodiscard]] bool DecodeKeyEvent(
    std::span<const std::uint8_t> payload,
    ui::tui::KeyEvent& event) noexcept;
[[nodiscard]] std::vector<std::uint8_t> EncodeViewportSize(
    std::size_t width,
    std::size_t height);
[[nodiscard]] bool DecodeViewportSize(
    std::span<const std::uint8_t> payload,
    std::size_t& width,
    std::size_t& height) noexcept;
[[nodiscard]] std::vector<std::uint8_t> EncodeTuiFrame(
    const ui::tui::Canvas& canvas);
[[nodiscard]] bool DecodeTuiFrame(
    std::span<const std::uint8_t> payload,
    TuiFrame& frame) noexcept;
[[nodiscard]] std::vector<std::uint8_t> EncodeWindowHandle(
    std::uintptr_t window);
[[nodiscard]] bool DecodeWindowHandle(
    std::span<const std::uint8_t> payload,
    std::uintptr_t& window) noexcept;

} // namespace inputweaver::win32
