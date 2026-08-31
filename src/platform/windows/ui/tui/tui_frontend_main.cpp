#include "platform/windows/support/unique_handle.hpp"
#include "platform/windows/ui/tui/tui_ipc.hpp"
#include "platform/windows/ui/tui/windows_tui_window.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <shellapi.h>

namespace {

[[nodiscard]] bool ParseHandle(
    std::wstring_view text,
    HANDLE& handle) noexcept
{
    if (text.empty()) {
        return false;
    }
    std::wstring value{text};
    wchar_t* end{};
    errno = 0;
    const unsigned long long parsed = std::wcstoull(
        value.c_str(),
        &end,
        10);
    if (errno != 0 || end == value.c_str() || *end != L'\0'
        || parsed > (std::numeric_limits<std::uintptr_t>::max)()) {
        return false;
    }
    handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(parsed));
    DWORD flags{};
    return handle != nullptr && handle != INVALID_HANDLE_VALUE
        && GetHandleInformation(handle, &flags) != FALSE;
}

[[nodiscard]] bool ParseArguments(
    int argumentCount,
    wchar_t** arguments,
    HANDLE& readHandle,
    HANDLE& writeHandle) noexcept
{
    if (argumentCount != 5) {
        return false;
    }
    for (int index = 1; index + 1 < argumentCount; index += 2) {
        const std::wstring_view option{arguments[index]};
        HANDLE parsed{};
        if (!ParseHandle(arguments[index + 1], parsed)) {
            return false;
        }
        if (option == L"--read-handle" && readHandle == nullptr) {
            readHandle = parsed;
        } else if (option == L"--write-handle" && writeHandle == nullptr) {
            writeHandle = parsed;
        } else {
            return false;
        }
    }
    return readHandle != nullptr && writeHandle != nullptr;
}

[[nodiscard]] bool SendViewport(
    inputweaver::win32::TuiIpcChannel& channel,
    const inputweaver::win32::WindowsTuiWindow& window,
    std::size_t& lastColumns,
    std::size_t& lastRows,
    std::string& error) noexcept
{
    const std::size_t columns = window.Columns();
    const std::size_t rows = window.Rows();
    if (columns == lastColumns && rows == lastRows) {
        return true;
    }
    try {
        const std::vector<std::uint8_t> payload =
            inputweaver::win32::EncodeViewportSize(columns, rows);
        if (!channel.Send(
                inputweaver::win32::TuiIpcMessageType::Resize,
                payload,
                error)) {
            return false;
        }
        lastColumns = columns;
        lastRows = rows;
        return true;
    } catch (...) {
        error = "Cannot encode the TUI viewport size.";
        return false;
    }
}

[[nodiscard]] bool SendWindow(
    inputweaver::win32::TuiIpcChannel& channel,
    HWND window,
    std::string& error) noexcept
{
    try {
        const std::vector<std::uint8_t> payload =
            inputweaver::win32::EncodeWindowHandle(
                reinterpret_cast<std::uintptr_t>(window));
        return channel.Send(
            inputweaver::win32::TuiIpcMessageType::Window,
            payload,
            error);
    } catch (...) {
        error = "Cannot encode the TUI window handle.";
        return false;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    (void)SetProcessDPIAware();
    int argumentCount{};
    wchar_t** arguments = CommandLineToArgvW(
        GetCommandLineW(),
        &argumentCount);
    if (arguments == nullptr) {
        return 2;
    }
    HANDLE readHandle{};
    HANDLE writeHandle{};
    const bool parsed = ParseArguments(
        argumentCount,
        arguments,
        readHandle,
        writeHandle);
    LocalFree(arguments);
    if (!parsed) {
        return 2;
    }

    inputweaver::win32::TuiIpcChannel channel{
        inputweaver::win32::UniqueHandle{readHandle},
        inputweaver::win32::UniqueHandle{writeHandle}};
    inputweaver::win32::WindowsTuiWindow window;
    std::string error;
    if (!window.Initialize(instance, showCommand, error)) {
        return 3;
    }
    std::size_t lastColumns{};
    std::size_t lastRows{};
    if (!SendViewport(
            channel,
            window,
            lastColumns,
            lastRows,
            error)
        || !SendWindow(channel, window.Handle(), error)) {
        return 4;
    }

    ULONGLONG nextHeartbeat = GetTickCount64();
    while (window.Running()) {
        std::vector<inputweaver::win32::TuiIpcMessage> messages;
        bool disconnected{};
        if (!channel.Poll(messages, disconnected, error) || disconnected) {
            return disconnected ? 0 : 5;
        }
        const inputweaver::win32::TuiIpcMessage* lastFrame{};
        for (const inputweaver::win32::TuiIpcMessage& message : messages) {
            if (message.type
                == inputweaver::win32::TuiIpcMessageType::Close) {
                return 0;
            }
            if (message.type
                == inputweaver::win32::TuiIpcMessageType::Frame) {
                lastFrame = &message;
            } else {
                return 6;
            }
        }
        if (lastFrame != nullptr) {
            inputweaver::win32::TuiFrame frame;
            if (!inputweaver::win32::DecodeTuiFrame(
                    lastFrame->payload,
                    frame)) {
                return 7;
            }
            window.Present(std::move(frame));
        }

        std::vector<inputweaver::ui::tui::KeyEvent> events;
        bool backgroundRequested{};
        window.Poll(events, backgroundRequested);
        for (const inputweaver::ui::tui::KeyEvent& event : events) {
            try {
                const std::vector<std::uint8_t> payload =
                    inputweaver::win32::EncodeKeyEvent(event);
                if (!channel.Send(
                        inputweaver::win32::TuiIpcMessageType::Key,
                        payload,
                        error)) {
                    return 8;
                }
            } catch (...) {
                return 8;
            }
        }
        if (backgroundRequested) {
            const std::span<const std::uint8_t> empty;
            (void)channel.Send(
                inputweaver::win32::TuiIpcMessageType::Background,
                empty,
                error);
            return 0;
        }
        if (!window.Running()) {
            break;
        }
        if (!SendViewport(
                channel,
                window,
                lastColumns,
                lastRows,
                error)) {
            return 9;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= nextHeartbeat) {
            const std::span<const std::uint8_t> empty;
            if (!channel.Send(
                    inputweaver::win32::TuiIpcMessageType::Heartbeat,
                    empty,
                    error)) {
                return 10;
            }
            nextHeartbeat = now + 500U;
        }
        (void)MsgWaitForMultipleObjects(
            0U,
            nullptr,
            FALSE,
            20U,
            QS_ALLINPUT);
    }
    return 0;
}
