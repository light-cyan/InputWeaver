#pragma once

#include "platform/windows/support/unique_handle.hpp"
#include "platform/windows/tui/tui_ipc.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace inputweaver::win32 {

class TuiFrontendSession final {
public:
    explicit TuiFrontendSession(std::filesystem::path executableDirectory);
    ~TuiFrontendSession();

    TuiFrontendSession(const TuiFrontendSession&) = delete;
    TuiFrontendSession& operator=(const TuiFrontendSession&) = delete;

    [[nodiscard]] bool Show(std::string& error);
    void Hide() noexcept;
    [[nodiscard]] bool Poll(
        std::vector<ui::tui::KeyEvent>& events,
        bool& backgroundRequested,
        std::string& error) noexcept;
    [[nodiscard]] bool SendFrame(
        const ui::tui::Canvas& canvas,
        std::string& error) noexcept;

    [[nodiscard]] bool Visible() noexcept;
    [[nodiscard]] std::size_t Width() const noexcept;
    [[nodiscard]] std::size_t Height() const noexcept;

private:
    void Reset() noexcept;

    std::filesystem::path executableDirectory_;
    UniqueHandle process_;
    TuiIpcChannel channel_;
    HWND window_{};
    std::size_t width_{80U};
    std::size_t height_{24U};
    std::vector<std::uint8_t> lastFramePayload_;
    ULONGLONG startedAt_{};
    ULONGLONG lastHeartbeatAt_{};
    bool ready_{};
};

} // namespace inputweaver::win32
