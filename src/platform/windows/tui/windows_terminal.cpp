#include "windows_terminal.hpp"

#include "platform/windows/support/text_encoding.hpp"
#include "ui/tui/support/color_scheme.hpp"
#include "ui/tui/support/text_layout.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>

namespace inputweaver::win32 {
namespace {

[[nodiscard]] bool SameStyle(
    const ui::tui::TextStyle& left,
    const ui::tui::TextStyle& right) noexcept
{
    return left.foreground == right.foreground
        && left.background == right.background
        && left.hasBackground == right.hasBackground
        && left.underline == right.underline;
}

void AppendStyle(std::string& output, const ui::tui::TextStyle& style)
{
    output += "\x1b[0;38;2;" + std::to_string(style.foreground.red) + ';'
        + std::to_string(style.foreground.green) + ';'
        + std::to_string(style.foreground.blue) + 'm';
    if (style.hasBackground) {
        output += "\x1b[48;2;" + std::to_string(style.background.red) + ';'
            + std::to_string(style.background.green) + ';'
            + std::to_string(style.background.blue) + 'm';
    }
    if (style.underline) {
        output += "\x1b[4m";
    }
}

[[nodiscard]] bool WriteAll(HANDLE output, std::string_view bytes) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD requested = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written{};
        if (WriteFile(
                output,
                bytes.data() + offset,
                requested,
                &written,
                nullptr) == FALSE
            || written == 0U) {
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] std::optional<ui::tui::Key> VirtualKey(WORD key) noexcept
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

[[nodiscard]] std::optional<ui::tui::Key> ControlKey(WORD key) noexcept
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

} // namespace

WindowsTerminal::~WindowsTerminal()
{
    Restore();
}

bool WindowsTerminal::Initialize(std::string& error)
{
    input_ = GetStdHandle(STD_INPUT_HANDLE);
    output_ = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input_ == INVALID_HANDLE_VALUE || output_ == INVALID_HANDLE_VALUE
        || GetConsoleMode(input_, &originalInputMode_) == FALSE
        || GetConsoleMode(output_, &originalOutputMode_) == FALSE) {
        error = "InputWeaverTUI requires an interactive Windows terminal.";
        return false;
    }
    originalInputCodePage_ = GetConsoleCP();
    originalOutputCodePage_ = GetConsoleOutputCP();
    const DWORD inputMode = (originalInputMode_
            | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT)
        & ~static_cast<DWORD>(
            ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT
            | ENABLE_QUICK_EDIT_MODE);
    const DWORD outputMode = originalOutputMode_
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | DISABLE_NEWLINE_AUTO_RETURN;
    if (SetConsoleMode(input_, inputMode) == FALSE
        || SetConsoleMode(output_, outputMode) == FALSE
        || SetConsoleCP(CP_UTF8) == FALSE
        || SetConsoleOutputCP(CP_UTF8) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        Restore();
        return false;
    }
    initialized_ = true;
    if (!WriteAll(output_, "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H")) {
        error = "Cannot initialize terminal drawing.";
        Restore();
        return false;
    }
    return true;
}

std::pair<std::size_t, std::size_t> WindowsTerminal::Size() const noexcept
{
    CONSOLE_SCREEN_BUFFER_INFO information{};
    if (output_ == INVALID_HANDLE_VALUE
        || GetConsoleScreenBufferInfo(output_, &information) == FALSE) {
        return {80U, 24U};
    }
    const int width = information.srWindow.Right
        - information.srWindow.Left + 1;
    const int height = information.srWindow.Bottom
        - information.srWindow.Top + 1;
    return {
        width > 0 ? static_cast<std::size_t>(width) : 80U,
        height > 0 ? static_cast<std::size_t>(height) : 24U};
}

