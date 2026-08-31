#include "windows_tray.hpp"

#include "platform/windows/support/text_encoding.hpp"
#include "resource_ids.h"

#include <system_error>
#include <utility>

#include <shellapi.h>

namespace inputweaver::win32 {
namespace {

inline constexpr wchar_t kWindowClassName[] =
    L"InputWeaverHostTrayWindow";
inline constexpr wchar_t kTooltip[] = L"InputWeaver TUI";
inline constexpr UINT kTrayCallbackMessage = WM_APP + 1U;
inline constexpr UINT kTrayIconId = 1U;
inline constexpr UINT kToggleMenuId = 1U;
inline constexpr UINT kExitMenuId = 2U;

[[nodiscard]] std::string WindowsError(DWORD error)
{
    return std::system_category().message(static_cast<int>(error));
}

} // namespace

WindowsTray::~WindowsTray()
{
    RemoveIcon();
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (classRegistered_) {
        UnregisterClassW(kWindowClassName, instance_);
        classRegistered_ = false;
    }
    if (icon_ != nullptr) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

bool WindowsTray::Initialize(std::string& error)
{
    instance_ = GetModuleHandleW(nullptr);
    if (instance_ == nullptr) {
        error = "InputWeaverHost cannot access its Windows module.";
        return false;
    }

    icon_ = reinterpret_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(INPUTWEAVER_TUI_ICON_RESOURCE_ID),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (icon_ == nullptr) {
        error = "Cannot load the InputWeaver TUI icon: "
            + WindowsError(GetLastError());
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = static_cast<UINT>(sizeof(windowClass));
    windowClass.lpfnWndProc = &WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = icon_;
    windowClass.hIconSm = icon_;
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0U) {
        error = "Cannot register the InputWeaver tray window: "
            + WindowsError(GetLastError());
        return false;
    }
    classRegistered_ = true;

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"InputWeaver TUI",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        error = "Cannot create the InputWeaver tray window: "
            + WindowsError(GetLastError());
        return false;
    }

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    return AddIcon(error);
}

TrayAction WindowsTray::Poll() noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            pendingAction_ = TrayAction::Exit;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return std::exchange(pendingAction_, TrayAction::None);
}

void WindowsTray::SetFrontendVisible(bool visible) noexcept
{
    frontendVisible_ = visible;
}

void WindowsTray::NotifyError(std::string_view message) noexcept
{
    if (!iconAdded_) {
        return;
    }
    std::wstring wideMessage;
    if (!Utf8ToWide(message, wideMessage)) {
        wideMessage = L"InputWeaver encountered an error.";
    }
    NOTIFYICONDATAW data{};
    data.cbSize = static_cast<DWORD>(sizeof(data));
    data.hWnd = window_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_ERROR;
    lstrcpynW(
        data.szInfoTitle,
        L"InputWeaver",
        static_cast<int>(
            sizeof(data.szInfoTitle) / sizeof(data.szInfoTitle[0])));
    lstrcpynW(
        data.szInfo,
        wideMessage.c_str(),
        static_cast<int>(sizeof(data.szInfo) / sizeof(data.szInfo[0])));
    (void)Shell_NotifyIconW(NIM_MODIFY, &data);
}

LRESULT CALLBACK WindowsTray::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter) noexcept
{
    WindowsTray* tray = reinterpret_cast<WindowsTray*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* created = reinterpret_cast<const CREATESTRUCTW*>(
            longParameter);
        tray = static_cast<WindowsTray*>(created->lpCreateParams);
        tray->window_ = window;
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(tray));
    }
    return tray == nullptr
        ? DefWindowProcW(window, message, wordParameter, longParameter)
        : tray->HandleMessage(message, wordParameter, longParameter);
}

bool WindowsTray::AddIcon(std::string& error) noexcept
{
    NOTIFYICONDATAW data{};
    data.cbSize = static_cast<DWORD>(sizeof(data));
    data.hWnd = window_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = icon_;
    lstrcpynW(
        data.szTip,
        kTooltip,
        static_cast<int>(sizeof(data.szTip) / sizeof(data.szTip[0])));
    if (Shell_NotifyIconW(NIM_ADD, &data) == FALSE) {
        error = "Cannot add the InputWeaver notification icon: "
            + WindowsError(GetLastError());
        return false;
    }
    iconAdded_ = true;
    return true;
}

void WindowsTray::RemoveIcon() noexcept
{
    if (!iconAdded_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = static_cast<DWORD>(sizeof(data));
    data.hWnd = window_;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    iconAdded_ = false;
}

void WindowsTray::ShowContextMenu() noexcept
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(
        menu,
        MF_STRING,
        kToggleMenuId,
        frontendVisible_ ? L"Hide TUI" : L"Show TUI");
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kExitMenuId, L"Exit InputWeaver");

    POINT cursor{};
    if (GetCursorPos(&cursor) != FALSE) {
        SetForegroundWindow(window_);
        const UINT command = static_cast<UINT>(TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            cursor.x,
            cursor.y,
            0,
            window_,
            nullptr));
        if (command == kToggleMenuId) {
            pendingAction_ = frontendVisible_
                ? TrayAction::Hide
                : TrayAction::Show;
        } else if (command == kExitMenuId) {
            pendingAction_ = TrayAction::Exit;
        }
        PostMessageW(window_, WM_NULL, 0U, 0);
    }
    DestroyMenu(menu);
}

LRESULT WindowsTray::HandleMessage(
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter) noexcept
{
    if (message == taskbarCreatedMessage_) {
        iconAdded_ = false;
        std::string ignored;
        (void)AddIcon(ignored);
        return 0;
    }
    if (message == WM_CLOSE) {
        pendingAction_ = TrayAction::Exit;
        return 0;
    }
    if (message == kTrayCallbackMessage) {
        const UINT notification = static_cast<UINT>(longParameter);
        if (notification == WM_LBUTTONUP
            || notification == WM_LBUTTONDBLCLK) {
            pendingAction_ = TrayAction::Show;
        } else if (notification == WM_RBUTTONUP
            || notification == WM_CONTEXTMENU) {
            ShowContextMenu();
        }
        return 0;
    }
    return DefWindowProcW(window_, message, wordParameter, longParameter);
}

} // namespace inputweaver::win32
