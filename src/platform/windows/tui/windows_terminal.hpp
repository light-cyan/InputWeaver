#pragma once

#include "ui/tui/support/canvas.hpp"
#include "ui/tui/tui_controller.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

namespace inputweaver::win32 {

class WindowsTerminal final {
public:
    WindowsTerminal() = default;
    ~WindowsTerminal();

    WindowsTerminal(const WindowsTerminal&) = delete;
    WindowsTerminal& operator=(const WindowsTerminal&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    [[nodiscard]] std::pair<std::size_t, std::size_t> Size() const noexcept;
    [[nodiscard]] bool Poll(
        ui::tui::KeyEvent& event,
        DWORD timeoutMilliseconds,
        bool& resized) noexcept;
    [[nodiscard]] bool Draw(
        const ui::tui::Canvas& canvas,
        std::string& error) noexcept;

private:
    void Restore() noexcept;

    HANDLE input_{INVALID_HANDLE_VALUE};
    HANDLE output_{INVALID_HANDLE_VALUE};
    DWORD originalInputMode_{};
    DWORD originalOutputMode_{};
    UINT originalInputCodePage_{};
    UINT originalOutputCodePage_{};
    wchar_t pendingHighSurrogate_{};
    bool initialized_{};
};

[[nodiscard]] bool LoadTuiColorScheme(
    const std::filesystem::path& executableDirectory,
    ui::tui::ColorScheme& colors,
    std::string& error);

} // namespace inputweaver::win32