bool WindowsTerminal::Poll(
    ui::tui::KeyEvent& event,
    DWORD timeoutMilliseconds,
    bool& resized) noexcept
{
    resized = false;
    if (!initialized_
        || WaitForSingleObject(input_, timeoutMilliseconds) != WAIT_OBJECT_0) {
        return false;
    }
    for (;;) {
        INPUT_RECORD record{};
        DWORD read{};
        if (ReadConsoleInputW(input_, &record, 1U, &read) == FALSE
            || read == 0U) {
            return false;
        }
        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            resized = true;
            return false;
        }
        if (record.EventType != KEY_EVENT
            || record.Event.KeyEvent.bKeyDown == FALSE) {
            if (WaitForSingleObject(input_, 0U) != WAIT_OBJECT_0) {
                return false;
            }
            continue;
        }
        const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
        const bool shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0U;
        const bool control = (key.dwControlKeyState
            & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0U;
        if (control) {
            const auto command = ControlKey(key.wVirtualKeyCode);
            if (command.has_value()) {
                pendingHighSurrogate_ = 0;
                event = {*command, 0U, false};
                return true;
            }
        }
        if (const auto mapped = VirtualKey(key.wVirtualKeyCode); mapped.has_value()) {
            pendingHighSurrogate_ = 0;
            event = {*mapped, 0U, shift};
            return true;
        }
        const wchar_t character = key.uChar.UnicodeChar;
        if (character >= 0xd800 && character <= 0xdbff) {
            pendingHighSurrogate_ = character;
            continue;
        }
        char32_t codePoint{};
        if (character >= 0xdc00 && character <= 0xdfff
            && pendingHighSurrogate_ != 0) {
            codePoint = 0x10000U
                + ((static_cast<char32_t>(pendingHighSurrogate_) - 0xd800U)
                    << 10U)
                + (static_cast<char32_t>(character) - 0xdc00U);
        } else {
            codePoint = static_cast<char32_t>(character);
        }
        pendingHighSurrogate_ = 0;
        if (codePoint >= 0x20U) {
            event = {ui::tui::Key::Character, codePoint, shift};
            return true;
        }
    }
}

bool WindowsTerminal::CopyText(
    std::string_view text,
    std::string& error) noexcept
{
    try {
        std::wstring wide;
        if (!Utf8ToWide(text, wide)) {
            error = "Cannot encode the selected text for the clipboard.";
            return false;
        }
        if (OpenClipboard(nullptr) == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        if (EmptyClipboard() == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            CloseClipboard();
            return false;
        }
        const SIZE_T bytes = (wide.size() + 1U) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        void* destination = memory == nullptr ? nullptr : GlobalLock(memory);
        if (destination == nullptr) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            if (memory != nullptr) {
                GlobalFree(memory);
            }
            CloseClipboard();
            return false;
        }
        std::memcpy(destination, wide.c_str(), bytes);
        GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    } catch (...) {
        error = "Cannot copy the selected text to the clipboard.";
        return false;
    }
}

bool WindowsTerminal::Draw(
    const ui::tui::Canvas& canvas,
    std::string& error) noexcept
{
    try {
        std::string output = "\x1b[H";
        const auto cells = canvas.Cells();
        ui::tui::TextStyle current{};
        bool hasStyle = false;
        for (std::size_t y = 0U; y < canvas.Height(); ++y) {
            for (std::size_t x = 0U; x < canvas.Width(); ++x) {
                const ui::tui::Cell& cell = cells[y * canvas.Width() + x];
                if (cell.continuation) {
                    continue;
                }
                if (!hasStyle || !SameStyle(current, cell.style)) {
                    AppendStyle(output, cell.style);
                    current = cell.style;
                    hasStyle = true;
                }
                output += ui::tui::EncodeUtf8(cell.codePoint);
            }
            output += "\x1b[0m";
            hasStyle = false;
            if (y + 1U < canvas.Height()) {
                output += "\r\n";
            }
        }
        if (!WriteAll(output_, output)) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void WindowsTerminal::Restore() noexcept
{
    if (initialized_) {
        (void)WriteAll(output_, "\x1b[0m\x1b[?25h\x1b[?1049l");
    }
    if (input_ != INVALID_HANDLE_VALUE && originalInputMode_ != 0U) {
        (void)SetConsoleMode(input_, originalInputMode_);
    }
    if (output_ != INVALID_HANDLE_VALUE && originalOutputMode_ != 0U) {
        (void)SetConsoleMode(output_, originalOutputMode_);
    }
    if (originalInputCodePage_ != 0U) {
        (void)SetConsoleCP(originalInputCodePage_);
    }
    if (originalOutputCodePage_ != 0U) {
        (void)SetConsoleOutputCP(originalOutputCodePage_);
    }
    initialized_ = false;
}

bool LoadTuiColorScheme(
    const std::filesystem::path& executableDirectory,
    ui::tui::ColorScheme& colors,
    std::string& error)
{
    const std::filesystem::path path = executableDirectory
        / L"res" / L"InputWeaverTUI.colors.json";
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Cannot open color scheme: " + path.string() + ".";
            return false;
        }
        std::ostringstream bytes;
        bytes << input.rdbuf();
        std::string parseError;
        if (!ui::tui::ParseColorScheme(bytes.str(), colors, parseError)) {
            error = "Invalid color scheme " + path.string() + ": "
                + parseError;
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = "Cannot load color scheme " + path.string() + ": "
            + exception.what();
        return false;
    }
}

} // namespace inputweaver::win32
