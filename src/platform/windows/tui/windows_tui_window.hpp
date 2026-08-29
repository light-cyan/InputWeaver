#pragma once

#include "platform/windows/tui/tui_ipc.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <string>
#include <vector>

namespace inputweaver::win32 {

class WindowsTuiWindow final {
public:
    WindowsTuiWindow() = default;
    ~WindowsTuiWindow();

    WindowsTuiWindow(const WindowsTuiWindow&) = delete;
    WindowsTuiWindow& operator=(const WindowsTuiWindow&) = delete;

    [[nodiscard]] bool Initialize(
        HINSTANCE instance,
        int showCommand,
        std::string& error);
    void Poll(
        std::vector<ui::tui::KeyEvent>& events,
        bool& backgroundRequested) noexcept;
    void Present(TuiFrame frame) noexcept;

    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] HWND Handle() const noexcept;
    [[nodiscard]] std::size_t Columns() const noexcept;
    [[nodiscard]] std::size_t Rows() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wordParameter,
        LPARAM longParameter) noexcept;

    LRESULT HandleMessage(
        UINT message,
        WPARAM wordParameter,
        LPARAM longParameter) noexcept;
    [[nodiscard]] bool RecreateFonts(UINT dpi, std::string& error) noexcept;
    void UpdateViewport() noexcept;
    void Paint() noexcept;
    void QueueEvent(ui::tui::KeyEvent event) noexcept;
    void QueueText(const wchar_t* text, std::size_t length) noexcept;
    void PasteClipboard() noexcept;
    void AcceptDroppedFiles(HDROP drop) noexcept;

    HINSTANCE instance_{};
    HWND window_{};
    HFONT font_{};
    HFONT underlineFont_{};
    HICON largeIcon_{};
    HICON smallIcon_{};
    int cellWidth_{8};
    int cellHeight_{16};
    std::size_t columns_{kMinimumTuiColumns};
    std::size_t rows_{kMinimumTuiRows};
    TuiFrame frame_;
    std::vector<ui::tui::KeyEvent> pendingEvents_;
    wchar_t pendingHighSurrogate_{};
    bool classRegistered_{};
    bool backgroundRequested_{};
    bool running_{};
};

} // namespace inputweaver::win32
