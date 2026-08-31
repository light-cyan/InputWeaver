#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace inputweaver::win32 {

enum class TrayAction : std::uint8_t {
    None,
    Show,
    Hide,
    Exit,
};

class WindowsTray final {
public:
    WindowsTray() = default;
    ~WindowsTray();

    WindowsTray(const WindowsTray&) = delete;
    WindowsTray& operator=(const WindowsTray&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    [[nodiscard]] TrayAction Poll() noexcept;

    void SetFrontendVisible(bool visible) noexcept;
    void NotifyError(std::string_view message) noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wordParameter,
        LPARAM longParameter) noexcept;

    [[nodiscard]] bool AddIcon(std::string& error) noexcept;
    void RemoveIcon() noexcept;
    void ShowContextMenu() noexcept;
    LRESULT HandleMessage(
        UINT message,
        WPARAM wordParameter,
        LPARAM longParameter) noexcept;

    HINSTANCE instance_{};
    HWND window_{};
    HICON icon_{};
    UINT taskbarCreatedMessage_{};
    TrayAction pendingAction_{TrayAction::None};
    bool classRegistered_{};
    bool iconAdded_{};
    bool frontendVisible_{};
};

} // namespace inputweaver::win32
