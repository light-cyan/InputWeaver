#include "windows_tui_window.hpp"

#include "resource_ids.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include <shellapi.h>

namespace inputweaver::win32 {
namespace {

inline constexpr wchar_t kWindowClassName[] = L"InputWeaverTuiWindow";
inline constexpr wchar_t kWindowTitle[] = L"InputWeaver";
inline constexpr std::size_t kDefaultColumns = 120U;
inline constexpr std::size_t kDefaultRows = 36U;
inline constexpr int kBaseFontPixels = 16;

[[nodiscard]] std::string WindowsError(DWORD error)
{
    return std::system_category().message(static_cast<int>(error));
}

[[nodiscard]] COLORREF NativeColor(const ui::tui::RgbColor& color) noexcept
{
    return RGB(color.red, color.green, color.blue);
}

[[nodiscard]] std::optional<ui::tui::Key> VirtualKey(WPARAM key) noexcept
{
    using ui::tui::Key;
    switch (key) {
    case VK_RETURN:
        return Key::Enter;
    case VK_ESCAPE:
        return Key::Escape;
    case VK_TAB:
        return Key::Tab;
    case VK_UP:
        return Key::Up;
    case VK_DOWN:
        return Key::Down;
    case VK_LEFT:
        return Key::Left;
    case VK_RIGHT:
        return Key::Right;
    case VK_PRIOR:
        return Key::PageUp;
    case VK_NEXT:
        return Key::PageDown;
    case VK_HOME:
        return Key::Home;
    case VK_END:
        return Key::End;
    case VK_BACK:
        return Key::Backspace;
    case VK_DELETE:
        return Key::Delete;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<ui::tui::Key> ControlKey(WPARAM key) noexcept
{
    using ui::tui::Key;
    switch (key) {
    case 'C':
        return Key::Copy;
    case 'X':
        return Key::Cut;
    case 'Z':
        return Key::Undo;
    case 'Y':
        return Key::Redo;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::array<wchar_t, 2U> Utf16(
    char32_t codePoint,
    int& length) noexcept
{
    if (codePoint <= 0xffffU) {
        length = 1;
        return {static_cast<wchar_t>(codePoint), L'\0'};
    }
    const char32_t adjusted = codePoint - 0x10000U;
    length = 2;
    return {
        static_cast<wchar_t>(0xd800U + (adjusted >> 10U)),
        static_cast<wchar_t>(0xdc00U + (adjusted & 0x3ffU))};
}

[[nodiscard]] char32_t DecodeUtf16CodeUnit(
    wchar_t character,
    wchar_t& highSurrogate) noexcept
{
    if (character >= 0xd800 && character <= 0xdbff) {
        highSurrogate = character;
        return 0U;
    }
    char32_t codePoint{};
    if (character >= 0xdc00 && character <= 0xdfff
        && highSurrogate != 0) {
        codePoint = 0x10000U
            + ((static_cast<char32_t>(highSurrogate) - 0xd800U) << 10U)
            + (static_cast<char32_t>(character) - 0xdc00U);
    } else if (character < 0xdc00 || character > 0xdfff) {
        codePoint = static_cast<char32_t>(character);
    }
    highSurrogate = 0;
    return codePoint;
}

} // namespace

WindowsTuiWindow::~WindowsTuiWindow()
{
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (classRegistered_) {
        UnregisterClassW(kWindowClassName, instance_);
        classRegistered_ = false;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (underlineFont_ != nullptr) {
        DeleteObject(underlineFont_);
        underlineFont_ = nullptr;
    }
    if (largeIcon_ != nullptr) {
        DestroyIcon(largeIcon_);
        largeIcon_ = nullptr;
    }
    if (smallIcon_ != nullptr) {
        DestroyIcon(smallIcon_);
        smallIcon_ = nullptr;
    }
}

bool WindowsTuiWindow::Initialize(
    HINSTANCE instance,
    int showCommand,
    std::string& error)
{
    instance_ = instance;
    largeIcon_ = reinterpret_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(INPUTWEAVER_TUI_ICON_RESOURCE_ID),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    smallIcon_ = reinterpret_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(INPUTWEAVER_TUI_ICON_RESOURCE_ID),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (largeIcon_ == nullptr || smallIcon_ == nullptr) {
        error = "Cannot load the InputWeaver window icon: "
            + WindowsError(GetLastError());
        return false;
    }

    HDC screen = GetDC(nullptr);
    const UINT dpi = screen == nullptr
        ? 96U
        : static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
    if (screen != nullptr) {
        ReleaseDC(nullptr, screen);
    }
    if (!RecreateFonts(dpi, error)) {
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = static_cast<UINT>(sizeof(windowClass));
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = largeIcon_;
    windowClass.hIconSm = smallIcon_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(
        GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0U) {
        error = "Cannot register the InputWeaver TUI window: "
            + WindowsError(GetLastError());
        return false;
    }
    classRegistered_ = true;

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    constexpr DWORD extendedStyle = WS_EX_APPWINDOW | WS_EX_ACCEPTFILES;
    RECT bounds{
        0,
        0,
        static_cast<LONG>(kDefaultColumns) * cellWidth_,
        static_cast<LONG>(kDefaultRows) * cellHeight_};
    if (AdjustWindowRectEx(&bounds, style, FALSE, extendedStyle) == FALSE) {
        error = "Cannot calculate the InputWeaver TUI window size: "
            + WindowsError(GetLastError());
        return false;
    }
    window_ = CreateWindowExW(
        extendedStyle,
        kWindowClassName,
        kWindowTitle,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        error = "Cannot create the InputWeaver TUI window: "
            + WindowsError(GetLastError());
        return false;
    }
    SendMessageW(
        window_,
        WM_SETICON,
        ICON_BIG,
        reinterpret_cast<LPARAM>(largeIcon_));
    SendMessageW(
        window_,
        WM_SETICON,
        ICON_SMALL,
        reinterpret_cast<LPARAM>(smallIcon_));
    DragAcceptFiles(window_, TRUE);
    running_ = true;
    UpdateViewport();
    ShowWindow(window_, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    return true;
}

void WindowsTuiWindow::Poll(
    std::vector<ui::tui::KeyEvent>& events,
    bool& backgroundRequested) noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            running_ = false;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    events = std::move(pendingEvents_);
    pendingEvents_.clear();
    backgroundRequested = std::exchange(backgroundRequested_, false);
}

void WindowsTuiWindow::Present(TuiFrame frame) noexcept
{
    frame_ = std::move(frame);
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

bool WindowsTuiWindow::Running() const noexcept
{
    return running_;
}

HWND WindowsTuiWindow::Handle() const noexcept
{
    return window_;
}

std::size_t WindowsTuiWindow::Columns() const noexcept
{
    return columns_;
}

std::size_t WindowsTuiWindow::Rows() const noexcept
{
    return rows_;
}

LRESULT CALLBACK WindowsTuiWindow::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter) noexcept
{
    WindowsTuiWindow* view = reinterpret_cast<WindowsTuiWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* created = reinterpret_cast<const CREATESTRUCTW*>(
            longParameter);
        view = static_cast<WindowsTuiWindow*>(created->lpCreateParams);
        view->window_ = window;
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(view));
    }
    return view == nullptr
        ? DefWindowProcW(window, message, wordParameter, longParameter)
        : view->HandleMessage(message, wordParameter, longParameter);
}

LRESULT WindowsTuiWindow::HandleMessage(
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter) noexcept
{
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (wordParameter == SIZE_MINIMIZED) {
            backgroundRequested_ = true;
            running_ = false;
        } else {
            UpdateViewport();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(longParameter);
        RECT bounds{
            0,
            0,
            static_cast<LONG>(kMinimumTuiColumns) * cellWidth_,
            static_cast<LONG>(kMinimumTuiRows) * cellHeight_};
        const DWORD style = static_cast<DWORD>(GetWindowLongW(window_, GWL_STYLE));
        const DWORD extendedStyle = static_cast<DWORD>(
            GetWindowLongW(window_, GWL_EXSTYLE));
        if (AdjustWindowRectEx(
                &bounds,
                style,
                FALSE,
                extendedStyle) != FALSE) {
            limits->ptMinTrackSize.x = bounds.right - bounds.left;
            limits->ptMinTrackSize.y = bounds.bottom - bounds.top;
        }
        return 0;
    }
    case WM_DPICHANGED: {
        std::string ignored;
        const UINT dpi = LOWORD(wordParameter);
        if (RecreateFonts(dpi, ignored)) {
            const auto* suggested = reinterpret_cast<const RECT*>(
                longParameter);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            UpdateViewport();
        }
        return 0;
    }
    case WM_KEYDOWN: {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        const bool control = GetKeyState(VK_CONTROL) < 0;
        if (control && wordParameter == 'V') {
            PasteClipboard();
            return 0;
        }
        if (control) {
            if (const auto key = ControlKey(wordParameter); key.has_value()) {
                QueueEvent({*key, 0U, false});
                return 0;
            }
        }
        if (const auto key = VirtualKey(wordParameter); key.has_value()) {
            pendingHighSurrogate_ = 0;
            QueueEvent({*key, 0U, shift});
            return 0;
        }
        break;
    }
    case WM_CHAR: {
        const wchar_t character = static_cast<wchar_t>(wordParameter);
        const char32_t codePoint = DecodeUtf16CodeUnit(
            character,
            pendingHighSurrogate_);
        if (codePoint >= 0x20U && codePoint != 0x7fU) {
            QueueEvent({ui::tui::Key::Character, codePoint, false});
        }
        return 0;
    }
    case WM_UNICHAR:
        if (wordParameter == UNICODE_NOCHAR) {
            return TRUE;
        }
        if (wordParameter <= 0x10ffffU
            && (wordParameter < 0xd800U || wordParameter > 0xdfffU)) {
            QueueEvent({
                ui::tui::Key::Character,
                static_cast<char32_t>(wordParameter),
                false});
        }
        return 0;
    case WM_DROPFILES:
        AcceptDroppedFiles(reinterpret_cast<HDROP>(wordParameter));
        return 0;
    case WM_CLOSE:
        ShowWindow(window_, SW_HIDE);
        backgroundRequested_ = true;
        running_ = false;
        return 0;
    case WM_DESTROY:
        running_ = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wordParameter, longParameter);
}

bool WindowsTuiWindow::RecreateFonts(UINT dpi, std::string& error) noexcept
{
    LOGFONTW descriptor{};
    descriptor.lfHeight = -MulDiv(kBaseFontPixels, static_cast<int>(dpi), 96);
    descriptor.lfWeight = FW_NORMAL;
    descriptor.lfCharSet = DEFAULT_CHARSET;
    descriptor.lfOutPrecision = OUT_TT_PRECIS;
    descriptor.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    descriptor.lfQuality = CLEARTYPE_QUALITY;
    descriptor.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpynW(
        descriptor.lfFaceName,
        L"Consolas",
        static_cast<int>(LF_FACESIZE));
    HFONT normal = CreateFontIndirectW(&descriptor);
    descriptor.lfUnderline = TRUE;
    HFONT underlined = CreateFontIndirectW(&descriptor);
    if (normal == nullptr || underlined == nullptr) {
        if (normal != nullptr) {
            DeleteObject(normal);
        }
        if (underlined != nullptr) {
            DeleteObject(underlined);
        }
        error = "Cannot create the InputWeaver TUI font: "
            + WindowsError(GetLastError());
        return false;
    }

    HDC screen = GetDC(nullptr);
    TEXTMETRICW metrics{};
    HGDIOBJ previous = screen == nullptr
        ? nullptr
        : SelectObject(screen, normal);
    const bool measured = screen != nullptr
        && GetTextMetricsW(screen, &metrics) != FALSE;
    if (screen != nullptr) {
        if (previous != nullptr) {
            SelectObject(screen, previous);
        }
        ReleaseDC(nullptr, screen);
    }
    if (!measured || metrics.tmAveCharWidth <= 0 || metrics.tmHeight <= 0) {
        DeleteObject(normal);
        DeleteObject(underlined);
        error = "Cannot measure the InputWeaver TUI font.";
        return false;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
    if (underlineFont_ != nullptr) {
        DeleteObject(underlineFont_);
    }
    font_ = normal;
    underlineFont_ = underlined;
    cellWidth_ = metrics.tmAveCharWidth;
    cellHeight_ = metrics.tmHeight + metrics.tmExternalLeading;
    return true;
}

void WindowsTuiWindow::UpdateViewport() noexcept
{
    RECT client{};
    if (window_ == nullptr || GetClientRect(window_, &client) == FALSE
        || cellWidth_ <= 0 || cellHeight_ <= 0) {
        return;
    }
    const auto width = static_cast<std::size_t>((std::max)(
        client.right - client.left,
        1L));
    const auto height = static_cast<std::size_t>((std::max)(
        client.bottom - client.top,
        1L));
    columns_ = (std::min)(
        kMaximumTuiColumns,
        (std::max)(
            width / static_cast<std::size_t>(cellWidth_),
            std::size_t{1U}));
    rows_ = (std::min)(
        kMaximumTuiRows,
        (std::max)(
            height / static_cast<std::size_t>(cellHeight_),
            std::size_t{1U}));
}

void WindowsTuiWindow::Paint() noexcept
{
    PAINTSTRUCT paint{};
    HDC context = BeginPaint(window_, &paint);
    if (context == nullptr) {
        return;
    }
    RECT client{};
    if (GetClientRect(window_, &client) == FALSE) {
        EndPaint(window_, &paint);
        return;
    }
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    HDC buffer = CreateCompatibleDC(context);
    HBITMAP bitmap = buffer == nullptr || clientWidth <= 0 || clientHeight <= 0
        ? nullptr
        : CreateCompatibleBitmap(context, clientWidth, clientHeight);
    if (buffer == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (buffer != nullptr) {
            DeleteDC(buffer);
        }
        FillRect(context, &paint.rcPaint, reinterpret_cast<HBRUSH>(
            GetStockObject(BLACK_BRUSH)));
        EndPaint(window_, &paint);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    FillRect(buffer, &client, reinterpret_cast<HBRUSH>(
        GetStockObject(BLACK_BRUSH)));
    SetTextAlign(buffer, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    const std::size_t width = (std::min)(frame_.width, columns_);
    const std::size_t height = (std::min)(frame_.height, rows_);
    if (frame_.cells.size() >= frame_.width * frame_.height) {
        HFONT selectedFont{};
        for (std::size_t y = 0U; y < height; ++y) {
            for (std::size_t x = 0U; x < width; ++x) {
                const ui::tui::Cell& cell = frame_.cells[
                    y * frame_.width + x];
                if (cell.continuation) {
                    continue;
                }
                const bool wide = x + 1U < frame_.width
                    && frame_.cells[y * frame_.width + x + 1U].continuation;
                RECT cellBounds{
                    static_cast<LONG>(x) * cellWidth_,
                    static_cast<LONG>(y) * cellHeight_,
                    static_cast<LONG>(x + (wide ? 2U : 1U)) * cellWidth_,
                    static_cast<LONG>(y + 1U) * cellHeight_};
                const HFONT desiredFont = cell.style.underline
                    ? underlineFont_
                    : font_;
                if (desiredFont != selectedFont) {
                    SelectObject(buffer, desiredFont);
                    selectedFont = desiredFont;
                }
                SetTextColor(buffer, NativeColor(cell.style.foreground));
                SetBkColor(
                    buffer,
                    cell.style.hasBackground
                        ? NativeColor(cell.style.background)
                        : RGB(0U, 0U, 0U));
                int textLength{};
                const std::array<wchar_t, 2U> text = Utf16(
                    cell.codePoint,
                    textLength);
                ExtTextOutW(
                    buffer,
                    cellBounds.left,
                    cellBounds.top,
                    ETO_CLIPPED | ETO_OPAQUE,
                    &cellBounds,
                    text.data(),
                    static_cast<UINT>(textLength),
                    nullptr);
            }
        }
    }
    BitBlt(
        context,
        paint.rcPaint.left,
        paint.rcPaint.top,
        paint.rcPaint.right - paint.rcPaint.left,
        paint.rcPaint.bottom - paint.rcPaint.top,
        buffer,
        paint.rcPaint.left,
        paint.rcPaint.top,
        SRCCOPY);
    SelectObject(buffer, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

void WindowsTuiWindow::QueueEvent(ui::tui::KeyEvent event) noexcept
{
    try {
        pendingEvents_.push_back(event);
    } catch (...) {
        running_ = false;
    }
}

void WindowsTuiWindow::QueueText(
    const wchar_t* text,
    std::size_t length) noexcept
{
    wchar_t highSurrogate{};
    for (std::size_t index = 0U; index < length; ++index) {
        const wchar_t character = text[index];
        if (character == L'\r' || character == L'\n') {
            if (character == L'\r' && index + 1U < length
                && text[index + 1U] == L'\n') {
                ++index;
            }
            QueueEvent({ui::tui::Key::Enter, 0U, false});
            continue;
        }
        if (character == L'\t') {
            QueueEvent({ui::tui::Key::Tab, 0U, false});
            continue;
        }
        const char32_t codePoint = DecodeUtf16CodeUnit(
            character,
            highSurrogate);
        if (codePoint >= 0x20U) {
            QueueEvent({ui::tui::Key::Character, codePoint, false});
        }
    }
}

void WindowsTuiWindow::PasteClipboard() noexcept
{
    if (OpenClipboard(window_) == FALSE) {
        return;
    }
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const auto* text = data == nullptr
        ? nullptr
        : static_cast<const wchar_t*>(GlobalLock(data));
    if (text != nullptr) {
        QueueText(text, std::char_traits<wchar_t>::length(text));
        GlobalUnlock(data);
    }
    CloseClipboard();
}

void WindowsTuiWindow::AcceptDroppedFiles(HDROP drop) noexcept
{
    const UINT count = DragQueryFileW(drop, 0xffffffffU, nullptr, 0U);
    for (UINT index = 0U; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0U);
        try {
            std::vector<wchar_t> path(static_cast<std::size_t>(length) + 1U);
            if (DragQueryFileW(
                    drop,
                    index,
                    path.data(),
                    static_cast<UINT>(path.size())) == 0U) {
                continue;
            }
            if (index != 0U) {
                constexpr wchar_t separator[] = L" ";
                QueueText(separator, 1U);
            }
            const std::span<const wchar_t> value(path.data(), length);
            const bool quoted = std::find_if(
                value.begin(),
                value.end(),
                [](wchar_t character) {
                    return character == L' ' || character == L'\t';
                }) != value.end();
            if (quoted) {
                constexpr wchar_t quote[] = L"\"";
                QueueText(quote, 1U);
            }
            QueueText(path.data(), length);
            if (quoted) {
                constexpr wchar_t quote[] = L"\"";
                QueueText(quote, 1U);
            }
        } catch (...) {
            running_ = false;
            break;
        }
    }
    DragFinish(drop);
}

} // namespace inputweaver::win32
